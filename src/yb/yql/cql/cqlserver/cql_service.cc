// Copyright (c) YugabyteDB, Inc.
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

#include "yb/yql/cql/cqlserver/cql_service.h"

#include <openssl/sha.h>

#include <fstream>
#include <mutex>
#include <thread>

#include <boost/compute/detail/lru_cache.hpp>

#include "yb/client/meta_data_cache.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/tablet_server_interface.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/cql_pg_util.h"
#include "yb/util/csv_util.h"
#include "yb/util/curl_util.h"
#include "yb/util/format.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/trace.h"

#include "yb/yql/cql/cqlserver/cql_processor.h"
#include "yb/yql/cql/cqlserver/cql_rpc.h"
#include "yb/yql/cql/cqlserver/cql_server.h"
#include "yb/yql/cql/cqlserver/system_query_cache.h"
#include "yb/yql/cql/ql/parser/parser.h"
#include "yb/util/flags.h"

#include "ybgate/ybgate_cpp_util.h"

using namespace std::placeholders;
using namespace yb::size_literals;

DECLARE_bool(use_cassandra_authentication);
DECLARE_int32(cql_update_system_query_cache_msecs);
DECLARE_string(tmp_dir);

DEFINE_UNKNOWN_int64(cql_service_max_prepared_statement_size_bytes, 128_MB,
             "The maximum amount of memory the CQL proxy should use to maintain prepared "
             "statements. 0 or negative means unlimited.");
DEPRECATE_FLAG(int32, cql_ybclient_reactor_threads, "02_2024");
DEFINE_UNKNOWN_int32(password_hash_cache_size, 64, "Number of password hashes to cache. 0 or "
             "negative disables caching.");
DEFINE_UNKNOWN_int64(cql_processors_limit, -4000,
             "Limit number of CQL processors. Positive means absolute limit. "
             "Negative means number of processors per 1GB of root mem tracker memory limit. "
             "0 - unlimited.");
DEFINE_UNKNOWN_bool(cql_check_table_schema_in_paging_state, true,
            "Return error for prepared SELECT statement execution if the table was altered "
            "during the prepared statement execution.");
DEFINE_RUNTIME_bool(ycql_enable_stat_statements, true, "If enabled, it will track queries "
            "and dump the metrics on http://localhost:12000/statements.");
DEFINE_RUNTIME_int64(cql_dump_statement_metrics_limit, 5000,
            "Limit the number of statements that are dumped at the /statements endpoint.");
DEFINE_RUNTIME_int32(cql_unprepared_stmts_entries_limit, 500,
            "Limit the number of unprepared statements that are being tracked.");

DEFINE_NON_RUNTIME_PREVIEW_bool(ycql_use_jwt_auth, false, "Use JWT for authentication.");

DEFINE_NON_RUNTIME_string(ycql_jwt_users_to_skip_csv, "",
    "Users that are authenticated via the local password"
    " check instead of JWT (if ycql_use_jwt_auth=true). This is a comma separated list.");
TAG_FLAG(ycql_jwt_users_to_skip_csv, sensitive_info);

DEFINE_NON_RUNTIME_string(ycql_jwt_conf, "",
    "The space-separated list of options to configure JWT authentication. "
    "The format is a list of 'key=value' pairs separated by space. "
    "Valid keys are:\n"
    "  * jwt_jwks_url: The URL from where to fetch the Json Web Key Set of the Identity Provider "
    "(IDP).\n"
    "  * jwt_audiences: The list of accepted audiences. One of the items within the list must match"
    " the 'aud' claim present in the token. Multiple values can be provided as a comma-separated "
    "list.\n"
    "  * jwt_issuers: The comma-separated list of issuers (IDP) that are valid. One of the items "
    "within the list must match the 'iss' claim present in the token.\n"
    "  * jwt_matching_claim_key: Key of the claim which represents the identity of the user on the "
    "Identity Provider (IDP). Some common values are 'sub', 'email', 'groups', 'roles' etc. The "
    "default value is 'sub'.");

DEFINE_NON_RUNTIME_string(ycql_ident_conf_csv, "",
    "CSV formatted line representing a list of identity mapping rules (in order). "
    "Each line contains two fields separated by space - IDP username and YCQL username. "
    "Only applicable in JWT authentication i.e. when ycql_use_jwt_auth=true.");

