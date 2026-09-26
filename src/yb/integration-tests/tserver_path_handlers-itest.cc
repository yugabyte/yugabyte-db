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

#include <string>

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/path_handlers_util.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/ql_protocol_util.h"

#include "yb/master/catalog_entity_info.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/flags.h"
#include "yb/util/json_document.h"
#include "yb/util/stol_utils.h"

DECLARE_bool(docdb_enable_sst_stats_collector);

namespace yb::integration_tests {

using std::string;

const uint kNumMasters(3);
const uint kNumTservers(3);

class TServerPathHandlersItest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void InitCluster() {
    MiniClusterOptions opts;
    opts.num_tablet_servers = kNumTservers;
    opts.num_masters = kNumMasters;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());
  }

  void SetUp() override {
    YBMiniClusterTestBase<MiniCluster>::SetUp();
    InitCluster();

    auto tserver_http_endpoint = cluster_->mini_tablet_server(0)->bound_http_addr();
    tserver_http_url_ = "http://" + AsString(tserver_http_endpoint);
  }

  void DoTearDown() override {
    LOG(INFO) << "Calling DoTearDown";
    cluster_->Shutdown();
  }

 protected:
  Result<string> FetchURL(const string& query_path) {
    faststring result;
    RETURN_NOT_OK(path_handlers_util::GetUrl(tserver_http_url_ + query_path, &result));
    return result.ToString();
  }

  string tserver_http_url_;
};

TEST_F(TServerPathHandlersItest, TestMasterPathHandlers) {
  faststring result;
  // TServer HTML paths.
  ASSERT_OK(FetchURL("/"));
  ASSERT_OK(FetchURL("/tables"));
  ASSERT_OK(FetchURL("/tablets"));
  ASSERT_OK(FetchURL("/operations"));
  ASSERT_OK(FetchURL("/tablet-consensus-status"));
  ASSERT_OK(FetchURL("/log-anchors"));
  ASSERT_OK(FetchURL("/transactions"));
  ASSERT_OK(FetchURL("/rocksdb"));
  ASSERT_OK(FetchURL("/sst-stats"));
  ASSERT_OK(FetchURL("/waitqueue"));
  ASSERT_OK(FetchURL("/api/v1/meta-cache"));
#ifndef NDEBUG
  ASSERT_OK(FetchURL("/intentsdb"));
#endif
  ASSERT_OK(FetchURL("/maintenance-manager"));

  // Default paths.
  ASSERT_OK(FetchURL("/logs"));
  ASSERT_OK(FetchURL("/varz"));
  ASSERT_OK(FetchURL("/status"));
  ASSERT_OK(FetchURL("/memz"));
  ASSERT_OK(FetchURL("/mem-trackers"));
  ASSERT_OK(FetchURL("/api/v1/mem-trackers"));
  ASSERT_OK(FetchURL("/api/v1/varz"));
  ASSERT_OK(FetchURL("/api/v1/version-info"));

  // API paths.
  ASSERT_OK(FetchURL("/api/v1/health-check"));
  ASSERT_OK(FetchURL("/api/v1/version"));
  ASSERT_OK(FetchURL("/api/v1/masters"));
  ASSERT_OK(FetchURL("/api/v1/tablets"));
}

