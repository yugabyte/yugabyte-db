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
#include <set>

#include "yb/tserver/remote_bootstrap_client-test.h"

#include "yb/tablet/tablet_snapshots.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tserver/remote_snapshot_transfer_client.h"

namespace yb {
namespace tserver {

using std::string;
using std::vector;


class RemoteBootstrapRocksDBClientTest : public RemoteBootstrapClientTest {
 public:
  RemoteBootstrapRocksDBClientTest() : RemoteBootstrapClientTest(YQL_TABLE_TYPE) {}

  void SetUpRemoteBootstrapClient() override {
    // Create 5 snapshots.
    for (int i = 0; i < 5; i++) {
      SnapshotId snapshot_id = "snapshot_" + std::to_string(i);
      CreateSnapshot(snapshot_id);
      snapshot_ids_.push_back(std::move(snapshot_id));
    }
    RemoteBootstrapClientTest::SetUpRemoteBootstrapClient();
  }

  void CreateSnapshot(const SnapshotId& snapshot_id) {
    LOG(INFO) << "Creating Snapshot " << snapshot_id << " ...";
    tablet::SnapshotOperation operation(ASSERT_RESULT(tablet_peer_->shared_tablet()));
    auto& request = *operation.AllocateRequest();
    request.ref_snapshot_id(snapshot_id);
    operation.set_hybrid_time(tablet_peer_->clock().Now());
    operation.set_op_id(tablet_peer_->log()->GetLatestEntryOpId());
    auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
    ASSERT_OK(tablet->snapshots().Create(&operation));
  }

  void CheckSnapshotsInSrc() {
    // Check folders on sending side.
    auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
    src_rocksdb_dir_ = tablet->metadata()->rocksdb_dir();
    src_top_snapshots_dir_ = tablet->metadata()->snapshots_dir();

    ASSERT_TRUE(env_->FileExists(src_rocksdb_dir_));
    ASSERT_TRUE(env_->FileExists(src_top_snapshots_dir_));

    for (const auto& snapshot_id : snapshot_ids_) {
      const string snapshot_dir = JoinPathSegments(src_top_snapshots_dir_, snapshot_id);
      ASSERT_TRUE(env_->FileExists(snapshot_dir));
    }
  }

  // Verify that all the files of a snapshot were correctly copied to the destination.
  void VerifySameSnapshotFiles(const std::string& src_snapshot_dir,
                               const std::string& dst_snapshot_dir) {
    // Check downloaded snapshot files.
    auto src_snapshot_files = ASSERT_RESULT(
        env_->GetChildren(src_snapshot_dir, ExcludeDots::kTrue));
    auto dst_snapshot_files = ASSERT_RESULT(
        env_->GetChildren(dst_snapshot_dir, ExcludeDots::kTrue));

    ASSERT_EQ(src_snapshot_files.size(), dst_snapshot_files.size());
    std::sort(src_snapshot_files.begin(), src_snapshot_files.end());
    std::sort(dst_snapshot_files.begin(), dst_snapshot_files.end());

    for (size_t i = 0; i < src_snapshot_files.size(); ++i) {
      // Verify that client has the same files that leader has.
      const string dst_snapshot_file = dst_snapshot_files[i];
      const string src_snapshot_file = src_snapshot_files[i];
      ASSERT_EQ(dst_snapshot_file, src_snapshot_file);

      const string dst_snapshot_file_path = JoinPathSegments(dst_snapshot_dir, dst_snapshot_file);
      const string src_snapshot_file_file_path = JoinPathSegments(src_snapshot_dir,
                                                                  src_snapshot_file);

      if (ASSERT_RESULT(Env::Default()->IsDirectory(dst_snapshot_file_path))) {
        ASSERT_TRUE(ASSERT_RESULT(Env::Default()->IsDirectory(src_snapshot_file_file_path)));
        continue;
      }
      LOG(INFO) << "Comparing file " << dst_snapshot_file_path
                << " and file " << src_snapshot_file_file_path;
      ASSERT_OK(CompareFileContents(dst_snapshot_file_path, src_snapshot_file_file_path));
    }
  }

  void VerifySnapshotFilesAreValid(const string& rocksdb_dir, const string& top_snapshots_dir) {
    for (const auto& snapshot_id : snapshot_ids_) {
      const string snapshot_dir = JoinPathSegments(top_snapshots_dir, snapshot_id);
      LOG(INFO) << "Checking that dir " << snapshot_dir << " exists";
      ASSERT_TRUE(env_->FileExists(snapshot_dir));

      const string src_snapshot_dir = JoinPathSegments(src_top_snapshots_dir_, snapshot_id);
      ASSERT_NO_FATAL_FAILURE(VerifySameSnapshotFiles(src_snapshot_dir, snapshot_dir));
    }
  }

