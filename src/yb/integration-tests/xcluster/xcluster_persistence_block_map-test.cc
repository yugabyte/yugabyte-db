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

#include <gtest/gtest.h>

#include "yb/client/client.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/master/xcluster/xcluster_manager.h"

#include "yb/server/clock.h"

#include "yb/tools/admin-test-base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_xcluster_context_if.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/timestamp.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_hang_on_namespace_transition);
DECLARE_bool(TEST_skip_persistence_block_erase_on_drop);
DECLARE_bool(TEST_tserver_disable_heartbeat);
DECLARE_bool(enforce_xcluster_guarded_lease);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_uint32(xcluster_guarded_lease_duration_ms);
DECLARE_bool(ysql_yb_enable_listen_notify);

using namespace std::literals;

namespace yb {

using pgwrapper::PGConn;
using pgwrapper::PgMiniTestBase;
using pgwrapper::PGOid;

// These tests check the block map's state on master and TServers only.
class XClusterPersistenceBlockMapTest : public PgMiniTestBase {
 protected:
  virtual MonoDelta GetLeaseDuration() const { return {10s}; }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_guarded_lease_duration_ms) =
        narrow_cast<uint32_t>(GetLeaseDuration().ToMilliseconds());
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_xcluster_guarded_lease) = true;
    PgMiniTestBase::SetUp();
  }

  static MonoTime MakeDeadline() { return MonoTime::Now() + MonoDelta(30s * kTimeMultiplier); }

  Result<master::CatalogManager*> GetCatalogManager() {
    return &VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  }

  Result<master::XClusterManager*> GetXClusterManager() {
    return VERIFY_RESULT(GetCatalogManager())->GetXClusterManagerImpl();
  }

  Status StopPersisting(const std::vector<NamespaceId>& namespace_ids) {
    auto* catalog_manager = VERIFY_RESULT(GetCatalogManager());
    return catalog_manager->GetXClusterManagerImpl()->StopPersisting(
        namespace_ids, MakeDeadline(), catalog_manager->GetLeaderEpochInternal());
  }

  Status StartPersisting(const std::vector<NamespaceId>& namespace_ids) {
    auto* catalog_manager = VERIFY_RESULT(GetCatalogManager());
    return catalog_manager->GetXClusterManagerImpl()->StartPersisting(
        namespace_ids, MakeDeadline(), catalog_manager->GetLeaderEpochInternal());
  }

  Result<PersistenceBlockMapPB> GetMasterMap() {
    return VERIFY_RESULT(VERIFY_RESULT(GetXClusterManager())->GetPersistenceBlockMap())
        .persistence_block_map();
  }

  Result<uint64_t> GetMasterMapVersion() {
    return VERIFY_RESULT(VERIFY_RESULT(GetXClusterManager())->GetPersistenceBlockMap()).version();
  }

  static std::optional<HybridTime> GetBlockedThroughHt(
      const PersistenceBlockMapPB& map, uint32_t db_oid) {
    auto it = map.blocked_through_ht().find(db_oid);
    if (it == map.blocked_through_ht().end()) {
      return std::nullopt;
    }
    return HybridTime::FromPB(it->second);
  }

  Result<NamespaceId> GetNamespaceId(PGConn& conn, const std::string& db_name) {
    return GetPgsqlNamespaceId(VERIFY_RESULT(
        conn.FetchRow<PGOid>(Format("SELECT oid FROM pg_database WHERE datname = '$0'", db_name))));
  }

  struct Database {
    NamespaceId namespace_id;
    uint32_t oid;
  };

  Result<Database> CreateDatabase(PGConn& conn, const std::string& db_name) {
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0", db_name));
    const auto namespace_id = VERIFY_RESULT(GetNamespaceId(conn, db_name));
    return Database{namespace_id, VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_id))};
  }

  // Checks that every TServer holds exactly master's current map.
  Status CheckTServersHaveMasterMap() {
    const auto expected = VERIFY_RESULT(GetMasterMap()).ShortDebugString();
    for (const auto& tserver : cluster_->mini_tablet_servers()) {
      auto map =
          VERIFY_RESULT(tserver->server()->GetXClusterContext().TEST_GetPersistenceBlockMap());
      SCHECK_EQ(
          map->ShortDebugString(), expected, IllegalState,
          Format("TServer $0 has a different map", tserver->server()->permanent_uuid()));
    }
    return Status::OK();
  }
};

