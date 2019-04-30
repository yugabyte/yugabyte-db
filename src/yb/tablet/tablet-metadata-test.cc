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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <glog/logging.h>

#include "yb/common/schema.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/fs/fs_manager.h"
#include "yb/gutil/ref_counted.h"
#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-util.h"

namespace yb {
namespace tablet {

class TestRaftGroupMetadata : public YBTabletTest {
 public:
  TestRaftGroupMetadata()
      : YBTabletTest(GetSimpleTestSchema()) {
  }

  void SetUp() override {
    YBTabletTest::SetUp();
    writer_.reset(new LocalTabletWriter(harness_->tablet().get()));
  }

  void BuildPartialRow(int key, int intval, const char* strval,
                       QLWriteRequestPB* req);

 protected:
  gscoped_ptr<LocalTabletWriter> writer_;
};

void TestRaftGroupMetadata::BuildPartialRow(int key, int intval, const char* strval,
                                         QLWriteRequestPB* req) {
  req->Clear();
  QLAddInt32HashValue(req, key);
  QLAddInt32ColumnValue(req, kFirstColumnId + 1, intval);
  QLAddStringColumnValue(req, kFirstColumnId + 2, strval);
}

// Test that loading & storing the superblock results in an equivalent file.
TEST_F(TestRaftGroupMetadata, TestLoadFromSuperBlock) {
  // Write some data to the tablet and flush.
  QLWriteRequestPB req;
  BuildPartialRow(0, 0, "foo", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync));

  // Create one more row. Write and flush.
  BuildPartialRow(1, 1, "bar", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync));

  // Shut down the tablet.
  harness_->tablet()->Shutdown();

  RaftGroupMetadata* meta = harness_->tablet()->metadata();

  // Dump the superblock to a PB. Save the PB to the side.
  RaftGroupReplicaSuperBlockPB superblock_pb_1;
  meta->ToSuperBlock(&superblock_pb_1);

  // Load the superblock PB back into the RaftGroupMetadata.
  ASSERT_OK(meta->ReplaceSuperBlock(superblock_pb_1));

  // Dump the tablet metadata to a superblock PB again, and save it.
  RaftGroupReplicaSuperBlockPB superblock_pb_2;
  meta->ToSuperBlock(&superblock_pb_2);

  // Compare the 2 dumped superblock PBs.
  ASSERT_EQ(superblock_pb_1.SerializeAsString(),
            superblock_pb_2.SerializeAsString())
    << superblock_pb_1.DebugString()
    << superblock_pb_2.DebugString();

  LOG(INFO) << "Superblocks match:\n"
            << superblock_pb_1.DebugString();
}


} // namespace tablet
} // namespace yb
