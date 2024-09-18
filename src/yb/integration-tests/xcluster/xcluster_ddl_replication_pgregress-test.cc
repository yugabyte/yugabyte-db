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

#include <regex>

#include "yb/client/yb_table_name.h"
#include "yb/common/common_types.pb.h"
#include "yb/integration-tests/xcluster/xcluster_ddl_replication_test_base.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/tools/tools_test_utils.h"
#include "yb/util/env_util.h"
#include "yb/util/tsan_util.h"

DECLARE_int32(ysql_num_tablets);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

// This test runs DDL files taken from pg_regress test files.
// Files have been slightly modified and moved to src/yb/integration-tests/xcluster/sql.
class XClusterPgRegressDDLReplicationTest : public XClusterDDLReplicationTestBase {
 public:
  void SetUp() override {
    // Skip in TSAN since it is slow, disabled in ASAN since there are known memory leaks in
    // ysql_dump inherited from pg_dump.
    YB_SKIP_TEST_IN_SANITIZERS();
    XClusterDDLReplicationTestBase::SetUp();
    // Reduce number of tablets to speed up tests.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_num_tablets) = 1;
    // Disable verbose logging to speed up tests.
    google::SetVLOGLevel("xcluster*", 0);
    google::SetVLOGLevel("add_table*", 0);
    google::SetVLOGLevel("xrepl*", 0);
    google::SetVLOGLevel("cdc*", 0);
  }

  Result<std::string> RunYSQLDump(Cluster& cluster, const std::string& database_name = "yugabyte") {
    const auto output = VERIFY_RESULT(tools::RunYSQLDump(cluster.pg_host_port_, database_name));

    // Filter out any lines in output that contain "binary_upgrade_set_next", since these contain
    // oids which will not match.
    const std::regex pattern("\n.*binary_upgrade_set_next.*\n");
    return std::regex_replace(output, pattern, "\n<binary_upgrade_set_next>\n");
  }

  void ExecutePgFile(const std::string& file_path) {
    std::vector<std::string> args;
    args.push_back(GetPgToolPath("ysqlsh"));
    args.push_back("--host");
    args.push_back(producer_cluster_.pg_host_port_.host());
    args.push_back("--port");
    args.push_back(AsString(producer_cluster_.pg_host_port_.port()));
    args.push_back("-f");
    args.push_back(file_path);

    auto s = CallAdminVec(args);
    LOG(INFO) << "Command output: " << s;
  }

  Status TestPgRegress(const std::string& create_file_name, const std::string& drop_file_name) {
    const auto sub_dir = "test_xcluster_ddl_replication_sql";
    const auto test_sql_dir = JoinPathSegments(env_util::GetRootDir(sub_dir), sub_dir);

    // Setup xCluster.
    RETURN_NOT_OK(SetUpClusters());
    RETURN_NOT_OK(EnableDDLReplicationExtension());
    RETURN_NOT_OK(CheckpointReplicationGroup());
    RETURN_NOT_OK(CreateReplicationFromCheckpoint());

    // First run just the create table part of the file, then run the drop parts.
    for (const auto& file_name : {create_file_name, drop_file_name}) {
      if (file_name.empty()) {
        continue;
      }

      ExecutePgFile(JoinPathSegments(test_sql_dir, file_name));

      RETURN_NOT_OK(PrintDDLQueue(producer_cluster_));
      RETURN_NOT_OK(WaitForSafeTimeToAdvanceToNow());
      RETURN_NOT_OK(PrintDDLQueue(consumer_cluster_));

      auto producer_dump = VERIFY_RESULT(RunYSQLDump(producer_cluster_));
      auto consumer_dump = VERIFY_RESULT(RunYSQLDump(consumer_cluster_));
      SCHECK_EQ(producer_dump, consumer_dump, IllegalState, "Ysqldumps do not match");
    }

    return Status::OK();
  }
};

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropTable) {
  // Tests basic create table commands and table with many columns.
  ASSERT_OK(TestPgRegress("create_table_basic.sql", "drop_table_basic.sql"));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropTable2) {
  // Tests basic create table with different types and if not exists.
  ASSERT_OK(TestPgRegress("create_table_basic2.sql", "drop_table_basic2.sql"));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateTableUnsupported) {
  // Tests unsupported create table commands.
  ASSERT_OK(TestPgRegress("create_table_unsupported.sql", ""));

  // Check no tables were created.
  master::NamespaceIdentifierPB filter;
  filter.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  filter.set_name(namespace_name);
  for (auto client : {producer_client(), consumer_client()}) {
    auto all_tables = ASSERT_RESULT(client->ListUserTables(filter, /* include_indices */ true));
    ASSERT_EQ(all_tables.size(), 0);
  }
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropPartitionedTable) {
  // Tests basic create and drop of partitioned tables.
  ASSERT_OK(TestPgRegress("create_table_partitioned.sql", "drop_table_partitioned.sql"));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropTablePartitions) {
  // Tests basic create and drop of tables with partitions.
  ASSERT_OK(TestPgRegress("create_table_partitions.sql", "drop_table_partitions.sql"));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropTablePartitions2) {
  // Tests basic create and drop of tables with partitions.
  ASSERT_OK(TestPgRegress("create_table_partitions2.sql", "drop_table_partitions2.sql"));
}

}  // namespace yb
