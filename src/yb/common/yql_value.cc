// Copyright (c) YugaByte, Inc.
//
// This file contains the YQLValue class that represents YQL values.

#include "yb/common/yql_value.h"

#include <cfloat>

#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/util/date_time.h"
#include "yb/util/bytes_formatter.h"

// The list of unsupported datypes to use in switch statements
#define YQL_UNSUPPORTED_TYPES_IN_SWITCH \
  case UINT8:  FALLTHROUGH_INTENDED;    \
  case UINT16: FALLTHROUGH_INTENDED;    \
  case UINT32: FALLTHROUGH_INTENDED;    \
  case UINT64: FALLTHROUGH_INTENDED;    \
  case BINARY: FALLTHROUGH_INTENDED;    \
  case UNKNOWN_DATA

namespace yb {

using std::string;
using std::to_string;
using util::FormatBytesAsStr;

//----------------------------------------- YQLValueCore --------------------------------------
YQLValueCore::YQLValueCore(const DataType type, const bool is_null, const YQLValueCore& other) {
  if (is_null) {
    new(this) YQLValueCore(type);
    return;
  }

  switch (type) {
    case INT8: int8_value_ = other.int8_value_; return;
    case INT16: int16_value_ = other.int16_value_; return;
    case INT32: int32_value_ = other.int32_value_; return;
    case INT64: int64_value_ = other.int64_value_; return;
    case FLOAT: float_value_ = other.float_value_; return;
    case DOUBLE: double_value_ = other.double_value_; return;
    case STRING: new(&string_value_) string(other.string_value_); return;
    case BOOL: bool_value_ = other.bool_value_; return;
    case TIMESTAMP: timestamp_value_ = other.timestamp_value_; return;
    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;
    // default: fall through
  }
  LOG(FATAL) << "Internal error: unsupported type " << type;
}

YQLValueCore::YQLValueCore(const DataType type, const bool is_null, YQLValueCore* other) {
  if (is_null) {
    new(this) YQLValueCore(type);
    other->Free(type);
    return;
  }

  switch (type) {
    case INT8: int8_value_ = other->int8_value_; return;
    case INT16: int16_value_ = other->int16_value_; return;
    case INT32: int32_value_ = other->int32_value_; return;
    case INT64: int64_value_ = other->int64_value_; return;
    case FLOAT: float_value_ = other->float_value_; return;
    case DOUBLE: double_value_ = other->double_value_; return;
    case STRING: new(&string_value_) string(std::move(other->string_value_)); return;
    case BOOL: bool_value_ = other->bool_value_; return;
    case TIMESTAMP: timestamp_value_ = other->timestamp_value_; return;
    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;
    // default: fall through
  }
  LOG(FATAL) << "Internal error: unsupported type " << type;
}

void YQLValueCore::Serialize(
    const DataType type, const bool is_null, const YQLClient client, faststring* buffer) const {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  if (is_null) {
    CQLEncodeLength(-1, buffer);
    return;
  }

  switch (type) {
    case INT8:
      CQLEncodeNum(Store8, int8_value_, buffer);
      return;
    case INT16:
      CQLEncodeNum(NetworkByteOrder::Store16, int16_value_, buffer);
      return;
    case INT32:
      CQLEncodeNum(NetworkByteOrder::Store32, int32_value_, buffer);
      return;
    case INT64:
      CQLEncodeNum(NetworkByteOrder::Store64, int64_value_, buffer);
      return;
    case FLOAT:
      CQLEncodeFloat(NetworkByteOrder::Store32, float_value_, buffer);
      return;
    case DOUBLE:
      CQLEncodeFloat(NetworkByteOrder::Store64, double_value_, buffer);
      return;
    case STRING:
      CQLEncodeBytes(string_value_, buffer);
      return;
    case BOOL:
      CQLEncodeNum(Store8, static_cast<uint8>(bool_value_ ? 1 : 0), buffer);
      return;
    case TIMESTAMP: {
      int64_t val = DateTime::AdjustPrecision(timestamp_value_.ToInt64(),
          DateTime::internal_precision,
          DateTime::CqlDateTimeInputFormat.input_precision());
      CQLEncodeNum(NetworkByteOrder::Store64, val, buffer);
      return;
    }

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type;
}

Status YQLValueCore::Deserialize(
    const DataType type, const YQLClient client, Slice* data, bool* is_null) {

  CHECK_EQ(client, YQL_CLIENT_CQL);

  int32_t len = 0;
  RETURN_NOT_OK(CQLDecodeNum(sizeof(len), NetworkByteOrder::Load32, data, &len));
  *is_null = (len == -1);
  if (*is_null) {
    return Status::OK();
  }

  switch (type) {
    case INT8:
      return CQLDecodeNum(len, Load8, data, &int8_value_);
    case INT16:
      return CQLDecodeNum(len, NetworkByteOrder::Load16, data, &int16_value_);
    case INT32:
      return CQLDecodeNum(len, NetworkByteOrder::Load32, data, &int32_value_);
    case INT64:
      return CQLDecodeNum(len, NetworkByteOrder::Load64, data, &int64_value_);
    case FLOAT:
      return CQLDecodeFloat(len, NetworkByteOrder::Load32, data, &float_value_);
    case DOUBLE:
      return CQLDecodeFloat(len, NetworkByteOrder::Load64, data, &double_value_);
    case STRING:
      return CQLDecodeBytes(len, data, &string_value_);
    case BOOL: {
      uint8_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, Load8, data, &value));
      bool_value_ = (value != 0);
      return Status::OK();;
    }
    case TIMESTAMP: {
      int64_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, NetworkByteOrder::Load64, data, &value));
      value = DateTime::AdjustPrecision(value,
          DateTime::CqlDateTimeInputFormat.input_precision(), DateTime::internal_precision);
      return timestamp_value_.FromInt64(value);
    }

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type;
  return STATUS(RuntimeError, "unsupported type");
}

