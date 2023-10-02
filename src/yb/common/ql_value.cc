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

#include "yb/common/ql_value.h"

#include <glog/logging.h>

#include "yb/common/jsonb.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/value.messages.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/date_time.h"
#include "yb/util/decimal.h"
#include "yb/util/enums.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/varint.h"
#include "yb/util/flags.h"

using yb::operator"" _MB;

// Maximumum value size is 64MB
DEFINE_UNKNOWN_int32(yql_max_value_size, 64_MB,
             "Maximum size of a value in the Yugabyte Query Layer");

namespace yb {

using std::string;
using std::shared_ptr;
using std::to_string;
using std::vector;
using util::Decimal;
using common::Jsonb;

template<typename T>
static int GenericCompare(const T& lhs, const T& rhs) {
  if (lhs < rhs) return -1;
  if (lhs > rhs) return 1;
  return 0;
}

template <class PB>
int TupleCompare(const PB& lhs, const PB& rhs);

//------------------------- instance methods for abstract QLValue class -----------------------

int QLValue::CompareTo(const QLValue& other) const {
  if (!IsVirtual() && other.IsVirtual()) {
    return -other.CompareTo(*this);
  }

  CHECK(type() == other.type() || EitherIsVirtual(other));
  CHECK(!IsNull());
  CHECK(!other.IsNull());
  switch (type()) {
    case InternalType::kInt8Value:   return GenericCompare(int8_value(), other.int8_value());
    case InternalType::kInt16Value:  return GenericCompare(int16_value(), other.int16_value());
    case InternalType::kInt32Value:  return GenericCompare(int32_value(), other.int32_value());
    case InternalType::kInt64Value:  return GenericCompare(int64_value(), other.int64_value());
    case InternalType::kUint32Value:  return GenericCompare(uint32_value(), other.uint32_value());
    case InternalType::kUint64Value:  return GenericCompare(uint64_value(), other.uint64_value());
    case InternalType::kFloatValue:  {
      bool is_nan_0 = util::IsNanFloat(float_value());
      bool is_nan_1 = util::IsNanFloat(other.float_value());
      if (is_nan_0 && is_nan_1) return 0;
      if (is_nan_0 && !is_nan_1) return 1;
      if (!is_nan_0 && is_nan_1) return -1;
      return GenericCompare(float_value(), other.float_value());
    }
    case InternalType::kDoubleValue: {
      bool is_nan_0 = util::IsNanDouble(double_value());
      bool is_nan_1 = util::IsNanDouble(other.double_value());
      if (is_nan_0 && is_nan_1) return 0;
      if (is_nan_0 && !is_nan_1) return 1;
      if (!is_nan_0 && is_nan_1) return -1;
      return GenericCompare(double_value(), other.double_value());
    }
    // Encoded decimal is byte-comparable.
    case InternalType::kDecimalValue: return decimal_value().compare(other.decimal_value());
    case InternalType::kVarintValue:  return varint_value().CompareTo(other.varint_value());
    case InternalType::kStringValue: return string_value().compare(other.string_value());
    case InternalType::kBoolValue: return Compare(bool_value(), other.bool_value());
    case InternalType::kTimestampValue:
      return GenericCompare(timestamp_value(), other.timestamp_value());
    case InternalType::kBinaryValue: return binary_value().compare(other.binary_value());
    case InternalType::kInetaddressValue:
      return GenericCompare(inetaddress_value(), other.inetaddress_value());
    case InternalType::kJsonbValue:
      return GenericCompare(jsonb_value(), other.jsonb_value());
    case InternalType::kUuidValue:
      return GenericCompare(uuid_value(), other.uuid_value());
    case InternalType::kTimeuuidValue:
      return GenericCompare(timeuuid_value(), other.timeuuid_value());
    case InternalType::kDateValue: return GenericCompare(date_value(), other.date_value());
    case InternalType::kTimeValue: return GenericCompare(time_value(), other.time_value());
    case InternalType::kFrozenValue: {
      return Compare(frozen_value(), other.frozen_value());
    }
    case InternalType::kTupleValue:
      return TupleCompare(tuple_value(), other.tuple_value());
    case InternalType::kMapValue: FALLTHROUGH_INTENDED;
    case InternalType::kSetValue: FALLTHROUGH_INTENDED;
    case InternalType::kListValue:
      LOG(FATAL) << "Internal error: collection types are not comparable";
      return 0;

    case InternalType::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      break;

    case InternalType::kVirtualValue:
      if (IsMax()) {
        return other.IsMax() ? 0 : 1;
      } else {
        return other.IsMin() ? 0 : -1;
      }

    case InternalType::kGinNullValue: {
      return GenericCompare(gin_null_value(), other.gin_null_value());
    }
  }

  LOG(FATAL) << "Internal error: unsupported type " << type();
  return 0;
}

namespace {

void AppendBytesToKey(const std::string& str, string* bytes) {
  YBPartition::AppendBytesToKey(str.c_str(), str.length(), bytes);
}

void AppendBytesToKey(const Slice& str, string* bytes) {
  YBPartition::AppendBytesToKey(str.cdata(), str.size(), bytes);
}

// TODO(mihnea) After the hash changes, this method does not do the key encoding anymore
// (not needed for hash computation), so AppendToBytes() is better describes what this method does.
// The internal methods such as AppendIntToKey should be renamed accordingly.
template <class PB>
void DoAppendToKey(const PB& value_pb, string* bytes) {
  switch (value_pb.value_case()) {
    case InternalType::kBoolValue: {
      YBPartition::AppendIntToKey<bool, uint8>(value_pb.bool_value() ? 1 : 0, bytes);
      break;
    }
    case InternalType::kInt8Value: {
      YBPartition::AppendIntToKey<int8, uint8>(value_pb.int8_value(), bytes);
      break;
    }
    case InternalType::kInt16Value: {
      YBPartition::AppendIntToKey<int16, uint16>(value_pb.int16_value(), bytes);
      break;
    }
    case InternalType::kInt32Value: {
      YBPartition::AppendIntToKey<int32, uint32>(value_pb.int32_value(), bytes);
      break;
    }
    case InternalType::kInt64Value: {
      YBPartition::AppendIntToKey<int64, uint64>(value_pb.int64_value(), bytes);
      break;
    }
    case InternalType::kUint32Value: {
      YBPartition::AppendIntToKey<uint32, uint32>(value_pb.uint32_value(), bytes);
      break;
    }
    case InternalType::kUint64Value: {
      YBPartition::AppendIntToKey<uint64, uint64>(value_pb.uint64_value(), bytes);
      break;
    }
    case InternalType::kTimestampValue: {
      YBPartition::AppendIntToKey<int64, uint64>(value_pb.timestamp_value(), bytes);
      break;
    }
    case InternalType::kDateValue: {
      YBPartition::AppendIntToKey<uint32, uint32>(value_pb.date_value(), bytes);
      break;
    }
    case InternalType::kTimeValue: {
      YBPartition::AppendIntToKey<int64, uint64>(value_pb.time_value(), bytes);
      break;
    }
    case InternalType::kStringValue: {
      AppendBytesToKey(value_pb.string_value(), bytes);
      break;
    }
    case InternalType::kUuidValue: {
      AppendBytesToKey(value_pb.uuid_value(), bytes);
      break;
    }
    case InternalType::kTimeuuidValue: {
      AppendBytesToKey(value_pb.timeuuid_value(), bytes);
      break;
    }
    case InternalType::kInetaddressValue: {
      AppendBytesToKey(value_pb.inetaddress_value(), bytes);
      break;
    }
    case InternalType::kDecimalValue: {
      AppendBytesToKey(value_pb.decimal_value(), bytes);
      break;
    }
    case InternalType::kVarintValue: {
      AppendBytesToKey(value_pb.varint_value(), bytes);
      break;
    }
    case InternalType::kBinaryValue: {
      AppendBytesToKey(value_pb.binary_value(), bytes);
      break;
    }
    case InternalType::kFloatValue: {
      YBPartition::AppendIntToKey<float, uint32>(util::CanonicalizeFloat(value_pb.float_value()),
                                                 bytes);
      break;
    }
    case InternalType::kDoubleValue: {
      YBPartition::AppendIntToKey<double, uint64>(util::CanonicalizeDouble(value_pb.double_value()),
                                                  bytes);
      break;
    }
    case InternalType::kFrozenValue: {
      for (const auto& elem_pb : value_pb.frozen_value().elems()) {
        AppendToKey(elem_pb, bytes);
      }
      break;
    }
    case InternalType::VALUE_NOT_SET:
      break;
    case InternalType::kMapValue: FALLTHROUGH_INTENDED;
    case InternalType::kSetValue: FALLTHROUGH_INTENDED;
    case InternalType::kListValue: FALLTHROUGH_INTENDED;
    case InternalType::kTupleValue: FALLTHROUGH_INTENDED;
    case InternalType::kJsonbValue:
      LOG(FATAL) << "Runtime error: This datatype("
                 << int(value_pb.value_case())
                 << ") is not supported in hash key";
    case InternalType::kVirtualValue:
      LOG(FATAL) << "Runtime error: virtual value should not be used to construct hash key";
    case InternalType::kGinNullValue: {
      LOG(ERROR) << "Runtime error: gin null value should not be used to construct hash key";
      YBPartition::AppendIntToKey<uint8, uint8>(value_pb.gin_null_value(), bytes);
      break;
    }
  }
}

} // namespace

void AppendToKey(const QLValuePB &value_pb, std::string *bytes) {
  DoAppendToKey(value_pb, bytes);
}

void AppendToKey(const LWQLValuePB &value_pb, std::string *bytes) {
  DoAppendToKey(value_pb, bytes);
}

Status CheckForNull(const QLValue& val) {
  return val.IsNull() ? STATUS(InvalidArgument, "null is not supported inside collections")
                      : Status::OK();
}

Status QLValue::Deserialize(
    const std::shared_ptr<QLType>& ql_type, const QLClient& client, Slice* data) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  int32_t len = 0;
  RETURN_NOT_OK(CQLDecodeNum(sizeof(len), NetworkByteOrder::Load32, data, &len));
  if (len == -1) {
    SetNull();
    return Status::OK();
  }
  if (len > FLAGS_yql_max_value_size) {
    return STATUS_SUBSTITUTE(NotSupported,
        "Value size ($0) is longer than max value size supported ($1)",
        len, FLAGS_yql_max_value_size);
  }