TEST_F(TServerPathHandlersItest, TestVarzAutoFlag) {
  static const auto kExpectedAutoFlag = "ysql_yb_enable_expression_pushdown";

  // In Non LTO builds the unexpected AutoFlag will not be found. In LTO builds and MiniCluster
  // tests the flag will appear in the Default section instead of the AutoFlags section.
  static const auto kUnExpectedAutoFlag = "use_parent_table_id_field";

  // Test the HTML endpoint.
  static const auto kAutoFlagsStart = ">Auto Flags<";
  static const auto kAutoFlagsEnd = ">Default Flags<";

  auto result = ASSERT_RESULT(FetchURL("/varz"));

  auto it_auto_flags_start = result.find(kAutoFlagsStart);
  ASSERT_NE(it_auto_flags_start, std::string::npos);
  auto it_auto_flags_end = result.find(kAutoFlagsEnd);
  ASSERT_NE(it_auto_flags_end, std::string::npos);

  auto it_expected_flag = result.find(kExpectedAutoFlag);
  ASSERT_GT(it_expected_flag, it_auto_flags_start);
  ASSERT_LT(it_expected_flag, it_auto_flags_end);

  auto it_unexpected_flag = result.find(kUnExpectedAutoFlag);
  ASSERT_GT(it_unexpected_flag, it_auto_flags_end);

  // We should not have any hidden flags in the UI. TEST flags are always marked hidden.
  ASSERT_STR_NOT_CONTAINS(result, "TEST_override_transaction_priority");

  // We should not have any master flags in the tserver.
  ASSERT_STR_NOT_CONTAINS(result, "master_yb_client_default_timeout_ms");

  // Test the JSON API endpoint.
  result = ASSERT_RESULT(FetchURL("/api/v1/varz"));

  JsonDocument doc;
  auto json_obj = ASSERT_RESULT(doc.Parse(result));
  auto flags = ASSERT_RESULT(json_obj["flags"].GetArray());

  auto it_expected_json_flag = std::find_if(flags.begin(), flags.end(), [](const auto& flag) {
    return EXPECT_RESULT(flag["name"].GetString()) == kExpectedAutoFlag;
  });
  ASSERT_NE(it_expected_json_flag, flags.end());
  ASSERT_EQ(ASSERT_RESULT((*it_expected_json_flag)["type"].GetString()), "Auto");

  auto it_unexpected_json_flag = std::find_if(flags.begin(), flags.end(), [](const auto& flag) {
    return EXPECT_RESULT(flag["name"].GetString()) == kUnExpectedAutoFlag;
  });

  ASSERT_NE(it_unexpected_json_flag, flags.end());
  ASSERT_EQ(ASSERT_RESULT((*it_unexpected_json_flag)["type"].GetString()), "Default");
}

TEST_F(TServerPathHandlersItest, TestListMetaCache) {
  auto result = ASSERT_RESULT(FetchURL("/api/v1/meta-cache"));
  JsonDocument doc;
  auto json_object = ASSERT_RESULT(doc.Parse(result));
  for (const auto& remote_tablet :
       ASSERT_RESULT(json_object["MainMetaCache"]["tablets"].GetArray())) {
    ASSERT_TRUE(remote_tablet["tablet_id"].IsValid());
    ASSERT_TRUE(remote_tablet["replicas"].IsValid());
  }
}

TEST_F(TServerPathHandlersItest, TestSnapshotsEndpoint) {
  client::SnapshotTestUtil snapshot_util;
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  snapshot_util.SetProxy(&client->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  client::TableHandle table;
  client::YBSchema schema;
  client::YBSchemaBuilder builder;
  builder.AddColumn("key")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  ASSERT_OK(builder.Build(&schema));

  const client::YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "my_table");
  ASSERT_OK(client->CreateNamespaceIfNotExists(
      kTableName.namespace_name(), kTableName.namespace_type()));
  ASSERT_OK(table.Create(kTableName, 1 /* num_tablets */, schema, client.get()));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      table, YQL_DATABASE_CQL, kTableName.namespace_name(), client::WaitSnapshot::kTrue));

  auto rows = ASSERT_RESULT(path_handlers_util::GetHtmlTableRows(
      tserver_http_url_ + "/snapshots", "snapshots_" + kTableName.namespace_name()));
  ASSERT_GE(rows.size(), 2);

  ASSERT_EQ(rows[0][0], "Active RocksDB");
  ASSERT_FALSE(rows[1][0].empty()); // snapshot id
  ASSERT_FALSE(rows[1][1].empty()); // snapshot time
  ASSERT_FALSE(rows[1][2].empty()); // cumulative size
  ASSERT_FALSE(rows[1][3].empty()); // exclusive size
  ASSERT_EQ(rows[1][4], schedule_id.ToString()); // schedule id
}

class TServerSstStatsPathHandlerItest : public TServerPathHandlersItest {
 public:
  void SetUp() override {
    // Read when a tablet opens its regular DB, so it has to be set before the cluster starts.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_docdb_enable_sst_stats_collector) = CollectorEnabled();
    TServerPathHandlersItest::SetUp();
  }

 protected:
  static constexpr int32_t kNumRows = 50;

  virtual bool CollectorEnabled() { return true; }

