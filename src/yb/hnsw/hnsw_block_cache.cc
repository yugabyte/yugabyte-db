// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/hnsw/hnsw_block_cache.h"

#include <boost/intrusive/list.hpp>

#include "yb/hnsw/block_writer.h"

#include "yb/util/crc.h"
#include "yb/util/metrics.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;

METRIC_DEFINE_counter(server, vector_index_cache_hit, "Vector index block cache hits",
                      yb::MetricUnit::kCacheHits,
                      "Number of hits of vector index block cache");

METRIC_DEFINE_counter(server, vector_index_cache_query, "Vector index block cache query",
                      yb::MetricUnit::kCacheQueries,
                      "Number of queries of vector index block cache");

METRIC_DEFINE_counter(server, vector_index_read, "Vector index block read bytes",
                      yb::MetricUnit::kBytes,
                      "Number of bytes read by vector index block cache");

METRIC_DEFINE_counter(server, vector_index_cache_add, "Vector index block bytes added to cache",
                      yb::MetricUnit::kBytes,
                      "Number of bytes added to vector index block cache");

METRIC_DEFINE_counter(server, vector_index_cache_evict,
                      "Vector index block bytes evicted from cache",
                      yb::MetricUnit::kBytes,
                      "Number of bytes evicted from vector index block cache");

METRIC_DEFINE_counter(server, vector_index_cache_remove,
                      "Vector index block bytes removed from cache",
                      yb::MetricUnit::kBytes,
                      "Number of bytes removed from vector index block cache");

namespace yb::hnsw {

namespace {

template <class Type, class Value, class Writer>
concept HasAppend = requires(Writer& writer) {
  writer.template Append<Type>(std::declval<Value>());
};

template <class Type, class Reader>
concept HasRead = requires(Reader& reader) {
  reader.template Read<Type>();
};

template <class Type, class Value, class Writer>
requires(HasAppend<Type, Value, Writer>)
void ConvertField(const Value& value, Writer& writer) {
  writer.template Append<Type>(value);
}

template <class Converter, class Type>
using RefForConverter = std::conditional_t<HasRead<uint64_t, Converter>, Type, const Type>&;

template <class Converter>
void Convert(size_t version, RefForConverter<Converter, LayerInfo> entry, Converter& serializer) {
  ConvertField<uint64_t>(entry.size, serializer);
  ConvertField<uint64_t>(entry.block, serializer);
  ConvertField<uint64_t>(entry.last_block_index, serializer);
  ConvertField<uint64_t>(entry.last_block_vectors_amount, serializer);
}

template <class Vector, class Writer>
requires(HasAppend<uint64_t, uint64_t, Writer>)
void ConvertVector(size_t version, const Vector& vector, Writer& writer) {
  writer.template Append<uint64_t>(vector.size());
  for (const auto& entry : vector) {
    Convert(version, entry, writer);
  }
}

template <class Type, class Value, class Reader>
requires(HasRead<Type, Reader>)
void ConvertField(Value& value, Reader& reader) {
  value = reader.template Read<Type>();
}

template <class Vector, class Reader>
requires(HasRead<uint64_t, Reader>)
void ConvertVector(size_t version, Vector& vector, Reader& reader) {
  auto size = reader.template Read<uint64_t>();
  vector.resize(size);
  for (size_t i = 0; i < size; ++i) {
    Convert(version, vector[i], reader);
  }
}

template <class Converter>
void Convert(size_t version, RefForConverter<Converter, Config> config, Converter& serializer) {
  ConvertField<uint64_t>(config.connectivity, serializer);
  ConvertField<uint64_t>(config.connectivity_base, serializer);
}

template <class Converter>
void Convert(size_t version, RefForConverter<Converter, Header> header, Converter& serializer) {
  ConvertField<uint64_t>(header.dimensions, serializer);
  ConvertField<uint64_t>(header.vector_data_size, serializer);
  ConvertField<VectorNo>(header.entry, serializer);
  ConvertField<uint64_t>(header.max_level, serializer);
  Convert(version, header.config, serializer);
  ConvertField<uint64_t>(header.max_block_size, serializer);
  ConvertField<uint64_t>(header.max_vectors_per_non_base_block, serializer);
  ConvertField<uint64_t>(header.vector_data_block, serializer);
  ConvertField<uint64_t>(header.vector_data_amount_per_block, serializer);
  ConvertVector(version, header.layers, serializer);
}

class WriteCounter {
 public:
  size_t value() const {
    return value_;
  }