  switch (ql_type->main()) {
    case DataType::INT8:
      return CQLDeserializeNum(
          len, Load8, static_cast<void (QLValue::*)(int8_t)>(&QLValue::set_int8_value), data);
    case DataType::INT16:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load16,
          static_cast<void (QLValue::*)(int16_t)>(&QLValue::set_int16_value), data);
    case DataType::INT32:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load32,
          static_cast<void (QLValue::*)(int32_t)>(&QLValue::set_int32_value), data);
    case DataType::INT64:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load64,
          static_cast<void (QLValue::*)(int64_t)>(&QLValue::set_int64_value), data);
    case DataType::FLOAT:
      return CQLDeserializeFloat(
          len, NetworkByteOrder::Load32,
          static_cast<void (QLValue::*)(float)>(&QLValue::set_float_value), data);
    case DataType::DOUBLE:
      return CQLDeserializeFloat(
          len, NetworkByteOrder::Load64,
          static_cast<void (QLValue::*)(double)>(&QLValue::set_double_value), data);
    case DataType::DECIMAL: {
      string value;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &value));
      Decimal decimal;
      RETURN_NOT_OK(decimal.DecodeFromSerializedBigDecimal(value));
      set_decimal_value(decimal.EncodeToComparable());
      return Status::OK();
    }
    case DataType::VARINT: {
      string value;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &value));
      VarInt varint;
      RETURN_NOT_OK(varint.DecodeFromTwosComplement(value));
      set_varint_value(varint);
      return Status::OK();
    }
    case DataType::STRING:
      return CQLDecodeBytes(len, data, mutable_string_value());
    case DataType::BOOL: {
      uint8_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, Load8, data, &value));
      set_bool_value(value != 0);
      return Status::OK();
    }
    case DataType::BINARY:
      return CQLDecodeBytes(len, data, mutable_binary_value());
    case DataType::TIMESTAMP: {
      int64_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, NetworkByteOrder::Load64, data, &value));
      value = DateTime::AdjustPrecision(value,
                                        DateTime::CqlInputFormat.input_precision,
                                        DateTime::kInternalPrecision);
      set_timestamp_value(value);
      return Status::OK();
    }
    case DataType::DATE: {
      uint32_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, NetworkByteOrder::Load32, data, &value));
      set_date_value(value);
      return Status::OK();
    }
    case DataType::TIME: {
      int64_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, NetworkByteOrder::Load64, data, &value));
      set_time_value(value);
      return Status::OK();
    }
    case DataType::INET: {
      string bytes;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &bytes));
      InetAddress addr;
      RETURN_NOT_OK(addr.FromSlice(bytes));
      set_inetaddress_value(addr);
      return Status::OK();
    }
    case DataType::JSONB: {
      string json;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &json));
      Jsonb jsonb;
      RETURN_NOT_OK(jsonb.FromString(json));
      set_jsonb_value(jsonb.MoveSerializedJsonb());
      return Status::OK();
    }
    case DataType::UUID: {
      string bytes;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &bytes));
      set_uuid_value(VERIFY_RESULT(Uuid::FromSlice(bytes)));
      return Status::OK();
    }
    case DataType::TIMEUUID: {
      string bytes;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &bytes));
      Uuid uuid = VERIFY_RESULT(Uuid::FromSlice(bytes));
      RETURN_NOT_OK(uuid.IsTimeUuid());
      set_timeuuid_value(uuid);
      return Status::OK();
    }
    case DataType::MAP: {
      const shared_ptr<QLType>& keys_type = ql_type->param_type(0);
      const shared_ptr<QLType>& values_type = ql_type->param_type(1);
      set_map_value();
      int32_t nr_elems = 0;
      RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
      for (int i = 0; i < nr_elems; i++) {
        QLValue key;
        RETURN_NOT_OK(key.Deserialize(keys_type, client, data));
        RETURN_NOT_OK(CheckForNull(key));
        *add_map_key() = std::move(*key.mutable_value());
        QLValue value;
        RETURN_NOT_OK(value.Deserialize(values_type, client, data));
        RETURN_NOT_OK(CheckForNull(value));
        *add_map_value() = std::move(*value.mutable_value());
      }
      return Status::OK();
    }
    case DataType::SET: {
      const shared_ptr<QLType>& elems_type = ql_type->param_type(0);
      set_set_value();
      int32_t nr_elems = 0;
      RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
      for (int i = 0; i < nr_elems; i++) {
        QLValue elem;
        RETURN_NOT_OK(elem.Deserialize(elems_type, client, data));
        RETURN_NOT_OK(CheckForNull(elem));
        *add_set_elem() = std::move(*elem.mutable_value());
      }
      return Status::OK();
    }
    case DataType::LIST: {
      const shared_ptr<QLType>& elems_type = ql_type->param_type(0);
      set_list_value();
      int32_t nr_elems = 0;
      RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
      for (int i = 0; i < nr_elems; i++) {
        QLValue elem;
        RETURN_NOT_OK(elem.Deserialize(elems_type, client, data));
        RETURN_NOT_OK(CheckForNull(elem));
        *add_list_elem() = std::move(*elem.mutable_value());
      }
      return Status::OK();
    }

    case DataType::USER_DEFINED_TYPE: {
      set_map_value();
      int fields_size = narrow_cast<int>(ql_type->udtype_field_names().size());
      for (int i = 0; i < fields_size; i++) {
        // TODO (mihnea) default to null if value missing (CQL behavior)
        QLValue value;
        RETURN_NOT_OK(value.Deserialize(ql_type->param_type(i), client, data));
        if (!value.IsNull()) {
          add_map_key()->set_int16_value(i);
          *add_map_value() = std::move(*value.mutable_value());
        }
      }
      return Status::OK();
    }

    case DataType::FROZEN: {
      set_frozen_value();
      const auto& type = ql_type->param_type(0);
      switch (type->main()) {
        case DataType::MAP: {
          std::map<QLValue, QLValue> map_values;
          const shared_ptr<QLType> &keys_type = type->param_type(0);
          const shared_ptr<QLType> &values_type = type->param_type(1);
          int32_t nr_elems = 0;
          RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
          for (int i = 0; i < nr_elems; i++) {
            QLValue key;
            RETURN_NOT_OK(key.Deserialize(keys_type, client, data));
            RETURN_NOT_OK(CheckForNull(key));
            QLValue value;
            RETURN_NOT_OK(value.Deserialize(values_type, client, data));
            RETURN_NOT_OK(CheckForNull(key));
            map_values[key] = value;
          }

          for (auto &pair : map_values) {
            *add_frozen_elem() = std::move(pair.first.value());
            *add_frozen_elem() = std::move(pair.second.value());
          }

          return Status::OK();
        }
        case DataType::SET: {
          const shared_ptr<QLType> &elems_type = type->param_type(0);
          int32_t nr_elems = 0;

          std::set<QLValue> set_values;
          RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
          for (int i = 0; i < nr_elems; i++) {
            QLValue elem;
            RETURN_NOT_OK(elem.Deserialize(elems_type, client, data));
            RETURN_NOT_OK(CheckForNull(elem));
            set_values.insert(std::move(elem));
          }
          for (auto &elem : set_values) {
            *add_frozen_elem() = std::move(elem.value());
          }
          return Status::OK();
        }
        case DataType::LIST: {
          const shared_ptr<QLType> &elems_type = type->param_type(0);
          int32_t nr_elems = 0;
          RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
          for (int i = 0; i < nr_elems; i++) {
            QLValue elem;
            RETURN_NOT_OK(elem.Deserialize(elems_type, client, data));
            RETURN_NOT_OK(CheckForNull(elem));
            *add_frozen_elem() = std::move(*elem.mutable_value());
          }
          return Status::OK();
        }

        case DataType::USER_DEFINED_TYPE: {
          const int fields_size = narrow_cast<int>(type->udtype_field_names().size());
          for (int i = 0; i < fields_size; i++) {
            // TODO (mihnea) default to null if value missing (CQL behavior)
            QLValue value;
            RETURN_NOT_OK(value.Deserialize(type->param_type(i), client, data));
            *add_frozen_elem() = std::move(*value.mutable_value());
          }
          return Status::OK();
        }
        default:
          break;

      }
      break;
    }

    case DataType::TUPLE: {
      set_tuple_value();
      size_t num_elems = ql_type->params().size();
      for (size_t i = 0; i < num_elems; ++i) {
        const shared_ptr<QLType>& elem_type = ql_type->param_type(i);
        QLValue elem;
        RETURN_NOT_OK(elem.Deserialize(elem_type, client, data));
        RETURN_NOT_OK(CheckForNull(elem));
        *add_tuple_elem() = std::move(*elem.mutable_value());
      }
      return Status::OK();
    }

    QL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    QL_INVALID_TYPES_IN_SWITCH:
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << ql_type->ToString();
  return STATUS(InternalError, "unsupported type");
}

