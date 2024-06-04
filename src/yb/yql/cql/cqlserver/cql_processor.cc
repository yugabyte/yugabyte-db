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

#include <ldap.h>

#include <boost/algorithm/string.hpp>

#include "yb/qlexpr/ql_rowblock.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/rpc/connection.h"
#include "yb/rpc/messenger.h"

#include "yb/util/crypt.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/trace.h"

#include "yb/yql/cql/cqlserver/cql_service.h"
#include "yb/yql/cql/ql/util/errcodes.h"

using namespace std::literals;

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

METRIC_DEFINE_gauge_int64(server, cql_processors_alive,
                          "Number of alive CQL Processors.",
                          yb::MetricUnit::kUnits,
                          "Number of alive CQL Processors.");

METRIC_DEFINE_counter(server, cql_processors_created,
                      "Number of created CQL Processors.",
                      yb::MetricUnit::kUnits,
                      "Number of created CQL Processors.");

METRIC_DEFINE_gauge_int64(server, cql_parsers_alive,
                          "Number of alive CQL Parsers.",
                          yb::MetricUnit::kUnits,
                          "Number of alive CQL Parsers.");

METRIC_DEFINE_counter(server, cql_parsers_created,
                      "Number of created CQL Parsers.",
                      yb::MetricUnit::kUnits,
                      "Number of created CQL Parsers.");

DECLARE_bool(use_cassandra_authentication);
DECLARE_bool(ycql_cache_login_info);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_bool(ycql_enable_stat_statements);

DEFINE_RUNTIME_bool(ycql_enable_tracing_flag, true,
    "If enabled, setting TRACING ON in cqlsh will cause "
    "the server to enable tracing for the requested RPCs and print them. Use this as a safety flag "
    "to disable tracing if an errant application has TRACING enabled by mistake.");

// LDAP specific flags
DEFINE_UNKNOWN_bool(ycql_use_ldap, false, "Use LDAP for user logins");
DEFINE_UNKNOWN_string(ycql_ldap_users_to_skip_csv, "",
    "Users that are authenticated via the local password"
    " check instead of LDAP (if ycql_use_ldap=true). This is a comma separated list");
TAG_FLAG(ycql_ldap_users_to_skip_csv, sensitive_info);
DEFINE_UNKNOWN_string(ycql_ldap_server, "", "LDAP server of the form <scheme>://<ip>:<port>");
DEFINE_UNKNOWN_bool(ycql_ldap_tls, false, "Connect to LDAP server using TLS encryption.");

// LDAP flags for simple bind mode
DEFINE_UNKNOWN_string(ycql_ldap_user_prefix, "",
    "String used for prepending the user name when forming "
    "the DN for binding to the LDAP server");
DEFINE_UNKNOWN_string(ycql_ldap_user_suffix, "",
    "String used for appending the user name when forming the "
    "DN for binding to the LDAP Server.");

// Flags for LDAP search + bind mode
DEFINE_UNKNOWN_string(ycql_ldap_base_dn, "",
    "Specifies the base directory to begin the user name search");
DEFINE_UNKNOWN_string(ycql_ldap_bind_dn, "",
    "Specifies the username to perform the initial search when "
    "doing search + bind authentication");
TAG_FLAG(ycql_ldap_bind_dn, sensitive_info);
DEFINE_UNKNOWN_string(ycql_ldap_bind_passwd, "",
    "Password for username being used to perform the initial "
    "search when doing search + bind authentication");
TAG_FLAG(ycql_ldap_bind_passwd, sensitive_info);
DEFINE_UNKNOWN_string(ycql_ldap_search_attribute, "",
    "Attribute to match against the username in the search when doing search + bind "
    "authentication. If no attribute is specified, the uid attribute is used.");
DEFINE_UNKNOWN_string(ycql_ldap_search_filter, "",
    "The search filter to use when doing search + bind "
    "authentication.");

