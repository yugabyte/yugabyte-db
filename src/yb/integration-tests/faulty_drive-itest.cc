//
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

#include <gtest/gtest.h>

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/rocksdb/util/multi_drive_test_env.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/faststring.h"
#include "yb/util/multi_drive_test_env.h"
#include "yb/util/result.h"

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

// Validates the master-side cached has_faulty_drive_ state on TSDescriptor.
// It must stay set across a TServer restart that did NOT fix the drive, and
// must clear on a TServer restart that DID fix the drive.
TEST_F(FaultyDriveTest, MasterClearsFaultyDriveOnTServerRestartAfterFix) {
  ASSERT_OK(WaitAllReplicasReady(mini_cluster(), kDefaultTimeout));

  auto* ts = mini_cluster()->mini_tablet_server(0);
  const auto ts_uuid = ts->server()->permanent_uuid();

  auto master_sees_faulty = [&]() -> Result<bool> {
    auto* mini_master = VERIFY_RESULT(mini_cluster()->GetLeaderMiniMaster());
    auto desc = VERIFY_RESULT(mini_master->master()->ts_manager()->LookupTSByUUID(ts_uuid));
    return desc->has_faulty_drive();
  };

  auto fetch_master_url = [&](const std::string& path) -> Result<std::string> {
    auto* mini_master = VERIFY_RESULT(mini_cluster()->GetLeaderMiniMaster());
    const auto url = Format("http://$0$1", mini_master->bound_http_addr(), path);
    EasyCurl curl;
    faststring body;
    RETURN_NOT_OK(curl.FetchURL(url, &body));
    return body.ToString();
  };

  // Baseline: master sees the TS as healthy.
  ASSERT_FALSE(ASSERT_RESULT(master_sees_faulty()));

  auto* env = dynamic_cast<MultiDriveTestEnv*>(ts_env_.get());
  auto* renv = dynamic_cast<rocksdb::MultiDriveTestEnv*>(ts_rocksdb_env_.get());
  auto ts0_drive1 = mini_cluster()->GetTabletServerDrive(0, 1);

  // (1) Inject the fault and restart so FSManager flips on bootstrap.
  env->AddFailedPath(ts0_drive1);
  renv->AddFailedPath(ts0_drive1);
  ASSERT_OK(ts->Restart());
  ASSERT_OK(ts->WaitStarted());
  ASSERT_TRUE(ts->fs_manager().has_faulty_drive());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return master_sees_faulty(); }, kDefaultTimeout,
      "Master to observe faulty_drive=true"));

  // /tablet-servers HTML and /api/v1/tablet-servers JSON should both surface the faulty drive.
  {
    const auto html = ASSERT_RESULT(fetch_master_url("/tablet-servers"));
    ASSERT_NE(html.find("Faulty Drive"), std::string::npos)
        << "Expected '/tablet-servers' HTML to contain 'Faulty Drive'";
    const auto json = ASSERT_RESULT(fetch_master_url("/api/v1/tablet-servers"));
    ASSERT_NE(json.find("\"has_faulty_drive\":true"), std::string::npos)
        << "Expected '/api/v1/tablet-servers' JSON to report has_faulty_drive=true";
  }

  // (2) Restart again WITHOUT fixing the drive. The master  must continue to see the TServer as
  // faulty after the restart settles.
  ASSERT_OK(ts->Restart());
  ASSERT_OK(ts->WaitStarted());
  ASSERT_TRUE(ts->fs_manager().has_faulty_drive());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return master_sees_faulty(); }, kDefaultTimeout,
      "Master to re-observe faulty_drive=true after restart"));
  // Hold the assertion across multiple heartbeat intervals to confirm it
  // does not flap back to false.
  std::this_thread::sleep_for(2s);
  ASSERT_TRUE(ASSERT_RESULT(master_sees_faulty()));

  // (3) Fix the drive and restart. Master must clear its cached state on
  // the registration that follows the restart, without a master restart.
  env->RemoveFailedPath(ts0_drive1);
  renv->RemoveFailedPath(ts0_drive1);
  ASSERT_OK(ts->Restart());
  ASSERT_OK(ts->WaitStarted());
  ASSERT_FALSE(ts->fs_manager().has_faulty_drive());
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return !VERIFY_RESULT(master_sees_faulty()); }, kDefaultTimeout,
      "Master to clear faulty_drive after TServer restart with fixed drive"));

  // After recovery the indicator must disappear from both the HTML and JSON endpoints.
  {
    const auto html = ASSERT_RESULT(fetch_master_url("/tablet-servers"));
    ASSERT_EQ(html.find("Faulty Drive"), std::string::npos)
        << "Expected '/tablet-servers' HTML to no longer contain 'Faulty Drive'";
    const auto json = ASSERT_RESULT(fetch_master_url("/api/v1/tablet-servers"));
    ASSERT_EQ(json.find("\"has_faulty_drive\":true"), std::string::npos)
        << "Expected '/api/v1/tablet-servers' JSON to no longer report has_faulty_drive=true";
  }
}

} // namespace integration_tests
} // namespace yb