namespace yb {
namespace cqlserver {

const char* const kRoleColumnNameSaltedHash = "salted_hash";
const char* const kRoleColumnNameCanLogin = "can_login";
const char* const kJwtAuthJwksUrl = "jwt_jwks_url";
const char* const kJwtAudiences = "jwt_audiences";
const char* const kJwtIssuers = "jwt_issuers";
const char* const kJwtMatchingClaimKey = "jwt_matching_claim_key";
const char* const kJwtIdentMapName = "YCQL_IDENT_MAPNAME";

using std::shared_ptr;
using std::string;
using strings::Substitute;
using yb::client::YBMetaDataCache;
using yb::rpc::InboundCall;

class ParserFactory {
 public:
  explicit ParserFactory(CQLMetrics* metrics) : metrics_(metrics) {}

  ql::Parser* operator()() const {
    metrics_->parsers_created_->Increment();
    metrics_->parsers_alive_->Increment();
    return new ql::Parser;
  }

 private:
  CQLMetrics* metrics_;
};

class ParserDeleter {
 public:
  explicit ParserDeleter(CQLMetrics* metrics) : metrics_(metrics) {}

  void operator()(ql::Parser* parser) const {
    metrics_->parsers_alive_->Decrement();
    delete parser;
  }

 private:
  CQLMetrics* metrics_;
};

int64_t CQLProcessorsLimit() {
  auto value = FLAGS_cql_processors_limit;
  if (value > 0) {
    return value;
  }
  if (value == 0) {
    return std::numeric_limits<int64_t>::max();
  }
  return (-value * MemTracker::GetRootTracker()->limit()) >> 30;
}

CQLServiceImpl::CQLServiceImpl(CQLServer* server, const CQLServerOptions& opts)
    : CQLServerServiceIf(server->metric_entity()),
      server_(server),
      next_available_processor_(processors_.end()),
      password_cache_(FLAGS_password_hash_cache_size),
      // TODO(ENG-446): Handle metrics for all the methods individually.
      cql_metrics_(std::make_shared<CQLMetrics>(server->metric_entity())),
      parser_pool_(ParserFactory(cql_metrics_.get()), ParserDeleter(cql_metrics_.get())),
      messenger_(server->messenger()) {

  // Setup statements' memory tracker. Add garbage-collect function to delete least
  // recently used statements when limit is hit. Tracks both the prepared as well as
  // unprepared statements' memory usage.
  stmts_mem_tracker_ = MemTracker::CreateTracker(
      FLAGS_cql_service_max_prepared_statement_size_bytes > 0 ?
      FLAGS_cql_service_max_prepared_statement_size_bytes : -1,
      "CQL prepared statements", server->mem_tracker());

  LOG(INFO) << "CQL processors limit: " << CQLProcessorsLimit();

  processors_mem_tracker_ = MemTracker::CreateTracker("CQL processors", server->mem_tracker());

  requests_mem_tracker_ = MemTracker::CreateTracker("CQL Requests", server->mem_tracker());

  auth_prepared_stmt_ = std::make_shared<ql::Statement>(
      "",
      Substitute("SELECT $0, $1 FROM system_auth.roles WHERE role = ?",
                 kRoleColumnNameSaltedHash, kRoleColumnNameCanLogin));

  if (FLAGS_cql_update_system_query_cache_msecs > 0) {
    system_cache_ = std::make_shared<SystemQueryCache>(this);
  } else {
    VLOG(1) << "System query cache disabled.";
  }
}

CQLServiceImpl::~CQLServiceImpl() {
}

client::YBClient* CQLServiceImpl::client() const {
  auto client = server_->tserver()->client();
  if (client && !is_metadata_initialized_.load(std::memory_order_acquire)) {
    std::lock_guard l(metadata_init_mutex_);
    if (!is_metadata_initialized_.load(std::memory_order_acquire)) {
      auto meta_data_cache_mem_tracker =
          MemTracker::FindOrCreateTracker(0, "CQL Metadata cache", server_->mem_tracker());
      // Create and save the metadata cache object.
      metadata_cache_ = std::make_shared<YBMetaDataCache>(
          client, FLAGS_use_cassandra_authentication, meta_data_cache_mem_tracker);
      is_metadata_initialized_.store(true, std::memory_order_release);
    }
  }
  return client;
}

const std::shared_ptr<client::YBMetaDataCache>& CQLServiceImpl::metadata_cache() const {
  // Call client to wait for client and initialize metadata_cache if not already done.
  (void)client();
  return metadata_cache_;
}

Status CQLServiceImpl::CompleteInit() {
  stmts_mem_tracker_->AddGarbageCollector(shared_from_this());
  RETURN_NOT_OK(InitJwtAuth());
  return Status::OK();
}

void CQLServiceImpl::Shutdown() {
  if (system_cache_) {
    system_cache_->Shutdown();
  }
  decltype(processors_) processors;
  {
    std::lock_guard guard(processors_mutex_);
    processors.swap(processors_);
    processors_closed_ = true;
  }
  for (const auto& processor : processors) {
    processor->Shutdown();
  }
  if (metadata_cache_) {
    metadata_cache_->Shutdown();
  }
}

void CQLServiceImpl::Handle(yb::rpc::InboundCallPtr inbound_call) {
  TRACE("Handling the CQL call");
  // Collect the call.
  CQLInboundCall* cql_call = down_cast<CQLInboundCall*>(CHECK_NOTNULL(inbound_call.get()));
  DVLOG(4) << "Handling " << cql_call->ToString();
  ADOPT_WAIT_STATE(cql_call->wait_state());
  SCOPED_WAIT_STATUS(OnCpu_Active);

  // Process the call.
  MonoTime start = MonoTime::Now();

  Result<CQLProcessor*> processor = GetProcessor();
  if (!processor.ok()) {
    inbound_call->RespondFailure(rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY, processor.status());
    return;
  }
  if (const auto& wait_state = ash::WaitStateInfo::CurrentWaitState()) {
    ash::AshMetadata metadata{
        .root_request_id = Uuid::Generate(),
        .pid = server_->tserver()->SharedObject()->pid(),
        .client_host_port = HostPort(inbound_call->remote_address()),
        .addr_family = static_cast<uint8_t>(inbound_call->remote_address().address().is_v4()
            ? AF_INET : AF_INET6)};
    auto uuid_res = Uuid::FromHexStringBigEndian(server_->instance_pb().permanent_uuid());
    if (uuid_res.ok()) {
      metadata.top_level_node_id = *uuid_res;
    }
    wait_state->UpdateMetadata(metadata);
  }

  MonoTime got_processor = MonoTime::Now();
  cql_metrics_->time_to_get_cql_processor_->Increment(
      got_processor.GetDeltaSince(start).ToMicroseconds());
  (**processor).ProcessCall(std::move(inbound_call));
}

Result<CQLProcessor*> CQLServiceImpl::GetProcessor() {
  CQLProcessorListPos pos;
  {
    // Retrieve the next available processor. If none is available, allocate a new slot in the list.
    // Then create the processor outside the mutex below.
    std::lock_guard guard(processors_mutex_);
    SCHECK(!processors_closed_, ShutdownInProgress, "CQL service is shutting down");
    if (next_available_processor_ != processors_.end()) {
      return (next_available_processor_++)->get();
    }

    auto limit = CQLProcessorsLimit();
    if (num_allocated_processors_ >= limit) {
      return STATUS_FORMAT(ServiceUnavailable,
                           "Unable to allocate CQL processor, already allocated $0 of $1",
                           num_allocated_processors_, limit);
    }
    ++num_allocated_processors_;
    pos = processors_.emplace(processors_.end());
  }

  *pos = std::make_unique<CQLProcessor>(this, pos);
  return pos->get();
}

void CQLServiceImpl::ReturnProcessor(const CQLProcessorListPos& pos) {
  // Put the processor back before the next available one.
  std::lock_guard guard(processors_mutex_);
  processors_.splice(next_available_processor_, processors_, pos);
  next_available_processor_ = pos;
}

shared_ptr<CQLStatement> CQLServiceImpl::AllocateStatement(
    const ql::CQLMessage::QueryId& query_id, const string& query,
    ql::QLEnv* ql_env, IsPrepare is_prepare) {
  shared_ptr<CQLStatement> stmt;
  if (!is_prepare &&
      (!FLAGS_ycql_enable_stat_statements || FLAGS_cql_unprepared_stmts_entries_limit == 0)) {
    return stmt;
  }
  // Get exclusive lock before allocating a statement and updating the LRU list.
  std::lock_guard guard(is_prepare ? prepared_stmts_mutex_ : unprepared_stmts_mutex_);

  CQLStatementMap& stmts_map = (is_prepare ? prepared_stmts_map_ : unprepared_stmts_map_);
  CQLStatementList& stmts_list = (is_prepare ? prepared_stmts_list_ : unprepared_stmts_list_);

  const auto itr = stmts_map.find(query_id);
  bool is_new_stmt = (itr == stmts_map.end());

  if (!is_new_stmt) {
    stmt = itr->second;
    if (is_prepare) {
      const Result<bool> is_altered_res = stmt->IsYBTableAltered(ql_env);
      // The table is not available if (!is_altered_res.ok()).
      // Usually it happens if the table was deleted.
      if (!is_altered_res.ok() || *is_altered_res) {
        is_new_stmt = true;
        DeleteLruStatementUnlocked(stmt, &stmts_list, &stmts_map);
      }
    }
  }

  if (is_new_stmt) {
    // Before inserting, in case of unprepared statements, if the limit for the maximum number of
    // statements stored in the cache is reached, delete the least recently used statement from
    // the cache.
    if (!is_prepare &&
        static_cast<int>(stmts_list.size()) >= FLAGS_cql_unprepared_stmts_entries_limit) {
      DeleteLruStatementUnlocked(stmts_list.back(), &stmts_list, &stmts_map);
      VLOG(1) << "InsertStatement: deleted the least recent unprepared statement due to CQL cache"
              << " limit = " << FLAGS_cql_unprepared_stmts_entries_limit;
    }

    // Allocate the prepared statement placeholder that multiple clients trying to prepare the same
    // statement to contend on. The statement will then be prepared by one client while the rest
    // wait for the results.
    stmt = stmts_map
               .emplace(
                   query_id, std::make_shared<CQLStatement>(
                                 DCHECK_NOTNULL(ql_env)->CurrentKeyspace(), query,
                                 stmts_list.end(), stmts_mem_tracker_))
               .first->second;
    std::shared_ptr<StmtCounters> stmt_counters = std::make_shared<StmtCounters>(
        query, ql_env->CurrentKeyspace());
    stmt->SetCounters(stmt_counters);
    InsertLruStatementUnlocked(stmt, &stmts_list);
  } else {
    // Return existing statement if found.
    LOG_IF(DFATAL, stmt == nullptr) << "Unexpected null cql statement.";
    MoveLruStatementUnlocked(stmt, &stmts_list);
  }

  VLOG(1) << "InsertStatement: CQL " << (is_prepare ? "" : "un") << "prepared "
          << "statement cache count = " << stmts_map.size() << "/"
          << stmts_list.size() << ", memory usage = " << stmts_mem_tracker_->consumption();

  return stmt;
}

Result<std::shared_ptr<const CQLStatement>> CQLServiceImpl::GetPreparedStatement(
    const ql::CQLMessage::QueryId& query_id, SchemaVersion version) {
  // Get exclusive lock before looking up a prepared statement and updating the LRU list.
  std::lock_guard guard(prepared_stmts_mutex_);

  const auto itr = prepared_stmts_map_.find(query_id);
  if (itr == prepared_stmts_map_.end()) {
    return ErrorStatus(ql::ErrorCode::UNPREPARED_STATEMENT);
  }

  shared_ptr<CQLStatement> stmt = itr->second;
  LOG_IF(DFATAL, stmt == nullptr) << "Unexpected null statement";

  // If the statement has not finished preparing, do not return it.
  if (stmt->unprepared()) {
    return ErrorStatus(ql::ErrorCode::UNPREPARED_STATEMENT);
  }
  // If the statement is stale, delete it.
  if (stmt->stale()) {
    DeleteLruStatementUnlocked(stmt, &prepared_stmts_list_, &prepared_stmts_map_);
    return ErrorStatus(ql::ErrorCode::UNPREPARED_STATEMENT);
  }
  // If the statement has a later schema version, return a error.
  if (version != ql::StatementParameters::kUseLatest &&
      FLAGS_cql_check_table_schema_in_paging_state) {
    const SchemaVersion stmt_schema_version = VERIFY_RESULT(stmt->GetYBTableSchemaVersion());
    if (version != stmt_schema_version) {
      return ErrorStatus(
          ql::ErrorCode::WRONG_METADATA_VERSION,
          Substitute(
              "Table has been altered. Execute the query again. Requested schema version $0, "
              "got $1.", version, stmt_schema_version));
    }
  }

  MoveLruStatementUnlocked(stmt, &prepared_stmts_list_);
  return stmt;
}

void CQLServiceImpl::DeleteStatement(
    const shared_ptr<const CQLStatement>& stmt, const IsPrepare is_prepare) {
  // Get exclusive lock before deleting the prepared statement.
  std::lock_guard guard(is_prepare ? prepared_stmts_mutex_ : unprepared_stmts_mutex_);
  CQLStatementList& stmts_list = (is_prepare ? prepared_stmts_list_ : unprepared_stmts_list_);
  CQLStatementMap& stmts_map = (is_prepare ? prepared_stmts_map_ : unprepared_stmts_map_);
  DeleteLruStatementUnlocked(stmt, &stmts_list, &stmts_map);

  VLOG(1) << "DeleteStatement: CQL " << (is_prepare ? "" : "un") << "prepared statement"
          << " cache count = " << stmts_map.size() << "/" << stmts_list.size()
          << ", memory usage = " << stmts_mem_tracker_->consumption();
}

bool CQLServiceImpl::CheckPassword(
    const std::string plain,
    const std::string expected_bcrypt_hash) {
  if (FLAGS_password_hash_cache_size <= 0) {
    return util::bcrypt_checkpw(plain.c_str(), expected_bcrypt_hash.c_str()) == 0;
  }

  std::string sha_hash(SHA256_DIGEST_LENGTH, '\0');
  {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, plain.c_str(), plain.length());
    SHA256_Final((unsigned char*) &sha_hash[0], &ctx);
  }
  // bcrypt can generate multiple hashes from a single key, since a salt is
  // randomly generated each time a password is set. Using a compound key allows
  // the same plaintext to be associated with different hashes.
  std::string key = sha_hash + ":" + expected_bcrypt_hash;