  template <class Value>
  void Append(Value value) {
    value_ += sizeof(Value);
  }

 private:
  size_t value_ = 0;
};

constexpr size_t kSerializationVersion = 1;

template <class Out, class... Args>
void Serialize(const Out& out, Args&&... args) {
  Convert(kSerializationVersion, out, std::forward<Args&&>(args)...);
}

size_t SerializedSize(const Header& header) {
  WriteCounter counter;
  Convert(kSerializationVersion, header, counter);
  return counter.value();
}

class SliceReader {
 public:
  explicit SliceReader(Slice input) : input_(input) {}

  size_t Left() const {
    return input_.size();
  }

  template <class Value>
  Value Read() {
    Value result;
    memcpy(&result, input_.data(), sizeof(result));
    input_.RemovePrefix(sizeof(Value));
    return result;
  }
 private:
  Slice input_;
};

template <class Out, class... Args>
void Deserialize(size_t version, Out& out, Args&&... args) {
  Convert(version, out, std::forward<Args&&>(args)...);
}

} // namespace

BlockCacheMetrics::BlockCacheMetrics(const MetricEntityPtr& entity)
    : hit(METRIC_vector_index_cache_hit.Instantiate(entity)),
      query(METRIC_vector_index_cache_query.Instantiate(entity)),
      read(METRIC_vector_index_read.Instantiate(entity)),
      add(METRIC_vector_index_cache_add.Instantiate(entity)),
      evict(METRIC_vector_index_cache_evict.Instantiate(entity)),
      remove(METRIC_vector_index_cache_remove.Instantiate(entity)) {
}

struct CachedBlock : boost::intrusive::list_base_hook<> {
  BlockCacheShard* shard;
  size_t end;
  size_t size;
  // Guarded by BlockCacheShard mutex.
  int64_t use_count;
  std::atomic<const std::byte*> data{nullptr};
  std::mutex load_mutex;
  DataBlock content GUARDED_BY(load_mutex);

  Result<const std::byte*> Load(
      RandomAccessFile& file, MemTracker& mem_tracker, BlockCacheMetrics& metrics) {
    std::lock_guard lock(load_mutex);
    auto result = data.load(std::memory_order_acquire);
    if (result) {
      return result;
    }
    if (!content.empty()) {
      // It could happen that block is in the process of eviction, in this case we could just
      // restore data pointing to content instead of reloading it
      data.store(result = content.data(), std::memory_order_release);
      return result;
    }
    mem_tracker.Consume(size);
    content.resize(size);
    Slice read_result;
    RETURN_NOT_OK(file.Read(end - size, size, &read_result, content.data()));
    RSTATUS_DCHECK_EQ(
        read_result.size(), content.size(), Corruption,
        Format("Wrong number of read bytes in block $0 - $1", end - size, size));
    metrics.read->IncrementBy(read_result.size());
    data.store(result = content.data(), std::memory_order_release);
    return result;
  }

  void Unload(MemTracker& mem_tracker) {
    std::lock_guard lock(load_mutex);
    if (data.load(std::memory_order_acquire)) {
      // The Load was called during eviction, no need to unload data.
      return;
    }
    content = {};
    mem_tracker.Release(size);
  }
};

class alignas(CACHELINE_SIZE) BlockCacheShard {
 public:
  void Init(size_t capacity, BlockCache& block_cache) {
    capacity_ = capacity;
    block_cache_ = &block_cache;
  }

