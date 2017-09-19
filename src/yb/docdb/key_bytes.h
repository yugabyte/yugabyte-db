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

#ifndef YB_DOCDB_KEY_BYTES_H_
#define YB_DOCDB_KEY_BYTES_H_

#include <string>

#include "yb/util/slice.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/value_type.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/decimal.h"
#include "yb/util/enums.h"

namespace yb {
namespace docdb {

// Represents part (usually a prefix) of a RocksDB key. Has convenience methods for composing keys
// used in our document DB layer -> RocksDB mapping.
class KeyBytes {
 public:

  KeyBytes() {}
  explicit KeyBytes(const std::string& data) : data_(data) {}
  explicit KeyBytes(const rocksdb::Slice& slice) : data_(slice.ToBuffer()) {}

  std::string ToString() const {
    return yb::util::FormatBytesAsStr(data_);
  }

  const std::string& data() const {
    return data_;
  }

  void Append(const KeyBytes& other) {
    data_ += other.data_;
  }

  void AppendRawBytes(const char* raw_bytes, size_t n) {
    data_.append(raw_bytes, n);
  }

  void AppendRawBytes(const Slice& slice) {
    data_.append(slice.cdata(), slice.size());
  }

  void AppendRawBytes(const std::string& data) {
    data_ += data;
  }

  void AppendValueType(ValueType value_type) {
    data_.push_back(static_cast<char>(value_type));
  }

  void AppendString(const std::string& raw_string) {
    ZeroEncodeAndAppendStrToKey(raw_string, &data_);
  }

  void AppendDescendingString(const std::string &raw_string) {
    ComplementZeroEncodeAndAppendStrToKey(raw_string, &data_);
  }

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

    for (auto c : encoded_decimal_str) {
      data_.push_back(~c);
    }
  }

  void AppendInt64(int64_t x) {
    AppendInt64ToKey(x, &data_);
  }

  void AppendInt32(int32_t x) {
    AppendInt32ToKey(x, &data_);
  }

  void AppendIntentType(IntentType intent_type) {
    data_.push_back(static_cast<char>(intent_type));
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
    AppendInt64ToKey(~x, &data_);
  }

  void AppendDescendingInt32(int32_t x) {
    AppendInt32ToKey(~x, &data_);
  }

  void AppendUInt16(int16_t x) {
    AppendUInt16ToKey(x, &data_);
  }

  void AppendHybridTimeForSeek(HybridTime hybrid_time) {
    DocHybridTime(hybrid_time, kMaxWriteId).AppendEncodedInDocDbFormat(&data_);
  }

  void AppendHybridTime(const DocHybridTime& hybrid_time) {
    hybrid_time.AppendEncodedInDocDbFormat(&data_);
  }

  void AppendColumnId(ColumnId column_id) {
    AppendColumnIdToKey(column_id, &data_);
  }

  void AppendFloat(float x) {
    AppendFloatToKey(x, &data_);
  }

  void AppendDouble(double x) {
    AppendDoubleToKey(x, &data_);
  }

  void RemoveValueTypeSuffix(ValueType value_type) {
    CHECK_GE(data_.size(), sizeof(char));
    CHECK_EQ(data_.back(), static_cast<char>(value_type));
    data_.pop_back();
  }

  // Assuming the key bytes currently end with a hybrid time, replace that hybrid time with a
  // different one.
  CHECKED_STATUS ReplaceLastHybridTimeForSeek(HybridTime hybrid_time);

  size_t size() const { return data_.size(); }

  bool IsPrefixOf(const rocksdb::Slice& slice) const {
    return slice.starts_with(data_);
  }

  // Checks whether the other slice can be obtained by adding an encoded DocHybridTime to
  // to this encoded key.
  CHECKED_STATUS OnlyLacksHybridTimeFrom(const rocksdb::Slice& other_slice, bool* result) const;

  rocksdb::Slice AsSlice() const { return rocksdb::Slice(data_); }

  // @return This key prefix as a string reference. Assumes the reference won't be used beyond
  //         the lifetime of this object.
  const std::string& AsStringRef() const { return data_; }

  void Reset(rocksdb::Slice slice) {
    data_.assign(slice.cdata(), slice.size());
  }

  void Clear() {
    data_.clear();
  }

  int CompareTo(const KeyBytes& other) const {
    return data_.compare(other.data_);
  }

  int CompareTo(const rocksdb::Slice& other) const {
    return rocksdb::Slice(data_).compare(other);
  }

  bool operator <(const KeyBytes& other) {
    return data_ < other.data_;
  }

  bool operator >(const KeyBytes& other) {
    return data_ > other.data_;
  }

  // This can be used to e.g. move the internal state of KeyBytes somewhere else, including a
  // string field in a protobuf, without copying the bytes.
  std::string* mutable_data() {
    return &data_;
  }

  std::string ToShortDebugStr() {
    return yb::docdb::ToShortDebugStr(data_);
  }

 private:
  int encoded_hybrid_time_size() const {
    return data_.empty() ? 0 : static_cast<uint8_t>(data_.back());
  }

  std::string data_;
};

void AppendIntentType(IntentType intent_type, KeyBytes* key);
void AppendDocHybridTime(const DocHybridTime& time, KeyBytes* key);

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_KEY_BYTES_H_
