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

#include <string>
#include <utility>

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/common/pgsql_error.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"

using namespace std::literals;

namespace yb::pgwrapper {

const std::string& DefaultColumnSeparator() {
  static const std::string result = ", ";
  return result;
}

const std::string& DefaultRowSeparator() {
  static const std::string result = "; ";
  return result;
}

namespace {

// Converts the given element of the ExecStatusType enum to a string.
std::string ExecStatusTypeToStr(ExecStatusType exec_status_type) {
#define EXEC_STATUS_SWITCH_CASE(r, data, item) case item: return BOOST_PP_STRINGIZE(item);
#define EXEC_STATUS_TYPE_ENUM_ELEMENTS \
    (PGRES_EMPTY_QUERY) \
    (PGRES_COMMAND_OK) \
    (PGRES_TUPLES_OK) \
    (PGRES_COPY_OUT) \
    (PGRES_COPY_IN) \
    (PGRES_BAD_RESPONSE) \
    (PGRES_NONFATAL_ERROR) \
    (PGRES_FATAL_ERROR) \
    (PGRES_COPY_BOTH) \
    (PGRES_SINGLE_TUPLE)
  switch (exec_status_type) {
    BOOST_PP_SEQ_FOR_EACH(EXEC_STATUS_SWITCH_CASE, ~, EXEC_STATUS_TYPE_ENUM_ELEMENTS)
  }
#undef EXEC_STATUS_SWITCH_CASE
#undef EXEC_STATUS_TYPE_ENUM_ELEMENTS
  return Format("Unknown ExecStatusType ($0)", exec_status_type);
}

YBPgErrorCode GetSqlState(PGresult* result) {
  auto exec_status_type = PQresultStatus(result);
  if (exec_status_type == ExecStatusType::PGRES_COMMAND_OK ||
      exec_status_type == ExecStatusType::PGRES_TUPLES_OK) {
    return YBPgErrorCode::YB_PG_SUCCESSFUL_COMPLETION;
  }

  const char* sqlstate_str = PQresultErrorField(result, PG_DIAG_SQLSTATE);
  if (sqlstate_str == nullptr) {
    auto err_msg = PQresultErrorMessage(result);
    YB_LOG_EVERY_N_SECS(WARNING, 5)
        << "SQLSTATE is not defined for result with "
        << "error message: " << (err_msg ? err_msg : "N/A") << ", "
        << "PQresultStatus: " << ExecStatusTypeToStr(exec_status_type);
    return YBPgErrorCode::YB_PG_INTERNAL_ERROR;
  }

  CHECK_EQ(5, strlen(sqlstate_str))
      << "sqlstate_str: " << sqlstate_str
      << ", PQresultStatus: " << ExecStatusTypeToStr(exec_status_type);

  uint32_t sqlstate = 0;

  for (int i = 0; i < 5; ++i) {
    sqlstate |= (sqlstate_str[i] - '0') << (6 * i);
  }
  return static_cast<YBPgErrorCode>(sqlstate);
}

// Taken from <https://stackoverflow.com/a/24315631> by Gauthier Boaglio.
inline void ReplaceAll(std::string* str, const std::string& from, const std::string& to) {
  CHECK(str);
  size_t start_pos = 0;
  while ((start_pos = str->find(from, start_pos)) != std::string::npos) {
    str->replace(start_pos, from.length(), to);
    start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
  }
}

std::string BuildConnectionString(const PGConnSettings& settings, bool mask_password = false) {
  std::string result;
  result.reserve(512);
  result += Format("host=$0 port=$1", settings.host, settings.port);
  if (!settings.dbname.empty()) {
    result += Format(" dbname=$0", PqEscapeLiteral(settings.dbname));
  }
  if (settings.connect_timeout) {
    result += Format(" connect_timeout=$0", settings.connect_timeout);
  }
  if (!settings.user.empty()) {
    result += Format(" user=$0", PqEscapeLiteral(settings.user));
  }
  if (!settings.password.empty()) {
    result += Format(" password=$0", mask_password ? "<REDACTED>" : settings.password);
  }
  if (!settings.replication.empty()) {
    result += Format(" replication=$0", PqEscapeLiteral(settings.replication));
  }
  return result;
}

std::string FormPQErrorMessage(const char* msg) {
  std::string result(msg);
  // Avoid double newline (postgres adds a newline after the error message).
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }
  return result;
}

std::string GetPQErrorMessage(const PGresult* res) {
  return FormPQErrorMessage(PQresultErrorMessage(res));
}

std::string GetPQErrorMessage(const PGconn* conn) {
  return FormPQErrorMessage(PQerrorMessage(conn));
}

Result<char*> GetValueWithLength(const PGresult* result, int row, int column, size_t size) {
  size_t len = PQgetlength(result, row, column);
  if (len != size) {
    return STATUS_FORMAT(Corruption, "Bad column length: $0, expected: $1, row: $2, column: $3",
                         len, size, row, column);
  }
  return PQgetvalue(result, row, column);
}

template<class T>
Result<T> GetValueImpl(const PGresult* result, int row, int column) {
  if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, char>) {
    static_assert(sizeof(bool) == sizeof(char));
    return *VERIFY_RESULT(GetValueWithLength(result, row, column, 1));
  } else if constexpr (std::is_same_v<T, std::uint16_t>) {
    return BigEndian::Load16(
        VERIFY_RESULT(GetValueWithLength(result, row, column, sizeof(uint16_t))));
  } else if constexpr (std::is_same_v<T, std::uint32_t>) {
    return BigEndian::Load32(
        VERIFY_RESULT(GetValueWithLength(result, row, column, sizeof(uint32_t))));
  } else if constexpr (std::is_same_v<T, std::uint64_t>) {
    return BigEndian::Load64(
        VERIFY_RESULT(GetValueWithLength(result, row, column, sizeof(uint64_t))));
  } else if constexpr (std::is_same_v<T, std::string>) {
    return std::string(PQgetvalue(result, row, column), PQgetlength(result, row, column));
  } else if constexpr (std::is_same_v<T, Uuid>) {
    return Uuid::FromSlice(
        Slice(PQgetvalue(result, row, column), PQgetlength(result, row, column)));
  }
}

