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

#include "yb/vector_index/vector_payload_map.h"

#include "yb/gutil/casts.h"

#include "yb/util/crc.h"
#include "yb/util/endian_util.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb::vector_index {

namespace {

constexpr char kPayloadFileSuffix[] = ".payload";
constexpr uint8_t kSerializationVersion = 1;
constexpr size_t kCrcSize = sizeof(uint32_t);
constexpr size_t kPayloadSizePrefix = sizeof(uint32_t);
constexpr size_t kHeaderSize = 1 + sizeof(uint64_t);

} // namespace

std::string VectorIndexPayloadFilePath(const std::string& index_path) {
  return index_path + kPayloadFileSuffix;
}

void VectorPayloadMap::Reserve(size_t capacity) {
  payloads_.resize(capacity);
}

void VectorPayloadMap::Insert(size_t slot, Slice payload) {
  DCHECK(!payload.empty());
  DCHECK_LT(slot, payloads_.size());
  auto* buf = static_cast<uint8_t*>(arena_.AllocateBytes(kPayloadSizePrefix + payload.size()));
  Store<uint32_t, LittleEndian>(buf, narrow_cast<uint32_t>(payload.size()));
  memcpy(buf + kPayloadSizePrefix, payload.data(), payload.size());
  payloads_[slot] = Slice(buf + kPayloadSizePrefix, payload.size());
}

Result<Slice> VectorPayloadMap::Get(size_t slot) const {
  if (slot >= payloads_.size() || payloads_[slot].empty()) {
    // Every slot of the chunk has a payload, see the class comment.
    return STATUS_FORMAT(Corruption, "Slot $0 is missing in the payload map", slot);
  }
  return payloads_[slot];
}

Status VectorPayloadMap::SaveToFile(const std::string& index_path, size_t num_payloads) const {
  DCHECK_LE(num_payloads, payloads_.size());
  std::vector<Slice> slices;
  slices.reserve(num_payloads + 2);
  uint8_t header[kHeaderSize];
  header[0] = kSerializationVersion;
  Store<uint64_t, LittleEndian>(header + 1, num_payloads);
  slices.emplace_back(header, sizeof(header));
  for (size_t slot = 0; slot != num_payloads; ++slot) {
    auto payload = payloads_[slot];
    DCHECK(!payload.empty());
    // The size prefix is already stored in front of the payload, see Insert.
    const auto* begin = payload.data() - kPayloadSizePrefix;
    // Consecutive entries are usually adjacent in the arena, extend the previous slice then.
    if (slices.back().end() == begin) {
      slices.back() = Slice(slices.back().data(), payload.end());
    } else {
      slices.emplace_back(begin, payload.end());
    }
  }
  crc::Crc32Accumulator crc;
  for (const auto& slice : slices) {
    crc.Feed(slice);
  }
  uint8_t crc_buf[kCrcSize];
  Store<uint32_t, LittleEndian>(crc_buf, crc.result());
  slices.emplace_back(crc_buf, sizeof(crc_buf));

  auto* env = Env::Default();
  auto path = VectorIndexPayloadFilePath(index_path);
  auto tmp_path = path + ".tmp";
  std::unique_ptr<WritableFile> file;
  RETURN_NOT_OK(env->NewWritableFile(tmp_path, &file));
  RETURN_NOT_OK(file->AppendVector(slices));
  RETURN_NOT_OK(file->Close());
  return env->RenameFile(tmp_path, path);
}

Status VectorPayloadMap::LoadFromFile(const std::string& index_path) {
  auto* env = Env::Default();
  auto path = VectorIndexPayloadFilePath(index_path);
  auto file_size = VERIFY_RESULT(env->GetFileSize(path));
  RSTATUS_DCHECK_GE(
      file_size, kHeaderSize + kCrcSize, Corruption,
      Format("Vector payload file $0 is too small: $1", path, file_size));
  // Read the whole file into a single arena segment and point payloads into it.
  auto* buf = static_cast<uint8_t*>(arena_.AllocateBytes(file_size));
  {
    std::unique_ptr<RandomAccessFile> file;
    RETURN_NOT_OK(env->NewRandomAccessFile(path, &file));
    Slice data;
    RETURN_NOT_OK(env_util::ReadFully(file.get(), 0, file_size, &data, buf));
    if (data.data() != buf) {
      memcpy(buf, data.data(), data.size());
    }
  }
  Slice input(buf, file_size);
  auto expected_crc = crc::Crc32c(input.data(), input.size() - kCrcSize);
  auto found_crc = Load<uint32_t, LittleEndian>(input.end() - kCrcSize);
  RSTATUS_DCHECK_EQ(
      expected_crc, found_crc, Corruption, Format("Wrong CRC in vector payload file $0", path));
  input.RemoveSuffix(kCrcSize);
  auto version = input.consume_byte();
  RSTATUS_DCHECK_EQ(
      version, kSerializationVersion, Corruption,
      Format("Unsupported vector payload file version in $0", path));
  auto count = VERIFY_RESULT((CheckedRead<uint64_t, LittleEndian>(input)));

  payloads_.resize(count);
  for (uint64_t slot = 0; slot != count; ++slot) {
    auto payload_size = VERIFY_RESULT((CheckedRead<uint32_t, LittleEndian>(input)));
    RSTATUS_DCHECK(
        payload_size != 0 && input.size() >= payload_size, Corruption,
        Format("Broken payload for slot $0 in vector payload file $1", slot, path));
    payloads_[slot] = input.Prefix(payload_size);
    input.RemovePrefix(payload_size);
  }
  RSTATUS_DCHECK(
      input.empty(), Corruption,
      Format("Extra data in vector payload file $0: $1 bytes", path, input.size()));
  return Status::OK();
}

}  // namespace yb::vector_index
