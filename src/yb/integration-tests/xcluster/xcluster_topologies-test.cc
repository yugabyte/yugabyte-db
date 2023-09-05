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
#include "yb/integration-tests/xcluster/xcluster_ycql_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/master/xcluster_consumer_registry_service.h"
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
#include "yb/util/thread.h"

using std::string;

using namespace std::literals;

DECLARE_bool(enable_log_retention_by_op_idx);
DECLARE_bool(enable_ysql);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_int32(cdc_state_checkpoint_update_interval_ms);
DECLARE_int32(cdc_wal_retention_time_secs);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_uint64(log_segment_size_bytes);

namespace yb {

using client::YBClient;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTable;
using client::YBTableName;

using SessionTransactionPair = std::pair<client::YBSessionPtr, client::YBTransactionPtr>;
using YBClusters = std::vector<XClusterTestBase::Cluster*>;

struct AdditionalClusters {
  std::vector<std::unique_ptr<XClusterTestBase::Cluster>> additional_producer_clusters_;
  std::vector<std::unique_ptr<XClusterTestBase::Cluster>> additional_consumer_clusters_;
};

class XClusterTopologiesTest : public XClusterYcqlTestBase {
 public:
  YBClusters producer_clusters_;
  YBClusters consumer_clusters_;
  // Maps cluster ID to a vector of producer tables.
  std::map<std::string, YBTables> producer_tables_;
  // consumer tables in consumer_tables_ are assumed to be in the same order as their corresponding
  // clusters in consumer_clusters_.
  YBTables consumer_tables_;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_table_num_tablets) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_num_shards_per_tserver) = 1;
    XClusterYcqlTestBase::SetUp();
  }

  Status BuildSchemaAndCreateTables(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets) {
    YBSchemaBuilder b;
    b.AddColumn("c0")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
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
      producer_tables_[producer_cluster()->GetClusterId()].push_back(producer_table);

      RETURN_NOT_OK(
          CreateTable(i, num_consumer_tablets[i], consumer_client(), consumer_schema, &tables));
      std::shared_ptr<client::YBTable> consumer_table;
      RETURN_NOT_OK(consumer_client()->OpenTable(tables[(i * 2) + 1], &consumer_table));
      consumer_tables_.push_back(consumer_table);
    }
    return Status::OK();
  }

  Status SetUpWithParams(
      const std::vector<uint32_t>& num_consumer_tablets,
      const std::vector<uint32_t>& num_producer_tablets, uint32_t replication_factor,
      uint32_t num_additional_consumers = 0, uint32_t num_additional_producers = 0,
      uint32_t num_tablets_per_table = 1, uint32_t num_masters = 1, uint32_t num_tservers = 1) {
    SetUp();
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

    for (uint32_t i = 0; i < num_additional_producers; ++i) {
      YBTables tables;
      std::unique_ptr<Cluster> additional_producer_cluster = VERIFY_RESULT(AddClusterWithTables(
          &producer_clusters_, &tables, i, num_producer_tablets.size(), num_tablets_per_table,
          /* is_producer= */ true, num_tservers));
      producer_tables_[additional_producer_cluster->mini_cluster_->GetClusterId()] =
          std::move(tables);
      additional_clusters_.additional_producer_clusters_.push_back(
          std::move(additional_producer_cluster));
    }

    for (uint32_t i = 0; i < num_additional_consumers; ++i) {
      std::unique_ptr<Cluster> additional_consumer_cluster = VERIFY_RESULT(AddClusterWithTables(
          &consumer_clusters_, &consumer_tables_, i, num_consumer_tablets.size(),
          num_tablets_per_table, /* is_producer= */ false, num_tservers));
      additional_clusters_.additional_consumer_clusters_.push_back(
          std::move(additional_consumer_cluster));
    }

    return WaitForLoadBalancersToStabilizeOnAllClusters();
  }

  Status SetupAllUniverseReplication() {
    for (auto& consumer_cluster : consumer_clusters_) {
      YBClient* consumer_cluster_client = consumer_cluster->client_.get();
      MiniCluster* consumer_cluster_mini_cluster = consumer_cluster->mini_cluster_.get();
      master::IsSetupUniverseReplicationDoneResponsePB resp;
      RETURN_NOT_OK(SetupUniverseReplication(
          producer_cluster(), consumer_cluster_mini_cluster, consumer_cluster_client,
          kReplicationGroupId, producer_tables_[producer_cluster()->GetClusterId()]));
      RETURN_NOT_OK(WaitForSetupUniverseReplication(
          consumer_cluster_mini_cluster, consumer_cluster_client, kReplicationGroupId, &resp));
    }
    return Status::OK();
  }

  // expected_fail_consumer_clusters is for running clusters that are not supposed to have
  // successfully written records and we expect DoVerifyWrittenRecords to fail on them.
  // skip_clusters is for clusters that are not running where it would result in an error trying to
  // verify that records were written to them.
  Status VerifyWrittenRecords(
      const std::unordered_set<XClusterTestBase::Cluster*>& expected_fail_consumer_clusters = {},
      const std::unordered_set<XClusterTestBase::Cluster*>& skip_clusters = {},
      int timeout_secs = kRpcTimeout, Cluster* producer_cluster = nullptr) {
    if (!producer_cluster) {
      producer_cluster = &producer_cluster_;
    }
    std::string cluster_id = producer_cluster->mini_cluster_.get()->GetClusterId();
    YBClient* producer_client = producer_cluster->client_.get();

    for (size_t i = 0; i < consumer_clusters_.size(); ++i) {
      for (size_t j = 0; j < producer_tables_[cluster_id].size(); ++j) {
        if (skip_clusters.contains(consumer_clusters_[i])) {
          continue;
        }
        const auto& producer_table = producer_tables_[cluster_id][j];
        const auto& consumer_table =
            consumer_tables_[j + (i * producer_tables_[cluster_id].size())];
        YBClient* client = consumer_clusters_[i]->client_.get();
        Status s = DoVerifyWrittenRecords(
            producer_table->name(), consumer_table->name(), producer_client, client);
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
      for (size_t j = 0; j < producer_tables_[producer_cluster()->GetClusterId()].size(); ++j) {
        const auto& table =
            consumer_tables_[j + (i * producer_tables_[producer_cluster()->GetClusterId()].size())];
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

  Status WriteWorkloadAndVerifyWrittenRows(
      const std::vector<std::shared_ptr<client::YBTable>>& producer_tables, uint32_t start,
      uint32_t end, const std::unordered_set<Cluster*>& expected_fail_consumer_clusters = {},
      const std::unordered_set<XClusterTestBase::Cluster*>& skip_clusters = {},
      int timeout = kRpcTimeout) {
    for (size_t i = 0; i < producer_tables.size(); ++i) {
      const auto& producer_table = producer_tables[i];
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      WriteWorkload(start, end, producer_client(), producer_table->name());
    }
    RETURN_NOT_OK(VerifyWrittenRecords(expected_fail_consumer_clusters, skip_clusters, timeout));
    return Status::OK();
  }

  // Empty stream_ids will pause all streams.
  Status SetPauseAndVerifyWrittenRows(
      const std::vector<xrepl::StreamId>& stream_ids, const YBTables& producer_tables,
      const std::unordered_set<Cluster*>& expected_fail_consumer_clusters,
      const std::unordered_set<XClusterTestBase::Cluster*>& skip_clusters, uint32_t start,
      uint32_t end, bool pause = true) {
    RETURN_NOT_OK(PauseResumeXClusterProducerStreams(stream_ids, pause));
    // Needs to sleep to wait for heartbeat to propagate.
    SleepFor(3s * kTimeMultiplier);

    // Reduce the timeout time when we don't expect replication to be successful.
    int timeout = pause ? 10 : kRpcTimeout;

    // If stream_ids is empty, then write and test replication on all streams, otherwise write and
    // test on only those selected in stream_ids.
    size_t size = stream_ids.size() ? stream_ids.size() : producer_tables.size();
    for (size_t i = 0; i < size; i++) {
      const auto& producer_table = producer_tables[i];
      LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
      WriteWorkload(start, end, producer_client(), producer_table->name());
    }
    RETURN_NOT_OK(VerifyWrittenRecords(expected_fail_consumer_clusters, skip_clusters, timeout));
    return Status::OK();
  }

  Status WaitForLoadBalancersToStabilizeOnAllClusters() {
    for (const auto& producer_cluster : producer_clusters_) {
      auto cluster = producer_cluster->mini_cluster_.get();
      RETURN_NOT_OK(WaitForLoadBalancerToStabilize(cluster));
    }
    for (const auto& consumer_cluster : consumer_clusters_) {
      auto cluster = consumer_cluster->mini_cluster_.get();
      RETURN_NOT_OK(WaitForLoadBalancerToStabilize(cluster));
    }
    return Status::OK();
  }

  AdditionalClusters additional_clusters_;
};

// Testing that a 1:3 replication setup works.
TEST_F(XClusterTopologiesTest, TestBasicBroadcastTopology) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 3;
  uint32_t kReplicationFactor = NonTsanVsTsan(3, 1);
  std::vector<uint32_t> tables_vector(kNumTables, kNTabletsPerTable);
  ASSERT_OK(SetUpWithParams(
      tables_vector, tables_vector, kReplicationFactor, /* num_additional_consumers= */ 2,
      /* num_additional_producers= */ 0, kNTabletsPerTable));
  ASSERT_OK(SetupAllUniverseReplication());
  for (int i = 0; i < kNumTables; ++i) {
    const auto& producer_table = producer_tables_[producer_cluster()->GetClusterId()][i];
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    WriteWorkload(0, 10, producer_client(), producer_table->name());
  }

  ASSERT_OK(VerifyWrittenRecords());
}

// Testing that a 2:1 replication setup fails.
TEST_F(XClusterTopologiesTest, TestNToOneReplicationFails) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 3;
  uint32_t kReplicationFactor = NonTsanVsTsan(3, 1);
  std::vector<uint32_t> tables_vector(kNumTables, kNTabletsPerTable);
  ASSERT_OK(SetUpWithParams(
      tables_vector, tables_vector, kReplicationFactor, 0 /* num additional consumers */,
      1 /* num additional producers */, kNTabletsPerTable));

  for (size_t i = 0; i < producer_clusters_.size(); ++i) {
    MiniCluster* producer_cluster_mini_cluster = producer_clusters_[i]->mini_cluster_.get();
    const cdc::ReplicationGroupId replication_group_id(Format("$0$1", kReplicationGroupId, i));
    master::IsSetupUniverseReplicationDoneResponsePB setup_resp;
    master::GetUniverseReplicationResponsePB verify_resp;
    if (i == 0) {
      ASSERT_OK(SetupUniverseReplication(
          producer_cluster_mini_cluster, consumer_cluster(), consumer_client(),
          replication_group_id, producer_tables_[producer_cluster_mini_cluster->GetClusterId()]));
    } else {
      ASSERT_NOK(SetupUniverseReplication(
          producer_cluster_mini_cluster, consumer_cluster(), consumer_client(),
          replication_group_id, producer_tables_[producer_cluster_mini_cluster->GetClusterId()]));
    }
  }
}

class XClusterTopologiesTestClusterFailure : public XClusterTopologiesTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_segment_size_bytes) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_log_retention_by_op_idx) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 100;
    XClusterTopologiesTest::SetUp();
  }

  // Call this after SetupAllUniverseReplication() -- replication setup does not work with only op
  // id retention due to delays getting stream checkpoints to WAL retention code
  void SwitchToRetainOnlyByOpId() {
    // Ensure initial checkpoint from each stream is available to WAL retention code:
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_cdc_state_checkpoint_update_interval_ms * 10));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_wal_retention_time_secs) = 1;
  }
};