namespace yb {
namespace cqlserver {

constexpr const char* const kCassandraPasswordAuthenticator =
    "org.apache.cassandra.auth.PasswordAuthenticator";

extern const char* const kRoleColumnNameSaltedHash;
extern const char* const kRoleColumnNameCanLogin;

using namespace yb::ql; // NOLINT

using std::make_unique;
using std::shared_ptr;
using std::unique_ptr;
using std::ostream;
using std::string;

using ql::ExecutedResult;
using ql::PreparedResult;
using ql::RowsResult;
using ql::SetKeyspaceResult;
using ql::SchemaChangeResult;
using ql::ParseTree;
using ql::Statement;
using ql::StatementBatch;
using ql::ErrorCode;
using ql::GetErrorCode;

using ql::audit::IsPrepare;
using ql::audit::ErrorIsFormatted;

using strings::Substitute;

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
  cql_processors_alive_ = METRIC_cql_processors_alive.Instantiate(metric_entity, 0);
  cql_processors_created_ = METRIC_cql_processors_created.Instantiate(metric_entity);
  parsers_alive_ = METRIC_cql_parsers_alive.Instantiate(metric_entity, 0);
  parsers_created_ = METRIC_cql_parsers_created.Instantiate(metric_entity);
}

//------------------------------------------------------------------------------------------------
CQLProcessor::CQLProcessor(CQLServiceImpl* service_impl, const CQLProcessorListPos& pos)
    : QLProcessor(service_impl->client(), service_impl->metadata_cache(),
                  service_impl->cql_metrics().get(),
                  &service_impl->parser_pool(),
                  service_impl->clock(),
                  std::bind(&CQLServiceImpl::TransactionPool, service_impl)),
      service_impl_(service_impl),
      cql_metrics_(service_impl->cql_metrics()),
      pos_(pos),
      statement_executed_cb_(Bind(&CQLProcessor::StatementExecuted, Unretained(this))),
      consumption_(service_impl->processors_mem_tracker(), sizeof(*this)) {
  IncrementCounter(cql_metrics_->cql_processors_created_);
  IncrementGauge(cql_metrics_->cql_processors_alive_);
}

CQLProcessor::~CQLProcessor() {
  DecrementGauge(cql_metrics_->cql_processors_alive_);
}

void CQLProcessor::Shutdown() {
  executor_.Shutdown();
  auto call = std::move(call_);
  if (call) {
    call->RespondFailure(
        rpc::ErrorStatusPB::FATAL_SERVER_SHUTTING_DOWN, STATUS(Aborted, "Aborted"));
  }
}

void CQLProcessor::ProcessCall(rpc::InboundCallPtr call) {
  call_ = std::dynamic_pointer_cast<CQLInboundCall>(std::move(call));
  call_->SetRpcMethodMetrics(cql_metrics_->rpc_method_metrics_);
  is_rescheduled_.store(IsRescheduled::kFalse, std::memory_order_release);
  audit_logger_.SetConnection(call_->connection());
  unique_ptr<CQLRequest> request;
  unique_ptr<CQLResponse> response;

  ADOPT_TRACE(call_->trace());
  if (const auto& wait_state = ash::WaitStateInfo::CurrentWaitState()) {
    wait_state->set_rpc_request_id(call_->instance_id());
    wait_state->UpdateAuxInfo({.method{"CQLProcessCall"}});
  }
  // Parse the CQL request. If the parser failed, it sets the error message in response.
  parse_begin_ = MonoTime::Now();
  const auto& context = static_cast<const CQLConnectionContext&>(call_->connection()->context());
  const auto compression_scheme = context.compression_scheme();
  if (!CQLRequest::ParseRequest(
          call_->serialized_request(), compression_scheme, &request, &response,
          service_impl_->requests_mem_tracker())) {
    cql_metrics_->num_errors_parsing_cql_->Increment();
    PrepareAndSendResponse(response);
    return;
  }

  execute_begin_ = MonoTime::Now();
  cql_metrics_->time_to_parse_cql_wrapper_->Increment(
      execute_begin_.GetDeltaSince(parse_begin_).ToMicroseconds());

  // Execute the request (perhaps asynchronously).
  SetCurrentSession(call_->ql_session());
  request_ = std::move(request);
  if (GetAtomicFlag(&FLAGS_ycql_enable_tracing_flag) && request_->trace_requested()) {
    call_->EnsureTraceCreated();
    call_->trace()->set_end_to_end_traces_requested(true);
  }
  call_->SetRequest(request_, service_impl_);
  retry_count_ = 0;
  response = ProcessRequest(*request_);
  PrepareAndSendResponse(response);
}

void CQLProcessor::Release() {
  call_ = nullptr;
  request_ = nullptr;
  stmts_.clear();
  parse_trees_.clear();
  SetCurrentSession(nullptr);
  is_rescheduled_.store(IsRescheduled::kFalse, std::memory_order_release);
  audit_logger_.SetConnection(nullptr);
  service_impl_->ReturnProcessor(pos_);
}

void CQLProcessor::PrepareAndSendResponse(const unique_ptr<CQLResponse>& response) {
  if (response) {
    const CQLConnectionContext& context =
        static_cast<const CQLConnectionContext&>(call_->connection()->context());
    response->set_registered_events(context.registered_events());
    response->set_rpc_queue_position(call_->GetRpcQueuePosition());
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
  call_->RespondSuccess(RefCntBuffer(msg));

  MonoTime response_done = MonoTime::Now();
  cql_metrics_->time_to_process_request_->Increment(
      response_done.GetDeltaSince(parse_begin_).ToMicroseconds());
  if (request_ != nullptr) {
    cql_metrics_->time_to_execute_cql_request_->Increment(
        response_begin.GetDeltaSince(execute_begin_).ToMicroseconds());
  }
  cql_metrics_->time_to_queue_cql_response_->Increment(
      response_done.GetDeltaSince(response_begin).ToMicroseconds());

  if (FLAGS_ycql_enable_stat_statements) {
    const IsPrepare is_prepare(request_ && request_->opcode() == ql::CQLMessage::Opcode::EXECUTE);
    const string query_id = (is_prepare ? GetPrepQueryId() : GetUnprepQueryId());
    if (!query_id.empty()) {
      service_impl_->UpdateStmtCounters(
          query_id, response_done.GetDeltaSince(execute_begin_).ToSeconds()*1000.,
          is_prepare);
    }
  }

  Release();
}

bool CQLProcessor::CheckAuthentication(const CQLRequest& req) const {
  return call_->ql_session()->is_user_authenticated() ||
      // CQL requests which do not need authorization.
      req.opcode() == CQLMessage::Opcode::STARTUP ||
      // Some drivers issue OPTIONS prior to AUTHENTICATE
      req.opcode() == CQLMessage::Opcode::OPTIONS ||
      req.opcode() == CQLMessage::Opcode::AUTH_RESPONSE;
}

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const CQLRequest& req) {
  if (FLAGS_use_cassandra_authentication && !CheckAuthentication(req)) {
    LOG(ERROR) << "Could not execute statement by not authenticated user!";
    return make_unique<ErrorResponse>(
        req, ErrorResponse::Code::SERVER_ERROR,
        "Could not execute statement by not authenticated user");
  }

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

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const OptionsRequest& req) {
  return make_unique<SupportedResponse>(req, &kSupportedOptions);
}

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const StartupRequest& req) {
  for (const auto& option : req.options()) {
    const auto& name = option.first;
    const auto& value = option.second;
    const auto it = kSupportedOptions.find(name);
    if (it == kSupportedOptions.end() ||
        std::find(it->second.begin(), it->second.end(), value) == it->second.end()) {
      YB_LOG_EVERY_N_SECS(WARNING, 60) << Format("Unsupported driver option $0 = $1", name, value);
    }
    if (name == CQLMessage::kCompressionOption) {
      auto& context = static_cast<CQLConnectionContext&>(call_->connection()->context());
      if (value == CQLMessage::kLZ4Compression) {
        context.set_compression_scheme(CQLMessage::CompressionScheme::kLz4);
      } else if (value == CQLMessage::kSnappyCompression) {
        context.set_compression_scheme(CQLMessage::CompressionScheme::kSnappy);
      } else {
        return make_unique<ErrorResponse>(
            req, ErrorResponse::Code::PROTOCOL_ERROR,
            Substitute("Unsupported compression scheme $0", value));
      }
    }
  }
  if (FLAGS_use_cassandra_authentication) {
    return make_unique<AuthenticateResponse>(req, kCassandraPasswordAuthenticator);
  } else {
    return make_unique<ReadyResponse>(req);
  }
}

