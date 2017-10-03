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
#ifndef KUDU_TSERVER_REMOTE_BOOTSTRAP_TEST_BASE_H_
#define KUDU_TSERVER_REMOTE_BOOTSTRAP_TEST_BASE_H_

#include "kudu/tserver/tablet_server-test-base.h"

#include <string>

#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/fs/block_manager.h"
#include "kudu/gutil/strings/fastmem.h"
#include "kudu/tablet/metadata.pb.h"
#include "kudu/tserver/remote_bootstrap.pb.h"
#include "kudu/util/crc.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"

namespace kudu {
namespace tserver {

using consensus::MinimumOpId;

// Number of times to roll the log.
static const int kNumLogRolls = 2;

class RemoteBootstrapTest : public TabletServerTestBase {
 public:
  virtual void SetUp() OVERRIDE {
    TabletServerTestBase::SetUp();
    StartTabletServer();
    // Prevent logs from being deleted out from under us until / unless we want
    // to test that we are anchoring correctly. Since GenerateTestData() does a
    // Flush(), Log GC is allowed to eat the logs before we get around to
    // starting a remote bootstrap session.
    tablet_peer_->log_anchor_registry()->Register(
      MinimumOpId().index(), CURRENT_TEST_NAME(), &anchor_);
    ASSERT_NO_FATAL_FAILURE(GenerateTestData());
  }

  virtual void TearDown() OVERRIDE {
    ASSERT_OK(tablet_peer_->log_anchor_registry()->Unregister(&anchor_));
    TabletServerTestBase::TearDown();
  }

 protected:
  // Grab the first column block we find in the SuperBlock.
  static BlockId FirstColumnBlockId(const tablet::TabletSuperBlockPB& superblock) {
    const tablet::RowSetDataPB& rowset = superblock.rowsets(0);
    const tablet::ColumnDataPB& column = rowset.columns(0);
    const BlockIdPB& block_id_pb = column.block();
    return BlockId::FromPB(block_id_pb);
  }

  // Check that the contents and CRC32C of a DataChunkPB are equal to a local buffer.
  static void AssertDataEqual(const uint8_t* local, int64_t size, const DataChunkPB& remote) {
    ASSERT_EQ(size, remote.data().size());
    ASSERT_TRUE(strings::memeq(local, remote.data().data(), size));
    uint32_t crc32 = crc::Crc32c(local, size);
    ASSERT_EQ(crc32, remote.crc32());
  }

  // Generate the test data for the tablet and do the flushing we assume will be
  // done in the unit tests for remote bootstrap.
  void GenerateTestData() {
    const int kIncr = 50;
    LOG_TIMING(INFO, "Loading test data") {
      for (int row_id = 0; row_id < kNumLogRolls * kIncr; row_id += kIncr) {
        InsertTestRowsRemote(0, row_id, kIncr);
        ASSERT_OK(tablet_peer_->tablet()->Flush());
        ASSERT_OK(tablet_peer_->log()->AllocateSegmentAndRollOver());
      }
    }
  }

  // Return the permananent_uuid of the local service.
  const std::string GetLocalUUID() const {
    return tablet_peer_->permanent_uuid();
  }

  const std::string& GetTabletId() const {
    return tablet_peer_->tablet()->tablet_id();
  }

  // Read a block file from the file system fully into memory and return a
  // Slice pointing to it.
  Status ReadLocalBlockFile(FsManager* fs_manager, const BlockId& block_id,
                            faststring* scratch, Slice* slice) {
    gscoped_ptr<fs::ReadableBlock> block;
    RETURN_NOT_OK(fs_manager->OpenBlock(block_id, &block));

    uint64_t size = 0;
    RETURN_NOT_OK(block->Size(&size));
    scratch->resize(size);
    RETURN_NOT_OK(block->Read(0, size, slice, scratch->data()));

    // Since the mmap will go away on return, copy the data into scratch.
    if (slice->data() != scratch->data()) {
      memcpy(scratch->data(), slice->data(), slice->size());
      *slice = Slice(scratch->data(), slice->size());
    }
    return Status::OK();
  }

  log::LogAnchor anchor_;
};

} // namespace tserver
} // namespace kudu

#endif // KUDU_TSERVER_REMOTE_BOOTSTRAP_TEST_BASE_H_
