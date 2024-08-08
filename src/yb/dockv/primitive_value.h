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

#include "yb/util/algorithm_util.h"
#include "yb/util/kv_util.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/timestamp.h"
#include "yb/util/uuid.h"

#include "yb/dockv/key_entry_value.h"

namespace yb::dockv {

// Used for extending a list.
// PREPEND prepends the arguments one by one (PREPEND a b c) will prepend [c b a] to the list,
// while PREPEND_BLOCK prepends the arguments together, so it will prepend [a b c] to the list.
YB_DEFINE_ENUM(ListExtendOrder, (APPEND)(PREPEND_BLOCK)(PREPEND))

// A necessary use of a forward declaration to avoid circular inclusion.
class SubDocument;

using FloatVector = std::vector<float>;
using UInt64Vector = std::vector<uint64_t>;

class PrimitiveValue {
 public:
  static const PrimitiveValue kInvalid;
  static const PrimitiveValue kTombstone;
  static const PrimitiveValue kObject;
  static const PrimitiveValue kNull;

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
  static PrimitiveValue FromQLValuePB(const QLValuePB& value);

  static PrimitiveValue FromQLValuePB(const LWQLValuePB& value);

  // Set a primitive value in a QLValuePB.
  void ToQLValuePB(const std::shared_ptr<QLType>& ql_type, QLValuePB* ql_val) const;

  ValueEntryType value_type() const { return type_; }
  ValueEntryType type() const { return type_; }

  // Convert this value to a human-readable string for logging / debugging.
  std::string ToString(bool render_options = false) const;

  ~PrimitiveValue();

  // Decodes a primitive value from the given slice representing a RocksDB value in our value
  // encoding format. Expects the entire slice to be consumed and returns an error otherwise.
  Status DecodeFromValue(const Slice& rocksdb_slice);

  static Status DecodeToQLValuePB(const Slice& input, DataType data_type, QLValuePB* out);

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

  bool IsTombstone() const;

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

  bool GetBoolean() const;

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

  static void AppendEncodedTo(const FloatVector& v, ValueBuffer& out);
  static void AppendEncodedTo(const UInt64Vector& v, ValueBuffer& out);

  template <class T>
  static ValueBuffer Encoded(const T& t) {
    ValueBuffer value;
    AppendEncodedTo(t, value);
    return value;
  }

  static Slice NullSlice();
  static Slice TombstoneSlice();

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

  std::string ValueToString() const;

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
    FloatVector* float_vector_;
    UInt64Vector* uint64_vector_;
  };

 private:
  template <class PB>
  static PrimitiveValue DoFromQLValuePB(const PB& value);

  template <class Vector, class Reader>
  Status DecodeVector(
      Slice slice, ValueEntryType value_type, Vector*& vector, const Reader& reader);

  template <class Vector, class Writer>
  static void AppendEncodedVector(
      ValueEntryType value_type, const Vector& v, ValueBuffer& out, const Writer& writer);

  // This is used in both the move constructor and the move assignment operator. Assumes this object
  // has not been constructed, or that the destructor has just been called.
  void MoveFrom(PrimitiveValue* other);
};

inline std::ostream& operator<<(std::ostream& out, const PrimitiveValue& primitive_value) {
  out << primitive_value.ToString();
  return out;
}

inline std::ostream& operator<<(std::ostream& out, const SortOrder sort_order) {
  std::string sort_order_name = sort_order == SortOrder::kAscending ? "kAscending" : "kDescending";
  out << sort_order_name;
  return out;
}

// Converts a SortingType to its SortOrder equivalent.
// SortingType::kAscending and SortingType::kNotSpecified get
// converted to SortOrder::kAscending.
// SortingType::kDescending gets converted to SortOrder::kDescending.
SortOrder SortOrderFromColumnSchemaSortingType(SortingType sorting_type);

void AppendEncodedValue(const QLValuePB& value, ValueBuffer* out);
void AppendEncodedValue(const QLValuePB& value, std::string* out);
void AppendEncodedValue(const LWQLValuePB& value, ValueBuffer* out);
size_t EncodedValueSize(const QLValuePB& value);
size_t EncodedValueSize(const LWQLValuePB& value);

}  // namespace yb::dockv
