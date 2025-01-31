// Copyright (c) YugaByte, Inc.
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

#include "yb/util/uuid.h"

#include <boost/lexical_cast.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "yb/gutil/endian.h"

#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

namespace yb {

Uuid::Uuid() {
  memset(&boost_uuid_, 0, sizeof(boost_uuid_));
}

Uuid::Uuid(const uuid_t copy) {
  for(int it = 0; it < 16; it ++) {
    boost_uuid_.data[it] = copy[it];
  }
}

Uuid Uuid::Nil() {
  return Uuid(boost::uuids::nil_uuid());
}

Uuid Uuid::Generate() {
  return Uuid::Generate(&ThreadLocalRandom());
}

Uuid Uuid::Generate(std::mt19937_64* rng) {
  return Uuid(boost::uuids::basic_random_generator<std::mt19937_64>(rng)());
}

std::string Uuid::ToString() const {
  std::string strval;
  CHECK_OK(ToString(&strval));
  return strval;
}

Status Uuid::ToString(std::string *strval) const {
  *strval = boost::uuids::to_string(boost_uuid_);
  return Status::OK();
}

void Uuid::EncodeToComparable(uint8_t* output) const {
  if (boost_uuid_.version() == boost::uuids::uuid::version_time_based) {
    // Take the MSB of the UUID and get the timestamp ordered bytes.
    ToTimestampBytes(output);
  } else {
    ToVersionFirstBytes(output);
  }
  memcpy(output + kUuidMsbSize, boost_uuid_.data + kUuidMsbSize, kUuidLsbSize);
}

void Uuid::EncodeToComparable(std::string* bytes) const {
  uint8_t output[kUuidSize];
  EncodeToComparable(output);
  bytes->assign(reinterpret_cast<char *>(output), kUuidSize);
}

void Uuid::ToBytes(std::string* bytes) const {
  bytes->assign(boost_uuid_.begin(), boost_uuid_.end());
}

void Uuid::ToBytes(std::array<uint8_t, kUuidSize>* out) const {
  memcpy(out->data(), boost_uuid_.data, kUuidSize);
}

void Uuid::ToBytes(void* out) const {
  memcpy(out, boost_uuid_.data, kUuidSize);
}

Slice Uuid::AsSlice() const {
  return Slice(boost_uuid_.data, boost_uuid_.size());
}

Result<Uuid> Uuid::FromSlice(const Slice& slice) {
  if (slice.size() != kUuidSize) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Size of slice is invalid: $0", slice.size());
  }
  Uuid result;
  memcpy(result.boost_uuid_.data, slice.data(), kUuidSize);
  return result;
}

std::string Uuid::ToHexString() const {
  using Word = unsigned long long; // NOLINT
  constexpr size_t kWordSize = sizeof(Word);
  char buffer[kUuidSize * 2 + 1];
  for (size_t i = boost_uuid_.size(); i != 0;) {
    char* outpos = buffer + (kUuidSize - i) * 2;
    i -= kWordSize;
    Word value;
    memcpy(&value, boost_uuid_.data + i, kWordSize);
    static_assert(sizeof(Word) == 8, "Adjust little endian conversion below");
    snprintf(outpos, kWordSize * 2 + 1, "%016" PRIx64, LittleEndian::ToHost64(value));
  }
  return std::string(buffer, sizeof(buffer) - 1);
}

Result<Uuid> Uuid::FromHexString(const std::string& hex_string) {
  if (hex_string.empty()) {
    return Uuid::Nil();
  }

  constexpr size_t kInputLen = kUuidSize * 2;
  if (hex_string.length() != kInputLen) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Size of hex_string is invalid: $0, expected: $1",
                             hex_string.size(), kInputLen);
  }
  using Word = unsigned long long; // NOLINT
  constexpr size_t kWordLen = sizeof(Word) * 2;
  static_assert(kInputLen % kWordLen == 0, "Unexpected word size");
  char buffer[kWordLen + 1];
  buffer[kWordLen] = 0;

  Uuid result;
  for (size_t i = 0; i != kInputLen;) {
    memcpy(buffer, hex_string.c_str() + i, kWordLen);
    char* endptr = nullptr;
    auto value = strtoull(buffer, &endptr, 0x10);
    if (endptr != buffer + kWordLen) {
      return STATUS_FORMAT(
          InvalidArgument, "$0 is not a valid uuid at $1", hex_string, i + endptr - buffer);
    }
    static_assert(sizeof(Word) == 8, "Adjust little endian conversion below");
    value = LittleEndian::FromHost64(value);
    i += kWordLen;
    memcpy(result.boost_uuid_.data + result.boost_uuid_.size() - i / 2, &value, sizeof(value));
  }

  return result;
}

