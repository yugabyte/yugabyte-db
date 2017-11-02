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

#ifndef YB_COMMON_QL_VALUE_H
#define YB_COMMON_QL_VALUE_H

#include <stdint.h>

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_type.h"
#include "yb/util/decimal.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/timestamp.h"
#include "yb/util/uuid.h"
#include "yb/util/yb_partition.h"

namespace yb {

// An abstract class that defines a QL value interface to support different implementations
// for in-memory / serialization trade-offs.
class QLValue {
 public:
  // The value type.
  typedef QLValuePB::ValueCase InternalType;

  virtual ~QLValue();

  //-----------------------------------------------------------------------------------------
  // Interfaces to be implemented.

  // Return the value's type.
  virtual InternalType type() const = 0;

  //------------------------------------ Nullness methods -----------------------------------
  // Is the value null?
  virtual bool IsNull() const = 0;
  // Set the value to null.
  virtual void SetNull() = 0;

  //----------------------------------- get value methods -----------------------------------
  // Get different datatype values. CHECK failure will result if the value stored is not of the
  // expected datatype or the value is null.
  virtual int8_t int8_value() const = 0;
  virtual int16_t int16_value() const = 0;
  virtual int32_t int32_value() const = 0;
  virtual int64_t int64_value() const = 0;
  virtual float float_value() const = 0;
  virtual double double_value() const = 0;
  virtual const std::string& decimal_value() const = 0;
  virtual bool bool_value() const = 0;
  virtual const std::string& string_value() const = 0;
  virtual Timestamp timestamp_value() const = 0;
  virtual const std::string& binary_value() const = 0;
  virtual InetAddress inetaddress_value() const = 0;
  virtual const QLMapValuePB& map_value() const = 0;
  virtual const QLSeqValuePB& set_value() const = 0;
  virtual const QLSeqValuePB& list_value() const = 0;
  virtual const QLSeqValuePB& frozen_value() const = 0;
  virtual Uuid uuid_value() const = 0;
  virtual Uuid timeuuid_value() const = 0;

  //----------------------------------- set value methods -----------------------------------
  // Set different datatype values.
  virtual void set_int8_value(int8_t val) = 0;
  virtual void set_int16_value(int16_t val) = 0;
  virtual void set_int32_value(int32_t val) = 0;
  virtual void set_int64_value(int64_t val) = 0;
  virtual void set_float_value(float val) = 0;
  virtual void set_double_value(double val) = 0;
  virtual void set_decimal_value(const std::string& val) = 0;
  virtual void set_bool_value(bool val) = 0;
  virtual void set_string_value(const std::string& val) = 0;
  virtual void set_string_value(const char* val) = 0;
  virtual void set_string_value(const char* val, size_t size) = 0;
  virtual void set_timestamp_value(const Timestamp& val) = 0;
  virtual void set_timestamp_value(int64_t val) = 0;
  virtual void set_binary_value(const std::string& val) = 0;
  virtual void set_binary_value(const void* val, size_t size) = 0;
  virtual void set_inetaddress_value(const InetAddress& val) = 0;
  virtual void set_uuid_value(const Uuid& val) = 0;
  virtual void set_timeuuid_value(const Uuid& val) = 0;

  //--------------------------------- mutable value methods ---------------------------------
  virtual std::string* mutable_decimal_value() = 0;
  virtual std::string* mutable_string_value() = 0;
  virtual std::string* mutable_binary_value() = 0;

  // for collections the setters just allocate the message and set the correct value type
  virtual void set_map_value() = 0;
  virtual void set_set_value() = 0;
  virtual void set_list_value() = 0;
  virtual void set_frozen_value() = 0;

  // the `add_foo` methods append a new element to the corresponding collection and return a pointer
  // so that its value can be set by the caller
  virtual QLValuePB* add_map_key() = 0;
  virtual QLValuePB* add_map_value() = 0;
  virtual QLValuePB* add_set_elem() = 0;
  virtual QLValuePB* add_list_elem() = 0;
  virtual QLValuePB* add_frozen_elem() = 0;

