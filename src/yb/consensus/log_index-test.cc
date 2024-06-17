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

#include "yb/common/opid.h"
#include "yb/common/opid.pb.h"

#include "yb/consensus/log_index.h"
#include "yb/consensus/opid_util.h"

#include "yb/util/test_util.h"

namespace yb {
namespace log {

using consensus::MakeOpId;

class LogIndexTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();
    index_ = CHECK_RESULT(LogIndex::NewLogIndex(GetTestDataDirectory()));
  }

 protected:
  Status AddEntry(const OpIdPB& op_id, int64_t segment, int64_t offset) {
    LogIndexEntry entry;
    entry.op_id = yb::OpId::FromPB(op_id);
    entry.segment_sequence_number = segment;
    entry.offset_in_segment = offset;
    return index_->AddEntry(entry);
  }

  void VerifyEntry(const OpIdPB& op_id, int64_t segment, int64_t offset) {
    SCOPED_TRACE(op_id);
    LogIndexEntry result;
    EXPECT_OK(index_->GetEntry(op_id.index(), &result));
    EXPECT_EQ(op_id.term(), result.op_id.term);
    EXPECT_EQ(op_id.index(), result.op_id.index);
    EXPECT_EQ(segment, result.segment_sequence_number);
    EXPECT_EQ(offset, result.offset_in_segment);
  }

  void VerifyNotFound(int64_t index) {
    SCOPED_TRACE(index);
    LogIndexEntry result;
    Status s = index_->GetEntry(index, &result);
    EXPECT_TRUE(s.IsNotFound()) << s.ToString();
  }

  scoped_refptr<LogIndex> index_;
};


TEST_F(LogIndexTest, TestBasic) {
  // Insert three entries.
  ASSERT_OK(AddEntry(MakeOpId(1, 1), 1, 62345));
  ASSERT_OK(AddEntry(MakeOpId(1, 999999), 1, 999));
  ASSERT_OK(AddEntry(MakeOpId(1, 1500000), 1, 54321));
  VerifyEntry(MakeOpId(1, 1), 1, 62345);
  VerifyEntry(MakeOpId(1, 999999), 1, 999);
  VerifyEntry(MakeOpId(1, 1500000), 1, 54321);

  // Overwrite one.
  ASSERT_OK(AddEntry(MakeOpId(5, 1), 1, 50000));
  VerifyEntry(MakeOpId(5, 1), 1, 50000);
}

TEST_F(LogIndexTest, TestMultiSegmentWithGC) {
  const auto entries_per_chunk = TEST_GetEntriesPerIndexChunk();

  const std::map<int64_t, int64_t> offset_by_op_index = {
      {1, 12345},
      {entries_per_chunk - 1, 23456},
      {entries_per_chunk, 34567},
      {entries_per_chunk * 1.5, 45678},
      {entries_per_chunk * 2.5, 56789},
  };
  const auto kTerm = 1;
  const auto kSegmentNum = 1;

  const auto verify_entry = [&](const int64_t op_index) {
    VerifyEntry(MakeOpId(kTerm, op_index), kSegmentNum, offset_by_op_index.at(op_index));
  };

  for (auto& op_index_with_offset : offset_by_op_index) {
    ASSERT_OK(AddEntry(
        MakeOpId(kTerm, op_index_with_offset.first),
        kSegmentNum,
        op_index_with_offset.second));
  }

  for (int min_op_idx_to_retain = 0;
       min_op_idx_to_retain < entries_per_chunk;
       min_op_idx_to_retain += entries_per_chunk / 10) {
    SCOPED_TRACE(min_op_idx_to_retain);
    index_->GC(min_op_idx_to_retain);
    if (min_op_idx_to_retain <= 1) {
      verify_entry(1);
    }
    if (min_op_idx_to_retain <= entries_per_chunk - 1) {
      verify_entry(entries_per_chunk - 1);
    }
    verify_entry(entries_per_chunk);
    verify_entry(entries_per_chunk * 1.5);
    verify_entry(entries_per_chunk * 2.5);
  }

  index_->GC(entries_per_chunk);
  VerifyNotFound(1);
  VerifyNotFound(entries_per_chunk - 1);
  verify_entry(entries_per_chunk);
  verify_entry(entries_per_chunk * 1.5);
  verify_entry(entries_per_chunk * 2.5);

  // GC everything
  index_->GC(9 * entries_per_chunk);
  VerifyNotFound(1);
  VerifyNotFound(entries_per_chunk - 1);
  VerifyNotFound(entries_per_chunk);
  VerifyNotFound(entries_per_chunk * 1.5);
  VerifyNotFound(entries_per_chunk * 2.5);
}

} // namespace log
} // namespace yb
