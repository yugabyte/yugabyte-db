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

#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/encryption/header_manager.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/yb_table_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/integration-tests/cluster_verifier.h"

#include "yb/master/encryption_manager.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/random_util.h"
#include "yb/util/status_log.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_util.h"

using std::string;

DECLARE_int64(db_write_buffer_size);
DECLARE_int32(memstore_size_mb);
DECLARE_int32(load_balancer_max_concurrent_tablet_remote_bootstraps);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int64(encryption_counter_min);
DECLARE_int64(encryption_counter_max);

using namespace std::chrono_literals;

namespace yb {
namespace integration_tests {

constexpr uint32_t kNumKeys = 1024;
constexpr uint32_t kNumFlushes = 8;
constexpr uint32_t kNumCompactions = 2;
constexpr uint32_t kKeySize = 100;
constexpr uint32_t kCounterOverflowDefault = 0xFFFFFFE0;


class EncryptionTest : public YBTableTestBase, public testing::WithParamInterface<bool> {
 public:

  bool use_external_mini_cluster() override { return true; }

  bool use_yb_admin_client() override { return true; }

  bool enable_ysql() override { return false; }

  size_t num_tablet_servers() override {
    return 3;
  }

  size_t num_masters() override {
    return 3;
  }

  int num_tablets() override {
    return 3;
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_tablet_remote_bootstraps) = 1;

    YBTableTestBase::SetUp();
  }

  void BeforeCreateTable() override {
    ASSERT_NO_FATALS(AddUniverseKeys());
    ASSERT_NO_FATALS(RotateKey());
    // Wait for the key to be propagated to tserver through heartbeat.
    SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_heartbeat_interval_ms));
  }

  void WriteWorkload(uint32_t start, uint32_t end) {
    auto total_num_keys = end - start;
    for (uint32_t i = start; i < end; i++) {
      string s(kKeySize, 'a' + (i % 26));
      PutKeyValue(Format("k_$0", i), s);
      auto num_keys_written = i - start + 1;
      if (num_keys_written % (total_num_keys / kNumFlushes) == 0) {
        ASSERT_OK(client_->FlushTables({table_->id()}, false, 30, false));
      }
      if (num_keys_written % (total_num_keys / kNumCompactions) == 0) {
        ASSERT_OK(client_->FlushTables({table_->id()}, false, 30, true));
      }
    }
  }

  void VerifyWrittenRecords() {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    for (uint32_t i = 0; i < result_kvs.size(); i++) {
      auto split = StringSplit(result_kvs[i].first, '_');
      int32_t key = CHECK_RESULT(CheckedStoInt<int32_t>(split.back()));
      ASSERT_EQ(Format("k_$0", key), result_kvs[i].first);
      ASSERT_TRUE(string(kKeySize, 'a' + (key % 26)) == result_kvs[i].second);
    }
  }

  void AddUniverseKeys() {
    current_key_id_ = RandomHumanReadableString(16);
    auto bytes = RandomBytes(32);
    ASSERT_OK(yb_admin_client_->AddUniverseKeyToAllMasters(
        current_key_id_, std::string(bytes.begin(), bytes.end())));
    current_key_ = bytes;
  }

  Status WaitForAllMastersHaveLatestKeyInMemory() {
    return LoggedWaitFor([&]() -> Result<bool> {
      return yb_admin_client_->AllMastersHaveUniverseKeyInMemory(current_key_id_).ok();
    }, 30s, "Wait for all masters to have key in memory");
  }

  void RotateKey() {
    ASSERT_OK(WaitForAllMastersHaveLatestKeyInMemory());
    ASSERT_OK(yb_admin_client_->RotateUniverseKeyInMemory(current_key_id_));
    ASSERT_OK(yb_admin_client_->IsEncryptionEnabled());
  }

  Status WaitForLoadBalanced() {
    SleepFor(MonoDelta::FromSeconds(5));
    return LoggedWaitFor([&]() -> Result<bool> {
      return client_->IsLoadBalanced(3);
    }, MonoDelta::FromSeconds(30), "Wait for load balanced");
  }

  void DisableEncryption() {
    ASSERT_OK(yb_admin_client_->DisableEncryptionInMemory());
  }
 protected:
  std::string current_key_id_ = "";
  std::vector<uint8_t> current_key_;
};