// Testing that 1:N replication still works even when a consumer drops.
TEST_F(XClusterTopologiesTestClusterFailure, TestBroadcastWithConsumerFailure) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 3;
  uint32_t kReplicationFactor = NonTsanVsTsan(3, 1);
  std::vector<uint32_t> tables_vector(kNumTables, kNTabletsPerTable);
  ASSERT_OK(SetUpWithParams(
      tables_vector, tables_vector, kReplicationFactor, /* num_additional_consumers= */ 2,
      /* num_additional_producers= */ 0, 0 /*num_additional_producers*/, kNTabletsPerTable));
  ASSERT_OK(SetupAllUniverseReplication());
  SwitchToRetainOnlyByOpId();

  const auto& producer_tables = producer_tables_[producer_cluster()->GetClusterId()];
  ASSERT_OK(WriteWorkloadAndVerifyWrittenRows(producer_tables, 0, 10));
  Cluster* down_cluster = consumer_clusters_[0];
  {
    LOG(INFO) << ">>>>> Bring down the first consumer cluster.";
    TEST_SetThreadPrefixScoped prefix_se("C");
    down_cluster->mini_cluster_.get()->StopSync();

    LOG(INFO) << ">>>>> Write some records to ensure that replication is still ongoing for the "
                 "remaining clusters.";
    ASSERT_OK(WriteWorkloadAndVerifyWrittenRows(producer_tables, 10, 100, {}, {down_cluster}));
    SleepFor(MonoDelta::FromSeconds(5));

    LOG(INFO) << ">>>>> Bring the consumer cluster back up.";
    ASSERT_OK(down_cluster->mini_cluster_.get()->StartSync());
  }
  LOG(INFO) << ">>>>> Verify that consumer cluster replication catches up.";
  ASSERT_OK(VerifyWrittenRecords());

  LOG(INFO) << ">>>>> Write workload and verify written rows.";
  ASSERT_OK(WriteWorkloadAndVerifyWrittenRows(producer_tables, 100, 110));
  LOG(INFO) << ">>>>> Successful!  Tearing down cluster now...";
}