Result<Uuid> Uuid::FromComparable(const Slice& slice) {
  Uuid result;
  size_t expected_size = slice.size();
  if (expected_size != kUuidSize) {
    return STATUS_SUBSTITUTE(InvalidArgument,
                             "Decode error: Size of slice is invalid: $0", expected_size);
  }
  const uint8_t* bytes = slice.data();
  if ((bytes[0] & 0xF0) == 0x10) {
    // Check the first byte to see if it is version 1.
    result.FromTimestampBytes(bytes);
  } else {
    result.FromVersionFirstBytes(bytes);
  }
  memcpy(result.boost_uuid_.data + kUuidMsbSize, bytes + kUuidMsbSize, kUuidLsbSize);
  return result;
}

Status Uuid::HashMACAddress() {
  RETURN_NOT_OK(IsTimeUuid());
  boost::uuids::detail::sha1 sha1;
  unsigned int hash[kShaDigestSize];
  sha1.process_bytes(boost_uuid_.data + kTimeUUIDMacOffset, kTimeUUIDTotalMacBytes);
  uint8_t tmp[kTimeUUIDTotalMacBytes];
  sha1.get_digest(hash);
  for (size_t i = 0; i < kTimeUUIDTotalMacBytes; i ++) {
    tmp[i] = (hash[i % kShaDigestSize] & 255);
    hash[i % kShaDigestSize] = hash[i % kShaDigestSize] >> 8;
  }
  memcpy(boost_uuid_.data + kTimeUUIDMacOffset, tmp, kTimeUUIDTotalMacBytes);
  return Status::OK();
}

void Uuid::FromTimestamp(int64_t ts_hnanos) {
  uint64_t ts_byte_data = BigEndian::FromHost64((uint64_t)ts_hnanos);
  auto* ts_bytes = reinterpret_cast<uint8_t *>(&ts_byte_data);
  ts_bytes[0] = ((ts_bytes[0] & 0x0F) | 0x10); // Set the version to 1.
  FromTimestampBytes(ts_bytes);
}

Status Uuid::MaxFromUnixTimestamp(int64_t timestamp_ms) {
  // Since we are converting to a finer-grained precision (milliseconds to 100's nanoseconds) the
  // input milliseconds really corresponds to a range in 100's nanoseconds precision.
  // So, to get a logically correct max timeuuid, we need to use the upper bound of that range
  // (i.e. add '9999' at the end not '0000').
  int64_t ts_hnanos = (timestamp_ms + 1 - kGregorianOffsetMillis) * kMillisPerHundredNanos - 1;

  FromTimestamp(ts_hnanos); // Set most-significant bits (i.e. timestamp).
  memset(boost_uuid_.data + kUuidMsbSize, 0xFF, kUuidLsbSize); // Set least-significant bits.
  return Status::OK();
}

Status Uuid::MinFromUnixTimestamp(int64_t timestamp_ms) {
  int64_t timestamp = (timestamp_ms - kGregorianOffsetMillis) * kMillisPerHundredNanos;
  FromTimestamp(timestamp); // Set most-significant bits (i.e. timestamp).
  memset(boost_uuid_.data + kUuidMsbSize, 0x00, kUuidLsbSize); // Set least-significant bits.
  return Status::OK();
}

Status Uuid::ToUnixTimestamp(int64_t* timestamp_ms) const {
  RETURN_NOT_OK(IsTimeUuid());
  uint8_t output[kUuidMsbSize];
  ToTimestampBytes(output);
  output[0] = (output[0] & 0x0f);
  *timestamp_ms = 0;
  for (size_t i = 0; i < kUuidMsbSize; i++) {
    *timestamp_ms = (*timestamp_ms << 8) | (output[i] & 0xff);
  }
  // Convert from nano seconds since Gregorian calendar start to millis since unix epoch.
  *timestamp_ms = (*timestamp_ms / kMillisPerHundredNanos) + kGregorianOffsetMillis;
  return Status::OK();
}

Status Uuid::IsTimeUuid() const {
  if (boost_uuid_.version() == boost::uuids::uuid::version_time_based) {
    return Status::OK();
  }

  return STATUS_SUBSTITUTE(InvalidArgument,
                           "Not a type 1 UUID. Current type: $0", boost_uuid_.version());
}

