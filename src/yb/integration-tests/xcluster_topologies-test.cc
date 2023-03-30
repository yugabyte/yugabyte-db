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
#include <map>
#include <string>
#include <utility>
#include <chrono>
#include <boost/assign.hpp>
#include "yb/util/flags.h"
// #include <gtest/gtest.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/consensus/log.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/xcluster_test_base.h"
#include "yb/integration-tests/xcluster_ycql_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master-test-util.h"

#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/faststring.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"

using std::string;

using namespace std::literals;

DECLARE_bool(enable_ysql);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_int32(yb_num_shards_per_tserver);

namespace yb {

using client::YBClient;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableName;

using SessionTransactionPair = std::pair<client::YBSessionPtr, client::YBTransactionPtr>;
using YBTables = std::vector<std::shared_ptr<client::YBTable>>;
using YBClusters = std::vector<XClusterTestBase::Cluster*>;

class XClusterTopologiesTest : public XClusterYcqlTestBase {
 public:
  YBClusters producer_clusters_;
  YBClusters consumer_clusters_;
  YBTables producer_tables_;
  // consumer tables in consumer_tables_ are assumed to be in the same order as their corresponding
  // clusters in consumer_clusters_.
  YBTables consumer_tables_;

  Status BuildSchemaAndCreateTables(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets) {
    YBSchemaBuilder b;
    b.AddColumn("c0")->Type(INT32)->NotNull()->HashPrimaryKey();
    // Create transactional table.
    TableProperties table_properties;
    b.SetTableProperties(table_properties);
    CHECK_OK(b.Build(GetSchema()));
    YBSchema consumer_schema;
    table_properties.SetDefaultTimeToLive(0);
    b.SetTableProperties(table_properties);
    CHECK_OK(b.Build(&consumer_schema));
    SCHECK_EQ(
        num_consumer_tablets.size(), num_producer_tablets.size(), IllegalState,
        Format(
            "Num consumer tables: $0 num producer tables: $1 must be equal.",
            num_consumer_tablets.size(), num_producer_tablets.size()));
    std::vector<YBTableName> tables;
    for (uint32_t i = 0; i < num_consumer_tablets.size(); i++) {
      RETURN_NOT_OK(CreateTable(i, num_producer_tablets[i], producer_client(), &tables));

      std::shared_ptr<client::YBTable> producer_table;
      RETURN_NOT_OK(producer_client()->OpenTable(tables[i * 2], &producer_table));
      producer_tables_.push_back(producer_table);

      RETURN_NOT_OK(
          CreateTable(i, num_consumer_tablets[i], consumer_client(), consumer_schema, &tables));
      std::shared_ptr<client::YBTable> consumer_table;
      RETURN_NOT_OK(consumer_client()->OpenTable(tables[(i * 2) + 1], &consumer_table));
      consumer_tables_.push_back(consumer_table);
    }
    return Status::OK();
  }

  Result<std::vector<std::unique_ptr<Cluster>>> SetUpWithParams(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets,
      uint32_t replication_factor,
      uint32_t num_additional_consumers = 0,
      uint32_t num_tablets_per_table = 1,
      uint32_t num_masters = 1,
      uint32_t num_tservers = 1) {
    FLAGS_enable_ysql = false;
    FLAGS_transaction_table_num_tablets = 1;
    XClusterYcqlTestBase::SetUp();
    FLAGS_yb_num_shards_per_tserver = 1;
    num_tservers = std::max(num_tservers, replication_factor);
    MiniClusterOptions opts;
    opts.num_tablet_servers = num_tservers;
    opts.num_masters = num_masters;
    opts.transaction_table_num_tablets = FLAGS_transaction_table_num_tablets;
    RETURN_NOT_OK(InitClusters(opts));
    RETURN_NOT_OK(GetClock()->Init());
    producer_cluster_.txn_mgr_.emplace(producer_client(), GetClock(), client::LocalTabletFilter());
    consumer_cluster_.txn_mgr_.emplace(consumer_client(), GetClock(), client::LocalTabletFilter());
    producer_clusters_.push_back(&producer_cluster_);
    consumer_clusters_.push_back(&consumer_cluster_);

    RETURN_NOT_OK(BuildSchemaAndCreateTables(num_consumer_tablets, num_producer_tablets));

    std::vector<std::unique_ptr<Cluster>> additional_consumer_clusters;
    for (uint32_t i = 0; i < num_additional_consumers; ++i) {
      std::unique_ptr<Cluster> additional_consumer_cluster =
          VERIFY_RESULT(AddConsumerClusterWithTables(
              &consumer_clusters_, &consumer_tables_, Format("additional_consumer_$0", i),
              num_consumer_tablets.size(), num_tablets_per_table, num_tservers));
      additional_consumer_clusters.push_back(std::move(additional_consumer_cluster));
    }
    RETURN_NOT_OK(WaitForLoadBalancersToStabilizeOnAllClusters());

    return additional_consumer_clusters;
  }