  //----------------------------------- assignment methods ----------------------------------
  QLValue& operator=(const QLValuePB& other) {
    Assign(other);
    return *this;
  }

  QLValue& operator=(QLValuePB&& other) {
    AssignMove(std::move(other));
    return *this;
  }

  virtual void Assign(const QLValuePB& other) = 0;
  virtual void AssignMove(QLValuePB&& other) = 0;

  //-----------------------------------------------------------------------------------------
  // Methods provided by this abstract class.

  //----------------------------------- comparison methods -----------------------------------
  virtual bool Comparable(const QLValue& other) const {
    return type() == other.type() || EitherIsNull(other);
  }
  virtual bool BothNotNull(const QLValue& other) const { return !IsNull() && !other.IsNull(); }
  virtual bool EitherIsNull(const QLValue& other) const { return IsNull() || other.IsNull(); }
  virtual int CompareTo(const QLValue& other) const;
  virtual bool operator <(const QLValue& v) const { return BothNotNull(v) && CompareTo(v) < 0; }
  virtual bool operator >(const QLValue& v) const { return BothNotNull(v) && CompareTo(v) > 0; }
  virtual bool operator <=(const QLValue& v) const { return BothNotNull(v) && CompareTo(v) <= 0; }
  virtual bool operator >=(const QLValue& v) const { return BothNotNull(v) && CompareTo(v) >= 0; }
  virtual bool operator ==(const QLValue& v) const { return BothNotNull(v) && CompareTo(v) == 0; }
  virtual bool operator !=(const QLValue& v) const { return BothNotNull(v) && CompareTo(v) != 0; }

  //----------------------------- serializer / deserializer ---------------------------------
  virtual void Serialize(const std::shared_ptr<QLType>& ql_type,
                         const QLClient& client,
                         faststring* buffer) const;
  virtual CHECKED_STATUS Deserialize(const std::shared_ptr<QLType>& type,
                                     const QLClient& client,
                                     Slice* data);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  virtual std::string ToString() const;

  //-----------------------------------------------------------------------------------------
  // The following static functions provide QLValue-equivalent interfaces for use with an
  // existing QLValuePB without wrapping it as a QLValue.
  //-----------------------------------------------------------------------------------------

  // The value's type.
  static InternalType type(const QLValuePB& v) { return v.value_case(); }

  //------------------------------------ Nullness methods -----------------------------------
  // Is the value null?
  static bool IsNull(const QLValuePB& v) { return v.value_case() == QLValuePB::VALUE_NOT_SET; }
  // Set the value to null.
  static void SetNull(QLValuePB* v);