// XClusterPersistenceBlockMapTest but with leases long enough that TServers keep their leases for
// the whole test even with heartbeats disabled.
class XClusterPersistenceBlockMapTestLongLease : public XClusterPersistenceBlockMapTest {
 protected:
  MonoDelta GetLeaseDuration() const override { return {10min}; }
};

// Tests that StopPersisting sets the entry of each given database to kMax and leaves other
// databases alone.
TEST_F(XClusterPersistenceBlockMapTest, StopPersistingBlocksEverything) {
  auto conn = ASSERT_RESULT(Connect());
  const auto [namespace_id1, oid1] = ASSERT_RESULT(CreateDatabase(conn, "db1"));
  const auto [namespace_id2, oid2] = ASSERT_RESULT(CreateDatabase(conn, "db2"));

  ASSERT_OK(StopPersisting({namespace_id1}));
  const auto map = ASSERT_RESULT(GetMasterMap());
  ASSERT_EQ(GetBlockedThroughHt(map, oid1), HybridTime::kMax);
  ASSERT_FALSE(GetBlockedThroughHt(map, oid2).has_value());
}

// Tests that StartPersisting sets the entry of each given database, whether previously stopped or
// never stopped, to a time at least max_clock_skew_usec past master's clock when the call began,
// and leaves other databases alone.
TEST_F(XClusterPersistenceBlockMapTest, StartPersistingAllowsForClockSkew) {
  auto conn = ASSERT_RESULT(Connect());
  const auto stopped = ASSERT_RESULT(CreateDatabase(conn, "db1"));
  const auto never_stopped = ASSERT_RESULT(CreateDatabase(conn, "db2"));
  const auto still_stopped = ASSERT_RESULT(CreateDatabase(conn, "db3"));
  ASSERT_OK(StopPersisting({stopped.namespace_id, still_stopped.namespace_id}));

  auto& clock = *ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master()->clock();
  const auto before_time = clock.Now();
  ASSERT_OK(StartPersisting({stopped.namespace_id, never_stopped.namespace_id}));

  const auto map = ASSERT_RESULT(GetMasterMap());
  for (const auto& database : {stopped, never_stopped}) {
    const auto blocked_through = GetBlockedThroughHt(map, database.oid);
    ASSERT_TRUE(blocked_through.has_value());
    ASSERT_GE(*blocked_through, before_time.AddMicroseconds(FLAGS_max_clock_skew_usec));
  }
  ASSERT_EQ(GetBlockedThroughHt(map, still_stopped.oid), HybridTime::kMax);
}

// Tests that StartPersisting returns only once master's clock is strictly past the entry it set
// plus skew, that is, once every clock in the cluster is strictly past the entry.
TEST_F(XClusterPersistenceBlockMapTest, StartPersistingWaitsOutClockSkew) {
  auto conn = ASSERT_RESULT(Connect());
  const auto [namespace_id, oid] = ASSERT_RESULT(CreateDatabase(conn, "db1"));

  auto& clock = *ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master()->clock();
  ASSERT_OK(StartPersisting({namespace_id}));
  const auto after_time = clock.Now();

  const auto blocked_through = GetBlockedThroughHt(ASSERT_RESULT(GetMasterMap()), oid);
  ASSERT_TRUE(blocked_through.has_value());
  ASSERT_GE(after_time, blocked_through->AddMicroseconds(FLAGS_max_clock_skew_usec + 1));
}

// Tests that every StopPersisting and StartPersisting call bumps the version exactly once, even
// when it repeats the previous call.
TEST_F(XClusterPersistenceBlockMapTest, EachCallBumpsVersionOnce) {
  auto conn = ASSERT_RESULT(Connect());
  const auto namespace_id = ASSERT_RESULT(CreateDatabase(conn, "db1")).namespace_id;
  const auto initial_version = ASSERT_RESULT(GetMasterMapVersion());

  ASSERT_OK(StopPersisting({namespace_id}));
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), initial_version + 1);
  ASSERT_OK(StopPersisting({namespace_id}));
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), initial_version + 2);
  ASSERT_OK(StartPersisting({namespace_id}));
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), initial_version + 3);
  ASSERT_OK(StartPersisting({namespace_id}));
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), initial_version + 4);
}

