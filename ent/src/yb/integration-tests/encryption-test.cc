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

#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/yb_table_test_base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/integration-tests/cluster_verifier.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/encryption_manager.h"

#include "yb/util/test_util.h"
#include "yb/util/random_util.h"
#include "yb/util/string_util.h"
#include "yb/master/master.pb.h"

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

  bool use_external_mini_cluster() override { return false; }

  int num_tablet_servers() override {
    return 3;
  }

  int num_masters() override {
    return 3;
  }

  int num_tablets() override {
    return 3;
  }

  void SetUp() override {
    FLAGS_load_balancer_max_concurrent_tablet_remote_bootstraps = 1;

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
        ASSERT_OK(mini_cluster()->FlushTablets());
      }
      if (num_keys_written % (total_num_keys / kNumCompactions) == 0) {
        ASSERT_OK(mini_cluster()->CompactTablets());
      }
    }
  }

  void VerifyWrittenRecords() {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    for (uint32_t i = 0; i < result_kvs.size(); i++) {
      int32_t key;
      auto split = StringSplit(result_kvs[i].first, '_');
      key = boost::lexical_cast<int32_t>(split.back());
      ASSERT_EQ(Format("k_$0", key), result_kvs[i].first);
      ASSERT_TRUE(string(kKeySize, 'a' + (key % 26)) == result_kvs[i].second);
    }
  }

  Result<std::string> IsEncryptionEnabled() {
    master::IsEncryptionEnabledRequestPB is_enabled_req;
    master::IsEncryptionEnabledResponsePB is_enabled_resp;

    auto* catalog_manager = mini_cluster()->leader_mini_master()->master()->catalog_manager();
    RETURN_NOT_OK(catalog_manager->IsEncryptionEnabled(&is_enabled_req, &is_enabled_resp));
    return is_enabled_resp.encryption_enabled() ? is_enabled_resp.key_id() : "";
  }

  void AddUniverseKeys() {
    current_key_id_ = RandomHumanReadableString(16);
    auto bytes = RandomBytes(32);

    for (int i = 0; i < mini_cluster()->num_masters(); i++) {
      master::AddUniverseKeysRequestPB req;
      master::AddUniverseKeysResponsePB resp;
      (*req.mutable_universe_keys()->mutable_map())[current_key_id_] =
          string(bytes.begin(), bytes.end());
      auto* catalog_manager = mini_cluster()->mini_master(i)->master()->catalog_manager();
      ASSERT_OK(catalog_manager->encryption_manager().AddUniverseKeys(&req, &resp));
      ASSERT_FALSE(resp.has_error());
    }
  }

  Result<bool> AllMastersHaveLatestKeyInMemory() {
    for (int i = 0; i < mini_cluster()->num_masters(); i++) {
      master::HasUniverseKeyInMemoryRequestPB req;
      master::HasUniverseKeyInMemoryResponsePB resp;
      req.set_version_id(current_key_id_);

      auto* catalog_manager = mini_cluster()->mini_master(i)->master()->catalog_manager();
      RETURN_NOT_OK(catalog_manager->encryption_manager().HasUniverseKeyInMemory(&req, &resp));
      if (resp.has_error() || !resp.has_key()) {
        return false;
      }
    }
    return true;
  }

  void WaitForAllMastersHaveLatestKeyInMemory() {
    AssertLoggedWaitFor([&]() -> Result<bool> {
      return AllMastersHaveLatestKeyInMemory();
    }, 30s, "Wait for all masters to have key in memory");
  }


  void RotateKey() {
    master::ChangeEncryptionInfoRequestPB encryption_info_req;
    master::ChangeEncryptionInfoResponsePB encryption_info_resp;
    encryption_info_req.set_encryption_enabled(true);

    auto bytes = RandomBytes(32);

    encryption_info_req.set_version_id(current_key_id_);
    encryption_info_req.set_in_memory(true);

    auto* catalog_manager = mini_cluster()->leader_mini_master()->master()->catalog_manager();
    ASSERT_OK(catalog_manager->ChangeEncryptionInfo(&encryption_info_req, &encryption_info_resp));
    ASSERT_FALSE(encryption_info_resp.has_error());
    auto res = ASSERT_RESULT(IsEncryptionEnabled());
    ASSERT_NE("", res);
    ASSERT_EQ(current_key_id_, res);
    ASSERT_NO_FATALS(WaitForAllMastersHaveLatestKeyInMemory());
  }

  void WaitForLoadBalanced() {
    SleepFor(MonoDelta::FromSeconds(5));
    AssertLoggedWaitFor([&]() -> Result<bool> {
      master::IsLoadBalancedRequestPB req;
      master::IsLoadBalancedResponsePB resp;
      auto* catalog_manager = mini_cluster()->leader_mini_master()->master()->catalog_manager();
      return catalog_manager->IsLoadBalanced(&req, &resp).ok();
    }, MonoDelta::FromSeconds(30), "Wait for load balanced");
  }

  void DisableEncryption() {
    master::ChangeEncryptionInfoRequestPB encryption_info_req;
    master::ChangeEncryptionInfoResponsePB encryption_info_resp;
    encryption_info_req.set_encryption_enabled(false);

    auto* catalog_manager = mini_cluster()->leader_mini_master()->master()->catalog_manager();
    ASSERT_OK(catalog_manager->ChangeEncryptionInfo(&encryption_info_req, &encryption_info_resp));
    ASSERT_FALSE(encryption_info_resp.has_error());

    auto res = ASSERT_RESULT(IsEncryptionEnabled());
    ASSERT_EQ(res, "");
    current_key_id_ = "";
  }
 private:
  std::string current_key_id_ = "";
};

