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

#include "yb/master/master.proxy.h"

#include "yb/util/test_util.h"
#include "yb/util/random_util.h"
#include "yb/util/string_util.h"
#include "yb/master/master.pb.h"

DECLARE_int64(db_write_buffer_size);
DECLARE_int32(memstore_size_mb);

namespace yb {
namespace integration_tests {

constexpr uint32_t num_keys = 1024;
constexpr uint32_t key_size = (1 << 15);

class EncryptionTest : public YBTableTestBase {
 public:

  bool use_external_mini_cluster() override { return true; }

  void SetUp() override {
    FLAGS_memstore_size_mb = 1;
    FLAGS_db_write_buffer_size = 1048576;
    YBTableTestBase::SetUp();

    ASSERT_NO_FATALS(RotateKey());
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

  void RotateKey() {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(5));

    master::ChangeEncryptionInfoRequestPB encryption_info_req;
    master::ChangeEncryptionInfoResponsePB encryption_info_resp;
    encryption_info_req.set_encryption_enabled(true);

    auto bytes = RandomBytes(32);
    gscoped_ptr<WritableFile> key_file;
    std::string file_name;
    ASSERT_OK(Env::Default()->NewTempWritableFile(
        WritableFileOptions(), "key_file_XXXXXX", &file_name, &key_file));
    ASSERT_OK(key_file->Append(Slice(bytes.data(), bytes.size())));
    ASSERT_OK(key_file->Close());
    encryption_info_req.set_key_path(file_name);

    ASSERT_OK(external_mini_cluster()->master_proxy()->
        ChangeEncryptionInfo(encryption_info_req, &encryption_info_resp, &rpc));
    ASSERT_FALSE(encryption_info_resp.has_error());
  }

  void DisableEncryption() {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(5));

    master::ChangeEncryptionInfoRequestPB encryption_info_req;
    master::ChangeEncryptionInfoResponsePB encryption_info_resp;
    encryption_info_req.set_encryption_enabled(false);

    ASSERT_OK(external_mini_cluster()->master_proxy()->
        ChangeEncryptionInfo(encryption_info_req, &encryption_info_resp, &rpc));
    ASSERT_FALSE(encryption_info_resp.has_error());
  }
};

TEST_F(EncryptionTest, BasicWriteRead) {
  WriteWorkload(0, num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, ClusterRestart) {
  WriteWorkload(0, num_keys);
  external_mini_cluster()->Shutdown();
  CHECK_OK(external_mini_cluster()->Restart());
  WriteWorkload(num_keys, 2 * num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, AddServer) {
  WriteWorkload(0, num_keys);
  ASSERT_OK(external_mini_cluster()->AddTabletServer());
  WriteWorkload(num_keys, 2 * num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, RotateKey) {
  WriteWorkload(0, num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(RotateKey());
  WriteWorkload(num_keys, 2 * num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}

TEST_F(EncryptionTest, DisableEncryption) {
  WriteWorkload(0, num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ASSERT_NO_FATALS(DisableEncryption());
  WriteWorkload(num_keys, 2 * num_keys);
  ASSERT_NO_FATALS(VerifyWrittenRecords());
  ClusterVerifier cv(external_mini_cluster());
  ASSERT_NO_FATALS(cv.CheckCluster());
}


} // namespace integration_tests
} // namespace yb