string QLValue::ToValueString(const QuotesType quotes_type) const {
  if (IsNull()) {
    return "null";
  }

  switch (type()) {
    case InternalType::kInt8Value: return to_string(int8_value());
    case InternalType::kInt16Value: return to_string(int16_value());
    case InternalType::kInt32Value: return to_string(int32_value());
    case InternalType::kInt64Value: return to_string(int64_value());
    case InternalType::kUint32Value: return to_string(uint32_value());
    case InternalType::kUint64Value: return to_string(uint64_value());
    case InternalType::kFloatValue: return to_string(float_value());
    case InternalType::kDoubleValue: return to_string(double_value());
    case InternalType::kDecimalValue:
      return util::DecimalFromComparable(decimal_value()).ToString();
    case InternalType::kVarintValue: return varint_value().ToString();
    case InternalType::kStringValue: return FormatBytesAsStr(string_value(), quotes_type);
    case InternalType::kTimestampValue: return timestamp_value().ToFormattedString();
    case InternalType::kDateValue: return to_string(date_value());
    case InternalType::kTimeValue: return to_string(time_value());
    case InternalType::kInetaddressValue: return inetaddress_value().ToString();
    case InternalType::kJsonbValue: return FormatBytesAsStr(jsonb_value(), quotes_type);
    case InternalType::kUuidValue: return uuid_value().ToString();
    case InternalType::kTimeuuidValue: return timeuuid_value().ToString();
    case InternalType::kBoolValue: return (bool_value() ? "true" : "false");
    case InternalType::kBinaryValue: return "0x" + b2a_hex(binary_value());

    case InternalType::kMapValue: {
      std::stringstream ss;
      QLMapValuePB map = map_value();
      DCHECK_EQ(map.keys_size(), map.values_size());
      ss << "{";
      for (int i = 0; i < map.keys_size(); i++) {
        if (i > 0) {
          ss << ", ";
        }
        ss << QLValue(map.keys(i)).ToString() << " -> "
           << QLValue(map.values(i)).ToString();
      }
      ss << "}";
      return ss.str();
    }
    case InternalType::kSetValue: {
      std::stringstream ss;
      QLSeqValuePB set = set_value();
      ss << "{";
      for (int i = 0; i < set.elems_size(); i++) {
        if (i > 0) {
          ss << ", ";
        }
        ss << QLValue(set.elems(i)).ToString();
      }
      ss << "}";
      return ss.str();
    }
    case InternalType::kListValue: {
      std::stringstream ss;
      QLSeqValuePB list = list_value();
      ss << "[";
      for (int i = 0; i < list.elems_size(); i++) {
        if (i > 0) {
          ss << ", ";
        }
        ss << QLValue(list.elems(i)).ToString();
      }
      ss << "]";
      return ss.str();
    }

    case InternalType::kFrozenValue: {
      std::stringstream ss;
      QLSeqValuePB frozen = frozen_value();
      ss << "frozen:<";
      for (int i = 0; i < frozen.elems_size(); i++) {
        if (i > 0) {
          ss << ", ";
        }
        ss << QLValue(frozen.elems(i)).ToString();
      }
      ss << ">";
      return ss.str();
    }
    case InternalType::kVirtualValue:
      if (IsMax()) {
        return "<MAX_LIMIT>";
      }
      return "<MIN_LIMIT>";
    case InternalType::kGinNullValue: {
      switch (gin_null_value()) {
        // case 0, gin:norm-key, should not exist since the actual data would be used instead.
        case 1:
          return "gin:null-key";
        case 2:
          return "gin:empty-item";
        case 3:
          return "gin:null-item";
        // case -1, gin:empty-query, should not exist since that's internal to postgres.
        default:
          LOG(FATAL) << "Unexpected gin null category: " << gin_null_value();
      }
    }

    case InternalType::kTupleValue: {
      std::stringstream ss;
      QLSeqValuePB tuple = tuple_value();
      ss << "(";
      for (int i = 0; i < tuple.elems_size(); i++) {
        if (i > 0) {
          ss << ", ";
        }
        ss << QLValue(tuple.elems(i)).ToString();
      }
      ss << ")";
      return ss.str();
    }

    case InternalType::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      return "null";
    // default: fall through
  }

  LOG(FATAL) << "Internal error: unknown or unsupported type " << type();
  return "unknown";
}

