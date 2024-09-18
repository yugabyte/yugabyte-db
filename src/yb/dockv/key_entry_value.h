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

#include <ostream>
#include <string>
#include <vector>

#include "yb/util/logging.h"

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/ql_datatype.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/gutil/integral_types.h"
#include "yb/util/algorithm_util.h"
#include "yb/util/kv_util.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/timestamp.h"
#include "yb/util/uuid.h"

namespace yb::dockv {

class KeyEntryValue;
using FrozenContainer = KeyEntryValues;

enum class SystemColumnIds : ColumnIdRep {
  kLivenessColumn = 0  // Stores the TTL for QL rows inserted using an INSERT statement.
};

class KeyEntryValue {
 public:
  static const KeyEntryValue kLivenessColumn;

  using Type = KeyEntryType;

  KeyEntryValue();
  explicit KeyEntryValue(KeyEntryType type);
  ~KeyEntryValue();

  KeyEntryValue(const KeyEntryValue& other);
  KeyEntryValue(KeyEntryValue&& other);

  explicit KeyEntryValue(
      const Slice& str, SortOrder sort_order = SortOrder::kAscending,
      bool is_collate = false);

  explicit KeyEntryValue(const DocHybridTime& hybrid_time);
  explicit KeyEntryValue(const HybridTime& hybrid_time);

  KeyEntryValue& operator =(const KeyEntryValue& other);
  KeyEntryValue& operator =(KeyEntryValue&& other);

  KeyEntryType type() const {
    return type_;
  }

  void AppendToKey(KeyBytes* key_bytes) const;
  KeyBytes ToKeyBytes() const;

  // Return non-zero encoded value size if provided datatype has fixed encoded value size, otherwise
  // return 0.
  static size_t GetEncodedKeyEntryValueSize(DataType data_type);

  std::string ToString(AutoDecodeKeys auto_decode_keys = AutoDecodeKeys::kFalse) const;

  int CompareTo(const KeyEntryValue& other) const;

  bool IsInfinity() const;

  // Decodes a primitive value from the given slice representing a RocksDB key in our key encoding
  // format and consumes a prefix of the slice.
  static Status DecodeKey(Slice* slice, KeyEntryValue* out);

  Status DecodeFromKey(Slice* slice);
  static Result<KeyEntryValue> FullyDecodeFromKey(const Slice& slice);

  void ToQLValuePB(const std::shared_ptr<QLType>& ql_type, QLValuePB* ql_val) const;

  bool IsString() const;
  bool IsInt32() const;
  bool IsUInt16Hash() const;
  bool IsInt64() const;
  bool IsFloat() const;
  bool IsDouble() const;
  bool IsColumnId() const;
  bool IsUuid() const;
  bool IsInetAddress() const;
  bool IsFrozen() const;
  bool IsUInt32() const;
  bool IsUInt64() const;
  bool IsDecimal() const;
  bool IsVarInt() const;
  bool IsTimestamp() const;

  const std::string& GetString() const;
  uint16_t GetUInt16Hash() const;
  int32_t GetInt32() const;
  int64_t GetInt64() const;
  float GetFloat() const;
  double GetDouble() const;
  ColumnId GetColumnId() const;
  uint8_t GetGinNull() const;
  uint32_t GetUInt32() const;
  uint64_t GetUInt64() const;
  const Uuid& GetUuid() const;
  const InetAddress& GetInetAddress() const;
  const std::string& GetDecimal() const;
  const std::string& GetVarInt() const;
  Timestamp GetTimestamp() const;
  const FrozenContainer& GetFrozen() const;

  static KeyEntryValue NullValue(SortingType sorting_type);

  static KeyEntryValue FromQLValuePB(const QLValuePB& value, SortingType sorting_type);
  static KeyEntryValue FromQLValuePBForKey(const QLValuePB& value, SortingType sorting_type);
  static KeyEntryValue FromQLValuePB(const LWQLValuePB& value, SortingType sorting_type);
  static KeyEntryValue FromQLValuePBForKey(const LWQLValuePB& value, SortingType sorting_type);
  static KeyEntryValue FromQLVirtualValue(QLVirtualValuePB value);