string YQLValueCore::ToString(const DataType type, const bool is_null) const {
  string s = DataType_Name(type) + ":";
  if (is_null) {
    return s + "null";
  }

  switch (type) {
    case INT8: return s + to_string(int8_value_);
    case INT16: return s + to_string(int16_value_);
    case INT32: return s + to_string(int32_value_);
    case INT64: return s + to_string(int64_value_);
    case FLOAT: return s + to_string(float_value_);
    case DOUBLE: return s + to_string(double_value_);
    case STRING: return s + FormatBytesAsStr(string_value_);
    case TIMESTAMP: return s + timestamp_value_.ToFormattedString();
    case BOOL: return s + (bool_value_ ? "true" : "false");

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;
    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type;
  return s;
}

//------------------------------------------- YQLValue ----------------------------------------
YQLValue& YQLValue::operator=(const YQLValue& other) {
  CHECK_EQ(type_, other.type_);
  is_null_ = other.is_null_;
  Free(type_);
  new(this) YQLValueCore(other.type_, other.is_null_, other);
  return *this;
}

YQLValue& YQLValue::operator=(YQLValue&& other) {
  CHECK_EQ(type_, other.type_);
  is_null_ = other.is_null_;
  Free(type_);
  new(this) YQLValueCore(other.type_, other.is_null_, &other);
  return *this;
}

int YQLValue::CompareTo(const YQLValue& v) const {
  CHECK_EQ(type_, v.type_);
  CHECK(!is_null_);
  CHECK(!v.is_null_);
  switch (type_) {
    case INT8:   return GenericCompare(int8_value_, v.int8_value_);
    case INT16:  return GenericCompare(int16_value_, v.int16_value_);
    case INT32:  return GenericCompare(int32_value_, v.int32_value_);
    case INT64:  return GenericCompare(int64_value_, v.int64_value_);
    case FLOAT:  return GenericCompare(float_value_, v.float_value_);
    case DOUBLE: return GenericCompare(double_value_, v.double_value_);
    case STRING: return string_value_.compare(v.string_value_);
    case BOOL:
      LOG(FATAL) << "Internal error: bool type not comparable";
      return 0;
    case TIMESTAMP:
      return GenericCompare(timestamp_value_.ToInt64(), v.timestamp_value_.ToInt64());

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type_;
  return 0;
}

YQLValue YQLValue::FromYQLValuePB(const YQLValuePB& vpb) {
  CHECK(vpb.has_datatype());
  YQLValue v(vpb.datatype());
  switch (vpb.datatype()) {
    case INT8:
      if (vpb.has_int8_value()) {
        v.set_int8_value(static_cast<int8_t>(vpb.int8_value()));
      }
      return v;
    case INT16:
      if (vpb.has_int16_value()) {
        v.set_int16_value(static_cast<int16_t>(vpb.int16_value()));
      }
      return v;
    case INT32:
      if (vpb.has_int32_value()) {
        v.set_int32_value(vpb.int32_value());
      }
      return v;
    case INT64:
      if (vpb.has_int64_value()) {
        v.set_int64_value(vpb.int64_value());
      }
      return v;
    case FLOAT:
      if (vpb.has_float_value()) {
        v.set_float_value(vpb.float_value());
      }
      return v;
    case DOUBLE:
      if (vpb.has_double_value()) {
        v.set_double_value(vpb.double_value());
      }
      return v;
    case STRING:
      if (vpb.has_string_value()) {
        v.set_string_value(vpb.string_value());
      }
      return v;
    case BOOL:
      if (vpb.has_bool_value()) {
        v.set_bool_value(vpb.bool_value());
      }
      return v;
    case TIMESTAMP:
      if (vpb.has_timestamp_value()) {
        v.set_timestamp_value(Timestamp(vpb.timestamp_value()));
      }
      return v;

    YQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported datatype " << vpb.datatype();
}

} // namespace yb