string QLValue::ToString() const {
  if (IsNull()) {
    return "null";
  }

  std::string res;
  switch (type()) {
    case InternalType::kInt8Value: res = "int8:"; break;
    case InternalType::kInt16Value: res = "int16:"; break;
    case InternalType::kInt32Value: res = "int32:"; break;
    case InternalType::kInt64Value: res = "int64:"; break;
    case InternalType::kUint32Value: res = "uint32:"; break;
    case InternalType::kUint64Value: res = "uint64:"; break;
    case InternalType::kFloatValue: res = "float:"; break;
    case InternalType::kDoubleValue: res = "double:"; break;
    case InternalType::kDecimalValue: res = "decimal: "; break;
    case InternalType::kVarintValue: res = "varint: "; break;
    case InternalType::kStringValue: res = "string:"; break;
    case InternalType::kTimestampValue: res = "timestamp:"; break;
    case InternalType::kDateValue: res = "date:"; break;
    case InternalType::kTimeValue: res = "time:"; break;
    case InternalType::kInetaddressValue: res = "inetaddress:"; break;
    case InternalType::kJsonbValue: res = "jsonb:"; break;
    case InternalType::kUuidValue: res = "uuid:"; break;
    case InternalType::kTimeuuidValue: res = "timeuuid:"; break;
    case InternalType::kBoolValue: res = "bool:"; break;
    case InternalType::kBinaryValue: res = "binary:0x"; break;
    case InternalType::kMapValue: res = "map:"; break;
    case InternalType::kSetValue: res = "set:"; break;
    case InternalType::kListValue: res = "list:"; break;
    case InternalType::kFrozenValue: res = "frozen:"; break;
    case InternalType::kVirtualValue: res = ""; break;
    case InternalType::kTupleValue: res = "tuple:"; break;

    case InternalType::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      return "null";
    default:
      LOG(FATAL) << "Internal error: unknown or unsupported type " << type();
      return "unknown";
  }
  return res + ToValueString();
}

