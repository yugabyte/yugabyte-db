//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/cqlserver/cql_processor.h"

#include "yb/gutil/strings/escaping.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/rpc_context.h"

#include "yb/util/crypt.h"

#include "yb/yql/cql/cqlserver/cql_service.h"

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

DECLARE_bool(use_cassandra_authentication);

namespace yb {
namespace cqlserver {

const unordered_map<string, vector<string>> kSupportedOptions = {
  {CQLMessage::kCQLVersionOption, {"3.0.0" /* minimum */, "3.4.2" /* current */} },
  {CQLMessage::kCompressionOption, {CQLMessage::kLZ4Compression, CQLMessage::kSnappyCompression} }
};

constexpr const char* const kCassandraPasswordAuthenticator =
    "org.apache.cassandra.auth.PasswordAuthenticator";

extern const char* const kRoleColumnNameSaltedHash;

using std::shared_ptr;
using std::unique_ptr;

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;
using ql::ExecutedResult;
using ql::PreparedResult;
using ql::RowsResult;
using ql::SetKeyspaceResult;
using ql::SchemaChangeResult;
using ql::QLProcessor;
using ql::Statement;
using ql::StatementExecutedCallback;
using ql::ErrorCode;
using ql::GetErrorCode;
using strings::Substitute;
using yb::util::bcrypt_checkpw;

//------------------------------------------------------------------------------------------------
CQLMetrics::CQLMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity)
    : QLMetrics(metric_entity) {
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
    : QLProcessor(
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
  call_ = std::dynamic_pointer_cast<CQLInboundCall>(std::move(call));
  unique_ptr<CQLRequest> request;
  unique_ptr<CQLResponse> response;

  // Parse the CQL request. If the parser failed, it sets the error message in response.
  parse_begin_ = MonoTime::Now();
  const auto& context = static_cast<const CQLConnectionContext&>(call_->connection()->context());
  const auto compression_scheme = context.compression_scheme();
  if (!CQLRequest::ParseRequest(call_->serialized_request(), compression_scheme,
                                &request, &response)) {
    cql_metrics_->num_errors_parsing_cql_->Increment();
    SendResponse(*response);
    return;
  }

  execute_begin_ = MonoTime::Now();
  cql_metrics_->time_to_parse_cql_wrapper_->Increment(
      execute_begin_.GetDeltaSince(parse_begin_).ToMicroseconds());

  // Execute the request (perhaps asynchronously).
  SetCurrentCall(call_);
  request_ = std::move(request);
  call_->SetRequest(request_, service_impl_);
  retry_count_ = 0;
  unprepared_id_.clear();
  response.reset(ProcessRequest(*request_));
  if (response != nullptr) {
    SendResponse(*response);
  }
}

void CQLProcessor::SendResponse(const CQLResponse& response) {
  // Serialize the response to return to the CQL client. In case of error, an error response
  // should still be present.
  MonoTime response_begin = MonoTime::Now();
  const auto& context = static_cast<const CQLConnectionContext&>(call_->connection()->context());
  const auto compression_scheme = context.compression_scheme();
  faststring msg;
  response.Serialize(compression_scheme, &msg);
  call_->RespondSuccess(RefCntBuffer(msg), cql_metrics_->rpc_method_metrics_);

  MonoTime response_done = MonoTime::Now();
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
  stmts_.clear();
  parse_trees_.clear();
  SetCurrentCall(nullptr);
  service_impl_->ReturnProcessor(pos_);
}

CQLResponse* CQLProcessor::ProcessRequest(const CQLRequest& req) {
  switch (req.opcode()) {
    case CQLMessage::Opcode::OPTIONS:
      return ProcessRequest(static_cast<const OptionsRequest&>(req));
    case CQLMessage::Opcode::STARTUP:
      return ProcessRequest(static_cast<const StartupRequest&>(req));
    case CQLMessage::Opcode::PREPARE:
      return ProcessRequest(static_cast<const PrepareRequest&>(req));
    case CQLMessage::Opcode::EXECUTE:
      return ProcessRequest(static_cast<const ExecuteRequest&>(req));
    case CQLMessage::Opcode::QUERY:
      return ProcessRequest(static_cast<const QueryRequest&>(req));
    case CQLMessage::Opcode::BATCH:
      return ProcessRequest(static_cast<const BatchRequest&>(req));
    case CQLMessage::Opcode::AUTH_RESPONSE:
      return ProcessRequest(static_cast<const AuthResponseRequest&>(req));
    case CQLMessage::Opcode::REGISTER:
      return ProcessRequest(static_cast<const RegisterRequest&>(req));

    case CQLMessage::Opcode::ERROR: FALLTHROUGH_INTENDED;
    case CQLMessage::Opcode::READY: FALLTHROUGH_INTENDED;
    case CQLMessage::Opcode::AUTHENTICATE: FALLTHROUGH_INTENDED;
    case CQLMessage::Opcode::SUPPORTED: FALLTHROUGH_INTENDED;
    case CQLMessage::Opcode::RESULT: FALLTHROUGH_INTENDED;
    case CQLMessage::Opcode::EVENT: FALLTHROUGH_INTENDED;
    case CQLMessage::Opcode::AUTH_CHALLENGE: FALLTHROUGH_INTENDED;
    case CQLMessage::Opcode::AUTH_SUCCESS:
      break;
  }

  LOG(FATAL) << "Invalid CQL request: opcode = " << static_cast<int>(req.opcode());
  return nullptr;
}

CQLResponse* CQLProcessor::ProcessRequest(const OptionsRequest& req) {
  return new SupportedResponse(req, &kSupportedOptions);
}

CQLResponse* CQLProcessor::ProcessRequest(const StartupRequest& req) {
  for (const auto& option : req.options()) {
    const auto& name = option.first;
    const auto& value = option.second;
    const auto it = kSupportedOptions.find(name);
    if (it == kSupportedOptions.end() ||
        std::find(it->second.begin(), it->second.end(), value) == it->second.end()) {
      return new ErrorResponse(
          req, ErrorResponse::Code::PROTOCOL_ERROR,
          Substitute("Unsupported option $0 = $1", name, value));
    }
    if (name == CQLMessage::kCompressionOption) {
      auto& context = static_cast<CQLConnectionContext&>(call_->connection()->context());
      if (value == CQLMessage::kLZ4Compression) {
        context.set_compression_scheme(CQLMessage::CompressionScheme::LZ4);
      } else if (value == CQLMessage::kSnappyCompression) {
        context.set_compression_scheme(CQLMessage::CompressionScheme::SNAPPY);
      } else {
        return new ErrorResponse(
            req, ErrorResponse::Code::PROTOCOL_ERROR,
            Substitute("Unsupported compression scheme $0", value));
      }
    }
  }
  if (FLAGS_use_cassandra_authentication) {
    return new AuthenticateResponse(req, kCassandraPasswordAuthenticator);
  } else {
    return new ReadyResponse(req);
  }
}

CQLResponse* CQLProcessor::ProcessRequest(const PrepareRequest& req) {
  VLOG(1) << "PREPARE " << req.query();
  const CQLMessage::QueryId query_id = CQLStatement::GetQueryId(
      ql_env_.CurrentKeyspace(), req.query());
  // To prevent multiple clients from preparing the same new statement in parallel and trying to
  // cache the same statement (a typical "login storm" scenario), each caller will try to allocate
  // the statement in the cached statement first. If it already exists, the existing one will be
  // returned instead. Then, each client will try to prepare the statement. The first one will do
  // the actual prepare while the rest wait. As the rest do the prepare afterwards, the statement
  // is already prepared so it will be an no-op (see Statement::Prepare).
  shared_ptr<CQLStatement> stmt = service_impl_->AllocatePreparedStatement(
      query_id, ql_env_.CurrentKeyspace(), req.query());
  PreparedResult::UniPtr result;
  const Status s = stmt->Prepare(this, service_impl_->prepared_stmts_mem_tracker(), &result);
  if (!s.ok()) {
    service_impl_->DeletePreparedStatement(stmt);
    return ProcessResult(s);
  }

  return (result != nullptr) ? new PreparedResultResponse(req, query_id, *result)
                             : new PreparedResultResponse(req, query_id);
}

CQLResponse* CQLProcessor::ProcessRequest(const ExecuteRequest& req) {
  VLOG(1) << "EXECUTE " << b2a_hex(req.query_id());
  const shared_ptr<const CQLStatement> stmt = GetPreparedStatement(req.query_id());
  if (stmt == nullptr) {
    unprepared_id_ = req.query_id();
    StatementExecuted(ErrorStatus(ErrorCode::UNPREPARED_STATEMENT));
  } else {
    Status s = stmt->ExecuteAsync(this, req.params(), statement_executed_cb_);
    if (PREDICT_FALSE(!s.ok())) {
      StatementExecuted(s);
    }
  }
  return nullptr;
}

CQLResponse* CQLProcessor::ProcessRequest(const QueryRequest& req) {
  VLOG(1) << "QUERY " << req.query();
  RunAsync(req.query(), req.params(), statement_executed_cb_);
  return nullptr;
}

CQLResponse* CQLProcessor::ProcessRequest(const BatchRequest& req) {
  VLOG(1) << "BATCH " << req.queries().size();

  int retry_count = retry_count_; // Save current retry count.

  BeginBatch(statement_executed_cb_);

  for (const BatchRequest::Query& query : req.queries()) {

    if (query.is_prepared) {

      VLOG(1) << "BATCH EXECUTE " << b2a_hex(query.query_id);
      const shared_ptr<const CQLStatement> stmt = GetPreparedStatement(query.query_id);
      if (stmt == nullptr) {
        unprepared_id_ = query.query_id;
        StatementExecuted(ErrorStatus(ErrorCode::UNPREPARED_STATEMENT));
      } else {
        Status s = stmt->ExecuteBatch(this, query.params);
        if (PREDICT_FALSE(!s.ok())) {
          StatementExecuted(s);
        }
      }

    } else {

      VLOG(1) << "BATCH QUERY " << query.query;
      ql::ParseTree::UniPtr parse_tree;
      RunBatch(query.query, query.params, &parse_tree, retry_count > 0);
      parse_trees_.insert(std::move(parse_tree));

    }

    // If an error occurs while a statement is queued in the batch above, our StatementExecuted
    // callback will be called synchronously under us, which can either trigger a recursive retry
    // or return an error response & end the call. If that has happened, just exit.
    if (retry_count_ > retry_count || call_ == nullptr) {
      return nullptr;
    }
  }

  // Apply the batch that flushes the buffered operations. Our StatementExecuted callback will be
  // called asynchronously to process the result when the operations complete.
  ApplyBatch();

  return nullptr;
}

CQLResponse* CQLProcessor::ProcessRequest(const AuthResponseRequest& req) {
  const auto& params = req.params();
  shared_ptr<Statement> stmt = service_impl_->GetAuthPreparedStatement();
  if (!stmt->Prepare(this).ok()) {
    return new ErrorResponse(
        req, ErrorResponse::Code::SERVER_ERROR,
        "Could not prepare statement for querying user " + params.username);
  }
  if (!stmt->ExecuteAsync(this, params, statement_executed_cb_).ok()) {
    LOG(ERROR) << "Could not execute prepared statement to fetch login info!";
    return new ErrorResponse(
        req, ErrorResponse::Code::SERVER_ERROR,
        "Could not execute prepared statement for querying roles for user " + params.username);
  }
  return nullptr;
}

CQLResponse* CQLProcessor::ProcessRequest(const RegisterRequest& req) {
  return new ReadyResponse(req);
}

shared_ptr<const CQLStatement> CQLProcessor::GetPreparedStatement(const CQLMessage::QueryId& id) {
  shared_ptr<const CQLStatement> stmt = service_impl_->GetPreparedStatement(id);
  if (stmt != nullptr) {
    stmt->clear_reparsed();
    stmts_.insert(stmt);
  }
  return stmt;
}

void CQLProcessor::StatementExecuted(const Status& s,
                                     const ql::ExecutedResult::SharedPtr& result) {
  unique_ptr<CQLResponse> response(ProcessResult(s, result));
  if (response != nullptr) {
    SendResponse(*response);
  }
}

CQLResponse* CQLProcessor::ProcessResult(Status s, const ExecutedResult::SharedPtr& result) {
  if (!s.ok()) {
    // Abort batch if it is being executed.
    if (request_->opcode() == CQLMessage::Opcode::BATCH) {
      AbortBatch();
    }
    if (s.IsQLError()) {
      ErrorCode ql_errcode = GetErrorCode(s);
      if (ql_errcode == ErrorCode::UNPREPARED_STATEMENT ||
          ql_errcode == ErrorCode::STALE_METADATA) {
        // Delete all stale prepared statements from our cache. Since CQL protocol allows only one
        // unprepared query id to be returned, we will return just the last unprepared / stale one
        // we found.
        for (auto stmt : stmts_) {
          if (stmt->stale()) {
            service_impl_->DeletePreparedStatement(stmt);
          }
          if (stmt->unprepared() || stmt->stale()) {
            unprepared_id_ = stmt->query_id();
          }
        }
        if (!unprepared_id_.empty()) {
          return new UnpreparedErrorResponse(*request_, unprepared_id_);
        }
        // When no unprepared_id is found, it means all statements we executed were queries
        // (non-prepared statements). In that case, just retry the request (once only).
        if (++retry_count_ == 1) {
          return ProcessRequest(*request_);
        }
        return new ErrorResponse(*request_, ErrorResponse::Code::INVALID,
                                 "Query failed to execute due to stale metadata cache");
      } else if (ql_errcode < ErrorCode::SUCCESS) {
        if (ql_errcode > ErrorCode::LIMITATION_ERROR) {
          // System errors, internal errors, or crashes.
          return new ErrorResponse(*request_, ErrorResponse::Code::SERVER_ERROR, s.ToUserMessage());
        } else if (ql_errcode > ErrorCode::SEM_ERROR) {
          // Limitation, lexical, or parsing errors.
          return new ErrorResponse(*request_, ErrorResponse::Code::SYNTAX_ERROR, s.ToUserMessage());
        } else {
          // Semantic or execution errors.
          return new ErrorResponse(*request_, ErrorResponse::Code::INVALID, s.ToUserMessage());
        }
      }

      LOG(ERROR) << "Internal error: invalid error code " << static_cast<int64_t>(GetErrorCode(s));
      return new ErrorResponse(*request_, ErrorResponse::Code::SERVER_ERROR, "Invalid error code");
    }
    return new ErrorResponse(*request_, ErrorResponse::Code::SERVER_ERROR, s.ToUserMessage());
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
      if (request_->opcode() != CQLMessage::Opcode::AUTH_RESPONSE) {
        cql_metrics_->ql_response_size_bytes_->Increment(rows_result->rows_data().size());
      }
      switch (request_->opcode()) {
        case CQLMessage::Opcode::EXECUTE:
          return new RowsResultResponse(down_cast<const ExecuteRequest&>(*request_), rows_result);
        case CQLMessage::Opcode::QUERY:
          return new RowsResultResponse(down_cast<const QueryRequest&>(*request_), rows_result);
        case CQLMessage::Opcode::AUTH_RESPONSE:
          {
            const auto& req = down_cast<const AuthResponseRequest&>(*request_);
            const auto& params = req.params();
            const auto row_block = rows_result->GetRowBlock();
            if (row_block->row_count() != 1) {
              return new ErrorResponse(*request_, ErrorResponse::Code::SERVER_ERROR,
                  "Could not get data for " + params.username);
            } else {
              const auto& row = row_block->row(0);
              const auto& schema = row_block->schema();
              const auto& saved_hash =
                  row.column(schema.find_column(kRoleColumnNameSaltedHash)).string_value();
              if (bcrypt_checkpw(params.password.c_str(), saved_hash.c_str())) {
                return new ErrorResponse(*request_, ErrorResponse::Code::BAD_CREDENTIALS,
                    "Invalid password for " + params.username);
              } else {
                call_->ql_session()->set_current_role_name(params.username);
                return new AuthSuccessResponse(*request_, "" /* this does not matter" */);
              }
            }
          }
          break;
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
