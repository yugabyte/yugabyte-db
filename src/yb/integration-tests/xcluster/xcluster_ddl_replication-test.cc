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

#include "yb/cdc/xcluster_types.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/common_types.pb.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/util/backoff_waiter.h"

DECLARE_bool(TEST_enable_xcluster_api_v2);
DECLARE_bool(TEST_xcluster_enable_ddl_replication);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDDLReplicationTest : public XClusterYsqlTestBase {
 public:
  struct SetupParams {
    // By default start with no consumer or producer tables.
    std::vector<uint32_t> num_consumer_tablets = {};
    std::vector<uint32_t> num_producer_tablets = {};
    uint32_t replication_factor = 3;
    uint32_t num_masters = 1;
    bool ranged_partitioned = false;
  };

  XClusterDDLReplicationTest() = default;
  ~XClusterDDLReplicationTest() = default;

  virtual void SetUp() override {
    XClusterYsqlTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_xcluster_api_v2) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_enable_ddl_replication) = true;
  }

  Status SetUpClusters() {
    static const SetupParams kDefaultParams;
    return SetUpClusters(kDefaultParams);
  }

  Status SetUpClusters(const SetupParams& params) {
    return XClusterYsqlTestBase::SetUpWithParams(
        params.num_consumer_tablets, params.num_producer_tablets, params.replication_factor,
        params.num_masters, params.ranged_partitioned);
  }

  Status EnableDDLReplicationExtension() {
    // TODO(#19184): This will be done as part of creating the replication groups.
    auto p_conn = VERIFY_RESULT(producer_cluster_.Connect());
    RETURN_NOT_OK(p_conn.ExecuteFormat("CREATE EXTENSION $0", xcluster::kDDLQueuePgSchemaName));
    RETURN_NOT_OK(p_conn.ExecuteFormat(
        "ALTER DATABASE $0 SET $1.replication_role = SOURCE", namespace_name,
        xcluster::kDDLQueuePgSchemaName));
    auto c_conn = VERIFY_RESULT(consumer_cluster_.Connect());
    RETURN_NOT_OK(c_conn.ExecuteFormat("CREATE EXTENSION $0", xcluster::kDDLQueuePgSchemaName));
    return c_conn.ExecuteFormat(
        "ALTER DATABASE $0 SET $1.replication_role = TARGET", namespace_name,
        xcluster::kDDLQueuePgSchemaName);
  }
};

TEST_F(XClusterDDLReplicationTest, CreateTable) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(EnableDDLReplicationExtension());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table;
  ASSERT_OK(producer_client()->OpenTable(producer_table_name, &producer_table));

  ASSERT_OK(InsertRowsInProducer(0, 50, producer_table));

  // Once the safe time advances, we should have the new table and its rows.
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto consumer_table_name = ASSERT_RESULT(GetYsqlTable(
      &consumer_cluster_, producer_table_name.namespace_name(), producer_table_name.pgschema_name(),
      producer_table_name.table_name()));
  std::shared_ptr<client::YBTable> consumer_table;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table_name, &consumer_table));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 2);

  ASSERT_OK(VerifyWrittenRecords(producer_table, consumer_table));
}

}  // namespace yb