  //----------------------------------- get value methods -----------------------------------
  // Get different datatype values. CHECK failure will result if the value stored is not of the
  // expected datatype or the value is null.
#define QL_GET_VALUE(v, type, field) \
  do { \
    CHECK(v.has_##field()) << "Value: " << v.ShortDebugString(); \
    return static_cast<type>(v.field()); \
  } while (0)

  static int8_t int8_value(const QLValuePB& v) { QL_GET_VALUE(v, int8_t, int8_value); }
  static int16_t int16_value(const QLValuePB& v) { QL_GET_VALUE(v, int16_t, int16_value); }
  static int32_t int32_value(const QLValuePB& v) { QL_GET_VALUE(v, int32_t, int32_value); }
  static int64_t int64_value(const QLValuePB& v) { QL_GET_VALUE(v, int64_t, int64_value); }
  static float float_value(const QLValuePB& v) { QL_GET_VALUE(v, float, float_value); }
  static double double_value(const QLValuePB& v) { QL_GET_VALUE(v, double, double_value); }
  static const std::string& decimal_value(const QLValuePB& v) {
    CHECK(v.has_decimal_value());
    return v.decimal_value();
  }
  static bool bool_value(const QLValuePB& v) { QL_GET_VALUE(v, bool, bool_value); }
  static const std::string& string_value(const QLValuePB& v) {
    CHECK(v.has_string_value());
    return v.string_value();
  }
  static Timestamp timestamp_value(const QLValuePB& v) {
    CHECK(v.has_timestamp_value());
    return Timestamp(v.timestamp_value());
  }
  static const std::string& binary_value(const QLValuePB& v) {
    CHECK(v.has_binary_value());
    return v.binary_value();
  }
  static const QLMapValuePB& map_value(const QLValuePB& v) {
    CHECK(v.has_map_value());
    return v.map_value();
  }
  static const QLSeqValuePB& set_value(const QLValuePB& v) {
    CHECK(v.has_set_value());
    return v.set_value();
  }
  static const QLSeqValuePB& list_value(const QLValuePB& v) {
    CHECK(v.has_list_value());
    return v.list_value();
  }
  static const QLSeqValuePB& frozen_value(const QLValuePB& v) {
    CHECK(v.has_frozen_value());
    return v.frozen_value();
  }

  static InetAddress inetaddress_value(const QLValuePB& v) {
    CHECK(v.has_inetaddress_value());
    InetAddress addr;
    CHECK_OK(addr.FromBytes(v.inetaddress_value()));
    return addr;
  }

  static Uuid uuid_value(const QLValuePB& v) {
    CHECK(v.has_uuid_value());
    Uuid uuid;
    CHECK_OK(uuid.FromBytes(v.uuid_value()));
    return uuid;
  }

  static Uuid timeuuid_value(const QLValuePB& v) {
    CHECK(v.has_timeuuid_value());
    Uuid timeuuid;
    CHECK_OK(timeuuid.FromBytes(v.timeuuid_value()));
    CHECK_OK(timeuuid.IsTimeUuid());
    return timeuuid;
  }

#undef QL_GET_VALUE

  static CHECKED_STATUS AppendToKeyBytes(const QLValuePB &value_pb, string *bytes);
  virtual CHECKED_STATUS AppendToKeyBytes(string *bytes) const = 0;

  //----------------------------------- set value methods -----------------------------------
  // Set different datatype values.
  static void set_int8_value(const int8_t val, QLValuePB *v) { v->set_int8_value(val); }
  static void set_int16_value(const int16_t val, QLValuePB *v) { v->set_int16_value(val); }
  static void set_int32_value(const int32_t val, QLValuePB *v) { v->set_int32_value(val); }
  static void set_int64_value(const int64_t val, QLValuePB *v) { v->set_int64_value(val); }
  static void set_float_value(const float val, QLValuePB *v) { v->set_float_value(val); }
  static void set_double_value(const double val, QLValuePB *v) { v->set_double_value(val); }
  static void set_decimal_value(const std::string& val, QLValuePB *v) {
    v->set_decimal_value(val);
  }
  static void set_decimal_value(const char* val, const size_t size, QLValuePB *v) {
    v->set_decimal_value(val, size);
  }
  static void set_bool_value(const bool val, QLValuePB *v) { v->set_bool_value(val); }
  static void set_string_value(const std::string& val, QLValuePB *v) { v->set_string_value(val); }
  static void set_string_value(const char* val, QLValuePB *v) { v->set_string_value(val); }
  static void set_string_value(const char* val, const size_t size, QLValuePB *v) {
    v->set_string_value(val, size);
  }
  static void set_timestamp_value(const Timestamp& val, QLValuePB *v) {
    v->set_timestamp_value(val.ToInt64());
  }

  static void set_inetaddress_value(const InetAddress& val, QLValuePB *v) {
    std::string bytes;
    CHECK_OK(val.ToBytes(&bytes));
    v->set_inetaddress_value(bytes);
  }

  static void set_uuid_value(const Uuid& val, QLValuePB *v) {
    std::string bytes;
    CHECK_OK(val.ToBytes(&bytes));
    v->set_uuid_value(bytes);
  }

  static void set_timeuuid_value(const Uuid& val, QLValuePB *v) {
    CHECK_OK(val.IsTimeUuid());
    std::string bytes;
    CHECK_OK(val.ToBytes(&bytes));
    v->set_timeuuid_value(bytes);
  }

  static void set_timestamp_value(const int64_t val, QLValuePB *v) { v->set_timestamp_value(val); }
  static void set_binary_value(const std::string& val, QLValuePB *v) { v->set_binary_value(val); }
  static void set_binary_value(const void* val, const size_t size, QLValuePB *v) {
    v->set_binary_value(static_cast<const char *>(val), size);
  }

  // For collections, the call to `mutable_foo` takes care of setting the correct type to `foo`
  // internally and allocating the message if needed
  static void set_map_value(QLValuePB *v) {v->mutable_map_value();}
  static void set_set_value(QLValuePB *v) {v->mutable_set_value();}
  static void set_list_value(QLValuePB *v) {v->mutable_list_value();}
  static void set_frozen_value(QLValuePB *v) { v->mutable_frozen_value(); }

  // To extend/construct collections we return freshly allocated elements for the caller to set.
  static QLValuePB* add_map_key(QLValuePB *v) {
    return v->mutable_map_value()->add_keys();
  }
  static QLValuePB* add_map_value(QLValuePB *v) {
    return v->mutable_map_value()->add_values();
  }
  static QLValuePB* add_set_elem(QLValuePB *v) {
    return v->mutable_set_value()->add_elems();
  }
  static QLValuePB* add_list_elem(QLValuePB *v) {
    return v->mutable_list_value()->add_elems();
  }
  static QLValuePB* add_frozen_elem(QLValuePB *v) {
    return v->mutable_frozen_value()->add_elems();
  }

  //--------------------------------- mutable value methods ----------------------------------
  static std::string* mutable_decimal_value(QLValuePB *v) { return v->mutable_decimal_value(); }
  static std::string* mutable_string_value(QLValuePB *v) { return v->mutable_string_value(); }
  static std::string* mutable_binary_value(QLValuePB *v) { return v->mutable_binary_value(); }

  //----------------------------------- comparison methods -----------------------------------
  static bool Comparable(const QLValuePB& lhs, const QLValuePB& rhs) {
    return lhs.value_case() == rhs.value_case() || EitherIsNull(lhs, rhs);
  }
  static bool BothNotNull(const QLValuePB& lhs, const QLValuePB& rhs) {
    return !IsNull(lhs) && !IsNull(rhs);
  }
  static bool EitherIsNull(const QLValuePB& lhs, const QLValuePB& rhs) {
    return IsNull(lhs) || IsNull(rhs);
  }
  static int CompareTo(const QLValuePB& lhs, const QLValuePB& rhs);
  static int CompareTo(const QLSeqValuePB& lhs, const QLSeqValuePB& rhs);

  static bool Comparable(const QLValuePB& lhs, const QLValue& rhs) {
    return type(lhs) == rhs.type() || EitherIsNull(lhs, rhs);
  }
  static bool BothNotNull(const QLValuePB& lhs, const QLValue& rhs) {
    return !IsNull(lhs) && !rhs.IsNull();
  }
  static bool EitherIsNull(const QLValuePB& lhs, const QLValue& rhs) {
    return IsNull(lhs) || rhs.IsNull();
  }
  static int CompareTo(const QLValuePB& lhs, const QLValue& rhs);

 private:
  // Deserialize a CQL number (8, 16, 32 and 64-bit integer). <num_type> is the parsed integer type.
  // <converter> converts the number from network byte-order to machine order and <data_type>
  // is the coverter's return type. The converter's return type <data_type> is unsigned while
  // <num_type> may be signed or unsigned. <setter> sets the value in QLValue.
  template<typename num_type, typename data_type>
  CHECKED_STATUS CQLDeserializeNum(
      size_t len, data_type (*converter)(const void*), void (QLValue::*setter)(num_type),
      Slice* data) {
    num_type value = 0;
    RETURN_NOT_OK(CQLDecodeNum(len, converter, data, &value));
    (this->*setter)(value);
    return Status::OK();
  }

  // Deserialize a CQL floating point number (float or double). <float_type> is the parsed floating
  // point type. <converter> converts the number from network byte-order to machine order and
  // <data_type> is the coverter's return type. The converter's return type <data_type> is an
  // integer type. <setter> sets the value in QLValue.
  template<typename float_type, typename data_type>
  CHECKED_STATUS CQLDeserializeFloat(
      size_t len, data_type (*converter)(const void*), void (QLValue::*setter)(float_type),
      Slice* data) {
    float_type value = 0.0;
    RETURN_NOT_OK(CQLDecodeFloat(len, converter, data, &value));
    (this->*setter)(value);
    return Status::OK();
  }
};

//-------------------------------------------------------------------------------------------
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

//-------------------------------------------------------------------------------------------
// A class that implements QLValue interface using QLValuePB.
class QLValueWithPB : public QLValue, public QLValuePB {
 public:
  QLValueWithPB() { }
  explicit QLValueWithPB(const QLValuePB& val) { *mutable_value() = val; }
  virtual ~QLValueWithPB() override { }