void CQLProcessor::UpdateAshQueryId(const CQLMessage::QueryId& query_id) {
  if (const auto& wait_state = ash::WaitStateInfo::CurrentWaitState()) {
    wait_state->set_query_id(CQLMessage::QueryIdAsUint64(query_id));
  }
}

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const PrepareRequest& req) {
  VLOG(1) << "PREPARE " << req.query();
  const CQLMessage::QueryId query_id = CQLStatement::GetQueryId(
      ql_env_.CurrentKeyspace(), req.query());
  VLOG(1) << "Generated Query Id = " << query_id;
  UpdateAshQueryId(query_id);
  // To prevent multiple clients from preparing the same new statement in parallel and trying to
  // cache the same statement (a typical "login storm" scenario), each caller will try to allocate
  // the statement in the cached statement first. If it already exists, the existing one will be
  // returned instead. Then, each client will try to prepare the statement. The first one will do
  // the actual prepare while the rest wait. As the rest do the prepare afterwards, the statement
  // is already prepared so it will be an no-op (see Statement::Prepare).
  shared_ptr<CQLStatement> stmt = service_impl_->AllocateStatement(
      query_id, req.query(), &ql_env_, IsPrepare::kTrue);
  PreparedResult::UniPtr result;
  Status s = stmt->Prepare(this, service_impl_->stmts_mem_tracker(),
                           false /* internal */, &result);

  if (s.ok()) {
    auto pt_result = stmt->GetParseTree();
    if (pt_result.ok()) {
      s = audit_logger_.LogStatement(pt_result->root().get(), req.query(),
                                     IsPrepare::kTrue);
    } else {
      s = pt_result.status();
    }
  } else {
    WARN_NOT_OK(audit_logger_.LogStatementError(req.query(), s,
                                                ErrorIsFormatted::kTrue),
                "Failed to log an audit record");
  }

  if (!s.ok()) {
    service_impl_->DeleteStatement(stmt, IsPrepare::kTrue);
    return ProcessError(s, stmt->query_id());
  }

  return (result != nullptr) ? make_unique<PreparedResultResponse>(req, query_id, *result)
                             : make_unique<PreparedResultResponse>(req, query_id);
}

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const ExecuteRequest& req) {
  VLOG(1) << "EXECUTE " << b2a_hex(req.query_id());
  UpdateAshQueryId(req.query_id());
  auto stmt_res = GetPreparedStatement(req.query_id(), req.params().schema_version());
  if (!stmt_res.ok()) {
    return ProcessError(stmt_res.status(), req.query_id());
  }

  LOG_IF(DFATAL, *stmt_res == nullptr) << "Null statement";
  const Status s = (*stmt_res)->ExecuteAsync(this, req.params(), statement_executed_cb_);
  return s.ok() ? nullptr : ProcessError(s, (*stmt_res)->query_id());
}

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const QueryRequest& req) {
  VLOG(1) << "QUERY " << req.query();
  if (service_impl_->system_cache() != nullptr) {
    auto cached_response = service_impl_->system_cache()->Lookup(req.query());
    if (cached_response) {
      VLOG(1) << "Using cached response for " << req.query();
      statement_executed_cb_.Run(
          Status::OK(),
          std::static_pointer_cast<ExecutedResult>(*cached_response));
      return nullptr;
    }
  }
  const CQLMessage::QueryId query_id =
      CQLStatement::GetQueryId(ql_env_.CurrentKeyspace(), req.query());
  UpdateAshQueryId(query_id);
  // Allocates space to unprepared statements in the cache.
  const shared_ptr<CQLStatement> stmt = service_impl_->AllocateStatement(
      query_id, req.query(), &ql_env_, IsPrepare::kFalse);

  const auto op_code = RunAsync(req.query(), req.params(), statement_executed_cb_);

  // For queries like "USE keyspace_name", the query_id of the query changes after the execution
  // of the query as query_id is generated by hashing its keyspace and the query text. Since
  // after the execution the keyspace changes, consequently the query id changes. So its entry in
  // the unprepared_stmts_map_ becomes stale. So, the corresponding entry is deleted from the cache.
  if(stmt && op_code == TreeNodeOpcode::kPTUseKeyspace) {
    service_impl_->DeleteStatement(stmt, IsPrepare::kFalse);
  }
  return nullptr;
}

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const BatchRequest& req) {
  VLOG(1) << "BATCH " << req.queries().size();

  StatementBatch batch;
  batch.reserve(req.queries().size());

  // If no errors happen, batch request started here will be ended by the executor.
  Status s = audit_logger_.StartBatchRequest(req.queries().size(),
                                             is_rescheduled_.load(std::memory_order_acquire));
  if (PREDICT_FALSE(!s.ok())) {
    return ProcessError(s);
  }

  unique_ptr<CQLResponse> result;
  // For each query in the batch, look up the query id if it is a prepared statement, or prepare the
  // query if it is not prepared. Then execute the parse trees with the parameters.
  for (const BatchRequest::Query& query : req.queries()) {
    if (query.is_prepared) {
      VLOG(1) << "BATCH EXECUTE " << b2a_hex(query.query_id);
      auto stmt_res = GetPreparedStatement(query.query_id, query.params.schema_version());
      if (!stmt_res.ok()) {
        result = ProcessError(stmt_res.status(), query.query_id);
        break;
      }

      LOG_IF(DFATAL, *stmt_res == nullptr) << "Null statement";
      const Result<const ParseTree&> parse_tree = (*stmt_res)->GetParseTree();
      if (!parse_tree) {
        result = ProcessError(parse_tree.status(), query.query_id);
        break;
      }
      batch.emplace_back(*parse_tree, query.params);
    } else {
      VLOG(1) << "BATCH QUERY " << query.query;
      ParseTree::UniPtr parse_tree;
      s = Prepare(query.query, &parse_tree);
      if (PREDICT_FALSE(!s.ok())) {
        result = ProcessError(s);
        break;
      }
      batch.emplace_back(*parse_tree, query.params);
      parse_trees_.insert(std::move(parse_tree));
    }
  }

  if (result) {
    s = audit_logger_.EndBatchRequest();
    // Otherwise, batch request will be ended by the executor or when processing an error.
    if (PREDICT_FALSE(!s.ok())) {
      result = ProcessError(s);
    }
    return result;
  }

  ExecuteAsync(batch, statement_executed_cb_);

  return nullptr;
}

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const AuthResponseRequest& req) {
  const auto& params = req.params();
  if (FLAGS_ycql_cache_login_info) {
    auto salted_hash_result = ql_env_.RoleSaltedHash(params.username);
    if (salted_hash_result.ok()) {
      auto can_login_result = ql_env_.RoleCanLogin(params.username);
      if (can_login_result.ok()) {
        unique_ptr<CQLResponse> response =
          ProcessAuthResult(*salted_hash_result, *can_login_result);
        VLOG(1) << "Used cached authentication";
        Status s = audit_logger_.LogAuthResponse(*response);
        return response;
      } else {
        VLOG(1) << "Unable to get can_login for user " << params.username << ": "
                  << can_login_result;
      }
    } else {
      VLOG(1) << "Unable to get salted hash for user " << params.username << ": "
                << salted_hash_result;
    }
  }
  shared_ptr<Statement> stmt = service_impl_->GetAuthPreparedStatement();
  if (!stmt->Prepare(this, nullptr /* memtracker */, true /* internal */).ok()) {
    return make_unique<ErrorResponse>(
        req, ErrorResponse::Code::SERVER_ERROR,
        "Could not prepare statement for querying user " + params.username);
  }
  if (!stmt->ExecuteAsync(this, params, statement_executed_cb_).ok()) {
    LOG(ERROR) << "Could not execute prepared statement to fetch login info!";
    return make_unique<ErrorResponse>(
        req, ErrorResponse::Code::SERVER_ERROR,
        "Could not execute prepared statement for querying roles for user " + params.username);
  }
  return nullptr;
}