  {
    std::lock_guard guard(password_cache_mutex_);
    auto entry = password_cache_.get(key);
    if (entry) {
      return true;
    }
  }

  // bcrypt_checkpw has stringcmp semantics.
  bool correct = util::bcrypt_checkpw(plain.c_str(), expected_bcrypt_hash.c_str()) == 0;
  if (correct) {
    std::lock_guard guard(password_cache_mutex_);
    // Boost's LRU cache interprets insertion of a duplicate key as a no-op, so
    // even if two threads successfully log in to the same account, there should
    // not be a race condition here.
    password_cache_.insert(key, true);
  }
  return correct;
}

void CQLServiceImpl::InsertLruStatementUnlocked(
    const shared_ptr<CQLStatement>& stmt, CQLStatementList* stmts_list) {
  // Insert the statement at the front of the LRU list.
  stmt->set_pos(stmts_list->insert(stmts_list->begin(), stmt));
}

void CQLServiceImpl::MoveLruStatementUnlocked(
    const shared_ptr<CQLStatement>& stmt, CQLStatementList* stmts_list) {
  // Move the statement to the front of the LRU list.
  stmts_list->splice(stmts_list->begin(), *stmts_list, stmt->pos());
}

void CQLServiceImpl::DeleteLruStatementUnlocked(
    const std::shared_ptr<const CQLStatement> stmt, CQLStatementList* stmts_list,
    CQLStatementMap* stmts_map) {
  // Remove statement from cache by looking it up by query ID and only when it is same statement
  // object. Note that the "stmt" parameter above is not a ref ("&") intentionally so that we have
  // a separate copy of the shared_ptr and not the very shared_ptr in prepared_stmts_map_ or
  // prepared_stmt_list_ we are deleting.
  const auto itr = stmts_map->find(stmt->query_id());
  if (itr != stmts_map->end() && itr->second == stmt) {
    stmts_map->erase(itr);
  }
  // Remove statement from LRU list only when it is in the list, i.e. pos() != end().
  if (stmt->pos() != stmts_list->end()) {
    stmts_list->erase(stmt->pos());
    stmt->set_pos(stmts_list->end());
  }
}

void CQLServiceImpl::CollectGarbage(size_t required) {
  // Remove an element from a bigger collection.
  const IsPrepare is_prepare(prepared_stmts_list_.size() > unprepared_stmts_list_.size());

  // Get exclusive lock before deleting the least recently used statement at the end of the LRU
  // list from the cache.
  std::lock_guard<std::mutex> guard(is_prepare ? prepared_stmts_mutex_ : unprepared_stmts_mutex_);

  CQLStatementList& stmts_list = (is_prepare ? prepared_stmts_list_ : unprepared_stmts_list_);
  CQLStatementMap& stmts_map = (is_prepare ? prepared_stmts_map_ : unprepared_stmts_map_);

  if (!stmts_list.empty()) {
    DeleteLruStatementUnlocked(stmts_list.back(), &stmts_list, &stmts_map);
  }

  VLOG(1) << "DeleteLruStatement: CQL "<< (is_prepare ? "" : "un") << "prepared statement cache "
          << "count = "<< stmts_map.size() << "/" << stmts_list.size()
          << ", memory usage = " << stmts_mem_tracker_->consumption();
}

client::TransactionPool& CQLServiceImpl::TransactionPool() {
  return server_->tserver()->TransactionPool();
}

server::Clock* CQLServiceImpl::clock() {
  return server_->clock();
}

void CQLServiceImpl::FillEndpoints(const rpc::RpcServicePtr& service, rpc::RpcEndpointMap* map) {
  map->emplace(CQLInboundCall::static_serialized_remote_method(), std::make_pair(service, 0ULL));
}

void CQLServiceImpl::DumpStatementMetricsAsJson(JsonWriter* jw) {
  jw->StartObject();
  for (const IsPrepare is_prepare : {IsPrepare::kTrue, IsPrepare::kFalse}) {
    jw->String(is_prepare ? "prepared_statements" : "unprepared_statements");
    jw->StartArray();
    const StmtCountersMap stmt_counters = GetStatementCountersForMetrics(is_prepare);
    for (auto& stmt : stmt_counters) {
      stmt.second.WriteAsJson(jw, stmt.first);
    }
    jw->EndArray();
  }
  jw->EndObject();
}

StmtCountersMap CQLServiceImpl::GetStatementCountersForMetrics(const IsPrepare& is_prepare) {
  auto const statement_limit = FLAGS_cql_dump_statement_metrics_limit;
  int64_t num_statements = 0;
  StmtCountersMap stmts_counters;
  std::lock_guard<std::mutex> guard(is_prepare ? prepared_stmts_mutex_ : unprepared_stmts_mutex_);
  const CQLStatementMap& stmts_map = (is_prepare ? prepared_stmts_map_ : unprepared_stmts_map_);
  for (auto& stmt : stmts_map) {
    shared_ptr<StmtCounters> stmt_counters = stmt.second->GetWritableCounters();
    if (stmt_counters) {
      if (stmt_counters->num_calls == 0) {
        continue;
      }
      stmts_counters.emplace(stmt.first, *stmt_counters);
      if (++num_statements >= statement_limit) {
        break;
      }
    }
  }
  return stmts_counters;
}

void CQLServiceImpl::UpdateStmtCounters(const ql::CQLMessage::QueryId& query_id,
    double execute_time_in_msec, IsPrepare is_prepare) {
  std::lock_guard<std::mutex> guard(is_prepare ? prepared_stmts_mutex_ : unprepared_stmts_mutex_);
  CQLStatementMap& stmts_map = (is_prepare ? prepared_stmts_map_ : unprepared_stmts_map_);

  auto itr = stmts_map.find(query_id);
  if (itr == stmts_map.end()) {
    if (is_prepare) {
      LOG(WARNING) << "Prepared Statement " << b2a_hex(query_id) << " not found in LRU cache.";
    } else {
      VLOG(1) << "Unprepared Statement not found in LRU cache.";
    }
    return;
  }
  std::shared_ptr<StmtCounters> stmt_counters = itr->second->GetWritableCounters();
  LOG_IF(DFATAL, stmt_counters == nullptr) << "Unexpected null statement counters.";
  LOG_IF(WARNING, stmt_counters->query.empty()) << "Unexpected empty query string in the counters.";
  UpdateCountersUnlocked(execute_time_in_msec, stmt_counters);
}

void CQLServiceImpl::UpdateCountersUnlocked(
    double execute_time_in_msec,
    std::shared_ptr<StmtCounters> stmt_counters) {
  LOG_IF(DFATAL, stmt_counters == nullptr) << "Null pointer counters received.";
  if (stmt_counters->num_calls == 0) {
    stmt_counters->num_calls = 1;
    stmt_counters->total_time_in_msec = execute_time_in_msec;
    stmt_counters->min_time_in_msec = execute_time_in_msec;
    stmt_counters->max_time_in_msec = execute_time_in_msec;
  } else {
    const double old_mean = stmt_counters->total_time_in_msec/stmt_counters->num_calls;
    stmt_counters->num_calls += 1;
    stmt_counters->total_time_in_msec += execute_time_in_msec;
    const double new_mean = stmt_counters->total_time_in_msec/stmt_counters->num_calls;

    // Welford's method for accurately computing variance. See
    // <http://www.johndcook.com/blog/standard_deviation/>
    stmt_counters->sum_var_time_in_msec +=
        (execute_time_in_msec - old_mean)*(execute_time_in_msec - new_mean);

    if (stmt_counters->max_time_in_msec < execute_time_in_msec) {
      stmt_counters->max_time_in_msec = execute_time_in_msec;
    }
    if (stmt_counters->min_time_in_msec > execute_time_in_msec) {
      stmt_counters->min_time_in_msec = execute_time_in_msec;
    }
  }
}

shared_ptr<StmtCounters> CQLServiceImpl::GetWritablePrepStmtCounters(const std::string& query_id) {
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);
  auto itr = prepared_stmts_map_.find(query_id);
  return itr == prepared_stmts_map_.end() ? nullptr : itr->second->GetWritableCounters();
}