  virtual InternalType type() const override { return QLValue::type(value()); }

  const QLValuePB& value() const { return *static_cast<const QLValuePB*>(this); }
  QLValuePB* mutable_value() { return static_cast<QLValuePB*>(this); }

  virtual CHECKED_STATUS AppendToKeyBytes(string *bytes) const override {
    return QLValue::AppendToKeyBytes(value(), bytes);
  }

  //------------------------------------ Nullness methods -----------------------------------
  virtual bool IsNull() const override { return QLValue::IsNull(value()); }
  virtual void SetNull() override { QLValue::SetNull(mutable_value()); }

  //----------------------------------- get value methods -----------------------------------
  virtual int8_t int8_value() const override { return QLValue::int8_value(value()); }
  virtual int16_t int16_value() const override { return QLValue::int16_value(value()); }
  virtual int32_t int32_value() const override { return QLValue::int32_value(value()); }
  virtual int64_t int64_value() const override { return QLValue::int64_value(value()); }
  virtual float float_value() const override { return QLValue::float_value(value()); }
  virtual double double_value() const override { return QLValue::double_value(value()); }
  virtual const std::string& decimal_value() const override {
    return QLValue::decimal_value(value());
  }
  virtual bool bool_value() const override { return QLValue::bool_value(value()); }
  virtual const string& string_value() const override { return QLValue::string_value(value()); }
  virtual Timestamp timestamp_value() const override { return QLValue::timestamp_value(value()); }
  virtual const std::string& binary_value() const override {
    return QLValue::binary_value(value());
  }
  virtual InetAddress inetaddress_value() const override {
    return QLValue::inetaddress_value(value());
  }
  virtual const QLMapValuePB& map_value() const override { return QLValue::map_value(value()); }
  virtual const QLSeqValuePB& set_value() const override { return QLValue::set_value(value()); }
  virtual const QLSeqValuePB& list_value() const override { return QLValue::list_value(value()); }
  virtual const QLSeqValuePB& frozen_value() const override {
    return QLValue::frozen_value(value());
  }

