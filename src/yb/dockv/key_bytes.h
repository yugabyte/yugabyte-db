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

#pragma once

#include <string>

#include "yb/common/common_fwd.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/util/kv_util.h"
#include "yb/util/slice.h"

namespace yb::dockv {

// Represents part (usually a prefix) of a RocksDB key. Has convenience methods for composing keys
// used in our DocDB layer -> RocksDB mapping.
class KeyBytes {
 public:
  KeyBytes() = default;
  explicit KeyBytes(const std::string& data) : data_(data) {}
  explicit KeyBytes(Slice slice) : data_(slice) {}

  KeyBytes(const KeyBytes& rhs) = default;
  KeyBytes& operator=(const KeyBytes& rhs) = default;
  KeyBytes(KeyBytes&& rhs) = default;
  KeyBytes& operator=(KeyBytes&& rhs) = default;

  KeyBytes(Slice slice, char suffix) {
    data_.reserve(slice.size() + 1);
    data_.append(slice.cdata(), slice.size());
    data_.push_back(suffix);
  }

  KeyBytes(Slice slice1, Slice slice2) : data_(slice1, slice2) {}

  void Reserve(size_t len) {
    data_.reserve(len);
  }

  std::string ToString() const;

  bool empty() const {
    return data_.empty();
  }

  std::string ToStringBuffer() const {
    return data().ToStringBuffer();
  }

  const KeyBuffer& data() const {
    return data_;
  }

  void Append(const KeyBytes& other) {
    data_.append(other.data_);
  }

  void AppendRawBytes(const char* raw_bytes, size_t n) {
    data_.append(raw_bytes, n);
  }

  void AppendRawBytes(Slice slice) {
    data_.append(slice);
  }

  void AppendRawBytes(const std::string& data) {
    data_.append(data);
  }

  void AppendKeyEntryType(KeyEntryType key_entry_type);

  void AppendKeyEntryTypeBeforeGroupEnd(KeyEntryType key_entry_type);

  void AppendString(const std::string& raw_string);

  void AppendDescendingString(const std::string &raw_string);

  void AppendDecimal(const std::string& encoded_decimal_str) {
    data_.append(encoded_decimal_str);
  }

  void AppendDecimalDescending(const std::string& encoded_decimal_str) {
    // Flipping all the bits negates the decimal number this string represents.

    // 0 is a special case because the first two bits are always 10.
    if ((unsigned char)encoded_decimal_str[0] == 128) {
      data_.push_back(encoded_decimal_str[0]);
      return;
    }

    data_.reserve(data_.size() + encoded_decimal_str.size());
    for (auto c : encoded_decimal_str) {
      data_.push_back(~c);
    }
  }

  void AppendVarInt(const std::string& encoded_varint_str) {
    data_.append(encoded_varint_str);
  }

  void AppendUInt64AsVarInt(uint64_t value);

  void AppendVarIntDescending(const std::string& encoded_varint_str) {
    // Flipping all the bits negates the varint number this string represents.

    // 0 is a special case because the first two bits are always 10.
    if (static_cast<uint8_t>(encoded_varint_str[0]) == 128) {
      data_.push_back(encoded_varint_str[0]);
      return;
    }

    for (auto c : encoded_varint_str) {
      data_.push_back(~c);
    }
  }

  void AppendInt64(int64_t x) {
    util::AppendInt64ToKey(x, &data_);
  }

  void AppendUInt64(uint64_t x);

  void AppendDescendingUInt64(uint64_t x);

  void AppendInt32(int32_t x) {
    util::AppendInt32ToKey(x, &data_);
  }

  void AppendUInt32(uint32_t x);

  void AppendDescendingUInt32(uint32_t x);

  template <class EnumBitSet>
  void AppendIntentTypeSet(EnumBitSet intent_type_set) {
    data_.push_back(static_cast<char>(intent_type_set.ToUIntPtr()));
  }

