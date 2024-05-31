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

#include "yb/rpc/proxy.h"

#include "yb/tserver/remote_bootstrap_session-test.h"

#include "yb/util/result.h"
#include "yb/util/status_log.h"

using std::string;
using std::vector;

namespace yb {
namespace tserver {

class RemoteBootstrapRocksDBTest : public RemoteBootstrapSessionTest {
 public:
  RemoteBootstrapRocksDBTest() : RemoteBootstrapSessionTest(YQL_TABLE_TYPE) {}

  void SetUp() override {
    RemoteBootstrapSessionTest::SetUp();
  }

  void TearDown() override {
    RemoteBootstrapSessionTest::TearDown();
  }
};

TEST_F(RemoteBootstrapRocksDBTest, TestCheckpointDirectory) {
  string checkpoint_dir;
  {
    auto temp_session = make_scoped_refptr<RemoteBootstrapSession>(
        tablet_peer_, "TestTempSession", "FakeUUID", nullptr /* nsessions */);
    CHECK_OK(temp_session->InitBootstrapSession());
    checkpoint_dir = temp_session->checkpoint_dir_;
    ASSERT_FALSE(checkpoint_dir.empty());
    ASSERT_TRUE(env_->FileExists(checkpoint_dir));
    bool is_dir = false;
    ASSERT_OK(env_->IsDirectory(checkpoint_dir, &is_dir));
    ASSERT_TRUE(is_dir);
    vector<string> rocksdb_files;
    ASSERT_OK(env_->GetChildren(checkpoint_dir, &rocksdb_files));
    // Ignore "." and ".." entries.
    ASSERT_GT(rocksdb_files.size(), 2);
  }
  // Verify that destructor deleted the checkpoint directory.
  ASSERT_FALSE(env_->FileExists(checkpoint_dir));
}

TEST_F(RemoteBootstrapRocksDBTest, CheckSuperBlockHasRocksDBFields) {
  auto superblock = session_->tablet_superblock();
  const auto& kv_store = superblock.kv_store();
  LOG(INFO) << superblock.ShortDebugString();
  ASSERT_EQ(1, kv_store.tables_size());
  ASSERT_EQ(YQL_TABLE_TYPE, kv_store.tables(0).table_type());
  ASSERT_TRUE(kv_store.has_rocksdb_dir());

  const auto& checkpoint_dir = session_->checkpoint_dir_;
  vector<string> checkpoint_files;
  ASSERT_OK(env_->GetChildren(checkpoint_dir, &checkpoint_files));

  // Ignore "." and ".." entries in session_->checkpoint_dir_.
  ASSERT_EQ(kv_store.rocksdb_files().size(), checkpoint_files.size() - 2);
  for (int i = 0; i < kv_store.rocksdb_files().size(); ++i) {
    const auto& rocksdb_file_name = kv_store.rocksdb_files(i).name();
    auto rocksdb_file_size_bytes = kv_store.rocksdb_files(i).size_bytes();
    auto file_path = JoinPathSegments(checkpoint_dir, rocksdb_file_name);
    ASSERT_TRUE(env_->FileExists(file_path));
    uint64 file_size_bytes = ASSERT_RESULT(env_->GetFileSize(file_path));
    ASSERT_EQ(rocksdb_file_size_bytes, file_size_bytes);
  }
}

TEST_F(RemoteBootstrapRocksDBTest, TestNonExistentRocksDBFile) {
  GetDataPieceInfo info;
  auto status = session_->GetRocksDBFilePiece("SomeNonExistentFile", &info);
  ASSERT_TRUE(status.IsNotFound());
}

}  // namespace tserver
}  // namespace yb
