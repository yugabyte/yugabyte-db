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

using MetricDelta = MetricCountersDescriber::DeltaType;

struct Dumper {
  explicit Dumper(const MetricDelta& source_) : source(source_) {}

  const MetricDelta& source;
};

std::ostream& operator<<(std::ostream& str, const Dumper& d) {
  return str << "master read rpc = " << d.source.master_read_rpc
             << ", resp cache queries = " << d.source.cache.queries
             << ", resp cache hits = " << d.source.cache.hits;
}

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

  struct MetricDeltaInfo {
    MetricCountersDescriber::DeltaType yb_db;
    MetricCountersDescriber::DeltaType test_db;
  };

  Result<MetricDeltaInfo> MetricDeltaForDefaultTablespaceGUC() {
    auto& catalog_manager = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
    auto sys_catalog_tablet =
        VERIFY_RESULT(catalog_manager.sys_catalog()->tablet_peer()->shared_tablet());

    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.ExecuteFormat(R"_tblsp_(
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
    RETURN_NOT_OK(conn.ExecuteFormat("ALTER DATABASE yugabyte SET  default_tablespace = 'rf3';"));

    LOG(INFO) << "Performing some writes on the catalog";
    auto conn3 = VERIFY_RESULT(Connect());
    const std::string kDatabaseName = "testdb";
    RETURN_NOT_OK(conn3.ExecuteFormat("CREATE DATABASE $0", kDatabaseName));

    // Compacting the sys catalog tablet updates the history cutoff time
    // on the tablet to reflect the h/istory_retention_interval_sec flags
    LOG(INFO) << "Trigger a flush and compaction of the sys catalog tablet";
    RETURN_NOT_OK(sys_catalog_tablet->Flush(tablet::FlushMode::kSync));
    RETURN_NOT_OK(sys_catalog_tablet->ForceManualRocksDBCompact());

    SleepFor(1s);

    LOG(INFO) << "Starting a new conn which should query pg_tablespace";
    VERIFY_RESULT(Connect());
    VERIFY_RESULT(ConnectToDB(kDatabaseName));

    // PGConn *conn4, *conn5;
    const auto yb_db_delta =
        VERIFY_RESULT(metrics_->Delta([this] { return ResultToStatus(Connect()); }));

    LOG(INFO) << "Metric deltas for yugabyte db: " << Dumper(yb_db_delta);

    const auto test_db_delta = VERIFY_RESULT(metrics_->Delta(
        [this, kDatabaseName] { return ResultToStatus(ConnectToDB(kDatabaseName)); }));

    LOG(INFO) << "Metric deltas for test db: " << Dumper(test_db_delta);

    return MetricDeltaInfo{.yb_db = yb_db_delta, .test_db = test_db_delta};
  }
};

class PgSnapshotTooOldWithPreloadTest : public PgSnapshotTooOldTest {
 protected:
  virtual void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_table_list) = "pg_tablespace";
    PgSnapshotTooOldTest::BeforePgProcessStart();
  }
};

}  // namespace

// This test verifies that we do not see a snapshot too old error
// when certain GUCs set via ALTER DATABASE are loaded on conn startup.
TEST_F(PgSnapshotTooOldTest, DefaultTablespaceGuc) {
  auto [yb_db, test_db] = ASSERT_RESULT(MetricDeltaForDefaultTablespaceGUC());
  ASSERT_GT(yb_db.master_read_rpc, test_db.master_read_rpc);
  ASSERT_EQ(yb_db.cache.hits, test_db.cache.hits);
  ASSERT_EQ(yb_db.cache.queries, test_db.cache.queries);
}

// Repeat the same test with catalog preloading set to preload pg_tablespace
// The validation of the default_tablespace GUC will not trigger a separate master RPC
TEST_F_EX(PgSnapshotTooOldTest, DefaultTablespaceGucWithPreload, PgSnapshotTooOldWithPreloadTest) {
  auto [yb_db, test_db] = ASSERT_RESULT(MetricDeltaForDefaultTablespaceGUC());
  ASSERT_EQ(yb_db.master_read_rpc, test_db.master_read_rpc);
  ASSERT_EQ(yb_db.cache.hits, test_db.cache.hits);
  ASSERT_EQ(yb_db.cache.queries, test_db.cache.queries);
}

}  // namespace yb::pgwrapper
