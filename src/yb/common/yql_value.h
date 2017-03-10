// Copyright (c) YugaByte, Inc.
//
// This file contains the YQLValue class that represents YQL values.

#ifndef YB_COMMON_YQL_VALUE_H
#define YB_COMMON_YQL_VALUE_H

#include <stdint.h>

#include "yb/common/yql_protocol.pb.h"
#include "yb/util/timestamp.h"

namespace yb {

// An abstract class that defines a YQL value interface to support different implementations
// for in-memory / serialization trade-offs.
class YQLValue {
 public:
  // The value type.
  typedef YQLValuePB::ValueCase InternalType;

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
  virtual bool bool_value() const = 0;
  virtual const std::string& string_value() const = 0;
  virtual Timestamp timestamp_value() const = 0;

  //----------------------------------- set value methods -----------------------------------
  // Set different datatype values.
  virtual void set_int8_value(int8_t val) = 0;
  virtual void set_int16_value(int16_t val) = 0;
  virtual void set_int32_value(int32_t val) = 0;
  virtual void set_int64_value(int64_t val) = 0;
  virtual void set_float_value(float val) = 0;
  virtual void set_double_value(double val) = 0;
  virtual void set_bool_value(bool val) = 0;
  virtual void set_string_value(const std::string& val) = 0;
  virtual void set_string_value(const char* val) = 0;
  virtual void set_string_value(const char* val, size_t size) = 0;
  virtual void set_timestamp_value(const Timestamp& val) = 0;
  virtual void set_timestamp_value(int64_t val) = 0;

  //----------------------------------- assignment methods ----------------------------------
  virtual YQLValue& operator=(const YQLValuePB& other) = 0;
  virtual YQLValue& operator=(YQLValuePB&& other) = 0;

  //-----------------------------------------------------------------------------------------
  // Methods provided by this abstract class.

  //----------------------------------- comparison methods -----------------------------------
  virtual bool Comparable(const YQLValue& other) const {
    return type() == other.type() || EitherIsNull(other);
  }
  virtual bool BothNotNull(const YQLValue& other) const { return !IsNull() && !other.IsNull(); }
  virtual bool EitherIsNull(const YQLValue& other) const { return IsNull() || other.IsNull(); }
  virtual int CompareTo(const YQLValue& other) const;
  virtual bool operator <(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) < 0; }
  virtual bool operator >(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) > 0; }
  virtual bool operator <=(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) <= 0; }
  virtual bool operator >=(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) >= 0; }
  virtual bool operator ==(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) == 0; }
  virtual bool operator !=(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) != 0; }

  //----------------------------- serializer / deserializer ---------------------------------
  virtual void Serialize(DataType sql_type, YQLClient client, faststring* buffer) const;
  virtual CHECKED_STATUS Deserialize(DataType sql_type, YQLClient client, Slice* data);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  virtual std::string ToString() const;

  //-----------------------------------------------------------------------------------------
  // The following static functions provide YQLValue-equivalent interfaces for use with an
  // existing YQLValuePB without wrapping it as a YQLValue.
  //-----------------------------------------------------------------------------------------

  // The value's type.
  static InternalType type(const YQLValuePB& v) { return v.value_case(); }

  //------------------------------------ Nullness methods -----------------------------------
  // Is the value null?
  static bool IsNull(const YQLValuePB& v) { return v.value_case() == YQLValuePB::VALUE_NOT_SET; }
  // Set the value to null.
  static void SetNull(YQLValuePB* v);

  //----------------------------------- get value methods -----------------------------------
  // Get different datatype values. CHECK failure will result if the value stored is not of the
  // expected datatype or the value is null.
