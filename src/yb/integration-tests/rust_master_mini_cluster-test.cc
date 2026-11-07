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

// MiniCluster with the Rust master embedded in the test process (D12): what lets in-process tests
// run against Rust masters with YB_RUST_MASTER.

#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master_admin.pb.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/master/mini_master.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

DECLARE_int32(blacklist_progress_initial_delay_secs);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(load_balancer_initial_delay_secs);
DECLARE_int64(outstanding_tablet_split_limit_per_tserver);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);

namespace yb {

class RustMasterMiniClusterTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
#ifndef YB_WITH_RUST_MASTER
    GTEST_SKIP() << "This build has no Rust master (--skip-rust)";
#endif
    // Tservers retire split parents (DeleteNotServingTablet) from this periodic cleanup.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
    // Tablet sizes reach the master in metrics heartbeats; read once, when a tserver starts.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 500;
    // Only the test that wants automatic splits gets them.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    MiniClusterOptions opts;
    opts.num_masters = 3;
    opts.num_tablet_servers = 3;
    opts.rust_masters = master::RustMasters::kAll;
    cluster_ = std::make_unique<MiniCluster>(opts);
    ASSERT_OK(cluster_->Start());
  }

  void TearDown() override {
    if (cluster_) {
      cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

  std::unique_ptr<MiniCluster> cluster_;
};

// Three Rust masters in one process elect a leader that every tserver registers with; the
// leadership moves on a step-down, and a restarted master rejoins.
TEST_F(RustMasterMiniClusterTest, LeadsFailsOverAndRestarts) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  ASSERT_TRUE(leader->is_rust());
  ASSERT_EQ(leader->master(), nullptr);
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  const auto old_leader_uuid = leader->permanent_uuid();

  ASSERT_OK(cluster_->StepDownMasterLeader());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto new_leader = cluster_->GetLeaderMiniMaster();
        return new_leader.ok() && (*new_leader)->permanent_uuid() != old_leader_uuid;
      },
      60s, "a new Rust leader"));
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));

  // A follower restarts on its own data and catches up.
  auto* new_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    auto* master = cluster_->mini_master(i);
    if (master != new_leader) {
      ASSERT_OK(master->Restart());
      break;
    }
  }
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())
                ->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
}

// A Rust leader serves YCQL keyspace and table DDL: the table's tablets are assigned, created and
// serve reads and writes, before and after a leader change, and the new leader alters and drops
// the table.
TEST_F(RustMasterMiniClusterTest, CreatesYcqlTables) {
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const client::YBTableName name(YQL_DATABASE_CQL, "ks", "kv");
  client::TableHandle table;
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /* num_tablets= */ 3, client.get(), &table, name));
  ASSERT_EQ(ASSERT_RESULT(client->ListTables("kv")).size(), 1);

  auto session = client->NewSession(60s);
  for (int32_t key = 0; key < 100; ++key) {
    ASSERT_RESULT(client::kv_table_test::WriteRow(&table, session, key, key * 2));
  }
  ASSERT_EQ(ASSERT_RESULT(client::kv_table_test::SelectAllRows(&table, session)).size(), 100);

  master::GetNamespaceInfoResponsePB info;
  ASSERT_OK(client->GetNamespaceInfo("ks", YQL_DATABASE_CQL, &info));
  ASSERT_EQ(info.namespace_().name(), "ks");
  auto status = client->DeleteNamespace("ks", YQL_DATABASE_CQL);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Cannot delete keyspace which has table: kv");

  // A new leader serves the same table, and creating it again finds it.
  ASSERT_OK(cluster_->StepDownMasterLeader());
  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())
                ->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  client::TableHandle reopened;
  ASSERT_OK(reopened.Open(name, client.get()));
  ASSERT_EQ(ASSERT_RESULT(client::kv_table_test::SelectAllRows(&reopened, session)).size(), 100);
  ASSERT_OK(client->CreateNamespaceIfNotExists("ks", YQL_DATABASE_CQL));

  // The new leader alters the table, waiting for every tablet to take the new schema, and drops
  // it, which frees the keyspace.
  auto alterer = client->NewTableAlterer(name);
  alterer->AddColumn("extra")->Type(DataType::INT32);
  ASSERT_OK(alterer->Alter());
  ASSERT_OK(reopened.Reopen());
  ASSERT_EQ(reopened->schema().num_columns(), 3);
  ASSERT_EQ(ASSERT_RESULT(client::kv_table_test::SelectAllRows(&reopened, session)).size(), 100);
  ASSERT_OK(client->DeleteTable(name));
  ASSERT_TRUE(ASSERT_RESULT(client->ListTables("kv")).empty());
  ASSERT_OK(client->DeleteNamespace("ks", YQL_DATABASE_CQL));

  ASSERT_OK(client->CreateNamespace("empty", YQL_DATABASE_CQL));
  ASSERT_TRUE(ASSERT_RESULT(client->NamespaceExists("empty", YQL_DATABASE_CQL)));
  ASSERT_OK(client->DeleteNamespace("empty", YQL_DATABASE_CQL));
  ASSERT_FALSE(ASSERT_RESULT(client->NamespaceExists("empty", YQL_DATABASE_CQL)));
}

