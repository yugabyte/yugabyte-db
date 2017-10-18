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

#include <cfloat>

#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/date_time.h"
#include "yb/util/decimal.h"
#include "yb/util/enums.h"

#include "yb/util/size_literals.h"

using yb::operator"" _MB;

// Maximumum value size is 64MB
DEFINE_int32(yql_max_value_size, 64_MB,
             "Maximum size of a value in the Yugabyte Query Layer");

// The list of unsupported datypes to use in switch statements
#define QL_UNSUPPORTED_TYPES_IN_SWITCH \
  case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED; \
  case VARINT: FALLTHROUGH_INTENDED;    \
  case TUPLE: FALLTHROUGH_INTENDED;     \
  case TYPEARGS: FALLTHROUGH_INTENDED;  \
  case DATE: FALLTHROUGH_INTENDED;      \
  case TIME: FALLTHROUGH_INTENDED;      \
  case UNKNOWN_DATA

#define QL_INVALID_TYPES_IN_SWITCH     \
  case UINT8:  FALLTHROUGH_INTENDED;    \
  case UINT16: FALLTHROUGH_INTENDED;    \
  case UINT32: FALLTHROUGH_INTENDED;    \
  case UINT64

namespace yb {

using std::string;
using std::shared_ptr;
using std::to_string;
using util::Decimal;
using util::FormatBytesAsStr;

template<typename T>
static int GenericCompare(const T& lhs, const T& rhs) {
  if (lhs < rhs) return -1;
  if (lhs > rhs) return 1;
  return 0;
}

QLValue::~QLValue() {}

//------------------------- instance methods for abstract QLValue class -----------------------

int QLValue::CompareTo(const QLValue& other) const {
  CHECK_EQ(type(), other.type());
  CHECK(!IsNull());
  CHECK(!other.IsNull());
  switch (type()) {
    case InternalType::kInt8Value:   return GenericCompare(int8_value(), other.int8_value());
    case InternalType::kInt16Value:  return GenericCompare(int16_value(), other.int16_value());
    case InternalType::kInt32Value:  return GenericCompare(int32_value(), other.int32_value());
    case InternalType::kInt64Value:  return GenericCompare(int64_value(), other.int64_value());
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
    case InternalType::kStringValue: return string_value().compare(other.string_value());
    case InternalType::kBoolValue:
      LOG(FATAL) << "Internal error: bool type not comparable";
      return 0;
    case InternalType::kTimestampValue:
      return GenericCompare(timestamp_value(), other.timestamp_value());
    case InternalType::kBinaryValue: return binary_value().compare(other.binary_value());
    case InternalType::kInetaddressValue:
      return GenericCompare(inetaddress_value(), other.inetaddress_value());
    case InternalType::kUuidValue:
      return GenericCompare(uuid_value(), other.uuid_value());
    case InternalType::kTimeuuidValue:
      return GenericCompare(timeuuid_value(), other.timeuuid_value());
    case QLValuePB::kFrozenValue: {
      return QLValue::CompareTo(frozen_value(), other.frozen_value());
    }
    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      LOG(FATAL) << "Internal error: collection types are not comparable";
      return 0;

    case InternalType::kVarintValue:
      LOG(FATAL) << "Internal error: varint not implemented";
    case InternalType::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << type();
  return 0;
}

CHECKED_STATUS QLValue::AppendToKeyBytes(const QLValuePB &value_pb, string *bytes) {
  switch (value_pb.value_case()) {
    case QLValue::InternalType::kInt8Value: {
      YBPartition::AppendIntToKey<int8, uint8>(value_pb.int8_value(), bytes);
      break;
    }
    case QLValue::InternalType::kInt16Value: {
      YBPartition::AppendIntToKey<int16, uint16>(value_pb.int16_value(), bytes);
      break;
    }
    case QLValue::InternalType::kInt32Value: {
      YBPartition::AppendIntToKey<int32, uint32>(value_pb.int32_value(), bytes);
      break;
    }
    case QLValue::InternalType::kInt64Value: {
      YBPartition::AppendIntToKey<int64, uint64>(value_pb.int64_value(), bytes);
      break;
    }
    case QLValue::InternalType::kTimestampValue: {
      YBPartition::AppendIntToKey<int64, uint64>(value_pb.timestamp_value(), bytes);
      break;
    }
    case QLValue::InternalType::kStringValue: {
      const string& str = value_pb.string_value();
      YBPartition::AppendBytesToKey(str.c_str(), str.length(), bytes);
      break;
    }
    case QLValue::InternalType::kUuidValue: {
      const string& str = value_pb.uuid_value();
      YBPartition::AppendBytesToKey(str.c_str(), str.length(), bytes);
      break;
    }
    case QLValue::InternalType::kTimeuuidValue: {
      const string& str = value_pb.timeuuid_value();
      YBPartition::AppendBytesToKey(str.c_str(), str.length(), bytes);
      break;
    }
    case QLValue::InternalType::kInetaddressValue: {
      const string& str = value_pb.inetaddress_value();
      YBPartition::AppendBytesToKey(str.c_str(), str.length(), bytes);
      break;
    }
    case QLValue::InternalType::kDecimalValue: {
      const string& str = value_pb.decimal_value();
      YBPartition::AppendBytesToKey(str.c_str(), str.length(), bytes);
      break;
    }
    case QLValue::InternalType::kBinaryValue: {
      const string& str = value_pb.binary_value();
      YBPartition::AppendBytesToKey(str.c_str(), str.length(), bytes);
      break;
    }
    case QLValue::InternalType::kFloatValue: {
      YBPartition::AppendIntToKey<float, uint32>(util::CanonicalizeFloat(value_pb.float_value()),
                                                 bytes);
      break;
    }
    case QLValue::InternalType::kDoubleValue: {
      YBPartition::AppendIntToKey<double, uint64>(util::CanonicalizeDouble(value_pb.double_value()),
                                                  bytes);
      break;
    }
    case QLValue::InternalType::kFrozenValue: {
      for (const auto& elem_pb : value_pb.frozen_value().elems()) {
        RETURN_NOT_OK(AppendToKeyBytes(elem_pb, bytes));
      }
      break;
    }
    case QLValue::InternalType::kBoolValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kVarintValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kMapValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kSetValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::kListValue: FALLTHROUGH_INTENDED;
    case QLValue::InternalType::VALUE_NOT_SET:
      LOG(FATAL) << "Runtime error: This datatype("
                 << int(value_pb.value_case())
                 << ") is not supported in hash key";
  }

  return Status::OK();
}

void QLValue::Serialize(
    const std::shared_ptr<QLType>& ql_type, const QLClient& client, faststring* buffer) const {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  if (IsNull()) {
    CQLEncodeLength(-1, buffer);
    return;
  }

  switch (ql_type->main()) {
    case INT8:
      CQLEncodeNum(Store8, int8_value(), buffer);
      return;
    case INT16:
      CQLEncodeNum(NetworkByteOrder::Store16, int16_value(), buffer);
      return;
    case INT32:
      CQLEncodeNum(NetworkByteOrder::Store32, int32_value(), buffer);
      return;
    case INT64:
      CQLEncodeNum(NetworkByteOrder::Store64, int64_value(), buffer);
      return;
    case FLOAT:
      CQLEncodeFloat(NetworkByteOrder::Store32, float_value(), buffer);
      return;
    case DOUBLE:
      CQLEncodeFloat(NetworkByteOrder::Store64, double_value(), buffer);
      return;
    case DECIMAL: {
      auto decimal = util::DecimalFromComparable(decimal_value());
      bool is_out_of_range = false;
      CQLEncodeBytes(decimal.EncodeToSerializedBigDecimal(&is_out_of_range), buffer);
      if(is_out_of_range) {
        LOG(ERROR) << "Out of range: Unable to encode decimal " << decimal.ToString()
                   << " into a BigDecimal serialized representation";
      }
      return;
    }
    case STRING:
      CQLEncodeBytes(string_value(), buffer);
      return;
    case BOOL:
      CQLEncodeNum(Store8, static_cast<uint8>(bool_value() ? 1 : 0), buffer);
      return;
    case BINARY:
      CQLEncodeBytes(binary_value(), buffer);
      return;
    case TIMESTAMP: {
      int64_t val = DateTime::AdjustPrecision(timestamp_value().ToInt64(),
          DateTime::internal_precision,
          DateTime::CqlDateTimeInputFormat.input_precision());
      CQLEncodeNum(NetworkByteOrder::Store64, val, buffer);
      return;
    }
    case INET: {
      std::string bytes;
      CHECK_OK(inetaddress_value().ToBytes(&bytes));
      CQLEncodeBytes(bytes, buffer);
      return;
    }
    case UUID: {
      std::string bytes;
      CHECK_OK(uuid_value().ToBytes(&bytes));
      CQLEncodeBytes(bytes, buffer);
      return;
    }
    case TIMEUUID: {
      std::string bytes;
      Uuid uuid = timeuuid_value();
      CHECK_OK(uuid.IsTimeUuid());
      CHECK_OK(uuid.ToBytes(&bytes));
      CQLEncodeBytes(bytes, buffer);
      return;
    }
    case MAP: {
      QLMapValuePB map = map_value();
      DCHECK_EQ(map.keys_size(), map.values_size());
      int32_t start_pos = CQLStartCollection(buffer);
      int32_t length = static_cast<int32_t>(map.keys_size());
      CQLEncodeLength(length, buffer);
      const shared_ptr<QLType>& keys_type = ql_type->params()[0];
      const shared_ptr<QLType>& values_type = ql_type->params()[1];
      for (int i = 0; i < length; i++) {
        QLValueWithPB(map.keys(i)).Serialize(keys_type, client, buffer);
        QLValueWithPB(map.values(i)).Serialize(values_type, client, buffer);
      }
      CQLFinishCollection(start_pos, buffer);
      return;
    }
    case SET: {
      QLSeqValuePB set = set_value();
      int32_t start_pos = CQLStartCollection(buffer);
      int32_t length = static_cast<int32_t>(set.elems_size());
      CQLEncodeLength(length, buffer); // number of elements in collection
      const shared_ptr<QLType>& elems_type = ql_type->param_type(0);
      for (auto& elem : set.elems()) {
        QLValueWithPB(elem).Serialize(elems_type, client, buffer);
      }
      CQLFinishCollection(start_pos, buffer);
      return;
    }
    case LIST: {
      QLSeqValuePB list = list_value();
      int32_t start_pos = CQLStartCollection(buffer);
      int32_t length = static_cast<int32_t>(list.elems_size());
      CQLEncodeLength(length, buffer);
      const shared_ptr<QLType>& elems_type = ql_type->param_type(0);
      for (auto& elem : list.elems()) {
        QLValueWithPB(elem).Serialize(elems_type, client, buffer);
      }
      CQLFinishCollection(start_pos, buffer);
      return;
    }

    case USER_DEFINED_TYPE: {
      QLMapValuePB map = map_value();
      DCHECK_EQ(map.keys_size(), map.values_size());
      int32_t start_pos = CQLStartCollection(buffer);

      // For every field the UDT has, we try to find a corresponding map entry. If found we
      // serialize the value, else null. Map keys should always be in ascending order.
      int key_idx = 0;
      for (int i = 0; i < ql_type->udtype_field_names().size(); i++) {
        if (key_idx < map.keys_size() && map.keys(key_idx).int16_value() == i) {
          QLValueWithPB(map.values(key_idx)).Serialize(ql_type->param_type(i), client, buffer);
          key_idx++;
        } else { // entry not found -> writing null
          CQLEncodeLength(-1, buffer);
        }
      }

      CQLFinishCollection(start_pos, buffer);
      return;
    }
    case FROZEN: {
      QLSeqValuePB frozen = frozen_value();
      const auto& type = ql_type->param_type(0);
      switch (type->main()) {
        case MAP: {
          DCHECK_EQ(frozen.elems_size() % 2, 0);
          int32_t start_pos = CQLStartCollection(buffer);
          int32_t length = static_cast<int32_t>(frozen.elems_size() / 2);
          CQLEncodeLength(length, buffer);
          const shared_ptr<QLType> &keys_type = type->params()[0];
          const shared_ptr<QLType> &values_type = type->params()[1];
          for (int i = 0; i < length; i++) {
            QLValueWithPB(frozen.elems(2 * i)).Serialize(keys_type, client, buffer);
            QLValueWithPB(frozen.elems(2 * i + 1)).Serialize(values_type, client, buffer);
          }
          CQLFinishCollection(start_pos, buffer);
          return;
        }
        case SET: FALLTHROUGH_INTENDED;
        case LIST: {
          int32_t start_pos = CQLStartCollection(buffer);
          int32_t length = static_cast<int32_t>(frozen.elems_size());
          CQLEncodeLength(length, buffer); // number of elements in collection
          const shared_ptr<QLType> &elems_type = type->param_type(0);
          for (auto &elem : frozen.elems()) {
            QLValueWithPB(elem).Serialize(elems_type, client, buffer);
          }
          CQLFinishCollection(start_pos, buffer);
          return;
        }
        case USER_DEFINED_TYPE: {
          int32_t start_pos = CQLStartCollection(buffer);
          for (int i = 0; i < frozen.elems_size(); i++) {
            QLValueWithPB(frozen.elems(i)).Serialize(type->param_type(i), client, buffer);
          }
          CQLFinishCollection(start_pos, buffer);
          return;
        }

        default:
          break;
      }
      break;
    }

    QL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    QL_INVALID_TYPES_IN_SWITCH:
      break;
    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << ql_type->ToString();
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
    return STATUS(InvalidArgument, "YQL value too long");
  }

  switch (ql_type->main()) {
    case INT8:
      return CQLDeserializeNum(
          len, Load8, static_cast<void (QLValue::*)(int8_t)>(&QLValue::set_int8_value), data);
    case INT16:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load16,
          static_cast<void (QLValue::*)(int16_t)>(&QLValue::set_int16_value), data);
    case INT32:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load32,
          static_cast<void (QLValue::*)(int32_t)>(&QLValue::set_int32_value), data);
    case INT64:
      return CQLDeserializeNum(
          len, NetworkByteOrder::Load64,
          static_cast<void (QLValue::*)(int64_t)>(&QLValue::set_int64_value), data);
    case FLOAT:
      return CQLDeserializeFloat(
          len, NetworkByteOrder::Load32,
          static_cast<void (QLValue::*)(float)>(&QLValue::set_float_value), data);
    case DOUBLE:
      return CQLDeserializeFloat(
          len, NetworkByteOrder::Load64,
          static_cast<void (QLValue::*)(double)>(&QLValue::set_double_value), data);
    case DECIMAL: {
      string value;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &value));
      Decimal decimal;
      RETURN_NOT_OK(decimal.DecodeFromSerializedBigDecimal(value));
      set_decimal_value(decimal.EncodeToComparable());
      return Status::OK();
    }
    case STRING:
      return CQLDecodeBytes(len, data, mutable_string_value());
    case BOOL: {
      uint8_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, Load8, data, &value));
      set_bool_value(value != 0);
      return Status::OK();
    }
    case BINARY:
      return CQLDecodeBytes(len, data, mutable_binary_value());
    case TIMESTAMP: {
      int64_t value = 0;
      RETURN_NOT_OK(CQLDecodeNum(len, NetworkByteOrder::Load64, data, &value));
      value = DateTime::AdjustPrecision(value,
          DateTime::CqlDateTimeInputFormat.input_precision(), DateTime::internal_precision);
      set_timestamp_value(value);
      return Status::OK();
    }
    case INET: {
      string bytes;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &bytes));
      InetAddress addr;
      RETURN_NOT_OK(addr.FromBytes(bytes));
      set_inetaddress_value(addr);
      return Status::OK();
    }
    case UUID: {
      string bytes;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &bytes));
      Uuid uuid;
      RETURN_NOT_OK(uuid.FromBytes(bytes));
      set_uuid_value(uuid);
      return Status::OK();
    }
    case TIMEUUID: {
      string bytes;
      RETURN_NOT_OK(CQLDecodeBytes(len, data, &bytes));
      Uuid uuid;
      RETURN_NOT_OK(uuid.FromBytes(bytes));
      RETURN_NOT_OK(uuid.IsTimeUuid());
      set_timeuuid_value(uuid);
      return Status::OK();
    }
    case MAP: {
      const shared_ptr<QLType>& keys_type = ql_type->param_type(0);
      const shared_ptr<QLType>& values_type = ql_type->param_type(1);
      set_map_value();
      int32_t nr_elems = 0;
      RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
      for (int i = 0; i < nr_elems; i++) {
        QLValueWithPB key;
        RETURN_NOT_OK(key.Deserialize(keys_type, client, data));
        // TODO (mihnea) refactor QLValue to avoid copying here and below
        add_map_key()->CopyFrom(key.value());
        QLValueWithPB value;
        RETURN_NOT_OK(value.Deserialize(values_type, client, data));
        add_map_value()->CopyFrom(value.value());
      }
      return Status::OK();
    }
    case SET: {
      const shared_ptr<QLType>& elems_type = ql_type->param_type(0);
      set_set_value();
      int32_t nr_elems = 0;
      RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
      for (int i = 0; i < nr_elems; i++) {
        QLValueWithPB elem;
        RETURN_NOT_OK(elem.Deserialize(elems_type, client, data));
        add_set_elem()->CopyFrom(elem.value());
      }
      return Status::OK();
    }
    case LIST: {
      const shared_ptr<QLType>& elems_type = ql_type->param_type(0);
      set_list_value();
      int32_t nr_elems = 0;
      RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
      for (int i = 0; i < nr_elems; i++) {
        QLValueWithPB elem;
        RETURN_NOT_OK(elem.Deserialize(elems_type, client, data));
        add_list_elem()->CopyFrom(elem.value());
      }
      return Status::OK();
    }

    case USER_DEFINED_TYPE: {
      set_map_value();
      size_t fields_size = ql_type->udtype_field_names().size();
      for (size_t i = 0; i < fields_size; i++) {
        // TODO (mihnea) default to null if value missing (CQL behavior)
        QLValueWithPB value;
        RETURN_NOT_OK(value.Deserialize(ql_type->param_type(i), client, data));
        if (!QLValue::IsNull(value)) {
          add_map_key()->set_int16_value(i);
          add_map_value()->CopyFrom(value.value());
        }
      }
      return Status::OK();
    }

    case FROZEN: {
      set_frozen_value();
      const auto& type = ql_type->param_type(0);
      switch (type->main()) {
        case MAP: {
          const shared_ptr<QLType> &keys_type = type->param_type(0);
          const shared_ptr<QLType> &values_type = type->param_type(1);
          int32_t nr_elems = 0;
          RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
          for (int i = 0; i < nr_elems; i++) {
            QLValueWithPB key;
            RETURN_NOT_OK(key.Deserialize(keys_type, client, data));
            add_frozen_elem()->CopyFrom(key.value());
            QLValueWithPB value;
            RETURN_NOT_OK(value.Deserialize(values_type, client, data));
            add_frozen_elem()->CopyFrom(value.value());
          }
          return Status::OK();
        }
        case SET: {
          const shared_ptr<QLType> &elems_type = type->param_type(0);
          int32_t nr_elems = 0;
          RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
          for (int i = 0; i < nr_elems; i++) {
            QLValueWithPB elem;
            RETURN_NOT_OK(elem.Deserialize(elems_type, client, data));
            add_frozen_elem()->CopyFrom(elem.value());
          }
          return Status::OK();
        }
        case LIST: {
          const shared_ptr<QLType> &elems_type = type->param_type(0);
          int32_t nr_elems = 0;
          RETURN_NOT_OK(CQLDecodeNum(sizeof(nr_elems), NetworkByteOrder::Load32, data, &nr_elems));
          for (int i = 0; i < nr_elems; i++) {
            QLValueWithPB elem;
            RETURN_NOT_OK(elem.Deserialize(elems_type, client, data));
            add_frozen_elem()->CopyFrom(elem.value());
          }
          return Status::OK();
        }

        case USER_DEFINED_TYPE: {
          size_t fields_size = type->udtype_field_names().size();
          for (size_t i = 0; i < fields_size; i++) {
            // TODO (mihnea) default to null if value missing (CQL behavior)
            QLValueWithPB value;
            RETURN_NOT_OK(value.Deserialize(type->param_type(i), client, data));
            add_frozen_elem()->CopyFrom(value.value());
          }
          return Status::OK();
        }
        default:
          break;

      }
      break;
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

string QLValue::ToString() const {
  if (IsNull()) {
    return "null";
  }

  switch (type()) {
    case InternalType::kInt8Value: return "int8:" + to_string(int8_value());
    case InternalType::kInt16Value: return "int16:" + to_string(int16_value());
    case InternalType::kInt32Value: return "int32:" + to_string(int32_value());
    case InternalType::kInt64Value: return "int64:" + to_string(int64_value());
    case InternalType::kFloatValue: return "float:" + to_string(float_value());
    case InternalType::kDoubleValue: return "double:" + to_string(double_value());
    case InternalType::kDecimalValue:
      return "decimal: " + util::DecimalFromComparable(decimal_value()).ToString();
    case InternalType::kStringValue: return "string:" + FormatBytesAsStr(string_value());
    case InternalType::kTimestampValue: return "timestamp:" + timestamp_value().ToFormattedString();
    case InternalType::kInetaddressValue: return "inetaddress:" + inetaddress_value().ToString();
    case InternalType::kUuidValue: return "uuid:" + uuid_value().ToString();
    case InternalType::kTimeuuidValue: return "timeuuid:" + timeuuid_value().ToString();
    case InternalType::kBoolValue: return (bool_value() ? "bool:true" : "bool:false");
    case InternalType::kBinaryValue: return "binary:0x" + b2a_hex(binary_value());

    case InternalType::kMapValue: {
      std::stringstream ss;
      QLMapValuePB map = map_value();
      DCHECK_EQ(map.keys_size(), map.values_size());
      ss << "map:{";
      for (int i = 0; i < map.keys_size(); i++) {
        if (i > 0) {
          ss << ", ";
        }
        ss << QLValueWithPB(map.keys(i)).ToString() << " -> "
           << QLValueWithPB(map.values(i)).ToString();
      }
      ss << "}";
      return ss.str();
    }
    case InternalType::kSetValue: {
      std::stringstream ss;
      QLSeqValuePB set = set_value();
      ss << "set:{";
      for (int i = 0; i < set.elems_size(); i++) {
        if (i > 0) {
          ss << ", ";
        }
        ss << QLValueWithPB(set.elems(i)).ToString();
      }
      ss << "}";
      return ss.str();
    }
    case InternalType::kListValue: {
      std::stringstream ss;
      QLSeqValuePB list = list_value();
      ss << "list:[";
      for (int i = 0; i < list.elems_size(); i++) {
        if (i > 0) {
          ss << ", ";
        }
        ss << QLValueWithPB(list.elems(i)).ToString();
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
        ss << QLValueWithPB(frozen.elems(i)).ToString();
      }
      ss << ">";
      return ss.str();
    }

    case InternalType::kVarintValue:
      LOG(FATAL) << "Internal error: varint not implemented";
    case InternalType::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      return "null";
    // default: fall through
  }

  LOG(FATAL) << "Internal error: unknown or unsupported type " << type();
  return "unknown";
}

//------------------------- static functions for existing QLValuePB --------------------------
void QLValue::SetNull(QLValuePB* v) {
  switch(v->value_case()) {
    case QLValuePB::kInt8Value:   v->clear_int8_value(); return;
    case QLValuePB::kInt16Value:  v->clear_int16_value(); return;
    case QLValuePB::kInt32Value:  v->clear_int32_value(); return;
    case QLValuePB::kInt64Value:  v->clear_int64_value(); return;
    case QLValuePB::kFloatValue:  v->clear_float_value(); return;
    case QLValuePB::kDoubleValue: v->clear_double_value(); return;
    case QLValuePB::kDecimalValue: v->clear_decimal_value(); return;
    case QLValuePB::kStringValue: v->clear_string_value(); return;
    case QLValuePB::kBoolValue:   v->clear_bool_value(); return;
    case QLValuePB::kTimestampValue: v->clear_timestamp_value(); return;
    case QLValuePB::kBinaryValue: v->clear_binary_value(); return;
    case QLValuePB::kInetaddressValue: v->clear_inetaddress_value(); return;
    case QLValuePB::kUuidValue:   v->clear_uuid_value(); return;
    case QLValuePB::kTimeuuidValue:    v->clear_timeuuid_value(); return;
    case QLValuePB::kMapValue:    v->clear_map_value(); return;
    case QLValuePB::kSetValue:    v->clear_set_value(); return;
    case QLValuePB::kListValue:   v->clear_list_value(); return;
    case QLValuePB::kFrozenValue:   v->clear_frozen_value(); return;
    case QLValuePB::kVarintValue:
      LOG(FATAL) << "Internal error: varint not implemented";
    case QLValuePB::VALUE_NOT_SET: return;
  }
  LOG(FATAL) << "Internal error: unknown or unsupported type " << v->value_case();
}

int QLValue::CompareTo(const QLSeqValuePB& lhs, const QLSeqValuePB& rhs) {
  // Compare elements one by one.
  int result = 0;
  int min_size = std::min(lhs.elems_size(), rhs.elems_size());
  for (int i = 0; i < min_size; i++) {
    bool lhs_is_null = QLValue::IsNull(lhs.elems(i));
    bool rhs_is_null = QLValue::IsNull(rhs.elems(i));

    if (lhs_is_null && rhs_is_null) result = 0;
    else if (lhs_is_null) result = -1;
    else if (rhs_is_null) result = 1;
    else
      result = QLValue::CompareTo(lhs.elems(i), rhs.elems(i));

    if (result != 0) {
      return result;
    }
  }

  // If elements are equal, compare lengths.
  return GenericCompare(lhs.elems_size(), rhs.elems_size());
}

int QLValue::CompareTo(const QLValuePB& lhs, const QLValuePB& rhs) {
  CHECK(Comparable(lhs, rhs));
  CHECK(BothNotNull(lhs, rhs));
  switch (lhs.value_case()) {
    case QLValuePB::kInt8Value:   return GenericCompare(lhs.int8_value(), rhs.int8_value());
    case QLValuePB::kInt16Value:  return GenericCompare(lhs.int16_value(), rhs.int16_value());
    case QLValuePB::kInt32Value:  return GenericCompare(lhs.int32_value(), rhs.int32_value());
    case QLValuePB::kInt64Value:  return GenericCompare(lhs.int64_value(), rhs.int64_value());
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
    case QLValuePB::kStringValue: return lhs.string_value().compare(rhs.string_value());
    case QLValuePB::kBoolValue:
      LOG(FATAL) << "Internal error: bool type not comparable";
      return 0;
    case QLValuePB::kTimestampValue:
      return GenericCompare(lhs.timestamp_value(), rhs.timestamp_value());
    case QLValuePB::kBinaryValue: return lhs.binary_value().compare(rhs.binary_value());
    case QLValuePB::kInetaddressValue:
      return GenericCompare(lhs.inetaddress_value(), rhs.inetaddress_value());
    case QLValuePB::kUuidValue:
      return GenericCompare(lhs.uuid_value(), rhs.uuid_value());
    case QLValuePB::kTimeuuidValue:
      return GenericCompare(lhs.timeuuid_value(), rhs.timeuuid_value());
    case QLValuePB::kFrozenValue: {
      return QLValue::CompareTo(lhs.frozen_value(), rhs.frozen_value());
    }
    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      LOG(FATAL) << "Internal error: collection types are not comparable";
      return 0;
    case InternalType::kVarintValue:
      LOG(FATAL) << "Internal error: varint not implemented";
    case QLValuePB::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      break;

    // default: fall through
  }

  LOG(FATAL) << "Internal error: unknown or unsupported type " << lhs.value_case();
  return 0;
}

int QLValue::CompareTo(const QLValuePB& lhs, const QLValue& rhs) {
  CHECK(Comparable(lhs, rhs));
  CHECK(BothNotNull(lhs, rhs));
  switch (type(lhs)) {
    case QLValuePB::kInt8Value:   return GenericCompare(int8_value(lhs), rhs.int8_value());
    case QLValuePB::kInt16Value:  return GenericCompare(int16_value(lhs), rhs.int16_value());
    case QLValuePB::kInt32Value:  return GenericCompare(int32_value(lhs), rhs.int32_value());
    case QLValuePB::kInt64Value:  return GenericCompare(int64_value(lhs), rhs.int64_value());
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
    case QLValuePB::kDecimalValue: return decimal_value(lhs).compare(rhs.decimal_value());
    case QLValuePB::kStringValue: return string_value(lhs).compare(rhs.string_value());
    case QLValuePB::kBoolValue:
      LOG(FATAL) << "Internal error: bool type not comparable";
      return 0;
    case QLValuePB::kTimestampValue:
      return GenericCompare(timestamp_value(lhs), rhs.timestamp_value());
    case QLValuePB::kBinaryValue: return binary_value(lhs).compare(rhs.binary_value());
    case QLValuePB::kInetaddressValue:
      return GenericCompare(inetaddress_value(lhs), rhs.inetaddress_value());
    case QLValuePB::kUuidValue:
      return GenericCompare(uuid_value(lhs), rhs.uuid_value());
    case QLValuePB::kTimeuuidValue:
      return GenericCompare(timeuuid_value(lhs), rhs.timeuuid_value());
    case QLValuePB::kFrozenValue: {
      return QLValue::CompareTo(lhs.frozen_value(), rhs.frozen_value());
    }
    case QLValuePB::kMapValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kSetValue: FALLTHROUGH_INTENDED;
    case QLValuePB::kListValue:
      LOG(FATAL) << "Internal error: collection types are not comparable";
      return 0;
    case InternalType::kVarintValue:
      LOG(FATAL) << "Internal error: varint not implemented";
    case QLValuePB::VALUE_NOT_SET:
      LOG(FATAL) << "Internal error: value should not be null";
      break;

    // default: fall through
  }

  FATAL_INVALID_ENUM_VALUE(QLValuePB::ValueCase, type(lhs));
}

//----------------------------------- QLValuePB operators --------------------------------

#define QL_COMPARE(lhs, rhs, op)                                       \
  do { return QLValue::BothNotNull(lhs, rhs) && QLValue::CompareTo(lhs, rhs) op 0; } while (0)

bool operator <(const QLValuePB& lhs, const QLValuePB& rhs) { QL_COMPARE(lhs, rhs, <); }
bool operator >(const QLValuePB& lhs, const QLValuePB& rhs) { QL_COMPARE(lhs, rhs, >); }
bool operator <=(const QLValuePB& lhs, const QLValuePB& rhs) { QL_COMPARE(lhs, rhs, <=); }
bool operator >=(const QLValuePB& lhs, const QLValuePB& rhs) { QL_COMPARE(lhs, rhs, >=); }
bool operator ==(const QLValuePB& lhs, const QLValuePB& rhs) {
  // Equality holds for null values
  if (QLValue::IsNull(lhs) && QLValue::IsNull(rhs)) {
    return true;
  }
  QL_COMPARE(lhs, rhs, ==);
}
bool operator !=(const QLValuePB& lhs, const QLValuePB& rhs) { QL_COMPARE(lhs, rhs, !=); }

bool operator <(const QLValuePB& lhs, const QLValue& rhs) { QL_COMPARE(lhs, rhs, <); }
bool operator >(const QLValuePB& lhs, const QLValue& rhs) { QL_COMPARE(lhs, rhs, >); }
bool operator <=(const QLValuePB& lhs, const QLValue& rhs) { QL_COMPARE(lhs, rhs, <=); }
bool operator >=(const QLValuePB& lhs, const QLValue& rhs) { QL_COMPARE(lhs, rhs, >=); }
bool operator ==(const QLValuePB& lhs, const QLValue& rhs) {
  // Equality holds for null values
  if (QLValue::IsNull(lhs) && rhs.IsNull()) {
    return true;
  }
  QL_COMPARE(lhs, rhs, ==);
}
bool operator !=(const QLValuePB& lhs, const QLValue& rhs) { QL_COMPARE(lhs, rhs, !=); }

#undef QL_COMPARE

} // namespace yb
