// Copyright (c) YugaByte, Inc.

#include "yb/cqlserver/cql_service.h"

#include <thread>

#include "yb/client/client.h"
#include "yb/gutil/strings/join.h"
#include "yb/cqlserver/cql_processor.h"
#include "yb/cqlserver/cql_server.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/cql_rpc.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/mem_tracker.h"

DEFINE_int64(cql_service_max_prepared_statement_size_bytes, 0,
             "The maximum amount of memory the CQL proxy should use to maintain prepared "
             "statements. 0 or negative means unlimited.");
DECLARE_int32(cql_service_num_threads);
DEFINE_int32(cql_ybclient_reactor_threads, 24,
             "The number of reactor threads to be used for processing ybclient "
             "requests originating in the cql layer");

namespace yb {
namespace cqlserver {

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBSession;
using yb::client::YBTableCache;
using yb::rpc::InboundCall;
using yb::rpc::CQLInboundCall;

CQLServiceImpl::CQLServiceImpl(
    CQLServer* server, shared_ptr<rpc::Messenger> messenger,
    const CQLServerOptions& opts)
    : CQLServerServiceIf(server->metric_entity()),
      messenger_(messenger),
      cql_rpcserver_env_(new rpc::CQLRpcServerEnv(server->first_rpc_address().address().to_string(),
                                                  opts.broadcast_rpc_address)) {
  // TODO(ENG-446): Handle metrics for all the methods individually.
  // Setup client.
  SetUpYBClient(opts.master_addresses_flag, server->metric_entity());
  cql_metrics_ = std::make_shared<CQLMetrics>(server->metric_entity());

  // Setup processors.
  processors_.reserve(FLAGS_cql_service_num_threads);
  for (unique_ptr<CQLProcessor>& processor : processors_) {
    processor.reset(new CQLProcessor(this));
  }

  // Setup prepared statements' memory tracker. Add garbage-collect function to delete least
  // recently used statements when limit is hit.
  prepared_stmts_mem_tracker_ = MemTracker::CreateTracker(
      FLAGS_cql_service_max_prepared_statement_size_bytes > 0 ?
      FLAGS_cql_service_max_prepared_statement_size_bytes : -1,
      "CQL prepared statements' memory usage", server->mem_tracker());
  prepared_stmts_mem_tracker_->AddGcFunction(
      std::bind(&CQLServiceImpl::DeleteLruPreparedStatement, this));
}

void CQLServiceImpl::SetUpYBClient(
    const string& yb_tier_master_addresses, const scoped_refptr<MetricEntity>& metric_entity) {
  YBClientBuilder client_builder;
  client_builder.set_client_name("cql_ybclient");
  client_builder.default_rpc_timeout(MonoDelta::FromSeconds(kRpcTimeoutSec));
  client_builder.add_master_server_addr(yb_tier_master_addresses);
  client_builder.set_metric_entity(metric_entity);
  client_builder.set_num_reactors(FLAGS_cql_ybclient_reactor_threads);
  CHECK_OK(client_builder.Build(&client_));
  table_cache_ = std::make_shared<YBTableCache>(client_);
}

void CQLServiceImpl::Handle(yb::rpc::InboundCallPtr inbound_call) {
  TRACE("Handling the CQL call");
  // Collect the call.
  CQLInboundCall* cql_call = down_cast<CQLInboundCall*>(CHECK_NOTNULL(inbound_call.get()));
  if (cql_call->TryResume()) {
    // This is a continuation/callback from a previous request.
    // Call the call back, and we are done.
    return;
  }
  DVLOG(4) << "Handling " << cql_call->ToString();

  // Process the call.
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  CQLProcessor *processor = GetProcessor();
  CHECK(processor != nullptr);
  MonoTime got_processor = MonoTime::Now(MonoTime::FINE);
  cql_metrics_->time_to_get_cql_processor_->Increment(
      got_processor.GetDeltaSince(start).ToMicroseconds());
  processor->ProcessCall(std::move(inbound_call));
}

CQLProcessor *CQLServiceImpl::GetProcessor() {
  // Must guard the processors_ pool as each processor can handle one and only one call at a time.
  std::lock_guard<std::mutex> guard(process_mutex_);

  CQLProcessor *cql_processor = nullptr;
  for (unique_ptr<CQLProcessor>& processor : processors_) {
    if (!processor->is_used()) {
      cql_processor = processor.get();
    }
  }

  // Create a new processor if needed.
  if (cql_processor == nullptr) {
    const int size = processors_.size();
    cql_processor = new CQLProcessor(this);
    processors_.reserve(std::max<int>(size * 2, size + 10));
    processors_.emplace_back(cql_processor);
  }

  // Make this processor used and return.
  cql_processor->used();
  return cql_processor;
}

shared_ptr<CQLStatement> CQLServiceImpl::AllocatePreparedStatement(
    const CQLMessage::QueryId& query_id, const string& keyspace, const string& sql_stmt) {
  // Get exclusive lock before allocating a prepared statement and updating the LRU list.
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);

  shared_ptr<CQLStatement> stmt;
  const auto itr = prepared_stmts_map_.find(query_id);
  if (itr == prepared_stmts_map_.end()) {
    // Allocate the prepared statement placeholder that multiple clients trying to prepare the same
    // statement to contend on. The statement will then be prepared by one client while the rest
    // wait for the results.
    stmt = prepared_stmts_map_.emplace(
        query_id, std::make_shared<CQLStatement>(keyspace, sql_stmt)).first->second;
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

shared_ptr<CQLStatement> CQLServiceImpl::GetPreparedStatement(const CQLMessage::QueryId& query_id) {
  // Get exclusive lock before looking up a prepared statement and updating the LRU list.
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);

  const auto itr = prepared_stmts_map_.find(query_id);
  if (itr == prepared_stmts_map_.end()) {
    return nullptr;
  }

  shared_ptr<CQLStatement> stmt = itr->second;
  MoveLruPreparedStatementUnlocked(stmt);
  return stmt;
}

void CQLServiceImpl::InsertLruPreparedStatementUnlocked(const shared_ptr<CQLStatement>& stmt) {
  // Insert the statement at the front of the LRU list.
  stmt->set_pos(prepared_stmts_list_.insert(prepared_stmts_list_.begin(), stmt));
}

void CQLServiceImpl::MoveLruPreparedStatementUnlocked(const shared_ptr<CQLStatement>& stmt) {
  // Move the statement to the front of the LRU list.
  prepared_stmts_list_.splice(prepared_stmts_list_.begin(), prepared_stmts_list_, stmt->pos());
}

void CQLServiceImpl::DeleteLruPreparedStatement() {
  // Get exclusive lock before deleting the least recently used statement at the end of the LRU
  // list from the cache.
  std::lock_guard<std::mutex> guard(prepared_stmts_mutex_);

  if (!prepared_stmts_list_.empty()) {
    prepared_stmts_map_.erase(prepared_stmts_list_.back()->query_id());
    prepared_stmts_list_.pop_back();
  }

  VLOG(1) << "DeletePreparedStatement: CQL prepared statement cache count = "
          << prepared_stmts_map_.size() << "/" << prepared_stmts_list_.size()
          << ", memory usage = " << prepared_stmts_mem_tracker_->consumption();
}

}  // namespace cqlserver
}  // namespace yb