  void AppendDescendingInt64(int64_t x) {
    // AppendInt64ToKey flips the highest bit. We flip all the x's bits before calling
    // AppendInt64ToKey, but the order of the operations (FLIP_HIGHEST_BIT and FLIP_ALL_BITS)
    // doesn't matter because they are commutative operations.
    // Example for an 8-bit integer (works the same for 64-bit integers):
    //    normal encoding (flip first bit)
    //    -128 = 0x80 -> 0x00
    //    -1   = 0xFF -> 0x7F
    //    0    = 0x00 -> 0x80
    //    127  = 0x7F -> 0xFF
    //    (everything compares correctly)
    //
    //    descending encoding (flip all bits, then first, or flip first bit, then all, doesn't
    //    matter which)
    //    -128 = 0x80 -> 0x7F -> 0xFF
    //    -1   = 0xFF -> 0x00 -> 0x80
    //    0    = 0x00 -> 0xFF -> 0x7F
    //    127  = 0x7F -> 0x80 -> 0x00
    util::AppendInt64ToKey(~x, &data_);
  }

  void AppendDescendingInt32(int32_t x) {
    util::AppendInt32ToKey(~x, &data_);
  }

  void AppendUint8(uint8_t x) {
    data_.push_back(static_cast<char>(x));
  }

  void AppendUInt16(uint16_t x);

  void AppendHybridTime(const DocHybridTime& hybrid_time);

  void AppendColumnId(ColumnId column_id);

  void AppendDescendingFloat(float x) {
    util::AppendFloatToKey(x, &data_, /* descending */ true);
  }

  void AppendFloat(float x) {
    util::AppendFloatToKey(x, &data_);
  }

  void AppendDescendingDouble(double x) {
    util::AppendDoubleToKey(x, &data_, /* descending */ true);
  }

  void AppendDouble(double x) {
    util::AppendDoubleToKey(x, &data_);
  }

  void AppendGroupEnd();

  void RemoveKeyEntryTypeSuffix(KeyEntryType key_entry_type);

  size_t size() const { return data_.size(); }

  bool IsPrefixOf(const rocksdb::Slice& slice) const {
    return slice.starts_with(data_.AsSlice());
  }

  Slice AsSlice() const { return data_.AsSlice(); }

  operator Slice() const { return data_.AsSlice(); }

  void Reset(Slice slice) {
    data_.assign(slice.cdata(), slice.size());
  }

  void ResetRawBytes(const char* raw_bytes, size_t n) {
    data_.assign(raw_bytes, n);
  }

  void Clear() {
    data_.clear();
  }

  int CompareTo(const KeyBytes& other) const {
    return data_.AsSlice().compare(other.data_.AsSlice());
  }

  int CompareTo(Slice other) const {
    return data_.AsSlice().compare(other);
  }

  // This can be used to e.g. move the internal state of KeyBytes somewhere else, including a
  // string field in a protobuf, without copying the bytes.
  KeyBuffer* mutable_data() {
    return &data_;
  }

  void Truncate(size_t new_size);

  void RemoveLastByte();

 private:
  KeyBuffer data_;
};

inline bool operator<(const KeyBytes& lhs, const KeyBytes& rhs) {
  return lhs.data() < rhs.data();
}

inline bool operator>=(const KeyBytes& lhs, const KeyBytes& rhs) {
  return !(lhs < rhs);
}

inline bool operator>(const KeyBytes& lhs, const KeyBytes& rhs) {
  return rhs < lhs;
}

inline bool operator<=(const KeyBytes& lhs, const KeyBytes& rhs) {
  return !(rhs < lhs);
}

inline bool operator==(const KeyBytes& lhs, const KeyBytes& rhs) {
  return lhs.AsSlice() == rhs.AsSlice();
}

inline bool operator!=(const KeyBytes& lhs, const KeyBytes& rhs) {
  return lhs.AsSlice() != rhs.AsSlice();
}

void AppendDocHybridTime(const DocHybridTime& time, KeyBytes* key);
void AppendHash(uint16_t hash, KeyBytes* key);

}  // namespace yb::dockv