InetAddress QLValue::inetaddress_value(const QLValuePB& pb) {
  InetAddress addr;
  CHECK_OK(addr.FromSlice(inetaddress_value_pb(pb)));
  return addr;
}

InetAddress QLValue::inetaddress_value(const LWQLValuePB& pb) {
  InetAddress addr;
  CHECK_OK(addr.FromSlice(inetaddress_value_pb(pb)));
  return addr;
}

Uuid QLValue::timeuuid_value(const QLValuePB& pb) {
  Uuid timeuuid = CHECK_RESULT(Uuid::FromSlice(timeuuid_value_pb(pb)));
  CHECK_OK(timeuuid.IsTimeUuid());
  return timeuuid;
}

Uuid QLValue::timeuuid_value(const LWQLValuePB& pb) {
  Uuid timeuuid = CHECK_RESULT(Uuid::FromSlice(timeuuid_value_pb(pb)));
  CHECK_OK(timeuuid.IsTimeUuid());
  return timeuuid;
}

Uuid QLValue::uuid_value(const QLValuePB& pb) {
  return CHECK_RESULT(Uuid::FromSlice(uuid_value_pb(pb)));
}

Uuid QLValue::uuid_value(const LWQLValuePB& pb) {
  return CHECK_RESULT(Uuid::FromSlice(uuid_value_pb(pb)));
}

VarInt QLValue::varint_value(const QLValuePB& pb) {
  CHECK(pb.has_varint_value()) << "Value: " << pb.ShortDebugString();
  VarInt varint;
  size_t num_decoded_bytes;
  CHECK_OK(varint.DecodeFromComparable(pb.varint_value(), &num_decoded_bytes));
  return varint;
}

VarInt QLValue::varint_value(const LWQLValuePB& pb) {
  CHECK(pb.has_varint_value()) << "Value: " << pb.ShortDebugString();
  VarInt varint;
  size_t num_decoded_bytes;
  CHECK_OK(varint.DecodeFromComparable(pb.varint_value(), &num_decoded_bytes));
  return varint;
}

void QLValue::set_inetaddress_value(const InetAddress& val, QLValuePB* pb) {
  pb->mutable_inetaddress_value()->clear();
  val.AppendToBytes(pb->mutable_inetaddress_value());
}

void QLValue::set_timeuuid_value(const Uuid& val, LWQLValuePB* out) {
  CHECK_OK(val.IsTimeUuid());
  auto* dest = static_cast<uint8_t*>(out->arena().AllocateBytes(kUuidSize));
  val.ToBytes(dest);
  out->ref_timeuuid_value(Slice(dest, kUuidSize));
}

void QLValue::set_timeuuid_value(const Uuid& val, QLValuePB* out) {
  CHECK_OK(val.IsTimeUuid());
  val.ToBytes(out->mutable_timeuuid_value());
}

void QLValue::set_timeuuid_value(const Uuid& val, QLValue* out) {
  set_timeuuid_value(val, out->mutable_value());
}

void QLValue::set_timeuuid_value(const Uuid& val) {
  set_timeuuid_value(val, this);
}

template<typename num_type, typename data_type>
Status QLValue::CQLDeserializeNum(
    size_t len, data_type (*converter)(const void*), void (QLValue::*setter)(num_type),
    Slice* data) {
  num_type value = 0;
  RETURN_NOT_OK(CQLDecodeNum(len, converter, data, &value));
  (this->*setter)(value);
  return Status::OK();
}

template<typename float_type, typename data_type>
Status QLValue::CQLDeserializeFloat(
    size_t len, data_type (*converter)(const void*), void (QLValue::*setter)(float_type),
    Slice* data) {
  float_type value = 0.0;
  RETURN_NOT_OK(CQLDecodeFloat(len, converter, data, &value));
  (this->*setter)(value);
  return Status::OK();
}

QLValuePB QLValue::Primitive(const std::string& str) {
  QLValuePB result;
  result.set_string_value(str);
  return result;
}

QLValuePB QLValue::Primitive(double value) {
  QLValuePB result;
  result.set_double_value(value);
  return result;
}

QLValuePB QLValue::Primitive(int32_t value) {
  QLValuePB result;
  result.set_int32_value(value);
  return result;
}

QLValuePB QLValue::PrimitiveInt64(int64_t value) {
  QLValuePB result;
  result.set_int64_value(value);
  return result;
}

Timestamp QLValue::timestamp_value(const QLValuePB& pb) {
  return Timestamp(timestamp_value_pb(pb));
}