void CQLServiceImpl::ResetStatementsCounters() {
  ResetPreparedStatementsCounters();
  // Clear the unprepared statements.
  std::lock_guard<std::mutex> guard(unprepared_stmts_mutex_);
  unprepared_stmts_map_.clear();
}

void CQLServiceImpl::ResetPreparedStatementsCounters() {
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);
  // Reset the counters for prepared statements.
  for (auto const & itr : prepared_stmts_map_) {
    auto stmt_counters = itr.second->GetWritableCounters();
    LOG_IF(DFATAL, stmt_counters == nullptr) << "Unexpected null pointer for statement counters";
    stmt_counters->ResetCounters();
  }
}

Status CQLServiceImpl::YCQLStatementStats(const tserver::PgYCQLStatementStatsRequestPB& req,
      tserver::PgYCQLStatementStatsResponsePB* resp) {
  for (const IsPrepare is_prepare : {IsPrepare::kTrue, IsPrepare::kFalse}) {
    const StmtCountersMap stmt_counters = this->GetStatementCountersForMetrics(is_prepare);
    for (auto &stmt : stmt_counters) {
      auto &stmt_pb = *resp->add_statements();
      stmt_pb.set_keyspace(stmt.second.keyspace);
      stmt_pb.set_queryid(ql::CQLMessage::QueryIdAsUint64(stmt.first));
      stmt_pb.set_query(stmt.second.query);
      stmt_pb.set_is_prepared(is_prepare == IsPrepare::kTrue);
      stmt_pb.set_calls(stmt.second.num_calls);
      stmt_pb.set_total_time(stmt.second.total_time_in_msec);
      stmt_pb.set_min_time(stmt.second.min_time_in_msec);
      stmt_pb.set_max_time(stmt.second.max_time_in_msec);
      stmt_pb.set_mean_time(stmt.second.total_time_in_msec / stmt.second.num_calls);
      const double stddev_time = stmt.second.GetStdDevTime();
      stmt_pb.set_stddev_time(stddev_time);
    }
  }
  return Status::OK();
}

