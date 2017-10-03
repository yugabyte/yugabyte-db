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
#include "kudu/tablet/tablet-test-util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>

#include "kudu/common/partial_row.h"
#include "kudu/common/row_operations.h"
#include "kudu/common/schema.h"
#include "kudu/consensus/consensus_meta.h"
#include "kudu/consensus/log.h"
#include "kudu/consensus/metadata.pb.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/fs/block_id.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/strings/fastmem.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/rpc/messenger.h"
#include "kudu/tserver/remote_bootstrap_session.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/util/crc.h"
#include "kudu/util/metrics.h"
#include "kudu/util/test_util.h"
#include "kudu/util/threadpool.h"

METRIC_DECLARE_entity(tablet);

using std::shared_ptr;
using std::string;

namespace kudu {
namespace tserver {

using consensus::ConsensusMetadata;
using consensus::OpId;
using consensus::RaftConfigPB;
using consensus::RaftPeerPB;
using fs::ReadableBlock;
using log::Log;
using log::LogOptions;
using log::LogAnchorRegistry;
using rpc::Messenger;
using rpc::MessengerBuilder;
using strings::Substitute;
using tablet::ColumnDataPB;
using tablet::DeltaDataPB;
using tablet::KuduTabletTest;
using tablet::RowSetDataPB;
using tablet::TabletPeer;
using tablet::TabletSuperBlockPB;
using tablet::WriteTransactionState;

class RemoteBootstrapTest : public KuduTabletTest {
 public:
  RemoteBootstrapTest()
    : KuduTabletTest(Schema({ ColumnSchema("key", STRING),
                              ColumnSchema("val", INT32) }, 1)) {
    CHECK_OK(ThreadPoolBuilder("test-exec").Build(&apply_pool_));
  }

  virtual void SetUp() OVERRIDE {
    KuduTabletTest::SetUp();
    SetUpTabletPeer();
    ASSERT_NO_FATAL_FAILURE(PopulateTablet());
    InitSession();
  }

  virtual void TearDown() OVERRIDE {
    session_.reset();
    tablet_peer_->Shutdown();
    KuduTabletTest::TearDown();
  }

 protected:
  void SetUpTabletPeer() {
    scoped_refptr<Log> log;
    CHECK_OK(Log::Open(LogOptions(), fs_manager(), tablet()->tablet_id(),
                       *tablet()->schema(),
                       0, // schema_version
                       NULL, &log));

    scoped_refptr<MetricEntity> metric_entity =
      METRIC_ENTITY_tablet.Instantiate(&metric_registry_, CURRENT_TEST_NAME());

    RaftPeerPB config_peer;
    config_peer.set_permanent_uuid(fs_manager()->uuid());
    config_peer.set_member_type(RaftPeerPB::VOTER);

    tablet_peer_.reset(
        new TabletPeer(tablet()->metadata(),
                       config_peer,
                       apply_pool_.get(),
                       Bind(&RemoteBootstrapTest::TabletPeerStateChangedCallback,
                            Unretained(this),
                            tablet()->tablet_id())));

    // TODO similar to code in tablet_peer-test, consider refactor.
    RaftConfigPB config;
    config.set_local(true);
    config.add_peers()->CopyFrom(config_peer);
    config.set_opid_index(consensus::kInvalidOpIdIndex);

    gscoped_ptr<ConsensusMetadata> cmeta;
    CHECK_OK(ConsensusMetadata::Create(tablet()->metadata()->fs_manager(),
                                       tablet()->tablet_id(), fs_manager()->uuid(),
                                       config, consensus::kMinimumTerm, &cmeta));

    shared_ptr<Messenger> messenger;
    MessengerBuilder mbuilder(CURRENT_TEST_NAME());
    mbuilder.Build(&messenger);

    log_anchor_registry_.reset(new LogAnchorRegistry());
    tablet_peer_->SetBootstrapping();
    CHECK_OK(tablet_peer_->Init(tablet(),
                                clock(),
                                messenger,
                                log,
                                metric_entity));
    consensus::ConsensusBootstrapInfo boot_info;
    CHECK_OK(tablet_peer_->Start(boot_info));

    ASSERT_OK(tablet_peer_->WaitUntilConsensusRunning(MonoDelta::FromSeconds(2)));
  }

