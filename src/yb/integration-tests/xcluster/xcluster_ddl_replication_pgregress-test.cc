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

#include <boost/regex.hpp>

#include "yb/client/yb_table_name.h"
#include "yb/common/common_types.pb.h"
#include "yb/integration-tests/xcluster/xcluster_ddl_replication_test_base.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/tools/tools_test_utils.h"
#include "yb/util/env_util.h"
#include "yb/util/tsan_util.h"

DECLARE_string(ysql_catalog_preload_additional_table_list);
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

    // Some of the scripts do take a long time to run so setting this timeout high.
    propagation_timeout_ = MonoDelta::FromMinutes(4 * kTimeMultiplier);
  }

  Result<std::string> RunYSQLDataOnlyDump(Cluster& cluster) {
    const auto output =
        VERIFY_RESULT(tools::RunYSQLDataOnlyDump(cluster.pg_host_port_, namespace_name));
    // Filter out any line corresponding to the replicated_ddls commit_time row since this only
    // exists on the target.
    const boost::regex pattern("\n1\t1\t\\{\"commit_times\": .*$");
    return boost::regex_replace(
        output, pattern, "", boost::format_first_only | boost::match_not_dot_newline);
  }

  Result<std::string> RunYSQLDump(Cluster& cluster) { return RunYSQLDump(cluster, namespace_name); }

  Result<std::string> RunYSQLDump(Cluster& cluster, const std::string& database_name) {
    const auto output = VERIFY_RESULT(tools::RunYSQLDump(cluster.pg_host_port_, database_name));

    // Filter out any lines in output that contain "binary_upgrade_set_next", since these contain
    // oids which may not match.
    const boost::regex pattern("^SELECT pg_catalog\\.binary_upgrade_set_next.*$");
    auto replaced = boost::regex_replace(
        output, pattern, "<binary_upgrade_set_next>", boost::match_not_dot_newline);

    return replaced;
  }

  void ExpectEqModuloSequenceValues(
      const std::string& producer_dump, const std::string& consumer_dump) {
    const boost::regex pattern("^SELECT pg_catalog\\.setval.*$", boost::match_not_dot_newline);

    auto producer = boost::regex_replace(producer_dump, pattern, "<setval>");
    auto consumer = boost::regex_replace(consumer_dump, pattern, "<setval>");

    if (producer == consumer) {
      return;
    }

    ADD_FAILURE()
        << "Expected the ysql_dump's of both sides to be the same ignoring sequence states and "
           "OIDs";
    LOG(INFO) << "producer side dump: " << producer;
    LOG(INFO) << "consumer side dump: " << consumer;
  }

  Result<std::string> ReadEnumLabelInfo(Cluster& cluster) {
    return ReadEnumLabelInfo(cluster, namespace_name);
  }

  Result<std::string> ReadEnumLabelInfo(Cluster& cluster, const std::string& database_name) {
    auto conn = VERIFY_RESULT(cluster.ConnectToDB(database_name));
    return VERIFY_RESULT(conn.FetchAllAsString(
        "SELECT typname, enumlabel, pg_enum.oid, enumsortorder FROM pg_enum "
        "JOIN pg_type ON pg_enum.enumtypid = pg_type.oid ORDER BY typname, enumlabel ASC;",
        ", ", "\n"));
  }

  Result<std::string> ReadTypeInfoThatMustMatch(Cluster& cluster) {
    auto conn = VERIFY_RESULT(cluster.ConnectToDB(namespace_name));
    return VERIFY_RESULT(conn.FetchAllAsString(R"(
        SELECT
            n.nspname,
            t.typname,
            t.oid,
            t.typtype::text AS typtype_str,
            c.relkind::text AS relkind_str
        FROM pg_type t
          LEFT JOIN pg_class c     ON t.typrelid     = c.oid
          LEFT JOIN pg_namespace n ON t.typnamespace = n.oid
        WHERE
              t.typtype = 'e'                              -- enum type
          OR (t.typtype = 'c' AND c.relkind = 'c')         -- user-defined composite type
          OR  t.typtype = 'r'                              -- single range type
          OR (t.typtype = 'b' AND NOT t.typcategory = 'A') -- base type that is not an array
          OR (t.typtype = 'p' AND NOT t.typisdefined)      -- shell type
        ORDER BY t.typnamespace ASC, t.typname ASC;
      )", ", ", "\n"));
  }

  Result<std::string> ReadSequenceOidInfo(Cluster& cluster) {
    auto conn = VERIFY_RESULT(cluster.ConnectToDB(namespace_name));
    return VERIFY_RESULT(conn.FetchAllAsString(
        "SELECT pg_namespace.nspname, pg_class.relname, pg_class.oid "
        "FROM pg_class "
        "JOIN pg_namespace ON pg_class.relnamespace = pg_namespace.oid "
        "WHERE pg_class.relkind = 'S' "
        "ORDER BY pg_namespace.nspname ASC, pg_class.relname ASC;",
        ", ", "\n"));
  }

  void ExpectEqOidsNeedingPreservation(Cluster& consumer_cluster, Cluster& producer_cluster) {
    // pg_enum OIDs.
    auto producer_enum_label_info = ASSERT_RESULT(ReadEnumLabelInfo(producer_cluster));
    auto consumer_enum_label_info = ASSERT_RESULT(ReadEnumLabelInfo(consumer_cluster));
    ASSERT_EQ(producer_enum_label_info, consumer_enum_label_info)
        << "enum label OID information does not match";
    LOG(INFO) << "pg_enum OIDs on both sides are:\n" << producer_enum_label_info;

    // Sequence pg_class OIDs.
    auto producer_sequence_info = ASSERT_RESULT(ReadSequenceOidInfo(producer_cluster));
    auto consumer_sequence_info = ASSERT_RESULT(ReadSequenceOidInfo(consumer_cluster));
    ASSERT_EQ(producer_sequence_info, consumer_sequence_info)
        << "sequence OID information does not match";
    LOG(INFO) << "Sequence pg_class OIDs on both sides are:\n" << producer_sequence_info;

    // pg_type OIDs.
    auto producer_type_info = ASSERT_RESULT(ReadTypeInfoThatMustMatch(producer_cluster));
    auto consumer_type_info = ASSERT_RESULT(ReadTypeInfoThatMustMatch(consumer_cluster));
    ASSERT_EQ(producer_type_info, consumer_type_info)
        << "type OID information that must match does not match";
    LOG(INFO) << "pg_type OIDs that must match on both sides are:\n" << producer_type_info;
  }

  void ExecutePgFile(const std::string& file_path) { ExecutePgFile(file_path, namespace_name); }

  void ExecutePgFile(const std::string& file_path, const std::string& database_name) {
    std::vector<std::string> args;
    args.push_back(GetPgToolPath("ysqlsh"));
    args.push_back("--host");
    args.push_back(producer_cluster_.pg_host_port_.host());
    args.push_back("--port");
    args.push_back(AsString(producer_cluster_.pg_host_port_.port()));
    // Fail the script on the first error.
    args.push_back("--variable=ON_ERROR_STOP=1");
    args.push_back("-f");
    args.push_back(file_path);
    args.push_back("-d");
    args.push_back(database_name);

    auto s = CallAdminVec(args);
    LOG(INFO) << "Command output: " << s;

    // Assert that the script executed without any errors.
    ASSERT_OK(s);
  }

  Status TestPgRegress(
      const std::vector<std::string>& file_names, const std::string& pre_execution_sql_text = "",
      bool check_data_only = false) {
    const auto sub_dir = "test_xcluster_ddl_replication_sql";
    const auto test_sql_dir = JoinPathSegments(env_util::GetRootDir(sub_dir), sub_dir, "sql");

    auto params = XClusterDDLReplicationTestBase::kDefaultParams;
    params.is_colocated = is_colocated_;
    RETURN_NOT_OK(SetUpClusters(params));

    if (!pre_execution_sql_text.empty()) {
      RETURN_NOT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
        auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
        RETURN_NOT_OK(conn.Execute(pre_execution_sql_text));
        return Status::OK();
      }));
    }

    // Perturb OIDs on producer side to make sure we don't accidentally preserve OIDs.
    auto conn = VERIFY_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    RETURN_NOT_OK(
        conn.Execute("CREATE TYPE gratuitous_enum AS ENUM ('red', 'orange', 'yellow', 'green', "
                     "'blue', 'purple');"));
    RETURN_NOT_OK(conn.Execute("DROP TYPE gratuitous_enum;"));

    // Setup xCluster.
    RETURN_NOT_OK(
        CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
    // Bootstrap here would have no effect because the database is empty so we skip it for the test.
    RETURN_NOT_OK(CreateReplicationFromCheckpoint());

    // First run just the create part of the file, then run the drop parts.
    std::string initial_dump;
    std::string initial_data_dump;
    for (const auto& file_name : file_names) {
      ExecutePgFile(JoinPathSegments(test_sql_dir, file_name));

      RETURN_NOT_OK(PrintDDLQueue(producer_cluster_));
      RETURN_NOT_OK(WaitForSafeTimeToAdvanceToNow());
      RETURN_NOT_OK(PrintDDLQueue(consumer_cluster_));

      if (check_data_only) {
        auto producer_data_dump = VERIFY_RESULT(RunYSQLDataOnlyDump(producer_cluster_));
        auto consumer_data_dump = VERIFY_RESULT(RunYSQLDataOnlyDump(consumer_cluster_));

        SCHECK_EQ(
            producer_data_dump, consumer_data_dump, IllegalState,
            "Data between the two clusters does not match");

        if (initial_data_dump.empty()) {
          initial_data_dump = producer_data_dump;
        } else {
          // Check to ensure that the test is working properly.
          SCHECK_NE(
              initial_data_dump, producer_data_dump, IllegalState,
              "YSQLDataOnlyDump after running scripts should not match the initial dump");
        }
        continue;
      }

      // Verify that the DDLs were replicated correctly.
      auto producer_dump = VERIFY_RESULT(RunYSQLDump(producer_cluster_));
      auto consumer_dump = VERIFY_RESULT(RunYSQLDump(consumer_cluster_));

      //
      // Check for equivalence of the two sides in steps of increasing strictness
      //

      // First, do we just have the same objects in the two sides?
      ExpectEqModuloSequenceValues(producer_dump, consumer_dump);

      // Second, are the OIDs that we need to match the same on both sides?
      ExpectEqOidsNeedingPreservation(producer_cluster_, consumer_cluster_);

      // Finally, do the sequence states (e.g., current values) match as well?
      SCHECK_EQ(
          producer_dump, consumer_dump, IllegalState,
          "Ysqldumps including sequence state do not match");

      // Ensure that the dump is not empty, should at least contain the extension.
      if (initial_dump.empty()) {
        initial_dump = producer_dump;
      } else {
        // Check to ensure that the test is working properly.
        SCHECK_NE(
            initial_dump, producer_dump, IllegalState,
            "Ysqldumps after running the scripts should not match the initial dump");
      }
    }

    return Status::OK();
  }

  Status VerifyDataMatch() {
    auto producer_data_dump = VERIFY_RESULT(RunYSQLDataOnlyDump(producer_cluster_));
    auto consumer_data_dump = VERIFY_RESULT(RunYSQLDataOnlyDump(consumer_cluster_));

    SCHECK_EQ(
        producer_data_dump, consumer_data_dump, IllegalState,
        "Data between the two clusters does not match");
    return Status::OK();
  }

  bool is_colocated_ = false;
};

