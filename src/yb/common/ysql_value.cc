// Copyright (c) YugaByte, Inc.
//
// This file contains the YSQLValue class that represents YSQL values.

#include "yb/common/ysql_value.h"

#include <glog/logging.h>

#include "yb/common/wire_protocol.h"

// The list of unsupported datypes to use in switch statements
#define YSQL_UNSUPPORTED_TYPES_IN_SWITCH \
  case UINT8:  FALLTHROUGH_INTENDED;     \
  case UINT16: FALLTHROUGH_INTENDED;     \
  case UINT32: FALLTHROUGH_INTENDED;     \
  case UINT64: FALLTHROUGH_INTENDED;     \
  case BINARY: FALLTHROUGH_INTENDED;     \
  case UNKNOWN_DATA

namespace yb {

void YSQLValueCore::CopyFrom(const DataType type, const YSQLValueCore& v) {
  switch (type) {
    case INT8: int8_value_ = v.int8_value_; return;
    case INT16: int16_value_ = v.int16_value_; return;
    case INT32: int32_value_ = v.int32_value_; return;
    case INT64: int64_value_ = v.int64_value_; return;
    case FLOAT: float_value_ = v.float_value_; return;
    case DOUBLE: double_value_ = v.double_value_; return;
    case STRING: string_value_ = v.string_value_; return;
    case BOOL: bool_value_ = v.bool_value_; return;
    case TIMESTAMP: timestamp_value_ = v.timestamp_value_; return;
    YSQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;
    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type;
}

void YSQLValueCore::Serialize(
    const DataType type, const bool is_null, const YSQLClient client, faststring* buffer) const {
  CHECK_EQ(client, YSQL_CLIENT_CQL);
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
    case TIMESTAMP:
      CQLEncodeNum(NetworkByteOrder::Store64, timestamp_value_.ToUint64(), buffer);
      return;

    YSQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type;
}

Status YSQLValueCore::Deserialize(
    const DataType type, const YSQLClient client, Slice* data, bool* is_null) {

  CHECK_EQ(client, YSQL_CLIENT_CQL);

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
      uint64_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, NetworkByteOrder::Load64, data, &value));
      return timestamp_value_.FromUint64(value);
    }

    YSQL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type;
  return STATUS(RuntimeError, "unsupported type");
}

} // namespace yb
