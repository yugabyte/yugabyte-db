// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#include "kudu/tserver/remote_bootstrap-test-base.h"

#include "kudu/consensus/quorum_util.h"
#include "kudu/gutil/strings/fastmem.h"
#include "kudu/tablet/tablet_bootstrap.h"
#include "kudu/tserver/remote_bootstrap_client.h"
#include "kudu/util/env_util.h"

using std::shared_ptr;

namespace kudu {
namespace tserver {

using consensus::GetRaftConfigLeader;
using consensus::RaftPeerPB;
using tablet::TabletMetadata;
using tablet::TabletStatusListener;

class RemoteBootstrapClientTest : public RemoteBootstrapTest {
 public:
  virtual void SetUp() OVERRIDE {
    RemoteBootstrapTest::SetUp();

    fs_manager_.reset(new FsManager(Env::Default(), GetTestPath("client_tablet")));
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_->Open());

    tablet_peer_->WaitUntilConsensusRunning(MonoDelta::FromSeconds(10.0));
    rpc::MessengerBuilder(CURRENT_TEST_NAME()).Build(&messenger_);
    client_.reset(new RemoteBootstrapClient(GetTabletId(),
                                            fs_manager_.get(),
                                            messenger_,
                                            fs_manager_->uuid()));
    ASSERT_OK(GetRaftConfigLeader(tablet_peer_->consensus()
        ->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED), &leader_));

    HostPort host_port;
    HostPortFromPB(leader_.last_known_addr(), &host_port);
    ASSERT_OK(client_->Start(leader_.permanent_uuid(), host_port, &meta_));
  }

 protected:
  Status CompareFileContents(const string& path1, const string& path2);

  gscoped_ptr<FsManager> fs_manager_;
  shared_ptr<rpc::Messenger> messenger_;
  gscoped_ptr<RemoteBootstrapClient> client_;
  scoped_refptr<TabletMetadata> meta_;
  RaftPeerPB leader_;
};

Status RemoteBootstrapClientTest::CompareFileContents(const string& path1, const string& path2) {
  shared_ptr<RandomAccessFile> file1, file2;
  RETURN_NOT_OK(env_util::OpenFileForRandom(fs_manager_->env(), path1, &file1));
  RETURN_NOT_OK(env_util::OpenFileForRandom(fs_manager_->env(), path2, &file2));

  uint64_t size1, size2;
  RETURN_NOT_OK(file1->Size(&size1));
  RETURN_NOT_OK(file2->Size(&size2));
  if (size1 != size2) {
    return Status::Corruption("Sizes of files don't match",
                              strings::Substitute("$0 vs $1 bytes", size1, size2));
  }

  Slice slice1, slice2;
  faststring scratch1, scratch2;
  scratch1.resize(size1);
  scratch2.resize(size2);
  RETURN_NOT_OK(env_util::ReadFully(file1.get(), 0, size1, &slice1, scratch1.data()));
  RETURN_NOT_OK(env_util::ReadFully(file2.get(), 0, size2, &slice2, scratch2.data()));
  int result = strings::fastmemcmp_inlined(slice1.data(), slice2.data(), size1);
  if (result != 0) {
    return Status::Corruption("Files do not match");
  }
  return Status::OK();
}

// Basic begin / end remote bootstrap session.
TEST_F(RemoteBootstrapClientTest, TestBeginEndSession) {
  TabletStatusListener listener(meta_);
  ASSERT_OK(client_->FetchAll(&listener));
  ASSERT_OK(client_->Finish());
}

// Basic data block download unit test.
TEST_F(RemoteBootstrapClientTest, TestDownloadBlock) {
  TabletStatusListener listener(meta_);
  BlockId block_id = FirstColumnBlockId(*client_->superblock_);
  Slice slice;
  faststring scratch;

  // Ensure the block wasn't there before (it shouldn't be, we use our own FsManager dir).
  Status s;
  s = ReadLocalBlockFile(fs_manager_.get(), block_id, &scratch, &slice);
  ASSERT_TRUE(s.IsNotFound()) << "Expected block not found: " << s.ToString();

  // Check that the client downloaded the block and verification passed.
  BlockId new_block_id;
  ASSERT_OK(client_->DownloadBlock(block_id, &new_block_id));

  // Ensure it placed the block where we expected it to.
  s = ReadLocalBlockFile(fs_manager_.get(), block_id, &scratch, &slice);
  ASSERT_TRUE(s.IsNotFound()) << "Expected block not found: " << s.ToString();
  ASSERT_OK(ReadLocalBlockFile(fs_manager_.get(), new_block_id, &scratch, &slice));
}

