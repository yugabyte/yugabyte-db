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

#pragma once

#include "yb/common/transaction.h"
#include "yb/common/value.pb.h"

#include "yb/client/client_fwd.h"

#include "yb/dockv/pg_row.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/util/physical_time.h"
#include "yb/util/uuid.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {
namespace util {

using dockv::PgTableRow;

// SET helpers
template <class T>
struct SetValueHelper;

template <>
struct SetValueHelper<std::string> {
  static Result<QLValuePB> Apply(const std::string& strval, const DataType data_type);
};

template <>
struct SetValueHelper<int32_t> {
  static Result<QLValuePB> Apply(int32_t intval, DataType data_type);
};

template <>
struct SetValueHelper<uint32_t> {
  static Result<QLValuePB> Apply(uint32_t intval, DataType data_type);
};

template <>
struct SetValueHelper<Uuid> {
  static Result<QLValuePB> Apply(const Uuid& uuid_val, const DataType data_type);
};

template <>
struct SetValueHelper<MicrosTime> {
  static Result<QLValuePB> Apply(const MicrosTime& time_val, const DataType data_type);
};

template <>
struct SetValueHelper<bool> {
  static Result<QLValuePB> Apply(const bool bool_val, const DataType data_type);
};

template <>
struct SetValueHelper<std::vector<std::string>> {
  static Result<QLValuePB> Apply(const std::vector<std::string>& str_vals, YBCPgOid oid);
};

template <>
struct SetValueHelper<google::protobuf::RepeatedPtrField<std::string>> {
  static Result<QLValuePB> Apply(
      const google::protobuf::RepeatedPtrField<std::string>& str_vals, YBCPgOid oid);
};

template <>
struct SetValueHelper<std::vector<TransactionId>> {
  static Result<QLValuePB> Apply(const std::vector<TransactionId>& transaction_vals, YBCPgOid oid);
};

template <class T>
Result<QLValuePB> SetValue(const T& t, DataType data_type) {
  typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type CleanedT;
  return SetValueHelper<CleanedT>::Apply(t, data_type);
}

template <class T>
Result<QLValuePB> SetArrayValue(const T& t, YBCPgOid oid, DataType data_type) {
  typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type CleanedT;
  if (data_type != DataType::BINARY) {
    return STATUS_FORMAT(InvalidArgument, "unexpected ARRAY datatype $0", data_type);
  }

  return SetValueHelper<CleanedT>::Apply(t, oid);
}

Result<std::tuple<ColumnId, YBCPgOid, DataType>> ColumnIndexAndType(
    const std::string& col_name, const Schema&);

// private helper to set QLValuePB to row after massaging
Status SetColumnValueFromQLValue(
    const ColumnId& column, const std::string& col_name, PgTableRow* row,
    Result<yb::QLValuePB>&& ql_value);

template <class T>
Status SetColumnValue(
    const std::string& col_name, const T& value, const Schema& schema, PgTableRow* row) {
  auto [column, _, datatype] = VERIFY_RESULT(ColumnIndexAndType(col_name, schema));

  return SetColumnValueFromQLValue(column, col_name, row, SetValue(value, datatype));
}

template <class T>
Status SetColumnArrayValue(
    const std::string& col_name, const T& value, const Schema& schema, PgTableRow* row) {
  auto [column, oid, datatype] = VERIFY_RESULT(ColumnIndexAndType(col_name, schema));

  return SetColumnValueFromQLValue(column, col_name, row, SetArrayValue(value, oid, datatype));
}

// GET helpers
template <class T>
struct GetValueHelper;

template <typename T>
using ValueAndIsNullPair = std::pair<T, bool>;

template <>
struct GetValueHelper<uint32_t> {
  static Result<ValueAndIsNullPair<uint32_t>> Retrieve(
      const QLValuePB& ql_val, const YBCPgTypeEntity* pg_type);
};

template <>
struct GetValueHelper<Uuid> {
  static Result<ValueAndIsNullPair<Uuid>> Retrieve(
      const QLValuePB& ql_val, const YBCPgTypeEntity* pg_type);
};

template <class T>
Result<ValueAndIsNullPair<T>> GetValue(const QLValuePB& ql_val, const YBCPgTypeEntity* pg_type) {
  typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type CleanedT;
  return GetValueHelper<CleanedT>::Retrieve(ql_val, pg_type);
}

}  // namespace util
}  // namespace pggate
}  // namespace yb
