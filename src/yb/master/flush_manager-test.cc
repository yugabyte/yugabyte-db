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

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/cql_test_base.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/status.h"

using std::string;

namespace yb {
namespace master {

const string kNamespace = "test";

class FlushManagerTest : public CqlTestBase<MiniCluster> {
 protected:

  Result<OpId> GetOpIdAtLeader(const string& table_id) {
    auto all_peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : all_peers) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      if (tablet->metadata()->table_id() == table_id) {
        return VERIFY_RESULT(tablet->MaxPersistentOpId()).regular;
      }
    }
    return STATUS(IllegalState, "No leader found for table.");
  }

  Result<OpId> GetMaxOpId(const string& table_id) {
    auto all_peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    OpId max_op_id(0, 0);
    for (const auto& peer : all_peers) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      if (tablet->metadata()->table_id() == table_id) {
        max_op_id = std::max(max_op_id, VERIFY_RESULT(tablet->MaxPersistentOpId()).regular);
      }
    }
    return max_op_id;
  }
};

TEST_F(FlushManagerTest, VerifyFlush) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(
      session.ExecuteQuery("CREATE TABLE IF NOT EXISTS t (key INT PRIMARY KEY, value INT) WITH "
                           "transactions = { 'enabled' : true } and tablets = 1"));
  ASSERT_OK(session.ExecuteQuery("CREATE INDEX IF NOT EXISTS idx ON T (value) WITH tablets = 1"));

  const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "t");
  const client::YBTableName index_name(YQL_DATABASE_CQL, kNamespace, "idx");

  auto table = ASSERT_RESULT(client_->OpenTable(table_name));
  auto index = ASSERT_RESULT(client_->OpenTable(index_name));

  ASSERT_OK(session.ExecuteQuery("INSERT INTO t(key, value) VALUES (1, 2)"));

  auto baseline_table_op_id = ASSERT_RESULT(GetOpIdAtLeader(table->id()));
  auto baseline_index_op_id = ASSERT_RESULT(GetOpIdAtLeader(index->id()));
  ASSERT_OK(client_->FlushTables({table->name()}, true, 30, false));
  EXPECT_GT(ASSERT_RESULT(GetMaxOpId(table->id())), baseline_table_op_id);
  EXPECT_GT(ASSERT_RESULT(GetMaxOpId(index->id())), baseline_index_op_id);
}

TEST_F(FlushManagerTest, TestRpcFailureSingleTserverDown) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(
      session.ExecuteQuery("CREATE TABLE IF NOT EXISTS t (key INT PRIMARY KEY, value INT) WITH "
                           "tablets = 1"));
  const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "t");
  auto table = ASSERT_RESULT(client_->OpenTable(table_name));

  ASSERT_OK(session.ExecuteQuery("INSERT INTO t(key, value) VALUES (1, 2)"));

  auto tablet_peers = ListTableTabletPeers(cluster_.get(), table->id());

  size_t leader_idx;
  tserver::MiniTabletServer* leader_mini_tserver =
      GetLeaderForTablet(cluster_.get(), tablet_peers[0]->tablet_id(), &leader_idx);

  LOG(INFO) << "TServer which will be shutdown = " << leader_mini_tserver->ToString();
  cluster_->mini_tablet_server(leader_idx)->Shutdown();

  auto status = client_->FlushTables({table->id()}, true, 30, false);
  ASSERT_NOK(status);
  ASSERT_EQ(status.code(), Status::kInternalError);

  // Restarting node.
  ASSERT_OK(cluster_->mini_tablet_server(leader_idx)->Start());
}

} // namespace master
} // namespace yb
