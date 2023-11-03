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

#include <algorithm>

#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/remote_bootstrap_client-test.h"

using std::vector;

namespace yb {
namespace tserver {

using tablet::TabletStatusListener;

class RemoteBootstrapRocksDBClientTest : public RemoteBootstrapClientTest {
 public:
  RemoteBootstrapRocksDBClientTest() : RemoteBootstrapClientTest(YQL_TABLE_TYPE) {}
};

// Basic begin / end remote bootstrap session.
TEST_F(RemoteBootstrapRocksDBClientTest, TestBeginEndSession) {
  TabletStatusListener listener(meta_);
  ASSERT_OK(client_->FetchAll(&listener));
  ASSERT_OK(client_->Finish());
}

// Basic RocksDB files download unit test.
TEST_F(RemoteBootstrapRocksDBClientTest, TestDownloadRocksDBFiles) {
  TabletStatusListener listener(meta_);
  ASSERT_OK(client_->FetchAll(&listener));
  auto tablet_peer_checkpoint_dir =
      tablet_peer_->tablet()->snapshots().TEST_LastRocksDBCheckpointDir();

  vector<std::string> rocksdb_files;
  LOG(INFO) << "RocksDB dir: " << meta_->rocksdb_dir();
  ASSERT_OK(fs_manager_->ListDir(meta_->rocksdb_dir(), &rocksdb_files));

  vector<std::string> tablet_peer_checkpoint_files;
  ASSERT_OK(tablet_peer_->tablet_metadata()->fs_manager()->ListDir(tablet_peer_checkpoint_dir,
                                                                   &tablet_peer_checkpoint_files));

  std::sort(rocksdb_files.begin(), rocksdb_files.end());
  std::sort(tablet_peer_checkpoint_files.begin(), tablet_peer_checkpoint_files.end());

  ASSERT_EQ(rocksdb_files.size(), tablet_peer_checkpoint_files.size())
      << AsString(rocksdb_files) << " vs " << AsString(tablet_peer_checkpoint_files);

  // Verify that the client has the same files that the leader has.
  for (size_t i = 0; i < rocksdb_files.size(); ++i) {
    auto local_rocksdb_file = rocksdb_files[i];
    auto tablet_peer_rocksdb_file = tablet_peer_checkpoint_files[i];
    ASSERT_EQ(local_rocksdb_file, tablet_peer_rocksdb_file);

    if (local_rocksdb_file == "." || local_rocksdb_file == "..") {
      continue;
    }

    auto local_rocksdb_file_path = JoinPathSegments(meta_->rocksdb_dir(), local_rocksdb_file);
    auto tablet_peer_rocksdb_file_path = JoinPathSegments(tablet_peer_checkpoint_dir,
                                                          tablet_peer_rocksdb_file);

    LOG(INFO) << "Comparing file " << local_rocksdb_file_path
              << " and file " << tablet_peer_rocksdb_file_path;
    ASSERT_OK(CompareFileContents(local_rocksdb_file_path, tablet_peer_rocksdb_file_path));
  }
}

} // namespace tserver
} // namespace yb
