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
            user_timestamp_(kInvalidUserTimestamp),
            merge_flags_(0) {
  }

  explicit Value(PrimitiveValue primitive_value,
                 MonoDelta ttl = kMaxTtl,
                 UserTimeMicros user_timestamp = kInvalidUserTimestamp,
                 uint64_t merge_flags = 0)
      : primitive_value_(primitive_value),
        ttl_(ttl),
        user_timestamp_(user_timestamp),
        merge_flags_(merge_flags) {
  }

  static const uint64_t kTtlFlag = 0x1;

  static const MonoDelta kMaxTtl;
  // kResetTtl is useful for CQL when zero TTL indicates no TTL.
  static const MonoDelta kResetTtl;
  static const int64_t kInvalidUserTimestamp;
  static constexpr int kBytesPerInt64 = sizeof(int64_t);

  MonoDelta ttl() const { return ttl_; }

  MonoDelta* mutable_ttl() { return &ttl_; }

  UserTimeMicros user_timestamp() const { return user_timestamp_; }

  bool has_ttl() const { return !ttl_.Equals(kMaxTtl); }

  bool has_user_timestamp() const { return user_timestamp_ != kInvalidUserTimestamp; }

  ValueType value_type() const { return primitive_value_.value_type(); }

  PrimitiveValue* mutable_primitive_value() { return &primitive_value_; }

  const PrimitiveValue& primitive_value() const { return primitive_value_; }

  uint64_t merge_flags() const { return merge_flags_; }

  const DocHybridTime& intent_doc_ht() const { return intent_doc_ht_; }

  void ClearIntentDocHt();

  // Consume the merge_flags portion of the slice if it exists and return it.
  static CHECKED_STATUS DecodeMergeFlags(rocksdb::Slice* slice, uint64_t* merge_flags);

  // A version that doesn't mutate the slice.
  static CHECKED_STATUS DecodeMergeFlags(const rocksdb::Slice& slice, uint64_t* merge_flags);

  // Consume the Ttl portion of the slice if it exists and return it.
  static CHECKED_STATUS DecodeTTL(rocksdb::Slice* rocksdb_value, MonoDelta* ttl);

  // A version that doesn't mutate the slice.
  static CHECKED_STATUS DecodeTTL(const rocksdb::Slice& rocksdb_value, MonoDelta* ttl);

  // Decode the entire value
  CHECKED_STATUS Decode(const Slice& rocksdb_value);

  // Decode value control fields, w/o decrypting actual value.
  CHECKED_STATUS DecodeControlFields(Slice* slice);

  std::string ToString() const;

  // Encode and return result as string.
  // If external_value is not null, then it is appended to end of result instead of
  // the primitive_value_ field of this object.
  std::string Encode(const Slice* external_value = nullptr) const;

  // Appends encoded value to value_bytes.
  // If external_value is not null, then it is appended to end of value_bytes instead of
  // the primitive_value_ field of this object.
  void EncodeAndAppend(std::string* value_bytes, const Slice* external_value = nullptr) const;

  // Decodes the ValueType of the primitive value stored in the
  // given RocksDB value and any other values before it.
  static CHECKED_STATUS DecodePrimitiveValueType(
      const rocksdb::Slice& rocksdb_value,
      ValueType* value_type,
      uint64_t* merge_flags = nullptr,
      MonoDelta* ttl = nullptr,
      int64_t* user_timestamp = nullptr);

  // Return the user timestamp portion from a slice that points to the RocksDB value.
  static CHECKED_STATUS DecodeUserTimestamp(const rocksdb::Slice& rocksdb_value,
                                            UserTimeMicros* user_timestamp);

  static const Value& Tombstone();
  static const std::string& EncodedTombstone();

  static std::string DebugSliceToString(const Slice& encoded_value);

  static Result<bool> IsTombstoned(const Slice& slice);

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

  // A place to store various merge flags; in particular, the MERGE flag currently used for TTL.
  // 0x1 = TTL-only entry
  // 0x3 = Value-only entry (potentially)
  uint64_t merge_flags_;

  // If this value was written using a transaction,
  // this field stores the original intent doc hybrid time.
  DocHybridTime intent_doc_ht_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_VALUE_H_
