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

#include "yb/util/monotime.h"

using namespace std::literals;

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

struct PGConn::CopyData {
  static constexpr size_t kBufferSize = 2048;

  Status error;
  char * pos;
  char buffer[kBufferSize];

  void Start() {
    pos = buffer;
    error = Status::OK();
  }

  void WriteUInt16(uint16_t value) {
    BigEndian::Store16(pos, value);
    pos += 2;
  }

  void WriteUInt32(uint32_t value) {
    BigEndian::Store32(pos, value);
    pos += 4;
  }

  void WriteUInt64(uint64_t value) {
    BigEndian::Store64(pos, value);
    pos += 8;
  }

  void Write(const char* value, size_t len) {
    memcpy(pos, value, len);
    pos += len;
  }

  size_t left() const {
    return buffer + kBufferSize - pos;
  }
};

Result<PGConn> PGConn::Connect(const HostPort& host_port, const std::string& db_name) {
  auto start = CoarseMonoClock::now();
  auto deadline = start + 60s;
  for (;;) {
    auto conn_info = Format("host=$0 port=$1 user=postgres", host_port.host(), host_port.port());
    if (!db_name.empty()) {
      conn_info = Format("$0 dbname=$1", conn_info, db_name);
    }
    PGConnPtr result(PQconnectdb(conn_info.c_str()));
    auto status = PQstatus(result.get());
    if (status == ConnStatusType::CONNECTION_OK) {
      return PGConn(std::move(result));
    }
    auto now = CoarseMonoClock::now();
    if (now >= deadline) {
      return STATUS_FORMAT(NetworkError, "Connect failed: $0, passed: $1",
                           status, MonoDelta(now - start));
    }
  }
}

PGConn::PGConn(PGConnPtr ptr) : impl_(std::move(ptr)) {
}

PGConn::~PGConn() {
}

PGConn::PGConn(PGConn&& rhs) : impl_(std::move(rhs.impl_)) {
}

PGConn& PGConn::operator=(PGConn&& rhs) {
  impl_ = std::move(rhs.impl_);
  return *this;
}

void PGResultClear::operator()(PGresult* result) const {
  PQclear(result);
}

Status PGConn::Execute(const std::string& command) {
  PGResultPtr res(PQexec(impl_.get(), command.c_str()));
  auto status = PQresultStatus(res.get());
  if (ExecStatusType::PGRES_COMMAND_OK != status) {
    return STATUS(NetworkError,
                  Format("Execute '$0' failed: $1, message: $2",
                         command, status, PQresultErrorMessage(res.get())), Slice(),
                  PgsqlError(GetSqlState(res.get())));
  }
  return Status::OK();
}

Result<PGResultPtr> CheckResult(PGResultPtr result, const std::string& command) {
  auto status = PQresultStatus(result.get());
  if (ExecStatusType::PGRES_TUPLES_OK != status && ExecStatusType::PGRES_COPY_IN != status) {
    return STATUS(NetworkError,
                  Format("Fetch '$0' failed: $1, message: $2",
                         command, status, PQresultErrorMessage(result.get())), Slice(),
                  PgsqlError(GetSqlState(result.get())));
  }
  return std::move(result);
}

Result<PGResultPtr> PGConn::Fetch(const std::string& command) {
  return CheckResult(
      PGResultPtr(
          PQexecParams(impl_.get(), command.c_str(), 0, nullptr, nullptr, nullptr, nullptr, 1)),
      command);
}

Result<PGResultPtr> PGConn::FetchMatrix(const std::string& command, int rows, int columns) {
  auto res = VERIFY_RESULT(Fetch(command));

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

Status PGConn::CopyBegin(const std::string& command) {
  auto result = VERIFY_RESULT(CheckResult(
      PGResultPtr(
          PQexecParams(impl_.get(), command.c_str(), 0, nullptr, nullptr, nullptr, nullptr, 0)),
      command));

  if (!copy_data_) {
    copy_data_.reset(new CopyData);
  }
  copy_data_->Start();

  static const char prefix[] = "PGCOPY\n\xff\r\n\0\0\0\0\0\0\0\0\0";
  copy_data_->Write(prefix, sizeof(prefix) - 1);

  return Status::OK();
}

bool PGConn::CopyEnsureBuffer(size_t len) {
  if (!copy_data_->error.ok()) {
    return false;
  }
  if (copy_data_->left() < len) {
    return CopyFlushBuffer();
  }
  return true;
}

void PGConn::CopyStartRow(int16_t columns) {
  if (!CopyEnsureBuffer(2)) {
    return;
  }
  copy_data_->WriteUInt16(columns);
}

bool PGConn::CopyFlushBuffer() {
  if (!copy_data_->error.ok()) {
    return false;
  }
  ptrdiff_t len = copy_data_->pos - copy_data_->buffer;
  if (len) {
    int res = PQputCopyData(impl_.get(), copy_data_->buffer, len);
    if (res < 0) {
      copy_data_->error = STATUS_FORMAT(NetworkError, "Put copy data failed: $0", res);
      return false;
    }
  }
  copy_data_->Start();
  return true;
}

void PGConn::CopyPutInt16(int16_t value) {
  if (!CopyEnsureBuffer(6)) {
    return;
  }
  copy_data_->WriteUInt32(2);
  copy_data_->WriteUInt16(value);
}

void PGConn::CopyPutInt32(int32_t value) {
  if (!CopyEnsureBuffer(8)) {
    return;
  }
  copy_data_->WriteUInt32(4);
  copy_data_->WriteUInt32(value);
}

void PGConn::CopyPutInt64(int64_t value) {
  if (!CopyEnsureBuffer(12)) {
    return;
  }
  copy_data_->WriteUInt32(8);
  copy_data_->WriteUInt64(value);
}

void PGConn::CopyPut(const char* value, size_t len) {
  if (!CopyEnsureBuffer(4)) {
    return;
  }
  copy_data_->WriteUInt32(static_cast<uint32_t>(len));
  for (;;) {
    size_t left = copy_data_->left();
    if (copy_data_->left() < len) {
      copy_data_->Write(value, left);
      value += left;
      len -= left;
      if (!CopyFlushBuffer()) {
        return;
      }
    } else {
      copy_data_->Write(value, len);
      break;
    }
  }
}

Result<PGResultPtr> PGConn::CopyEnd() {
  if (CopyEnsureBuffer(2)) {
    copy_data_->WriteUInt16(static_cast<uint16_t>(-1));
  }
  if (!CopyFlushBuffer()) {
    return copy_data_->error;
  }
  int res = PQputCopyEnd(impl_.get(), 0);
  if (res <= 0) {
    return STATUS_FORMAT(NetworkError, "Put copy end failed: $0", res);
  }

  return PGResultPtr(PQgetResult(impl_.get()));
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
  constexpr Oid INT8OID = 20;
  constexpr Oid INT4OID = 23;
  constexpr Oid TEXTOID = 25;
  constexpr Oid BPCHAROID = 1042;
  constexpr Oid VARCHAROID = 1043;

  auto type = PQftype(result, column);
  switch (type) {
    case INT8OID:
      return std::to_string(VERIFY_RESULT(GetInt64(result, row, column)));
    case INT4OID:
      return std::to_string(VERIFY_RESULT(GetInt32(result, row, column)));
    case TEXTOID: FALLTHROUGH_INTENDED;
    case BPCHAROID: FALLTHROUGH_INTENDED;
    case VARCHAROID:
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
