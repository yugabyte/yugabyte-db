// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/client_fwd.h"
#include "yb/client/client-test-util.h"
#include "yb/client/meta_cache.h"
#include "yb/client/namespace_info.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_rpc.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_cluster_client.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/json_document.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/scope_exit.h"
#include "yb/rpc/sidecars.h"
#include "yb/util/sync_point.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.pb.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"


using yb::client::YBTableName;
using yb::client::YBTableType;
// DECLARE_bool(TEST_always_return_consensus_info_for_succeeded_rpc);
DECLARE_bool(TEST_check_broadcast_address);
DECLARE_int32(blacklist_progress_initial_delay_secs);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_metacache_partial_refresh);
DECLARE_bool(send_blacklisted_tservers_on_heartbeat);
DECLARE_bool(ysql_enable_auto_analyze_infra);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(retry_failed_replica_ms);

namespace yb {

class MetacacheRefreshITest : public MiniClusterTestWithClient<ExternalMiniCluster> {
 public:
  const std::string kPgsqlNamespaceName = "test_namespace";
  const std::string kPgsqlTableName = "test_table";
  const std::string kPgsqlTableId = "test_table_id";
  const std::string kPgsqlKeyspaceName = "test_keyspace";
  const std::string kPgsqlKeyspaceID = "test_keyspace_id";

  Result<pgwrapper::PGConn> ConnectToDB(
      const std::string& dbname, bool simple_query_protocol = false) {
    return pgwrapper::PGConnBuilder({.host = cluster_->ysql_hostport(0).host(),
                                     .port = cluster_->ysql_hostport(0).port(),
                                     .dbname = dbname})
        .Connect(simple_query_protocol);
  }

  void SetUp() {
    YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
    opts_.num_tablet_servers = 3;
    opts_.num_masters = 1;
    opts_.enable_ysql = true;
    cluster_.reset(new ExternalMiniCluster(opts_));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(MiniClusterTestWithClient<ExternalMiniCluster>::CreateClient());

    std::vector<std::string> hosts;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      hosts.push_back(cluster_->tablet_server(i)->bind_host());
    }
    CreatePgSqlTable();
  }

  void CreatePgSqlTable() {
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(client_->CreateNamespace(
        kPgsqlNamespaceName, YQL_DATABASE_PGSQL, "" /* creator */, "" /* ns_id */,
        "" /* src_ns_id */, std::nullopt /* next_pg_oid */, nullptr /* txn */, false));
    std::string kNamespaceId;
    {
      auto namespaces = ASSERT_RESULT(client_->ListNamespaces());
      for (const auto& ns : namespaces) {
        if (ns.id.name() == kPgsqlNamespaceName) {
          kNamespaceId = ns.id.id();
          break;
        }
      }
    }
    auto pgsql_table_name =
        YBTableName(YQL_DATABASE_PGSQL, kNamespaceId, kPgsqlNamespaceName, kPgsqlTableName);

    client::YBSchemaBuilder schema_builder;
    schema_builder.AddColumn("key")->PrimaryKey()->Type(DataType::STRING)->NotNull();
    schema_builder.AddColumn("value")->Type(DataType::INT64)->NotNull();
    EXPECT_OK(client_->CreateNamespaceIfNotExists(
        kPgsqlNamespaceName, YQLDatabase::YQL_DATABASE_PGSQL, "" /* creator_role_name */,
        kNamespaceId));
    client::YBSchema schema;
    EXPECT_OK(schema_builder.Build(&schema));
    EXPECT_OK(table_creator->table_name(pgsql_table_name)
                  .table_id(kPgsqlTableId)
                  .schema(&schema)
                  .table_type(YBTableType::PGSQL_TABLE_TYPE)
                  .set_range_partition_columns({"key"})
                  .num_tablets(1)
                  .Create());
  }

  Result<client::internal::RemoteTabletPtr> GetRemoteTablet(
      const TabletId& tablet_id, bool use_cache, client::YBClient* client) {
    std::promise<Result<client::internal::RemoteTabletPtr>> tablet_lookup_promise;
    auto future = tablet_lookup_promise.get_future();
    client->LookupTabletById(
        tablet_id, /* table =*/nullptr, master::IncludeHidden::kTrue,
        master::IncludeDeleted::kFalse, CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(1000),
        [&tablet_lookup_promise](const Result<client::internal::RemoteTabletPtr>& result) {
          tablet_lookup_promise.set_value(result);
        },
        client::UseCache(use_cache));
    return VERIFY_RESULT(future.get());
  }