// Tests that StopPersisting and StartPersisting deliver the new map to every TServer through their
// own propagation, without help from heartbeats.
TEST_F_EX(
    XClusterPersistenceBlockMapTest, CallsPropagateMapWithoutHeartbeats,
    XClusterPersistenceBlockMapTestLongLease) {
  auto conn = ASSERT_RESULT(Connect());
  const auto namespace_id = ASSERT_RESULT(CreateDatabase(conn, "db1")).namespace_id;

  // Stop heartbeats, and let any in flight finish, so that only the calls' own propagation can
  // deliver the map.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;
  SleepFor(FLAGS_heartbeat_interval_ms * 2ms * kTimeMultiplier);

  ASSERT_OK(StopPersisting({namespace_id}));
  ASSERT_OK(CheckTServersHaveMasterMap());
  ASSERT_OK(StartPersisting({namespace_id}));
  ASSERT_OK(CheckTServersHaveMasterMap());
}

class XClusterPersistenceBlockMapMultiMasterTest : public XClusterPersistenceBlockMapTest {
 protected:
  size_t NumMasters() override { return 3; }
};

TEST_F(XClusterPersistenceBlockMapMultiMasterTest, SurvivesLeaderChangeAndRestart) {
  auto conn = ASSERT_RESULT(Connect());
  const auto namespace_id1 = ASSERT_RESULT(CreateDatabase(conn, "db1")).namespace_id;
  const auto namespace_id2 = ASSERT_RESULT(CreateDatabase(conn, "db2")).namespace_id;
  ASSERT_OK(StopPersisting({namespace_id1, namespace_id2}));
  ASSERT_OK(StartPersisting({namespace_id2}));
  const auto expected = ASSERT_RESULT(GetMasterMap()).ShortDebugString();
  const auto expected_version = ASSERT_RESULT(GetMasterMapVersion());

  ASSERT_RESULT(cluster_->StepDownMasterLeader());
  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())
                ->master()
                ->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  ASSERT_EQ(ASSERT_RESULT(GetMasterMap()).ShortDebugString(), expected);
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), expected_version);

  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    cluster_->mini_master(i)->Shutdown();
  }
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    ASSERT_OK(cluster_->mini_master(i)->Start());
  }
  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())
                ->master()
                ->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  ASSERT_EQ(ASSERT_RESULT(GetMasterMap()).ShortDebugString(), expected);
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), expected_version);
}

// Tests that a TServer without a lease reports the map unavailable, and that when it regains its
// lease it also has the changes made to the map while it had no lease.
TEST_F(XClusterPersistenceBlockMapTest, TServerWithoutLeaseCatchesUp) {
  auto conn = ASSERT_RESULT(Connect());
  const auto [namespace_id, oid] = ASSERT_RESULT(CreateDatabase(conn, "db1"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;
  // Wait until master knows no TServer holds a lease, so propagation skips them all and only
  // heartbeats can deliver the map afterwards.
  auto& ts_manager = *ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master()->ts_manager();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        for (const auto& descriptor : ts_manager.GetAllDescriptors()) {
          if (descriptor->MaybeHasXClusterGuardedLease()) {
            return false;
          }
        }
        return true;
      },
      GetLeaseDuration() + MonoDelta(30s * kTimeMultiplier),
      "Wait for master to know no TServer holds a lease"));

  for (const auto& tserver : cluster_->mini_tablet_servers()) {
    ASSERT_NOK_STR_CONTAINS(
        tserver->server()->GetXClusterContext().TEST_GetPersistenceBlockMap(),
        "persistence block map is unavailable");
  }

  ASSERT_OK(StopPersisting({namespace_id}));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = false;
  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> { return CheckTServersHaveMasterMap().ok(); }, 30s * kTimeMultiplier,
      "Wait for TServers to catch up"));
  auto& xcluster_context = cluster_->mini_tablet_server(0)->server()->GetXClusterContext();
  auto map = ASSERT_RESULT(xcluster_context.TEST_GetPersistenceBlockMap());
  ASSERT_EQ(GetBlockedThroughHt(*map, oid), HybridTime::kMax);
}

