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

#include "yb/common/ql_serialization.h"

#include "yb/common/jsonb.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"

#include "yb/gutil/casts.h"

#include "yb/util/date_time.h"
#include "yb/util/decimal.h"

namespace yb {

void SerializeValue(
    const std::shared_ptr<QLType>& ql_type, const QLClient& client, const QLValuePB& pb,
    faststring* buffer) {
  CHECK_EQ(client, YQL_CLIENT_CQL);
  if (IsNull(pb)) {
    CQLEncodeLength(-1, buffer);
    return;
  }

  switch (ql_type->main()) {
    case INT8:
      CQLEncodeNum(Store8, static_cast<int8_t>(pb.int8_value()), buffer);
      return;
    case INT16:
      CQLEncodeNum(NetworkByteOrder::Store16, static_cast<int16_t>(pb.int16_value()), buffer);
      return;
    case INT32:
      CQLEncodeNum(NetworkByteOrder::Store32, pb.int32_value(), buffer);
      return;
    case INT64:
      CQLEncodeNum(NetworkByteOrder::Store64, pb.int64_value(), buffer);
      return;
    case FLOAT:
      CQLEncodeFloat(NetworkByteOrder::Store32, pb.float_value(), buffer);
      return;
    case DOUBLE:
      CQLEncodeFloat(NetworkByteOrder::Store64, pb.double_value(), buffer);
      return;
    case DECIMAL: {
      auto decimal = util::DecimalFromComparable(pb.decimal_value());
      bool is_out_of_range = false;
      CQLEncodeBytes(decimal.EncodeToSerializedBigDecimal(&is_out_of_range), buffer);
      if(is_out_of_range) {
        LOG(ERROR) << "Out of range: Unable to encode decimal " << decimal.ToString()
                   << " into a BigDecimal serialized representation";
      }
      return;
    }
    case VARINT: {
      CQLEncodeBytes(QLValue::varint_value(pb).EncodeToTwosComplement(), buffer);
      return;
    }
    case STRING:
      CQLEncodeBytes(pb.string_value(), buffer);
      return;
    case BOOL:
      CQLEncodeNum(Store8, static_cast<uint8>(pb.bool_value() ? 1 : 0), buffer);
      return;
    case BINARY:
      CQLEncodeBytes(pb.binary_value(), buffer);
      return;
    case TIMESTAMP: {
      int64_t val = DateTime::AdjustPrecision(QLValue::timestamp_value_pb(pb),
                                              DateTime::kInternalPrecision,
                                              DateTime::CqlInputFormat.input_precision);
      CQLEncodeNum(NetworkByteOrder::Store64, val, buffer);
      return;
    }
    case DATE: {
      CQLEncodeNum(NetworkByteOrder::Store32, pb.date_value(), buffer);
      return;
    }
    case TIME: {
      CQLEncodeNum(NetworkByteOrder::Store64, pb.time_value(), buffer);
      return;
    }
    case INET: {
      CQLEncodeBytes(QLValue::inetaddress_value(pb).ToBytes(), buffer);
      return;
    }
    case JSONB: {
      std::string json;
      common::Jsonb jsonb(pb.jsonb_value());
      CHECK_OK(jsonb.ToJsonString(&json));
      CQLEncodeBytes(json, buffer);
      return;
    }
    case UUID: {
      std::string bytes;
      QLValue::uuid_value(pb).ToBytes(&bytes);
      CQLEncodeBytes(bytes, buffer);
      return;
    }
    case TIMEUUID: {
      std::string bytes;
      Uuid uuid = QLValue::timeuuid_value(pb);
      CHECK_OK(uuid.IsTimeUuid());
      uuid.ToBytes(&bytes);
      CQLEncodeBytes(bytes, buffer);
      return;
    }
    case MAP: {
      const QLMapValuePB& map = pb.map_value();
      DCHECK_EQ(map.keys_size(), map.values_size());
      int32_t start_pos = CQLStartCollection(buffer);
      int32_t length = static_cast<int32_t>(map.keys_size());
      CQLEncodeLength(length, buffer);
      const auto& keys_type = ql_type->params()[0];
      const auto& values_type = ql_type->params()[1];
      for (int i = 0; i < length; i++) {
        SerializeValue(keys_type, client, map.keys(i), buffer);
        SerializeValue(values_type, client, map.values(i), buffer);
      }
      CQLFinishCollection(start_pos, buffer);
      return;
    }
    case SET: {
      const QLSeqValuePB& set = pb.set_value();
      int32_t start_pos = CQLStartCollection(buffer);
      int32_t length = static_cast<int32_t>(set.elems_size());
      CQLEncodeLength(length, buffer); // number of elements in collection
      const auto& elems_type = ql_type->param_type(0);
      for (auto& elem : set.elems()) {
        SerializeValue(elems_type, client, elem, buffer);
      }
      CQLFinishCollection(start_pos, buffer);
      return;
    }
    case LIST: {
      const QLSeqValuePB& list = pb.list_value();
      int32_t start_pos = CQLStartCollection(buffer);
      int32_t length = static_cast<int32_t>(list.elems_size());
      CQLEncodeLength(length, buffer);
      const auto& elems_type = ql_type->param_type(0);
      for (auto& elem : list.elems()) {
        SerializeValue(elems_type, client, elem, buffer);
      }
      CQLFinishCollection(start_pos, buffer);
      return;
    }

    case USER_DEFINED_TYPE: {
      const QLMapValuePB& map = pb.map_value();
      DCHECK_EQ(map.keys_size(), map.values_size());
      int32_t start_pos = CQLStartCollection(buffer);

      // For every field the UDT has, we try to find a corresponding map entry. If found we
      // serialize the value, else null. Map keys should always be in ascending order.
      int key_idx = 0;
      for (size_t i = 0; i < ql_type->udtype_field_names().size(); i++) {
        if (key_idx < map.keys_size() &&
            implicit_cast<size_t>(map.keys(key_idx).int16_value()) == i) {
          SerializeValue(ql_type->param_type(i), client, map.values(key_idx), buffer);
          key_idx++;
        } else { // entry not found -> writing null
          CQLEncodeLength(-1, buffer);
        }
      }

      CQLFinishCollection(start_pos, buffer);
      return;
    }
    case FROZEN: {
      const QLSeqValuePB& frozen = pb.frozen_value();
      const auto& type = ql_type->param_type(0);
      switch (type->main()) {
        case MAP: {
          DCHECK_EQ(frozen.elems_size() % 2, 0);
          int32_t start_pos = CQLStartCollection(buffer);
          int32_t length = static_cast<int32_t>(frozen.elems_size() / 2);
          CQLEncodeLength(length, buffer);
          const auto& keys_type = type->params()[0];
          const auto& values_type = type->params()[1];
          for (int i = 0; i < length; i++) {
            SerializeValue(keys_type, client, frozen.elems(2 * i), buffer);
            SerializeValue(values_type, client, frozen.elems(2 * i + 1), buffer);
          }
          CQLFinishCollection(start_pos, buffer);
          return;
        }
        case SET: FALLTHROUGH_INTENDED;
        case LIST: {
          int32_t start_pos = CQLStartCollection(buffer);
          int32_t length = static_cast<int32_t>(frozen.elems_size());
          CQLEncodeLength(length, buffer); // number of elements in collection
          const auto& elems_type = type->param_type(0);
          for (auto &elem : frozen.elems()) {
            SerializeValue(elems_type, client, elem, buffer);
          }
          CQLFinishCollection(start_pos, buffer);
          return;
        }
        case USER_DEFINED_TYPE: {
          int32_t start_pos = CQLStartCollection(buffer);
          for (int i = 0; i < frozen.elems_size(); i++) {
            SerializeValue(type->param_type(i), client, frozen.elems(i), buffer);
          }
          CQLFinishCollection(start_pos, buffer);
          return;
        }

        default:
          break;
      }
      break;
    }
    case TUPLE: {
      const QLSeqValuePB& tuple = pb.tuple_value();
      size_t num_elems = tuple.elems_size();
      DCHECK_EQ(num_elems, ql_type->params().size());
      int32_t start_pos = CQLStartCollection(buffer);
      for (size_t i = 0; i < num_elems; i++) {
        SerializeValue(ql_type->param_type(i), client, tuple.elems(static_cast<int>(i)), buffer);
      }
      CQLFinishCollection(start_pos, buffer);
      return;
    }

    QL_UNSUPPORTED_TYPES_IN_SWITCH:
      break;

    QL_INVALID_TYPES_IN_SWITCH:
      break;
    // default: fall through
  }

  LOG(FATAL) << "Internal error: unsupported type " << ql_type->ToString();
}

}  // namespace yb