  Result<const std::byte*> Take(CachedBlock& block, RandomAccessFile& file) {
    block_cache_->metrics().query->Increment();
    {
      std::lock_guard lock(mutex_);
      ++block.use_count;
      // Remove block from LRU while it is used.
      if (block.is_linked()) {
        DCHECK_EQ(block.use_count, 1);
        RemoveBlockFromLRU(block);
      }

      auto data = block.data.load(std::memory_order_acquire);
      if (data) {
        block_cache_->metrics().hit->Increment();
        return data;
      }
    }
    return VERIFY_RESULT(
        block.Load(file, *block_cache_->mem_tracker(), block_cache_->metrics()));
  }

  void Release(CachedBlock& block) {
    boost::container::small_vector<CachedBlock*, 10> evicted_blocks;
    {
      std::lock_guard lock(mutex_);
      DCHECK(!block.is_linked());
      if (--block.use_count != 0) {
        return;
      }
      Evict(block.size, evicted_blocks);
      block_cache_->metrics().add->IncrementBy(block.size);
      consumption_ += block.size;
      lru_.push_back(block);
    }
    size_t evicted_bytes = 0;
    for (auto* evicted_block : evicted_blocks) {
      evicted_block->Unload(*block_cache_->mem_tracker());
      evicted_bytes += evicted_block->size;
    }
    if (evicted_bytes) {
      block_cache_->metrics().evict->IncrementBy(evicted_bytes);
    }
  }

  void Remove(CachedBlock& block) {
    std::lock_guard lock(mutex_);
    DCHECK_EQ(block.use_count, 0);
    if (block.is_linked()) {
      RemoveBlockFromLRU(block);
    }
  }

 private:
  using Blocks = boost::intrusive::list<CachedBlock>;

  void RemoveBlockFromLRU(CachedBlock& block) REQUIRES(mutex_) {
    block_cache_->metrics().remove->IncrementBy(block.size);
    lru_.erase(lru_.iterator_to(block));
    consumption_ -= block.size;
  }

  void Evict(
      size_t space_required,
      boost::container::small_vector_base<CachedBlock*>& evicted_blocks) REQUIRES(mutex_) {
    space_required = std::min(space_required, capacity_);
    auto it = lru_.begin();
    while (consumption_ + space_required > capacity_ && it != lru_.end()) {
      auto& block = *it;
      ++it;
      block.data.store(nullptr, std::memory_order_release);
      lru_.erase(lru_.iterator_to(block));
      consumption_ -= block.size;
      evicted_blocks.push_back(&block);
    }
  }

