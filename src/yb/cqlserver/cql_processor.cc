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
using client::YBTableCache;
using rpc::RpcContext;
using sql::ExecutedResult;
using sql::PreparedResult;
using sql::RowsResult;
using sql::SetKeyspaceResult;
using sql::SchemaChangeResult;
using sql::SqlProcessor;
using sql::Statement;
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
          service_impl->messenger(), service_impl->client(), service_impl->table_cache(),
          service_impl->cql_metrics().get(), service_impl->cql_rpc_env()),
      service_impl_(service_impl),
      cql_metrics_(service_impl->cql_metrics()),
      pos_(pos) {
}

CQLProcessor::~CQLProcessor() {
}

void CQLProcessor::ProcessCall(rpc::InboundCallPtr cql_call) {
  const Slice& msg = cql_call->serialized_request();
  unique_ptr<CQLRequest> request;
  unique_ptr<CQLResponse> response;

  // Parse the CQL request. If the parser failed, it sets the error message in response.
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  if (!CQLRequest::ParseRequest(msg, &request, &response)) {
    cql_metrics_->num_errors_parsing_cql_->Increment();
    SendResponse(cql_call, response.release());
    service_impl_->ReturnProcessor(pos_);
    return;
  }

  MonoTime parsed = MonoTime::Now(MonoTime::FINE);
  cql_metrics_->time_to_parse_cql_wrapper_->Increment(parsed.GetDeltaSince(start).ToMicroseconds());

  // Execute the request (perhaps asynchronously). Passing ownership of the request to the callback.
  SetCurrentCall(cql_call);
  request->ExecuteAsync(this, Bind(
      &CQLProcessor::ProcessCallDone, Unretained(this), cql_call, Owned(request.release()),
      parsed));
}

void CQLProcessor::ProcessCallDone(rpc::InboundCallPtr call,
                                   const CQLRequest* request,
                                   const MonoTime& start,
                                   CQLResponse* response) {
  // Reply to client.
  MonoTime begin_response = MonoTime::Now(MonoTime::FINE);
  cql_metrics_->time_to_execute_cql_request_->Increment(
      begin_response.GetDeltaSince(start).ToMicroseconds());
  SendResponse(std::move(call), response);

  // Release the processor.
  MonoTime response_done = MonoTime::Now(MonoTime::FINE);
  cql_metrics_->time_to_process_request_->Increment(
      response_done.GetDeltaSince(start).ToMicroseconds());
  cql_metrics_->time_to_queue_cql_response_->Increment(
      response_done.GetDeltaSince(begin_response).ToMicroseconds());
  SetCurrentCall(nullptr);
  service_impl_->ReturnProcessor(pos_);
}

