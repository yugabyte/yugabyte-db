// Copyright (c) YugaByte, Inc.

#include <algorithm>

#include "remote_bootstrap_client-test.h"


using std::shared_ptr;

namespace yb {
namespace tserver {

using consensus::GetRaftConfigLeader;
using consensus::RaftPeerPB;
using tablet::TabletMetadata;
using tablet::TabletStatusListener;

class RemoteBootstrapRocksDBClientTest : public RemoteBootstrapClientTest {
 public:
  RemoteBootstrapRocksDBClientTest() : RemoteBootstrapClientTest(KEY_VALUE_TABLE_TYPE) {}

  virtual void SetUp() OVERRIDE {
    RemoteBootstrapClientTest::SetUp();
  }
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
  ASSERT_OK(client_->DownloadRocksDBFiles());
  auto tablet_peer_checkpoint_dir = tablet_peer_->tablet()->GetLastRocksDBCheckpointDirForTest();

  vector<std::string> rocksdb_files;
  ASSERT_OK(fs_manager_->ListDir(meta_->rocksdb_dir(), &rocksdb_files));

  vector<std::string> tablet_peer_checkpoint_files;
  ASSERT_OK(tablet_peer_->tablet_metadata()->fs_manager()->ListDir(tablet_peer_checkpoint_dir,
                                                                   &tablet_peer_checkpoint_files));

  ASSERT_EQ(rocksdb_files.size(), tablet_peer_checkpoint_files.size());
  std::sort(rocksdb_files.begin(), rocksdb_files.end());
  std::sort(tablet_peer_checkpoint_files.begin(), tablet_peer_checkpoint_files.end());
  // Verify that the client has the same files that the leader has.
  for (int i = 0; i < rocksdb_files.size(); ++i) {
    auto local_rocksdb_file = rocksdb_files[i];
    auto tablet_peer_rocksdb_file = tablet_peer_checkpoint_files[i];
    ASSERT_EQ(local_rocksdb_file, tablet_peer_rocksdb_file);

    if (local_rocksdb_file == "." || local_rocksdb_file == "..") {
      continue;
    }

    auto local_rocksdb_file_path = JoinPathSegments(meta_->rocksdb_dir(), local_rocksdb_file);
    auto tablet_peer_rocksdb_file_path = JoinPathSegments(tablet_peer_checkpoint_dir,
                                                          tablet_peer_rocksdb_file);

    LOG(INFO) << "Comparing file " << local_rocksdb_file_path << " and file " << tablet_peer_rocksdb_file_path;
    ASSERT_OK(CompareFileContents(local_rocksdb_file_path, tablet_peer_rocksdb_file_path));
  }
}

} // namespace tserver
} // namespace yb