// A transactional table makes the Rust leader create system.transactions first; the table
// serves writes once both run.
TEST_F(RustMasterMiniClusterTest, CreatesTransactionalYcqlTables) {
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const client::YBTableName name(YQL_DATABASE_CQL, "ks", "txn_kv");
  client::TableHandle table;
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, /* num_tablets= */ 2, client.get(), &table, name));
  ASSERT_TRUE(ASSERT_RESULT(client->TableExists(
      client::YBTableName(YQL_DATABASE_CQL, "system", "transactions"))));
  auto session = client->NewSession(60s);
  for (int32_t key = 0; key < 10; ++key) {
    ASSERT_RESULT(client::kv_table_test::WriteRow(&table, session, key, key));
  }
  ASSERT_EQ(ASSERT_RESULT(client::kv_table_test::SelectAllRows(&table, session)).size(), 10);
}

// The Rust leader's cluster balancer moves replicas to a new tserver until the load is even, then
// reports itself idle; the data stays readable throughout.
TEST_F(RustMasterMiniClusterTest, BalancesReplicasToANewTserver) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_initial_delay_secs) = 0;
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const client::YBTableName name(YQL_DATABASE_CQL, "ks", "balanced");
  client::TableHandle table;
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /* num_tablets= */ 6, client.get(), &table, name));
  auto session = client->NewSession(60s);
  for (int32_t key = 0; key < 100; ++key) {
    ASSERT_RESULT(client::kv_table_test::WriteRow(&table, session, key, key));
  }

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(4));
  // 18 replicas over 4 tservers: the new one gets at least 4.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto on_new = cluster_->mini_tablet_server(3)->server()->tablet_manager()
            ->GetNumLiveTablets();
        return on_new >= 4 && VERIFY_RESULT(client->IsLoadBalancerIdle());
      },
      120s, "replicas moved to the new tserver and the balancer idle"));
  ASSERT_TRUE(ASSERT_RESULT(client->IsLoadBalanced(4)));
  ASSERT_EQ(ASSERT_RESULT(client::kv_table_test::SelectAllRows(&table, session)).size(), 100);
}

// The Rust leader splits a tablet on request: the children serve the table's rows, and the parent
// is deleted once they run.
TEST_F(RustMasterMiniClusterTest, SplitsATablet) {
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const client::YBTableName name(YQL_DATABASE_CQL, "ks", "split");
  client::TableHandle table;
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /* num_tablets= */ 1, client.get(), &table, name));
  auto session = client->NewSession(60s);
  for (int32_t key = 0; key < 500; ++key) {
    ASSERT_RESULT(client::kv_table_test::WriteRow(&table, session, key, key));
  }
  ASSERT_OK(cluster_->FlushTablets());

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(client->GetTabletsFromTableId(table->id(), 0, &tablets));
  ASSERT_EQ(tablets.size(), 1);
  const auto parent_id = tablets[0].tablet_id();

  master::SplitTabletRequestPB req;
  req.set_tablet_id(parent_id);
  master::SplitTabletResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s);
  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>());
  ASSERT_OK(proxy.SplitTablet(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        google::protobuf::RepeatedPtrField<master::TabletLocationsPB> now;
        RETURN_NOT_OK(client->GetTabletsFromTableId(table->id(), 0, &now));
        if (now.size() != 2) {
          return false;
        }
        for (const auto& tablet : now) {
          if (tablet.tablet_id() == parent_id) {
            return false;
          }
          bool has_leader = false;
          for (const auto& replica : tablet.replicas()) {
            has_leader |= replica.role() == PeerRole::LEADER;
          }
          if (!has_leader) {
            return false;
          }
        }
        return true;
      },
      120s, "two children with leaders in place of the parent"));
  // Every row is read from the children once their first elections settle.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(client::kv_table_test::SelectAllRows(&table, session)).size() == 500;
      },
      60s, "all rows read from the children"));

  // Once the children serve, the tserver retires the parent: the master answers it as deleted.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        master::GetTabletLocationsRequestPB loc_req;
        loc_req.add_tablet_ids(parent_id);
        master::GetTabletLocationsResponsePB loc_resp;
        rpc::RpcController loc_rpc;
        loc_rpc.set_timeout(10s);
        auto client_proxy =
            VERIFY_RESULT(cluster_->GetLeaderMasterProxy<master::MasterClientProxy>());
        RETURN_NOT_OK(client_proxy.GetTabletLocations(loc_req, &loc_resp, &loc_rpc));
        // A deleted parent is an error that carries its children.
        return loc_resp.tablet_locations_size() == 0 && loc_resp.errors_size() == 1;
      },
      60s, "the split parent deleted"));
}

