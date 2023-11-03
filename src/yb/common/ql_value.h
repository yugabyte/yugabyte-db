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
//
// This file contains the QLValue class that represents QL values.

#pragma once

#include <stdint.h>

#include "yb/util/logging.h"

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/ql_datatype.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/timestamp.h"
#include "yb/util/uuid.h"
#include "yb/util/varint.h"

// The list of unsupported datatypes to use in switch statements
#define QL_UNSUPPORTED_TYPES_IN_SWITCH \
  case DataType::NULL_VALUE_TYPE: FALLTHROUGH_INTENDED; \
  case DataType::TYPEARGS: FALLTHROUGH_INTENDED;  \
  case DataType::UNKNOWN_DATA

#define QL_INVALID_TYPES_IN_SWITCH     \
  case DataType::UINT8:  FALLTHROUGH_INTENDED;    \
  case DataType::UINT16: FALLTHROUGH_INTENDED;    \
  case DataType::UINT32: FALLTHROUGH_INTENDED;    \
  case DataType::UINT64: FALLTHROUGH_INTENDED;    \
  case DataType::GIN_NULL

namespace yb {

//--------------------------------------------------------------------------------------------------
void AppendToKey(const QLValuePB &value_pb, std::string *bytes);
void AppendToKey(const LWQLValuePB &value_pb, std::string *bytes);

//--------------------------------------------------------------------------------------------------
// An abstract class that defines a QL value interface to support different implementations
// for in-memory / serialization trade-offs.
class QLValue {
 public:
  // Shared_ptr.
  typedef std::shared_ptr<QLValue> SharedPtr;

  // Constructors & destructors.
  QLValue() { }
  explicit QLValue(const QLValuePB& pb) : pb_(pb) { }
  explicit QLValue(QLValuePB&& pb) : pb_(std::move(pb)) { }

  //-----------------------------------------------------------------------------------------
  // Access functions to value and type.
  InternalType type() const { return pb_.value_case(); }
  InternalType value_case() const { return pb_.value_case(); }

  const QLValuePB& value() const { return pb_; }
  QLValuePB* mutable_value() { return &pb_; }

  //------------------------------------ Nullness methods -----------------------------------
  // Is the value null?
  static bool IsNull(const QLValuePB& pb) { return pb.value_case() == QLValuePB::VALUE_NOT_SET; }
  bool IsNull() const { return IsNull(pb_); }
  // Set the value to null by clearing all existing values.
  void SetNull() { pb_.Clear(); }

  //-------------------------------- virtual value methods ----------------------------------
  static bool IsVirtual(const QLValuePB& pb) {
    return pb.value_case() == QLValuePB::kVirtualValue;
  }
  bool IsVirtual() const {
    return type() == QLValuePB::kVirtualValue;
  }
  static bool IsMax(const QLValuePB& pb) {
    return IsVirtual(pb) && pb.virtual_value() == QLVirtualValuePB::LIMIT_MAX;
  }
  static bool IsMin(const QLValuePB& pb) {
    return IsVirtual(pb) && pb.virtual_value() == QLVirtualValuePB::LIMIT_MIN;
  }
  bool IsMax() const { return IsMax(pb_); }
  bool IsMin() const { return IsMin(pb_); }
  void SetMax() { pb_.set_virtual_value(QLVirtualValuePB::LIMIT_MAX); }
  void SetMin() { pb_.set_virtual_value(QLVirtualValuePB::LIMIT_MIN); }

  //----------------------------------- get value methods -----------------------------------
  // Get different datatype values. CHECK failure will result if the value stored is not of the
  // expected datatype or the value is null.

  #define QLVALUE_PRIMITIVE_GETTER(name) \
  template <class PB> \
  static auto BOOST_PP_CAT(name, _value)(const PB& pb) -> \
      decltype(pb.BOOST_PP_CAT(name, _value)()) { \
    CHECK(pb.BOOST_PP_CAT(has_, BOOST_PP_CAT(name, _value))()) \
        << "Value: " << pb.ShortDebugString(); \
    return pb.BOOST_PP_CAT(name, _value)(); \
  } \
  \
  auto BOOST_PP_CAT(name, _value)() const -> \
      decltype(BOOST_PP_CAT(name, _value)(std::declval<QLValuePB>())) { \
    return BOOST_PP_CAT(name, _value)(pb_); \
  } \

