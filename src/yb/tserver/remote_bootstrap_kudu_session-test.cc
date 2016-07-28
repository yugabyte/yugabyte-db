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
#include "remote_bootstrap_session-test.h"

namespace yb {
namespace tserver {

class RemoteBootstrapKuduTest : public RemoteBootstrapTest {
 public:
  RemoteBootstrapKuduTest() : RemoteBootstrapTest(KUDU_COLUMNAR_TABLE_TYPE) {}

  virtual void SetUp() OVERRIDE {
    RemoteBootstrapTest::SetUp();
  }

  virtual void TearDown() OVERRIDE {
    RemoteBootstrapTest::TearDown();
  }
};

// Ensure that the serialized SuperBlock included in the RemoteBootstrapSession is
// equal to the serialized live superblock (on a quiesced tablet).
TEST_F(RemoteBootstrapKuduTest, TestSuperBlocksEqual) {
  // Compare content of superblocks.
  faststring session_buf;
  faststring tablet_buf;

  {
    TabletSuperBlockPB session_superblock;
    session_superblock.CopyFrom(session_->tablet_superblock());
    session_superblock.mutable_rocksdb_files()->Clear();
    int size = session_superblock.ByteSize();
    session_buf.resize(size);
    uint8_t* session_dst = session_buf.data();
    session_dst = session_superblock.SerializeWithCachedSizesToArray(session_dst);
  }

  {
    TabletSuperBlockPB tablet_superblock;
    ASSERT_OK(tablet()->metadata()->ToSuperBlock(&tablet_superblock));
    int size = tablet_superblock.ByteSize();
    tablet_buf.resize(size);
    uint8_t* tablet_dst = tablet_buf.data();
    tablet_dst = tablet_superblock.SerializeWithCachedSizesToArray(tablet_dst);
  }

  ASSERT_EQ(session_buf.size(), tablet_buf.size());
  int size = tablet_buf.size();
  ASSERT_EQ(0, strings::fastmemcmp_inlined(session_buf.data(), tablet_buf.data(), size));
}

// Test fetching all files from tablet server, ensure the checksums for each
// chunk and the total file sizes match.
TEST_F(RemoteBootstrapKuduTest, TestBlocksEqual) {
 TabletSuperBlockPB tablet_superblock;
  ASSERT_OK(tablet()->metadata()->ToSuperBlock(&tablet_superblock));
  for (int i = 0; i < tablet_superblock.rowsets_size(); i++) {
    const RowSetDataPB& rowset = tablet_superblock.rowsets(i);
    for (int j = 0; j < rowset.columns_size(); j++) {
      const ColumnDataPB& column = rowset.columns(j);
      const BlockIdPB& block_id_pb = column.block();
      BlockId block_id = BlockId::FromPB(block_id_pb);

      string path;
      gscoped_ptr<SequentialFile> file;
      FetchBlockToFile(block_id, &path, &file);
      uint64_t session_block_size = 0;
      ASSERT_OK(Env::Default()->GetFileSize(path, &session_block_size));
      faststring buf;
      buf.resize(session_block_size);
      Slice data;
      ASSERT_OK(file->Read(session_block_size, &data, buf.data()));
      uint32_t session_crc = crc::Crc32c(data.data(), data.size());
      LOG(INFO) << "session block file has size of " << session_block_size
                << " and CRC32C of " << session_crc << ": " << path;

      gscoped_ptr<ReadableBlock> tablet_block;
      ASSERT_OK(fs_manager()->OpenBlock(block_id, &tablet_block));
      uint64_t tablet_block_size = 0;
      ASSERT_OK(tablet_block->Size(&tablet_block_size));
      buf.resize(tablet_block_size);
      ASSERT_OK(tablet_block->Read(0, tablet_block_size, &data, buf.data()));
      uint32_t tablet_crc = crc::Crc32c(data.data(), data.size());
      LOG(INFO) << "tablet block file has size of " << tablet_block_size
                << " and CRC32C of " << tablet_crc
                << ": " << block_id;

      // Compare the blocks.
      ASSERT_EQ(tablet_block_size, session_block_size);
      ASSERT_EQ(tablet_crc, session_crc);
    }
  }
}

// Ensure that blocks are still readable through the open session even
// after they've been deleted.
TEST_F(RemoteBootstrapKuduTest, TestBlocksAreFetchableAfterBeingDeleted) {
  TabletSuperBlockPB tablet_superblock;
  ASSERT_OK(tablet()->metadata()->ToSuperBlock(&tablet_superblock));

  // Gather all the blocks.
  vector<BlockId> data_blocks;
  for (const RowSetDataPB& rowset : tablet_superblock.rowsets()) {
    for (const DeltaDataPB& redo : rowset.redo_deltas()) {
      data_blocks.push_back(BlockId::FromPB(redo.block()));
    }
    for (const DeltaDataPB& undo : rowset.undo_deltas()) {
      data_blocks.push_back(BlockId::FromPB(undo.block()));
    }
    for (const ColumnDataPB& column : rowset.columns()) {
      data_blocks.push_back(BlockId::FromPB(column.block()));
    }
    if (rowset.has_bloom_block()) {
      data_blocks.push_back(BlockId::FromPB(rowset.bloom_block()));
    }
    if (rowset.has_adhoc_index_block()) {
      data_blocks.push_back(BlockId::FromPB(rowset.adhoc_index_block()));
    }
  }

  // Delete them.
  for (const BlockId& block_id : data_blocks) {
    ASSERT_OK(fs_manager()->DeleteBlock(block_id));
  }

  // Read them back.
  for (const BlockId& block_id : data_blocks) {
    ASSERT_TRUE(session_->IsBlockOpenForTests(block_id));
    string data;
    RemoteBootstrapErrorPB::Code error_code;
    int64_t piece_size;
    ASSERT_OK(session_->GetBlockPiece(block_id, 0, 0,
                                      &data, &piece_size, &error_code));
  }
}

}  // namespace tserver
}  // namespace yb