template<class F>
struct FloatTraits {
  using FloatType = F;
  using IntType = uint32_t;
};

template<class F>
requires(std::is_same_v<F, double>)
struct FloatTraits<F> {
  using FloatType = F;
  using IntType = uint64_t;
};

std::vector<std::string> PerfArguments(int pid) {
  return {"perf", "record", "-g", Format("-p$0", pid), Format("-o/tmp/perf.$0.data", pid)};
}

}  // namespace

template<BasePGType T>
GetValueResult<T> GetValue(const PGresult* result, int row, int column) {
  if constexpr (IsPGNonNeg<T>) {
    const auto value = VERIFY_RESULT(GetValue<typename T::Type>(result, row, column));
    SCHECK_GE(value, 0, Corruption, "Bad narrow cast");
    return value;
  } if constexpr (IsPGFloatType<T>) {
    using FloatType = typename FloatTraits<T>::FloatType;
    using IntType = typename FloatTraits<T>::IntType;
    static_assert(sizeof(FloatType) == sizeof(IntType), "Wrong sizes");
    const auto value = VERIFY_RESULT(GetValueImpl<IntType>(result, row, column));
    return *pointer_cast<const FloatType*>(&value);
  } else if constexpr (IsPGIntType<T>) {
    return GetValueImpl<std::make_unsigned_t<T>>(result, row, column);
  } else {
    return GetValueImpl<typename PGTypeTraits<T>::ReturnType>(result, row, column);
  }
}

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