  QLVALUE_PRIMITIVE_GETTER(int8);
  QLVALUE_PRIMITIVE_GETTER(int16);
  QLVALUE_PRIMITIVE_GETTER(int32);
  QLVALUE_PRIMITIVE_GETTER(int64);
  QLVALUE_PRIMITIVE_GETTER(uint32);
  QLVALUE_PRIMITIVE_GETTER(uint64);
  QLVALUE_PRIMITIVE_GETTER(float);
  QLVALUE_PRIMITIVE_GETTER(double);
  QLVALUE_PRIMITIVE_GETTER(bool);
  QLVALUE_PRIMITIVE_GETTER(decimal);
  QLVALUE_PRIMITIVE_GETTER(string);
  QLVALUE_PRIMITIVE_GETTER(binary);
  QLVALUE_PRIMITIVE_GETTER(jsonb);
  QLVALUE_PRIMITIVE_GETTER(date);
  QLVALUE_PRIMITIVE_GETTER(time);
  QLVALUE_PRIMITIVE_GETTER(map);
  QLVALUE_PRIMITIVE_GETTER(set);
  QLVALUE_PRIMITIVE_GETTER(list);
  QLVALUE_PRIMITIVE_GETTER(frozen);
  QLVALUE_PRIMITIVE_GETTER(gin_null);
  QLVALUE_PRIMITIVE_GETTER(tuple);
  #undef QLVALUE_PRIMITIVE_GETTER

  static Timestamp timestamp_value(const QLValuePB& pb);
  static Timestamp timestamp_value(const LWQLValuePB& pb);
  static Timestamp timestamp_value(const QLValue& value);

  template <class PB>
  static int64_t timestamp_value_pb(const PB& pb) {
    // Caller of this function should already read and know the PB value type before calling.
    CHECK(pb.has_timestamp_value()) << "Value: " << pb.ShortDebugString();
    return pb.timestamp_value();
  }

  Timestamp timestamp_value() const {
    return timestamp_value(pb_);
  }

  int64_t timestamp_value_pb() const {
    return timestamp_value_pb(pb_);
  }

  template <class PB>
  static Slice inetaddress_value_pb(const PB& pb) {
    CHECK(pb.has_inetaddress_value()) << "Value: " << pb.ShortDebugString();
    return pb.inetaddress_value();
  }

  static InetAddress inetaddress_value(const QLValuePB& pb);
  static InetAddress inetaddress_value(const LWQLValuePB& pb);

  static InetAddress inetaddress_value(const QLValue& pb) {
    return inetaddress_value(pb.value());
  }

  Slice inetaddress_value_pb() const {
    return inetaddress_value_pb(pb_);
  }

  InetAddress inetaddress_value() const {
    return inetaddress_value(pb_);
  }

  template <class PB>
  static auto uuid_value_pb(const PB& pb) -> decltype(pb.uuid_value()) {
    CHECK(pb.has_uuid_value()) << "Value: " << pb.ShortDebugString();
    return pb.uuid_value();
  }

  static Uuid uuid_value(const QLValuePB& pb);
  static Uuid uuid_value(const LWQLValuePB& pb);

  static Uuid uuid_value(const QLValue& value) {
    return uuid_value(value.value());
  }

  const std::string& uuid_value_pb() const {
    return uuid_value_pb(pb_);
  }
  Uuid uuid_value() const {
    return uuid_value(pb_);
  }

  template <class PB>
  static auto timeuuid_value_pb(const PB& pb) -> decltype(pb.timeuuid_value()) {
    // Caller of this function should already read and know the PB value type before calling.
    DCHECK(pb.has_timeuuid_value()) << "Value: " << pb.ShortDebugString();
    return pb.timeuuid_value();
  }

  static Uuid timeuuid_value(const QLValue& pb) {
    return timeuuid_value(pb.value());
  }

  static Uuid timeuuid_value(const QLValuePB& pb);
  static Uuid timeuuid_value(const LWQLValuePB& pb);

  const std::string& timeuuid_value_pb() const {
    return timeuuid_value_pb(pb_);
  }

  Uuid timeuuid_value() const {
    return timeuuid_value(pb_);
  }

  static VarInt varint_value(const QLValuePB& pb);
  static VarInt varint_value(const LWQLValuePB& pb);

  static VarInt varint_value(const QLValue& value) {
    return varint_value(value.pb_);
  }

  VarInt varint_value() const {
    return varint_value(pb_);
  }

  void AppendToKeyBytes(std::string *bytes) const {
    AppendToKey(pb_, bytes);
  }