  virtual Uuid uuid_value() const override { return QLValue::uuid_value(value()); }
  virtual Uuid timeuuid_value() const override { return QLValue::timeuuid_value(value()); }

  //----------------------------------- set value methods -----------------------------------
  virtual void set_int8_value(const int8_t val) override {
    QLValue::set_int8_value(val, mutable_value());
  }
  virtual void set_int16_value(const int16_t val) override {
    QLValue::set_int16_value(val, mutable_value());
  }
  virtual void set_int32_value(const int32_t val) override {
    QLValue::set_int32_value(val, mutable_value());
  }
  virtual void set_int64_value(const int64_t val) override {
    QLValue::set_int64_value(val, mutable_value());
  }
  virtual void set_float_value(const float val) override {
    QLValue::set_float_value(val, mutable_value());
  }
  virtual void set_double_value(const double val) override {
    QLValue::set_double_value(val, mutable_value());
  }
  virtual void set_decimal_value(const std::string& val) override {
    QLValue::set_decimal_value(val, mutable_value());
  }
  virtual void set_bool_value(const bool val) override {
    QLValue::set_bool_value(val, mutable_value());
  }
  virtual void set_string_value(const string& val) override {
    QLValue::set_string_value(val, mutable_value());
  }
  virtual void set_string_value(const char* val) override {
    QLValue::set_string_value(val, mutable_value());
  }
  virtual void set_string_value(const char* val, const size_t size) override {
    QLValue::set_string_value(val, size, mutable_value());
  }
  virtual void set_timestamp_value(const Timestamp& val) override {
    QLValue::set_timestamp_value(val, mutable_value());
  }
  virtual void set_inetaddress_value(const InetAddress& val) override {
    QLValue::set_inetaddress_value(val, mutable_value());
  }
  virtual void set_timestamp_value(const int64_t val) override {
    QLValue::set_timestamp_value(val, mutable_value());
  }
  virtual void set_uuid_value(const Uuid& val) override {
    QLValue::set_uuid_value(val, mutable_value());
  }
  virtual void set_timeuuid_value(const Uuid& val) override {
    QLValue::set_timeuuid_value(val, mutable_value());
  }
  virtual void set_binary_value(const std::string& val) override {
    QLValue::set_binary_value(val, mutable_value());
  }
  virtual void set_binary_value(const void* val, const size_t size) override {
    QLValue::set_binary_value(val, size, mutable_value());
  }
  virtual void set_map_value() override {
    QLValue::set_map_value(mutable_value());
  }
  virtual void set_set_value() override {
    QLValue::set_set_value(mutable_value());
  }
  virtual void set_list_value() override {
    QLValue::set_list_value(mutable_value());
  }
  virtual void set_frozen_value() override {
    QLValue::set_frozen_value(mutable_value());
  }
  virtual QLValuePB* add_map_key() override {
    return QLValue::add_map_key(mutable_value());
  }
  virtual QLValuePB* add_map_value() override {
    return QLValue::add_map_value(mutable_value());
  }
  virtual QLValuePB* add_set_elem() override {
    return QLValue::add_set_elem(mutable_value());
  }
  virtual QLValuePB* add_list_elem() override {
    return QLValue::add_list_elem(mutable_value());
  }
  virtual QLValuePB* add_frozen_elem() override {
    return QLValue::add_frozen_elem(mutable_value());
  }

  //--------------------------------- mutable value methods ---------------------------------
  virtual std::string* mutable_decimal_value() override {
    return QLValue::mutable_decimal_value(mutable_value());
  }
  virtual std::string* mutable_string_value() override {
    return QLValue::mutable_string_value(mutable_value());
  }
  virtual std::string* mutable_binary_value() override {
    return QLValue::mutable_binary_value(mutable_value());
  }

  //----------------------------------- assignment methods ----------------------------------
  virtual void Assign(const QLValuePB& other) override {
    CopyFrom(other);
  }
  virtual void AssignMove(QLValuePB&& other) override {
    Swap(&other);
  }
};

#define YB_SET_INT_VALUE(ql_valuepb, input, bits) \
  case DataType::BOOST_PP_CAT(INT, bits): { \
    auto value = util::CheckedStoInt<BOOST_PP_CAT(BOOST_PP_CAT(int, bits), _t)>(input); \
    RETURN_NOT_OK(value); \
    ql_valuepb->BOOST_PP_CAT(BOOST_PP_CAT(set_int, bits), _value)(*value); \
  } break;

} // namespace yb

#endif // YB_COMMON_QL_VALUE_H
