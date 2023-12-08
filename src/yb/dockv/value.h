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

#include "yb/common/common_types.pb.h"
#include "yb/common/typedefs.h"

#include "yb/dockv/primitive_value.h"

#include "yb/util/monotime.h"

namespace yb::dockv {

// Value could contain extra attributes, such as TTL or user timestamp.
// They are represented in this struct.
struct ValueControlFields {
  static const MonoDelta kMaxTtl;
  // kResetTtl is useful for CQL when zero TTL indicates no TTL.
  static const MonoDelta kResetTtl;
  static const int64_t kInvalidTimestamp;

  static const uint64_t kTtlFlag = 0x1;

  // A place to store various merge flags; in particular, the MERGE flag currently used for TTL.
  // 0x1 = TTL-only entry
  // 0x3 = Value-only entry (potentially)
  uint64_t merge_flags = 0;

  // The ttl of the Value. kMaxTtl is the default value. TTL is not included in encoded
  // form if it is equal to kMax.
  // The unit is milliseconds.
  MonoDelta ttl = kMaxTtl;

  // The timestamp assigned to this value, if it was externally specified.
  UserTimeMicros timestamp = kInvalidTimestamp;

  void AppendEncoded(ValueBuffer* out) const;
  void AppendEncoded(std::string* out) const;
  std::string ToString() const;

  // Decode value control fields, w/o decrypting actual value.
  static Result<ValueControlFields> Decode(Slice* slice);
  static Result<ValueControlFields> DecodeWithIntentDocHt(Slice* slice, Slice* intent_doc_ht);

  bool has_ttl() const {
    return !ttl.Equals(kMaxTtl);
  }

  bool has_timestamp() const {
    return timestamp != kInvalidTimestamp;
  }
};

// This class represents the data stored in the value portion of rocksdb. It consists of the TTL
// for the given key, the user specified timestamp and finally the value. These items are encoded
// into a RocksDB Slice in the order mentioned above. The TTL and user timestamp are optional.
class Value {
 public:
  Value() = default;

  explicit Value(const PrimitiveValue& primitive_value,
                 const ValueControlFields& control_fields = ValueControlFields())
      : primitive_value_(primitive_value),
        control_fields_(control_fields) {
  }

  const ValueControlFields& control_fields() const {
    return control_fields_;
  }

  MonoDelta ttl() const { return control_fields_.ttl; }

  MonoDelta* mutable_ttl() { return &control_fields_.ttl; }

  UserTimeMicros timestamp() const { return control_fields_.timestamp; }

  bool has_ttl() const { return control_fields_.has_ttl(); }

  bool has_timestamp() const { return control_fields_.has_timestamp(); }

  ValueEntryType value_type() const { return primitive_value_.value_type(); }

  PrimitiveValue* mutable_primitive_value() { return &primitive_value_; }

  const PrimitiveValue& primitive_value() const { return primitive_value_; }

  uint64_t merge_flags() const { return control_fields_.merge_flags; }

  // Decode the entire value
  Status Decode(const Slice& rocksdb_value);
  Status Decode(const Slice& rocksdb_value, const ValueControlFields& control_fields);

  std::string ToString() const;

  // Decodes the ValueType of the primitive value stored in the
  // given RocksDB value and any other values before it.
  static Result<ValueEntryType> DecodePrimitiveValueType(const Slice& rocksdb_value);

  static const Value& Tombstone();
  static const std::string& EncodedTombstone();

  static std::string DebugSliceToString(const Slice& encoded_value);

  static Result<bool> IsTombstoned(const Slice& slice);

 private:
  // Consume the timestamp portion of the slice assuming the beginning of the slice points to
  // the timestamp.
  PrimitiveValue primitive_value_;
  ValueControlFields control_fields_;
};

bool IsFullRowValue(Slice value);

}  // namespace yb::dockv
