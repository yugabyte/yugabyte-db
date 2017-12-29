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

#include "yb/common/typedefs.h"
#include "yb/docdb/value_type.h"
#include "yb/docdb/primitive_value.h"
#include "yb/util/monotime.h"

namespace yb {
namespace docdb {

// This class represents the data stored in the value portion of rocksdb. It consists of the TTL
// for the given key, the user specified timestamp and finally the value. These items are encoded
// into a RocksDB Slice in the order mentioned above. The TTL and user timestamp are optional.
class Value {
 public:
  Value() : primitive_value_(),
            ttl_(kMaxTtl),
            user_timestamp_(kInvalidUserTimestamp) {
  }

  explicit Value(PrimitiveValue primitive_value,
                 MonoDelta ttl = kMaxTtl,
                 UserTimeMicros user_timestamp = kInvalidUserTimestamp)
      : primitive_value_(primitive_value),
        ttl_(ttl),
        user_timestamp_(user_timestamp) {
  }

  static const MonoDelta kMaxTtl;
  static const int64_t kInvalidUserTimestamp;
  static constexpr int kBytesPerInt64 = sizeof(int64_t);

  MonoDelta ttl() const { return ttl_; }

  UserTimeMicros user_timestamp() const { return user_timestamp_; }

  bool has_ttl() const { return !ttl_.Equals(kMaxTtl); }

  bool has_user_timestamp() const { return user_timestamp_ != kInvalidUserTimestamp; }

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

  // Decodes the ValueType of the primitive value stored in the given rocksdb_value.
  static CHECKED_STATUS DecodePrimitiveValueType(const rocksdb::Slice& rocksdb_value,
                                                 ValueType* value_type);

  // Return the user timestamp portion from a slice that points to the rocksdb_value.
  static CHECKED_STATUS DecodeUserTimestamp(const rocksdb::Slice& rocksdb_value,
                                            UserTimeMicros* user_timestamp);

 private:
  // Consume the timestamp portion of the slice assuming the beginning of the slice points to
  // the timestamp.
  static CHECKED_STATUS DecodeUserTimestamp(rocksdb::Slice* slice, UserTimeMicros* user_timestamp);

  PrimitiveValue primitive_value_;

  // The ttl of the Value. kMaxTtl is the default value. TTL is not included in encoded
  // form if it is equal to kMax.
  // The unit is milliseconds.
  MonoDelta ttl_;

  // The timestamp provided by the user as part of a 'USING TIMESTAMP' clause in CQL.
  UserTimeMicros user_timestamp_;

  // If this value was written using a transaction,
  // this field stores the original intent doc hybrid time.
  DocHybridTime intent_doc_ht_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_VALUE_H_
