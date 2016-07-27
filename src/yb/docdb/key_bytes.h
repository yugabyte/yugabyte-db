// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_KEY_BYTES_H
#define YB_DOCDB_KEY_BYTES_H

#include <string>

#include "rocksdb/slice.h"

#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/value_type.h"
#include "yb/util/bytes_formatter.h"

namespace yb {
namespace docdb {

// Represents part (usually a prefix) of a RocksDB key. Has convenience methods for composing keys
// used in our document DB layer -> RocksDB mapping.
class KeyBytes {
 public:

  std::string ToString() {
    return yb::util::FormatBytesAsStr(data_);
  }

  std::string data() {
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

  size_t size() { return data_.size(); }

  bool IsPrefixOf(const rocksdb::Slice& slice) const {
    return slice.starts_with(data_);
  }

  // Checks whether the given slice starts with this sequence of key bytes, excluding the timestamp
  // at the end of this sequence of key bytes that is assumed to exist.
  bool SharesPrefixWithoutTimestampWith(const rocksdb::Slice& slice) {
    assert(size() >= kBytesPerTimestamp);
    auto this_as_slice = AsSlice();
    this_as_slice.remove_suffix(kBytesPerTimestamp);
    return slice.starts_with(this_as_slice);
  }

  rocksdb::Slice AsSlice() const { return rocksdb::Slice(data_); }

  void Reset(rocksdb::Slice slice) {
    data_ = std::string(slice.data(), slice.size());
  }

  void Clear() {
    data_.clear();
  }

  int CompareTo(const KeyBytes& other) const {
    return data_.compare(other.data_);
  }

  bool operator <(const KeyBytes& other) {
    return data_ < other.data_;
  }

  bool operator >(const KeyBytes& other) {
    return data_ > other.data_;
  }

 private:
  std::string data_;
};

}
}

#endif
