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

#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"

DECLARE_bool(TEST_enable_xcluster_api_v2);
DECLARE_string(certs_for_cdc_dir);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDBScopedTest : public XClusterYsqlTestBase {
 public:
  XClusterDBScopedTest() = default;
  ~XClusterDBScopedTest() = default;

  virtual void SetUp() override {
    XClusterYsqlTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_xcluster_api_v2) = true;
  }
};

TEST_F(XClusterDBScopedTest, TestCreateWithCheckpoint) {
  ASSERT_OK(SetUpClusters());

  ASSERT_OK(CheckpointReplicationGroup());

  ASSERT_NOK(CreateReplicationFromCheckpoint("bad-master-addr"));

  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

  ASSERT_OK(InsertRowsInProducer(0, 50));

  ASSERT_OK(VerifyWrittenRecords());
}

TEST_F(XClusterDBScopedTest, CreateTable) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Creating a new table on target first should fail.
  auto status = CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Table public.test_table_1 not found");

  auto new_producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> new_producer_table;
  ASSERT_OK(producer_client()->OpenTable(new_producer_table_name, &new_producer_table));

  ASSERT_OK(InsertRowsInProducer(0, 50, new_producer_table));

  auto new_consumer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> new_consumer_table;
  ASSERT_OK(consumer_client()->OpenTable(new_consumer_table_name, &new_consumer_table));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 2);

  ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(VerifyWrittenRecords());

  // Make sure the other table remains unchanged.
  ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));
}

TEST_F(XClusterDBScopedTest, DropTableOnProducerThenConsumer) {
  // Setup replication with two tables
  SetupParams params;
  params.num_consumer_tablets = params.num_producer_tablets = {3, 3};
  ASSERT_OK(SetUpClusters(params));

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Perform the drop on producer cluster.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));

  // Perform the drop on consumer cluster.
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  std::promise<Result<master::GetXClusterStreamsResponsePB>> promise;
  client::XClusterClient remote_client(*producer_client());
  auto outbound_table_info = remote_client.GetXClusterStreams(
      CoarseMonoClock::Now() + kTimeout, kReplicationGroupId, namespace_id,
      {producer_table_->name().table_name()}, {producer_table_->name().pgschema_name()},
      [&promise](Result<master::GetXClusterStreamsResponsePB> result) {
        promise.set_value(std::move(result));
      });
  auto result = promise.get_future().get();
  ASSERT_NOK(result) << result->DebugString();
  ASSERT_STR_CONTAINS(result.status().ToString(), "test_table_0 not found in namespace");
}

}  // namespace yb
