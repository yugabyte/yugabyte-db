//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/cqlserver/cql_processor.h"

#include <sasl/md5global.h>
#include <sasl/md5.h>

#include "yb/cqlserver/cql_service.h"
#include "yb/gutil/strings/escaping.h"

METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_GetProcessor,
    "Time spent to get a processor for processing a CQL query request.",
    yb::MetricUnit::kMicroseconds,
    "Time spent to get a processor for processing a CQL query request.", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_ProcessRequest,
    "Time spent processing a CQL query request. From parsing till executing",
    yb::MetricUnit::kMicroseconds,
    "Time spent processing a CQL query request. From parsing till executing", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_ParseRequest,
    "Time spent parsing CQL query request", yb::MetricUnit::kMicroseconds,
    "Time spent parsing CQL query request", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_QueueResponse,
    "Time spent to queue the response for a CQL query request back on the network",
    yb::MetricUnit::kMicroseconds,
    "Time spent after computing the CQL response to queue it onto the connection.", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_ExecuteRequest,
    "Time spent executing the CQL query request", yb::MetricUnit::kMicroseconds,
    "Time spent executing the CQL query request", 60000000LU, 2);
METRIC_DEFINE_counter(
    server, yb_cqlserver_CQLServerService_ParsingErrors, "Errors encountered when parsing ",
    yb::MetricUnit::kRequests, "Errors encountered when parsing ");

