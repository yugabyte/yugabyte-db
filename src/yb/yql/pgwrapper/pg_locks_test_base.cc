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

#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/erase.hpp>

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
#include "yb/util/monotime.h"
#include "yb/yql/pgwrapper/pg_locks_test_base.h"

DECLARE_int32(heartbeat_interval_ms);

using namespace std::literals;
using std::string;

// Returns a status of 'status_type' with message 'msg' when 'condition' evaluates to false.
#define RETURN_ON_FALSE(condition, status_type, msg) do { \
  if (!(condition)) { \
    return STATUS_FORMAT(status_type, msg); \
  } \
} while (false);

namespace yb {
namespace pgwrapper {

using client::internal::RemoteTablet;
using client::internal::RemoteTabletPtr;

using tserver::TabletServerServiceProxy;

void PgLocksTestBase::SetUp() {
  PgMiniTestBase::SetUp();
  InitTSProxies();
}

void PgLocksTestBase::InitTSProxies() {
  for (size_t i = 0 ; i < NumTabletServers() ; i++) {
    ts_proxies_.push_back(std::make_unique<TabletServerServiceProxy>(
        &client_->proxy_cache(),
        HostPort::FromBoundEndpoint(cluster_->mini_tablet_server(i)->bound_rpc_addr())));
  }
}

Result<TabletId> PgLocksTestBase::GetSingularTabletOfTable(const string& table_name) {
  auto tables = VERIFY_RESULT(client_->ListTables(table_name));
  RETURN_ON_FALSE(tables.size() == 1, IllegalState, "");

  auto table_id = tables.at(0).table_id();
  RETURN_ON_FALSE(!table_id.empty(), IllegalState, "");

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  RETURN_NOT_OK(client_->GetTabletsFromTableId(table_id, 5000, &tablets));
  RETURN_ON_FALSE(tablets.size() == 1, IllegalState, "");

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

Result<tserver::GetLockStatusResponsePB> PgLocksTestBase::GetLockStatus(
    const TabletId& tablet_id,
    const std::vector<TransactionId>& transactions_ids) {
  tserver::GetLockStatusRequestPB req;
  tserver::GetLockStatusResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kTimeoutMs * 1ms * kTimeMultiplier);
  req.set_tablet_id(tablet_id);

  for (const auto& txn_id : transactions_ids) {
    auto txn_id_str = txn_id.ToString();
    boost::erase_all(txn_id_str, "-");
    req.add_transaction_ids(boost::algorithm::unhex(txn_id_str));
  }

  Status s;
  for (auto& proxy : get_ts_proxies()) {
    resp.Clear();
    controller.Reset();

    RETURN_NOT_OK(proxy->GetLockStatus(req, &resp, &controller));
    if (!resp.has_error()) {
      return resp;
    }
    s = StatusFromPB(resp.error().status()).CloneAndAppend("\n").CloneAndAppend(s.message());
  }

  return STATUS_FORMAT(IllegalState,
                "GetLockStatus request for tablet $0 failed: $1", tablet_id, s);
}

Result<TransactionId> PgLocksTestBase::GetSingularTransactionOnTablet(const TabletId& tablet_id) {
  auto resp = VERIFY_RESULT(GetLockStatus(tablet_id));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::string txn_id_str = "";
  for (const auto& tablet_lock_info : resp.tablet_lock_infos()) {
    for (const auto& lock : tablet_lock_info.locks()) {
      if (txn_id_str.empty()) {
        txn_id_str = lock.transaction_id();
        continue;
      }

      RETURN_ON_FALSE(txn_id_str == lock.transaction_id(),
                      IllegalState,
                      "Expected to see single transaction, but found more than one.");
    }
  }

  RETURN_ON_FALSE(!txn_id_str.empty(), IllegalState, "Expected to see one txn, but found none.");
  return TransactionId::FromString(txn_id_str);
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

} // namespace pgwrapper
} // namespace yb
