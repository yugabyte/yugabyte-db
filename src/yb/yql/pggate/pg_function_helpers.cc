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

#include <boost/container/small_vector.hpp>

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/yql/pggate/pg_function_helpers.h"
#include "yb/yql/pggate/ybc_pggate.h"

#include "yb/util/net/net_util.h"
#include "yb/util/yb_partition.h"

namespace yb {
namespace pggate {
namespace util {

Result<QLValuePB> SetValueHelper<std::string>::Apply(
    const std::string& strval, const DataType data_type) {
  QLValuePB value_pb;
  switch (data_type) {
    case DataType::STRING:
      value_pb.set_string_value(strval);
      break;
    case DataType::BINARY:
      value_pb.set_binary_value(strval);
      break;
    default:
      return STATUS_FORMAT(InvalidArgument, "unexpected string type $0", data_type);
  }
  return value_pb;
}

Result<QLValuePB> SetValueHelper<int32_t>::Apply(const int32_t intval, const DataType data_type) {
  QLValuePB value_pb;
  switch (data_type) {
    case DataType::INT64:
      value_pb.set_int64_value(intval);
      break;
    case DataType::INT32:
      value_pb.set_int32_value(intval);
      break;
    case DataType::INT16:
      if (intval < std::numeric_limits<int16_t>::min() ||
          intval > std::numeric_limits<int16_t>::max()) {
        return STATUS_FORMAT(
            InvalidArgument, "overflow or underflow in conversion of value $0 to int16_t", intval);
      }
      value_pb.set_int16_value(intval);
      break;
    case DataType::INT8:
      if (intval < std::numeric_limits<int8_t>::min() ||
          intval > std::numeric_limits<int8_t>::max()) {
        return STATUS_FORMAT(
            InvalidArgument, "overflow or underflow in conversion of value $0 to int8_t", intval);
      }
      value_pb.set_int8_value(intval);
      break;
    case DataType::UINT64:
      if (intval < 0) {
        return STATUS_FORMAT(
            InvalidArgument, "underflow in conversion of value $0 to uint64_t", intval);
      }
      value_pb.set_uint64_value(static_cast<uint64_t>(intval));
      break;
    case DataType::UINT32:
      if (intval < 0) {
        return STATUS_FORMAT(
            InvalidArgument, "underflow in conversion of value $0 to uint32_t", intval);
      }
      value_pb.set_uint32_value(static_cast<uint32_t>(intval));
      break;
    default:
      return STATUS_FORMAT(InvalidArgument, "unexpected int type $0", data_type);
  }
  return value_pb;
}

Result<QLValuePB> SetValueHelper<uint32_t>::Apply(const uint32_t intval, const DataType data_type) {
  QLValuePB value_pb;
  switch (data_type) {
    case DataType::INT64:
      value_pb.set_int64_value(intval);
      break;
    case DataType::INT32:
      if (intval > std::numeric_limits<int32_t>::max()) {
        return STATUS_FORMAT(
            InvalidArgument, "overflow in conversion of value $0 to int32_t", intval);
      }
      value_pb.set_int32_value(intval);
      break;
    case DataType::INT16:
      if (intval > std::numeric_limits<int16_t>::max()) {
        return STATUS_FORMAT(
            InvalidArgument, "overflow in conversion of value $0 to int16_t", intval);
      }
      value_pb.set_int16_value(intval);
      break;
    case DataType::INT8:
      if (intval > std::numeric_limits<int8_t>::max()) {
        return STATUS_FORMAT(
            InvalidArgument, "overflow in conversion of value $0 to int8_t", intval);
      }
      value_pb.set_int8_value(intval);
      break;
    case DataType::UINT64:
      value_pb.set_uint64_value(intval);
      break;
    case DataType::UINT32:
      value_pb.set_uint32_value(intval);
      break;
    default:
      return STATUS_FORMAT(InvalidArgument, "unexpected int type $0", data_type);
  }
  return value_pb;
}

Result<QLValuePB> SetValueHelper<Uuid>::Apply(const Uuid& uuid_val, const DataType data_type) {
  std::string buffer;
  uuid_val.ToBytes(&buffer);

  QLValuePB result;
  result.set_binary_value(buffer);
  return result;
}

Result<QLValuePB> SetValueHelper<MicrosTime>::Apply(
    const MicrosTime& time_val, const DataType data_type) {
  QLValuePB value_pb;

  value_pb.set_int64_value(YBCGetPgCallbacks()->UnixEpochToPostgresEpoch(time_val));
  return value_pb;
}

Result<QLValuePB> SetValueHelper<bool>::Apply(const bool bool_val, const DataType data_type) {
  QLValuePB value_pb;
  value_pb.set_bool_value(bool_val);
  return value_pb;
}

template <typename Container>
Result<QLValuePB> ConvertStringArrayToQLValue(const Container& str_vals, const DataType data_type) {
  if (data_type != DataType::BINARY) {
    return STATUS_FORMAT(InvalidArgument, "unexpected string array type $0", data_type);
  }
  QLValuePB value_pb;
  size_t size;
  char* value;

  if (str_vals.size() > std::numeric_limits<int>::max()) {
    return STATUS(InvalidArgument, "overflow in conversion to int");
  }

  int count = static_cast<int>(str_vals.size());
  std::vector<const char*> strings;
  strings.reserve(count);

  for (const auto& str : str_vals) {
    strings.push_back(str.c_str());
  }

  // This makes a copy of the strings and returns a new palloc'd datum representing the TEXT array.
  YBCGetPgCallbacks()->ConstructTextArrayDatum(strings.data(), count, &value, &size);

  value_pb.set_binary_value(value, size);
  return value_pb;
}

Result<QLValuePB> SetValueHelper<std::vector<std::string>>::Apply(
    const std::vector<std::string>& str_vals, const DataType data_type) {
  return ConvertStringArrayToQLValue(str_vals, data_type);
}

Result<QLValuePB> SetValueHelper<google::protobuf::RepeatedPtrField<std::string>>::Apply(
    const google::protobuf::RepeatedPtrField<std::string>& str_vals, const DataType data_type) {
  return ConvertStringArrayToQLValue(str_vals, data_type);
}

Result<ValueAndIsNullPair<uint32_t>> GetValueHelper<uint32_t>::Retrieve(
    const QLValuePB& ql_val, const YBCPgTypeEntity* pg_type) {
  if (pg_type->yb_type != YB_YQL_DATA_TYPE_UINT32) {
    return STATUS_FORMAT(InvalidArgument, "unexpected data type $0", YB_YQL_DATA_TYPE_UINT32);
  }

  return ValueAndIsNullPair<uint32_t>(ql_val.uint32_value(), IsNull(ql_val));
}

Result<ValueAndIsNullPair<Uuid>> GetValueHelper<Uuid>::Retrieve(
    const QLValuePB& ql_val, const YBCPgTypeEntity* pg_type) {
  if (pg_type->yb_type != YB_YQL_DATA_TYPE_BINARY) {
    return STATUS_FORMAT(InvalidArgument, "unexpected data type $0", YB_YQL_DATA_TYPE_BINARY);
  }
  if (IsNull(ql_val)) {
    return ValueAndIsNullPair<Uuid>(Uuid::Nil(), true);
  }

  // Postgres stores UUIDs in host byte order, so this should be fine
  return ValueAndIsNullPair<Uuid>(
      VERIFY_RESULT(Uuid::FullyDecode(Slice(ql_val.binary_value()))), false);
}

Result<std::pair<ColumnId, DataType>> ColumnIndexAndType(
    const std::string& col_name, const Schema& schema) {
  const auto column_id = VERIFY_RESULT(schema.ColumnIdByName(col_name));
  const auto column = VERIFY_RESULT(schema.column_by_id(column_id));
  const DataType data_type = column.get().type_info()->type;
  return std::make_pair(column_id, data_type);
}

}  // namespace util
}  // namespace pggate
}  // namespace yb