Result<PGConn> PGConn::Connect(const std::string& conn_str,
                               CoarseTimePoint deadline,
                               bool simple_query_protocol,
                               const std::string& explicit_conn_str_for_log) {
  PGConnPtr result;
  ConnStatusType status;
  const auto& conn_str_for_log = explicit_conn_str_for_log.empty()
      ? conn_str
      : explicit_conn_str_for_log;
  CoarseBackoffWaiter waiter(deadline, std::chrono::milliseconds(kDefaultMaxWaitDelayMs));
  auto start = CoarseMonoClock::now();
  if (waiter.ExpiredNow()) {
    return STATUS_FORMAT(
        TimedOut, "Reached deadline before attempting connection: $0", conn_str_for_log);
  }
  do {
    result = PGConnPtr(PQconnectdb(conn_str.c_str()));
    if (!result) {
      return STATUS(NetworkError, "Failed to connect to DB");
    }
    status = PQstatus(result.get());
    if (status == CONNECTION_OK) {
      LOG(INFO) << "Connected to PG ("
                << conn_str_for_log
                << "), time taken: "
                << MonoDelta(CoarseMonoClock::Now() - start);
      return PGConn(std::move(result), simple_query_protocol);
    }
  } while (waiter.Wait());
  const MonoDelta duration(CoarseMonoClock::now() - start);
  const auto msg = status == CONNECTION_BAD
      ? GetPQErrorMessage(result.get())
      : std::string();
  LOG(INFO) << "Connect with \"" << conn_str_for_log << "\" failed: "
            << (msg.empty() ? AsString(status) : msg) << ", time taken: " << duration;
  return STATUS(NetworkError,
                Format("Connect failed: $0, passed: $1", msg, duration),
                AuxilaryMessage(msg));
}

PGConn::PGConn(PGConnPtr ptr, bool simple_query_protocol)
    : impl_(std::move(ptr)), simple_query_protocol_(simple_query_protocol) {
}

PGConn::~PGConn() {
}

PGConn::PGConn(PGConn&& rhs)
    : impl_(std::move(rhs.impl_)), simple_query_protocol_(rhs.simple_query_protocol_) {
}

PGConn& PGConn::operator=(PGConn&& rhs) {
  impl_ = std::move(rhs.impl_);
  simple_query_protocol_ = rhs.simple_query_protocol_;
  return *this;
}

void PGResultClear::operator()(PGresult* result) const {
  PQclear(result);
}

void PGConn::Reset() {
  PQreset(impl_.get());
}

Status PGConn::Execute(const std::string& command, bool show_query_in_error) {
  VLOG(1) << __func__ << " " << command;
  PGResultPtr res(PQexec(impl_.get(), command.c_str()));
  auto status = PQresultStatus(res.get());
  if (ExecStatusType::PGRES_COMMAND_OK != status) {
    if (status == ExecStatusType::PGRES_TUPLES_OK) {
      return STATUS_FORMAT(IllegalState,
                           "Tuples received in Execute$0",
                           show_query_in_error ? Format(" of '$0'", command) : "");
    }
    auto msg = GetPQErrorMessage(res.get());
    return STATUS(NetworkError,
                  Format("Execute$0 failed: $1, message: $2",
                         show_query_in_error ? Format(" of '$0'", command) : "",
                         status,
                         msg),
                  Slice() /* msg2 */,
                  PgsqlError(GetSqlState(res.get()))).CloneAndAddErrorCode(AuxilaryMessage(msg));
  }
  return Status::OK();
}

bool PGConn::IsBusy() {
  static constexpr int kIsBusy = 1;
  return PQisBusy(impl_.get()) == kIsBusy;
}

Result<PGResultPtr> CheckResult(PGResultPtr result, const std::string& command) {
  auto status = PQresultStatus(result.get());
  if (ExecStatusType::PGRES_TUPLES_OK != status && ExecStatusType::PGRES_COPY_IN != status) {
    auto msg = GetPQErrorMessage(result.get());
    return STATUS(NetworkError,
                  Format("Fetch '$0' failed: $1, message: $2",
                         command, status, msg),
                  Slice() /* msg2 */,
                  PgsqlError(GetSqlState(result.get()))).CloneAndAddErrorCode(AuxilaryMessage(msg));
  }
  return result;
}

