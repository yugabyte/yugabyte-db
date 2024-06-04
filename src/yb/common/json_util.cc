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

#include "yb/common/json_util.h"

#include <rapidjson/prettywriter.h>

#include "yb/bfql/bfunc_convert.h"

#include "yb/common/jsonb.h"
#include "yb/common/ql_value.h"

#include "yb/util/status_format.h"
#include "yb/util/string_case.h"

using std::string;

namespace yb {
namespace common {

Status ConvertQLValuePBToRapidJson(const QLValuePB& ql_value_pb,
                                   rapidjson::Value* rapidjson_value,
                                   rapidjson::Document::AllocatorType* alloc) {
  auto is_nan_or_inf = [](double number) -> bool {
    const string str = SimpleDtoa(number);
    return str == "nan"|| str == "-nan"|| str == "inf" || str == "-inf";
  };

  auto seq_to_json = [](rapidjson::Value* value,
                        const QLSeqValuePB& seq_pb,
                        rapidjson::Document::AllocatorType* alloc) -> Status {
    value->SetArray();
    rapidjson::Value seq_value;
    for (int i = 0; i < seq_pb.elems_size(); ++i) {
      RETURN_NOT_OK(ConvertQLValuePBToRapidJson(seq_pb.elems(i), &seq_value, alloc));
      value->PushBack(seq_value, *alloc);
    }
    return Status::OK();
  };

  switch (ql_value_pb.value_case()) {
    case QLValuePB::ValueCase::VALUE_NOT_SET: // NULL -> null.
      rapidjson_value->SetNull();
      break;

    case QLValuePB::ValueCase::kJsonbValue: { // JSONB -> RapidJson.
        common::Jsonb jsonb(ql_value_pb.jsonb_value());
        rapidjson::Document doc;
        RETURN_NOT_OK(jsonb.ToRapidJson(&doc));
        rapidjson_value->CopyFrom(doc, *alloc);
      }
      break;

    case QLValuePB::ValueCase::kFloatValue:
      if (is_nan_or_inf(ql_value_pb.float_value())) {
        rapidjson_value->SetNull();
      } else {
        rapidjson_value->SetFloat(ql_value_pb.float_value());
      }
      break;
    case QLValuePB::ValueCase::kDoubleValue:
      if (is_nan_or_inf(ql_value_pb.double_value())) {
        rapidjson_value->SetNull();
      } else {
        rapidjson_value->SetDouble(ql_value_pb.double_value());
      }
      break;

    case QLValuePB::ValueCase::kInt8Value:
      rapidjson_value->SetInt(ql_value_pb.int8_value());
      break;
    case QLValuePB::ValueCase::kInt16Value:
      rapidjson_value->SetInt(ql_value_pb.int16_value());
      break;
    case QLValuePB::ValueCase::kInt32Value:
      rapidjson_value->SetInt(ql_value_pb.int32_value());
      break;
    case QLValuePB::ValueCase::kInt64Value:
      rapidjson_value->SetInt64(ql_value_pb.int64_value());
      break;
    case QLValuePB::ValueCase::kStringValue:
      rapidjson_value->SetString(ql_value_pb.string_value().c_str(), *alloc);
      break;
    case QLValuePB::ValueCase::kBoolValue:
      rapidjson_value->SetBool(ql_value_pb.bool_value());
      break;

    case QLValuePB::ValueCase::kVarintValue: { // VARINT -> INT64
        VarInt varint;
        size_t num_decoded_bytes = 0;
        RETURN_NOT_OK(varint.DecodeFromComparable(ql_value_pb.varint_value(), &num_decoded_bytes));
        rapidjson_value->SetInt64(VERIFY_RESULT(varint.ToInt64()));
      }
      break;

    case QLValuePB::ValueCase::kDecimalValue: { // DECIMAL -> INT64 or DOUBLE
        util::Decimal d;
        RETURN_NOT_OK(d.DecodeFromComparable(ql_value_pb.decimal_value()));
        if (d.is_integer()) {
          rapidjson_value->SetInt64(VERIFY_RESULT(VERIFY_RESULT(d.ToVarInt()).ToInt64()));
        } else {
          rapidjson_value->SetDouble(VERIFY_RESULT(d.ToDouble()));
        }
      }
      break;

    case QLValuePB::ValueCase::kTimestampValue: FALLTHROUGH_INTENDED;
    case QLValuePB::ValueCase::kDateValue: FALLTHROUGH_INTENDED;
    case QLValuePB::ValueCase::kTimeValue: FALLTHROUGH_INTENDED;
    case QLValuePB::ValueCase::kUuidValue: FALLTHROUGH_INTENDED;
    case QLValuePB::ValueCase::kTimeuuidValue: FALLTHROUGH_INTENDED;
    case QLValuePB::ValueCase::kBinaryValue: FALLTHROUGH_INTENDED;
    case QLValuePB::ValueCase::kInetaddressValue: {
        // Any simple type -> String.
        auto target = VERIFY_RESULT(bfql::ConvertToString(
            ql_value_pb, bfql::BFFactory()));
        rapidjson_value->SetString(target.string_value().c_str(), *alloc);
      }
      break;

    case QLValuePB::ValueCase::kSetValue: // SET -> RapidJson.
      RETURN_NOT_OK(seq_to_json(rapidjson_value, ql_value_pb.set_value(), alloc));
      break;
    case QLValuePB::ValueCase::kListValue: // LIST -> RapidJson.
      RETURN_NOT_OK(seq_to_json(rapidjson_value, ql_value_pb.list_value(), alloc));
      break;

    case QLValuePB::ValueCase::kFrozenValue: // FROZEN -> RapidJson.
      // Note: This implementation is correct for FROZEN<SET> & FROZEN<LIST> only.
      //       Implementation for other cases needs type info, the cases:
      //       FROZEN<MAP>, FROZEN<UDT>, FROZEN<SET<FROZEN<UDT>>>.
      RETURN_NOT_OK(seq_to_json(rapidjson_value, ql_value_pb.frozen_value(), alloc));
      break;

    case QLValuePB::ValueCase::kMapValue: { // MAP -> RapidJson.
      const QLMapValuePB& map_pb = ql_value_pb.map_value();

      if (map_pb.keys_size() != map_pb.values_size()) {
        return STATUS_SUBSTITUTE(QLError, "Invalid map: $0 keys and $1 values",
            map_pb.keys_size(), map_pb.values_size());
      }

      rapidjson_value->SetObject();
      rapidjson::Value map_key, map_value;
      for (int i = 0; i < map_pb.keys_size(); ++i) {
        RETURN_NOT_OK(ConvertQLValuePBToRapidJson(map_pb.keys(i), &map_key, alloc));
        // Quote the key if the key is not a string OR
        // if the key string contains characters in upper-case.
        if (!map_key.IsString() || ContainsUpperCase(map_key.GetString())) {
          string map_key_str = WriteRapidJsonToString(map_key);
          map_key.Swap(rapidjson::Value().SetString(map_key_str.c_str(), *alloc));
        }

        RETURN_NOT_OK(ConvertQLValuePBToRapidJson(map_pb.values(i), &map_value, alloc));
        rapidjson_value->AddMember(map_key, map_value, *alloc);
      }
    }
    break;
    case QLValuePB::ValueCase::kTupleValue:
      FALLTHROUGH_INTENDED;
    default:
        return STATUS_SUBSTITUTE(
            QLError, "Unexpected value type: $0", ql_value_pb.ShortDebugString());
  }

  return Status::OK();
}

std::string WriteRapidJsonToString(const rapidjson::Value& document) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  return std::string(buffer.GetString());
}

std::string PrettyWriteRapidJsonToString(const rapidjson::Value& document) {
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  return std::string(buffer.GetString());
}

} // namespace common
} // namespace yb