#define YQL_GET_VALUE(v, type, field)                                   \
  do { CHECK(v.has_##field()); return static_cast<type>(v.field()); } while (0)

  static int8_t int8_value(const YQLValuePB& v) { YQL_GET_VALUE(v, int8_t, int8_value); }
  static int16_t int16_value(const YQLValuePB& v) { YQL_GET_VALUE(v, int16_t, int16_value); }
  static int32_t int32_value(const YQLValuePB& v) { YQL_GET_VALUE(v, int32_t, int32_value); }
  static int64_t int64_value(const YQLValuePB& v) { YQL_GET_VALUE(v, int64_t, int64_value); }
  static float float_value(const YQLValuePB& v) { YQL_GET_VALUE(v, float, float_value); }
  static double double_value(const YQLValuePB& v) { YQL_GET_VALUE(v, double, double_value); }
  static bool bool_value(const YQLValuePB& v) { YQL_GET_VALUE(v, bool, bool_value); }
  static const std::string& string_value(const YQLValuePB& v) {
    CHECK(v.has_string_value());
    return v.string_value();
  }
  static Timestamp timestamp_value(const YQLValuePB& v) {
    CHECK(v.has_timestamp_value());
    return Timestamp(v.timestamp_value());
  }

#undef YQL_GET_VALUE

  //----------------------------------- set value methods -----------------------------------
  // Set different datatype values.
  static void set_int8_value(const int8_t val, YQLValuePB *v) { v->set_int8_value(val); }
  static void set_int16_value(const int16_t val, YQLValuePB *v) { v->set_int16_value(val); }
  static void set_int32_value(const int32_t val, YQLValuePB *v) { v->set_int32_value(val); }
  static void set_int64_value(const int64_t val, YQLValuePB *v) { v->set_int64_value(val); }
  static void set_float_value(const float val, YQLValuePB *v) { v->set_float_value(val); }
  static void set_double_value(const double val, YQLValuePB *v) { v->set_double_value(val); }
  static void set_bool_value(const bool val, YQLValuePB *v) { v->set_bool_value(val); }
  static void set_string_value(const std::string& val, YQLValuePB *v) { v->set_string_value(val); }
  static void set_string_value(const char* val, YQLValuePB *v) { v->set_string_value(val); }
  static void set_string_value(const char* val, const size_t size, YQLValuePB *v) {
    v->set_string_value(val, size);
  }
  static void set_timestamp_value(const Timestamp& val, YQLValuePB *v) {
    v->set_timestamp_value(val.ToInt64());
  }
  static void set_timestamp_value(const int64_t val, YQLValuePB *v) { v->set_timestamp_value(val); }

  //----------------------------------- comparison methods -----------------------------------
  static bool Comparable(const YQLValuePB& lhs, const YQLValuePB& rhs) {
    return lhs.value_case() == rhs.value_case() || EitherIsNull(lhs, rhs);
  }

  static bool BothNotNull(const YQLValuePB& lhs, const YQLValuePB& rhs) {
    return !IsNull(lhs) && !IsNull(rhs);
  }

  static bool EitherIsNull(const YQLValuePB& lhs, const YQLValuePB& rhs) {
    return IsNull(lhs) || IsNull(rhs);
  }

  static int CompareTo(const YQLValuePB& lhs, const YQLValuePB& rhs);

 private:
  // Deserialize a CQL number (8, 16, 32 and 64-bit integer). <num_type> is the parsed integer type.
  // <converter> converts the number from network byte-order to machine order and <data_type>
  // is the coverter's return type. The converter's return type <data_type> is unsigned while
  // <num_type> may be signed or unsigned. <setter> sets the value in YQLValue.
  template<typename num_type, typename data_type>
  CHECKED_STATUS CQLDeserializeNum(
      size_t len, data_type (*converter)(const void*), void (YQLValue::*setter)(num_type),
      Slice* data) {
    num_type value = 0;
    RETURN_NOT_OK(CQLDecodeNum(len, converter, data, &value));
    (this->*setter)(value);
    return Status::OK();
  }

  // Deserialize a CQL floating point number (float or double). <float_type> is the parsed floating
  // point type. <converter> converts the number from network byte-order to machine order and
  // <data_type> is the coverter's return type. The converter's return type <data_type> is an
  // integer type. <setter> sets the value in YQLValue.
  template<typename float_type, typename data_type>
  CHECKED_STATUS CQLDeserializeFloat(
      size_t len, data_type (*converter)(const void*), void (YQLValue::*setter)(float_type),
      Slice* data) {
    float_type value = 0.0;
    RETURN_NOT_OK(CQLDecodeFloat(len, converter, data, &value));
    (this->*setter)(value);
    return Status::OK();
  }
};

//-------------------------------------------------------------------------------------------
// YQLValuePB operators
bool operator <(const YQLValuePB& lhs, const YQLValuePB& rhs);
bool operator >(const YQLValuePB& lhs, const YQLValuePB& rhs);
bool operator <=(const YQLValuePB& lhs, const YQLValuePB& rhs);
bool operator >=(const YQLValuePB& lhs, const YQLValuePB& rhs);
bool operator ==(const YQLValuePB& lhs, const YQLValuePB& rhs);
bool operator !=(const YQLValuePB& lhs, const YQLValuePB& rhs);

//-------------------------------------------------------------------------------------------
// A class that implements YQLValue interface using YQLValuePB.
class YQLValueWithPB : public YQLValue {
 public:
  YQLValueWithPB() { }
  virtual ~YQLValueWithPB() { }

  virtual InternalType type() const override { return YQLValue::type(value_); }

  //------------------------------------ Nullness methods -----------------------------------
  virtual bool IsNull() const override { return YQLValue::IsNull(value_); }
  virtual void SetNull() override { YQLValue::SetNull(&value_); }

  //----------------------------------- get value methods -----------------------------------
  virtual int8_t int8_value() const override { return YQLValue::int8_value(value_); }
  virtual int16_t int16_value() const override { return YQLValue::int16_value(value_); }
  virtual int32_t int32_value() const override { return YQLValue::int32_value(value_); }
  virtual int64_t int64_value() const override { return YQLValue::int64_value(value_); }
  virtual float float_value() const override { return YQLValue::float_value(value_); }
  virtual double double_value() const override { return YQLValue::double_value(value_); }
  virtual bool bool_value() const override { return YQLValue::bool_value(value_); }
  virtual const std::string& string_value() const override {
    return YQLValue::string_value(value_);
  }
  virtual Timestamp timestamp_value() const override { return YQLValue::timestamp_value(value_); }

  //----------------------------------- set value methods -----------------------------------
  virtual void set_int8_value(int8_t val) override { YQLValue::set_int8_value(val, &value_); }
  virtual void set_int16_value(int16_t val) override { YQLValue::set_int16_value(val, &value_); }
  virtual void set_int32_value(int32_t val) override { YQLValue::set_int32_value(val, &value_); }
  virtual void set_int64_value(int64_t val) override { YQLValue::set_int64_value(val, &value_); }
  virtual void set_float_value(float val) override { YQLValue::set_float_value(val, &value_); }
  virtual void set_double_value(double val) override { YQLValue::set_double_value(val, &value_); }
  virtual void set_bool_value(bool val) override { YQLValue::set_bool_value(val, &value_); }
  virtual void set_string_value(const std::string& val) override {
    YQLValue::set_string_value(val, &value_);
  }
  virtual void set_string_value(const char* val) override {
    YQLValue::set_string_value(val, &value_);
  }
  virtual void set_string_value(const char* val, const size_t size) override {
    YQLValue::set_string_value(val, size, &value_);
  }
  virtual void set_timestamp_value(const Timestamp& val) override {
    YQLValue::set_timestamp_value(val, &value_);
  }
  virtual void set_timestamp_value(const int64_t val) override {
    YQLValue::set_timestamp_value(val, &value_);
  }

  //----------------------------------- assignment methods ----------------------------------
  virtual YQLValue& operator=(const YQLValuePB& other) override {
    value_ = other;
    return *this;
  }
  virtual YQLValue& operator=(YQLValuePB&& other) override {
    value_ = std::move(other);
    return *this;
  }

 private:
  YQLValuePB value_;
};

} // namespace yb

#endif // YB_COMMON_YQL_VALUE_H
