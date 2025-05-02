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

#include "yb/hnsw/block_writer.h"

#include "yb/util/crc.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;

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
  ConvertField<uint64_t>(config.expansion_search, serializer);
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

void BlockCache::Register(FileBlockCachePtr&& file_block_cache) {
  std::lock_guard guard(mutex_);
  files_.push_back(std::move(file_block_cache));
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
    std::unique_ptr<RandomAccessFile> file, FileBlockCacheBuilder* builder)
    : file_(std::move(file)) {
  if (!builder) {
    return;
  }
  auto& blocks = builder->blocks();
  blocks_.reserve(blocks.size());
  size_t total_size = 0;
  for (auto& block : blocks) {
    total_size += block.size();
    blocks_.emplace_back(BlockInfo {
      .end = total_size,
      .content = std::move(block),
    });
  }
  blocks.clear();
}

FileBlockCache::~FileBlockCache() = default;

Result<Header> FileBlockCache::Load() {
  DCHECK(blocks_.empty());

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
  blocks_.resize(reader.Left() / sizeof(uint64_t));
  size_t prev_end = 0;
  for (size_t i = 0; i < blocks_.size(); i++) {
    blocks_[i].end = reader.Read<uint64_t>();
    blocks_[i].content.resize(blocks_[i].end - prev_end);
    Slice read_result;
    RETURN_NOT_OK(file_->Read(
        prev_end, blocks_[i].content.size(), &read_result, blocks_[i].content.data()));
    RSTATUS_DCHECK_EQ(
        read_result.size(), blocks_[i].content.size(), Corruption,
        Format("Wrong number of read bytes in block $0", i));
    prev_end = blocks_[i].end;
  }
  return header;
}

} // namespace yb::hnsw
