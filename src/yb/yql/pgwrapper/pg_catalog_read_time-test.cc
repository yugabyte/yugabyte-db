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

#include <atomic>
#include <future>

#include "yb/common/pg_catversions.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_heartbeat.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/clock.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_pause_pg_catalog_versions_cache_refresh);
DECLARE_bool(enable_heartbeat_pg_catalog_versions_cache);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_enable_catalog_version_read_time);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(ysql_yb_enable_invalidation_messages);

namespace yb::pgwrapper {

class PgCatalogReadTimeTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override {
    return 1;
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_invalidation_messages) = true;
    PgMiniTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_pg_catalog_versions_cache_refresh) = true;
  }
};

TEST_F(PgCatalogReadTimeTest, CachedSnapshotReadTime) {
  auto& catalog_manager =
      ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  master::DbOidToCatalogVersionMap versions;
  uint64_t fingerprint;
  HybridTime read_time;
  ASSERT_OK(catalog_manager.GetYsqlAllDBCatalogVersions(
      false /* use_cache */, &versions, &fingerprint, &read_time));
  ASSERT_FALSE(versions.empty());
  ASSERT_FALSE(read_time.is_special());

  catalog_manager.ResetCachedCatalogVersions();
  const auto generation = catalog_manager.GetPgCatalogVersionsCacheGeneration();
  ASSERT_TRUE(catalog_manager.InstallPgCatalogVersionsSnapshot(
      generation, read_time, versions, fingerprint, false /* update_messages */, std::nullopt));

  master::DbOidToCatalogVersionMap cached_versions;
  uint64_t cached_fingerprint;
  HybridTime cached_read_time;
  ASSERT_OK(catalog_manager.GetYsqlAllDBCatalogVersions(
      true /* use_cache */, &cached_versions, &cached_fingerprint, &cached_read_time));
  ASSERT_EQ(cached_read_time, read_time);
  ASSERT_EQ(cached_fingerprint, fingerprint);
  ASSERT_EQ(cached_versions.size(), versions.size());
  for (const auto& [db_oid, version] : versions) {
    const auto it = cached_versions.find(db_oid);
    ASSERT_NE(it, cached_versions.end());
    ASSERT_EQ(it->second.current_version, version.current_version);
    ASSERT_EQ(it->second.last_breaking_version, version.last_breaking_version);
  }

  catalog_manager.ResetCachedCatalogVersions();
  ASSERT_FALSE(catalog_manager.InstallPgCatalogVersionsSnapshot(
      generation, read_time, versions, fingerprint, false /* update_messages */, std::nullopt));
  ASSERT_OK(catalog_manager.GetYsqlAllDBCatalogVersions(
      true /* use_cache */, &cached_versions, &cached_fingerprint, &cached_read_time));
  ASSERT_TRUE(cached_versions.empty());
  ASSERT_FALSE(cached_read_time.is_valid());
}

class PgCatalogHeartbeatReadTimeTest : public PgCatalogReadTimeTest,
                                     public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_heartbeat_pg_catalog_versions_cache) = GetParam();
    PgCatalogReadTimeTest::SetUp();
  }
};

