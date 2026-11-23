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
#include <vector>

#include "yb/gutil/strings/join.h"

#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

namespace yb::pgwrapper {

class PgCboDependentFlagsTest : public LibPqTestBase {
 protected:
  void SetUp() override {
    LibPqTestBase::SetUp();
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      tserver_initial_flags_.push_back(*cluster_->tablet_server(i)->mutable_flags());
    }
  }

  // Restart cluster with specified tserver flags
  void RestartClusterWithFlags(const std::vector<std::string>& tserver_flags) {
    LOG(INFO) << "Restarting cluster with tserver flags: " << JoinStrings(tserver_flags, ", ");
    cluster_->Shutdown();

    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      auto* ts_flags = cluster_->tablet_server(i)->mutable_flags();
      // Clear previous flags before adding new ones
      ts_flags->clear();
      for (const auto& flag : tserver_flags) {
        ts_flags->push_back(flag);
      }
      for (const auto& flag : tserver_initial_flags_[i]) {
        ts_flags->push_back(flag);
      }
    }

    ASSERT_OK(cluster_->Restart());
  }

  // Set a GUC value at runtime (after connection is established)
  Status SetGucAtRuntime(PGConn* conn, const std::string& guc_name, const std::string& value) {
    return conn->ExecuteFormat("SET $0 = $1", guc_name, value);
  }

  // ============================================================================
  // Helper Functions for GUC Validation
  // ============================================================================

  // Get the current value of a GUC setting
  Result<std::string> GetGucValue(PGConn* conn, const std::string& guc_name) {
    auto result = VERIFY_RESULT(conn->FetchFormat("SHOW $0", guc_name));
    SCHECK_EQ(PQntuples(result.get()), 1, InternalError,
              Format("Expected 1 row for SHOW $0", guc_name));
    return VERIFY_RESULT(GetValue<std::string>(result.get(), 0, 0));
  }

  // Validate that a GUC has the expected value
  Status ValidateGucValue(
      PGConn* conn,
      const std::string& guc_name,
      const std::string& expected_value) {
    auto actual_value = VERIFY_RESULT(GetGucValue(conn, guc_name));
    SCHECK_EQ(actual_value, expected_value, InternalError,
              Format("GUC $0: expected '$1', got '$2'", guc_name, expected_value, actual_value));
    return Status::OK();
  }

 private:
  std::vector<std::vector<std::string>> tserver_initial_flags_;
};

// ============================================================================
// Test Cases
// ============================================================================

TEST_F(PgCboDependentFlagsTest, TestCboDependentFlags) {
  for (const auto& cbo_startup_flag : {"ysql_yb_enable_cbo", "ysql_pg_conf_csv=yb_enable_cbo"}) {
    // 1. Enable CBO dependent flags with ysql_yb_enable_cbo=on
    // Require that the CBO dependent flags are enabled.
    RestartClusterWithFlags({Format("--$0=on", cbo_startup_flag)});
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(ValidateGucValue(&conn, "yb_enable_cbo", "on"));

    // Validate CBO-dependent flags
    ASSERT_OK(ValidateGucValue(&conn, "yb_enable_bitmapscan", "on"));
    ASSERT_OK(ValidateGucValue(&conn, "yb_parallel_range_rows", "10000"));
    ASSERT_OK(ValidateGucValue(&conn, "yb_enable_update_reltuples_after_create_index", "on"));
  }

  // 2. Set CBO dependent flags explicitly.
  // Require that the CBO dependent flags are not overriden when setting CBO.
  // Use both ysql_yb_parallel_range_rows and ysql_pg_conf_csv flags to set the value.
  for (const auto& parallel_query_flag :
       {"ysql_yb_parallel_range_rows",
        "ysql_pg_conf_csv=yb_parallel_range_rows"}) {
    RestartClusterWithFlags({
      "--ysql_yb_enable_cbo=on",
      "--allowed_preview_flags_csv=ysql_yb_parallel_range_rows",
      Format("--$0=1024", parallel_query_flag),
    });
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(ValidateGucValue(&conn, "yb_enable_cbo", "on"));
    ASSERT_OK(ValidateGucValue(&conn, "yb_parallel_range_rows", "1024"));
    ASSERT_OK(ValidateGucValue(&conn, "yb_enable_bitmapscan", "on"));
  }

  // 3. GUC yb_enable_cbo does not override user specified GUCs.
  RestartClusterWithFlags({"--ysql_pg_conf_csv=yb_parallel_range_rows=1024"});
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(SetGucAtRuntime(&conn, "yb_enable_bitmapscan", "off"));
  ASSERT_OK(ValidateGucValue(&conn, "yb_enable_bitmapscan", "off"));
  ASSERT_OK(SetGucAtRuntime(&conn, "yb_enable_update_reltuples_after_create_index", "off"));
  ASSERT_OK(ValidateGucValue(&conn, "yb_enable_update_reltuples_after_create_index", "off"));
  ASSERT_OK(SetGucAtRuntime(&conn, "yb_enable_cbo", "on"));
  ASSERT_OK(ValidateGucValue(&conn, "yb_enable_cbo", "on"));

  ASSERT_OK(ValidateGucValue(&conn, "yb_enable_bitmapscan", "off"));
  ASSERT_OK(ValidateGucValue(&conn, "yb_enable_update_reltuples_after_create_index", "off"));
  ASSERT_OK(ValidateGucValue(&conn, "yb_parallel_range_rows", "1024"));
}

} // namespace yb::pgwrapper