unique_ptr<CQLResponse> CQLProcessor::ProcessRequest(const RegisterRequest& req) {
  CQLConnectionContext& context =
      static_cast<CQLConnectionContext&>(call_->connection()->context());
  context.add_registered_events(req.events());
  return make_unique<ReadyResponse>(req);
}

Result<shared_ptr<const CQLStatement>> CQLProcessor::GetPreparedStatement(
    const CQLMessage::QueryId& id, SchemaVersion version) {
  const shared_ptr<const CQLStatement> stmt =
      VERIFY_RESULT(service_impl_->GetPreparedStatement(id, version));
  LOG_IF(DFATAL, stmt == nullptr) << "Null statement";
  stmt->clear_reparsed();
  stmts_.insert(stmt);
  return stmt;
}

void CQLProcessor::StatementExecuted(const Status& s, const ExecutedResult::SharedPtr& result) {
  ADOPT_WAIT_STATE(call_ ? call_->wait_state() : nullptr);
  SCOPED_WAIT_STATUS(OnCpu_Active);
  unique_ptr<CQLResponse> response(s.ok() ? ProcessResult(result) : ProcessError(s));
  if (response && !s.ok()) {
    // Error response means we're not going to be transparently restarting a query.
    WARN_NOT_OK(audit_logger_.EndBatchRequest(), "Failed to end batch request");
  }
  PrepareAndSendResponse(response);
}

