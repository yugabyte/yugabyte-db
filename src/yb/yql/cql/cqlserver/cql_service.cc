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

#include "yb/yql/cql/cqlserver/cql_service.h"

#include <mutex>
#include <thread>

#include "yb/client/meta_data_cache.h"
#include "yb/client/transaction_pool.h"

#include "yb/gutil/strings/join.h"

#include "yb/yql/cql/cqlserver/cql_processor.h"
#include "yb/yql/cql/cqlserver/cql_rpc.h"
#include "yb/yql/cql/cqlserver/cql_server.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/mem_tracker.h"

using namespace std::placeholders;
using namespace yb::size_literals;

DECLARE_bool(use_cassandra_authentication);

DEFINE_int64(cql_service_max_prepared_statement_size_bytes, 128_MB,
             "The maximum amount of memory the CQL proxy should use to maintain prepared "
             "statements. 0 or negative means unlimited.");
DEFINE_int32(cql_ybclient_reactor_threads, 24,
             "The number of reactor threads to be used for processing ybclient "
             "requests originating in the cql layer");

namespace yb {
namespace cqlserver {

const char* const kRoleColumnNameSaltedHash = "salted_hash";
const char* const kRoleColumnNameCanLogin = "can_login";

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;
using strings::Substitute;
using yb::client::YBSchema;
using yb::client::YBSession;
using yb::client::YBMetaDataCache;
using yb::rpc::InboundCall;

CQLServiceImpl::CQLServiceImpl(CQLServer* server, const CQLServerOptions& opts,
                               ql::TransactionPoolProvider transaction_pool_provider)
    : CQLServerServiceIf(server->metric_entity()),
      server_(server),
      async_client_init_(
          "cql_ybclient", FLAGS_cql_ybclient_reactor_threads, kRpcTimeoutSec,
          server->tserver() ? server->tserver()->permanent_uuid() : "",
          &opts, server->metric_entity(), server->mem_tracker(),
          server->tserver() ? server->tserver()->messenger() : nullptr),
      next_available_processor_(processors_.end()),
      messenger_(server->messenger()),
      transaction_pool_provider_(std::move(transaction_pool_provider)) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  cql_metrics_ = std::make_shared<CQLMetrics>(server->metric_entity());

  // Setup prepared statements' memory tracker. Add garbage-collect function to delete least
  // recently used statements when limit is hit.
  prepared_stmts_mem_tracker_ = MemTracker::CreateTracker(
      FLAGS_cql_service_max_prepared_statement_size_bytes > 0 ?
      FLAGS_cql_service_max_prepared_statement_size_bytes : -1,
      "CQL prepared statements", server->mem_tracker());

  auth_prepared_stmt_ = std::make_shared<ql::Statement>(
      "",
      Substitute("SELECT $0, $1 FROM system_auth.roles WHERE role = ?",
                 kRoleColumnNameSaltedHash, kRoleColumnNameCanLogin));