TEST_P(PgCatalogHeartbeatReadTimeTest, SnapshotReadTime) {
  auto* master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto& catalog_manager = master->catalog_manager_impl();
  master::DbOidToCatalogVersionMap versions;
  uint64_t fingerprint;
  HybridTime read_time;
  ASSERT_OK(catalog_manager.GetYsqlAllDBCatalogVersions(
      false /* use_cache */, &versions, &fingerprint, &read_time));
  ASSERT_FALSE(versions.empty());
  ASSERT_FALSE(read_time.is_special());
  catalog_manager.ResetCachedCatalogVersions();
  ASSERT_TRUE(catalog_manager.InstallPgCatalogVersionsSnapshot(
      catalog_manager.GetPgCatalogVersionsCacheGeneration(), read_time, versions, fingerprint,
      false /* update_messages */, std::nullopt));

  master::MasterHeartbeatProxy proxy(&client_->proxy_cache(), master->bound_rpc_addr());
  master::TSHeartbeatRequestPB req;
  req.set_universe_uuid(ASSERT_RESULT(catalog_manager.GetClusterConfig()).universe_uuid());
  *req.mutable_common()->mutable_ts_instance() =
      cluster_->mini_tablet_server(0)->server()->instance_pb();
  for (bool send_read_time : {false, true}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_catalog_version_read_time) = send_read_time;
    master::TSHeartbeatResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(MonoDelta::FromSeconds(10));
    ASSERT_OK(proxy.TSHeartbeat(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    const auto& data = resp.db_catalog_version_data();
    ASSERT_GT(data.db_catalog_versions_size(), 0);
    ASSERT_EQ(data.has_read_time(), send_read_time);
    if (!send_read_time) {
      continue;
    }
    const auto heartbeat_read_time = HybridTime::FromPB(data.read_time());
    ASSERT_FALSE(heartbeat_read_time.is_special());
    if (GetParam()) {
      ASSERT_EQ(heartbeat_read_time, read_time);
      ASSERT_EQ(data.db_catalog_versions_size(), versions.size());
      for (const auto& version : data.db_catalog_versions()) {
        const auto it = versions.find(version.db_oid());
        ASSERT_NE(it, versions.end());
        ASSERT_EQ(version.current_version(), it->second.current_version);
        ASSERT_EQ(version.last_breaking_version(), it->second.last_breaking_version);
      }
    } else {
      ASSERT_GE(heartbeat_read_time, read_time);
    }
  }
}

INSTANTIATE_TEST_CASE_P(Cache, PgCatalogHeartbeatReadTimeTest, ::testing::Bool());

TEST_F(PgCatalogReadTimeTest, ObjectLockReleaseSnapshotReadTime) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
  const auto db_oid = ASSERT_RESULT(conn.FetchRow<PGOid>(
      "SELECT oid FROM pg_database WHERE datname = current_database()"));
  auto& catalog_manager =
      ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  auto* server = cluster_->mini_tablet_server(0)->server();
  auto* sync_point = SyncPoint::GetInstance();

  for (bool send_read_time : {false, true}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_catalog_version_read_time) = send_read_time;
    master::DbOidToCatalogVersionMap versions;
    HybridTime read_before;
    ASSERT_OK(catalog_manager.GetYsqlAllDBCatalogVersions(
        false /* use_cache */, &versions, nullptr /* fingerprint */, &read_before));
    ASSERT_TRUE(versions.contains(db_oid));
    const auto version_before = versions.at(db_oid).current_version;

    std::atomic<bool> received{false};
    std::promise<std::pair<tserver::DBCatalogVersionDataPB, HybridTime>> promise;
    auto future = promise.get_future();
    auto cleanup = ScopeExit([&] {
      sync_point->DisableProcessing();
      sync_point->ClearAllCallBacks();
    });
    sync_point->SetCallBack("TabletServer::SetYsqlDBCatalogVersions:BeforePublish", [&](void* arg) {
      const auto& data = *static_cast<tserver::DBCatalogVersionDataPB*>(arg);
      if (!data.ignore_catalog_version_staleness_check()) {
        return;
      }
      for (const auto& version : data.db_catalog_versions()) {
        if (version.db_oid() == db_oid && version.current_version() > version_before &&
            !received.exchange(true)) {
          promise.set_value({data, server->Clock()->Now()});
        }
      }
    });
    sync_point->EnableProcessing();

    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE t ADD COLUMN c$0 INT", send_read_time ? 1 : 0));
    ASSERT_EQ(
        future.wait_for(std::chrono::seconds(10 * kTimeMultiplier)), std::future_status::ready);
    const auto [report, clock_at_publication] = future.get();
    ASSERT_EQ(report.has_read_time(), send_read_time);
    if (send_read_time) {
      const auto read_time = HybridTime::FromPB(report.read_time());
      ASSERT_FALSE(read_time.is_special());
      ASSERT_GT(read_time, read_before);
      ASSERT_GT(clock_at_publication, read_time);
    }
  }
}

}  // namespace yb::pgwrapper
