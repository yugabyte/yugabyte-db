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

#include <algorithm>

#include "yb/tserver/remote_bootstrap_client-test.h"
#include "yb/tablet/operations/snapshot_operation.h"

namespace yb {
namespace tserver {
namespace enterprise {

using std::string;
using std::vector;

using yb::tablet::enterprise::Tablet;

static const string kSnapshotId = "0123456789ABCDEF0123456789ABCDEF";

class RemoteBootstrapRocksDBClientTest : public RemoteBootstrapClientTest {
 public:
  RemoteBootstrapRocksDBClientTest() : RemoteBootstrapClientTest(YQL_TABLE_TYPE) {}

  void SetUpRemoteBootstrapClient() override {
    CreateSnapshot();
    RemoteBootstrapClientTest::SetUpRemoteBootstrapClient();
  }

  void CreateSnapshot() {
    LOG(INFO) << "Creating Snapshot " << kSnapshotId << " ...";
    TabletSnapshotOpRequestPB request;
    request.set_snapshot_id(kSnapshotId);
    tablet::SnapshotOperationState tx_state(tablet_peer_->tablet(), &request);
    tx_state.set_hybrid_time(tablet_peer_->clock().Now());
    tablet_peer_->log()->GetLatestEntryOpId().ToPB(tx_state.mutable_op_id());
    ASSERT_OK(tablet_peer_->tablet()->CreateSnapshot(&tx_state));
  }
};

// Basic snapshot files download unit test.
TEST_F(RemoteBootstrapRocksDBClientTest, TestDownloadSnapshotFiles) {
  // Check folders on sending side.
  const string src_rocksdb_dir = tablet_peer_->tablet()->metadata()->rocksdb_dir();
  const string src_top_snapshots_dir = Tablet::SnapshotsDirName(src_rocksdb_dir);
  const string src_snapshot_dir = JoinPathSegments(src_top_snapshots_dir, kSnapshotId);
  ASSERT_TRUE(env_->FileExists(src_rocksdb_dir));
  ASSERT_TRUE(env_->FileExists(src_top_snapshots_dir));
  ASSERT_TRUE(env_->FileExists(src_snapshot_dir));

  // Check folders on receiving side.
  TabletStatusListener listener(meta_);

  const string rocksdb_dir = meta_->rocksdb_dir();
  const string top_snapshots_dir = Tablet::SnapshotsDirName(rocksdb_dir);
  const string snapshot_dir = JoinPathSegments(top_snapshots_dir, kSnapshotId);
  ASSERT_FALSE(env_->FileExists(rocksdb_dir));

  // Download tablet files.
  ASSERT_OK(client_->DownloadRocksDBFiles());
  ASSERT_TRUE(env_->FileExists(rocksdb_dir));
  ASSERT_TRUE(env_->FileExists(top_snapshots_dir));

  // Download snapshot files.
  ASSERT_OK(client_->DownloadSnapshotFiles());
  ASSERT_TRUE(env_->FileExists(top_snapshots_dir));
  ASSERT_TRUE(env_->FileExists(snapshot_dir));

  // Check downloaded snapshot files.
  vector<string> snapshot_files;
  ASSERT_OK(fs_manager_->ListDir(snapshot_dir, &snapshot_files));

  vector<string> src_snapshot_files;
  ASSERT_OK(tablet_peer_->tablet_metadata()->fs_manager()->ListDir(src_snapshot_dir,
                                                                   &src_snapshot_files));

  ASSERT_EQ(snapshot_files.size(), src_snapshot_files.size());
  std::sort(snapshot_files.begin(), snapshot_files.end());
  std::sort(src_snapshot_files.begin(), src_snapshot_files.end());

  for (int i = 0; i < snapshot_files.size(); ++i) {
    // Verify that client has the same files that leader has.
    const string snapshot_file = snapshot_files[i];
    const string src_snapshot_file = src_snapshot_files[i];
    ASSERT_EQ(snapshot_file, src_snapshot_file);

    if (snapshot_file == "." || snapshot_file == "..") {
      continue;
    }

    const string snapshot_file_path = JoinPathSegments(snapshot_dir, snapshot_file);
    const string src_snapshot_file_file_path = JoinPathSegments(src_snapshot_dir,
                                                                src_snapshot_file);

    if (ASSERT_RESULT(Env::Default()->IsDirectory(snapshot_file_path))) {
      ASSERT_TRUE(ASSERT_RESULT(Env::Default()->IsDirectory(src_snapshot_file_file_path)));
      continue;
    }
    LOG(INFO) << "Comparing file " << snapshot_file_path
              << " and file " << src_snapshot_file_file_path;
    ASSERT_OK(CompareFileContents(snapshot_file_path, src_snapshot_file_file_path));
  }
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