  //----------------------------------- set value methods -----------------------------------
  // Set different datatype values.
  void set_int8_value(int8_t val) {
    pb_.set_int8_value(val);
  }
  void set_int16_value(int16_t val) {
    pb_.set_int16_value(val);
  }
  void set_int32_value(int32_t val) {
    pb_.set_int32_value(val);
  }
  void set_int64_value(int64_t val) {
    pb_.set_int64_value(val);
  }
  void set_uint32_value(uint32_t val) {
    pb_.set_uint32_value(val);
  }
  void set_uint64_value(uint64_t val) {
    pb_.set_uint64_value(val);
  }
  void set_float_value(float val) {
    pb_.set_float_value(val);
  }
  void set_double_value(double val) {
    pb_.set_double_value(val);
  }
  // val is a serialized binary representation of a decimal, not a plaintext
  void set_decimal_value(const std::string& val) {
    pb_.set_decimal_value(val);
  }
  // val is a serialized binary representation of a decimal, not a plaintext
  void set_decimal_value(std::string&& val) {
    pb_.set_decimal_value(std::move(val));
  }
  void set_jsonb_value(std::string&& val) {
    pb_.set_jsonb_value(std::move(val));
  }
  void set_jsonb_value(const std::string& val) {
    pb_.set_jsonb_value(val);
  }
  void set_jsonb_value(const void* value, size_t size) {
    pb_.set_jsonb_value(value, size);
  }
  void set_bool_value(bool val) {
    pb_.set_bool_value(val);
  }
  void set_string_value(const std::string& val) {
    pb_.set_string_value(val);
  }
  void set_string_value(std::string&& val) {
    pb_.set_string_value(std::move(val));
  }
  void set_string_value(const char* val) {
    pb_.set_string_value(val);
  }
  void set_string_value(const char* val, size_t size) {
    pb_.set_string_value(val, size);
  }
  void set_timestamp_value(const Timestamp& val) {
    pb_.set_timestamp_value(val.ToInt64());
  }
  void set_timestamp_value(int64_t val) {
    pb_.set_timestamp_value(val);
  }
  void set_date_value(uint32_t val) {
    pb_.set_date_value(val);
  }
  void set_time_value(int64_t val) {
    pb_.set_time_value(val);
  }
  void set_binary_value(const std::string& val) {
    pb_.set_binary_value(val);
  }
  void set_binary_value(std::string&& val) {
    pb_.set_binary_value(std::move(val));
  }
  void set_binary_value(const void* val, size_t size) {
    pb_.set_binary_value(val, size);
  }

  static QLValuePB Primitive(const std::string& str);

  static QLValuePB Primitive(double value);

  static QLValuePB Primitive(int32_t value);

  static QLValuePB Primitive(int64_t value) {
    return PrimitiveInt64(value);
  }

  static QLValuePB PrimitiveInt64(int64_t value);

  static void AppendPrimitiveArray(QLValuePB* arr) {
  }

  template <class Val, class... Args>
  static void AppendPrimitiveArray(QLValuePB* arr, const Val& val, Args&&... args) {
    *arr->mutable_list_value()->mutable_elems()->Add() = Primitive(val);
    AppendPrimitiveArray(arr, std::forward<Args>(args)...);
  }

  template <class... Args>
  static QLValuePB PrimitiveArray(Args&&... args) {
    QLValuePB result;
    AppendPrimitiveArray(&result, std::forward<Args>(args)...);
    return result;
  }

  static void set_value(const QLValue& src, QLValuePB* dest) {
    set_value(src.value(), dest);
  }

  static void set_value(const QLValuePB& src, QLValuePB* dest) {
    *dest = src;
  }

  static void set_inetaddress_value(const InetAddress& val, QLValuePB* pb);

  void set_inetaddress_value(const InetAddress& val) {
    set_inetaddress_value(val, &pb_);
  }

  static void set_uuid_value(const Uuid& val, QLValuePB* out) {
    val.ToBytes(out->mutable_uuid_value());
  }

  static void set_uuid_value(const Uuid& val, QLValue* out) {
    set_uuid_value(val, out->mutable_value());
  }

  void set_uuid_value(const Uuid& val) {
    set_uuid_value(val, &pb_);
  }

  void set_timeuuid_value(const Uuid& val);

  static void set_timeuuid_value(const Uuid& val, LWQLValuePB* out);
  static void set_timeuuid_value(const Uuid& val, QLValuePB* out);
  static void set_timeuuid_value(const Uuid& val, QLValue* out);

  void set_varint_value(const VarInt& val) {
    pb_.set_varint_value(val.EncodeToComparable());
  }

  void set_varint_value(const std::string& encoded_val) {
    pb_.set_varint_value(encoded_val);
  }

  void set_gin_null_value(uint8_t val) {
    pb_.set_gin_null_value(val);
  }

