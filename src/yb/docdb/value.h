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

#ifndef YB_DOCDB_VALUE_H_
#define YB_DOCDB_VALUE_H_

#include "yb/docdb/value_type.h"
#include "yb/docdb/primitive_value.h"
#include "yb/util/monotime.h"

namespace yb {
namespace docdb {

class Value {
 public:
  Value() : primitive_value_(),
            ttl_(kMaxTtl),
            user_timestamp_micros_(kInvalidUserTimestamp) {
  }

  explicit Value(PrimitiveValue primitive_value,
                 MonoDelta ttl = kMaxTtl,
                 int64_t user_timestamp_micros = kInvalidUserTimestamp)
      : primitive_value_(primitive_value),
        ttl_(ttl),
        user_timestamp_micros_(user_timestamp_micros) {
  }

  static const MonoDelta kMaxTtl;
  static const int64_t kInvalidUserTimestamp;
  static constexpr int kBytesPerInt64 = sizeof(int64_t);

  MonoDelta ttl() const { return ttl_; }

  int64_t user_timestamp_micros() const { return user_timestamp_micros_; }

  bool has_ttl() const { return !ttl_.Equals(kMaxTtl); }

  ValueType value_type() const { return primitive_value_.value_type(); }

  PrimitiveValue* mutable_primitive_value() { return &primitive_value_; }

  const PrimitiveValue primitive_value() const { return primitive_value_; }

  // Consume the Ttl portion of the slice if it exists and return it.
  static CHECKED_STATUS DecodeTTL(rocksdb::Slice* rocksdb_value, MonoDelta* ttl);

  // A version that doesn't mutate the slice.
  static CHECKED_STATUS DecodeTTL(const rocksdb::Slice& rocksdb_value, MonoDelta* ttl) {
    rocksdb::Slice value_copy = rocksdb_value;
    return DecodeTTL(&value_copy, ttl);
  }

  // Decode the entire value
  CHECKED_STATUS Decode(const rocksdb::Slice &rocksdb_value);

  std::string ToString() const;

  std::string Encode() const;

  void EncodeAndAppend(std::string* value_bytes) const;

 private:
  // Consume the timestamp portion of the slice assuming the beginning of the slice points to
  // the timestamp.
  CHECKED_STATUS DecodeUserTimestamp(rocksdb::Slice* rocksdb_value);

  // Returns true if the expected_value_type is found, otherwise returns false and sets val to
  // default_value.
  template <typename T>
  static bool DecodeType(const ValueType& expected_value_type, const T& default_value,
                         rocksdb::Slice* slice, T* val);

  PrimitiveValue primitive_value_;

  // The ttl of the Value. kMaxTtl is the default value. TTL is not included in encoded
  // form if it is equal to kMax.
  // The unit is milliseconds.
  MonoDelta ttl_;

  // The timestamp provided by the user as part of a 'USING TIMESTAMP' clause in CQL.
  int64_t user_timestamp_micros_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_VALUE_H_