  Status SetupAllUniverseReplication() {
    for (auto& consumer_cluster : consumer_clusters_) {
      YBClient* consumer_cluster_client = consumer_cluster->client_.get();
      MiniCluster* consumer_cluster_mini_cluster = consumer_cluster->mini_cluster_.get();
      master::IsSetupUniverseReplicationDoneResponsePB resp;
      RETURN_NOT_OK(SetupUniverseReplication(
          producer_cluster(), consumer_cluster_mini_cluster, consumer_cluster_client, kUniverseId,
          producer_tables_));
      RETURN_NOT_OK(WaitForSetupUniverseReplication(
          consumer_cluster_mini_cluster, consumer_cluster_client, kUniverseId, &resp));
    }
    return Status::OK();
  }

  Status VerifyWrittenRecords(
      const std::unordered_set<XClusterTestBase::Cluster*>& expected_fail_consumer_clusters,
      int timeout_secs = kRpcTimeout,
      YBClient* prod_client = nullptr) {
    if (!prod_client) {
      prod_client = producer_client();
    }

    for (size_t i = 0; i < consumer_clusters_.size(); ++i) {
      for (size_t j = 0; j < producer_tables_.size(); ++j) {
        const auto& producer_table = producer_tables_[j];
        const auto& consumer_table = consumer_tables_[j + (i * producer_tables_.size())];
        YBClient* client = consumer_clusters_[i]->client_.get();
        Status s = DoVerifyWrittenRecords(
            producer_table->name(), consumer_table->name(), prod_client, client);
        if (!expected_fail_consumer_clusters.contains(consumer_clusters_[i]) && !s.ok()) {
          return s;
        } else if (expected_fail_consumer_clusters.contains(consumer_clusters_[i]) && s.ok()) {
          return STATUS(
              IllegalState, Format("Expected verification to fail for table: $0", consumer_table));
        }
      }
    }
    return Status::OK();
  }

  Status VerifyNumRecords(
      const std::unordered_set<XClusterTestBase::Cluster*>& expected_fail_clusters,
      size_t expected_size) {
    for (size_t i = 0; i < consumer_clusters_.size(); ++i) {
      for (size_t j = 0; j < producer_tables_.size(); ++j) {
        const auto& table = consumer_tables_[j + (i * producer_tables_.size())];
        YBClient* client = consumer_clusters_[i]->client_.get();
        Status s = DoVerifyNumRecords(table->name(), client, expected_size);
        if (!expected_fail_clusters.contains(consumer_clusters_[i]) && !s.ok()) {
          return s;
        } else if (expected_fail_clusters.contains(consumer_clusters_[i]) && s.ok()) {
          return STATUS(
              IllegalState,
              Format("Expected table $0 to not have $1 records", table, expected_size));
        }
      }
    }
    return Status::OK();
  }

  void WriteWorkloadAndVerifyWrittenRows(
      const std::shared_ptr<client::YBTable>& producer_table,
      const std::unordered_set<Cluster*>& expected_fail_consumer_clusters, uint32_t start,
      uint32_t end, int timeout = kRpcTimeout) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(start, end, producer_client(), producer_table->name());
    ASSERT_OK(VerifyWrittenRecords(expected_fail_consumer_clusters, timeout));
  }

  // Empty stream_ids will pause all streams.
  void SetPauseAndVerifyWrittenRows(
      const std::vector<std::string>& stream_ids, const YBTables& producer_tables,
      const std::unordered_set<Cluster*>& expected_fail_consumer_clusters, uint32_t start,
      uint32_t end, bool pause = true) {
    ASSERT_OK(PauseResumeXClusterProducerStreams(stream_ids, pause));
    // Needs to sleep to wait for heartbeat to propogate.
    SleepFor(3s * kTimeMultiplier);

    // If stream_ids is empty, then write and test replication on all streams, otherwise write and
    // test on only those selected in stream_ids.
    size_t size = stream_ids.size() ? stream_ids.size() : producer_tables.size();
    for (size_t i = 0; i < size; i++) {
      const auto& producer_table = producer_tables[i];
      // Reduce the timeout time when we don't expect replication to be successful.
      int timeout = pause ? 10 : kRpcTimeout;
      WriteWorkloadAndVerifyWrittenRows(
          producer_table, expected_fail_consumer_clusters, start, end, timeout);
    }
  }

  Status WaitForLoadBalancersToStabilizeOnAllClusters() {
    for (const auto& producer_cluster : producer_clusters_) {
      auto cluster = producer_cluster->mini_cluster_.get();
      RETURN_NOT_OK(WaitForLoadBalancersToStabilize(cluster));
    }
    for (const auto& consumer_cluster : consumer_clusters_) {
      auto cluster = consumer_cluster->mini_cluster_.get();
      RETURN_NOT_OK(WaitForLoadBalancersToStabilize(cluster));
    }
    return Status::OK();
  }
};

// Testing that a 1:3 replication setup works.
TEST_F(XClusterTopologiesTest, TestBasicBroadcastTopology) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 3;
  uint32_t kReplicationFactor = NonTsanVsTsan(3, 1);
  std::vector<uint32_t> tables_vector(kNumTables, kNTabletsPerTable);
  auto additional_consumer_clusters = ASSERT_RESULT(SetUpWithParams(
      tables_vector, tables_vector, kReplicationFactor, 2 /* num additional consumers */,
      kNTabletsPerTable));
  ASSERT_OK(SetupAllUniverseReplication());
  for (int i = 0; i < kNumTables; ++i) {
    const auto& producer_table = producer_tables_[i];
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 10, producer_client(), producer_table->name());
  }

  ASSERT_OK(VerifyWrittenRecords({}));
}

}  // namespace yb
