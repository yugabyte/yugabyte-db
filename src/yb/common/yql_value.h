// Copyright (c) YugaByte, Inc.
//
// This file contains the YQLValue class that represents YQL values.

#ifndef YB_COMMON_YQL_VALUE_H
#define YB_COMMON_YQL_VALUE_H

#include <stdint.h>

#include "yb/common/common.pb.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/hybrid_time.h"
#include "yb/util/faststring.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb {

// A class that stores just the YQL value part. The datatype and null state are stored out-of-line.
// For YQL row blocks, it is memory-inefficient for the same column in repeated rows to store the
// datatype info. Also, the null state of the columns in the same row can be stored efficiently
// in a bitmap. See YQLRow for details.
class YQLValueCore {

 private:
  friend class YQLValue;
  friend class YQLRow;

  // Construct a YQLValueCore for the specified datatype.
  inline explicit YQLValueCore(const DataType type) {
    // No need to call timestamp's constructor because it is a simple wrapper over an uint64_t.
    if (type == STRING) {
      new(&string_value_) std::string();
    }
  }

  // Copy/move constructor for the specified datatype.
  YQLValueCore(DataType type, bool is_null, const YQLValueCore& other);
  YQLValueCore(DataType type, bool is_null, YQLValueCore* other);

  // Since destructor cannot take an argument to indicate the datatype of the value stored, a
  // YQLValueCore should be destroyed by first calling the Free() method below with the datatype.
  virtual inline ~YQLValueCore() { }

  inline void Free(const DataType type) {
    // No need to call timestamp's destructor because it is a simple wrapper over an uint64_t.
    if (type == STRING) {
      string_value_.~basic_string();
    }
  }

  //---------------------------------- get value template -----------------------------------
  // A template to get different datatype value fields. CHECK failure will result if the value
  // is not of the expected or the value is null.
  template<typename type_t>
  static inline type_t value(
      const DataType type, const DataType expected_type, const bool is_null, const type_t value) {
    CHECK_EQ(type, expected_type);
    CHECK(!is_null);
    return value;
  }

  //---------------------------------- set value template -----------------------------------
  // A template to set different datatype value fields. CHECK failure will result if the value
  // is not of the expected.
  template<typename type_t>
  static inline void set_value(
      const DataType type, const DataType expected_type, const type_t other, type_t* value) {
    CHECK_EQ(type, expected_type);
    *value = other;
  }

  //----------------------------- serializer / deserializer ---------------------------------
  void Serialize(DataType type, bool is_null, YQLClient client, faststring* buffer) const;
  CHECKED_STATUS Deserialize(DataType type, YQLClient client, Slice* data, bool* is_null);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToString(const DataType type, bool is_null) const;

  //-------------------------------- union of different value types -------------------------
  // Just primitive values currently.
  union {
    int8_t int8_value_;
    int16_t int16_value_;
    int32_t int32_value_;
    int64_t int64_value_;
    float float_value_;
    double double_value_;
    std::string string_value_;
    bool bool_value_;
    int64_t timestamp_value_;
  };
};

// A YQL value with datatype and null state. This class is good for expression evaluation.
class YQLValue : YQLValueCore {
 public:
  explicit YQLValue(DataType type) : YQLValueCore(type), type_(type), is_null_(true) { }
  YQLValue(const YQLValue& other) : YQLValue(other.type_, other.is_null_, other) { }
  YQLValue(YQLValue&& other) : YQLValue(other.type_, other.is_null_, &other) { }

  virtual ~YQLValue() {
    Free(type_);
  }

  // The value's datatype
  DataType type() const { return type_; }

  // Is the value null?
  bool IsNull() const { return is_null_; }

  // Set the value to null or not.
  void SetNull(const bool is_null) { is_null_ = is_null; }

  //----------------------------------- get value methods -----------------------------------
  // Get different datatype values. CHECK failure will result if the value stored is not of the
  // expected datatype or the value is null.
  int8_t int8_value() const { return value(type_, INT8, is_null_, int8_value_); }
  int16_t int16_value() const { return value(type_, INT16, is_null_, int16_value_); }
  int32_t int32_value() const { return value(type_, INT32, is_null_, int32_value_); }
  int64_t int64_value() const { return value(type_, INT64, is_null_, int64_value_); }
  float float_value() const { return value(type_, FLOAT, is_null_, float_value_); }
  double double_value() const { return value(type_, DOUBLE, is_null_, double_value_); }
  std::string string_value() const { return value(type_, STRING, is_null_, string_value_); }
  bool bool_value() const { return value(type_, BOOL, is_null_, bool_value_); }
  int64_t timestamp_value() const { return value(type_, TIMESTAMP, is_null_, timestamp_value_); }

  //----------------------------------- set value methods -----------------------------------
  // Set different datatype values. CHECK failure will result if the value stored is not of the
  // expected datatype.
  template<typename type_t>
  void set_value(const DataType expected_type, const type_t other, type_t* value) {
    YQLValueCore::set_value(type_, expected_type, other, value);
    is_null_ = false;
  }

  void set_int8_value(int8_t v) { return set_value(INT8, v, &int8_value_); }
  void set_int16_value(int16_t v) { return set_value(INT16, v, &int16_value_); }
  void set_int32_value(int32_t v) { return set_value(INT32, v, &int32_value_); }
  void set_int64_value(int64_t v) { return set_value(INT64, v, &int64_value_); }
  void set_float_value(float v) { return set_value(FLOAT, v, &float_value_); }
  void set_double_value(double v) { return set_value(DOUBLE, v, &double_value_); }
  void set_string_value(const std::string& v) { return set_value(STRING, v, &string_value_); }
  void set_bool_value(bool v) { return set_value(BOOL, v, &bool_value_); }
  void set_timestamp_value(const int64_t& v) {
    return set_value(TIMESTAMP, v, &timestamp_value_);
  }

  // Assignment operator
  YQLValue& operator=(const YQLValue& other);
  YQLValue& operator=(YQLValue&& other);

  // Comparison methods / operators
  bool Comparable(const YQLValue& other) const { return type_ == other.type_; }

  template<typename T>
  int GenericCompare(const T& a, const T& b) const {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
  }

  int CompareTo(const YQLValue& v) const;

  inline bool operator <(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) < 0; }
  inline bool operator >(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) > 0; }
  inline bool operator <=(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) <= 0; }
  inline bool operator >=(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) >= 0; }
  inline bool operator ==(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) == 0; }
  inline bool operator !=(const YQLValue& v) const { return BothNotNull(v) && CompareTo(v) != 0; }

  // Get a YQLValue from a YQLValuePB protobuf.
  static YQLValue FromYQLValuePB(const YQLValuePB& v);

  // Note: YQLValue doesn't have serialize / deserialize methods because we expect YQL values
  // to be serialized / deserialized as part of a row block. See YQLRowBlock.

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToString() const { return YQLValueCore::ToString(type_, is_null_); }

  //-------------------------------- value datatype and null state --------------------------
 private:
  friend class YQLRow;

  YQLValue(const DataType type, const bool is_null, const YQLValueCore& v)
      : YQLValueCore(type, is_null, v), type_(type), is_null_(is_null) {
  }

  YQLValue(const DataType type, const bool is_null, YQLValueCore* v)
      : YQLValueCore(type, is_null, v), type_(type), is_null_(is_null) {
  }

  inline bool BothNotNull(const YQLValue& v) const { return !is_null_ && !v.is_null_; }

  const DataType type_;
  bool is_null_;
};

} // namespace yb

#endif // YB_COMMON_YQL_VALUE_H