  void TabletPeerStateChangedCallback(const string& tablet_id, const string& reason) {
    LOG(INFO) << "Tablet peer state changed for tablet " << tablet_id << ". Reason: " << reason;
  }

  void PopulateTablet() {
    for (int32_t i = 0; i < 1000; i++) {
      WriteRequestPB req;
      req.set_tablet_id(tablet_peer_->tablet_id());
      ASSERT_OK(SchemaToPB(client_schema_, req.mutable_schema()));
      RowOperationsPB* data = req.mutable_row_operations();
      RowOperationsPBEncoder enc(data);
      KuduPartialRow row(&client_schema_);

      string key = Substitute("key$0", i);
      ASSERT_OK(row.SetString(0, key));
      ASSERT_OK(row.SetInt32(1, i));
      enc.Add(RowOperationsPB::INSERT, row);

      WriteResponsePB resp;
      CountDownLatch latch(1);

      auto state = new WriteTransactionState(tablet_peer_.get(), &req, &resp);
      state->set_completion_callback(gscoped_ptr<tablet::TransactionCompletionCallback>(
          new tablet::LatchTransactionCompletionCallback<WriteResponsePB>(&latch, &resp)).Pass());
      ASSERT_OK(tablet_peer_->SubmitWrite(state));
      latch.Wait();
      ASSERT_FALSE(resp.has_error()) << "Request failed: " << resp.error().ShortDebugString();
      ASSERT_EQ(0, resp.per_row_errors_size()) << "Insert error: " << resp.ShortDebugString();
    }
    ASSERT_OK(tablet()->Flush());
  }

  void InitSession() {
    session_.reset(new RemoteBootstrapSession(tablet_peer_.get(), "TestSession", "FakeUUID",
                   fs_manager()));
    CHECK_OK(session_->Init());
  }

  // Read the specified BlockId, via the RemoteBootstrapSession, into a file.
  // 'path' will be populated with the name of the file used.
  // 'file' will be set to point to the SequentialFile containing the data.
  void FetchBlockToFile(const BlockId& block_id,
                        string* path,
                        gscoped_ptr<SequentialFile>* file) {
    string data;
    int64_t block_file_size = 0;
    RemoteBootstrapErrorPB::Code error_code;
    CHECK_OK(session_->GetBlockPiece(block_id, 0, 0, &data, &block_file_size, &error_code));
    if (block_file_size > 0) {
      CHECK_GT(data.size(), 0);
    }

    // Write the file to a temporary location.
    WritableFileOptions opts;
    string path_template = GetTestPath(Substitute("test_block_$0.tmp.XXXXXX", block_id.ToString()));
    gscoped_ptr<WritableFile> writable_file;
    CHECK_OK(Env::Default()->NewTempWritableFile(opts, path_template, path, &writable_file));
    CHECK_OK(writable_file->Append(Slice(data.data(), data.size())));
    CHECK_OK(writable_file->Close());

    CHECK_OK(Env::Default()->NewSequentialFile(*path, file));
  }

  MetricRegistry metric_registry_;
  scoped_refptr<LogAnchorRegistry> log_anchor_registry_;
  gscoped_ptr<ThreadPool> apply_pool_;
  scoped_refptr<TabletPeer> tablet_peer_;
  scoped_refptr<RemoteBootstrapSession> session_;
};

// Ensure that the serialized SuperBlock included in the RemoteBootstrapSession is
// equal to the serialized live superblock (on a quiesced tablet).
TEST_F(RemoteBootstrapTest, TestSuperBlocksEqual) {
  // Compare content of superblocks.
  faststring session_buf;
  faststring tablet_buf;

  {
    const TabletSuperBlockPB& session_superblock = session_->tablet_superblock();
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
TEST_F(RemoteBootstrapTest, TestBlocksEqual) {
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
TEST_F(RemoteBootstrapTest, TestBlocksAreFetchableAfterBeingDeleted) {
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
}  // namespace kudu