Status CQLServiceImpl::LoadJwtOptions(std::string* jwks_url) {
  auto jwt_options = StringSplit(FLAGS_ycql_jwt_conf, ' ');

  for (const auto& option : jwt_options) {
    auto option_kv = StringSplit(option, '=');
    if (option_kv.size() != 2) {
      return STATUS(InvalidArgument, "Invalid JWT option format");
    }

    if (option_kv[0] == kJwtAuthJwksUrl) {
      DCHECK(jwks_url);
      *jwks_url = option_kv[1];
    } else if (option_kv[0] == kJwtAudiences) {
      RETURN_NOT_OK(ReadCSVValues(option_kv[1], &jwt_allowed_audience_));
    } else if (option_kv[0] == kJwtIssuers) {
      RETURN_NOT_OK(ReadCSVValues(option_kv[1], &jwt_allowed_issuers_));
    } else if (option_kv[0] == kJwtMatchingClaimKey) {
      jwt_matching_claim_key_ = option_kv[1];
    } else {
      return STATUS_FORMAT(InvalidArgument, "Unknown JWT option $0", option_kv[0]);
    }
  }

  VLOG(4) << "Loaded JWT Options: "
          << "JWKS URL=" << *jwks_url << ", "
          << "Audiences=" << CollectionToString(jwt_allowed_audience_) << ", "
          << "Issuers=" << CollectionToString(jwt_allowed_issuers_) << ", "
          << "Matching Claim Key=" << jwt_matching_claim_key_;

  return Status::OK();
}