TEST_F(XClusterPersistenceBlockMapTest, DropDatabaseErasesEntry) {
  auto conn = ASSERT_RESULT(Connect());
  const auto [namespace_id1, oid1] = ASSERT_RESULT(CreateDatabase(conn, "db1"));
  const auto [namespace_id2, oid2] = ASSERT_RESULT(CreateDatabase(conn, "db2"));
  ASSERT_OK(StopPersisting({namespace_id1, namespace_id2}));
  const auto version = ASSERT_RESULT(GetMasterMapVersion());

  ASSERT_OK(conn.Execute("DROP DATABASE db1"));
  // Master finishes dropping a database asynchronously.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return !GetBlockedThroughHt(VERIFY_RESULT(GetMasterMap()), oid1); },
      30s * kTimeMultiplier, "Wait for the dropped database's entry to be erased"));

  const auto map = ASSERT_RESULT(GetMasterMap());
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), version + 1);
  ASSERT_EQ(GetBlockedThroughHt(map, oid2), HybridTime::kMax);

  // Dropping a database without an entry leaves the map alone.
  ASSERT_RESULT(CreateDatabase(conn, "db3"));
  ASSERT_OK(conn.Execute("DROP DATABASE db3"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto* catalog_manager = VERIFY_RESULT(GetCatalogManager());
        return !catalog_manager->FindNamespaceByName(YQL_DATABASE_PGSQL, "db3").ok();
      },
      30s * kTimeMultiplier, "Wait for db3 to be dropped"));
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), version + 1);
}

// Tests that an entry the drop's own erase missed is erased when the next leader load removes the
// dropped database's tombstone.
TEST_F(XClusterPersistenceBlockMapTest, TombstoneRemovalErasesLeftoverEntry) {
  auto conn = ASSERT_RESULT(Connect());
  const auto [namespace_id, oid] = ASSERT_RESULT(CreateDatabase(conn, "db1"));
  ASSERT_OK(StopPersisting({namespace_id}));

  // Stands in for the drop's erase being lost to a crash or failing.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_persistence_block_erase_on_drop) = true;
  ASSERT_OK(conn.Execute("DROP DATABASE db1"));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto* catalog_manager = VERIFY_RESULT(GetCatalogManager());
        return !catalog_manager->FindNamespaceByName(YQL_DATABASE_PGSQL, "db1").ok();
      },
      30s * kTimeMultiplier, "Wait for db1 to be dropped"));
  ASSERT_EQ(GetBlockedThroughHt(ASSERT_RESULT(GetMasterMap()), oid), HybridTime::kMax);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_persistence_block_erase_on_drop) = false;

  ASSERT_OK(RestartMaster());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return !GetBlockedThroughHt(VERIFY_RESULT(GetMasterMap()), oid); },
      30s * kTimeMultiplier, "Wait for the leftover entry to be erased"));
}

