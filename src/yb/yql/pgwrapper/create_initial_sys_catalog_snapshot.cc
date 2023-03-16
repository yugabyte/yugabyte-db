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

// This program creates an "initial sys catalog snapshot" at a given directory that can later be
// used to bring up YSQL clusters without the time-consuming step of running initdb.
#include "yb/util/path_util.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_string(initial_sys_catalog_snapshot_dest_path, "",
              "Destination path to write the initial sys catalog snapshot to");

namespace yb {
namespace pgwrapper {

class CreateInitialSysCatalogSnapshotTest : public PgWrapperTestBase {
 protected:
  void SetUp() override {
    PgWrapperTestBase::SetUp();
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    if (FLAGS_initial_sys_catalog_snapshot_dest_path.empty()) {
      // Allow running this test without an argument.
      std::string test_tmpdir;
      CHECK_OK(Env::Default()->GetTestDirectory(&test_tmpdir));
      FLAGS_initial_sys_catalog_snapshot_dest_path = JoinPathSegments(
          test_tmpdir, "initial_sys_catalog_snapshot");
      LOG(INFO) << "Using a temporary directory! Sys catalog snapshot will not be saved.";
    }
    LOG(INFO) << "Creating initial system catalog snapshot at: "
              << FLAGS_initial_sys_catalog_snapshot_dest_path;
    options->extra_master_flags.emplace_back("--create_initial_sys_catalog_snapshot");
    options->extra_master_flags.emplace_back("--net_address_filter=ipv4_external,ipv4_all");
    options->extra_master_flags.emplace_back(
        "--initial_sys_catalog_snapshot_path=" + FLAGS_initial_sys_catalog_snapshot_dest_path);
    options->extra_master_flags.emplace_back("--master_auto_run_initdb");
  }

  int GetNumTabletServers() const override {
    // No tablet servers necessary to run initdb.
    return 0;
  }
};

TEST_F(CreateInitialSysCatalogSnapshotTest,
       CreateInitialSysCatalogSnapshot) {
  // All the work is done by the master on cluster startup.
}

}  // namespace pgwrapper
}  // namespace yb