  async_client_init_.Start();
}

CQLServiceImpl::~CQLServiceImpl() {
}

client::YBClient* CQLServiceImpl::client() const {
  auto client = async_client_init_.client();
  if (client && !is_metadata_initialized_.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> l(metadata_init_mutex_);
    if (!is_metadata_initialized_.load(std::memory_order_acquire)) {
      // Add local tserver if available.
      if (server_->tserver() != nullptr && server_->tserver()->proxy() != nullptr) {
        client->SetLocalTabletServer(
            server_->tserver()->permanent_uuid(), server_->tserver()->proxy(), server_->tserver());
      }
      // Create and save the metadata cache object.
      metadata_cache_ = std::make_shared<YBMetaDataCache>(client,
                                                          FLAGS_use_cassandra_authentication);
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

void CQLServiceImpl::CompleteInit() {
  prepared_stmts_mem_tracker_->AddGarbageCollector(shared_from_this());
}

void CQLServiceImpl::Shutdown() {
  decltype(processors_) processors;
  {
    std::lock_guard<std::mutex> guard(processors_mutex_);
    processors.swap(processors_);
  }
  for (const auto& processor : processors) {
    processor->Shutdown();
  }

  async_client_init_.Shutdown();
  auto client = this->client();
  if (client) {
    client->messenger()->Shutdown();
  }
}

void CQLServiceImpl::Handle(yb::rpc::InboundCallPtr inbound_call) {
  TRACE("Handling the CQL call");
  // Collect the call.
  CQLInboundCall* cql_call = down_cast<CQLInboundCall*>(CHECK_NOTNULL(inbound_call.get()));
  DVLOG(4) << "Handling " << cql_call->ToString();

  // Process the call.
  MonoTime start = MonoTime::Now();
  CQLProcessor *processor = GetProcessor();
  CHECK(processor != nullptr);
  MonoTime got_processor = MonoTime::Now();
  cql_metrics_->time_to_get_cql_processor_->Increment(
      got_processor.GetDeltaSince(start).ToMicroseconds());
  processor->ProcessCall(std::move(inbound_call));
}

CQLProcessor *CQLServiceImpl::GetProcessor() {
  CQLProcessorListPos pos;
  {
    // Retrieve the next available processor. If none is available, allocate a new slot in the list.
    // Then create the processor outside the mutex below.
    std::lock_guard<std::mutex> guard(processors_mutex_);
    pos = (next_available_processor_ != processors_.end() ?
           next_available_processor_++ : processors_.emplace(processors_.end()));
  }

  if (pos->get() == nullptr) {
    pos->reset(new CQLProcessor(this, pos));
  }
  return pos->get();
}

void CQLServiceImpl::ReturnProcessor(const CQLProcessorListPos& pos) {
  // Put the processor back before the next available one.
  std::lock_guard<std::mutex> guard(processors_mutex_);
  processors_.splice(next_available_processor_, processors_, pos);
  next_available_processor_ = pos;
}

shared_ptr<CQLStatement> CQLServiceImpl::AllocatePreparedStatement(
    const CQLMessage::QueryId& query_id, const string& keyspace, const string& query) {
  // Get exclusive lock before allocating a prepared statement and updating the LRU list.
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);

  shared_ptr<CQLStatement> stmt;
  const auto itr = prepared_stmts_map_.find(query_id);
  if (itr == prepared_stmts_map_.end()) {
    // Allocate the prepared statement placeholder that multiple clients trying to prepare the same
    // statement to contend on. The statement will then be prepared by one client while the rest
    // wait for the results.
    stmt = prepared_stmts_map_.emplace(
        query_id, std::make_shared<CQLStatement>(
            keyspace, query, prepared_stmts_list_.end())).first->second;
    InsertLruPreparedStatementUnlocked(stmt);
  } else {
    // Return existing statement if found.
    stmt = itr->second;
    MoveLruPreparedStatementUnlocked(stmt);
  }

  VLOG(1) << "InsertPreparedStatement: CQL prepared statement cache count = "
          << prepared_stmts_map_.size() << "/" << prepared_stmts_list_.size()
          << ", memory usage = " << prepared_stmts_mem_tracker_->consumption();

  return stmt;
}

shared_ptr<const CQLStatement> CQLServiceImpl::GetPreparedStatement(
    const CQLMessage::QueryId& query_id) {
  // Get exclusive lock before looking up a prepared statement and updating the LRU list.
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);

  const auto itr = prepared_stmts_map_.find(query_id);
  if (itr == prepared_stmts_map_.end()) {
    return nullptr;
  }

  shared_ptr<CQLStatement> stmt = itr->second;

  // If the statement has not finished preparing, do not return it.
  if (stmt->unprepared()) {
    return nullptr;
  }
  // If the statement is stale, delete it.
  if (stmt->stale()) {
    DeletePreparedStatementUnlocked(stmt);
    return nullptr;
  }

  MoveLruPreparedStatementUnlocked(stmt);
  return stmt;
}

void CQLServiceImpl::DeletePreparedStatement(const shared_ptr<const CQLStatement>& stmt) {
  // Get exclusive lock before deleting the prepared statement.
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);

  DeletePreparedStatementUnlocked(stmt);

  VLOG(1) << "DeletePreparedStatement: CQL prepared statement cache count = "
          << prepared_stmts_map_.size() << "/" << prepared_stmts_list_.size()
          << ", memory usage = " << prepared_stmts_mem_tracker_->consumption();
}

void CQLServiceImpl::InsertLruPreparedStatementUnlocked(const shared_ptr<CQLStatement>& stmt) {
  // Insert the statement at the front of the LRU list.
  stmt->set_pos(prepared_stmts_list_.insert(prepared_stmts_list_.begin(), stmt));
}

void CQLServiceImpl::MoveLruPreparedStatementUnlocked(const shared_ptr<CQLStatement>& stmt) {
  // Move the statement to the front of the LRU list.
  prepared_stmts_list_.splice(prepared_stmts_list_.begin(), prepared_stmts_list_, stmt->pos());
}

void CQLServiceImpl::DeletePreparedStatementUnlocked(
    const std::shared_ptr<const CQLStatement> stmt) {
  // Remove statement from cache by looking it up by query ID and only when it is same statement
  // object. Note that the "stmt" parameter above is not a ref ("&") intentionally so that we have
  // a separate copy of the shared_ptr and not the very shared_ptr in prepared_stmts_map_ or
  // prepared_stmt_list_ we are deleting.
  const auto itr = prepared_stmts_map_.find(stmt->query_id());
  if (itr != prepared_stmts_map_.end() && itr->second == stmt) {
    prepared_stmts_map_.erase(itr);
  }
  // Remove statement from LRU list only when it is in the list, i.e. pos() != end().
  if (stmt->pos() != prepared_stmts_list_.end()) {
    prepared_stmts_list_.erase(stmt->pos());
    stmt->set_pos(prepared_stmts_list_.end());
  }
}

void CQLServiceImpl::CollectGarbage(size_t required) {
  // Get exclusive lock before deleting the least recently used statement at the end of the LRU
  // list from the cache.
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);

  if (!prepared_stmts_list_.empty()) {
    DeletePreparedStatementUnlocked(prepared_stmts_list_.back());
  }

  VLOG(1) << "DeleteLruPreparedStatement: CQL prepared statement cache count = "
          << prepared_stmts_map_.size() << "/" << prepared_stmts_list_.size()
          << ", memory usage = " << prepared_stmts_mem_tracker_->consumption();
}

server::Clock* CQLServiceImpl::clock() {
  return server_->clock();
}


}  // namespace cqlserver
}  // namespace yb