// Tests that StopPersisting and StartPersisting reject a database while it is being dropped and
// after its drop finished, so neither can leave an entry behind for a dropped database.
TEST_F(XClusterPersistenceBlockMapTest, RejectsDroppedDatabases) {
  auto conn = ASSERT_RESULT(Connect());
  const auto [namespace_id, oid] = ASSERT_RESULT(CreateDatabase(conn, "db1"));
  const auto other_namespace_id = ASSERT_RESULT(CreateDatabase(conn, "db2")).namespace_id;
  ASSERT_OK(StopPersisting({namespace_id}));

  auto check_rejected = [&]() -> Status {
    const auto expected = VERIFY_RESULT(GetMasterMap()).ShortDebugString();
    const auto expected_version = VERIFY_RESULT(GetMasterMapVersion());
    for (auto status :
         {StopPersisting({other_namespace_id, namespace_id}),
          StartPersisting({namespace_id, other_namespace_id})}) {
      SCHECK(!status.ok(), IllegalState, "Call on a dropped database succeeded");
      SCHECK_STR_CONTAINS(status.ToString(), "is not running");
    }
    SCHECK_EQ(
        VERIFY_RESULT(GetMasterMap()).ShortDebugString(), expected, IllegalState, "Map changed");
    SCHECK_EQ(
        VERIFY_RESULT(GetMasterMapVersion()), expected_version, IllegalState, "Version changed");
    return Status::OK();
  };

  // Pause the drop just after master marks the database DELETING, until the flag is cleared below.
  TestThreadHolder thread_holder;
  // Declared after thread_holder so that, if an assertion fails, the drop is unpaused before
  // thread_holder joins the dropping thread.
  ScopeExit unpause_drop(
      [] { ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_on_namespace_transition) = false; });
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_on_namespace_transition) = true;
  thread_holder.AddThreadFunctor([this] {
    auto drop_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(drop_conn.Execute("DROP DATABASE db1"));
  });
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto* catalog_manager = VERIFY_RESULT(GetCatalogManager());
        return VERIFY_RESULT(catalog_manager->FindNamespaceById(namespace_id))->state() ==
               master::SysNamespaceEntryPB::DELETING;
      },
      30s * kTimeMultiplier, "Wait for db1 to be DELETING"));
  ASSERT_OK(check_rejected());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_on_namespace_transition) = false;
  thread_holder.JoinAll();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return !GetBlockedThroughHt(VERIFY_RESULT(GetMasterMap()), oid); },
      30s * kTimeMultiplier, "Wait for the dropped database's entry to be erased"));
  // Until the next leader load, master still finds the dropped database by its ID, as a DELETED
  // tombstone.
  ASSERT_EQ(
      ASSERT_RESULT(ASSERT_RESULT(GetCatalogManager())->FindNamespaceById(namespace_id))->state(),
      master::SysNamespaceEntryPB::DELETED);
  ASSERT_OK(check_rejected());
}

TEST_F(XClusterPersistenceBlockMapTest, RejectsInvalidDatabases) {
  auto conn = ASSERT_RESULT(Connect());
  const auto namespace_id = ASSERT_RESULT(CreateDatabase(conn, "db1")).namespace_id;
  // Creates system_postgres, the master-only namespace holding sequence data.
  ASSERT_OK(conn.Execute("CREATE SEQUENCE seq"));
  const auto expected = ASSERT_RESULT(GetMasterMap()).ShortDebugString();
  const auto expected_version = ASSERT_RESULT(GetMasterMapVersion());

  // With LISTEN/NOTIFY enabled, a master background task creates yb_system.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_listen_notify) = true;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return GetNamespaceId(conn, "yb_system").ok(); },
      30s * kTimeMultiplier, "Wait for yb_system to be created"));

  std::vector<NamespaceId> system_namespace_ids = {kPgSequencesDataNamespaceId};
  for (const auto& db_name : {"template0", "template1", "system_platform", "yb_system"}) {
    system_namespace_ids.push_back(ASSERT_RESULT(GetNamespaceId(conn, db_name)));
  }
  for (const auto& system_namespace_id : system_namespace_ids) {
    LOG(INFO) << "Trying " << system_namespace_id;
    ASSERT_NOK_STR_CONTAINS(
        StopPersisting({namespace_id, system_namespace_id}), "is a system database");
    ASSERT_NOK_STR_CONTAINS(StartPersisting({system_namespace_id}), "is a system database");
  }
  ASSERT_OK(client_->CreateNamespace("ks", YQL_DATABASE_CQL));
  const auto keyspace_id =
      ASSERT_RESULT(ASSERT_RESULT(GetCatalogManager())->FindNamespaceByName(YQL_DATABASE_CQL, "ks"))
          ->id();
  ASSERT_NOK_STR_CONTAINS(StopPersisting({namespace_id, keyspace_id}), "is not a YSQL database");
  // The YCQL system keyspace, which is rejected for more than one reason.
  ASSERT_NOK(StopPersisting({master::kSystemNamespaceId}));
  ASSERT_NOK(StopPersisting({namespace_id, GetPgsqlNamespaceId(kPgFirstNormalObjectId + 123456)}));

  // A call with any invalid database changes nothing.
  ASSERT_EQ(ASSERT_RESULT(GetMasterMap()).ShortDebugString(), expected);
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), expected_version);
}