INSTANTIATE_TEST_CASE_P(TestWithCounterOverflow, EncryptionTest, ::testing::Bool());

TEST_P(EncryptionTest, BasicWriteRead) {
  if (GetParam()) {
    // If testing with counter overflow, make sure we set counter to a value that will overflow
    // for sst files.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_encryption_counter_min) = kCounterOverflowDefault;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_encryption_counter_max) = kCounterOverflowDefault;
  }

  WriteWorkload(0, kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, MasterLeaderRestart) {
  WriteWorkload(0, kNumKeys);
  // Restart the master leader.
  auto* master_leader = external_mini_cluster()->GetLeaderMaster();
  master_leader->Shutdown();
  ASSERT_OK(master_leader->Restart());
  // Recreate the admin client after restarting master leader.
  CreateAdminClient();
  ASSERT_OK(WaitForAllMastersHaveLatestKeyInMemory());
  // Restart the tablet servers and make sure they can contact the new master leader for the key.
  for (size_t i = 0; i < external_mini_cluster()->num_tablet_servers(); i++) {
    external_mini_cluster()->tablet_server(i)->Shutdown();
    CHECK_OK(external_mini_cluster()->tablet_server(i)->Restart());
    SleepFor(MonoDelta::FromSeconds(5));\
    /*ASSERT_OK(external_mini_cluster()->WaitForTabletsRunning(
        external_mini_cluster()->tablet_server(i), MonoDelta::FromSeconds(30)));*/
  }

  ASSERT_OK(WaitForLoadBalanced());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, AllMastersRestart) {
  WriteWorkload(0, kNumKeys);
  for (size_t i = 0; i < external_mini_cluster()->num_masters(); i++) {
    external_mini_cluster()->master(i)->Shutdown();
    CHECK_OK(external_mini_cluster()->master(i)->Restart());
  }

  ASSERT_OK(WaitForLoadBalanced());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, RollingMasterRestart) {
  WriteWorkload(0, kNumKeys);
  for (size_t i = 0; i < external_mini_cluster()->num_masters(); i++) {
    external_mini_cluster()->master(i)->Shutdown();
    CHECK_OK(external_mini_cluster()->master(i)->Restart());
    ASSERT_OK(WaitForAllMastersHaveLatestKeyInMemory());
  }
  // Recreate the admin client after rolling the masters.
  CreateAdminClient();
  // Test that each master bootstraps from each other.
  ASSERT_NO_FATALS(AddUniverseKeys());
  ASSERT_NO_FATALS(RotateKey());

  ASSERT_OK(WaitForLoadBalanced());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, AddServer) {
  // Write 1000 values, add a server, and write 1000 more.
  WriteWorkload(0, kNumKeys);
  ASSERT_OK(external_mini_cluster()->AddTabletServer());
  ASSERT_OK(WaitForLoadBalanced());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, RotateKey) {
  // Write 1000 values, rotate a new key, and write 1000 more.
  WriteWorkload(0, kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(AddUniverseKeys());
  ASSERT_NO_FATALS(RotateKey());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, DisableEncryptionAndRestartCluster) {
  // Write 1000 values, disable encryption, and write 1000 more.
  WriteWorkload(0, kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(DisableEncryption());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
  auto* tablet_server = external_mini_cluster()->tablet_server(0);
  tablet_server->Shutdown();
  ASSERT_OK(tablet_server->Restart());
}

TEST_F(EncryptionTest, EmptyTable) {
  // No values added, make sure add server works with empty tables.
  ASSERT_OK(external_mini_cluster()->AddTabletServer());
  ASSERT_OK(WaitForLoadBalanced());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, EnableEncryption) {
  // Disable encryption, add 1000 values, enable, and write 1000 more.
  ASSERT_NO_FATALS(DisableEncryption());
  WriteWorkload(0, kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(AddUniverseKeys());
  ASSERT_NO_FATALS(RotateKey());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, ServerRestart) {
  WriteWorkload(0, kNumKeys);
  auto* tablet_server = external_mini_cluster()->tablet_server(0);
  tablet_server->Shutdown();
  ASSERT_OK(tablet_server->Restart());
  ASSERT_OK(external_mini_cluster()->WaitForTabletsRunning(
      tablet_server, MonoDelta::FromSeconds(30)));
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, MasterSendKeys) {
  // Test the following set of steps:
  // 1. Write some data with key1.
  // 2. Rotate to key2 and write some more data.
  // 3. Restart the masters all at once and just add key2 to the masters memory (similar to the YBA
  // task of just seeding with the latest key, so key1 is not in master memory).
  // 4. Restart a tserver, and make sure master sends it both key1 and key2.
  WriteWorkload(0, kNumKeys);
  ASSERT_NO_FATALS(AddUniverseKeys());
  ASSERT_NO_FATALS(RotateKey());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  for (size_t i = 0; i < external_mini_cluster()->num_masters(); i++) {
    external_mini_cluster()->master(i)->Shutdown();
  }
  for (size_t i = 0; i < external_mini_cluster()->num_masters(); i++) {
    ASSERT_OK(external_mini_cluster()->master(i)->Restart());
  }
  ASSERT_NOK(yb_admin_client_->AllMastersHaveUniverseKeyInMemory(current_key_id_));
  ASSERT_OK(yb_admin_client_->AddUniverseKeyToAllMasters(
        current_key_id_, std::string(current_key_.begin(), current_key_.end())));
  ASSERT_OK(WaitForAllMastersHaveLatestKeyInMemory());

  for (size_t i = 0; i < external_mini_cluster()->num_tablet_servers(); i++) {
    external_mini_cluster()->tablet_server(i)->Shutdown();
    ASSERT_OK(external_mini_cluster()->tablet_server(i)->Restart());
  }

  ClusterVerifier cv(external_mini_cluster());
  cv.SetVerificationTimeout(MonoDelta::FromSeconds(30));
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, AutoFlags) {
  // Test the following set of steps to ensure auto flags work properly with encryption
  // 1. Write some data with key1
  // 2. Promote auto flags with class kExternal to trigger an overwrite of the flagfile.
  // 3. Restart a tserver to ensure its able to properly Init and read the auto flagfile.
  WriteWorkload(0, kNumKeys);
  ASSERT_OK(yb_admin_client_->PromoteAutoFlags(
      ToString(AutoFlagClass::kExternal), true, true));

  SleepFor(MonoDelta::FromSeconds(5));

  for (size_t i = 0; i < external_mini_cluster()->num_tablet_servers(); i++) {
    external_mini_cluster()->tablet_server(i)->Shutdown();
    ASSERT_OK(external_mini_cluster()->tablet_server(i)->Restart());
  }

  ClusterVerifier cv(external_mini_cluster());
  cv.SetVerificationTimeout(MonoDelta::FromSeconds(30));
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, ShellModeMaster) {
  // Start a shell mode master and ensure that it eventually receives the universe key after RBS.
  ExternalMaster* new_master = nullptr;
  external_mini_cluster()->StartShellMaster(&new_master);
  ASSERT_OK(external_mini_cluster()->ChangeConfig(new_master, consensus::ADD_SERVER));

  yb_admin_client_.reset();
  CreateAdminClient();
  ASSERT_OK(WaitForAllMastersHaveLatestKeyInMemory());
}

class WALReuseEncryptionTest : public EncryptionTest {
 public:
  size_t num_tablet_servers() override {
    return 1;
  }

  size_t num_masters() override {
    return 1;
  }

  int num_tablets() override {
    return 1;
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--reuse_unclosed_segment_threshold_bytes=524288");
    opts->extra_master_flags.push_back("--reuse_unclosed_segment_threshold_bytes=524288");
    opts->extra_master_flags.push_back("--replication_factor=1");
  }

  void TestEncryptWALDataAfterWALReuse(bool rotate_key);
};

void WALReuseEncryptionTest::TestEncryptWALDataAfterWALReuse(bool rotate_key) {
  constexpr uint32_t kNumInsert = 100;
  auto* tablet_server = external_mini_cluster()->tablet_server(0);
  WriteWorkload(0, kNumInsert);
  tablet_server->Shutdown();
  ASSERT_OK(tablet_server->Restart());
  ASSERT_OK(external_mini_cluster()->WaitForTabletsRunning(
      tablet_server, MonoDelta::FromSeconds(30)));
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
  // Insert data after reuse log.
  WriteWorkload(kNumInsert, 2 * kNumInsert);
  auto current_segment_size =
      ASSERT_RESULT(external_mini_cluster()->GetSegmentCounts(tablet_server));
  ASSERT_EQ(1, current_segment_size);
  tablet_server->Shutdown();
  if (rotate_key) {
    ASSERT_NO_FATALS(AddUniverseKeys());
    ASSERT_NO_FATALS(RotateKey());
  }
  ASSERT_OK(tablet_server->Restart());
  // Verify the tablet can successfully finish boostrapping.
  ASSERT_OK(external_mini_cluster()->WaitForTabletsRunning(
      tablet_server, MonoDelta::FromSeconds(30)));
  ASSERT_NO_FATALS(cv.CheckCluster());
  // Verify number of WAL segment. If the key is changed, WAL will not be reused after restart.
  // Instead, it allocates a new segment to obtain the lastest universe key id.
  current_segment_size =
      ASSERT_RESULT(external_mini_cluster()->GetSegmentCounts(tablet_server));
  auto expect_segment_size = rotate_key ? 2 : 1;
  ASSERT_EQ(expect_segment_size, current_segment_size);
}

TEST_F_EX(EncryptionTest, EncryptWALDataAfterWALReuse,
    WALReuseEncryptionTest) {
  TestEncryptWALDataAfterWALReuse(false);
}

TEST_F_EX(EncryptionTest, EncryptWALDataAfterWALReuseWithRotateKey,
    WALReuseEncryptionTest) {
  TestEncryptWALDataAfterWALReuse(true);
}

class WALRolloverTest : public EncryptionTest {
 public:
  size_t num_tablet_servers() override {
    return 1;
  }

  size_t num_masters() override {
    return 1;
  }

  int num_tablets() override {
    return 1;
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--initial_log_segment_size_bytes=262144");
    opts->extra_tserver_flags.push_back("--save_index_into_wal_segments=true");
    opts->extra_master_flags.push_back("--replication_factor=1");
  }
};

TEST_F_EX(EncryptionTest, WALRolloverAndRestart, WALRolloverTest) {
  auto* tablet_server = external_mini_cluster()->tablet_server(0);
  WriteWorkload(0, kNumKeys);
  auto current_segment_size =
      ASSERT_RESULT(external_mini_cluster()->GetSegmentCounts(tablet_server));
  // Verify WAL get rollovered.
  ASSERT_GT(current_segment_size, 1);
  tablet_server->Shutdown();
  // After restart, bootstrap will read all entries to make sure no corruption.
  ASSERT_OK(tablet_server->Restart());
  ASSERT_OK(external_mini_cluster()->WaitForTabletsRunning(
      tablet_server, MonoDelta::FromSeconds(30)));
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

} // namespace integration_tests
} // namespace yb