unique_ptr<CQLResponse> CQLProcessor::ProcessError(const Status& s,
                                                   boost::optional<CQLMessage::QueryId> query_id) {
  if (s.IsQLError()) {
    ErrorCode ql_errcode = GetErrorCode(s);
    if (ql_errcode == ErrorCode::UNPREPARED_STATEMENT ||
        ql_errcode == ErrorCode::STALE_METADATA) {
      // Delete all stale prepared statements from our cache. Since CQL protocol allows only one
      // unprepared query id to be returned, we will return just the last unprepared / stale one
      // we found.
      for (auto stmt : stmts_) {
        if (stmt->stale()) {
          service_impl_->DeleteStatement(stmt, IsPrepare::kTrue);
        }
        if (stmt->unprepared() || stmt->stale()) {
          query_id = stmt->query_id();
        }
      }
      if (query_id) {
        return make_unique<UnpreparedErrorResponse>(*request_, *query_id);
      }
      // When no unprepared query id is found, it means all statements we executed were queries
      // (non-prepared statements). In that case, just retry the request (once only). The retry
      // needs to be rescheduled in because this callback may not be executed in the RPC worker
      // thread. Also, rescheduling gives other calls a chance to execute first before we do.
      if (++retry_count_ == 1) {
        stmts_.clear();
        parse_trees_.clear();
        Reschedule(&process_request_task_.Bind(this));
        return nullptr;
      }
      return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::INVALID,
                                        "Query failed to execute due to stale metadata cache");
    } else if (ql_errcode < ErrorCode::SUCCESS) {
      if (ql_errcode == ErrorCode::UNAUTHORIZED) {
        return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::UNAUTHORIZED,
                                          s.ToUserMessage());
      } else if (ql_errcode > ErrorCode::LIMITATION_ERROR) {
        // System errors, internal errors, or crashes.
        return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::SERVER_ERROR,
                                          s.ToUserMessage());
      } else if (ql_errcode > ErrorCode::SEM_ERROR) {
        // Limitation, lexical, or parsing errors.
        return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::SYNTAX_ERROR,
                                          s.ToUserMessage());
      } else {
        // Semantic or execution errors.
        return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::INVALID,
                                          s.ToUserMessage());
      }
    }

    LOG(ERROR) << "Internal error: invalid error code " << static_cast<int64_t>(GetErrorCode(s));
    return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::SERVER_ERROR,
                                      "Invalid error code");
  } else if (s.IsNotAuthorized()) {
    return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::UNAUTHORIZED,
                                      s.ToUserMessage());
  }

  return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::SERVER_ERROR,
                                    s.ToUserMessage());
}

namespace {

struct LDAPMemoryDeleter {
  void operator()(void* ptr) const { ldap_memfree(ptr); }
};

struct LDAPMessageDeleter {
  void operator()(LDAPMessage* ptr) const { ldap_msgfree(ptr); }
};

struct LDAPDeleter {
  void operator()(LDAP* ptr) const { ldap_unbind_ext(ptr, NULL, NULL); }
};

using LDAPHolder = unique_ptr<LDAP, LDAPDeleter>;
using LDAPMessageHolder = unique_ptr<LDAPMessage, LDAPMessageDeleter>;
template<class T>
using LDAPMemoryHolder = unique_ptr<T, LDAPMemoryDeleter>;

class LDAPError {
 public:
  explicit LDAPError(int c) : code(c), ldap_(nullptr) {}
  LDAPError(int c, const LDAPHolder& l) : code(c), ldap_(&l) {}

  LDAP* GetLDAP() const { return ldap_ ? ldap_->get() : nullptr; }
  const int code;