Result<PGResultPtr> PGConn::Fetch(const std::string& command) {
  VLOG(1) << __func__ << " " << command;
  return CheckResult(
      PGResultPtr(simple_query_protocol_
          ? PQexec(impl_.get(), command.c_str())
          : PQexecParams(impl_.get(), command.c_str(), 0, nullptr, nullptr, nullptr, nullptr, 1)),
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

Status PGConn::StartTransaction(IsolationLevel isolation_level) {
  switch (isolation_level) {
    case IsolationLevel::NON_TRANSACTIONAL:
      return Status::OK();
    case IsolationLevel::READ_COMMITTED:
      return Execute("START TRANSACTION ISOLATION LEVEL READ COMMITTED");
    case IsolationLevel::SNAPSHOT_ISOLATION:
      return Execute("START TRANSACTION ISOLATION LEVEL REPEATABLE READ");
    case IsolationLevel::SERIALIZABLE_ISOLATION:
      return Execute("START TRANSACTION ISOLATION LEVEL SERIALIZABLE");
  }

  FATAL_INVALID_ENUM_VALUE(IsolationLevel, isolation_level);
}

Status PGConn::CommitTransaction() {
  return Execute("COMMIT");
}

Status PGConn::RollbackTransaction() {
  return Execute("ROLLBACK");
}

Status PGConn::TestFailDdl(const std::string& ddl_to_fail) {
  RETURN_NOT_OK(Execute("SET yb_test_fail_next_ddl=true"));
  Status s = Execute(ddl_to_fail);
  if (s.ok()) {
    return STATUS_FORMAT(InternalError,
                         "DDL '$0' should have failed, we explicitly instructed it to!",
                         ddl_to_fail);
  }
  std::string msg = reinterpret_cast<const char*>(s.message().data());
  if (msg.find("Failed DDL operation as requested") != std::string::npos) {
    return Status::OK();
  }
  // Unexpected error.
  return s;
}

Result<bool> PGConn::HasIndexScan(const std::string& query) {
  return VERIFY_RESULT(HasScanType(query, "Index")) ||
         VERIFY_RESULT(HasScanType(query, "Index Only"));
}

Result<bool> PGConn::HasScanType(const std::string& query, const std::string expected_scan_type) {
  const auto rows = VERIFY_RESULT(FetchRows<std::string>(Format("EXPLAIN $0", query)));
  const auto search = Format("$0 Scan", expected_scan_type);
  for (const auto& row : rows) {
    if (row.find(search) != std::string::npos) {
      return true;
    }
  }
  return false;
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
    int res = PQputCopyData(impl_.get(), copy_data_->buffer, narrow_cast<int>(len));
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

Result<std::string> ToString(PGresult* result, int row, int column) {
  constexpr Oid BOOLOID = 16;
  constexpr Oid NAMEOID = 19;
  constexpr Oid INT8OID = 20;
  constexpr Oid INT2OID = 21;
  constexpr Oid INT4OID = 23;
  constexpr Oid TEXTOID = 25;
  constexpr Oid OIDOID = 26;
  constexpr Oid FLOAT4OID = 700;
  constexpr Oid FLOAT8OID = 701;
  constexpr Oid BPCHAROID = 1042;
  constexpr Oid VARCHAROID = 1043;
  constexpr Oid CSTRINGOID = 2275;
  constexpr Oid UUIDOID = 2950;

  if (PQgetisnull(result, row, column)) {
    return "NULL";
  }

  auto type = PQftype(result, column);
  switch (type) {
    case BOOLOID:
      return yb::ToString(VERIFY_RESULT(GetValue<bool>(result, row, column)));
    case INT8OID:
      return yb::ToString(VERIFY_RESULT(GetValue<int64_t>(result, row, column)));
    case INT2OID:
      return yb::ToString(VERIFY_RESULT(GetValue<int16_t>(result, row, column)));
    case INT4OID:
      return yb::ToString(VERIFY_RESULT(GetValue<int32_t>(result, row, column)));
    case FLOAT4OID:
      return yb::ToString(VERIFY_RESULT(GetValue<float>(result, row, column)));
    case FLOAT8OID:
      return yb::ToString(VERIFY_RESULT(GetValue<double>(result, row, column)));
    case NAMEOID: FALLTHROUGH_INTENDED;
    case TEXTOID: FALLTHROUGH_INTENDED;
    case BPCHAROID: FALLTHROUGH_INTENDED;
    case VARCHAROID: FALLTHROUGH_INTENDED;
    case CSTRINGOID:
      return VERIFY_RESULT(GetValue<std::string>(result, row, column));
    case OIDOID:
      return yb::ToString(VERIFY_RESULT(GetValue<PGOid>(result, row, column)));
    case UUIDOID:
      return yb::ToString(VERIFY_RESULT(GetValue<Uuid>(result, row, column)));
    default:
      return Format("Type not supported: $0", type);
  }
}

// Escape literals in postgres (e.g. to make a libpq connection to a database named
// `this->'\<-this`, use `dbname='this->\'\\<-this'`).
//
// This should behave like `PQescapeLiteral` except that it doesn't need an existing connection
// passed in.
std::string PqEscapeLiteral(const std::string& input) {
  std::string output = input;
  // Escape certain characters.
  ReplaceAll(&output, "\\", "\\\\");
  ReplaceAll(&output, "'", "\\'");
  // Quote.
  output.insert(0, 1, '\'');
  output.push_back('\'');
  return output;
}

// Escape identifiers in postgres (e.g. to create a database named `this->"\<-this`, use `CREATE
// DATABASE "this->""\<-this"`).
//
// This should behave like `PQescapeIdentifier` except that it doesn't need an existing connection
// passed in.
std::string PqEscapeIdentifier(const std::string& input) {
  std::string output = input;
  // Escape certain characters.
  ReplaceAll(&output, "\"", "\"\"");
  // Quote.
  output.insert(0, 1, '"');
  output.push_back('"');
  return output;
}

PGConnBuilder::PGConnBuilder(const PGConnSettings& settings)
    : conn_str_(BuildConnectionString(settings)),
      conn_str_for_log_(BuildConnectionString(settings, true /* mask_password */)),
      connect_timeout_(settings.connect_timeout) {
}

Result<PGConn> PGConnBuilder::Connect(bool simple_query_protocol) const {
  // If connect_timeout is specified, also set it as the total deadline among connection attempts
  // because that is likely what the caller intended.  There is logic in connectDBComplete to make
  // connect_timeout of 1 effectively mean 2, but don't bother with that conversion for this
  // deadline since the caller likely intended a deadline of 1.
  if (connect_timeout_) {
    const auto deadline = CoarseMonoClock::Now() + MonoDelta::FromSeconds(connect_timeout_);
    return PGConn::Connect(conn_str_, deadline, simple_query_protocol, conn_str_for_log_);
  }
  return PGConn::Connect(conn_str_, simple_query_protocol, conn_str_for_log_);
}

Result<PGConn> Execute(Result<PGConn> connection, const std::string& query) {
  if (connection.ok()) {
    RETURN_NOT_OK((*connection).Execute(query));
  }
  return connection;
}

Result<PGConn> SetHighPriTxn(Result<PGConn> connection) {
  return Execute(std::move(connection), "SET yb_transaction_priority_lower_bound=0.5");
}
Result<PGConn> SetLowPriTxn(Result<PGConn> connection) {
  return Execute(std::move(connection), "SET yb_transaction_priority_upper_bound=0.4");
}

Status SetMaxBatchSize(PGConn* conn, size_t max_batch_size) {
  return conn->ExecuteFormat("SET ysql_session_max_batch_size = $0", max_batch_size);
}

PGConnPerf::PGConnPerf(yb::pgwrapper::PGConn* conn)
    : process_("perf",
               PerfArguments(CHECK_RESULT(conn->FetchRow<PGUint32>("SELECT pg_backend_pid()")))) {

  CHECK_OK(process_.Start());
}

PGConnPerf::~PGConnPerf() {
  CHECK_OK(process_.Kill(SIGINT));
  LOG(INFO) << "Perf exec code: " << CHECK_RESULT(process_.Wait());
}

template GetValueResult<int16_t> GetValue<int16_t>(const PGresult*, int, int);
template GetValueResult<int32_t> GetValue<int32_t>(const PGresult*, int, int);
template GetValueResult<int64_t> GetValue<int64_t>(const PGresult*, int, int);
template GetValueResult<PGUint16> GetValue<PGUint16>(const PGresult*, int, int);
template GetValueResult<PGUint32> GetValue<PGUint32>(const PGresult*, int, int);
template GetValueResult<PGUint64> GetValue<PGUint64>(const PGresult*, int, int);
template GetValueResult<float> GetValue<float>(const PGresult*, int, int);
template GetValueResult<double> GetValue<double>(const PGresult*, int, int);
template GetValueResult<bool> GetValue<bool>(const PGresult*, int, int);
template GetValueResult<std::string> GetValue<std::string>(const PGresult*, int, int);
template GetValueResult<char> GetValue<char>(const PGresult*, int, int);
template GetValueResult<PGOid> GetValue<PGOid>(const PGresult*, int, int);
template GetValueResult<Uuid> GetValue<Uuid>(const PGresult*, int, int);

} // namespace yb::pgwrapper