  // Writes two versions of every row into one memtable and flushes it, leaving the tablet with a
  // single SST whose rows all have a version chain and whose older writes are garbage. One write
  // per row would leave the stretch and age distributions with nothing to measure.
  Result<string> WriteOneFileWithGarbage() {
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    client::YBSchema schema;
    client::YBSchemaBuilder builder;
    builder.AddColumn("key")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    builder.AddColumn("value")->Type(DataType::INT32);
    RETURN_NOT_OK(builder.Build(&schema));

    const client::YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "sst_stats_table");
    RETURN_NOT_OK(client->CreateNamespaceIfNotExists(
        table_name.namespace_name(), table_name.namespace_type()));
    client::TableHandle table;
    RETURN_NOT_OK(table.Create(table_name, /* num_tablets = */ 1, schema, client.get()));

    auto session = client->NewSession(30s);
    for (int32_t pass = 0; pass != 2; ++pass) {
      for (int32_t key = 0; key != kNumRows; ++key) {
        auto insert = table.NewInsertOp(session->arena());
        auto* req = insert->mutable_request();
        QLAddInt32HashValue(req, key);
        table.AddInt32ColumnValue(req, "value", pass);
        session->Apply(insert);
      }
      RETURN_NOT_OK(session->TEST_Flush());
    }
    RETURN_NOT_OK(cluster_->FlushTablets());

    const auto table_info = VERIFY_RESULT(FindTable(cluster_.get(), table_name));
    const auto peers = cluster_->GetTabletManager(0)->GetTabletPeersWithTableId(table_info->id());
    SCHECK_EQ(peers.size(), 1, IllegalState, "Expected one replica of the table on this tserver");
    return tserver_http_url_ + Format("/sst-stats?id=$0", peers.front()->tablet_id());
  }
};

TEST_F(TServerSstStatsPathHandlerItest, RendersPerFileDistributions) {
  const auto url = ASSERT_RESULT(WriteOneFileWithGarbage());

  const auto file_rows =
      ASSERT_RESULT(path_handlers_util::GetHtmlTableRows(url, "sst_stats_files"));
  ASSERT_EQ(file_rows.size(), 1);
  const auto& file = file_rows[0];
  ASSERT_EQ(file.size(), 12);
  ASSERT_EQ(file[4], AsString(kNumRows));
  ASSERT_EQ(file[11], "complete");
  // How many entries one write becomes is a storage-layout detail, so the chain length is not
  // asserted outright. Every row got the same writes, so all three quantiles must agree, and they
  // must agree with the longest chain the collector recorded independently of the histogram.
  ASSERT_EQ(file[7], Format("$0 / $0 / $0", file[9]));
  ASSERT_GT(ASSERT_RESULT(CheckedStoll(file[9])), 1);
  const auto reclaimable_entries = ASSERT_RESULT(CheckedStoll(file[5]));
  ASSERT_GT(reclaimable_entries, 0);

  const auto total_rows =
      ASSERT_RESULT(path_handlers_util::GetHtmlTableRows(url, "sst_stats_totals"));
  const auto total = [&total_rows](const string& name) -> string {
    for (const auto& row : total_rows) {
      if (row.size() == 2 && row[0] == name) {
        return row[1];
      }
    }
    return "";
  };
  ASSERT_EQ(total("Files measured"), "1 of 1");
  // One measured file, so the merge is the identity over it.
  ASSERT_EQ(total("Row chain length, by rows"), file[7]);
  // The write that shadows an entry is what makes it droppable, and those happened seconds ago, so
  // the youngest band has to account for every reclaimable entry the per-file table reports.
  ASSERT_STR_CONTAINS(
      total("Reclaimable entries by age"), Format("&lt;5m: $0,", reclaimable_entries));
}

class TServerSstStatsPathHandlerNoCollectorItest : public TServerSstStatsPathHandlerItest {
 protected:
  bool CollectorEnabled() override { return false; }
};

TEST_F(TServerSstStatsPathHandlerNoCollectorItest, ReportsFileWithoutStatistics) {
  const auto url = ASSERT_RESULT(WriteOneFileWithGarbage());

  faststring page;
  ASSERT_OK(path_handlers_util::GetUrl(url, &page));
  ASSERT_STR_CONTAINS(page.ToString(), "No live SST file of this tablet carries collector");
  // The page has nothing to merge, so it must not claim a distribution either.
  ASSERT_NOK(path_handlers_util::GetHtmlTableRows(url, "sst_stats_totals"));
}

}  // namespace yb::integration_tests