YB_STRONGLY_TYPED_BOOL(UseColocated);

class XClusterPgRegressDDLReplicationParamTest : public XClusterPgRegressDDLReplicationTest,
                                                 public testing::WithParamInterface<UseColocated> {
  void SetUp() override {
    is_colocated_ = GetParam();
    XClusterPgRegressDDLReplicationTest::SetUp();
  }
};

INSTANTIATE_TEST_SUITE_P(
    , XClusterPgRegressDDLReplicationParamTest, ::testing::Values(UseColocated::kFalse));
INSTANTIATE_TEST_SUITE_P(
    UseColocated, XClusterPgRegressDDLReplicationParamTest, ::testing::Values(UseColocated::kTrue));

TEST_P(XClusterPgRegressDDLReplicationParamTest, PgRegressCreateDropTable) {
  // Tests basic create table commands and table with many columns.
  ASSERT_OK(TestPgRegress({"create_table_basic.sql", "drop_table_basic.sql"}));
}

TEST_P(XClusterPgRegressDDLReplicationParamTest, PgRegressCreateDropTable2) {
  // Tests basic create table with different types and if not exists.
  ASSERT_OK(TestPgRegress({"create_table_basic2.sql", "drop_table_basic2.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateTableUnsupported) {
  // Tests unsupported create table commands.
  ASSERT_OK(TestPgRegress({"create_table_unsupported.sql"}));

  // Check no tables were created.
  master::NamespaceIdentifierPB filter;
  filter.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  filter.set_name(namespace_name);
  for (auto client : {producer_client(), consumer_client()}) {
    auto all_tables = ASSERT_RESULT(client->ListUserTables(filter, /* include_indices */ true));
    // Only expect to end up with ddl_queue and replicated_ddls tables.
    ASSERT_EQ(all_tables.size(), 2);
    for (const auto& table : all_tables) {
      ASSERT_TRUE(table.pgschema_name() == xcluster::kDDLQueuePgSchemaName)
          << "Unexpected pgschema_name: " << table.pgschema_name();
      ASSERT_TRUE(table.table_name() == "ddl_queue" || table.table_name() == "replicated_ddls")
          << "Unexpected table_name: " << table.table_name();
    }
  }
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropPartitionedTable) {
  // Tests basic create and drop of partitioned tables.
  // Need to prefetch pg_operator to avoid DFATAL in pg systable prefetch. See GHI #25639.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_table_list) = "pg_operator";
  ASSERT_OK(TestPgRegress({"create_table_partitioned.sql", "drop_table_partitioned.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropTablePartitions) {
  // Tests basic create and drop of tables with partitions.
  ASSERT_OK(TestPgRegress({"create_table_partitions.sql", "drop_table_partitions.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropTablePartitions2) {
  // Tests basic create and drop of tables with partitions.
  ASSERT_OK(TestPgRegress({"create_table_partitions2.sql", "drop_table_partitions2.sql"}));
}

TEST_P(XClusterPgRegressDDLReplicationParamTest, PgRegressAlterTable) {
  // Tests various add column types, alter index columns, renames and partitioned tables.
  ASSERT_OK(TestPgRegress({"alter_table.sql", "alter_table2.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropPgOnlyDdls) {
  // Tests create and drop of pass through ddls that dont require special handling.
  ASSERT_OK(TestPgRegress({"pgonly_ddls_create.sql", "pgonly_ddls_drop.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressRolesOwnersPermissions) {
  std::string pre_execute_sql_text = "CREATE ROLE sandeep WITH LOGIN PASSWORD 'password';";
  ASSERT_OK(TestPgRegress(
      {"owners_and_permissions1.sql", "owners_and_permissions2.sql"}, pre_execute_sql_text));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressAlterDefaultPrivileges) {
  std::string pre_execute_sql_text = "CREATE ROLE sandeep WITH LOGIN PASSWORD 'password';";
  ASSERT_OK(TestPgRegress(
      {"alter_default_privileges.sql"}, pre_execute_sql_text));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressAlterPgOnlyDdls) {
  // Tests create and alters of pass through ddls that dont require special handling.
  ASSERT_OK(TestPgRegress({"pgonly_ddls_create.sql", "pgonly_ddls_alter.sql"}));
}

TEST_P(XClusterPgRegressDDLReplicationParamTest, PgRegressTableRewrite) {
  ASSERT_OK(TestPgRegress({"table_rewrite.sql", "table_rewrite2.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressMultipleInheritance) {
  // Tests use of multiple levels of inheritance.
  ASSERT_OK(TestPgRegress({"inheritance.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropExtensions) {
  // Tests create and drops of the extensions supported by YB
  ASSERT_OK(TestPgRegress({"pgonly_extensions_create.sql", "pgonly_extensions_drop.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropEnum) {
  ASSERT_OK(TestPgRegress({"create_enum.sql", "drop_enum.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropType) {
  ASSERT_OK(TestPgRegress({"create_type.sql", "drop_type.sql"}));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropTemp) {
  ASSERT_OK(TestPgRegress({"temporary_objects.sql"}));

  // Ensure no DDLs on temporary objects got replicated.  For this test, there should be no DDLs on
  // non-temporary objects so it suffices to check that the count of replicated DDLs is 0.
  auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto num_replicated_ddls = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
      "SELECT count(*) FROM yb_xcluster_ddl_replication.ddl_queue;"));
  ASSERT_EQ(num_replicated_ddls, 0);
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateDropSequence) {
  ASSERT_OK(TestPgRegress({"create_sequence.sql", "drop_sequence.sql"}));
}

TEST_P(XClusterPgRegressDDLReplicationParamTest, PgRegressNonconcurrentBackfills) {
  ASSERT_OK(TestPgRegress({"nonconcurrent_backfills1.sql", "nonconcurrent_backfills2.sql"}));
}

TEST_P(XClusterPgRegressDDLReplicationParamTest, PgRegressTruncateTable) {
  google::SetVLOGLevel("xcluster_ddl_queue_handler", 2);

  ASSERT_OK(TestPgRegress(
      {"truncate_table1.sql", "truncate_table2.sql", "truncate_table3.sql"},
      /*pre_execution_sql_text=*/"",
      /*check_data_only=*/true));
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressCreateTableAs) {
  ASSERT_OK(TestPgRegress({"create_table_as.sql"}));
  ASSERT_OK(VerifyDataMatch());
}

TEST_F(XClusterPgRegressDDLReplicationTest, PgRegressMaterializedViews) {
  ASSERT_OK(TestPgRegress({"matview_create.sql", "matview_drop.sql"}));
}

}  // namespace yb
