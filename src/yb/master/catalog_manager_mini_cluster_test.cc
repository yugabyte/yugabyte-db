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

#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/mini_cluster_utils.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_loaders.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/util/env.h"
#include "yb/util/path_util.h"

DECLARE_bool(master_join_existing_universe);

namespace yb::master {

class CatalogManagerWithMiniClusterTest : public integration_tests::YBTableTestBase {
 public:
  size_t num_masters() override {
    return 1;
  }

  size_t num_tablet_servers() override {
    return 1;
  }
};

TEST_F(
    CatalogManagerWithMiniClusterTest, PreventRestoringInitialSnapshotWhenJoinExistingUniverseSet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_initial_sys_catalog_snapshot_path) =
      JoinPathSegments(mini_cluster()->GetMasterFsRoot(0), "snapshot_path");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_join_existing_universe) = true;
  ASSERT_OK(Env::Default()->CreateDir(FLAGS_initial_sys_catalog_snapshot_path));
  auto& catalog_manager =
      ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster())->catalog_manager_impl();
  SysCatalogLoadingState state{ catalog_manager.GetLeaderEpochInternal() };
  auto status = catalog_manager.VisitSysCatalog(&state);
  ASSERT_TRUE(status.IsIllegalState())
      << "Visiting sys catalog should fail with IllegalState status, instead status was: "
      << status.ToString();
  ASSERT_STR_CONTAINS(
      status.ToString(),
      "Master is joining an existing universe but wants to restore initial sys catalog snapshot");
}
}  // namespace yb::master