  static KeyEntryValue Double(double d, SortOrder sort_order = SortOrder::kAscending);
  static KeyEntryValue Float(float f, SortOrder sort_order = SortOrder::kAscending);
  // decimal_str represents a human readable string representing the decimal number, e.g. "0.03".
  static KeyEntryValue Decimal(const Slice& decimal_str, SortOrder sort_order);
  static KeyEntryValue VarInt(const Slice& varint_str, SortOrder sort_order);
  static KeyEntryValue ArrayIndex(int64_t index);
  static KeyEntryValue UInt16Hash(uint16_t hash);
  static KeyEntryValue MakeColumnId(ColumnId column_id);
  static KeyEntryValue SystemColumnId(ColumnId column_id);
  static KeyEntryValue SystemColumnId(SystemColumnIds system_column_id);
  static KeyEntryValue Int32(int32_t v, SortOrder sort_order = SortOrder::kAscending);
  static KeyEntryValue UInt32(uint32_t v, SortOrder sort_order = SortOrder::kAscending);
  static KeyEntryValue Int64(int64_t v, SortOrder sort_order = SortOrder::kAscending);
  static KeyEntryValue UInt64(uint64_t v, SortOrder sort_order = SortOrder::kAscending);
  static KeyEntryValue VectorVertexId(uint64_t v);
  static KeyEntryValue MakeTimestamp(
      const Timestamp& timestamp, SortOrder sort_order = SortOrder::kAscending);
  static KeyEntryValue MakeInetAddress(
      const InetAddress& value, SortOrder sort_order = SortOrder::kAscending);
  static KeyEntryValue MakeUuid(const Uuid& value, SortOrder sort_order = SortOrder::kAscending);
  static KeyEntryValue GinNull(uint8_t v);

  static KeyEntryValue Create(int64_t value) {
    return KeyEntryValue::Int64(value);
  }

  static KeyEntryValue Create(int32_t value) {
    return KeyEntryValue::Int32(value);
  }

  static KeyEntryValue Create(const std::string& value) {
    return KeyEntryValue(value);
  }

 private:
  friend bool operator==(const KeyEntryValue& lhs, const KeyEntryValue& rhs);

  template <class PB>
  static KeyEntryValue DoFromQLValuePB(const PB& value, SortingType sorting_type);

  KeyEntryType type_;

  bool IsStoredAsString() const;
  void Destroy();

  union {
    int32_t int32_val_;
    uint32_t uint32_val_;
    int64_t int64_val_;
    uint64_t uint64_val_;
    uint16_t uint16_val_;
    DocHybridTime hybrid_time_val_;
    std::string str_val_;
    float float_val_;
    double double_val_;
    Timestamp timestamp_val_;
    InetAddress* inetaddress_val_;
    Uuid uuid_val_;
    FrozenContainer* frozen_val_;
    ColumnId column_id_val_;
    uint8_t gin_null_val_;
  };
};

bool operator==(const KeyEntryValue& lhs, const KeyEntryValue& rhs);

inline bool operator!=(const KeyEntryValue& lhs, const KeyEntryValue& rhs) {
  return !(lhs == rhs);
}

inline bool operator<(const KeyEntryValue& lhs, const KeyEntryValue& rhs) {
  return lhs.CompareTo(rhs) < 0;
}

inline bool operator<=(const KeyEntryValue& lhs, const KeyEntryValue& rhs) {
  return lhs.CompareTo(rhs) <= 0;
}

inline bool operator>(const KeyEntryValue& lhs, const KeyEntryValue& rhs) {
  return lhs.CompareTo(rhs) > 0;
}

inline bool operator>=(const KeyEntryValue& lhs, const KeyEntryValue& rhs) {
  return lhs.CompareTo(rhs) >= 0;
}

inline std::ostream& operator<<(std::ostream& out, const KeyEntryValue& rhs) {
  return out << rhs.ToString();
}

// A variadic template utility for creating vectors with PrimitiveValue elements out of arbitrary
// sequences of arguments of supported types.
inline void AppendKeyEntryValues(KeyEntryValues* dest) {}

template <class T, class ...U>
inline void AppendKeyEntryValues(KeyEntryValues* dest,
                                  T first_arg,
                                  U... more_args) {
  dest->push_back(KeyEntryValue::Create(first_arg));
  AppendKeyEntryValues(dest, more_args...);
}

template <class ...T>
inline KeyEntryValues MakeKeyEntryValues(T... args) {
  KeyEntryValues v;
  AppendKeyEntryValues(&v, args...);
  return v;
}

}  // namespace yb::dockv