Timestamp QLValue::timestamp_value(const LWQLValuePB& pb) {
  return Timestamp(timestamp_value_pb(pb));
}

Timestamp QLValue::timestamp_value(const QLValue& value) {
  return timestamp_value(value.value());
}

//----------------------------------- QLValuePB operators --------------------------------

InternalType type(const QLValuePB& v) {
  return v.value_case();
}
bool IsNull(const QLValuePB& v) {
  return v.value_case() == QLValuePB::VALUE_NOT_SET;
}

bool IsNull(const LWQLValuePB& v) {
  return v.value_case() == QLValuePB::VALUE_NOT_SET;
}

void SetNull(QLValuePB* v) {
  v->Clear();
}

void SetNull(LWQLValuePB* v) {
  v->Clear();
}

bool EitherIsNull(const QLValuePB& lhs, const QLValuePB& rhs) {
  return IsNull(lhs) || IsNull(rhs);
}

bool EitherIsNull(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return IsNull(lhs) || IsNull(rhs);
}

bool BothNotNull(const QLValuePB& lhs, const QLValuePB& rhs) {
  return !IsNull(lhs) && !IsNull(rhs);
}

bool BothNull(const QLValuePB& lhs, const QLValuePB& rhs) {
  return IsNull(lhs) && IsNull(rhs);
}

bool BothNull(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return IsNull(lhs) && IsNull(rhs);
}

bool EitherIsVirtual(const QLValuePB& lhs, const QLValuePB& rhs) {
  return lhs.value_case() == QLValuePB::kVirtualValue ||
         rhs.value_case() == QLValuePB::kVirtualValue;
}

bool EitherIsVirtual(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return lhs.value_case() == QLValuePB::kVirtualValue ||
         rhs.value_case() == QLValuePB::kVirtualValue;
}

template <class PB>
bool DoComparable(const PB& lhs, const PB& rhs) {
  return (lhs.value_case() == rhs.value_case() ||
          EitherIsNull(lhs, rhs) ||
          EitherIsVirtual(lhs, rhs));
}

bool Comparable(const QLValuePB& lhs, const QLValuePB& rhs) {
  return DoComparable(lhs, rhs);
}

bool Comparable(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return DoComparable(lhs, rhs);
}

bool EitherIsNull(const QLValuePB& lhs, const QLValue& rhs) {
  return IsNull(lhs) || rhs.IsNull();
}

bool EitherIsVirtual(const QLValuePB& lhs, const QLValue& rhs) {
  return lhs.value_case() == QLValuePB::kVirtualValue || rhs.IsVirtual();
}

bool Comparable(const QLValuePB& lhs, const QLValue& rhs) {
  return (lhs.value_case() == rhs.type() ||
          EitherIsNull(lhs, rhs) ||
          EitherIsVirtual(lhs, rhs));
}

bool BothNotNull(const QLValuePB& lhs, const QLValue& rhs) {
  return !IsNull(lhs) && !rhs.IsNull();
}

bool BothNotNull(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return !IsNull(lhs) && !IsNull(rhs);
}

bool BothNull(const QLValuePB& lhs, const QLValue& rhs) {
  return IsNull(lhs) && rhs.IsNull();
}

template <class PB>
int TupleCompare(const PB& lhs_tuple, const PB& rhs_tuple) {
  DCHECK(lhs_tuple.elems().size() == rhs_tuple.elems().size());
  auto li = lhs_tuple.elems().begin();
  auto ri = rhs_tuple.elems().begin();
  for (auto i = lhs_tuple.elems().size(); i > 0; --i, ++li, ++ri) {
    if (IsNull(*li)) {
      if (!IsNull(*ri)) {
        return -1;
      }
    } else {
      if (IsNull(*ri)) {
        return 1;
      }
      int result = Compare(*li, *ri);
      if (result != 0) {
        return result;
      }
    }
  }
  return 0;
}

template <class Seq>
int SeqCompare(const Seq& lhs, const Seq& rhs) {
  // Compare elements one by one.
  auto min_size = std::min(lhs.elems().size(), rhs.elems().size());
  auto li = lhs.elems().begin();
  auto ri = rhs.elems().begin();
  for (auto i = min_size; i > 0; --i, ++li, ++ri) {
    if (IsNull(*li)) {
      if (!IsNull(*ri)) {
        return -1;
      }
    } else {
      if (IsNull(*ri)) {
        return 1;
      }
      int result = DoCompare(*li, *ri);
      if (result != 0) {
        return result;
      }
    }
  }

  // If elements are equal, compare lengths.
  return GenericCompare(lhs.elems().size(), rhs.elems().size());
}