// Testing that 1:N replication still works even when a producer drops.
TEST_F(XClusterTopologiesTestClusterFailure, TestBroadcastWithProducerFailure) {
  constexpr int kNTabletsPerTable = 3;
  constexpr int kNumTables = 3;
  uint32_t kReplicationFactor = NonTsanVsTsan(3, 1);
  std::vector<uint32_t> tables_vector(kNumTables, kNTabletsPerTable);
  ASSERT_OK(SetUpWithParams(
      tables_vector, tables_vector, kReplicationFactor, /* num_additional_consumers= */ 2,
      /* num_additional_producers= */ 0, kNTabletsPerTable));
  ASSERT_OK(SetupAllUniverseReplication());
  SwitchToRetainOnlyByOpId();

  const auto& producer_tables = producer_tables_[producer_cluster()->GetClusterId()];
  ASSERT_OK(WriteWorkloadAndVerifyWrittenRows(producer_tables, 0, 10));
  {
    // Stop the producer cluster and sleep.
    TEST_SetThreadPrefixScoped prefix_se("P");
    producer_cluster()->StopSync();
    SleepFor(MonoDelta::FromSeconds(5));

    // Bring the producer back up.
    ASSERT_OK(producer_cluster()->StartSync());
  }

  // Verify that replication is still working.
  ASSERT_OK(WriteWorkloadAndVerifyWrittenRows(producer_tables, 10, 100));
}

}  // namespace yb