 private:
  const LDAPHolder* ldap_;
};

/*
* Add a detail error message text to the current error if one can be
* constructed from the LDAP 'diagnostic message'.
*/
ostream& operator<<(ostream& str, const LDAPError& error) {
  str << ldap_err2string(error.code);
  auto ldap = error.GetLDAP();
  if (ldap) {
    char *message = nullptr;
    const auto rc = ldap_get_option(ldap, LDAP_OPT_DIAGNOSTIC_MESSAGE, &message);
    if (rc == LDAP_SUCCESS && message != nullptr) {
      LDAPMemoryHolder<char> holder(message);
      str << " LDAP diagnostics: " << message;
    }
  }
  return str;
}

Result<LDAPHolder> InitializeLDAPConnection(const char *uris) {
  LDAP* ldap_ptr = nullptr;
  auto r = ldap_initialize(&ldap_ptr, uris);
  if (r != LDAP_SUCCESS) {
    return STATUS_FORMAT(InternalError, "could not initialize LDAP: $0", LDAPError(r));
  }
  LDAPHolder ldap(ldap_ptr);
  VLOG(4) << "Successfully initialized LDAP struct";

  int ldapversion = LDAP_VERSION3;
  if ((r = ldap_set_option(ldap_ptr, LDAP_OPT_PROTOCOL_VERSION, &ldapversion)) != LDAP_SUCCESS) {
    return STATUS_FORMAT(InternalError, "could not set LDAP protocol version: $0",
        LDAPError(r, ldap));
  }
  VLOG(4) << "Successfully set protocol version option";

  if (FLAGS_ycql_ldap_tls && ((r = ldap_start_tls_s(ldap_ptr, NULL, NULL)) != LDAP_SUCCESS)) {
    return STATUS_FORMAT(InternalError, "could not start LDAP TLS session: $0", LDAPError(r, ldap));
  }

  return ldap;
}

/*
 * Return a newly allocated string copied from "pattern" with all
 * occurrences of the placeholder "$username" replaced with "user_name".
 */
std::string FormatSearchFilter(const std::string& pattern, const std::string& user_name) {
  return boost::replace_all_copy(pattern, "$username", user_name);
}

Result<bool> CheckLDAPAuth(const ql::AuthResponseRequest::AuthQueryParameters& params) {
  if (params.username.empty() || params.password.empty()) {
    // Refer https://datatracker.ietf.org/doc/html/rfc4513#section-6.3.1 for details. Applications
    // are required to explicitly have this check and can't rely on an LDAP server to report
    // invalid credentials error.
    return STATUS(InvalidArgument, "Empty username and/or password not allowed");
  }

  if (FLAGS_ycql_ldap_server.empty())
    return STATUS(InvalidArgument, "LDAP server not specified");

  VLOG(4) << "Attempting ldap_initialize() with " << FLAGS_ycql_ldap_server;

  const auto& uris = FLAGS_ycql_ldap_server;
  auto ldap = VERIFY_RESULT(InitializeLDAPConnection(uris.c_str()));

  int r;
  std::string fulluser;
  if (!FLAGS_ycql_ldap_base_dn.empty()) {
    if (FLAGS_ycql_ldap_bind_dn.empty() || FLAGS_ycql_ldap_bind_passwd.empty()) {
      return STATUS(InvalidArgument,
                    "Empty bind dn and/or bind password not allowed for search+bind mode");
    }

    /*
    * First perform an LDAP search to find the DN for the user we are
    * trying to log in as.
    */
    char ldap_no_attrs[sizeof(LDAP_NO_ATTRS)+1];
    strncpy(ldap_no_attrs, LDAP_NO_ATTRS, sizeof(ldap_no_attrs));
    char *attributes[] = {ldap_no_attrs, NULL};

    /*
    * Disallow any characters that we would otherwise need to escape,
    * since they aren't really reasonable in a username anyway. Allowing
    * them would make it possible to inject any kind of custom filters in
    * the LDAP filter.
    */
    for (const char& c : params.username) {
      switch (c) {
        case '*':
        case '(':
        case ')':
        case '\\':
        case '/':
          return STATUS(InvalidArgument, "invalid character in user name for LDAP authentication");
      }
    }

    /*
    * Bind with a pre-defined username/password (if available) for
    * searching. If none is specified, this turns into an anonymous bind.
    */
    struct berval cred;
    ber_str2bv(FLAGS_ycql_ldap_bind_passwd.c_str(), 0 /* len */, 0 /* duplicate */ , &cred);
    r = ldap_sasl_bind_s(ldap.get(), FLAGS_ycql_ldap_bind_dn.c_str(),
                         LDAP_SASL_SIMPLE, &cred,
                         NULL /* serverctrls */, NULL /* clientctrls */,
                         NULL /* servercredp */);
    if (r != LDAP_SUCCESS) {
      return STATUS_FORMAT(
        InvalidArgument,
        "could not perform initial LDAP bind for ldapbinddn '$0' on server '$1': $2",
        FLAGS_ycql_ldap_bind_dn, FLAGS_ycql_ldap_server, LDAPError(r, ldap));
    }

    std::string filter;
    /* Build a custom filter or a single attribute filter? */
    if (!FLAGS_ycql_ldap_search_filter.empty()) {
      filter = FormatSearchFilter(FLAGS_ycql_ldap_search_filter, params.username);
    } else if (!FLAGS_ycql_ldap_search_attribute.empty()) {
      filter = "(" + FLAGS_ycql_ldap_search_attribute + "=" + params.username + ")";
    } else {
      filter = "(uid=" + params.username + ")";
    }

    LDAPMessage *search_message;
    r = ldap_search_ext_s(ldap.get(), FLAGS_ycql_ldap_base_dn.c_str(), LDAP_SCOPE_SUBTREE,
                          filter.c_str(), attributes, 0, NULL, NULL, NULL, 0, &search_message);
    LDAPMessageHolder search_message_holder{search_message};

    if (r != LDAP_SUCCESS) {
      return STATUS_FORMAT(
          InternalError, "could not search LDAP for filter '$0' on server '$1': $2", filter,
          FLAGS_ycql_ldap_server, LDAPError(r, ldap));
    }

    auto count = ldap_count_entries(ldap.get(), search_message);
    switch(count) {
      case 0:
        return STATUS_FORMAT(
            NotFound,
            "LDAP user '$0' does not exist. "\
            "LDAP search for filter '$1' on server '$2' returned no entries.",
            params.username, filter, FLAGS_ycql_ldap_server);
      case 1:
        break;
      default:
        return STATUS_FORMAT(
            NotFound, "LDAP user '$0' is not unique, $1 entries exist.", params.username, count);
    }

    // No need to free entry pointer since it is a pointer to data in
    // search_message. Freeing search_message takes cares of it.
    auto *entry = ldap_first_entry(ldap.get(), search_message);
    char *dn = ldap_get_dn(ldap.get(), entry);
    if (dn == NULL) {
      int error;
      ldap_get_option(ldap.get(), LDAP_OPT_ERROR_NUMBER, &error);
      return STATUS_FORMAT(
          NotFound, "could not get dn for the first entry matching '$0' on server '$1': $2",
          filter, FLAGS_ycql_ldap_server, LDAPError(error, ldap));
    }
    LDAPMemoryHolder<char> dn_holder{dn};
    fulluser = dn;

    /*
    * Need to re-initialize the LDAP connection, so that we can bind to
    * it with a different username.
    */
    ldap = VERIFY_RESULT(InitializeLDAPConnection(uris.c_str()));
  } else {
    fulluser = FLAGS_ycql_ldap_user_prefix + params.username + FLAGS_ycql_ldap_user_suffix;
  }

  VLOG(4) << "Checking authentication using LDAP for user DN=" << fulluser;

  struct berval cred;
  ber_str2bv(params.password.c_str(), 0 /* len */, 0 /* duplicate */, &cred);
  r = ldap_sasl_bind_s(ldap.get(), fulluser.c_str(),
                       LDAP_SASL_SIMPLE, &cred,
                       NULL /* serverctrls */, NULL /* clientctrls */,
                       NULL /* servercredp */);
  VLOG(4) << "ldap_sasl_bind_s return value =" << r;

  if (r != LDAP_SUCCESS) {
    std::ostringstream str;
    str << "LDAP login failed for user '" << fulluser << "' on server '"
        << FLAGS_ycql_ldap_server << "': " << LDAPError(r, ldap);
    auto error_msg = str.str();
    if (r == LDAP_INVALID_CREDENTIALS) {
      LOG(ERROR) << error_msg;
      return false;
    }

    return STATUS(InternalError, error_msg);
  }

  return true;
}

static bool UserIn(const std::string& username, const std::string& users_to_skip) {
  size_t comma_index = 0;
  size_t prev_comma_index = -1;

  // TODO(Piyush): Store a static list of usernames from csv instead of traversing each time.
  while ((comma_index = users_to_skip.find(",", prev_comma_index + 1)) != std::string::npos) {
    if (users_to_skip.substr(prev_comma_index + 1,
                             comma_index - (prev_comma_index + 1)) == username)
      return true;
    VLOG(2) << "Check " << username << " with "
            << users_to_skip.substr(prev_comma_index + 1, comma_index - (prev_comma_index + 1));
    prev_comma_index = comma_index;
  }
  VLOG(2) << "Check " << username << " with "
          << users_to_skip.substr(
              prev_comma_index + 1, users_to_skip.size() - (prev_comma_index + 1));
  return users_to_skip.substr(prev_comma_index + 1, users_to_skip.size() - (prev_comma_index + 1))
    == username;
}

} // namespace

