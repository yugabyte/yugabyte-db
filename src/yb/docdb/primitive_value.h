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

#ifndef YB_DOCDB_PRIMITIVE_VALUE_H_
#define YB_DOCDB_PRIMITIVE_VALUE_H_

#include <ostream>
#include <string>
#include <vector>

#include <glog/logging.h>

#include "yb/common/common_fwd.h"
#include "yb/common/column_id.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/util/algorithm_util.h"
#include "yb/util/kv_util.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/timestamp.h"
#include "yb/util/uuid.h"

namespace yb {
namespace docdb {

// Used for extending a list.
// PREPEND prepends the arguments one by one (PREPEND a b c) will prepend [c b a] to the list,
// while PREPEND_BLOCK prepends the arguments together, so it will prepend [a b c] to the list.
YB_DEFINE_ENUM(ListExtendOrder, (APPEND)(PREPEND_BLOCK)(PREPEND))

YB_STRONGLY_TYPED_BOOL(CheckIsCollate);

// A necessary use of a forward declaration to avoid circular inclusion.
class SubDocument;

enum class SystemColumnIds : ColumnIdRep {
  kLivenessColumn = 0  // Stores the TTL for QL rows inserted using an INSERT statement.
};

using FrozenContainer = std::vector<KeyEntryValue>;

class PrimitiveValue {
 public:
  static const PrimitiveValue kInvalid;
  static const PrimitiveValue kTombstone;
  static const PrimitiveValue kObject;

  using Type = ValueEntryType;

  PrimitiveValue();
  explicit PrimitiveValue(ValueEntryType key_entry_type);

  PrimitiveValue(const PrimitiveValue& other);

  PrimitiveValue(PrimitiveValue&& other) {
    MoveFrom(&other);
  }

  PrimitiveValue& operator =(const PrimitiveValue& other) {
    this->~PrimitiveValue();
    new(this) PrimitiveValue(other);
    return *this;
  }

  PrimitiveValue& operator =(PrimitiveValue&& other) {
    this->~PrimitiveValue();
    MoveFrom(&other);
    return *this;
  }

  explicit PrimitiveValue(const Slice& s, bool is_collate = false);

  explicit PrimitiveValue(const std::string& s, bool is_collate = false);

  explicit PrimitiveValue(const char* s, bool is_collate = false);

  explicit PrimitiveValue(const Timestamp& timestamp);

  explicit PrimitiveValue(const InetAddress& inetaddress);

  explicit PrimitiveValue(const Uuid& uuid);

  // Construct a primitive value from a QLValuePB.
  static PrimitiveValue FromQLValuePB(
      const QLValuePB& value, CheckIsCollate check_is_collate = CheckIsCollate::kTrue);

  static PrimitiveValue FromQLValuePB(
      const LWQLValuePB& value, CheckIsCollate check_is_collate = CheckIsCollate::kTrue);

  // Set a primitive value in a QLValuePB.
  void ToQLValuePB(const std::shared_ptr<QLType>& ql_type, QLValuePB* ql_val) const;

  ValueEntryType value_type() const { return type_; }
  ValueEntryType type() const { return type_; }

  // Convert this value to a human-readable string for logging / debugging.
  std::string ToString() const;

  ~PrimitiveValue();

  // Decodes a primitive value from the given slice representing a RocksDB value in our value
  // encoding format. Expects the entire slice to be consumed and returns an error otherwise.
  CHECKED_STATUS DecodeFromValue(const rocksdb::Slice& rocksdb_slice);

  static PrimitiveValue Double(double v);
  static PrimitiveValue Float(float v);
  static PrimitiveValue Decimal(const Slice& decimal_str);
  static PrimitiveValue VarInt(const Slice& varint_str);
  static PrimitiveValue Int32(int32_t v);
  static PrimitiveValue UInt32(uint32_t v);
  static PrimitiveValue Int64(int64_t v);
  static PrimitiveValue UInt64(uint64_t v);
  static PrimitiveValue Jsonb(const Slice& json);
  static PrimitiveValue GinNull(uint8_t v);

  static PrimitiveValue Create(int64_t v) {
    return Int64(v);
  }

  static PrimitiveValue Create(const std::string& v) {
    return PrimitiveValue(v);
  }

  // As strange as it may sound, an instance of this class may sometimes contain a single byte that
  // indicates an empty data structure of a certain type (object, array), or a tombstone. This
  // method can tell whether what's stored here is an actual primitive value.
  bool IsPrimitive() const;

  bool IsTombstoneOrPrimitive() const;

  int CompareTo(const PrimitiveValue& other) const;

  // Assuming this PrimitiveValue represents a string, return a Slice pointing to it.
  // This returns a YB slice, not a RocksDB slice, based on what was needed when this function was
  // implemented. This distinction should go away if we merge RocksDB and YB Slice classes.
  Slice GetStringAsSlice() const {
    DCHECK(IsStoredAsString());
    return Slice(str_val_);
  }

  bool IsInt64() const;

  bool IsStoredAsString() const;
  bool IsString() const;

  bool IsDouble() const;

  const std::string& GetString() const;

  int32_t GetInt32() const;

  uint32_t GetUInt32() const;

  int64_t GetInt64() const;

  uint64_t GetUInt64() const;

  uint16_t GetUInt16() const;

  double GetDouble() const {
    DCHECK(IsDouble());
    return double_val_;
  }

