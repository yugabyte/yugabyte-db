//
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

#include <gtest/gtest.h>

#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/rocksdb/util/multi_drive_test_env.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/multi_drive_test_env.h"

namespace yb {
namespace integration_tests {

const auto kDefaultTimeout = 30000ms;

class FaultyDriveTest : public YBTableTestBase {
 protected:
  void SetUp() override {
    ts_env_.reset(new MultiDriveTestEnv());
    ts_rocksdb_env_.reset(new rocksdb::MultiDriveTestEnv());
    YBTableTestBase::SetUp();
  }

  int num_drives() override {
    return 3;
  }

  int num_tablets() override {
    return 4;
  }

  size_t num_tablet_servers() override {
    return 3;
  }
};

TEST_F(FaultyDriveTest, FixFaultyDriveAndRestartTS) {
  ASSERT_OK(WaitAllReplicasReady(mini_cluster(), kDefaultTimeout));

  // Make TS0 drive1 faulty.
  auto ts0_drive1 = mini_cluster()->GetTabletServerDrive(0, 1);
  dynamic_cast<MultiDriveTestEnv*>(ts_env_.get())->AddFailedPath(ts0_drive1);
  dynamic_cast<rocksdb::MultiDriveTestEnv*>(ts_rocksdb_env_.get())->AddFailedPath(ts0_drive1);

  auto ts = mini_cluster()->mini_tablet_server(0);

  // Restart the tserver, so its FSManager will mark drive1 as faulty during server bootstrap.
  // Then FSManager ignore drive1 util the next server startup.
  ASSERT_OK(ts->Restart());
  ASSERT_OK(ts->WaitStarted());
  ASSERT_TRUE(ts->fs_manager().has_faulty_drive());

  // Once raft leaders realize tablets in drive1 are missing, they'll send RBS requests.
  // We expect TS0 to continue rejecting the RBS request as long as the drive1 remain faulty.
  std::this_thread::sleep_for(3s);

  // Fix the drive1.
  dynamic_cast<MultiDriveTestEnv*>(ts_env_.get())->RemoveFailedPath(ts0_drive1);
  dynamic_cast<rocksdb::MultiDriveTestEnv*>(ts_rocksdb_env_.get())->RemoveFailedPath(ts0_drive1);

  // Verify all tablets are up and running after the restart.
  ASSERT_OK(ts->Restart());
  ASSERT_OK(ts->WaitStarted());
  ASSERT_FALSE(ts->fs_manager().has_faulty_drive());
}

} // namespace integration_tests
} // namespace yb
