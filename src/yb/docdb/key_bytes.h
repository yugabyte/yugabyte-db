// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_KEY_BYTES_H_
#define YB_DOCDB_KEY_BYTES_H_

#include <string>

#include "rocksdb/slice.h"

#include "yb/common/schema.h"
#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/value_type.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/enums.h"

namespace yb {
namespace docdb {

// Represents part (usually a prefix) of a RocksDB key. Has convenience methods for composing keys
// used in our document DB layer -> RocksDB mapping.
class KeyBytes {
 public:

  KeyBytes() {}
  explicit KeyBytes(const std::string& data) : data_(data) {}
  explicit KeyBytes(const rocksdb::Slice& slice) : data_(slice.data(), slice.size()) {}

  std::string ToString() const {
    return yb::util::FormatBytesAsStr(data_);
  }

  const std::string data() const {
    return data_;
  }

  void Append(const KeyBytes& other) {
    data_ += other.data_;
  }

  void AppendRawBytes(const char* raw_bytes, size_t n) {
    data_.append(raw_bytes, n);
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

  void AppendInt64(int64_t x) {
    AppendInt64ToKey(x, &data_);
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

  void AppendUInt32(uint32_t x) {
    AppendUInt32ToKey(x, &data_);
  }

  void AppendUInt16(int16_t x) {
    AppendUInt16ToKey(x, &data_);
  }

  void AppendHybridTime(HybridTime hybrid_time) {
    AppendEncodedHybridTimeToKey(hybrid_time, &data_);
  }

  void AppendColumnId(ColumnId column_id) {
    AppendColumnIdToKey(column_id, &data_);
  }

  void RemoveValueTypeSuffix(ValueType value_type) {
    CHECK_GE(data_.size(), sizeof(char));
    CHECK_EQ(data_.back(), static_cast<char>(value_type));
    data_.pop_back();
  }

  // Assuming the key bytes currently end with a hybrid time, replace that hybrid time with a
  // different one.
  void ReplaceLastHybridTime(HybridTime hybrid_time) {
    CHECK_GE(data_.size(), kBytesPerHybridTime);
    data_.resize(data_.size() - kBytesPerHybridTime);
    AppendHybridTime(hybrid_time);
  }

  size_t size() const { return data_.size(); }

  bool IsPrefixOf(const rocksdb::Slice& slice) const {
    return slice.starts_with(data_);
  }

  // Checks whether the other slice can be obtained by adding a hybrid_time.
  bool OnlyLacksTimeStampFrom(const rocksdb::Slice& other_slice) const {
    if (size() + 1 + kBytesPerHybridTime != other_slice.size()) {
      return false;
    }
    if (other_slice[size()] != static_cast<char>(ValueType::kHybridTime)) {
      return false;
    }
    return other_slice.starts_with(AsSlice());
  }

  // Checks whether the given slice starts with this sequence of key bytes.
  bool OnlyDiffersByLastHybridTimeFrom(const rocksdb::Slice& other_slice) const {
    if (size() != other_slice.size() || size() < kBytesPerHybridTime) {
      return false;
    }

    auto this_as_slice = AsSlice();
    this_as_slice.remove_suffix(kBytesPerHybridTime);
    return other_slice.starts_with(this_as_slice);
  }

  rocksdb::Slice AsSlice() const { return rocksdb::Slice(data_); }

  rocksdb::Slice AsSliceWithoutHybridTime() const {
    CHECK_GE(data_.size(), kBytesPerHybridTime);
    auto slice = AsSlice();
    slice.remove_suffix(kBytesPerHybridTime);
    return slice;
  }

  // @return This key prefix as a string reference. Assumes the reference won't be used beyond
  //         the lifetime of this object.
  const std::string& AsStringRef() const { return data_; }

  void Reset(rocksdb::Slice slice) {
    data_ = std::string(slice.data(), slice.size());
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
  std::string data_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_KEY_BYTES_H_
