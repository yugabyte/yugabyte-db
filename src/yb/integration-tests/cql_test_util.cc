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

#include "yb/integration-tests/cql_test_util.h"

#include <cassandra.h>

#include <thread>

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/join.h"

#include "yb/util/enums.h"
#include "yb/util/status_log.h"
#include "yb/util/tsan_util.h"

using std::string;

using namespace std::literals;

namespace yb {

// Supported types - read value:
template <>
CassError GetCassandraValue<std::string>::Apply(const CassValue* val, std::string* v) {
  const char* s = nullptr;
  size_t sz = 0;
  auto result = cass_value_get_string(val, &s, &sz);
  if (result != CASS_OK) {
    return result;
  }
  *v = std::string(s, sz);
  return result;
}

template <>
CassError GetCassandraValue<Slice>::Apply(const CassValue* val, Slice* v) {
  const cass_byte_t* data = nullptr;
  size_t size = 0;
  auto result = cass_value_get_bytes(val, &data, &size);
  *v = Slice(data, size);
  return result;
}

template <>
CassError GetCassandraValue<cass_bool_t>::Apply(const CassValue* val, cass_bool_t* v) {
  return cass_value_get_bool(val, v);
}

template <>
CassError GetCassandraValue<cass_float_t>::Apply(const CassValue* val, cass_float_t* v) {
  return cass_value_get_float(val, v);
}

template <>
CassError GetCassandraValue<cass_double_t>::Apply(const CassValue* val, cass_double_t* v) {
  return cass_value_get_double(val, v);
}

template <>
CassError GetCassandraValue<cass_int32_t>::Apply(const CassValue* val, cass_int32_t* v) {
  return cass_value_get_int32(val, v);
}

template <>
CassError GetCassandraValue<cass_int64_t>::Apply(const CassValue* val, cass_int64_t* v) {
  return cass_value_get_int64(val, v);
}

template <>
CassError GetCassandraValue<CassandraJson>::Apply(const CassValue* val, CassandraJson* v) {
  std::string temp;
  auto result = GetCassandraValue<std::string>::Apply(val, &temp);
  *v = CassandraJson(std::move(temp));
  return result;
}

template <>
CassError GetCassandraValue<CassUuid>::Apply(const CassValue* val, CassUuid* v) {
  return cass_value_get_uuid(val, v);
}

template <>
CassError GetCassandraValue<CassInet>::Apply(const CassValue* val, CassInet* v) {
  return cass_value_get_inet(val, v);
}

bool CassandraValue::IsNull() const {
  return cass_value_is_null(value_);
}

std::string CassandraValue::ToString() const {
  auto value_type = cass_value_type(value_);
  if (IsNull()) {
    return "NULL";
  }
  switch (value_type) {
    case CASS_VALUE_TYPE_BLOB:
      return As<Slice>().ToDebugHexString();
    case CASS_VALUE_TYPE_VARCHAR:
      return As<std::string>();
    case CASS_VALUE_TYPE_BIGINT:
      return std::to_string(As<cass_int64_t>());
    case CASS_VALUE_TYPE_INT:
      return std::to_string(As<cass_int32_t>());
    case CASS_VALUE_TYPE_UUID: {
      char buffer[CASS_UUID_STRING_LENGTH];
      cass_uuid_string(As<CassUuid>(), buffer);
      return buffer;
    }
    case CASS_VALUE_TYPE_INET: {
      char buffer[CASS_INET_STRING_LENGTH];
      cass_inet_string(As<CassInet>(), buffer);
      return buffer;
    }
    case CASS_VALUE_TYPE_MAP: {
      std::string result = "{";
      CassIteratorPtr iterator(cass_iterator_from_map(value_));
      bool first = true;
      while (cass_iterator_next(iterator.get())) {
        if (first) {
          first = false;
        } else {
          result += ", ";
        }
        result += CassandraValue(cass_iterator_get_map_key(iterator.get())).ToString();
        result += " => ";
        result += CassandraValue(cass_iterator_get_map_value(iterator.get())).ToString();
      }
      result += "}";
      return result;
    }
    case CASS_VALUE_TYPE_LIST: {
      std::string result = "[";
      CassIteratorPtr iterator(cass_iterator_from_collection(value_));
      bool first = true;
      while (cass_iterator_next(iterator.get())) {
        if (!first) {
          result += ", ";
        }
        first = false;
        result += CassandraValue(cass_iterator_get_value(iterator.get())).ToString();
      }
      result += "]";
      return result;
    }
    default:
      return "Not supported: " + std::to_string(to_underlying(value_type));
  }
}

bool CassandraRowIterator::Next() {
  return cass_iterator_next(cass_iterator_.get()) != cass_false;
}

CassandraValue CassandraRowIterator::Value() const {
  return CassandraValue(cass_iterator_get_column(cass_iterator_.get()));
}

CassandraValue CassandraRow::Value(size_t index) const {
  return CassandraValue(cass_row_get_column(cass_row_, index));
}

CassandraRowIterator CassandraRow::CreateIterator() const {
  return CassandraRowIterator(cass_iterator_from_row(cass_row_));
}

std::string CassandraRow::RenderToString(const std::string& separator) {
  std::string result;
  auto iter = CreateIterator();
  while (iter.Next()) {
    if (!result.empty()) {
      result += separator;
    }
    result += iter.Value().ToString();
  }
  return result;
}

void CassandraRow::TakeIterator(CassIteratorPtr iterator) {
  cass_iterator_ = std::move(iterator);
}

bool CassandraIterator::Next() {
  return cass_iterator_next(cass_iterator_.get()) != cass_false;
}

CassandraRow CassandraIterator::Row() {
  return CassandraRow(cass_iterator_get_row(cass_iterator_.get()));
}

void CassandraIterator::MoveToRow(CassandraRow* row) {
  row->TakeIterator(std::move(cass_iterator_));
}

CassandraIterator CassandraResult::CreateIterator() const {
  return CassandraIterator(cass_iterator_from_result(cass_result_.get()));
}

bool CassandraResult::HasMorePages() const {
  return cass_result_has_more_pages(cass_result_.get());
}

std::string CassandraResult::RenderToString(
    const std::string& line_separator, const std::string& value_separator) const {
  std::string result;
  auto iter = CreateIterator();
  while (iter.Next()) {
    auto row = iter.Row();
    if (!result.empty()) {
      result += ";";
    }
    result += row.RenderToString();
  }
  return result;
}

bool CassandraFuture::Ready() const {
  return cass_future_ready(future_.get());
}

Status CassandraFuture::Wait() {
  cass_future_wait(future_.get());
  return CheckErrorCode();
}

Status CassandraFuture::WaitFor(MonoDelta duration) {
  if (!cass_future_wait_timed(future_.get(), duration.ToMicroseconds())) {
    return STATUS(TimedOut, "Future timed out");
  }

  return CheckErrorCode();
}

CassandraResult CassandraFuture::Result() {
  return CassandraResult(cass_future_get_result(future_.get()));
}

CassandraPrepared CassandraFuture::Prepared() {
  return CassandraPrepared(cass_future_get_prepared(future_.get()));
}

Status CassandraFuture::CheckErrorCode() {
  const CassError rc = cass_future_error_code(future_.get());
  VLOG(2) << "Last operation RC: " << rc;

  if (rc != CASS_OK) {
    const char* message = nullptr;
    size_t message_sz = 0;
    cass_future_error_message(future_.get(), &message, &message_sz);
    if (message_sz == 0) {
      message = cass_error_desc(rc);
      message_sz = strlen(message);
    }
    Slice message_slice(message, message_sz);
    switch (rc) {
      case CASS_ERROR_LIB_NO_HOSTS_AVAILABLE: FALLTHROUGH_INTENDED;
      case CASS_ERROR_SERVER_OVERLOADED:
        return STATUS(ServiceUnavailable, message_slice);
      case CASS_ERROR_LIB_REQUEST_TIMED_OUT:
        return STATUS(TimedOut, message_slice);
      case CASS_ERROR_SERVER_INVALID_QUERY:
        return STATUS(QLError, message_slice);
      default:
        LOG(INFO) << "Cassandra error code: " << rc << ": " << message;
        return STATUS(RuntimeError, message_slice);
    }
  }

  return Status::OK();
}

namespace {

void CheckErrorCode(const CassError& error_code) {
  CHECK_EQ(CASS_OK, error_code) << ": " << cass_error_desc(error_code);
}

} // namespace

void CassandraStatement::SetKeyspace(const string& keyspace) {
  CheckErrorCode(cass_statement_set_keyspace(cass_statement_.get(), keyspace.c_str()));
}

void CassandraStatement::SetPageSize(int page_size) {
  CheckErrorCode(cass_statement_set_paging_size(cass_statement_.get(), page_size));
}

void CassandraStatement::SetPagingState(const CassandraResult& result) {
  CheckErrorCode(cass_statement_set_paging_state(cass_statement_.get(), result.get()));
}

CassandraStatement& CassandraStatement::Bind(size_t index, const string& v) {
  CheckErrorCode(cass_statement_bind_string(cass_statement_.get(), index, v.c_str()));
  return *this;
}

CassandraStatement& CassandraStatement::Bind(size_t index, const cass_bool_t& v) {
  CheckErrorCode(cass_statement_bind_bool(cass_statement_.get(), index, v));
  return *this;
}

CassandraStatement& CassandraStatement::Bind(size_t index, const cass_float_t& v) {
  CheckErrorCode(cass_statement_bind_float(cass_statement_.get(), index, v));
  return *this;
}

CassandraStatement& CassandraStatement::Bind(size_t index, const cass_double_t& v) {
  CheckErrorCode(cass_statement_bind_double(cass_statement_.get(), index, v));
  return *this;
}

CassandraStatement& CassandraStatement::Bind(size_t index, const cass_int32_t& v) {
  CheckErrorCode(cass_statement_bind_int32(cass_statement_.get(), index, v));
  return *this;
}

CassandraStatement& CassandraStatement::Bind(size_t index, const cass_int64_t& v) {
  CheckErrorCode(cass_statement_bind_int64(cass_statement_.get(), index, v));
  return *this;
}

CassandraStatement& CassandraStatement::Bind(size_t index, const CassandraJson& v) {
  CheckErrorCode(cass_statement_bind_string(cass_statement_.get(), index, v.value().c_str()));
  return *this;
}

CassStatement* CassandraStatement::get() const {
  return cass_statement_.get();
}

void CassandraBatch::Add(CassandraStatement* statement) {
  cass_batch_add_statement(cass_batch_.get(), statement->cass_statement_.get());
}

void DeleteSession::operator()(CassSession* session) const {
  if (session != nullptr) {
    WARN_NOT_OK(CassandraFuture(cass_session_close(session)).Wait(), "Close session");
    cass_session_free(session);
  }
}

Status CassandraSession::Connect(CassCluster* cluster) {
  cass_session_.reset(CHECK_NOTNULL(cass_session_new()));
  return CassandraFuture(cass_session_connect(cass_session_.get(), cluster)).Wait();
}

Result<CassandraSession> CassandraSession::Create(CassCluster* cluster) {
  LOG(INFO) << "Create new session ...";
  CassandraSession result;
  RETURN_NOT_OK(result.Connect(cluster));
  LOG(INFO) << "Create new session - DONE";
  return result;
}

Status CassandraSession::Execute(const CassandraStatement& statement) {
  CassandraFuture future(cass_session_execute(
      cass_session_.get(), statement.cass_statement_.get()));
  return future.Wait();
}

Result<CassandraResult> CassandraSession::ExecuteWithResult(const CassandraStatement& statement) {
  CassandraFuture future(cass_session_execute(
      cass_session_.get(), statement.cass_statement_.get()));
  RETURN_NOT_OK(future.Wait());
  return future.Result();
}

Result<std::string> CassandraSession::ExecuteAndRenderToString(const std::string& statement) {
  return VERIFY_RESULT(ExecuteWithResult(statement)).RenderToString();
}

CassandraFuture CassandraSession::ExecuteGetFuture(const CassandraStatement& statement) {
  return CassandraFuture(
      cass_session_execute(cass_session_.get(), statement.cass_statement_.get()));
}

CassandraFuture CassandraSession::ExecuteGetFuture(const string& query) {
  LOG(INFO) << "Execute query: " << query;
  return ExecuteGetFuture(CassandraStatement(query));
}

Status CassandraSession::ExecuteQuery(const string& query) {
  LOG(INFO) << "Execute query: " << query;
  return Execute(CassandraStatement(query));
}

Result<CassandraResult> CassandraSession::ExecuteWithResult(const string& query) {
  LOG(INFO) << "Execute query: " << query;
  return ExecuteWithResult(CassandraStatement(query));
}

Status CassandraSession::ExecuteBatch(const CassandraBatch& batch) {
  return SubmitBatch(batch).Wait();
}

CassandraFuture CassandraSession::SubmitBatch(const CassandraBatch& batch) {
  return CassandraFuture(
      cass_session_execute_batch(cass_session_.get(), batch.cass_batch_.get()));
}

Result<CassandraPrepared> CassandraSession::Prepare(
    const string& prepare_query, MonoDelta timeout, const string& local_keyspace) {
  VLOG(2) << "Execute prepare request: " << prepare_query << ", timeout: " << timeout
          << ", keyspace: " << local_keyspace;
  auto deadline = CoarseMonoClock::now() + timeout;
  for (;;) {
    CassFuture* cass_future_ptr = nullptr;
    if (local_keyspace.empty()) {
        cass_future_ptr = cass_session_prepare(cass_session_.get(), prepare_query.c_str());
    } else {
        CassandraStatement statement(prepare_query);
        statement.SetKeyspace(local_keyspace);
        cass_future_ptr = cass_session_prepare_from_existing(cass_session_.get(), statement.get());
    }

    CassandraFuture future(cass_future_ptr);
    auto wait_result = future.Wait();
    if (wait_result.ok()) {
      return future.Prepared();
    }

    if (timeout == MonoDelta::kZero || CoarseMonoClock::now() > deadline) {
      return wait_result;
    }
    std::this_thread::sleep_for(100ms);
  }
}

void CassandraSession::Reset() {
  cass_session_.reset();
}

CassandraStatement CassandraPrepared::Bind() {
  return CassandraStatement(cass_prepared_bind(prepared_.get()));
}

const MonoDelta kCassandraTimeOut = 20s * kTimeMultiplier;
const std::string kCqlTestKeyspace = "test";

CppCassandraDriver::CppCassandraDriver(
    const std::vector<std::string>& hosts, uint16_t port,
    UsePartitionAwareRouting use_partition_aware_routing) {

  // Enable detailed tracing inside driver.
  if (VLOG_IS_ON(4)) {
    cass_log_set_level(CASS_LOG_TRACE);
  } else if (VLOG_IS_ON(3)) {
    cass_log_set_level(CASS_LOG_DEBUG);
  } else if (VLOG_IS_ON(2)) {
    cass_log_set_level(CASS_LOG_INFO);
  } else if (VLOG_IS_ON(1)) {
    cass_log_set_level(CASS_LOG_WARN);
  }

  auto hosts_str = JoinStrings(hosts, ",");
  LOG(INFO) << "Create Cassandra cluster to " << hosts_str << " :" << port << " ...";
  cass_cluster_ = CHECK_NOTNULL(cass_cluster_new());
  CheckErrorCode(cass_cluster_set_contact_points(cass_cluster_, hosts_str.c_str()));
  CheckErrorCode(cass_cluster_set_port(cass_cluster_, port));
  cass_cluster_set_request_timeout(
      cass_cluster_, narrow_cast<uint32_t>(kCassandraTimeOut.ToMilliseconds()));

  // Setup cluster configuration: partitions metadata refresh timer = 3 seconds.
  cass_cluster_set_partition_aware_routing(
      cass_cluster_, use_partition_aware_routing ? cass_true : cass_false, 3);
}

CppCassandraDriver::~CppCassandraDriver() {
  LOG(INFO) << "Terminating driver...";

  if (cass_cluster_) {
    cass_cluster_free(cass_cluster_);
    cass_cluster_ = nullptr;
  }

  LOG(INFO) << "Terminating driver - DONE";
}

void CppCassandraDriver::EnableTLS(const std::vector<std::string>& ca_certs) {
  LOG(INFO) << "Enabling TLS...";

  CassSsl* ssl = cass_ssl_new();
  cass_ssl_set_verify_flags(ssl, CASS_SSL_VERIFY_PEER_CERT | CASS_SSL_VERIFY_PEER_IDENTITY);
  for (const auto& ca_cert : ca_certs) {
    CheckErrorCode(cass_ssl_add_trusted_cert(ssl, ca_cert.c_str()));
  }

  cass_cluster_set_ssl(cass_cluster_, ssl);
  cass_ssl_free(ssl);
}

void CppCassandraDriver::SetCredentials(const std::string& username, const std::string& password) {
  LOG(INFO) << "Setting YCQL credentials: " << username << " / " << password;
  cass_cluster_set_credentials(cass_cluster_, username.c_str(), password.c_str());
}

Result<CassandraSession> CppCassandraDriver::CreateSession() {
  return CassandraSession::Create(cass_cluster_);
}

Result<CassandraSession> EstablishSession(CppCassandraDriver* driver) {
  auto session = VERIFY_RESULT(driver->CreateSession());
  RETURN_NOT_OK(
      session.ExecuteQuery(Format("CREATE KEYSPACE IF NOT EXISTS $0;", kCqlTestKeyspace)));
  RETURN_NOT_OK(session.ExecuteQuery(Format("USE $0;", kCqlTestKeyspace)));
  return session;
}

} // namespace yb