template <class PB>
int DoCompare(const PB& lhs, const PB& rhs) {
  if (rhs.value_case() == QLValuePB::kVirtualValue &&
      lhs.value_case() != QLValuePB::kVirtualValue) {
    return -DoCompare(rhs, lhs);
  }
  CHECK(DoComparable(lhs, rhs));
  CHECK(BothNotNull(lhs, rhs));
  switch (lhs.value_case()) {
    case QLValuePB::kInt8Value:   return GenericCompare(lhs.int8_value(), rhs.int8_value());
    case QLValuePB::kInt16Value:  return GenericCompare(lhs.int16_value(), rhs.int16_value());
    case QLValuePB::kInt32Value:  return GenericCompare(lhs.int32_value(), rhs.int32_value());
    case QLValuePB::kInt64Value:  return GenericCompare(lhs.int64_value(), rhs.int64_value());
    case QLValuePB::kUint32Value:  return GenericCompare(lhs.uint32_value(), rhs.uint32_value());
    case QLValuePB::kUint64Value:  return GenericCompare(lhs.uint64_value(), rhs.uint64_value());
    case QLValuePB::kFloatValue:  {
      bool is_nan_0 = util::IsNanFloat(lhs.float_value());
      bool is_nan_1 = util::IsNanFloat(rhs.float_value());
      if (is_nan_0 && is_nan_1) return 0;
      if (is_nan_0 && !is_nan_1) return 1;
      if (!is_nan_0 && is_nan_1) return -1;
      return GenericCompare(lhs.float_value(), rhs.float_value());
    }
    case QLValuePB::kDoubleValue: {
      bool is_nan_0 = util::IsNanDouble(lhs.double_value());
      bool is_nan_1 = util::IsNanDouble(rhs.double_value());
      if (is_nan_0 && is_nan_1) return 0;
      if (is_nan_0 && !is_nan_1) return 1;
      if (!is_nan_0 && is_nan_1) return -1;
      return GenericCompare(lhs.double_value(), rhs.double_value());
    }
    // Encoded decimal is byte-comparable.
    case QLValuePB::kDecimalValue: return lhs.decimal_value().compare(rhs.decimal_value());
    case QLValuePB::kVarintValue: return lhs.varint_value().compare(rhs.varint_value());
    case QLValuePB::kStringValue: return lhs.string_value().compare(rhs.string_value());
    case QLValuePB::kBoolValue: return Compare(lhs.bool_value(), rhs.bool_value());
    case QLValuePB::kTimestampValue:
      return GenericCompare(lhs.timestamp_value(), rhs.timestamp_value());
    case QLValuePB::kDateValue: return GenericCompare(lhs.date_value(), rhs.date_value());
    case QLValuePB::kTimeValue: return GenericCompare(lhs.time_value(), rhs.time_value());
    case QLValuePB::kBinaryValue: return lhs.binary_value().compare(rhs.binary_value());
    case QLValuePB::kInetaddressValue:
      return GenericCompare(lhs.inetaddress_value(), rhs.inetaddress_value());
    case QLValuePB::kJsonbValue:
      return GenericCompare(lhs.jsonb_value(), rhs.jsonb_value());
    case QLValuePB::kUuidValue:
      return GenericCompare(QLValue::uuid_value(lhs), QLValue::uuid_value(rhs));
    case QLValuePB::kTimeuuidValue:
      return GenericCompare(QLValue::timeuuid_value(lhs), QLValue::timeuuid_value(rhs));
    case QLValuePB::kFrozenValue:
      return SeqCompare(lhs.frozen_value(), rhs.frozen_value());
    case QLValuePB::kTupleValue:
      return TupleCompare(lhs.tuple_value(), rhs.tuple_value());
    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      LOG(FATAL) << "Internal error: collection types are not comparable";
      return 0;
    case QLValuePB::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
    case QLValuePB::kVirtualValue:
      if (lhs.virtual_value() == QLVirtualValuePB::LIMIT_MAX) {
        return (rhs.value_case() == QLValuePB::kVirtualValue &&
                rhs.virtual_value() == QLVirtualValuePB::LIMIT_MAX) ? 0 : 1;
      } else {
        return (rhs.value_case() == QLValuePB::kVirtualValue &&
                rhs.virtual_value() == QLVirtualValuePB::LIMIT_MIN) ? 0 : -1;
      }
      break;
    case QLValuePB::kGinNullValue:
      return GenericCompare(lhs.gin_null_value(), rhs.gin_null_value());

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unknown or unsupported type " << lhs.value_case();
  return 0;
}

int Compare(const QLValuePB& lhs, const QLValuePB& rhs) {
  return DoCompare(lhs, rhs);
}

int Compare(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return DoCompare(lhs, rhs);
}

int Compare(const QLValuePB& lhs, const QLValue& rhs) {
  if (rhs.IsVirtual() && lhs.value_case() != QLValuePB::kVirtualValue) {
    return -Compare(rhs.value(), lhs);
  }
  CHECK(Comparable(lhs, rhs));
  CHECK(BothNotNull(lhs, rhs));
  switch (type(lhs)) {
    case QLValuePB::kInt8Value:
      return GenericCompare<int8_t>(lhs.int8_value(), rhs.int8_value());
    case QLValuePB::kInt16Value:
      return GenericCompare<int16_t>(lhs.int16_value(), rhs.int16_value());
    case QLValuePB::kInt32Value:  return GenericCompare(lhs.int32_value(), rhs.int32_value());
    case QLValuePB::kInt64Value:  return GenericCompare(lhs.int64_value(), rhs.int64_value());
    case QLValuePB::kUint32Value:  return GenericCompare(lhs.uint32_value(), rhs.uint32_value());
    case QLValuePB::kUint64Value:  return GenericCompare(lhs.uint64_value(), rhs.uint64_value());
    case QLValuePB::kFloatValue:  {
      bool is_nan_0 = util::IsNanFloat(lhs.float_value());
      bool is_nan_1 = util::IsNanFloat(rhs.float_value());
      if (is_nan_0 && is_nan_1) return 0;
      if (is_nan_0 && !is_nan_1) return 1;
      if (!is_nan_0 && is_nan_1) return -1;
      return GenericCompare(lhs.float_value(), rhs.float_value());
    }
    case QLValuePB::kDoubleValue: {
      bool is_nan_0 = util::IsNanDouble(lhs.double_value());
      bool is_nan_1 = util::IsNanDouble(rhs.double_value());
      if (is_nan_0 && is_nan_1) return 0;
      if (is_nan_0 && !is_nan_1) return 1;
      if (!is_nan_0 && is_nan_1) return -1;
      return GenericCompare(lhs.double_value(), rhs.double_value());
    }
    // Encoded decimal is byte-comparable.
    case QLValuePB::kDecimalValue: return lhs.decimal_value().compare(rhs.decimal_value());
    case QLValuePB::kVarintValue: return lhs.varint_value().compare(rhs.value().varint_value());
    case QLValuePB::kStringValue: return lhs.string_value().compare(rhs.string_value());
    case QLValuePB::kBoolValue: return Compare(lhs.bool_value(), rhs.bool_value());
    case QLValuePB::kTimestampValue:
      return GenericCompare(lhs.timestamp_value(), rhs.timestamp_value_pb());
    case QLValuePB::kDateValue: return GenericCompare(lhs.date_value(), rhs.date_value());
    case QLValuePB::kTimeValue: return GenericCompare(lhs.time_value(), rhs.time_value());
    case QLValuePB::kBinaryValue: return lhs.binary_value().compare(rhs.binary_value());
    case QLValuePB::kInetaddressValue:
      return GenericCompare(QLValue::inetaddress_value(lhs), rhs.inetaddress_value());
    case QLValuePB::kJsonbValue:
      return GenericCompare(QLValue::jsonb_value(lhs), rhs.jsonb_value());
    case QLValuePB::kUuidValue:
      return GenericCompare(QLValue::uuid_value(lhs), rhs.uuid_value());
    case QLValuePB::kTimeuuidValue:
      return GenericCompare(QLValue::timeuuid_value(lhs), rhs.timeuuid_value());
    case QLValuePB::kFrozenValue:
      return Compare(lhs.frozen_value(), rhs.frozen_value());
    case QLValuePB::kTupleValue:
      return TupleCompare(lhs.tuple_value(), rhs.tuple_value());
    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      LOG(FATAL) << "Internal error: collection types are not comparable";
      return 0;
    case QLValuePB::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      break;
    case QLValuePB::kVirtualValue:
      if (lhs.virtual_value() == QLVirtualValuePB::LIMIT_MAX) {
        return rhs.IsMax() ? 0 : 1;
      } else {
        return rhs.IsMin() ? 0 : -1;
      }
      break;
    case QLValuePB::kGinNullValue:
      return GenericCompare<uint8_t>(lhs.gin_null_value(), rhs.gin_null_value());

    // default: fall through
  }

  FATAL_INVALID_ENUM_VALUE(QLValuePB::ValueCase, type(lhs));
}

int Compare(const QLSeqValuePB& lhs, const QLSeqValuePB& rhs) {
  return SeqCompare(lhs, rhs);
}

int Compare(const bool lhs, const bool rhs) {
  // Using Cassandra semantics: true > false.
  if (lhs) {
    return rhs ? 0 : 1;
  } else {
    return rhs ? -1 : 0;
  }
}

// In YCQL null is not comparable with regular values (w.r.t. ordering).
bool operator <(const QLValuePB& lhs, const QLValuePB& rhs) {
  return BothNotNull(lhs, rhs) && Compare(lhs, rhs) < 0;
}
bool operator >(const QLValuePB& lhs, const QLValuePB& rhs) {
  return BothNotNull(lhs, rhs) && Compare(lhs, rhs) > 0;
}

// In YCQL equality holds for null values.
bool operator <=(const QLValuePB& lhs, const QLValuePB& rhs) {
  return (BothNotNull(lhs, rhs) && Compare(lhs, rhs) <= 0) || BothNull(lhs, rhs);
}
bool operator >=(const QLValuePB& lhs, const QLValuePB& rhs) {
  return (BothNotNull(lhs, rhs) && Compare(lhs, rhs) >= 0) || BothNull(lhs, rhs);
}
bool operator ==(const QLValuePB& lhs, const QLValuePB& rhs) {
  return (BothNotNull(lhs, rhs) && Compare(lhs, rhs) == 0) || BothNull(lhs, rhs);
}
bool operator !=(const QLValuePB& lhs, const QLValuePB& rhs) {
  return !(lhs == rhs);
}

// In YCQL null is not comparable with regular values (w.r.t. ordering).
bool operator <(const QLValuePB& lhs, const QLValue& rhs) {
  return BothNotNull(lhs, rhs) && Compare(lhs, rhs) < 0;
}
bool operator >(const QLValuePB& lhs, const QLValue& rhs) {
  return BothNotNull(lhs, rhs) && Compare(lhs, rhs) > 0;
}

// In YCQL equality holds for null values.
bool operator <=(const QLValuePB& lhs, const QLValue& rhs) {
  return (BothNotNull(lhs, rhs) && Compare(lhs, rhs) <= 0) || BothNull(lhs, rhs);
}
bool operator >=(const QLValuePB& lhs, const QLValue& rhs) {
  return (BothNotNull(lhs, rhs) && Compare(lhs, rhs) >= 0) || BothNull(lhs, rhs);
}
bool operator ==(const QLValuePB& lhs, const QLValue& rhs) {
  return (BothNotNull(lhs, rhs) && Compare(lhs, rhs) == 0) || BothNull(lhs, rhs);
}
bool operator !=(const QLValuePB& lhs, const QLValue& rhs) {
  return !(lhs == rhs);
}

bool operator <(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return BothNotNull(lhs, rhs) && Compare(lhs, rhs) < 0;
}
bool operator >(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return BothNotNull(lhs, rhs) && Compare(lhs, rhs) > 0;
}

// In YCQL equality holds for null values.
bool operator <=(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return (BothNotNull(lhs, rhs) && Compare(lhs, rhs) <= 0) || BothNull(lhs, rhs);
}
bool operator >=(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return (BothNotNull(lhs, rhs) && Compare(lhs, rhs) >= 0) || BothNull(lhs, rhs);
}

bool operator ==(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  if (IsNull(lhs)) {
    return IsNull(rhs);
  }
  return !IsNull(rhs) && DoCompare(lhs, rhs) == 0;
}

bool operator !=(const LWQLValuePB& lhs, const LWQLValuePB& rhs) {
  return !(lhs == rhs);
}

void ConcatStrings(const std::string& lhs, const std::string& rhs, QLValuePB* result) {
  result->set_string_value(lhs + rhs);
}

void ConcatStrings(const std::string& lhs, const std::string& rhs, QLValue* result) {
  ConcatStrings(lhs, rhs, result->mutable_value());
}

void ConcatStrings(const Slice& lhs, const Slice& rhs, LWQLValuePB* result) {
  auto* data = static_cast<char*>(result->arena().AllocateBytes(lhs.size() + rhs.size()));
  memcpy(data, lhs.cdata(), lhs.size());
  memcpy(data + lhs.size(), rhs.cdata(), rhs.size());
  result->ref_string_value(Slice(data, lhs.size() + rhs.size()));
}

} // namespace yb