// Tests that concurrent StopPersisting and StartPersisting calls on overlapping databases each take
// effect exactly once (one version bump each), and that every TServer ends with master's final map.
TEST_F(XClusterPersistenceBlockMapTest, ConcurrentCallsSerialize) {
  auto conn = ASSERT_RESULT(Connect());
  std::vector<NamespaceId> namespace_ids;
  for (const auto& db_name : {"db1", "db2", "db3"}) {
    namespace_ids.push_back(ASSERT_RESULT(CreateDatabase(conn, db_name)).namespace_id);
  }
  const auto initial_version = ASSERT_RESULT(GetMasterMapVersion());

  constexpr int kThreads = 2;
  constexpr int kCallsPerThread = 4;
  TestThreadHolder thread_holder;
  for (int t = 0; t < kThreads; ++t) {
    // Overlapping sets: {db1, db2} and {db2, db3}.
    const std::vector<NamespaceId> dbs = {namespace_ids[t % 2], namespace_ids[t % 2 + 1]};
    thread_holder.AddThreadFunctor([this, t, dbs] {
      for (int i = 0; i < kCallsPerThread; ++i) {
        if ((t + i) % 2 == 0) {
          ASSERT_OK(StopPersisting(dbs));
        } else {
          ASSERT_OK(StartPersisting(dbs));
        }
      }
    });
  }
  thread_holder.JoinAll();

  const auto map = ASSERT_RESULT(GetMasterMap());
  ASSERT_EQ(ASSERT_RESULT(GetMasterMapVersion()), initial_version + kThreads * kCallsPerThread);
  ASSERT_EQ(map.blocked_through_ht().size(), namespace_ids.size());
  ASSERT_OK(CheckTServersHaveMasterMap());
}

// Tests that the yb-admin commands take database names, reach master through the RPCs, and that
// get_persistence_block_map reports what master holds.
TEST_F(XClusterPersistenceBlockMapTest, YbAdminCommands) {
  auto conn = ASSERT_RESULT(Connect());
  const auto [namespace_id1, oid1] = ASSERT_RESULT(CreateDatabase(conn, "db1"));
  const auto [namespace_id2, oid2] = ASSERT_RESULT(CreateDatabase(conn, "db2"));
  const auto master_addresses = cluster_->GetMasterAddresses();

  ASSERT_RESULT(tools::RunAdminToolCommand(master_addresses, "stop_persisting", "db1,db2"));
  auto map = ASSERT_RESULT(GetMasterMap());
  ASSERT_EQ(GetBlockedThroughHt(map, oid1), HybridTime::kMax);
  ASSERT_EQ(GetBlockedThroughHt(map, oid2), HybridTime::kMax);

  ASSERT_RESULT(tools::RunAdminToolCommand(master_addresses, "start_persisting", "db1"));
  map = ASSERT_RESULT(GetMasterMap());
  const auto blocked_through = GetBlockedThroughHt(map, oid1);
  ASSERT_TRUE(blocked_through.has_value());
  ASSERT_NE(*blocked_through, HybridTime::kMax);

  const auto output =
      ASSERT_RESULT(tools::RunAdminToolCommand(master_addresses, "get_persistence_block_map"));
  LOG(INFO) << "get_persistence_block_map output:\n" << output;
  rapidjson::Document document;
  ASSERT_FALSE(document.Parse(output.c_str(), output.length()).HasParseError()) << output;
  ASSERT_EQ(document["version"].GetString(), std::to_string(ASSERT_RESULT(GetMasterMapVersion())));
  struct DumpedEntry {
    std::string database_oid;
    std::string blocked_through;
  };
  std::map<std::string, DumpedEntry> entries_by_name;
  for (const auto& database : document["databases"].GetArray()) {
    entries_by_name[database["database_name"].GetString()] = {
        database["database_oid"].GetString(), database["blocked_through"].GetString()};
  }

  ASSERT_EQ(entries_by_name.size(), 2) << output;
  ASSERT_EQ(entries_by_name["db1"].database_oid, std::to_string(oid1));
  // yb-admin formats times this way (tools::HybridTimeToString).
  ASSERT_EQ(
      entries_by_name["db1"].blocked_through,
      Timestamp(blocked_through->GetPhysicalValueMicros()).ToHumanReadableTime());
  ASSERT_EQ(entries_by_name["db2"].database_oid, std::to_string(oid2));
  ASSERT_EQ(entries_by_name["db2"].blocked_through, "max");

  ASSERT_NOK(tools::RunAdminToolCommand(master_addresses, "stop_persisting", "no_such_db"));
}

}  // namespace yb