unique_ptr<CQLResponse> CQLProcessor::ProcessAuthResult(const string& saved_hash, bool can_login) {
  const auto& req = down_cast<const AuthResponseRequest&>(*request_);
  const auto& params = req.params();
  unique_ptr<CQLResponse> response = nullptr;
  bool authenticated = false;

  if (FLAGS_ycql_use_ldap && !UserIn(params.username, FLAGS_ycql_ldap_users_to_skip_csv)) {
    Result<bool> ldap_auth_result = CheckLDAPAuth(req.params());
    if (!ldap_auth_result.ok()) {
      return make_unique<ErrorResponse>(
          *request_, ErrorResponse::Code::SERVER_ERROR,
          "Failed to authenticate using LDAP: " + ldap_auth_result.status().ToString());
    } else if (!*ldap_auth_result) {
      response = make_unique<ErrorResponse>(
          *request_, ErrorResponse::Code::BAD_CREDENTIALS,
          "Failed to authenticate using LDAP: Provided username '" + params.username +
          "' and/or password are incorrect");
    } else {
      authenticated = true;
      call_->ql_session()->set_current_role_name(params.username);
      response = make_unique<AuthSuccessResponse>(*request_,
                                                  "" /* this does not matter */);
    }
  } else if (saved_hash.empty()) {
    // Username doesn't have a password, but one is required for authentication. Return an error.
    response = make_unique<ErrorResponse>(
        *request_, ErrorResponse::Code::BAD_CREDENTIALS,
        "Provided username '" + params.username + "' and/or password are incorrect");
  } else {
    if (!service_impl_->CheckPassword(params.password, saved_hash)) {
      response = make_unique<ErrorResponse>(
          *request_, ErrorResponse::Code::BAD_CREDENTIALS,
          "Provided username '" + params.username + "' and/or password are incorrect");
    } else if (!can_login) {
      response = make_unique<ErrorResponse>(
          *request_, ErrorResponse::Code::BAD_CREDENTIALS,
          params.username + " is not permitted to log in");
    } else {
      call_->ql_session()->set_current_role_name(params.username);
      response = make_unique<AuthSuccessResponse>(*request_, "" /* this does not matter */);
      authenticated = true;
    }
  }
  call_->ql_session()->set_user_authenticated(authenticated);
  return response;
}

