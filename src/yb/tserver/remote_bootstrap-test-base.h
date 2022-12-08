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
#pragma once

#include <string>

#include "yb/consensus/log.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/opid_util.h"

#include "yb/gutil/strings/fastmem.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/remote_bootstrap.pb.h"
#include "yb/tserver/tablet_server-test-base.h"

#include "yb/util/crc.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

namespace yb {
namespace tserver {

using consensus::MinimumOpId;

// Number of times to roll the log.
static const int kNumLogRolls = 2;

class RemoteBootstrapTest : public TabletServerTestBase {
 public:
  explicit RemoteBootstrapTest(TableType table_type = DEFAULT_TABLE_TYPE)
      : TabletServerTestBase(table_type) {}

  virtual void SetUp() override {
    TabletServerTestBase::SetUp();
    StartTabletServer();
    // Prevent logs from being deleted out from under us until / unless we want
    // to test that we are anchoring correctly. Since GenerateTestData() does a
    // Flush(), Log GC is allowed to eat the logs before we get around to
    // starting a remote bootstrap session.
    tablet_peer_->log_anchor_registry()->Register(
      MinimumOpId().index(), CURRENT_TEST_NAME(), &anchor_);
    ASSERT_NO_FATALS(GenerateTestData());
  }

  virtual void TearDown() override {
    ASSERT_OK(tablet_peer_->log_anchor_registry()->Unregister(&anchor_));
    TabletServerTestBase::TearDown();
  }

 protected:
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
        ASSERT_OK(tablet_peer_->tablet()->Flush(tablet::FlushMode::kSync));
        ASSERT_OK(tablet_peer_->log()->AllocateSegmentAndRollOver());
      }
    }
  }

  // Return the permanent_uuid of the local service.
  const std::string GetLocalUUID() const {
    return tablet_peer_->permanent_uuid();
  }

  std::string GetTableId() const {
    return tablet_peer_->tablet()->metadata()->table_id();
  }

  const std::string& GetTabletId() const {
    return tablet_peer_->tablet()->tablet_id();
  }

  log::LogAnchor anchor_;
};

} // namespace tserver
} // namespace yb