  void ChangeClusterConfig(size_t idx = 0) {
    // add TServer to blacklist
    ASSERT_OK(cluster_->AddTServerToBlacklist(cluster_->master(), cluster_->tablet_server(idx)));
    // Add a node to the cluster
    ASSERT_OK(cluster_->AddTabletServer());
    ASSERT_OK(cluster_->WaitForTabletServerCount(3, 10s * kTimeMultiplier));
  }

  client::YBPgsqlWriteOpPtr CreateNewWriteOp(
      rpc::Sidecars& sidecars, std::shared_ptr<client::YBTable> pgsql_table,
      const std::string& key) {
    auto pgsql_write_op = client::YBPgsqlWriteOp::NewInsert(
        pgsql_table, SharedThreadSafeArena(), &sidecars);
    auto* psql_write_request = pgsql_write_op->mutable_request();
    psql_write_request->add_range_column_values()->mutable_value()->dup_string_value(key);
    auto* pgsql_column = psql_write_request->add_column_values();
    pgsql_column->set_column_id(pgsql_table->schema().ColumnId(1));
    pgsql_column->mutable_expr()->mutable_value()->set_int64_value(3);
    return pgsql_write_op;
  }

  ExternalMiniClusterOptions opts_;
};

// Tests that the metacache refresh works when a follower read is issued.
// Can either go to a leader or a follower.
// TEST_F(MetacacheRefreshITest, TestMetacacheRefreshFromFollowerRead) {
//   // ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_always_return_consensus_info_for_succeeded_rpc) =
//   //     false;
//   ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_metacache_partial_refresh) =
//       true;
//   std::shared_ptr<client::YBTable> pgsql_table;
//   EXPECT_OK(client_->OpenTable(kPgsqlTableId, &pgsql_table));
//   std::shared_ptr<client::YBSession> session = client_->NewSession(10s * kTimeMultiplier);
//   rpc::Sidecars sidecars;
//   auto write_op = CreateNewWriteOp(sidecars, pgsql_table, "pgsql_key1");
//   session->Apply(write_op);
//   FlushSessionOrDie(session);

//   google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
//   ASSERT_OK(client_->GetTabletsFromTableId(kPgsqlTableId, 0, &tablets));
//   const auto& tablet = tablets.Get(0);
//   auto tablet_id = tablet.tablet_id();
//   ASSERT_FALSE(tablet_id.empty());
//   ChangeClusterConfig();

//   auto remote_tablet = ASSERT_RESULT(GetRemoteTablet(tablet.tablet_id(), true, client_.get()));
//   auto pgsql_read_op = client::YBPgsqlReadOp::NewSelect(pgsql_table, &sidecars);
//   pgsql_read_op->set_yb_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
//   session->Apply(pgsql_read_op);
//   auto* sync_point_instance = yb::SyncPoint::GetInstance();
//   Synchronizer sync;
//   bool refresh_succeeded = false;
//   sync_point_instance->SetCallBack(
//       "TabletInvoker::RefreshFinishedWithOkRPCResponse",
//       [sync_point_instance, callback = sync.AsStdStatusCallback(), &refresh_succeeded](void* arg)
//       {
//         refresh_succeeded = *reinterpret_cast<bool*>(arg);
//         sync_point_instance->DisableProcessing();
//         callback(Status::OK());
//       });
//   sync_point_instance->EnableProcessing();
//   FlushSessionOrDie(session);
//   ASSERT_OK(sync.Wait());
//   ASSERT_TRUE(refresh_succeeded);
// }

TEST_F(MetacacheRefreshITest, TestMetacacheNoRefreshFromWrite) {
  // ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_always_return_consensus_info_for_succeeded_rpc) =
  //     false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_metacache_partial_refresh) =
      true;
  std::shared_ptr<client::YBTable> pgsql_table;
  EXPECT_OK(client_->OpenTable(kPgsqlTableId, &pgsql_table));
  std::shared_ptr<client::YBSession> session = client_->NewSession(10s * kTimeMultiplier);
  rpc::Sidecars sidecars;
  auto write_op = CreateNewWriteOp(sidecars, pgsql_table, "pgsql_key1");
  session->Apply(write_op);
  FlushSessionOrDie(session);
  Synchronizer sync;
  bool refresh_succeeded = false;
  auto* sync_point_instance = yb::SyncPoint::GetInstance();
  sync_point_instance->SetCallBack(
      "TabletInvoker::RefreshFinishedWithOkRPCResponse",
      [sync_point_instance, callback = sync.AsStdStatusCallback(), &refresh_succeeded](void* arg) {
        refresh_succeeded = *reinterpret_cast<bool*>(arg);
        sync_point_instance->DisableProcessing();
        callback(Status::OK());
      });
  sync_point_instance->EnableProcessing();
  write_op = CreateNewWriteOp(sidecars, pgsql_table, "pgsql_key2");
  session->Apply(write_op);
  FlushSessionOrDie(session);
  ASSERT_OK(sync.Wait());
  ASSERT_FALSE(refresh_succeeded);
}

