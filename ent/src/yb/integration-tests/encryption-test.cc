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

#include "yb/util/test_util.h"
#include "yb/util/random_util.h"
#include "yb/util/string_util.h"
#include "yb/master/master.pb.h"

DECLARE_int64(db_write_buffer_size);
DECLARE_int32(memstore_size_mb);
DECLARE_int32(load_balancer_max_concurrent_tablet_remote_bootstraps);
DECLARE_int32(heartbeat_interval_ms);

namespace yb {
namespace integration_tests {

constexpr uint32_t num_keys = 1024;
constexpr uint32_t key_size = (1 << 15);

class EncryptionTest : public YBTableTestBase {
 public:

  bool use_external_mini_cluster() override { return false; }

  int num_tablet_servers() override {
    return 1;
  }

  int num_tablets() override {
    return 4;
  }

  void SetUp() override {
    FLAGS_memstore_size_mb = 1;
    FLAGS_db_write_buffer_size = 1048576;
    FLAGS_load_balancer_max_concurrent_tablet_remote_bootstraps = 1;

    YBTableTestBase::SetUp();
  }

  void BeforeCreateTable() override {
    ASSERT_NO_FATALS(RotateKey());
    // Wait for the key to be propagated to tserver through heartbeat.
    SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_heartbeat_interval_ms));
  }

  void WriteWorkload(uint32_t start, uint32_t end) {
    for (uint32_t i = start; i < end; i++) {
      string s(key_size, 'a' + (i % 26));
      PutKeyValue(Format("k_$0", i), s);
    }
  }

  void VerifyWrittenRecords() {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    for (uint32_t i = 0; i < result_kvs.size(); i++) {
      int32_t key;
      auto split = StringSplit(result_kvs[i].first, '_');
      key = boost::lexical_cast<int32_t>(split.back());
      ASSERT_EQ(Format("k_$0", key), result_kvs[i].first);
      ASSERT_TRUE(string(key_size, 'a' + (key % 26)) == result_kvs[i].second);
    }
  }

  Result<std::string> IsEncryptionEnabled() {
    master::IsEncryptionEnabledRequestPB is_enabled_req;
    master::IsEncryptionEnabledResponsePB is_enabled_resp;

    RETURN_NOT_OK(mini_cluster()->leader_mini_master()->master()->catalog_manager()->
        IsEncryptionEnabled(&is_enabled_req, &is_enabled_resp));
    return is_enabled_resp.encryption_enabled() ? is_enabled_resp.key_id() : "";
  }

  void RotateKey() {
    master::ChangeEncryptionInfoRequestPB encryption_info_req;
    master::ChangeEncryptionInfoResponsePB encryption_info_resp;
    encryption_info_req.set_encryption_enabled(true);

    auto bytes = RandomBytes(32);
    std::unique_ptr<WritableFile> key_file;
    std::string file_name;
    ASSERT_OK(Env::Default()->NewTempWritableFile(
        WritableFileOptions(), "key_file_XXXXXX", &file_name, &key_file));
    ASSERT_OK(key_file->Append(Slice(bytes.data(), bytes.size())));
    ASSERT_OK(key_file->Close());
    encryption_info_req.set_key_path(file_name);

    ASSERT_OK(mini_cluster()->leader_mini_master()->master()->catalog_manager()->
        ChangeEncryptionInfo(&encryption_info_req, &encryption_info_resp));
    ASSERT_FALSE(encryption_info_resp.has_error());
    auto res = ASSERT_RESULT(IsEncryptionEnabled());
    ASSERT_NE("", res);
    ASSERT_NE(current_key_id_, res);
    current_key_id_ = res;
  }

  void WaitForLoadBalanced() {
    SleepFor(MonoDelta::FromSeconds(5));
    AssertLoggedWaitFor([&]() -> Result<bool> {
      master::IsLoadBalancedRequestPB req;
      master::IsLoadBalancedResponsePB resp;
      return mini_cluster()->leader_mini_master()->master()->catalog_manager()->
             IsLoadBalanced(&req, &resp).ok();
    }, MonoDelta::FromSeconds(30), "Wait for load balanced");
  }

  void DisableEncryption() {
    master::ChangeEncryptionInfoRequestPB encryption_info_req;
    master::ChangeEncryptionInfoResponsePB encryption_info_resp;
    encryption_info_req.set_encryption_enabled(false);

    ASSERT_OK(mini_cluster()->leader_mini_master()->master()->catalog_manager()->
              ChangeEncryptionInfo(&encryption_info_req, &encryption_info_resp));
    ASSERT_FALSE(encryption_info_resp.has_error());

    auto res = ASSERT_RESULT(IsEncryptionEnabled());
    ASSERT_EQ(res, "");
    current_key_id_ = "";
  }
 private:
  std::string current_key_id_ = "";
};

TEST_F(EncryptionTest, BasicWriteRead) {
  // Writes 1000 values and verifies they can be read.
  WriteWorkload(0, num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, ClusterRestart) {
  // Write 1000 values, restart the cluster, write 1000 more, and verify.
  WriteWorkload(0, num_keys);
  CHECK_OK(mini_cluster()->RestartSync());
  ASSERT_NO_FATALS(WaitForLoadBalanced());
  WriteWorkload(num_keys, 2 * num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, AddServer) {
  // Write 1000 values, add a server, and write 1000 more.
  WriteWorkload(0, num_keys);
  ASSERT_OK(mini_cluster()->AddTabletServer());
  ASSERT_NO_FATALS(WaitForLoadBalanced());
  WriteWorkload(num_keys, 2 * num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, RotateKey) {
  // Write 1000 values, rotate a new key, and write 1000 more.
  WriteWorkload(0, num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(RotateKey());
  WriteWorkload(num_keys, 2 * num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, DisableEncryption) {
  // Write 1000 values, disable encryption, and write 1000 more.
  WriteWorkload(0, num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(DisableEncryption());
  WriteWorkload(num_keys, 2 * num_keys);
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
  WriteWorkload(0, num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(RotateKey());
  WriteWorkload(num_keys, 2 * num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}


} // namespace integration_tests
} // namespace yb
