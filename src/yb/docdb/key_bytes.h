// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_KEY_BYTES_H_
#define YB_DOCDB_KEY_BYTES_H_

#include <string>

#include "rocksdb/slice.h"

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

  void AppendInt64(int64_t x) {
    AppendInt64ToKey(x, &data_);
  }

  void AppendUInt32(int32_t x) {
    AppendUInt32ToKey(x, &data_);
  }

  void AppendTimestamp(Timestamp timestamp) {
    AppendEncodedTimestampToKey(timestamp, &data_);
  }

  // Assuming the key bytes currently end with a timestamp, replace that timestamp with a different
  // one.
  void ReplaceLastTimestamp(Timestamp timestamp) {
    assert(data_.size() >= kBytesPerTimestamp);
    data_.resize(data_.size() - kBytesPerTimestamp);
    AppendTimestamp(timestamp);
  }

  size_t size() const { return data_.size(); }

  bool IsPrefixOf(const rocksdb::Slice& slice) const {
    return slice.starts_with(data_);
  }

  // Checks whether the given slice starts with this sequence of key bytes.
  bool OnlyDiffersByLastTimestampFrom(const rocksdb::Slice& other_slice) const {
    if (size() != other_slice.size() || size() < kBytesPerTimestamp) {
      return false;
    }

    auto this_as_slice = AsSlice();
    this_as_slice.remove_suffix(kBytesPerTimestamp);
    return other_slice.starts_with(this_as_slice);
  }

  rocksdb::Slice AsSlice() const { return rocksdb::Slice(data_); }

  rocksdb::Slice AsSliceWithoutTimestamp() const {
    assert(data_.size() >= kBytesPerTimestamp);
    auto slice = AsSlice();
    slice.remove_suffix(kBytesPerTimestamp);
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

  // Increments this key to the smallest key that is greater than it.
  KeyBytes& Increment() {
    data_.push_back('\0');
    return *this;
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