// A tserver process shares one meta cache across all of its PG sessions, so a replica that was
// cached once stays cached until that tablet is looked up again. When a blacklisted tserver has
// been drained, the master names it in the heartbeat response and every tserver marks its cached
// replicas on it as permanently failed, so no query is routed to it once it is taken down. This
// drives the scenario end to end: follower reads from a gateway in the same region as the victim
// warm the cache, the victim is blacklisted and drained, and reads of the untouched tablets must
// stop dispatching to it before it is shut down. The victim is then brought back and its
// decommission reverted; once it hosts replicas again the gateway must route to it once more, so
// the permanent mark has to yield to the Raft config that a live replica piggybacks on a read.
class BlacklistedTServerMetacacheITest : public pgwrapper::PgMiniTestBase {
 protected:
  struct TestTable {
    std::string name;
    int value;
    std::string table_id;
  };

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_send_blacklisted_tservers_on_heartbeat) = true;
    // The master was just elected; do not sit out the post-failover grace period.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_blacklist_progress_initial_delay_secs) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze_infra) = false;
    // An ordinary failed mark would be retried almost immediately; only a permanent one keeps the
    // victim out of the replica selection for the rest of the test.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retry_failed_replica_ms) = 100;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override { return 4; }

  void OverrideMiniClusterOptions(MiniClusterOptions* options) override {
    options->transaction_table_num_tablets = 1;
  }

  std::vector<tserver::TabletServerOptions> ExtraTServerOptions() override {
    std::vector<tserver::TabletServerOptions> options;
    for (size_t i = 0; i != NumTabletServers(); ++i) {
      auto ts_options = CHECK_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
      // The gateway (index kPgTsIndex) and the victim (index 1) share a region, so with the gateway
      // hosting nothing the victim is the closest replica for the gateway's follower reads.
      ts_options.SetPlacement("cloud", i < 2 ? "near" : Format("region-$0", i), "zone");
      options.push_back(std::move(ts_options));
    }
    return options;
  }

  Status EnableFollowerReads(pgwrapper::PGConn* conn) {
    RETURN_NOT_OK(conn->Execute("SET yb_follower_read_staleness_ms = 2000"));
    RETURN_NOT_OK(conn->Execute("SET yb_read_from_followers = true"));
    return conn->Execute("SET default_transaction_read_only = true");
  }

  // Returns, for every tablet in the client's meta cache that has a replica on ts_uuid, whether
  // that replica is marked as permanently failed.
  Result<std::map<TabletId, bool>> PermanentlyFailedReplicasOn(
      client::YBClient* client, const std::string& ts_uuid) {
    std::stringstream stream;
    JsonWriter writer(&stream, JsonWriter::COMPACT);
    client->AddMetaCacheInfo(&writer);
    JsonDocument doc;
    auto root = VERIFY_RESULT(doc.Parse(stream.str()));
    std::map<TabletId, bool> result;
    for (const auto& tablet : VERIFY_RESULT(root["tablets"].GetArray())) {
      for (const auto& replica : VERIFY_RESULT(tablet["replicas"].GetArray())) {
        if (VERIFY_RESULT(replica["permanent_uuid"].GetString()) != ts_uuid) {
          continue;
        }
        result[VERIFY_RESULT(tablet["tablet_id"].GetString())] =
            VERIFY_RESULT(replica["permanent_failure"].GetBool());
      }
    }
    return result;
  }
};

