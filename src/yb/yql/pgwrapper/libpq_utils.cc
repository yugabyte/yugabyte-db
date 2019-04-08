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

#include "yb/gutil/endian.h"

namespace yb {
namespace pgwrapper {

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
    return STATUS_FORMAT(NetworkError, "Execute '$0' failed: $1, message: $2",
                         command, status, PQresultErrorMessage(res.get()));
  }
  return Status::OK();
}

Result<PGResultPtr> Fetch(PGconn* conn, const std::string& command) {
  PGResultPtr res(PQexecParams(conn, command.c_str(), 0, nullptr, nullptr, nullptr, nullptr, 1));
  auto status = PQresultStatus(res.get());
  if (ExecStatusType::PGRES_TUPLES_OK != status) {
    return STATUS_FORMAT(NetworkError, "Fetch '$0' failed: $1, message: $2",
                         command, status, PQresultErrorMessage(res.get()));
  }
  return std::move(res);
}

Result<int32_t> GetInt32(PGresult* result, int row, int column) {
  int32_t res;
  auto len = PQgetlength(result, row, column);
  if (len != sizeof(res)) {
    return STATUS_FORMAT(Corruption, "Bad column length: $0, expected: $1, row: $2, column: $3",
                         len, sizeof(res), row, column);
  }
  return BigEndian::Load32(PQgetvalue(result, row, column));
}

Result<std::string> GetString(PGresult* result, int row, int column) {
  auto len = PQgetlength(result, row, column);
  auto value = PQgetvalue(result, row, column);
  return std::string(value, len);
}

} // namespace pgwrapper
} // namespace yb
