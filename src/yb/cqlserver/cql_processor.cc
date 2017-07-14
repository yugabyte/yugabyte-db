//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/cqlserver/cql_processor.h"

#include <sasl/md5global.h>
#include <sasl/md5.h>

#include "yb/cqlserver/cql_service.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/rpc/rpc_context.h"

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
    "Time spent executing the CQL query request in the handler", yb::MetricUnit::kMicroseconds,
    "Time spent executing the CQL query request in the handler", 60000000LU, 2);
METRIC_DEFINE_counter(
    server, yb_cqlserver_CQLServerService_ParsingErrors, "Errors encountered when parsing ",
    yb::MetricUnit::kRequests, "Errors encountered when parsing ");
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_Any,
    "yb.cqlserver.CQLServerService.AnyMethod RPC Time", yb::MetricUnit::kMicroseconds,
    "Microseconds spent handling "
    "yb.cqlserver.CQLServerService.AnyMethod() "
    "RPC requests",
    60000000LU, 2);

namespace yb {
namespace cqlserver {

using std::shared_ptr;
using std::unique_ptr;

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;
using sql::ExecutedResult;
using sql::PreparedResult;
using sql::RowsResult;
using sql::SetKeyspaceResult;
using sql::SchemaChangeResult;
using sql::SqlProcessor;
using sql::Statement;
using sql::StatementExecutedCallback;
using sql::ErrorCode;
using sql::GetErrorCode;

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
  rpc_method_metrics_.handler_latency =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_Any.Instantiate(metric_entity);
  num_errors_parsing_cql_ =
      METRIC_yb_cqlserver_CQLServerService_ParsingErrors.Instantiate(metric_entity);
}

//------------------------------------------------------------------------------------------------
CQLProcessor::CQLProcessor(CQLServiceImpl* service_impl, const CQLProcessorListPos& pos)
    : SqlProcessor(
          service_impl->messenger(), service_impl->client(), service_impl->metadata_cache(),
          service_impl->cql_metrics().get(), service_impl->cql_rpc_env()),
      service_impl_(service_impl),
      cql_metrics_(service_impl->cql_metrics()),
      pos_(pos),
      statement_executed_cb_(Bind(&CQLProcessor::StatementExecuted, Unretained(this))) {
}

CQLProcessor::~CQLProcessor() {
}

void CQLProcessor::ProcessCall(rpc::InboundCallPtr call) {
  call_ = std::move(call);
  unique_ptr<CQLRequest> request;
  unique_ptr<CQLResponse> response;

  // Parse the CQL request. If the parser failed, it sets the error message in response.
  parse_begin_ = MonoTime::Now(MonoTime::FINE);
  if (!CQLRequest::ParseRequest(call_->serialized_request(), &request, &response)) {
    cql_metrics_->num_errors_parsing_cql_->Increment();
    SendResponse(*response);
    service_impl_->ReturnProcessor(pos_);
    return;
  }

  execute_begin_ = MonoTime::Now(MonoTime::FINE);
  cql_metrics_->time_to_parse_cql_wrapper_->Increment(
      execute_begin_.GetDeltaSince(parse_begin_).ToMicroseconds());

  // Execute the request (perhaps asynchronously).
  SetCurrentCall(call_);
  request_ = std::move(request);
  switch (request_->opcode()) {
    case CQLMessage::Opcode::PREPARE:
      response.reset(ProcessPrepare(static_cast<const PrepareRequest&>(*request_)));
      break;
    case CQLMessage::Opcode::EXECUTE:
      response.reset(ProcessExecute(static_cast<const ExecuteRequest&>(*request_)));
      break;
    case CQLMessage::Opcode::QUERY:
      response.reset(ProcessQuery(static_cast<const QueryRequest&>(*request_)));
      break;
    case CQLMessage::Opcode::BATCH:
      batch_index_ = 0;
      response.reset(ProcessBatch(static_cast<const BatchRequest&>(*request_)));
      break;
    default:
      response.reset(request_->Execute());
      break;
  }
  if (response != nullptr) {
    SendResponse(*response);
  }
}

void CQLProcessor::SendResponse(const CQLResponse& response) {
  // Serialize the response to return to the CQL client. In case of error, an error response
  // should still be present.
  MonoTime response_begin = MonoTime::Now(MonoTime::FINE);
  faststring msg;
  response.Serialize(&msg);
  auto* cql_call = down_cast<CQLInboundCall*>(call_.get());
  cql_call->RespondSuccess(util::RefCntBuffer(msg), cql_metrics_->rpc_method_metrics_);

  MonoTime response_done = MonoTime::Now(MonoTime::FINE);
  cql_metrics_->time_to_process_request_->Increment(
      response_done.GetDeltaSince(parse_begin_).ToMicroseconds());
  if (request_ != nullptr) {
    cql_metrics_->time_to_execute_cql_request_->Increment(
        response_begin.GetDeltaSince(execute_begin_).ToMicroseconds());
  }
  cql_metrics_->time_to_queue_cql_response_->Increment(
      response_done.GetDeltaSince(response_begin).ToMicroseconds());

  // Release the processor.
  call_ = nullptr;
  request_ = nullptr;
  stmt_ = nullptr;
  SetCurrentCall(nullptr);
  service_impl_->ReturnProcessor(pos_);
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
  const Status s = stmt->Prepare(this, service_impl_->prepared_stmts_mem_tracker(), &result);
  if (!s.ok()) {
    service_impl_->DeletePreparedStatement(stmt);
    return new ErrorResponse(req, ErrorResponse::Code::SYNTAX_ERROR, s.ToString());
  }

  return result != nullptr ?
      new PreparedResultResponse(req, query_id, *result) :
      new PreparedResultResponse(req, query_id);
}

CQLResponse* CQLProcessor::ProcessExecute(const ExecuteRequest& req) {
  VLOG(1) << "EXECUTE " << b2a_hex(req.query_id());
  stmt_ = service_impl_->GetPreparedStatement(req.query_id());
  if (stmt_ == nullptr || !stmt_->ExecuteAsync(this, req.params(), statement_executed_cb_)) {
    // If the query is not found or it is not prepared successfully, return UNPREPARED error. Upon
    // receiving the error, the client will reprepare the query and execute again.
    return new UnpreparedErrorResponse(req, req.query_id());
  }
  return nullptr;
}

CQLResponse* CQLProcessor::ProcessQuery(const QueryRequest& req) {
  VLOG(1) << "QUERY " << req.query();
  RunAsync(req.query(), req.params(), statement_executed_cb_);
  return nullptr;
}

CQLResponse* CQLProcessor::ProcessBatch(const BatchRequest& req) {
  VLOG(1) << "BATCH " << batch_index_ << "/" << req.queries().size();
  if (batch_index_ >= req.queries().size()) {
    return ProcessResult(Status::OK(), nullptr /* result */);
  }
  const BatchRequest::Query& query = req.queries()[batch_index_];
  if (query.is_prepared) {
    VLOG(1) << "BATCH EXECUTE " << query.query_id;
    stmt_ = service_impl_->GetPreparedStatement(query.query_id);
    if (stmt_ == nullptr || !stmt_->ExecuteAsync(this, query.params, statement_executed_cb_)) {
      // If the query is not found or it is not prepared successfully, return UNPREPARED error. Upon
      // receiving the error, the client will reprepare the query and execute again.
      return new UnpreparedErrorResponse(req, query.query_id);
    }
  } else {
    VLOG(1) << "BATCH QUERY " << query.query;
    stmt_ = nullptr;
    RunAsync(query.query, query.params, statement_executed_cb_);
  }
  return nullptr;
}

void CQLProcessor::StatementExecuted(
    const Status& s, const sql::ExecutedResult::SharedPtr& result) {
  unique_ptr<CQLResponse> response;
  if (s.ok() && request_->opcode() == CQLMessage::Opcode::BATCH) {
    batch_index_++;
    response.reset(ProcessBatch(static_cast<const BatchRequest&>(*request_)));
  } else {
    response.reset(ProcessResult(s, result));
  }
  if (response != nullptr) {
    SendResponse(*response);
  }
}

CQLResponse* CQLProcessor::ProcessResult(Status s, const ExecutedResult::SharedPtr& result) {
  if (!s.ok()) {
    if (s.IsSqlError()) {
      ErrorCode yql_errcode = GetErrorCode(s);
      if (yql_errcode == ErrorCode::STALE_PREPARED_STATEMENT) {
        if (stmt_ != nullptr) {
          // The prepared statement is stale. Delete it and return UNPREPARED error to tell the
          // client to reprepare it.
          service_impl_->DeletePreparedStatement(stmt_);
          return new UnpreparedErrorResponse(*request_, stmt_->query_id());
        } else {
          // This is an internal error as the caller should always provide the stale statement.
          return new ErrorResponse(*request_, ErrorResponse::Code::SERVER_ERROR,
                                   "internal error: stale prepared statement not provided");
        }
      } else if (yql_errcode < ErrorCode::SUCCESS) {
        if (yql_errcode > ErrorCode::LIMITATION_ERROR) {
          // System errors, internal errors, or crashes.
          return new ErrorResponse(*request_, ErrorResponse::Code::SERVER_ERROR, s.ToString());
        } else if (yql_errcode > ErrorCode::SEM_ERROR) {
          // Limitation, lexical, or parsing errors.
          return new ErrorResponse(*request_, ErrorResponse::Code::SYNTAX_ERROR, s.ToString());
        } else {
          // Semantic or execution errors.
          return new ErrorResponse(*request_, ErrorResponse::Code::INVALID, s.ToString());
        }
      }

      LOG(ERROR) << "Internal error: invalid error code " << static_cast<int64_t>(GetErrorCode(s));
      return new ErrorResponse(*request_, ErrorResponse::Code::SERVER_ERROR, "Invalid error code");
    }
    return new ErrorResponse(*request_, ErrorResponse::Code::SERVER_ERROR, s.ToString());
  }
  if (result == nullptr) {
    return new VoidResultResponse(*request_);
  }
  switch (result->type()) {
    case ExecutedResult::Type::SET_KEYSPACE: {
      const auto& set_keyspace_result = static_cast<const SetKeyspaceResult&>(*result);
      return new SetKeyspaceResultResponse(*request_, set_keyspace_result);
    }
    case ExecutedResult::Type::ROWS: {
      const RowsResult::SharedPtr& rows_result = std::static_pointer_cast<RowsResult>(result);
      cql_metrics_->sql_response_size_bytes_->Increment(rows_result->rows_data().size());
      switch (request_->opcode()) {
        case CQLMessage::Opcode::EXECUTE:
          return new RowsResultResponse(down_cast<const ExecuteRequest&>(*request_), rows_result);
        case CQLMessage::Opcode::QUERY:
          return new RowsResultResponse(down_cast<const QueryRequest&>(*request_), rows_result);
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
                 << static_cast<int>(request_->opcode());
      break;
    }
    case ExecutedResult::Type::SCHEMA_CHANGE: {
      const auto& schema_change_result = static_cast<const SchemaChangeResult&>(*result);
      return new SchemaChangeResultResponse(*request_, schema_change_result);
    }

    // default: fall through
  }
  LOG(ERROR) << "Internal error: unknown result type " << static_cast<int>(result->type());
  return new ErrorResponse(
      *request_, ErrorResponse::Code::SERVER_ERROR, "Internal error: unknown result type");
}

}  // namespace cqlserver
}  // namespace yb