// The Rust leader splits a table's only tablet once its leader reports SST files over the low
// phase threshold, but not while splitting is disabled for a feature; IsTabletSplittingComplete
// follows the split.
TEST_F(RustMasterMiniClusterTest, SplitsATabletAutomatically) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_outstanding_tablet_split_limit_per_tserver) = 0;
  // The children keep their parent's data until compacted, so they are not split again.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const client::YBTableName name(YQL_DATABASE_CQL, "ks", "auto_split");
  client::TableHandle table;
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /* num_tablets= */ 1, client.get(), &table, name));
  auto session = client->NewSession(60s);
  for (int32_t key = 0; key < 500; ++key) {
    ASSERT_RESULT(client::kv_table_test::WriteRow(&table, session, key, key));
  }
  ASSERT_OK(cluster_->FlushTablets());

  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>());
  auto splitting_complete = [&]() -> Result<bool> {
    master::IsTabletSplittingCompleteRequestPB req;
    master::IsTabletSplittingCompleteResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(10s);
    RETURN_NOT_OK(proxy.IsTabletSplittingComplete(req, &resp, &rpc));
    SCHECK(!resp.has_error(), IllegalState, resp.ShortDebugString());
    return resp.is_tablet_splitting_complete();
  };
  auto num_tablets = [&]() -> Result<int> {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(client->GetTabletsFromTableId(table->id(), 0, &tablets));
    return tablets.size();
  };
  ASSERT_TRUE(ASSERT_RESULT(splitting_complete()));

  {
    master::DisableTabletSplittingRequestPB req;
    req.set_disable_duration_ms(5000);
    req.set_feature_name("RustMasterMiniClusterTest");
    master::DisableTabletSplittingResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(10s);
    ASSERT_OK(proxy.DisableTabletSplitting(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
  std::this_thread::sleep_for(3s);
  ASSERT_EQ(ASSERT_RESULT(num_tablets()), 1);

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return VERIFY_RESULT(num_tablets()) == 2; }, 120s,
      "the tablet split automatically"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(client::kv_table_test::SelectAllRows(&table, session)).size() == 500;
      },
      60s, "all rows read from the children"));
  ASSERT_OK(WaitFor(splitting_complete, 60s, "splitting complete"));
  // The uncompacted children are not split again.
  std::this_thread::sleep_for(3s);
  ASSERT_EQ(ASSERT_RESULT(num_tablets()), 2);
}

// Leader-blacklisting a tserver makes the Rust leader's balancer move every leader off it; the
// completion RPC and the leader-preference check then agree.
TEST_F(RustMasterMiniClusterTest, DrainsLeadersFromALeaderBlacklistedTserver) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_initial_delay_secs) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_blacklist_progress_initial_delay_secs) = 0;
  ASSERT_OK(cluster_->WaitForTabletServerCount(3));
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const client::YBTableName name(YQL_DATABASE_CQL, "ks", "drained");
  client::TableHandle table;
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /* num_tablets= */ 6, client.get(), &table, name));

  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  master::GetMasterClusterConfigRequestPB get_req;
  master::GetMasterClusterConfigResponsePB get_resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s);
  ASSERT_OK(proxy.GetMasterClusterConfig(get_req, &get_resp, &rpc));
  master::ChangeMasterClusterConfigRequestPB change_req;
  *change_req.mutable_cluster_config() = get_resp.cluster_config();
  const auto addr = cluster_->mini_tablet_server(0)->bound_rpc_addr();
  auto* host = change_req.mutable_cluster_config()->mutable_leader_blacklist()->add_hosts();
  host->set_host(addr.address().to_string());
  host->set_port(addr.port());
  master::ChangeMasterClusterConfigResponsePB change_resp;
  rpc.Reset();
  ASSERT_OK(proxy.ChangeMasterClusterConfig(change_req, &change_resp, &rpc));
  ASSERT_FALSE(change_resp.has_error()) << change_resp.ShortDebugString();

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        master::GetLeaderBlacklistPercentRequestPB req;
        master::GetLoadMovePercentResponsePB resp;
        rpc::RpcController controller;
        controller.set_timeout(10s);
        RETURN_NOT_OK(proxy.GetLeaderBlacklistCompletion(req, &resp, &controller));
        return !resp.has_error() && resp.percent() >= 100;
      },
      120s, "no leaders left on the leader-blacklisted tserver"));
  ASSERT_EQ(cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetLeaderCount(), 0);
}

}  // namespace yb
