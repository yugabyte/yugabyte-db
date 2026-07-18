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
#include "yb/rocksdb/cache.h"

#include "yb/util/crc.h"
#include "yb/util/metrics.h"
#include "yb/util/size_literals.h"
#include "yb/util/unique_lock.h"

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

METRIC_DEFINE_event_stats(server, vector_index_cache_read_us,
                          "Time to read vector index chunk", yb::MetricUnit::kMicroseconds,
                          "Time (microseconds) that was spent reading index chunk.");

METRIC_DEFINE_event_stats(server, vector_index_cache_take_wait_us,
                          "Time to take wait vector index chunk", yb::MetricUnit::kMicroseconds,
                          "Time (microseconds) that was spent waiting while taking index chunk.");

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
      remove(METRIC_vector_index_cache_remove.Instantiate(entity)),
      read_us(METRIC_vector_index_cache_read_us.Instantiate(entity)),
      take_wait_us(METRIC_vector_index_cache_read_us.Instantiate(entity)) {
}

struct CachedBlock {
  BlockCache* cache = nullptr;
  Uuid block_id;
  size_t size;
  size_t end;
  ScopedTrackedConsumption scoped_consumption;

  std::mutex mutex;
  size_t use_count GUARDED_BY(mutex) = 0;
  BlockCacheHandle handle GUARDED_BY(mutex);
  DataBlock content GUARDED_BY(mutex);
  std::shared_future<Result<const std::byte*>> load_future GUARDED_BY(mutex);

  void Init(BlockCache& cache_, size_t block_size, size_t block_end) {
    cache = &cache_;
    block_id = Uuid::Generate();
    size = block_size;
    end = block_end;
    scoped_consumption = ScopedTrackedConsumption(cache->mem_tracker(), sizeof(*this));
  }

  void Init(BlockCache& cache_, DataBlock&& context_data, size_t block_end) {
    Init(cache_, context_data.size, block_end);

    DCHECK_GT(context_data.size, 0);
    bool has_data;
    {
      std::lock_guard lock(mutex);
      content = std::move(context_data);
      has_data = content.data != nullptr;
    }
    if (has_data) {
      cache->Insert(*this, false);
    }
  }

  Result<const std::byte*> Take(RandomAccessFile& file) {
    cache->metrics().query->Increment();

    UniqueLock lock(mutex);
    ++use_count;
    if (content.data) {
      cache->metrics().hit->Increment();
      auto result = content.data.get();
      auto need_handle = !handle && use_count == 1;
      lock.unlock();
      if (need_handle) {
        auto new_handle = cache->Lookup(*this);
        if (new_handle) {
          lock.lock();
          DCHECK(!handle);
          handle = new_handle;
        }
      }
      return result;
    }
    if (!load_future.valid()) {
      std::promise<Result<const std::byte*>> promise;
      load_future = promise.get_future();
      lock.unlock();
      auto result = Load(file);
      promise.set_value(result);
      return result;
    }

    auto future = load_future;
    lock.unlock();

    auto start = MonoTime::Now();
    auto result = future.get();
    cache->metrics().take_wait_us->Increment((MonoTime::Now() - start).ToMicroseconds());
    return result;
  }

  void Release() {
    BlockCacheHandle release_handle;
    {
      std::lock_guard lock(mutex);
      if (--use_count != 0) {
        return;
      }
      release_handle = std::exchange(handle, {});
    }
    if (release_handle) {
      cache->metrics().add->IncrementBy(size);
      cache->Release(release_handle);
    } else {
      cache->metrics().evict->IncrementBy(size);
    }
  }

  void Unload() {
    {
      std::lock_guard lock(mutex);
      handle = {};
      if (use_count != 0) {
        return;
      }
      content.data.reset();
      load_future = {};
    }
    cache->metrics().evict->IncrementBy(size);
  }

  static void Delete(const Slice& key, void* value) {
    static_cast<CachedBlock*>(value)->Unload();
  }

 private:
  Result<const std::byte*> Load(RandomAccessFile& file) {
    auto start = MonoTime::Now();
    std::unique_ptr<std::byte[]> new_content(new std::byte[size]);
    Slice read_result;
    RETURN_NOT_OK(file.Read(end - size, size, &read_result, new_content.get()));
    RSTATUS_DCHECK_EQ(
        read_result.size(), size, Corruption,
        Format("Wrong number of read bytes in block $0 - $1", end - size, size));

    cache->metrics().read_us->Increment((MonoTime::Now() - start).ToMicroseconds());
    cache->metrics().read->IncrementBy(size);
    auto new_handle = cache->Insert(*this, true);
    cache->metrics().remove->IncrementBy(size);

    std::lock_guard lock(mutex);
    DCHECK_GT(use_count, 0);
    handle = new_handle;
    content.size = size;
    content.data = std::move(new_content);
    return content.data.get();
  }
};

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
    sum += block.size;
    writer.Append(sum);
  }
  writer.Append(crc::Crc32c(buffer.data.get(), buffer.size - writer.SpaceLeft()));
  writer.Append<uint64_t>(buffer.size);
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
    total_size += data.size;
    blocks_[index].Init(block_cache_, std::move(data), total_size);
    ++index;
  }
  blocks.clear();
}

FileBlockCache::~FileBlockCache() {
  for (size_t i = 0; i != size_; ++i) {
    block_cache_.Erase(blocks_[i]);
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
    auto new_end = reader.Read<uint64_t>();
    blocks_[i].Init(block_cache_, new_end - prev_end, new_end);
    prev_end = new_end;
  }
  return header;
}

Result<const std::byte*> FileBlockCache::Take(size_t index) {
  return blocks_[index].Take(*file_);
}

void FileBlockCache::Release(size_t index) {
  blocks_[index].Release();
}

BlockCache::BlockCache(
    Env& env, const MemTrackerPtr& mem_tracker, const MetricEntityPtr& metric_entity,
    rocksdb::Cache& block_cache)
    : env_(env),
      mem_tracker_(MemTracker::FindOrCreateTracker("vector_index_block_cache", mem_tracker)),
      metadata_mem_tracker_(MemTracker::FindOrCreateTracker("metadata", mem_tracker_)),
      metrics_(std::make_unique<BlockCacheMetrics>(metric_entity)),
      block_cache_(block_cache) {
}

BlockCache::~BlockCache() = default;

BlockCacheHandle BlockCache::Insert(CachedBlock& block, bool retain) {
  rocksdb::Cache::Handle* result;
  auto status = block_cache_.Insert(
      block.block_id.AsSlice(), rocksdb::kInMultiTouchId, &block, block.size, &CachedBlock::Delete,
      retain ? &result : nullptr);
  if (!status.ok()) {
    LOG_WITH_FUNC(WARNING) << "Failure: " << status;
    return BlockCacheHandle{};
  }
  return BlockCacheHandle(result);
}

void BlockCache::Release(BlockCacheHandle handle) {
  block_cache_.Release(static_cast<rocksdb::Cache::Handle*>(handle.handle_));
}

BlockCacheHandle BlockCache::Lookup(CachedBlock& block) {
  return BlockCacheHandle(block_cache_.Lookup(block.block_id.AsSlice(), rocksdb::kInMultiTouchId));
}

void BlockCache::Erase(CachedBlock& block) {
  block_cache_.Erase(block.block_id.AsSlice());
}

} // namespace yb::hnsw