namespace yb {
namespace cqlserver {

using std::shared_ptr;
using std::unique_ptr;

using client::YBClient;
using client::YBSession;
using client::YBTableCache;
using sql::ExecuteResult;
using sql::PreparedResult;
using sql::RowsResult;
using sql::SetKeyspaceResult;
using sql::SqlProcessor;
using sql::Statement;

//------------------------------------------------------------------------------------------------
CQLMetrics::CQLMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity)
    : SqlMetrics(metric_entity) {
  time_to_process_request_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_ProcessRequest.Instantiate(
          metric_entity);
  time_to_get_cql_processor_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_GetProcessor.Instantiate(metric_entity);
  time_to_parse_cql_wrapper_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_ParseRequest.Instantiate(metric_entity);
  time_to_execute_cql_request_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_ExecuteRequest.Instantiate(
          metric_entity);
  time_to_queue_cql_response_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_QueueResponse.Instantiate(metric_entity);
  num_errors_parsing_cql_ =
      METRIC_yb_cqlserver_CQLServerService_ParsingErrors.Instantiate(metric_entity);
}

//------------------------------------------------------------------------------------------------
CQLStatement::CQLStatement(const string& keyspace, const string& sql_stmt)
    : Statement(keyspace, sql_stmt) {
}

CQLMessage::QueryId CQLStatement::GetQueryId(const string& keyspace, const string& sql_stmt) {
  unsigned char md5[16];
  MD5_CTX md5ctx;
  _sasl_MD5Init(&md5ctx);
  _sasl_MD5Update(&md5ctx, util::to_uchar_ptr(keyspace.data()), keyspace.length());
  _sasl_MD5Update(&md5ctx, util::to_uchar_ptr(sql_stmt.data()), sql_stmt.length());
  _sasl_MD5Final(md5, &md5ctx);
  return CQLMessage::QueryId(util::to_char_ptr(md5), sizeof(md5));
}

//------------------------------------------------------------------------------------------------
CQLProcessor::CQLProcessor(CQLServiceImpl* service_impl)
    : SqlProcessor(
          service_impl->client(), service_impl->table_cache(), service_impl->cql_metrics().get()),
      service_impl_(service_impl),
      cql_metrics_(service_impl->cql_metrics()) {
}

CQLProcessor::~CQLProcessor() {
}

void CQLProcessor::ProcessCall(const Slice& msg, unique_ptr<CQLResponse> *response) {
  unique_ptr<CQLRequest> request;

  // Parse the CQL request. If the parser failed, it sets the error message in response.
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  if (CQLRequest::ParseRequest(msg, &request, response)) {
    MonoTime parsed = MonoTime::Now(MonoTime::FINE);
    cql_metrics_->time_to_parse_cql_wrapper_->Increment(
        parsed.GetDeltaSince(start).ToMicroseconds());
    // Execute the request.
    response->reset(request->Execute(this));
    MonoTime executed = MonoTime::Now(MonoTime::FINE);
    cql_metrics_->time_to_execute_cql_request_->Increment(
        executed.GetDeltaSince(parsed).ToMicroseconds());
  } else {
    cql_metrics_->num_errors_parsing_cql_->Increment();
  }
}

CQLResponse *CQLProcessor::ProcessPrepare(const PrepareRequest& req) {
  VLOG(1) << "PREPARE " << req.query();
  const CQLMessage::QueryId query_id = CQLStatement::GetQueryId(
      sql_env_->CurrentKeyspace(), req.query());
  // To prevent multiple clients from preparing the same new statement in parallel and trying to
  // cache the same statement (a typical "login storm" scenario), each caller will try to allocate
  // the statement in the cached statement first. If it already exists, the existing one will be
  // returned instead. Then, each client will try to prepare the statement. The first one will do
  // the actual prepare while the rest wait. As the rest do the prepare afterwards, the statement
  // is already prepared so it will be an no-op (see Statement::Prepare).
  shared_ptr<CQLStatement> stmt = service_impl_->AllocatePreparedStatement(
      query_id, sql_env_->CurrentKeyspace(), req.query());
  PreparedResult::UniPtr result;
  const Status s = stmt->Prepare(
      this, Statement::kNoLastPrepareTime, false /* refresh_cache */,
      service_impl_->prepared_stmts_mem_tracker(), &result);
  if (!s.ok()) {
    return new ErrorResponse(req, ErrorResponse::Code::SYNTAX_ERROR, s.ToString());
  }

  return new PreparedResultResponse(req, query_id, result.get());
}

CQLResponse *CQLProcessor::ProcessExecute(const ExecuteRequest& req) {
  VLOG(1) << "EXECUTE " << b2a_hex(req.query_id());
  shared_ptr<CQLStatement> stmt = service_impl_->GetPreparedStatement(req.query_id());
  if (stmt == nullptr) {
    // If the query is not found, it may have been aged out. Return UNPREPARED error. Upon receiving
    // the error, the client will reprepare the query and execute again.
    return new UnpreparedErrorResponse(req, req.query_id());
  }
  ExecuteResult::UniPtr result;
  Status s = stmt->Execute(this, req.params(), &result);
  if (!s.ok()) {
    return new ErrorResponse(req, ErrorResponse::Code::SYNTAX_ERROR, s.ToString());
  }
  return ReturnResult(req, std::move(result));
}

CQLResponse *CQLProcessor::ProcessQuery(const QueryRequest& req) {
  VLOG(1) << "QUERY " << req.query();
  ExecuteResult::UniPtr result;
  Status s = Run(req.query(), req.params(), &result);
  if (!s.ok()) {
    return new ErrorResponse(req, ErrorResponse::Code::SYNTAX_ERROR, s.ToString());
  }
  return ReturnResult(req, move(result));
}

CQLResponse *CQLProcessor::ReturnResult(const CQLRequest& req, ExecuteResult::UniPtr result) {
  if (result == nullptr) {
    return new VoidResultResponse(req);
  }
  switch (result->type()) {
    case ExecuteResult::Type::SET_KEYSPACE: {
      const SetKeyspaceResult *set_keyspace_result =
          static_cast<const SetKeyspaceResult*>(result.get());
      return new SetKeyspaceResultResponse(req, *set_keyspace_result);
    }
    case ExecuteResult::Type::ROWS:
      RowsResult::UniPtr rows_result(static_cast<RowsResult*>(result.release()));
      cql_metrics_->sql_response_size_bytes_->Increment(rows_result->rows_data().size());
      switch (req.opcode()) {
        case CQLMessage::Opcode::EXECUTE:
          return new RowsResultResponse(static_cast<const ExecuteRequest&>(req), move(rows_result));
        case CQLMessage::Opcode::QUERY:
          return new RowsResultResponse(static_cast<const QueryRequest&>(req), move(rows_result));
        case CQLMessage::Opcode::ERROR:   FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::STARTUP: FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::READY:   FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::AUTHENTICATE: FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::OPTIONS:   FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::SUPPORTED: FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::RESULT:    FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::PREPARE:   FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::REGISTER:  FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::EVENT:     FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::BATCH:     FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::AUTH_CHALLENGE: FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::AUTH_RESPONSE:  FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::AUTH_SUCCESS:
          break;
        // default: fall through
      }
      LOG(FATAL) << "Internal error: not a request that returns result "
                 << static_cast<int>(req.opcode());
      break;

    // default: fall through
  }
  LOG(FATAL) << "Internal error: unknown result type " << static_cast<int>(result->type());
}

}  // namespace cqlserver
}  // namespace yb