INSTANTIATE_TEST_CASE_P(TestWithCounterOverflow, EncryptionTest, ::testing::Bool());

TEST_P(EncryptionTest, BasicWriteRead) {
  if (GetParam()) {
    // If testing with counter overflow, make sure we set counter to a value that will overflow
    // for sst files.
    FLAGS_encryption_counter_min = kCounterOverflowDefault;
    FLAGS_encryption_counter_max = kCounterOverflowDefault;
  }

  WriteWorkload(0, kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, MasterLeaderRestart) {
  WriteWorkload(0, kNumKeys);
  // Restart the master leader.
  CHECK_OK(mini_cluster()->leader_mini_master()->Restart());
  ASSERT_NO_FATALS(WaitForAllMastersHaveLatestKeyInMemory());
  // Restart the tablet servers and make sure they can contact the new master leader for the key.
  for (int i = 0; i < mini_cluster()->num_tablet_servers(); i++) {
    CHECK_OK(mini_cluster()->mini_tablet_server(i)->Restart());
  }

  ASSERT_NO_FATALS(WaitForLoadBalanced());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, AllMastersRestart) {
  WriteWorkload(0, kNumKeys);
  // Restart all the master leaders and make sure the
  for (int i = 0; i < mini_cluster()->num_masters(); i++) {
    CHECK_OK(mini_cluster()->mini_master(i)->Restart());
  }

  ASSERT_NO_FATALS(WaitForLoadBalanced());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, RollingMasterRestart) {
  WriteWorkload(0, kNumKeys);
  // Restart all the master leaders and make sure the

  for (int i = 0; i < mini_cluster()->num_masters(); i++) {
    CHECK_OK(mini_cluster()->mini_master(i)->Restart());
    ASSERT_NO_FATALS(WaitForAllMastersHaveLatestKeyInMemory());
  }
  // Test that each master bootstraps from each other.
  ASSERT_NO_FATALS(AddUniverseKeys());
  ASSERT_NO_FATALS(RotateKey());

  ASSERT_NO_FATALS(WaitForLoadBalanced());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, AddServer) {
  // Write 1000 values, add a server, and write 1000 more.
  WriteWorkload(0, kNumKeys);
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_NO_FATALS(WaitForLoadBalanced());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
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
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, DisableEncryption) {
  // Write 1000 values, disable encryption, and write 1000 more.
  WriteWorkload(0, kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(DisableEncryption());
  WriteWorkload(kNumKeys, 2 * kNumKeys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, EmptyTable) {
  // No values added, make sure add server works with empty tables.
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_NO_FATALS(WaitForLoadBalanced());
  ClusterVerifier cv(mini_cluster());
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
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}


} // namespace integration_tests
} // namespace yb
