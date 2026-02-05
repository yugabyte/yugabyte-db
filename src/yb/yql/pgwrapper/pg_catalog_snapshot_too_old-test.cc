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

#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/master/master.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(stream_compression_algo);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(timestamp_syscatalog_history_retention_interval_sec);
DECLARE_bool(ysql_use_relcache_file);
DECLARE_string(ysql_catalog_preload_additional_table_list);

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);
METRIC_DECLARE_counter(pg_response_cache_hits);
METRIC_DECLARE_counter(pg_response_cache_queries);

namespace yb::pgwrapper {
namespace {
struct MetricCounters {
  struct ResponseCache {
    size_t queries;
    size_t hits;
  };

  ResponseCache cache;
  size_t master_read_rpc;
};

struct MetricCountersDescriber : public MetricWatcherDeltaDescriberTraits<MetricCounters, 3> {
  MetricCountersDescriber(
      std::reference_wrapper<const MetricEntity> master_metric,
      std::reference_wrapper<const MetricEntity> tserver_metric)
      : descriptors{
            Descriptor{
                &delta.master_read_rpc, master_metric,
                METRIC_handler_latency_yb_tserver_TabletServerService_Read},
            Descriptor{&delta.cache.queries, tserver_metric, METRIC_pg_response_cache_queries},
            Descriptor{&delta.cache.hits, tserver_metric, METRIC_pg_response_cache_hits}} {}

  DeltaType delta;
  Descriptors descriptors;
};

class PgSnapshotTooOldTest : public PgMiniTestBase {
 protected:
  virtual void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_syscatalog_history_retention_interval_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_relcache_file) = false;

    metrics_.emplace(
        *cluster_->mini_master()->master()->metric_entity(),
        *cluster_->mini_tablet_server(0)->server()->metric_entity());
  }

  std::optional<MetricWatcher<MetricCountersDescriber>> metrics_;

  void TestDefaultTablespaceGUC() {
    auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
    auto sys_catalog_tablet = catalog_manager.sys_catalog()->tablet_peer()->tablet();

    PGConn conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat(R"_tblsp_(
    create tablespace rf3 with
    (replica_placement='{"num_replicas":3, "placement_blocks":
    [{"cloud": "cloud0", "region":"region1",
    "zone":"zone","min_num_replicas":1},
    {"cloud": "cloud1", "region":"region2",
    "zone":"zone","min_num_replicas":1},
    {"cloud": "cloud1", "region":"region3",
    "zone":"zone","min_num_replicas":1}]}');
    )_tblsp_"));
    // The following GUCs also seem to trigger table reads
    // during validation when set via ALTER DATABASE/ROLE
    // SET client_encoding = 'LATIN1';
    // SET temp_tablespaces (though this GUC is not useful in YBDB, it is allowed to be set)
    // SET default_text_search_config (created via CREATE TEXT SEARCH CONFIGURATION)
    ASSERT_OK(conn.ExecuteFormat("ALTER DATABASE yugabyte SET  default_tablespace = 'rf3';"));

    LOG(INFO) << "Performing some writes on the catalog";
    PGConn conn3 = ASSERT_RESULT(Connect());
    const std::string kDatabaseName = "testdb";
    ASSERT_OK(conn3.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));

    // Compacting the sys catalog tablet updates the history cutoff time
    // on the tablet to reflect the h/istory_retention_interval_sec flags
    LOG(INFO) << "Trigger a flush and compaction of the sys catalog tablet";
    ASSERT_OK(sys_catalog_tablet->Flush(tablet::FlushMode::kSync));
    ASSERT_OK(sys_catalog_tablet->ForceManualRocksDBCompact());

    SleepFor(1s);

    LOG(INFO) << "Starting a new conn which should query pg_tablespace";
    (void)ASSERT_RESULT(Connect());
    (void)ASSERT_RESULT(ConnectToDB(kDatabaseName));

    // PGConn *conn4, *conn5;
    const auto deltaForYBDB =
        ASSERT_RESULT(metrics_->Delta([&] { return ResultToStatus(Connect()); }));

    LOG(INFO) << "Metric deltas for yugabyte db, master read rpc = " << deltaForYBDB.master_read_rpc
              << " resp cache queries " << deltaForYBDB.cache.queries << " resp cache hits "
              << deltaForYBDB.cache.hits;

    const auto deltaForTestDB =
        ASSERT_RESULT(metrics_->Delta([&] { return ResultToStatus(ConnectToDB(kDatabaseName)); }));

    LOG(INFO) << "Metric deltas for test db, master read rpc = " << deltaForTestDB.master_read_rpc
              << " resp cache queries " << deltaForTestDB.cache.queries << " resp cache hits "
              << deltaForTestDB.cache.hits;

    // We expect one extra master read for pg_tablespace for yugabyte db with the default_tablespace
    // GUC.
    ASSERT_GT(deltaForYBDB.master_read_rpc, deltaForTestDB.master_read_rpc);
    ASSERT_EQ(deltaForYBDB.cache.hits, deltaForTestDB.cache.hits);
    ASSERT_EQ(deltaForYBDB.cache.queries, deltaForTestDB.cache.queries);
  }
};

class PgSnapshotTooOldWithPreloadTest : public PgSnapshotTooOldTest {
 protected:
  virtual void BeforePgProcessStart() override {
    PgSnapshotTooOldTest::BeforePgProcessStart();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_table_list) = "pg_tablespace";
  }
};

}  // namespace

// This test verifies that we do not see a snapshot too old error
// when certain GUCs set via ALTER DATABASE are loaded on conn startup.
TEST_F(PgSnapshotTooOldTest, DefaultTablespaceGuc) {
  TestDefaultTablespaceGUC();
}

// Repeat the same test with catalog preloading set to preload pg_tablespace
// The validation of the default_tablespace GUC will still trigger a separate master RPC
TEST_F_EX(PgSnapshotTooOldTest, DefaultTablespaceGucWithPreload, PgSnapshotTooOldWithPreloadTest) {
  TestDefaultTablespaceGUC();
}

}  // namespace yb::pgwrapper
