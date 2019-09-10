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

#include "yb/yql/pgwrapper/libpq_utils.h"

#include "yb/common/pgsql_error.h"

#include "yb/gutil/endian.h"

namespace yb {
namespace pgwrapper {

namespace {

YBPgErrorCode GetSqlState(PGresult* result) {
  auto status = PQresultStatus(result);
  if (status == ExecStatusType::PGRES_COMMAND_OK) {
    return YBPgErrorCode::YB_PG_SUCCESSFUL_COMPLETION;
  }

  const char* sqlstate_str = PQresultErrorField(result, PG_DIAG_SQLSTATE);
  CHECK_NOTNULL(sqlstate_str);
  CHECK_EQ(5, strlen(sqlstate_str));

  uint32_t sqlstate = 0;

  for (int i = 0; i < 5; ++i) {
    sqlstate |= (sqlstate_str[i] - '0') << (6 * i);
  }
  return static_cast<YBPgErrorCode>(sqlstate);
}

}  // anonymous namespace


void PGConnClose::operator()(PGconn* conn) const {
  PQfinish(conn);
}

void PGResultClear::operator()(PGresult* result) const {
  PQclear(result);
}

Status Execute(PGconn* conn, const std::string& command) {
  PGResultPtr res(PQexec(conn, command.c_str()));
  auto status = PQresultStatus(res.get());
  if (ExecStatusType::PGRES_COMMAND_OK != status) {
    return STATUS(NetworkError,
                  Format("Execute '$0' failed: $1, message: $2",
                         command, status, PQresultErrorMessage(res.get())), Slice(),
                  PgsqlError(GetSqlState(res.get())));
  }
  return Status::OK();
}

Result<PGResultPtr> Fetch(PGconn* conn, const std::string& command) {
  PGResultPtr res(PQexecParams(conn, command.c_str(), 0, nullptr, nullptr, nullptr, nullptr, 1));
  auto status = PQresultStatus(res.get());
  if (ExecStatusType::PGRES_TUPLES_OK != status) {
    return STATUS(NetworkError,
                  Format("Fetch '$0' failed: $1, message: $2",
                         command, status, PQresultErrorMessage(res.get())), Slice(),
                  PgsqlError(GetSqlState(res.get())));
  }
  return std::move(res);
}

Result<PGResultPtr> FetchMatrix(PGconn* conn, const std::string& command, int rows, int columns) {
  auto res = VERIFY_RESULT(Fetch(conn, command));

  auto fetched_columns = PQnfields(res.get());
  if (fetched_columns != columns) {
    return STATUS_FORMAT(
        RuntimeError, "Fetched $0 columns, while $1 expected", fetched_columns, columns);
  }

  auto fetched_rows = PQntuples(res.get());
  if (fetched_rows != rows) {
    return STATUS_FORMAT(
        RuntimeError, "Fetched $0 rows, while $1 expected", fetched_rows, rows);
  }

  return res;
}

Result<char*> GetValueWithLength(PGresult* result, int row, int column, size_t size) {
  auto len = PQgetlength(result, row, column);
  if (len != size) {
    return STATUS_FORMAT(Corruption, "Bad column length: $0, expected: $1, row: $2, column: $3",
                         len, size, row, column);
  }
  return PQgetvalue(result, row, column);
}

Result<int32_t> GetInt32(PGresult* result, int row, int column) {
  return BigEndian::Load32(VERIFY_RESULT(GetValueWithLength(result, row, column, sizeof(int32_t))));
}

Result<int64_t> GetInt64(PGresult* result, int row, int column) {
  return BigEndian::Load64(VERIFY_RESULT(GetValueWithLength(result, row, column, sizeof(int64_t))));
}

Result<std::string> GetString(PGresult* result, int row, int column) {
  auto len = PQgetlength(result, row, column);
  auto value = PQgetvalue(result, row, column);
  return std::string(value, len);
}

Result<std::string> AsString(PGresult* result, int row, int column) {
  constexpr Oid INT4OID = 23;
  constexpr Oid TEXTOID = 25;

  auto type = PQftype(result, column);
  switch (type) {
    case INT4OID:
      return std::to_string(VERIFY_RESULT(GetInt32(result, row, column)));
    case TEXTOID:
      return VERIFY_RESULT(GetString(result, row, column));
    default:
      return Format("Type not supported: $0", type);
  }
}

void LogResult(PGresult* result) {
  int cols = PQnfields(result);
  int rows = PQntuples(result);
  for (int row = 0; row != rows; ++row) {
    std::string line;
    for (int col = 0; col != cols; ++col) {
      if (col) {
        line += ", ";
      }
      line += CHECK_RESULT(AsString(result, row, col));
    }
    LOG(INFO) << line;
  }
}

} // namespace pgwrapper
} // namespace yb