TEST_F(BlacklistedTServerMetacacheITest, DrainedTServerIsNotRoutedTo) {
  auto* master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  master::MasterClusterClient cluster_client(
      master::MasterClusterProxy(&client_->proxy_cache(), master->bound_rpc_addr()));
  auto* gateway = cluster_->mini_tablet_server(kPgTsIndex)->server();
  auto* gateway_client = gateway->client_future().get();
  ASSERT_NE(gateway_client, nullptr);
  auto* victim = cluster_->mini_tablet_server(1);
  const auto victim_uuid = victim->server()->permanent_uuid();
  auto gateway_desc = ASSERT_RESULT(master->ts_manager().LookupTSByUUID(gateway->permanent_uuid()));
  auto victim_desc = ASSERT_RESULT(master->ts_manager().LookupTSByUUID(victim_uuid));
  const auto timeout = 60s * kTimeMultiplier;

  // The gateway hosts nothing and the victim hosts only followers, so a follower read from the
  // gateway is routed to the victim deterministically.
  ASSERT_OK(cluster_client.BlacklistHost(
      HostPortPB(gateway_desc->GetRegistration().private_rpc_addresses(0))));
  auto config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  *config.mutable_leader_blacklist()->add_hosts() =
      victim_desc->GetRegistration().private_rpc_addresses(0);
  ASSERT_OK(cluster_client.ChangeMasterClusterConfig(std::move(config)));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    const auto config = VERIFY_RESULT(cluster_client.GetMasterClusterConfig());
    return master->catalog_manager_impl().GetNumRelevantReplicas(
               config.server_blacklist(), false) == 0 &&
           master->catalog_manager_impl().GetNumRelevantReplicas(
               config.leader_blacklist(), true) == 0;
  }, timeout, "Drain gateway replicas and victim leaders"));

  // True once the table's single tablet has exactly three replicas, one of them a follower on the
  // victim. Sets *tablet_id whenever the victim is among the replicas.
  auto victim_hosts_follower =
      [&](const std::string& table_id, TabletId* tablet_id) -> Result<bool> {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> locations;
    RETURN_NOT_OK(client_->GetTabletsFromTableId(table_id, 0, &locations));
    if (locations.size() != 1 || locations.Get(0).replicas_size() != 3) {
      return false;
    }
    for (const auto& replica : locations.Get(0).replicas()) {
      if (replica.ts_info().permanent_uuid() == victim_uuid) {
        *tablet_id = locations.Get(0).tablet_id();
        return replica.role() == PeerRole::FOLLOWER;
      }
    }
    return false;
  };

  auto conn = ASSERT_RESULT(Connect());
  std::map<TabletId, TestTable> test_tablets;
  constexpr int kNumTables = 6;
  for (int i = 0; i != kNumTables; ++i) {
    const auto name = Format("drained_cache_$0", i);
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (k int PRIMARY KEY, v int) SPLIT INTO 1 TABLETS", name));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, $1)", name, i));
    const auto table_id = ASSERT_RESULT(GetTableIDFromTableName(name));
    TabletId tablet_id;
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return victim_hosts_follower(table_id, &tablet_id);
    }, timeout, "Victim hosts a follower of the test tablet"));
    test_tablets.emplace(tablet_id, TestTable{name, i, table_id});
  }
  // Half of the tablets are read while the victim is drained. The other half are left alone until
  // the victim is back, so their cached victim replica still carries the permanent mark by then.
  std::set<TabletId> all_tablets;
  std::set<TabletId> drained_phase_tablets;
  std::set<TabletId> held_back_tablets;
  for (const auto& [id, table] : test_tablets) {
    all_tablets.insert(id);
    (table.value < kNumTables / 2 ? drained_phase_tablets : held_back_tablets).insert(id);
  }

  std::mutex mutex;
  size_t victim_dispatches = 0;
  std::set<TabletId> dispatched_tablets;
  auto* sync_point = SyncPoint::GetInstance();
  auto cleanup = ScopeExit([&] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });
  sync_point->SetCallBack("TabletInvoker::BeforeSendRpcToTserver", [&](void* arg) {
    const auto& data = *static_cast<client::internal::TabletInvoker::RpcSendTestData*>(arg);
    if (data.client != gateway_client || data.ts_uuid != victim_uuid ||
        !test_tablets.contains(data.tablet_id)) {
      return;
    }
    std::lock_guard lock(mutex);
    ++victim_dispatches;
    dispatched_tablets.insert(data.tablet_id);
  });
  sync_point->EnableProcessing();

  auto read_all = [&](const std::set<TabletId>& tablets) -> Status {
    auto session = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(EnableFollowerReads(&session));
    for (const auto& tablet_id : tablets) {
      const auto& table = test_tablets.at(tablet_id);
      auto value = VERIFY_RESULT(session.FetchRow<int32_t>(
          Format("SELECT v FROM $0 WHERE k = 1", table.name)));
      SCHECK_EQ(value, table.value, IllegalState, "Unexpected value");
    }
    return Status::OK();
  };

  // Warm the gateway's meta cache: each read goes to the victim, the closest follower. Wait out the
  // follower read staleness first so the stale snapshot includes the rows inserted above.
  SleepFor(3s * kTimeMultiplier);
  ASSERT_OK(read_all(all_tablets));
  size_t dispatches_before_drain;
  {
    std::lock_guard lock(mutex);
    ASSERT_EQ(dispatched_tablets.size(), kNumTables);
    ASSERT_GE(victim_dispatches, kNumTables);
    dispatches_before_drain = victim_dispatches;
  }

  // Decommission the victim: add a replacement, blacklist it, and let the load balancer drain it.
  auto replacement_options =
      ASSERT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
  replacement_options.SetPlacement("cloud", "region-4", "zone");
  ASSERT_OK(cluster_->AddTabletServer(replacement_options));
  ASSERT_OK(cluster_client.BlacklistHost(
      HostPortPB(victim_desc->GetRegistration().private_rpc_addresses(0))));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    const auto config = VERIFY_RESULT(cluster_client.GetMasterClusterConfig());
    return master->catalog_manager_impl().GetNumRelevantReplicas(
        config.server_blacklist(), false) == 0;
  }, timeout, "Evacuate victim replicas"));

  // The heartbeat hint reaches the gateway while the victim is still alive, and marks the cached
  // victim replica of every tablet the gateway has not touched since.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    const auto failed = VERIFY_RESULT(PermanentlyFailedReplicasOn(gateway_client, victim_uuid));
    return std::all_of(all_tablets.begin(), all_tablets.end(), [&](const auto& tablet_id) {
      auto it = failed.find(tablet_id);
      return it != failed.end() && it->second;
    });
  }, timeout, "Gateway marked the victim's cached replicas as permanently failed"));

  // Reads of the untouched tablets no longer go to the victim, well past retry_failed_replica_ms.
  for (int i = 0; i != 3; ++i) {
    ASSERT_OK(read_all(drained_phase_tablets));
    SleepFor(MonoDelta::FromMilliseconds(5 * FLAGS_retry_failed_replica_ms));
  }
  {
    std::lock_guard lock(mutex);
    ASSERT_EQ(victim_dispatches, dispatches_before_drain);
  }

  // Nor after it is gone.
  victim->Shutdown();
  ASSERT_OK(read_all(drained_phase_tablets));
  {
    std::lock_guard lock(mutex);
    ASSERT_EQ(victim_dispatches, dispatches_before_drain);
  }

  // Revert the decommission: bring the victim back at the same address, take it off the blacklist
  // and blacklist the replacement instead. That leaves exactly three eligible tservers, so the load
  // balancer has to put a replica of every tablet back on the victim. It stays leader-blacklisted,
  // so those replicas are followers and the gateway's follower reads pick it as the closest one.
  ASSERT_OK(victim->Start(tserver::WaitTabletsBootstrapped::kFalse));
  ASSERT_OK(cluster_client.UnBlacklistHost(
      HostPortPB(victim_desc->GetRegistration().private_rpc_addresses(0))));
  auto* replacement = cluster_->mini_tablet_server(cluster_->num_tablet_servers() - 1);
  auto replacement_desc = ASSERT_RESULT(
      master->ts_manager().LookupTSByUUID(replacement->server()->permanent_uuid()));
  ASSERT_OK(cluster_client.BlacklistHost(
      HostPortPB(replacement_desc->GetRegistration().private_rpc_addresses(0))));
  for (const auto& [tablet_id, table] : test_tablets) {
    TabletId readded_tablet_id;
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return victim_hosts_follower(table.table_id, &readded_tablet_id);
    }, timeout, "Victim hosts a follower of the test tablet again"));
    ASSERT_EQ(readded_tablet_id, tablet_id);
  }

  // The held-back tablets were not touched since the hint, so the gateway still has their victim
  // replica cached as permanently failed. The first read of each tablet goes to another replica,
  // whose response carries the Raft config that the gateway's cached config index predates; that
  // rebuilds the replica list with the victim unmarked, and the second read is routed to it again.
  {
    const auto failed = ASSERT_RESULT(PermanentlyFailedReplicasOn(gateway_client, victim_uuid));
    for (const auto& tablet_id : held_back_tablets) {
      auto it = failed.find(tablet_id);
      ASSERT_TRUE(it != failed.end() && it->second) << tablet_id;
    }
  }
  {
    std::lock_guard lock(mutex);
    dispatched_tablets.clear();
  }
  ASSERT_OK(read_all(all_tablets));
  ASSERT_OK(read_all(all_tablets));
  {
    std::lock_guard lock(mutex);
    ASSERT_EQ(dispatched_tablets, all_tablets);
  }
  const auto failed = ASSERT_RESULT(PermanentlyFailedReplicasOn(gateway_client, victim_uuid));
  for (const auto& tablet_id : all_tablets) {
    auto it = failed.find(tablet_id);
    ASSERT_TRUE(it != failed.end()) << tablet_id;
    ASSERT_FALSE(it->second) << tablet_id;
  }
}

}  // namespace yb