unique_ptr<CQLResponse> CQLProcessor::ProcessResult(const ExecutedResult::SharedPtr& result) {
  if (result == nullptr) {
    return make_unique<VoidResultResponse>(*request_);
  }
  switch (result->type()) {
    case ExecutedResult::Type::SET_KEYSPACE: {
      const auto& set_keyspace_result = static_cast<const SetKeyspaceResult&>(*result);
      return make_unique<SetKeyspaceResultResponse>(*request_, set_keyspace_result);
    }
    case ExecutedResult::Type::ROWS: {
      const RowsResult::SharedPtr& rows_result = std::static_pointer_cast<RowsResult>(result);
      if (request_->opcode() != CQLMessage::Opcode::AUTH_RESPONSE) {
        cql_metrics_->ql_response_size_bytes_->Increment(rows_result->rows_data().size());
      }
      switch (request_->opcode()) {
        case CQLMessage::Opcode::EXECUTE:
          return make_unique<RowsResultResponse>(down_cast<const ExecuteRequest&>(*request_),
                                                 rows_result);
        case CQLMessage::Opcode::QUERY:
          return make_unique<RowsResultResponse>(down_cast<const QueryRequest&>(*request_),
                                                 rows_result);
        case CQLMessage::Opcode::BATCH:
          return make_unique<RowsResultResponse>(down_cast<const BatchRequest&>(*request_),
                                                 rows_result);

        case CQLMessage::Opcode::AUTH_RESPONSE: {
          const auto& req = down_cast<const AuthResponseRequest&>(*request_);
          const auto& params = req.params();
          const auto row_block = rows_result->GetRowBlock();
          unique_ptr<CQLResponse> response = nullptr;
          if (row_block->row_count() != 1) {
            response = make_unique<ErrorResponse>(
                *request_, ErrorResponse::Code::BAD_CREDENTIALS,
                "Provided username '" + params.username + "' and/or password are incorrect");
          } else {
            const auto& row = row_block->row(0);
            const auto& schema = row_block->schema();

            const QLValue& salted_hash_value =
                row.column(schema.find_column(kRoleColumnNameSaltedHash));
            const auto& can_login =
                row.column(schema.find_column(kRoleColumnNameCanLogin)).bool_value();
            // Returning empty string is fine since it would error out as expected,
            // if the hash is empty
            const string saved_hash = salted_hash_value.IsNull() ?
                "" : salted_hash_value.string_value();
            response = ProcessAuthResult(saved_hash, can_login);
          }
          Status s = audit_logger_.LogAuthResponse(*response);
          if (!s.ok()) {
            return make_unique<ErrorResponse>(*request_, ErrorResponse::Code::SERVER_ERROR,
                                              "Failed to write an audit log record");
          }

          return response;
        }
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
        case CQLMessage::Opcode::AUTH_CHALLENGE: FALLTHROUGH_INTENDED;
        case CQLMessage::Opcode::AUTH_SUCCESS:
          break;
        // default: fall through.
      }
      LOG(FATAL) << "Internal error: not a request that returns result "
                 << static_cast<int>(request_->opcode());
      break;
    }
    case ExecutedResult::Type::SCHEMA_CHANGE: {
      const auto& schema_change_result = static_cast<const SchemaChangeResult&>(*result);
      return make_unique<SchemaChangeResultResponse>(*request_, schema_change_result);
    }

    // default: fall through.
  }
  LOG(ERROR) << "Internal error: unknown result type " << static_cast<int>(result->type());
  return make_unique<ErrorResponse>(
      *request_, ErrorResponse::Code::SERVER_ERROR, "Internal error: unknown result type");
}

bool CQLProcessor::NeedReschedule() {
  auto messenger = service_impl_->messenger();
  if (!messenger) {
    return false;
  }
  return !messenger->ThreadPool(rpc::ServicePriority::kNormal).OwnsThisThread();
}

void CQLProcessor::Reschedule(rpc::ThreadPoolTask* task) {
  is_rescheduled_.store(IsRescheduled::kTrue, std::memory_order_release);
  auto messenger = service_impl_->messenger();
  DCHECK(messenger != nullptr) << "No messenger to reschedule CQL call";
  messenger->ThreadPool(rpc::ServicePriority::kNormal).Enqueue(task);
}

CoarseTimePoint CQLProcessor::GetDeadline() const {
  return call_ ? call_->GetClientDeadline()
               : CoarseMonoClock::now() + FLAGS_client_read_write_timeout_ms * 1ms;
}

}  // namespace cqlserver
}  // namespace yb