// Basic WAL segment download unit test.
TEST_F(RemoteBootstrapClientTest, TestDownloadWalSegment) {
  ASSERT_OK(fs_manager_->CreateDirIfMissing(fs_manager_->GetTabletWalDir(GetTabletId())));

  uint64_t seqno = client_->wal_seqnos_[0];
  string path = fs_manager_->GetWalSegmentFileName(GetTabletId(), seqno);

  ASSERT_FALSE(fs_manager_->Exists(path));
  ASSERT_OK(client_->DownloadWAL(seqno));
  ASSERT_TRUE(fs_manager_->Exists(path));

  log::SegmentSequence local_segments;
  ASSERT_OK(tablet_peer_->log()->GetLogReader()->GetSegmentsSnapshot(&local_segments));
  const scoped_refptr<log::ReadableLogSegment>& segment = local_segments[0];
  string server_path = segment->path();

  // Compare the downloaded file with the source file.
  ASSERT_OK(CompareFileContents(path, server_path));
}

// Ensure that we detect data corruption at the per-transfer level.
TEST_F(RemoteBootstrapClientTest, TestVerifyData) {
  string good = "This is a known good string";
  string bad = "This is a known bad! string";
  const int kGoodOffset = 0;
  const int kBadOffset = 1;
  const int64_t kDataTotalLen = std::numeric_limits<int64_t>::max(); // Ignored.

  // Create a known-good PB.
  DataChunkPB valid_chunk;
  valid_chunk.set_offset(0);
  valid_chunk.set_data(good);
  valid_chunk.set_crc32(crc::Crc32c(good.data(), good.length()));
  valid_chunk.set_total_data_length(kDataTotalLen);

  // Make sure we work on the happy case.
  ASSERT_OK(client_->VerifyData(kGoodOffset, valid_chunk));

  // Test unexpected offset.
  DataChunkPB bad_offset = valid_chunk;
  bad_offset.set_offset(kBadOffset);
  Status s;
  s = client_->VerifyData(kGoodOffset, bad_offset);
  ASSERT_TRUE(s.IsInvalidArgument()) << "Bad offset expected: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "Offset did not match");
  LOG(INFO) << "Expected error returned: " << s.ToString();

  // Test bad checksum.
  DataChunkPB bad_checksum = valid_chunk;
  bad_checksum.set_data(bad);
  s = client_->VerifyData(kGoodOffset, bad_checksum);
  ASSERT_TRUE(s.IsCorruption()) << "Invalid checksum expected: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "CRC32 does not match");
  LOG(INFO) << "Expected error returned: " << s.ToString();
}

namespace {

vector<BlockId> GetAllSortedBlocks(const tablet::TabletSuperBlockPB& sb) {
  vector<BlockId> data_blocks;

  for (const tablet::RowSetDataPB& rowset : sb.rowsets()) {
    for (const tablet::DeltaDataPB& redo : rowset.redo_deltas()) {
      data_blocks.push_back(BlockId::FromPB(redo.block()));
    }
    for (const tablet::DeltaDataPB& undo : rowset.undo_deltas()) {
      data_blocks.push_back(BlockId::FromPB(undo.block()));
    }
    for (const tablet::ColumnDataPB& column : rowset.columns()) {
      data_blocks.push_back(BlockId::FromPB(column.block()));
    }
    if (rowset.has_bloom_block()) {
      data_blocks.push_back(BlockId::FromPB(rowset.bloom_block()));
    }
    if (rowset.has_adhoc_index_block()) {
      data_blocks.push_back(BlockId::FromPB(rowset.adhoc_index_block()));
    }
  }

  std::sort(data_blocks.begin(), data_blocks.end(), BlockIdCompare());
  return data_blocks;
}

} // anonymous namespace

TEST_F(RemoteBootstrapClientTest, TestDownloadAllBlocks) {
  // Download all the blocks.
  ASSERT_OK(client_->DownloadBlocks());

  // Verify that the new superblock reflects the changes in block IDs.
  //
  // As long as block IDs are generated with UUIDs or something equally
  // unique, there's no danger of a block in the new superblock somehow
  // being assigned the same ID as a block in the existing superblock.
  vector<BlockId> old_data_blocks = GetAllSortedBlocks(*client_->superblock_.get());
  vector<BlockId> new_data_blocks = GetAllSortedBlocks(*client_->new_superblock_.get());
  vector<BlockId> result;
  std::set_intersection(old_data_blocks.begin(), old_data_blocks.end(),
                        new_data_blocks.begin(), new_data_blocks.end(),
                        std::back_inserter(result), BlockIdCompare());
  ASSERT_TRUE(result.empty());
  ASSERT_EQ(old_data_blocks.size(), new_data_blocks.size());

  // Verify that the old blocks aren't found. We're using a different
  // FsManager than 'tablet_peer', so the only way an old block could end
  // up in ours is due to a remote bootstrap client bug.
  for (const BlockId& block_id : old_data_blocks) {
    gscoped_ptr<fs::ReadableBlock> block;
    Status s = fs_manager_->OpenBlock(block_id, &block);
    ASSERT_TRUE(s.IsNotFound()) << "Expected block not found: " << s.ToString();
  }
  // And the new blocks are all present.
  for (const BlockId& block_id : new_data_blocks) {
    gscoped_ptr<fs::ReadableBlock> block;
    ASSERT_OK(fs_manager_->OpenBlock(block_id, &block));
  }
}

} // namespace tserver
} // namespace kudu