Status CQLServiceImpl::LoadJwtJwks(const std::string& jwks_url) {
  LOG(INFO) << "Fetching JWT JWKS from URL: " << jwks_url;
  EasyCurl curl;
  faststring buf_ret;
  RETURN_NOT_OK(curl.FetchURL(jwks_url, &buf_ret));
  jwt_jwks_ = buf_ret.ToString();
  LOG(INFO) << "Loaded JWKS for JWT auth: " << jwt_jwks_;
  return Status::OK();
}

Status CQLServiceImpl::LoadIdentConf() {
  PG_RETURN_NOT_OK(YbgCreateMemoryContext(nullptr, "ycql_jwt_ident_memctx_", &jwt_ident_memctx_));

  if (FLAGS_ycql_ident_conf_csv.empty()) {
    LOG(INFO) << "Found empty ycql_ident_conf_csv";
    return Status::OK();
  }

  std::vector<std::string> ident_conf_lines;
  RETURN_NOT_OK(ReadCSVValues(FLAGS_ycql_ident_conf_csv, &ident_conf_lines));
  LOG(INFO) << "Read FLAGS_ycql_ident_conf_csv lines: " << CollectionToString(ident_conf_lines);

  const auto conf_path = JoinPathSegments(FLAGS_tmp_dir, "ycql_ident.conf");
  std::ofstream conf_file;
  conf_file.open(conf_path, std::ios_base::out | std::ios_base::trunc);
  if (!conf_file) {
    return STATUS_FORMAT(
        IOError,
        "Failed to write ycql_ident file '%s': errno=$0: $1",
        conf_path,
        errno,
        ErrnoToString(errno));
  }

  conf_file << "# This is an autogenerated file, do not edit manually!" << std::endl;
  conf_file << "# MAPNAME IDP-USERNAME YB-USERNAME" << std::endl;
  for (const auto& line : ident_conf_lines) {
      conf_file << kJwtIdentMapName << " " << line << std::endl;
  }
  conf_file.close();
  LOG(INFO) << "Wrote ycql_ident.conf file at " << conf_path;

  ScopedSetMemoryContext set_memctx(jwt_ident_memctx_);
  YbgStatus s = YbgLoadIdent(conf_path.c_str(), jwt_ident_memctx_);
  if (YbgStatusIsError(s)) {
    LOG(ERROR) << "Error in loading JWT Ident file: " << YbgStatusGetMessage(s);
    YbgDeleteMemoryContext();
    PG_RETURN_NOT_OK(s);
  }
  LOG(INFO) << "Successfully loaded Ident file for JWT auth";
  return Status::OK();
}

Status CQLServiceImpl::InitJwtAuth() {
  if (!FLAGS_ycql_use_jwt_auth) {
    return Status::OK();
  }

  LOG(INFO) << "Initializing JWT authentication";
  std::string jwks_url;
  RETURN_NOT_OK(LoadJwtOptions(&jwks_url));
  RETURN_NOT_OK(LoadJwtJwks(jwks_url));
  RETURN_NOT_OK(LoadIdentConf());
  return ValidateJwtConfig();
}

Status CQLServiceImpl::ValidateJwtConfig() {
  if (jwt_jwks_.empty()) {
    return STATUS(InvalidArgument, Format("JWKS received from the jwt_jwks_url cannot be empty"));
  }

  if (jwt_allowed_audience_.empty()) {
    return STATUS(InvalidArgument, "jwt_audiences cannot be empty");
  }

  if (jwt_allowed_issuers_.empty()) {
    return STATUS(InvalidArgument, "jwt_issuers cannot be empty");
  }

  return Status::OK();
}

}  // namespace cqlserver
}  // namespace yb