  size_t capacity_ = 0;
  BlockCache* block_cache_ = nullptr;
  std::mutex mutex_;
  Blocks lru_ GUARDED_BY(mutex_);
  size_t consumption_ GUARDED_BY(mutex_) = 0;
};

BlockCache::BlockCache(
    Env& env, const MemTrackerPtr& mem_tracker, const MetricEntityPtr& metric_entity,
    size_t capacity, size_t num_shard_bits)
    : env_(env),
      mem_tracker_(mem_tracker),
      metrics_(std::make_unique<BlockCacheMetrics>(metric_entity)),
      shards_mask_((1ULL << num_shard_bits) - 1),
      shards_(std::make_unique<BlockCacheShard[]>(shards_mask_ + 1)) {
  for (size_t i = 0; i <= shards_mask_; ++i) {
    shards_[i].Init(capacity >> num_shard_bits, *this);
  }
}

BlockCache::~BlockCache() = default;

BlockCacheShard& BlockCache::NextShard() {
  return shards_[(next_shard_++) & shards_mask_];
}

DataBlock FileBlockCacheBuilder::MakeFooter(const Header& header) const {
  DataBlock buffer(
    sizeof(uint8_t) +
    SerializedSize(header) + sizeof(uint64_t) * blocks_.size() +
    sizeof(uint32_t) + // CRC32
    sizeof(uint64_t)); // Size
  BlockWriter writer(buffer);
  writer.Append<uint8_t>(kSerializationVersion);
  Serialize(header, writer);
  uint64_t sum = 0;
  for (const auto& block : blocks_) {
    sum += block.size();
    writer.Append(sum);
  }
  writer.Append(crc::Crc32c(buffer.data(), buffer.size() - writer.SpaceLeft()));
  writer.Append<uint64_t>(buffer.size());
  DCHECK_EQ(writer.SpaceLeft(), 0);
  return buffer;
}

FileBlockCache::FileBlockCache(
    BlockCache& block_cache, std::unique_ptr<RandomAccessFile> file,
    FileBlockCacheBuilder* builder)
    : block_cache_(block_cache), file_(std::move(file)) {
  if (!builder) {
    return;
  }
  auto& blocks = builder->blocks();
  size_t total_size = 0;
  size_t index = 0;
  AllocateBlocks(blocks.size());
  for (auto& data : blocks) {
    total_size += data.size();
    auto& block = blocks_[index];
    block.shard = &block_cache_.NextShard();
    block.end = total_size;
    block.size = data.size();
    block.content = std::move(data);
    block_cache.mem_tracker()->Consume(block.size);
    block.use_count = 1;
    block.shard->Release(block);
    ++index;
  }
  blocks.clear();
}

FileBlockCache::~FileBlockCache() {
  for (size_t i = 0; i != size_; ++i) {
    blocks_[i].shard->Remove(blocks_[i]);
  }
}

void FileBlockCache::AllocateBlocks(size_t size) {
  size_ = size;
  blocks_.reset(new CachedBlock[size]);
}

Result<Header> FileBlockCache::Load() {
  DCHECK_EQ(blocks_, nullptr);

  using FooterSizeType = uint64_t;
  auto file_size = VERIFY_RESULT(file_->Size());
  RSTATUS_DCHECK_GE(file_size, sizeof(FooterSizeType), Corruption, "Wrong file size");
  std::vector<std::byte> buffer(std::min<size_t>(1_KB, file_size));
  Slice footer_data;
  RETURN_NOT_OK(file_->Read(file_size - buffer.size(), buffer.size(), &footer_data, buffer.data()));
  RSTATUS_DCHECK_GE(
      footer_data.size(), sizeof(FooterSizeType), Corruption, "Wrong number of read bytes");
  auto footer_size = LittleEndian::Load64(footer_data.end() - sizeof(FooterSizeType));
  RSTATUS_DCHECK_GE(file_size, footer_size, Corruption, "Footer size greater than file size");
  if (footer_size > buffer.size()) {
    buffer.resize(footer_size);
    RETURN_NOT_OK(file_->Read(
        file_size - buffer.size(), buffer.size(), &footer_data, buffer.data()));
    RSTATUS_DCHECK_EQ(
        footer_data.size(), footer_size, Corruption, "Wrong number of read footer bytes");
  }
  footer_data = footer_data.Suffix(footer_size)
                           .WithoutSuffix(sizeof(FooterSizeType) + sizeof(uint32_t));
  auto expected_crc = crc::Crc32c(footer_data.data(), footer_data.size());
  auto found_crc = LittleEndian::Load32(footer_data.end());
  RSTATUS_DCHECK_EQ(expected_crc, found_crc, Corruption, "Wrong footer CRC");
  Header header;
  SliceReader reader(footer_data);
  size_t version = reader.Read<uint8_t>();
  Deserialize(version, header, reader);
  AllocateBlocks(reader.Left() / sizeof(uint64_t));
  size_t prev_end = 0;
  for (size_t i = 0; i < size_; i++) {
    blocks_[i].shard = &block_cache_.NextShard();
    blocks_[i].end = reader.Read<uint64_t>();
    blocks_[i].size = blocks_[i].end - prev_end;
    blocks_[i].use_count = 0;
    prev_end = blocks_[i].end;
  }
  return header;
}

Result<const std::byte*> FileBlockCache::Take(size_t index) {
  auto& block = blocks_[index];
  return block.shard->Take(block, *file_);
}

void FileBlockCache::Release(size_t index) {
  auto& block = blocks_[index];
  block.shard->Release(block);
}

} // namespace yb::hnsw