  //--------------------------------- mutable value methods ----------------------------------
  std::string* mutable_decimal_value() {
    return pb_.mutable_decimal_value();
  }
  std::string* mutable_jsonb_value() {
    return pb_.mutable_jsonb_value();
  }
  std::string* mutable_varint_value() {
    return pb_.mutable_varint_value();
  }
  std::string* mutable_string_value() {
    return pb_.mutable_string_value();
  }
  std::string* mutable_binary_value() {
    return pb_.mutable_binary_value();
  }
  QLMapValuePB* mutable_map_value() {
    return pb_.mutable_map_value();
  }
  QLSeqValuePB* mutable_set_value() {
    return pb_.mutable_set_value();
  }
  QLSeqValuePB* mutable_list_value() {
    return pb_.mutable_list_value();
  }
  QLSeqValuePB* mutable_frozen_value() {
    return pb_.mutable_frozen_value();
  }

  // To extend/construct collections we return freshly allocated elements for the caller to set.
  QLValuePB* add_map_key() {
    return pb_.mutable_map_value()->add_keys();
  }
  QLValuePB* add_map_value() {
    return pb_.mutable_map_value()->add_values();
  }
  QLValuePB* add_set_elem() {
    return pb_.mutable_set_value()->add_elems();
  }
  QLValuePB* add_list_elem() {
    return pb_.mutable_list_value()->add_elems();
  }
  QLValuePB* add_frozen_elem() {
    return pb_.mutable_frozen_value()->add_elems();
  }
  QLValuePB* add_tuple_elem() {
    return pb_.mutable_tuple_value()->add_elems();
  }

  // For collections, the call to `mutable_foo` takes care of setting the correct type to `foo`
  // internally and allocating the message if needed
  // TODO(neil) Change these set to "mutable_xxx_value()".
  void set_map_value() {
    pb_.mutable_map_value();
  }
  void set_set_value() {
    pb_.mutable_set_value();
  }
  void set_list_value() {
    pb_.mutable_list_value();
  }
  void set_frozen_value() {
    pb_.mutable_frozen_value();
  }
  void set_tuple_value() { pb_.mutable_tuple_value(); }

  //----------------------------------- assignment methods ----------------------------------
  QLValue& operator=(const QLValuePB& other) {
    pb_ = other;
    return *this;
  }
  QLValue& operator=(QLValuePB&& other) {
    pb_ = std::move(other);
    return *this;
  }

  //----------------------------------- comparison methods -----------------------------------
  bool Comparable(const QLValue& other) const {
    return type() == other.type() || EitherIsNull(other) || EitherIsVirtual(other);
  }
  bool BothNotNull(const QLValue& other) const {
    return !IsNull() && !other.IsNull();
  }
  bool BothNull(const QLValue& other) const {
    return IsNull() && other.IsNull();
  }
  bool EitherIsNull(const QLValue& other) const {
    return IsNull() || other.IsNull();
  }
  bool EitherIsVirtual(const QLValue& other) const {
    return IsVirtual() || other.IsVirtual();
  }

  int CompareTo(const QLValue& other) const;

  // In YCQL null is not comparable with regular values (w.r.t. ordering).
  bool operator <(const QLValue& v) const {
    return BothNotNull(v) && CompareTo(v) < 0;
  }
  bool operator >(const QLValue& v) const {
    return BothNotNull(v) && CompareTo(v) > 0;
  }

  // In YCQL equality holds for null values.
  bool operator <=(const QLValue& v) const {
    return (BothNotNull(v) && CompareTo(v) <= 0) || BothNull(v);
  }
  bool operator >=(const QLValue& v) const {
    return (BothNotNull(v) && CompareTo(v) >= 0) || BothNull(v);
  }
  bool operator ==(const QLValue& v) const {
    return (BothNotNull(v) && CompareTo(v) == 0) || BothNull(v);
  }
  bool operator !=(const QLValue& v) const {
    return !(*this == v);
  }

  //----------------------------- serializer / deserializer ---------------------------------
  Status Deserialize(const std::shared_ptr<QLType>& ql_type,
                     const QLClient& client,
                     Slice* data);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToValueString(const QuotesType quotes_type = QuotesType::kDoubleQuotes) const;
  std::string ToString() const;

 private:
  // Deserialize a CQL number (8, 16, 32 and 64-bit integer). <num_type> is the parsed integer type.
  // <converter> converts the number from network byte-order to machine order and <data_type>
  // is the coverter's return type. The converter's return type <data_type> is unsigned while
  // <num_type> may be signed or unsigned. <setter> sets the value in QLValue.
  template<typename num_type, typename data_type>
  Status CQLDeserializeNum(
      size_t len, data_type (*converter)(const void*), void (QLValue::*setter)(num_type),
      Slice* data);