bool operator<(const Uuid& lhs, const Uuid& rhs) {
  // First compare the version, variant and then the timestamp bytes.
  if (lhs.boost_uuid_.version() < rhs.boost_uuid_.version()) {
    return true;
  } else if (lhs.boost_uuid_.version() > rhs.boost_uuid_.version()) {
    return false;
  }
  if (lhs.boost_uuid_.version() == boost::uuids::uuid::version_time_based) {
    // Compare the hi timestamp bits.
    for (size_t i = 6; i < Uuid::kUuidMsbSize; i++) {
      if (lhs.boost_uuid_.data[i] < rhs.boost_uuid_.data[i]) {
        return true;
      } else if (lhs.boost_uuid_.data[i] > rhs.boost_uuid_.data[i]) {
        return false;
      }
    }
    // Compare the mid timestamp bits.
    for (int i = 4; i < 6; i++) {
      if (lhs.boost_uuid_.data[i] < rhs.boost_uuid_.data[i]) {
        return true;
      } else if (lhs.boost_uuid_.data[i] > rhs.boost_uuid_.data[i]) {
        return false;
      }
    }
    // Compare the low timestamp bits.
    for (int i = 0; i < 4; i++) {
      if (lhs.boost_uuid_.data[i] < rhs.boost_uuid_.data[i]) {
        return true;
      } else if (lhs.boost_uuid_.data[i] > rhs.boost_uuid_.data[i]) {
        return false;
      }
    }
  } else {
    // Compare all the other bits
    for (size_t i = 0; i < Uuid::kUuidMsbSize; i++) {
      if (lhs.boost_uuid_.data[i] < rhs.boost_uuid_.data[i]) {
        return true;
      } else if (lhs.boost_uuid_.data[i] > rhs.boost_uuid_.data[i]) {
        return false;
      }
    }
  }

  // Then compare the remaining bytes.
  for (size_t i = Uuid::kUuidMsbSize; i < kUuidSize; i++) {
    if (lhs.boost_uuid_.data[i] < rhs.boost_uuid_.data[i]) {
      return true;
    } else if (lhs.boost_uuid_.data[i] > rhs.boost_uuid_.data[i]) {
      return false;
    }
  }
  return false;
}

namespace {

// Makes transaction id from its binary representation.
// If check_exact_size is true, checks that slice contains only TransactionId.
Result<Uuid> DoDecodeUuid(
    const Slice &slice, const bool check_exact_size, const char* name) {
  if (check_exact_size ? slice.size() != boost::uuids::uuid::static_size()
                       : slice.size() < boost::uuids::uuid::static_size()) {
    if (!name) {
      name = "UUID";
    }
    return STATUS_FORMAT(
        Corruption, "Invalid length of binary data with $4 '$0': $1 (expected $2$3)",
        slice.ToDebugHexString(), slice.size(), check_exact_size ? "" : "at least ",
        boost::uuids::uuid::static_size(), name);
  }
  Uuid id;
  memcpy(id.data(), slice.data(), boost::uuids::uuid::static_size());
  return id;
}

} // namespace

Result<Uuid> Uuid::FullyDecode(const Slice& slice, const char* name) {
  return DoDecodeUuid(slice, /* check_exact_size= */ true, name);
}

Uuid Uuid::TryFullyDecode(const Slice& slice) {
  if (slice.size() != boost::uuids::uuid::static_size()) {
    return Uuid::Nil();
  }
  Uuid id;
  memcpy(id.data(), slice.data(), boost::uuids::uuid::static_size());
  return id;
}

Result<Uuid> Uuid::Decode(Slice* slice, const char* name) {
  auto id = VERIFY_RESULT(DoDecodeUuid(*slice, /* check_exact_size= */ false, name));
  slice->remove_prefix(boost::uuids::uuid::static_size());
  return id;
}

Result<Uuid> Uuid::FromString(const std::string& strval) {
  if (strval.empty()) {
    return Uuid::Nil();
  }
  try {
    return Uuid(boost::lexical_cast<boost::uuids::uuid>(strval));
  } catch (std::exception& e) {
    return STATUS(Corruption, "Couldn't read Uuid from string", strval);
  }
}

Result<Uuid> Uuid::FromHexStringBigEndian(const std::string& strval) {
  if (strval.empty()) {
    return Uuid::Nil();
  }
  try {
    boost::uuids::string_generator uuid_gen;
    return Uuid(uuid_gen(strval));
  } catch (std::exception& e) {
    return STATUS(Corruption, "Couldn't read Uuid from string", strval);
  }
}

} // namespace yb