 protected:
  vector<SnapshotId> snapshot_ids_;
  string src_rocksdb_dir_;
  string src_top_snapshots_dir_;
};

// Basic snapshot files download unit test.
TEST_F(RemoteBootstrapRocksDBClientTest, TestDownloadSnapshotFiles) {
  ASSERT_NO_FATAL_FAILURE(CheckSnapshotsInSrc());

  // Check folders on receiving side.
  TabletStatusListener listener(meta_);

  const string rocksdb_dir = meta_->rocksdb_dir();
  const string top_snapshots_dir = meta_->snapshots_dir();
  ASSERT_FALSE(env_->FileExists(rocksdb_dir));

  ASSERT_OK(client_->FetchAll(&listener));
  ASSERT_TRUE(env_->FileExists(rocksdb_dir));
  ASSERT_TRUE(env_->FileExists(top_snapshots_dir));
  VerifySnapshotFilesAreValid(rocksdb_dir, top_snapshots_dir);
}

// Test that a failure during a download of a snapshot file doesn't cause the remote bootstrap
// process to fail.
TEST_F(RemoteBootstrapRocksDBClientTest, TestDownloadSnapshotFilesFailure) {
  ASSERT_NO_FATAL_FAILURE(CheckSnapshotsInSrc());

  // For snapshots 0 and 3 delete a file.
  std::set<SnapshotId> failed_snapshot_ids = {snapshot_ids_[0], snapshot_ids_[3]};
  for (auto failed_snapshot_id : failed_snapshot_ids) {
    const string src_snapshot_dir = JoinPathSegments(src_top_snapshots_dir_, failed_snapshot_id);
    auto file_to_delete = JoinPathSegments(src_snapshot_dir, "CURRENT");
    LOG(INFO) << Format(
        "Deleting file $0 for snapshot remote bootstrap test", file_to_delete);
    ASSERT_OK(env_->DeleteFile(file_to_delete));
  }

  // Check folders on receiving side.
  TabletStatusListener listener(meta_);

  const string rocksdb_dir = meta_->rocksdb_dir();
  const string top_snapshots_dir = meta_->snapshots_dir();


  ASSERT_OK(client_->FetchAll(&listener));
  ASSERT_TRUE(env_->FileExists(rocksdb_dir));
  ASSERT_TRUE(env_->FileExists(top_snapshots_dir));

  // Since we deleted a file from snapshots 0 and 3, the client should have deleted the snapshot
  // directories, and skipped downloading any other file that was part of that snapshot.
  for (auto failed_snapshot_id : failed_snapshot_ids) {
    const string deleted_snapshot_dir = JoinPathSegments(top_snapshots_dir, failed_snapshot_id);
    LOG(INFO) << "Checking that directory " << deleted_snapshot_dir << " doesn't exist";
    ASSERT_FALSE(env_->FileExists(deleted_snapshot_dir));
  }

  for (auto snapshot_id : snapshot_ids_) {
    if (failed_snapshot_ids.find(snapshot_id) != failed_snapshot_ids.end()) {
      // Skip failed snapshots.
      continue;
    }
    const string snapshot_dir = JoinPathSegments(top_snapshots_dir, snapshot_id);
    LOG(INFO) << "Checking that dir " << snapshot_dir << " exists";
    ASSERT_TRUE(env_->FileExists(snapshot_dir));

    const string src_snapshot_dir = JoinPathSegments(src_top_snapshots_dir_, snapshot_id);
    ASSERT_NO_FATAL_FAILURE(VerifySameSnapshotFiles(src_snapshot_dir, snapshot_dir));
  }
}

class RemoteSnapshotTransferDocksDBClientTest : public RemoteBootstrapRocksDBClientTest {
 public:
  RemoteSnapshotTransferDocksDBClientTest() : RemoteBootstrapRocksDBClientTest() {}

  void SetUpRemoteBootstrapClient() override {
    // Set custom rocksdb directory, this pattern is from RaftGroupMetadata::CreateNew.
    auto data_root_dirs = fs_manager_->GetDataRootDirs();
    CHECK(!data_root_dirs.empty()) << "No data root directories found";
    auto data_top_dir = data_root_dirs[0];

    rocksdb_dir_ =
        JoinPathSegments(data_top_dir, FsManager::kRocksDBDirName, "test-tablet-rfts-client-dir");

    // RemoteSnapshotTransferClient expects the metadata value passed in to be non-null
    meta_ = tablet_peer_->tablet_metadata();

    // Create snapshot.
    SnapshotId snapshot_id = "snapshot_0";
    CreateSnapshot(snapshot_id);
    snapshot_ids_.push_back(std::move(snapshot_id));

    // SetUp RemoteSnapshotTransferClient
    messenger_ = ASSERT_RESULT(rpc::MessengerBuilder(CURRENT_TEST_NAME()).Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());

    client_ = std::make_unique<RemoteSnapshotTransferClient>(GetTabletId(), fs_manager_.get());
    ASSERT_OK(GetRaftConfigLeader(
        ASSERT_RESULT(tablet_peer_->GetConsensus())
            ->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED),
        &leader_));

    HostPort host_port = HostPortFromPB(leader_.last_known_private_addr()[0]);
    ASSERT_OK(
        client_->Start(proxy_cache_.get(), GetTabletId(), host_port, rocksdb_dir_));
  }

 protected:
  std::unique_ptr<RemoteSnapshotTransferClient> client_;
  std::string rocksdb_dir_;
};

// Basic snapshot files download unit test.
TEST_F(RemoteSnapshotTransferDocksDBClientTest, TestDownloadSnapshotFiles) {
  ASSERT_NO_FATAL_FAILURE(CheckSnapshotsInSrc());
  ASSERT_FALSE(env_->FileExists(rocksdb_dir_));

  auto snapshot_id = snapshot_ids_.front();
  ASSERT_OK(client_->FetchSnapshot(snapshot_id));

  const string top_snapshots_dir = tablet::TabletSnapshots::SnapshotsDirName(rocksdb_dir_);
  ASSERT_TRUE(env_->FileExists(top_snapshots_dir));
  VerifySnapshotFilesAreValid(rocksdb_dir_, top_snapshots_dir);
}

} // namespace tserver
} // namespace yb