  // Deserialize a CQL floating point number (float or double). <float_type> is the parsed floating
  // point type. <converter> converts the number from network byte-order to machine order and
  // <data_type> is the coverter's return type. The converter's return type <data_type> is an
  // integer type. <setter> sets the value in QLValue.
  template<typename float_type, typename data_type>
  Status CQLDeserializeFloat(
      size_t len, data_type (*converter)(const void*), void (QLValue::*setter)(float_type),
      Slice* data);

  // TODO(neil) This should be changed to shared_ptr<QLValuePB>. That way, we assign the pointers
  // instead of copying the same value many times during expression evaluation.
  // Protobuf value.
  QLValuePB pb_;
};

//--------------------------------------------------------------------------------------------------
// QLValuePB operators
bool operator <(const QLValuePB& lhs, const QLValuePB& rhs);
bool operator >(const QLValuePB& lhs, const QLValuePB& rhs);
bool operator <=(const QLValuePB& lhs, const QLValuePB& rhs);
bool operator >=(const QLValuePB& lhs, const QLValuePB& rhs);
bool operator ==(const QLValuePB& lhs, const QLValuePB& rhs);
bool operator !=(const QLValuePB& lhs, const QLValuePB& rhs);

bool operator <(const QLValuePB& lhs, const QLValue& rhs);
bool operator >(const QLValuePB& lhs, const QLValue& rhs);
bool operator <=(const QLValuePB& lhs, const QLValue& rhs);
bool operator >=(const QLValuePB& lhs, const QLValue& rhs);
bool operator ==(const QLValuePB& lhs, const QLValue& rhs);
bool operator !=(const QLValuePB& lhs, const QLValue& rhs);

bool operator <(const LWQLValuePB& lhs, const LWQLValuePB& rhs);
bool operator >(const LWQLValuePB& lhs, const LWQLValuePB& rhs);
bool operator <=(const LWQLValuePB& lhs, const LWQLValuePB& rhs);
bool operator >=(const LWQLValuePB& lhs, const LWQLValuePB& rhs);
bool operator ==(const LWQLValuePB& lhs, const LWQLValuePB& rhs);
bool operator !=(const LWQLValuePB& lhs, const LWQLValuePB& rhs);

InternalType type(const QLValuePB& v);

bool IsNull(const QLValuePB& v);

inline bool IsNull(const QLValue& v) {
  return IsNull(v.value());
}

void SetNull(QLValuePB* v);
void SetNull(LWQLValuePB* v);

inline void SetNull(QLValue* v) {
  SetNull(v->mutable_value());
}

bool EitherIsNull(const QLValuePB& lhs, const QLValuePB& rhs);
bool BothNotNull(const QLValuePB& lhs, const QLValuePB& rhs);
bool BothNull(const QLValuePB& lhs, const QLValuePB& rhs);
bool Comparable(const QLValuePB& lhs, const QLValuePB& rhs);
bool Comparable(const LWQLValuePB& lhs, const LWQLValuePB& rhs);
int Compare(const QLValuePB& lhs, const QLValuePB& rhs);
bool EitherIsNull(const QLValuePB& lhs, const QLValue& rhs);
bool Comparable(const QLValuePB& lhs, const QLValue& rhs);
bool BothNotNull(const QLValuePB& lhs, const QLValue& rhs);
bool BothNull(const QLValuePB& lhs, const QLValue& rhs);
int Compare(const QLValuePB& lhs, const QLValue& rhs);
int Compare(const QLSeqValuePB& lhs, const QLSeqValuePB& rhs);
int Compare(const bool lhs, const bool rhs);

bool IsNull(const LWQLValuePB& v);

inline void AppendToKey(const QLValue &value_pb, std::string *bytes) {
  AppendToKey(value_pb.value(), bytes);
}

void ConcatStrings(const std::string& lhs, const std::string& rhs, QLValuePB* result);
void ConcatStrings(const std::string& lhs, const std::string& rhs, QLValue* result);
void ConcatStrings(const Slice& lhs, const Slice& rhs, LWQLValuePB* result);

#define YB_SET_INT_VALUE(ql_valuepb, input, bits) \
  case DataType::BOOST_PP_CAT(INT, bits): { \
    auto value = CheckedStoInt<BOOST_PP_CAT(BOOST_PP_CAT(int, bits), _t)>(input); \
    RETURN_NOT_OK(value); \
    ql_valuepb->BOOST_PP_CAT(BOOST_PP_CAT(set_int, bits), _value)(*value); \
  } break;

} // namespace yb
