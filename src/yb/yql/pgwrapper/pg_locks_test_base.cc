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

#include <google/protobuf/repeated_field.h>
#include <gtest/gtest.h>

#include "yb/client/meta_cache.h"
#include "yb/client/table.h"
#include "yb/client/tablet_server.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"
#include "yb/master/master_defaults.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/yql/pgwrapper/pg_locks_test_base.h"

DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(enable_wait_queues);

using namespace std::literals;
using std::string;

namespace yb {
namespace pgwrapper {

using client::internal::RemoteTablet;
using client::internal::RemoteTabletPtr;

using tserver::TabletServerServiceProxy;

void PgLocksTestBase::SetUp() {
  // Enable wait-queues and deadlock detection.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;

  PgMiniTestBase::SetUp();
  InitTSProxies();
  InitPgClientProxies();
}

void PgLocksTestBase::InitTSProxies() {
  for (size_t i = 0 ; i < NumTabletServers() ; i++) {
    ts_proxies_.push_back(std::make_unique<TabletServerServiceProxy>(
        &client_->proxy_cache(),
        HostPort::FromBoundEndpoint(cluster_->mini_tablet_server(i)->bound_rpc_addr())));
  }
}

void PgLocksTestBase::InitPgClientProxies() {
  for (size_t i = 0 ; i < NumTabletServers() ; i++) {
    pg_client_service_proxies_.push_back(std::make_unique<PgClientServiceProxy>(
        &client_->proxy_cache(),
        HostPort::FromBoundEndpoint(cluster_->mini_tablet_server(i)->bound_rpc_addr())));
  }
}

Result<TabletId> PgLocksTestBase::GetSingularTabletOfTable(const string& table_name) {
  auto tables = VERIFY_RESULT(client_->ListTables(table_name));
  RSTATUS_DCHECK(tables.size() == 1, IllegalState, "Expected tables.size() == 1");

  auto table_id = tables.at(0).table_id();
  RSTATUS_DCHECK(!table_id.empty(), IllegalState, "Expected non-empty table_id");

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  RETURN_NOT_OK(client_->GetTabletsFromTableId(table_id, 5000, &tablets));
  RSTATUS_DCHECK(tablets.size() == 1, IllegalState, "Expected tablets.size() == 1");

  return tablets.begin()->tablet_id();
}

Result<TabletId> PgLocksTestBase::GetSingularStatusTablet() {
  auto table_name = client::YBTableName(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  std::vector<TabletId> tablet_uuids;
  RETURN_NOT_OK(client_->GetTablets(
      table_name, 1000 /* max_tablets */, &tablet_uuids, nullptr /* ranges */));
  CHECK_EQ(tablet_uuids.size(), 1);
  return tablet_uuids.at(0);
}

Result<TabletId> PgLocksTestBase::CreateTableAndGetTabletId(const string& table_name) {
  auto setup_conn = VERIFY_RESULT(Connect());

  RETURN_NOT_OK(setup_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS", table_name));
  RETURN_NOT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT generate_series(1, 10), 0", table_name));

  return VERIFY_RESULT(GetSingularTabletOfTable(table_name));
}

Result<tserver::GetOldTransactionsResponsePB> PgLocksTestBase::GetOldTransactions(
    const TabletId& status_tablet, uint32_t min_txn_age_ms, uint32_t max_num_txns) {
  rpc::RpcController controller;
  controller.set_timeout(kTimeoutMs * 1ms * kTimeMultiplier);

  tserver::GetOldTransactionsRequestPB req;
  req.set_tablet_id(status_tablet);
  req.set_min_txn_age_ms(min_txn_age_ms);
  req.set_max_num_txns(max_num_txns);

  Status s;
  for (auto& proxy : get_ts_proxies()) {
    tserver::GetOldTransactionsResponsePB resp;
    controller.Reset();

    RETURN_NOT_OK(proxy->GetOldTransactions(req, &resp, &controller));
    if (!resp.has_error()) {
      return resp;
    }
    s = StatusFromPB(resp.error().status()).CloneAndAppend("\n").CloneAndAppend(s.message());
  }

  return STATUS_FORMAT(IllegalState,
                       "GetLockStatus request failed: $0", s);
}

Result<tserver::PgGetLockStatusResponsePB> PgLocksTestBase::GetLockStatus(
    const tserver::PgGetLockStatusRequestPB& req) {
  RSTATUS_DCHECK(!pg_client_service_proxies_.empty(),
                 IllegalState,
                 "Found empty pg_client_service_proxies_ list.");

  tserver::PgGetLockStatusResponsePB resp;
  rpc::RpcController controller;
  RETURN_NOT_OK(pg_client_service_proxies_[0]->GetLockStatus(req, &resp, &controller));
  return resp;
}

Result<tserver::GetLockStatusResponsePB> PgLocksTestBase::GetLockStatus(
    const tserver::GetLockStatusRequestPB& req) {
  tserver::GetLockStatusResponsePB accumulated_resp;
  rpc::RpcController controller;
  controller.set_timeout(kTimeoutMs * 1ms * kTimeMultiplier);

  Status s;
  auto got_valid_resp = false;
  for (auto& proxy : get_ts_proxies()) {
    tserver::GetLockStatusResponsePB resp;
    controller.Reset();
    RETURN_NOT_OK(proxy->GetLockStatus(req, &resp, &controller));
    if (!resp.has_error()) {
      got_valid_resp = got_valid_resp || resp.tablet_lock_infos_size() > 0;
      for (const auto& tablet_lock_info : resp.tablet_lock_infos()) {
        *accumulated_resp.add_tablet_lock_infos() = tablet_lock_info;
      }
    }
    s = StatusFromPB(resp.error().status()).CloneAndAppend("\n").CloneAndAppend(s.message());
  }

  if (got_valid_resp) {
    return accumulated_resp;
  }
  return STATUS_FORMAT(IllegalState,
                       "GetLockStatus request failed: $0", s);
}

Result<tserver::GetLockStatusResponsePB> PgLocksTestBase::GetLockStatus(
    const TabletId& tablet_id,
    const std::vector<TransactionId>& transactions_ids) {
  tserver::GetLockStatusRequestPB req;
  auto& txn_info = (*req.mutable_transactions_by_tablet())[tablet_id];
  for (const auto& txn_id : transactions_ids) {
    txn_info.add_transactions()->set_id(txn_id.data(), txn_id.size());
  }
  return GetLockStatus(req);
}

Result<tserver::GetLockStatusResponsePB> PgLocksTestBase::GetLockStatus(
    const std::vector<TransactionId>& transactions_ids) {
  tserver::GetLockStatusRequestPB req;
  for (const auto& txn_id : transactions_ids) {
    req.add_transaction_ids(txn_id.data(), txn_id.size());
  }
  return GetLockStatus(req);
}

Result<TransactionId> PgLocksTestBase::GetSingularTransactionOnTablet(const TabletId& tablet_id) {
  auto resp = VERIFY_RESULT(GetLockStatus(tablet_id));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  RSTATUS_DCHECK(resp.tablet_lock_infos_size() == 1,
                 IllegalState,
                 "Expected to see single tablet, bot found more than one.");
  const auto& tablet_lock_info = resp.tablet_lock_infos(0);

  RSTATUS_DCHECK(tablet_lock_info.transaction_locks().size() == 1,
                 IllegalState,
                 "Expected to see single transaction, but found more than one.");
  return FullyDecodeTransactionId(tablet_lock_info.transaction_locks(0).id());
}

Result<TransactionId> PgLocksTestBase::OpenTransaction(
    const std::shared_ptr<PGConn>& conn, const string& table_name, const TabletId& tablet_id,
    const std::string& key) {
  RETURN_NOT_OK(conn->StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  RETURN_NOT_OK(conn->FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table_name, key));
  SleepFor(2ms * FLAGS_heartbeat_interval_ms * kTimeMultiplier);
  return GetSingularTransactionOnTablet(tablet_id);
}

Result<PgLocksTestBase::TestSession> PgLocksTestBase::Init(
    const string& table_name, const std::string& key_to_lock, const bool create_table) {
  TestSession s {
    .conn =  std::make_shared<PGConn>(VERIFY_RESULT(Connect())),
    .first_involved_tablet = VERIFY_RESULT(create_table
                                               ? CreateTableAndGetTabletId(table_name)
                                               : GetSingularTabletOfTable(table_name)),
    .table_name = table_name,
  };
  s.txn_id = VERIFY_RESULT(
      OpenTransaction(s.conn, table_name, s.first_involved_tablet, key_to_lock));
  return s;
}

std::vector<TabletServerServiceProxy*> PgLocksTestBase::get_ts_proxies(const std::string& ts_uuid) {
  std::vector<TabletServerServiceProxy*> ts_proxies;
  for (size_t i = 0 ; i < NumTabletServers() ; i++) {
    const auto& ts_ptr = cluster_->mini_tablet_server(i)->server();
    if (ts_uuid.empty() || ts_ptr->permanent_uuid() == ts_uuid) {
      ts_proxies.push_back(ts_proxies_[i].get());
    }
  }
  return ts_proxies;
}

std::vector<PgClientServiceProxy*> PgLocksTestBase::get_pg_client_service_proxies() {
  std::vector<PgClientServiceProxy*> pg_client_proxies;
  for (size_t i = 0 ; i < NumTabletServers() ; i++) {
    pg_client_proxies.push_back(pg_client_service_proxies_[i].get());
  }
  return pg_client_proxies;
}

Result<std::future<Status>> PgLocksTestBase::ExpectBlockedAsync(
    pgwrapper::PGConn* conn, const std::string& query) {
  auto status = std::async(std::launch::async, [&conn, query]() {
    return conn->Execute(query);
  });
  // TODO: Once https://github.com/yugabyte/yugabyte-db/issues/17295 is fixed, remove the
  // below annotations ignoring race.
  ANNOTATE_IGNORE_READS_BEGIN();
  RETURN_NOT_OK(WaitFor([&conn] () {
    return conn->IsBusy();
  }, 1s * kTimeMultiplier, "Wait for blocking request to be submitted to the query layer"));
  ANNOTATE_IGNORE_READS_END();
  return status;
}

} // namespace pgwrapper
} // namespace yb