  float GetFloat() const;

  const std::string& GetDecimal() const;

  const std::string& GetVarInt() const;

  Timestamp GetTimestamp() const;

  const InetAddress& GetInetAddress() const;

  const std::string& GetJson() const;

  const Uuid& GetUuid() const;

  uint8_t GetGinNull() const;

  const FrozenContainer& GetFrozen() const;

  bool operator <(const PrimitiveValue& other) const {
    return CompareTo(other) < 0;
  }

  bool operator <=(const PrimitiveValue& other) const {
    return CompareTo(other) <= 0;
  }

  bool operator >(const PrimitiveValue& other) const {
    return CompareTo(other) > 0;
  }

  bool operator >=(const PrimitiveValue& other) const {
    return CompareTo(other) >= 0;
  }

  bool operator==(const PrimitiveValue& other) const;

  bool operator!=(const PrimitiveValue& other) const { return !(*this == other); }

  ListExtendOrder GetExtendOrder() const {
    return extend_order_;
  }

  int64_t GetTtl() const {
    return ttl_seconds_;
  }

  bool IsWriteTimeSet() const {
    return write_time_ != kUninitializedWriteTime;
  }

  int64_t GetWriteTime() const {
    DCHECK_NE(kUninitializedWriteTime, write_time_);
    return write_time_;
  }

  void SetTtl(const int64_t ttl_seconds) {
    ttl_seconds_ = ttl_seconds;
  }

  void SetExtendOrder(const ListExtendOrder extend_order) const {
    extend_order_ = extend_order;
  }

  void SetWriteTime(const int64_t write_time) {
    write_time_ = write_time;
  }

 protected:

  static constexpr int64_t kUninitializedWriteTime = std::numeric_limits<int64_t>::min();

  // Column attributes.
  int64_t ttl_seconds_ = -1;
  int64_t write_time_ = kUninitializedWriteTime;

  // TODO: make PrimitiveValue extend SubDocument and put this field
  // in SubDocument.
  // This field gives the extension order of elements of a list and
  // is applicable only to SubDocuments of type kArray.
  mutable ListExtendOrder extend_order_ = ListExtendOrder::APPEND;

  ValueEntryType type_;

  // TODO: do we have to worry about alignment here?
  union {
    int32_t int32_val_;
    uint32_t uint32_val_;
    int64_t int64_val_;
    uint64_t uint64_val_;
    uint16_t uint16_val_;
    std::string str_val_;
    float float_val_;
    double double_val_;
    Timestamp timestamp_val_;
    InetAddress* inetaddress_val_;
    Uuid uuid_val_;
    FrozenContainer* frozen_val_;
    // This is used in SubDocument to hold a pointer to a map or a vector.
    void* complex_data_structure_;
    uint8_t gin_null_val_;
  };

 private:
  template <class PB>
  static PrimitiveValue DoFromQLValuePB(const PB& value, CheckIsCollate check_is_collate);


  // This is used in both the move constructor and the move assignment operator. Assumes this object
  // has not been constructed, or that the destructor has just been called.
  void MoveFrom(PrimitiveValue* other);
};

inline std::ostream& operator<<(std::ostream& out, const PrimitiveValue& primitive_value) {
  out << primitive_value.ToString();
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const SortOrder sort_order) {
  string sort_order_name = sort_order == SortOrder::kAscending ? "kAscending" : "kDescending";
  out << sort_order_name;
  return out;
}

// Converts a SortingType to its SortOrder equivalent.
// SortingType::kAscending and SortingType::kNotSpecified get
// converted to SortOrder::kAscending.
// SortingType::kDescending gets converted to SortOrder::kDescending.
SortOrder SortOrderFromColumnSchemaSortingType(SortingType sorting_type);

void AppendEncodedValue(const QLValuePB& value, CheckIsCollate check_is_collate, ValueBuffer* out);
void AppendEncodedValue(const QLValuePB& value, CheckIsCollate check_is_collate, std::string* out);

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

  std::string ToString(AutoDecodeKeys auto_decode_keys = AutoDecodeKeys::kFalse) const;

  int CompareTo(const KeyEntryValue& other) const;

  bool IsInfinity() const;

  // Decodes a primitive value from the given slice representing a RocksDB key in our key encoding
  // format and consumes a prefix of the slice.
  static CHECKED_STATUS DecodeKey(Slice* slice, KeyEntryValue* out);

  CHECKED_STATUS DecodeFromKey(Slice* slice);

  void ToQLValuePB(const std::shared_ptr<QLType>& ql_type, QLValuePB* ql_val) const;

  bool IsString() const;
  bool IsInt32() const;
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
  static KeyEntryValue FromQLValuePB(const LWQLValuePB& value, SortingType sorting_type);

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
inline void AppendKeyEntryValues(std::vector<KeyEntryValue>* dest) {}

template <class T, class ...U>
inline void AppendKeyEntryValues(std::vector<KeyEntryValue>* dest,
                                  T first_arg,
                                  U... more_args) {
  dest->push_back(KeyEntryValue::Create(first_arg));
  AppendKeyEntryValues(dest, more_args...);
}

template <class ...T>
inline std::vector<KeyEntryValue> KeyEntryValues(T... args) {
  std::vector<KeyEntryValue> v;
  AppendKeyEntryValues(&v, args...);
  return v;
}

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_PRIMITIVE_VALUE_H_