void CQLProcessor::SendResponse(rpc::InboundCallPtr call, CQLResponse* response) {
  CHECK(response != nullptr);
  // Serialize the response to return to the CQL client. In case of error, an error response
  // should still be present.
  faststring temp;
  response->Serialize(&temp);
  rpc::CQLInboundCall* cql_call = down_cast<rpc::CQLInboundCall*>(call.get());
  cql_call->response_msg_buf() = util::RefCntBuffer(temp);
  delete response;
  RpcContext context(std::move(call), cql_metrics_->rpc_method_metrics_);
  context.RespondSuccess();
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

void CQLProcessor::ProcessExecute(const ExecuteRequest& req, Callback<void(CQLResponse*)> cb) {
  VLOG(1) << "EXECUTE " << b2a_hex(req.query_id());
  shared_ptr<CQLStatement> stmt = service_impl_->GetPreparedStatement(req.query_id());
  if (stmt == nullptr ||
      !stmt->ExecuteAsync(
          this, req.params(),
          Bind(&CQLProcessor::ProcessExecuteDone, Unretained(this), ConstRef(req), stmt,
               std::move(cb)))) {
    // If the query is not found or it is not prepared successfully, return UNPREPARED error. Upon
    // receiving the error, the client will reprepare the query and execute again.
    cb.Run(new UnpreparedErrorResponse(req, req.query_id()));
  }
}

void CQLProcessor::ProcessExecuteDone(
    const ExecuteRequest& req, shared_ptr<CQLStatement> stmt, Callback<void(CQLResponse*)> cb,
    const Status& s, const ExecutedResult::SharedPtr& result) {
  cb.Run(ReturnResponse(req, stmt, s, result));
}

void CQLProcessor::ProcessQuery(const QueryRequest& req, Callback<void(CQLResponse*)> cb) {
  VLOG(1) << "QUERY " << req.query();
  RunAsync(
      req.query(), req.params(),
      Bind(&CQLProcessor::ProcessQueryDone, Unretained(this), ConstRef(req), std::move(cb)));
}

void CQLProcessor::ProcessQueryDone(
    const QueryRequest& req, Callback<void(CQLResponse*)> cb, const Status& s,
    const ExecutedResult::SharedPtr& result) {
  cb.Run(ReturnResponse(req, nullptr /* stmt */, s, result));
}

void CQLProcessor::ProcessBatch(const BatchRequest& req, Callback<void(CQLResponse*)> cb, int idx) {
  VLOG(1) << "BATCH " << idx << "/" << req.queries().size();
  if (idx >= req.queries().size()) {
    cb.Run(ReturnResponse(req, nullptr /* stmt */, Status::OK(), nullptr /* result */));
    return;
  }
  const BatchRequest::Query& query = req.queries()[idx];
  if (query.is_prepared) {
    shared_ptr<CQLStatement> stmt = service_impl_->GetPreparedStatement(query.query_id);
    if (stmt == nullptr ||
        !stmt->ExecuteAsync(
            this, query.params,
            Bind(&CQLProcessor::ProcessBatchDone, Unretained(this), ConstRef(req),
                 idx, stmt, std::move(cb)))) {
      // If the query is not found or it is not prepared successfully, return UNPREPARED error. Upon
      // receiving the error, the client will reprepare the query and execute again.
      cb.Run(new UnpreparedErrorResponse(req, query.query_id));
      return;
    }
  } else {
    RunAsync(
        query.query, query.params,
        Bind(&CQLProcessor::ProcessBatchDone, Unretained(this), ConstRef(req),
             idx, nullptr /* stmt */, std::move(cb)));
  }
}

void CQLProcessor::ProcessBatchDone(
    const BatchRequest& req, const int idx, shared_ptr<CQLStatement> stmt,
    Callback<void(CQLResponse*)> cb, const Status& s, const ExecutedResult::SharedPtr& result) {
  if (PREDICT_FALSE(!s.ok())) {
    cb.Run(ReturnResponse(req, stmt, s, result));
    return;
  }
  ProcessBatch(req, std::move(cb), idx + 1);
}

CQLResponse* CQLProcessor::ReturnResponse(
    const CQLRequest& req, shared_ptr<CQLStatement> stmt, Status s,
    const ExecutedResult::SharedPtr& result) {
  if (!s.ok()) {
    if (s.IsSqlError()) {
      switch (GetErrorCode(s)) {
        // Syntax errors.
        case ErrorCode::SQL_STATEMENT_INVALID: FALLTHROUGH_INTENDED;
        case ErrorCode::CQL_STATEMENT_INVALID: FALLTHROUGH_INTENDED;
        case ErrorCode::LEXICAL_ERROR: FALLTHROUGH_INTENDED;
        case ErrorCode::CHARACTER_NOT_IN_REPERTOIRE: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_ESCAPE_SEQUENCE: FALLTHROUGH_INTENDED;
        case ErrorCode::NAME_TOO_LONG: FALLTHROUGH_INTENDED;
        case ErrorCode::NONSTANDARD_USE_OF_ESCAPE_CHARACTER: FALLTHROUGH_INTENDED;
        case ErrorCode::SYNTAX_ERROR: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_PARAMETER_VALUE:
          return new ErrorResponse(req, ErrorResponse::Code::SYNTAX_ERROR, s.ToString());

        // Semantic errors.
        case ErrorCode::FEATURE_NOT_YET_IMPLEMENTED: FALLTHROUGH_INTENDED;
        case ErrorCode::FEATURE_NOT_SUPPORTED: FALLTHROUGH_INTENDED;
        case ErrorCode::SEM_ERROR: FALLTHROUGH_INTENDED;
        case ErrorCode::DATATYPE_MISMATCH: FALLTHROUGH_INTENDED;
        case ErrorCode::DUPLICATE_TABLE: FALLTHROUGH_INTENDED;
        case ErrorCode::UNDEFINED_COLUMN: FALLTHROUGH_INTENDED;
        case ErrorCode::DUPLICATE_COLUMN: FALLTHROUGH_INTENDED;
        case ErrorCode::MISSING_PRIMARY_KEY: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_PRIMARY_COLUMN_TYPE: FALLTHROUGH_INTENDED;
        case ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY: FALLTHROUGH_INTENDED;
        case ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY: FALLTHROUGH_INTENDED;
        case ErrorCode::INCOMPARABLE_DATATYPES: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_TABLE_PROPERTY: FALLTHROUGH_INTENDED;
        case ErrorCode::DUPLICATE_TABLE_PROPERTY: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_DATATYPE: FALLTHROUGH_INTENDED;
        case ErrorCode::SYSTEM_NAMESPACE_READONLY: FALLTHROUGH_INTENDED;
        case ErrorCode::NO_NAMESPACE_USED: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_ARGUMENTS: FALLTHROUGH_INTENDED;
        case ErrorCode::TOO_FEW_ARGUMENTS: FALLTHROUGH_INTENDED;
        case ErrorCode::TOO_MANY_ARGUMENTS: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_FUNCTION_CALL: FALLTHROUGH_INTENDED;
        case ErrorCode::INSERT_TABLE_OF_COUNTERS: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_COUNTING_EXPR: FALLTHROUGH_INTENDED;

        // Execution errors that are not server errors.
        case ErrorCode::TABLE_NOT_FOUND: FALLTHROUGH_INTENDED;
        case ErrorCode::INVALID_TABLE_DEFINITION: FALLTHROUGH_INTENDED;
        case ErrorCode::KEYSPACE_ALREADY_EXISTS: FALLTHROUGH_INTENDED;
        case ErrorCode::KEYSPACE_NOT_FOUND: FALLTHROUGH_INTENDED;
        case ErrorCode::WRONG_METADATA_VERSION: FALLTHROUGH_INTENDED;
        case ErrorCode::TABLET_NOT_FOUND:
          return new ErrorResponse(req, ErrorResponse::Code::INVALID, s.ToString());

        // Execution errors that are server errors.
        case ErrorCode::FAILURE: FALLTHROUGH_INTENDED;
        case ErrorCode::EXEC_ERROR:
          return new ErrorResponse(req, ErrorResponse::Code::SERVER_ERROR, s.ToString());

        case ErrorCode::STALE_PREPARED_STATEMENT:
          if (stmt != nullptr) {
            // The prepared statement is stale. Delete it and return UNPREPARED error to tell the
            // client to reprepare it.
            service_impl_->DeletePreparedStatement(stmt);
            return new UnpreparedErrorResponse(req, stmt->query_id());
          } else {
            // This is an internal error as the caller should always provide the statement that is
            // stale.
            return new ErrorResponse(
                req, ErrorResponse::Code::SERVER_ERROR,
                "internal error: stale prepared statement not provided");
          }

        case ErrorCode::SUCCESS:
          break;

        // Warnings.
        case ErrorCode::NOTFOUND:
          break;
      }
      LOG(ERROR) << "Internal error: invalid error code " << static_cast<int64_t>(GetErrorCode(s));
      return new ErrorResponse(req, ErrorResponse::Code::SERVER_ERROR, "Invalid error code");
    }
    return new ErrorResponse(req, ErrorResponse::Code::SERVER_ERROR, s.ToString());
  }
  if (result == nullptr) {
    return new VoidResultResponse(req);
  }
  switch (result->type()) {
    case ExecutedResult::Type::SET_KEYSPACE: {
      SetKeyspaceResult *set_keyspace_result =
          down_cast<SetKeyspaceResult*>(result.get());
      return new SetKeyspaceResultResponse(req, *set_keyspace_result);
    }
    case ExecutedResult::Type::ROWS: {
      const RowsResult::SharedPtr& rows_result = std::static_pointer_cast<RowsResult>(result);
      cql_metrics_->sql_response_size_bytes_->Increment(rows_result->rows_data().size());
      switch (req.opcode()) {
        case CQLMessage::Opcode::EXECUTE:
          return new RowsResultResponse(down_cast<const ExecuteRequest&>(req), rows_result);
        case CQLMessage::Opcode::QUERY:
          return new RowsResultResponse(down_cast<const QueryRequest&>(req), rows_result);
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
    }
    case ExecutedResult::Type::SCHEMA_CHANGE: {
      SchemaChangeResult *schema_change_result =
          down_cast<SchemaChangeResult*>(result.get());
      return new SchemaChangeResultResponse(req, *schema_change_result);
    }

    // default: fall through
  }
  LOG(ERROR) << "Internal error: unknown result type " << static_cast<int>(result->type());
  return new ErrorResponse(
      req, ErrorResponse::Code::SERVER_ERROR, "Internal error: unknown result type");
}

}  // namespace cqlserver
}  // namespace yb
