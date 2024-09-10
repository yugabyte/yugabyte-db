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

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include <boost/function.hpp>

#include <glog/stl_logging.h>

#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/log.messages.h"
#include "yb/consensus/log-test-base.h"
#include "yb/consensus/log_index.h"
#include "yb/consensus/opid_util.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/random.h"
#include "yb/util/size_literals.h"
#include "yb/util/stopwatch.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_int32(num_batches, 10000,
             "Number of batches to write to/read from the Log in TestWriteManyBatches");

DECLARE_int32(log_min_segments_to_retain);
DECLARE_bool(never_fsync);
DECLARE_bool(writable_file_use_fsync);
DECLARE_int32(o_direct_block_alignment_bytes);
DECLARE_int32(o_direct_block_size_bytes);
DECLARE_bool(TEST_simulate_abrupt_server_restart);
DECLARE_bool(TEST_skip_file_close);
DECLARE_int64(reuse_unclosed_segment_threshold_bytes);

namespace yb {
namespace log {

using std::shared_ptr;
using std::vector;
using std::string;
using consensus::MakeOpId;
using strings::Substitute;

extern const char* kTestTable;
extern const char* kTestTablet;

struct TestLogSequenceElem {
  enum ElemType {
    REPLICATE,
    ROLL
  };
  ElemType type;
  OpIdPB id;
};

class LogTest : public LogTestBase {
 public:
  static constexpr TableType kTableType = TableType::YQL_TABLE_TYPE;

  void CreateAndRegisterNewAnchor(int64_t log_index, vector<LogAnchor*>* anchors) {
    anchors->push_back(new LogAnchor());
    log_anchor_registry_->Register(log_index, CURRENT_TEST_NAME(), anchors->back());
  }

  // Create a series of NO_OP entries in the log.
  // Anchor each segment on the first OpId of each log segment,
  // and update op_id to point to the next valid OpId.
  Status AppendMultiSegmentSequence(
      int num_total_segments, int num_ops_per_segment, OpIdPB* op_id, vector<LogAnchor*>* anchors) {
    CHECK(op_id->IsInitialized());
    for (int i = 0; i < num_total_segments - 1; i++) {
      if (anchors) {
        CreateAndRegisterNewAnchor(op_id->index(), anchors);
      }
      RETURN_NOT_OK(AppendNoOps(op_id, num_ops_per_segment));
      RETURN_NOT_OK(RollLog());
    }

    if (anchors) {
      CreateAndRegisterNewAnchor(op_id->index(), anchors);
    }
    RETURN_NOT_OK(AppendNoOps(op_id, num_ops_per_segment));
    return Status::OK();
  }

  Status AppendNewEmptySegmentToReader(int sequence_number,
                                       int first_repl_index,
                                       LogReader* reader) {
    string fqp = GetTestPath(strings::Substitute("wal-00000000$0", sequence_number));
    std::unique_ptr<WritableFile> w_log_seg;
    RETURN_NOT_OK(fs_manager_->env()->NewWritableFile(fqp, &w_log_seg));
    std::unique_ptr<RandomAccessFile> r_log_seg;
    RETURN_NOT_OK(fs_manager_->env()->NewRandomAccessFile(fqp, &r_log_seg));

    scoped_refptr<ReadableLogSegment> readable_segment(
        new ReadableLogSegment(fqp, shared_ptr<RandomAccessFile>(r_log_seg.release())));

    LogSegmentHeaderPB header;
    header.set_sequence_number(sequence_number);
    header.set_major_version(0);
    header.set_minor_version(0);
    header.set_unused_tablet_id(kTestTablet);
    SchemaToPB(GetSimpleTestSchema(), header.mutable_deprecated_schema());

    LogSegmentFooterPB footer;
    footer.set_num_entries(10);
    footer.set_min_replicate_index(first_repl_index);
    footer.set_max_replicate_index(first_repl_index + 9);

    RETURN_NOT_OK(readable_segment->Init(header, footer, 0));
    RETURN_NOT_OK(reader->AppendSegment(readable_segment));
    return Status::OK();
  }

  void GenerateTestSequence(size_t seq_len,
                            vector<TestLogSequenceElem>* ops,
                            vector<int64_t>* terms_by_index);
  void AppendTestSequence(const vector<TestLogSequenceElem>& seq);

  // Where to corrupt the log entry.
  enum CorruptionPosition {
    // Corrupt/truncate within the header.
    IN_HEADER,
    // Corrupt/truncate within the entry data itself.
    IN_ENTRY
  };

  void DoCorruptionTest(CorruptionType type, CorruptionPosition place,
                        Status expected_status, int expected_entries);

  void DoReuseLastSegmentTest(bool durable_wal_write);

  Result<std::vector<OpId>> AppendAndCopy(size_t num_batches, size_t num_entries_per_batch);

  std::string GetLogCopyPath(size_t copy_idx) {
    return Format("$0.copy-$1", tablet_wal_path_, copy_idx);
  }

  Result<std::unique_ptr<LogReader>> GetLogCopyReader(const size_t copy_idx) {
    const auto log_copy_dir = GetLogCopyPath(copy_idx);
    std::unique_ptr<LogReader> copied_log_reader;
    auto log_index = VERIFY_RESULT(LogIndex::NewLogIndex(log_copy_dir));
    RETURN_NOT_OK(LogReader::Open(
        fs_manager_->env(), log_index, "Log reader: ",
        log_copy_dir, /* table_metric_entity = */ nullptr,
        /* tablet_metric_entity = */ nullptr, &copied_log_reader));

    return copied_log_reader;
  }

  Result<SegmentSequence> GetSegmentsAndCheckMaxOpIndex(
      LogReader* log_reader, const int64_t copy_up_to_idx) {
    SegmentSequence segments;
    RETURN_NOT_OK(log_reader->GetSegmentsSnapshot(&segments));

    const ReadableLogSegmentPtr& last_segment = VERIFY_RESULT(segments.back());
    SCHECK_GE(
        last_segment->footer().max_replicate_index(), copy_up_to_idx, InternalError,
        "Max replicated operation index should be >= index of the operation to copy up to passed to"
        " Log::CopyTo. It could be larger in case of overwriting not committed operations, for "
        "example: 3.30, 3.31, 3.32, 4.30.");

    return segments;
  }

  Result<SegmentSequence> GetSegmentsFromLogCopyAndCheckMaxOpIndex(
      const size_t copy_idx, const int64_t copy_up_to_idx) {
    auto log_copy_reader = VERIFY_RESULT(GetLogCopyReader(copy_idx));
    return GetSegmentsAndCheckMaxOpIndex(log_copy_reader.get(), copy_up_to_idx);
  }

  struct Op {
    OpId id;
    bool committed;
  };

  Result<std::vector<Op>> GenerateOpsAndAppendToLog(
      const int num_segments, const int num_batches_per_segment, const int num_entries_per_batch,
      const double commit_probability, const int num_approx_term_changes,
      const int num_approx_term_changes_with_index_rollback);
};

void LogTest::DoReuseLastSegmentTest(bool durable_wal_write) {
  // log_->Close() simulates crash now
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_file_close) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reuse_unclosed_segment_threshold_bytes) = 512_KB;
  // Restore value options_.durable_wal_write back later.
  bool temp = options_.durable_wal_write;
  options_.durable_wal_write = durable_wal_write;

  uint64_t num_batches = 10;
  if (AllowSlowTests()) {
    num_batches = FLAGS_num_batches;
  }
  BuildLog();
  LOG_TIMING(INFO, "Wrote batches to log") {
    AppendReplicateBatchToLog(num_batches);
  }
  // Check number of entries.
  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  uint32_t num_entries = ASSERT_RESULT(GetEntries(segments));
  ASSERT_EQ(num_entries, num_batches);

  // Simulate crash then insert entries.
  size_t segment_num_before_crash = segments.size();
  ASSERT_OK(log_->Close());
  BuildLog();
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  // Make sure segments number didn't get increase after restart.
  ASSERT_EQ(segments.size(), segment_num_before_crash);
  num_entries = ASSERT_RESULT(GetEntries(segments));
  ASSERT_EQ(num_entries, num_batches);
  LOG_TIMING(INFO, "Wrote batches to log") {
    AppendReplicateBatchToLog(num_batches);
  }
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  num_entries = ASSERT_RESULT(GetEntries(segments));
  ASSERT_EQ(num_entries, num_batches*2);

  // Close this segment properly and add another segment.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_file_close) = false;
  ASSERT_OK(log_->AllocateSegmentAndRollOver());
  LOG_TIMING(INFO, "Wrote batches to log") {
    AppendReplicateBatchToLog(num_batches);
  }
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  num_entries = ASSERT_RESULT(GetEntries(segments));
  ASSERT_EQ(num_entries, num_batches*3);

  // Simulate log crashs in the middle of writing an entry.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_file_close) = true;
  segment_num_before_crash = segments.size();
  ASSERT_OK(log_->TEST_WriteCorruptedEntryBatchAndSync());
  ASSERT_OK(log_->Close());
  BuildLog();
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  // Make sure segments number didn't get increase after restart.
  ASSERT_EQ(segments.size(), segment_num_before_crash);
  num_entries = ASSERT_RESULT(GetEntries(segments));
  ASSERT_EQ(num_entries, num_batches*3);

  // Test log index.
  int64_t wal_segment_number = ASSERT_RESULT(
                    log_->GetLogReader()->LookupOpWalSegmentNumber(/*op_index=*/1));
  ASSERT_EQ(1, wal_segment_number);
  wal_segment_number = ASSERT_RESULT(
                    log_->GetLogReader()->LookupOpWalSegmentNumber(/*op_index=*/num_entries));
  ASSERT_EQ(segments.size(), wal_segment_number);
  options_.durable_wal_write = temp;
}

TEST_F(LogTest, TestReuseLastSegment) {
  DoReuseLastSegmentTest(/*durable_wal_write=*/false);
}

TEST_F(LogTest, TestReuseLastSegmentWithDirectIO) {
  DoReuseLastSegmentTest(/*durable_wal_write=*/true);
}

// If we write more than one entry in a batch, we should be able to
// read all of those entries back.
TEST_F(LogTest, TestMultipleEntriesInABatch) {
  BuildLog();

  OpIdPB opid;
  opid.set_term(1);
  opid.set_index(1);

  ASSERT_OK(AppendNoOpsToLogSync(clock_, log_.get(), &opid, 2));

  // RollOver() the batch so that we have a properly formed footer.
  ASSERT_OK(log_->AllocateSegmentAndRollOver());

  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));

  const ReadableLogSegmentPtr& first_segment = ASSERT_RESULT(segments.front());
  auto read_entries = first_segment->ReadEntries();
  ASSERT_OK(read_entries.status);

  ASSERT_EQ(2, read_entries.entries.size());

  // Verify the index.
  {
    LogIndexEntry entry;
    ASSERT_OK(log_->log_index_->GetEntry(1, &entry));
    ASSERT_EQ(1, entry.op_id.term);
    ASSERT_EQ(1, entry.segment_sequence_number);
    int64_t offset = entry.offset_in_segment;

    ASSERT_OK(log_->log_index_->GetEntry(2, &entry));
    ASSERT_EQ(1, entry.op_id.term);
    ASSERT_EQ(1, entry.segment_sequence_number);
    int64_t second_offset = entry.offset_in_segment;

    // The second entry should be at the same offset as the first entry
    // since they were written in the same batch.
    ASSERT_EQ(second_offset, offset);
  }

  // Test LookupOpId
  {
    auto loaded_op = ASSERT_RESULT(log_->GetLogReader()->LookupOpId(1));
    ASSERT_EQ(yb::OpId(1, 1), loaded_op);
    loaded_op = ASSERT_RESULT(log_->GetLogReader()->LookupOpId(2));
    ASSERT_EQ(yb::OpId(1, 2), loaded_op);
    auto result = log_->GetLogReader()->LookupOpId(3);
    ASSERT_TRUE(!result.ok() && result.status().IsNotFound())
        << "unexpected status: " << result.status();
  }

  ASSERT_OK(log_->Close());
}

// Tests that everything works properly with fsync enabled:
// This also tests SyncDir() (see KUDU-261), which is called whenever
// a new log segment is initialized.
TEST_F(LogTest, TestFsync) {
  options_.durable_wal_write = true;
  BuildLog();

  OpIdPB opid;
  opid.set_term(0);
  opid.set_index(1);

  ASSERT_OK(AppendNoOp(&opid));
  ASSERT_OK(log_->Close());
}

// Tests interval for durable wal write
TEST_F(LogTest, TestFsyncInterval) {
  options_.interval_durable_wal_write = MonoDelta::FromMilliseconds(1);
  BuildLog();

  OpIdPB opid;
  opid.set_term(0);
  opid.set_index(1);

  ASSERT_OK(AppendNoOp(&opid));
  SleepFor(MonoDelta::FromMilliseconds(2));
  ASSERT_OK(AppendNoOp(&opid));
  ASSERT_OK(log_->Close());
}

// Tests interval for durable wal write physically
TEST_F(LogTest, TestFsyncIntervalPhysical) {
  int interval = 1;
  options_.interval_durable_wal_write = MonoDelta::FromMilliseconds(interval);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_never_fsync) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_durable_wal_write) = false;
  options_.preallocate_segments = false;
  BuildLog();

  OpIdPB opid;
  opid.set_term(0);
  opid.set_index(1);

  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), 1);
  const ReadableLogSegmentPtr& first_segment = ASSERT_RESULT(segments.front());
  int64_t orig_size = first_segment->file_size();
  string fileName = first_segment->readable_file()->filename();

  ASSERT_OK(AppendNoOp(&opid));
  SleepFor(MonoDelta::FromMilliseconds(interval + 1));
  ASSERT_OK(AppendNoOp(&opid));

  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), 1);
  const ReadableLogSegmentPtr& segment = ASSERT_RESULT(segments.front());
  int64_t new_size = segment->file_size();
  ASSERT_GT(new_size, orig_size);

#if defined(__linux__)
  int fd = open(fileName.c_str(), O_RDONLY | O_DIRECT);
  ASSERT_GE(fd, 0);
#elif defined(__APPLE__)
  int fd = open(fileName.c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);
  ASSERT_NE(fcntl(fd, F_NOCACHE, 1), -1);
#endif
  void* temp_buf = nullptr;
  ASSERT_EQ(posix_memalign(&temp_buf, FLAGS_o_direct_block_alignment_bytes,
                           FLAGS_o_direct_block_size_bytes), 0);
  ASSERT_GT(pread(fd, temp_buf, FLAGS_o_direct_block_size_bytes, 0), orig_size);
  ASSERT_OK(log_->Close());
  free(temp_buf);
}

// Tests data size for durable wal write
TEST_F(LogTest, TestFsyncDataSize) {
  options_.bytes_durable_wal_write_mb = 1;
  options_.interval_durable_wal_write = MonoDelta::FromMilliseconds(10000);
  BuildLog();

  OpIdPB opid;
  opid.set_term(0);
  opid.set_index(1);

  ssize_t size = 0;
  ASSERT_OK(AppendNoOps(&opid, 100 * 1024, &size));
  SleepFor(MonoDelta::FromMilliseconds(1));
  ASSERT_OK(AppendNoOp(&opid));
  ASSERT_OK(log_->Close());
  LOG(INFO)<< "Wrote " << size << " batches to log";
}

// Regression test for part of KUDU-735:
// if a log is not preallocated, we should properly track its on-disk size as we append to
// it.
TEST_F(LogTest, TestSizeIsMaintained) {
  options_.preallocate_segments = false;
  BuildLog();

  OpIdPB opid = MakeOpId(0, 1);
  ASSERT_OK(AppendNoOp(&opid));

  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ReadableLogSegmentPtr first_segment = ASSERT_RESULT(segments.front());
  int64_t orig_size = first_segment->file_size();
  ASSERT_GT(orig_size, 0);

  ASSERT_OK(AppendNoOp(&opid));

  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  first_segment = ASSERT_RESULT(segments.front());
  int64_t new_size = first_segment->file_size();
  ASSERT_GT(new_size, orig_size);

  ASSERT_OK(log_->Close());
}

// Test that the reader can read from the log even if it hasn't been
// properly closed.
TEST_F(LogTest, TestLogNotTrimmed) {
  BuildLog();

  OpIdPB opid;
  opid.set_term(0);
  opid.set_index(1);

  ASSERT_OK(AppendNoOp(&opid));

  LogEntries entries;
  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));

  const ReadableLogSegmentPtr& first_segment = ASSERT_RESULT(segments.front());
  ASSERT_OK(first_segment->ReadEntries().status);
  // Close after testing to ensure correct shutdown
  // TODO : put this in TearDown() with a test on log state?
  ASSERT_OK(log_->Close());
}

// Test that the reader will not fail if a log file is completely blank.
// This happens when it's opened but nothing has been written.
// The reader should gracefully handle this situation, but somehow expose that
// the segment is uninitialized. See KUDU-140.
TEST_F(LogTest, TestBlankLogFile) {
  BuildLog();

  // The log's reader will have a segment...
  ASSERT_EQ(log_->GetLogReader()->num_segments(), 1);

  // ...and we're able to read from it.
  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));

  const ReadableLogSegmentPtr& first_segment = ASSERT_RESULT(segments.front());
  auto read_entries = first_segment->ReadEntries();
  ASSERT_OK(read_entries.status);

  // ...It's just that it's empty.
  ASSERT_EQ(read_entries.entries.size(), 0);
}

void LogTest::DoCorruptionTest(CorruptionType type, CorruptionPosition place,
                               Status expected_status, int expected_entries) {
  const int kNumEntries = 4;
  BuildLog();
  OpIdPB op_id = MakeOpId(1, 1);
  ASSERT_OK(AppendNoOps(&op_id, kNumEntries));

  // Find the entry that we want to corrupt before closing the log.
  LogIndexEntry entry;
  ASSERT_OK(log_->log_index_->GetEntry(4, &entry));

  ASSERT_OK(log_->Close());

  // Corrupt the log as specified.
  ssize_t offset = 0;
  switch (place) {
    case IN_HEADER:
      offset = entry.offset_in_segment + 1;
      break;
    case IN_ENTRY:
      offset = entry.offset_in_segment + kEntryHeaderSize + 1;
      break;
  }
  ASSERT_OK(CorruptLogFile(env_.get(), log_->TEST_ActiveSegment()->path(), type, offset));

  // Open a new reader -- we don't reuse the existing LogReader from log_
  // because it has a cached header.
  std::unique_ptr<LogReader> reader;
  auto log_index = ASSERT_RESULT(LogIndex::NewLogIndex(log_->wal_dir_));
  ASSERT_OK(LogReader::Open(
      fs_manager_->env(), log_index, "Log reader: ", tablet_wal_path_,
      /* table_metric_entity = */ nullptr, /* tablet_metric_entity = */ nullptr, &reader));
  ASSERT_EQ(1, reader->num_segments());

  SegmentSequence segments;
  ASSERT_OK(reader->GetSegmentsSnapshot(&segments));
  const ReadableLogSegmentPtr& first_segment = ASSERT_RESULT(segments.front());
  auto read_entries = first_segment->ReadEntries();
  ASSERT_EQ(read_entries.status.CodeAsString(), expected_status.CodeAsString())
      << "Got unexpected status: " << read_entries.status;

  // Last entry is ignored, but we should still see the previous ones.
  ASSERT_EQ(expected_entries, read_entries.entries.size());
}

// Tests that the log reader reads up until some truncated entry is found.
// It should still return OK, since on a crash, it's acceptable to have
// a partial entry at EOF.
TEST_F(LogTest, TestTruncateLogInEntry) {
  DoCorruptionTest(TRUNCATE_FILE, IN_ENTRY, Status::OK(), 3);
}

// Same, but truncate in the middle of the header of that entry.
TEST_F(LogTest, TestTruncateLogInHeader) {
  DoCorruptionTest(TRUNCATE_FILE, IN_HEADER, Status::OK(), 3);
}

// Similar to the above, except flips a byte. In this case, it should return
// a Corruption instead of an OK, because we still have a valid footer in
// the file (indicating that all of the entries should be valid as well).
TEST_F(LogTest, TestCorruptLogInEntry) {
  DoCorruptionTest(FLIP_BYTE, IN_ENTRY, STATUS(Corruption, ""), 3);
}

// Same, but corrupt in the middle of the header of that entry.
TEST_F(LogTest, TestCorruptLogInHeader) {
  DoCorruptionTest(FLIP_BYTE, IN_HEADER, STATUS(Corruption, ""), 3);
}

// Tests log metrics for WAL files size
TEST_F(LogTest, TestLogMetrics) {
  BuildLog();
// Set a small segment size so that we have roll overs.
  log_->SetMaxSegmentSizeForTests(990);
  const int kNumEntriesPerBatch = 100;

  OpIdPB op_id = MakeOpId(1, 1);

  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), 1);

  while (segments.size() < 3) {
    ASSERT_OK(AppendNoOps(&op_id, kNumEntriesPerBatch));
    // Update the segments
    ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  }

  ASSERT_OK(log_->Close());

  int64_t wal_size_old = log_->metrics_->wal_size->value();
  BuildLog();
  int64_t wal_size_new = log_->metrics_->wal_size->value();

  ASSERT_EQ(wal_size_old, wal_size_new);
}

// Tests that segments roll over when max segment size is reached
// and that the player plays all entries in the correct order.
TEST_F(LogTest, TestSegmentRollover) {
  BuildLog();
  // Set a small segment size so that we have roll overs.
  log_->SetMaxSegmentSizeForTests(990);
  const int kNumEntriesPerBatch = 100;

  OpIdPB op_id = MakeOpId(1, 1);
  int num_entries = 0;

  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));

  while (segments.size() < 3) {
    ASSERT_OK(AppendNoOps(&op_id, kNumEntriesPerBatch));
    num_entries += kNumEntriesPerBatch;
    // Update the segments
    ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  }

  ReadableLogSegmentPtr last_segment = ASSERT_RESULT(segments.back());
  ASSERT_FALSE(last_segment->HasFooter());
  ASSERT_OK(log_->Close());

  std::unique_ptr<LogReader> reader;
  ASSERT_OK(LogReader::Open(
      fs_manager_->env(), nullptr, "Log reader: ", tablet_wal_path_, nullptr, nullptr, &reader));
  ASSERT_OK(reader->GetSegmentsSnapshot(&segments));

  last_segment = ASSERT_RESULT(segments.back());
  ASSERT_TRUE(last_segment->HasFooter());

  size_t total_read = 0;
  for (const scoped_refptr<ReadableLogSegment>& entry : segments) {
    auto read_entries = entry->ReadEntries();
    if (!read_entries.status.ok()) {
      FAIL() << "Failed to read entries in segment: " << entry->path()
          << ". Status: " << read_entries.status
          << ".\nSegments: " << DumpSegmentsToString(segments);
    }
    total_read += read_entries.entries.size();
  }

  ASSERT_EQ(num_entries, total_read);
}

TEST_F(LogTest, TestWriteAndReadToAndFromInProgressSegment) {
  const int kNumEntries = 4;
  BuildLog();

  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), 1);
  scoped_refptr<ReadableLogSegment> readable_segment = ASSERT_RESULT(segments.front());

  auto header_size = log_->TEST_ActiveSegment()->written_offset();
  ASSERT_GT(header_size, 0);
  readable_segment->UpdateReadableToOffset(header_size);

  // Reading the readable segment now should return OK but yield no
  // entries.
  auto read_entries = readable_segment->ReadEntries();
  ASSERT_OK(read_entries.status);
  ASSERT_EQ(read_entries.entries.size(), 0);

  // Dummy add_entry to help us estimate the size of what
  // gets written to disk.
  LogEntryBatchPB batch;
  OpIdPB op_id = MakeOpId(1, 1);
  batch.set_mono_time(1);
  LogEntryPB* log_entry = batch.add_entry();
  log_entry->set_type(REPLICATE);
  ReplicateMsg* repl = log_entry->mutable_replicate();
  repl->mutable_id()->CopyFrom(op_id);
  repl->set_op_type(NO_OP);
  repl->set_hybrid_time(0L);

  // Entries are prefixed with a header.
  auto single_entry_size = batch.ByteSize() + kEntryHeaderSize;

  ssize_t written_entries_size = header_size;
  ASSERT_OK(AppendNoOps(&op_id, kNumEntries, &written_entries_size));
  ASSERT_EQ(written_entries_size, log_->TEST_ActiveSegment()->written_offset());
  ASSERT_EQ(single_entry_size * kNumEntries, written_entries_size - header_size);

  // Updating the readable segment with the offset of the first entry should
  // make it read a single entry even though there are several in the log.
  readable_segment->UpdateReadableToOffset(header_size + single_entry_size);
  read_entries = readable_segment->ReadEntries();
  ASSERT_OK(read_entries.status);
  ASSERT_EQ(read_entries.entries.size(), 1);

  // Now append another entry so that the Log sets the correct readable offset
  // on the reader.
  ASSERT_OK(AppendNoOps(&op_id, 1, &written_entries_size));

  // Now the reader should be able to read all 5 entries.
  read_entries = readable_segment->ReadEntries();
  ASSERT_OK(read_entries.status);
  ASSERT_EQ(read_entries.entries.size(), 5);

  // Offset should get updated for an additional entry.
  ASSERT_EQ(single_entry_size * (kNumEntries + 1) + header_size,
            written_entries_size);
  ASSERT_EQ(written_entries_size, log_->TEST_ActiveSegment()->written_offset());

  // When we roll it should go back to the header size.
  ASSERT_OK(log_->AllocateSegmentAndRollOver());
  ASSERT_EQ(header_size, log_->TEST_ActiveSegment()->written_offset());
  written_entries_size = header_size;

  // Now that we closed the original segment. If we get a segment from the reader
  // again, we should get one with a footer and we should be able to read all entries.
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), 2);
  readable_segment = ASSERT_RESULT(segments.front());
  read_entries = readable_segment->ReadEntries();
  ASSERT_OK(read_entries.status);
  ASSERT_EQ(read_entries.entries.size(), 5);

  // Offset should get updated for an additional entry, again.
  ASSERT_OK(AppendNoOp(&op_id, &written_entries_size));
  ASSERT_EQ(single_entry_size  + header_size, written_entries_size);
  ASSERT_EQ(written_entries_size, log_->TEST_ActiveSegment()->written_offset());
}

// Tests that segments can be GC'd while the log is running.
TEST_F(LogTest, TestGCWithLogRunning) {
  BuildLog();

  vector<LogAnchor*> anchors;
  ElementDeleter deleter(&anchors);

  SegmentSequence segments;

  const int kNumTotalSegments = 4;
  const int kNumOpsPerSegment = 5;
  int num_gced_segments;
  OpIdPB op_id = MakeOpId(1, 1);
  int64_t anchored_index = -1;

  ASSERT_OK(AppendMultiSegmentSequence(kNumTotalSegments, kNumOpsPerSegment,
                                              &op_id, &anchors));

  // We should get 4 anchors, each pointing at the beginning of a new segment
  ASSERT_EQ(anchors.size(), 4);

  // Anchors should prevent GC.
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(4, segments.size()) << DumpSegmentsToString(segments);
  ASSERT_OK(log_anchor_registry_->GetEarliestRegisteredLogIndex(&anchored_index));
  ASSERT_OK(log_->GC(anchored_index, &num_gced_segments));
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(4, segments.size()) << DumpSegmentsToString(segments);

  // Freeing the first 2 anchors should allow GC of them.
  ASSERT_OK(log_anchor_registry_->Unregister(anchors[0]));
  ASSERT_OK(log_anchor_registry_->Unregister(anchors[1]));
  ASSERT_OK(log_anchor_registry_->GetEarliestRegisteredLogIndex(&anchored_index));
  // We should now be anchored on op 0.11, i.e. on the 3rd segment
  ASSERT_EQ(anchors[2]->log_index, anchored_index);

  // However, first, we'll try bumping the min retention threshold and
  // verify that we don't GC any.
  {
    google::FlagSaver saver;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 10;
    ASSERT_OK(log_->GC(anchored_index, &num_gced_segments));
    ASSERT_EQ(0, num_gced_segments);
  }

  // Try again without the modified flag.
  ASSERT_OK(log_->GC(anchored_index, &num_gced_segments));
  ASSERT_EQ(2, num_gced_segments) << DumpSegmentsToString(segments);
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(2, segments.size()) << DumpSegmentsToString(segments);

  // Release the remaining "rolled segment" anchor. GC will not delete the
  // last rolled segment.
  ASSERT_OK(log_anchor_registry_->Unregister(anchors[2]));
  ASSERT_OK(log_anchor_registry_->GetEarliestRegisteredLogIndex(&anchored_index));
  ASSERT_OK(log_->GC(anchored_index, &num_gced_segments));
  ASSERT_EQ(0, num_gced_segments) << DumpSegmentsToString(segments);
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(2, segments.size()) << DumpSegmentsToString(segments);

  // Check that we get a NotFound if we try to read before the GCed point.
  {
    ReplicateMsgs repls;
    int64_t starting_op_segment_seq_num;
    Status s = log_->GetLogReader()->ReadReplicatesInRange(
      1, 2, LogReader::kNoSizeLimit, &repls, &starting_op_segment_seq_num);
    ASSERT_TRUE(s.IsNotFound()) << s.ToString();
  }

  ASSERT_OK(log_->Close());
  CheckRightNumberOfSegmentFiles(2);

  // We skip the first three, since we unregistered them above.
  for (int i = 3; i < kNumTotalSegments; i++) {
    ASSERT_OK(log_anchor_registry_->Unregister(anchors[i]));
  }
}

// Test that, when we are set to retain a given number of log segments,
// we also retain any relevant log index chunks, even if those operations
// are not necessary for recovery.
TEST_F(LogTest, TestGCOfIndexChunks) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 4;
  BuildLog();

  const auto entries_per_chunk = TEST_GetEntriesPerIndexChunk();
  // Append some segments which cross from one index chunk into another.
  // entries_per_chunk-10 ... entries_per_chunk-6        \___ the first index
  // entries_per_chunk-5  ... entries_per_chunk-1        /    chunk points to these
  // entries_per_chunk    ... entries_per_chunk+4       \_
  // entries_per_chunk+5  ... entries_per_chunk+9        _|- the second index chunk points to these
  // entries_per_chunk+10 ... <still open>              /
  const int kNumTotalSegments = 5;
  const int kNumOpsPerSegment = 5;
  OpIdPB op_id = MakeOpId(1, entries_per_chunk - 10);
  ASSERT_OK(AppendMultiSegmentSequence(kNumTotalSegments, kNumOpsPerSegment,
                                              &op_id, /* anchors = */ nullptr));

  // Run a GC on an op in the second index chunk. We should remove only the
  // earliest segment, because we are set to retain 4.
  int num_gced_segments = 0;
  ASSERT_OK(log_->GC(entries_per_chunk + 6, &num_gced_segments));
  ASSERT_EQ(1, num_gced_segments);

  // And we should still be able to read ops in the retained segment, even though
  // the GC index was higher.
  auto loaded_op = ASSERT_RESULT(log_->GetLogReader()->LookupOpId(entries_per_chunk - 5));
  ASSERT_EQ(yb::OpId(1, entries_per_chunk - 5), loaded_op);

  // If we drop the retention count down to 1, we can now GC, and the log index
  // chunk should also be GCed.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  ASSERT_OK(log_->GC(entries_per_chunk + 3, &num_gced_segments));
  ASSERT_EQ(1, num_gced_segments);

  auto result = log_->GetLogReader()->LookupOpId(entries_per_chunk - 5);
  ASSERT_TRUE(!result.ok() && result.status().IsNotFound()) << "unexpected result: " << result;
}

// Tests that we can append FLUSH_MARKER messages to the log queue to make sure
// all messages up to a certain point were fsync()ed without actually
// writing them to the log.
TEST_F(LogTest, TestWaitUntilAllFlushed) {
  BuildLog();
  // Append 2 replicate pairs asynchronously
  AppendReplicateBatchToLog(2, AppendSync::kTrue);

  ASSERT_OK(log_->WaitUntilAllFlushed());

  // Make sure we only get 4 entries back and that no FLUSH_MARKER commit is found.
  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));

  const ReadableLogSegmentPtr& first_segment = ASSERT_RESULT(segments.front());
  auto read_entries = first_segment->ReadEntries();
  ASSERT_OK(read_entries.status);
  ASSERT_EQ(read_entries.entries.size(), 2);
  for (size_t i = 0; i < read_entries.entries.size(); i++) {
    ASSERT_TRUE(read_entries.entries[i]->has_replicate());
  }
}

// Tests log reopening and that GC'ing the old log's segments works.
TEST_F(LogTest, TestLogReopenAndGC) {
  BuildLog();

  SegmentSequence segments;

  vector<LogAnchor*> anchors;
  ElementDeleter deleter(&anchors);

  const int kNumTotalSegments = 3;
  const int kNumOpsPerSegment = 5;
  int num_gced_segments;
  OpIdPB op_id = MakeOpId(1, 1);
  int64_t anchored_index = -1;

  ASSERT_OK(AppendMultiSegmentSequence(kNumTotalSegments, kNumOpsPerSegment,
                                              &op_id, &anchors));
  // Anchors should prevent GC.
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(3, segments.size());
  ASSERT_OK(log_anchor_registry_->GetEarliestRegisteredLogIndex(&anchored_index));
  ASSERT_OK(log_->GC(anchored_index, &num_gced_segments));
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(3, segments.size());

  ASSERT_OK(log_->Close());

  // Now reopen the log as if we had replayed the state into the stores.
  // that were in memory and do GC.
  BuildLog();

  // The "old" data consists of 3 segments. We still hold anchors.
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(4, segments.size());

  // Write to a new log segment, as if we had taken new requests and the
  // mem stores are holding anchors, but don't roll it.
  CreateAndRegisterNewAnchor(op_id.index(), &anchors);
  ASSERT_OK(AppendNoOps(&op_id, kNumOpsPerSegment));

  // Now release the "old" anchors and GC them.
  for (int i = 0; i < 3; i++) {
    ASSERT_OK(log_anchor_registry_->Unregister(anchors[i]));
  }
  ASSERT_OK(log_anchor_registry_->GetEarliestRegisteredLogIndex(&anchored_index));

  // If we set the min_seconds_to_retain high, then we'll retain the logs even
  // though we could GC them based on our anchoring.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 500;
  ASSERT_OK(log_->GC(anchored_index, &num_gced_segments));
  ASSERT_EQ(0, num_gced_segments);

  // Turn off the time-based retention and try GCing again. This time
  // we should succeed.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 0;
  log_->set_wal_retention_secs(0);
  ASSERT_OK(log_->GC(anchored_index, &num_gced_segments));
  ASSERT_EQ(2, num_gced_segments);

  // After GC there should be only one left, besides the one currently being
  // written to. That is because min_segments_to_retain defaults to 2.
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(2, segments.size()) << DumpSegmentsToString(segments);
  ASSERT_OK(log_->Close());

  CheckRightNumberOfSegmentFiles(2);

  // Unregister the final anchor.
  ASSERT_OK(log_anchor_registry_->Unregister(anchors[3]));
}

// Helper to measure the performance of the log.
TEST_F(LogTest, TestWriteManyBatches) {
  uint64_t num_batches = 10;
  if (AllowSlowTests()) {
    num_batches = FLAGS_num_batches;
  }
  BuildLog();

  LOG(INFO)<< "Starting to write " << num_batches << " to log";
  LOG_TIMING(INFO, "Wrote all batches to log") {
    AppendReplicateBatchToLog(num_batches);
  }
  ASSERT_OK(log_->Close());
  LOG(INFO) << "Done writing";

  LOG_TIMING(INFO, "Read all entries from Log") {
    LOG(INFO) << "Starting to read log";
    uint32_t num_entries = 0;

    std::unique_ptr<LogReader> reader;
    ASSERT_OK(LogReader::Open(
        fs_manager_->env(), /* index= */ nullptr, "Log reader: ", tablet_wal_path_,
        /* table_metric_entity= */ nullptr, /* tablet_metric_entity= */ nullptr, &reader));

    SegmentSequence segments;
    ASSERT_OK(reader->GetSegmentsSnapshot(&segments));

    for (const scoped_refptr<ReadableLogSegment>& entry : segments) {
      auto read_entries = entry->ReadEntries();
      ASSERT_OK(read_entries.status);
      num_entries += read_entries.entries.size();
    }
    ASSERT_EQ(num_entries, num_batches);
    LOG(INFO) << "End readfile";
  }
}

// This tests that querying LogReader works.
// This sets up a reader with some segments to query which amount to the
// following:
// seg002: 0.10 through 0.19
// seg003: 0.20 through 0.29
// seg004: 0.30 through 0.39
TEST_F(LogTest, TestLogReader) {
  LogReader reader(fs_manager_->env(),
                   scoped_refptr<LogIndex>(),
                   "Log reader: ",
                   nullptr,
                   nullptr);
  ASSERT_OK(reader.InitEmptyReaderForTests());
  ASSERT_OK(AppendNewEmptySegmentToReader(2, 10, &reader));
  ASSERT_OK(AppendNewEmptySegmentToReader(3, 20, &reader));
  ASSERT_OK(AppendNewEmptySegmentToReader(4, 30, &reader));

  OpIdPB op;
  op.set_term(0);
  SegmentSequence segments;

  // Queries for segment prefixes (used for GC)

  // Asking the reader the prefix of segments that does not include op 1
  // should return the empty set.
  ASSERT_OK(reader.GetSegmentPrefixNotIncluding(1, &segments));
  ASSERT_TRUE(segments.empty());

  // .. same for op 10
  ASSERT_OK(reader.GetSegmentPrefixNotIncluding(10, &segments));
  ASSERT_TRUE(segments.empty());

  // Asking for the prefix of segments not including op 20 should return
  // the first segment, since 20 is the first operation in segment 3.
  ASSERT_OK(reader.GetSegmentPrefixNotIncluding(20, &segments));
  ASSERT_EQ(segments.size(), 1);
  const ReadableLogSegmentPtr& first_segment = ASSERT_RESULT(segments.front());
  ASSERT_EQ(first_segment->header().sequence_number(), 2);

  // Asking for 30 should include the first two.
  ASSERT_OK(reader.GetSegmentPrefixNotIncluding(30, &segments));
  ASSERT_EQ(segments.size(), 2);
  ASSERT_EQ((*segments.begin())->header().sequence_number(), 2);
  ASSERT_EQ((*(segments.begin() + 1))->header().sequence_number(), 3);

  // Asking for anything higher should return all segments.
  ASSERT_OK(reader.GetSegmentPrefixNotIncluding(1000, &segments));
  ASSERT_EQ(segments.size(), 3);
  ASSERT_EQ((*segments.begin())->header().sequence_number(), 2);
  ASSERT_EQ((*(segments.begin() + 1))->header().sequence_number(), 3);
  ASSERT_EQ((*(segments.begin() + 2))->header().sequence_number(), 4);

  // Queries for specific segment sequence numbers.
  ReadableLogSegmentPtr segment = ASSERT_RESULT(reader.GetSegmentBySequenceNumber(2));
  ASSERT_EQ(2, segment->header().sequence_number());
  segment = ASSERT_RESULT(reader.GetSegmentBySequenceNumber(3));
  ASSERT_EQ(3, segment->header().sequence_number());

  segment = ASSERT_RESULT(reader.GetSegmentBySequenceNumber(4));
  ASSERT_EQ(4, segment->header().sequence_number());

  auto result = reader.GetSegmentBySequenceNumber(5);
  ASSERT_TRUE(!result.ok() && result.status().IsNotFound());
}

// Test that, even if the LogReader's index is empty because no segments
// have been properly closed, we can still read the entries as the reader
// returns the current segment.
TEST_F(LogTest, TestLogReaderReturnsLatestSegmentIfIndexEmpty) {
  BuildLog();

  AppendReplicateBatch({
    .op_id = {1, 1},
    .committed_op_id = {0, 0},
    .writes = {},
    .sync = AppendSync::kTrue,
  });

  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), 1);

  const ReadableLogSegmentPtr& first_segment = ASSERT_RESULT(segments.front());
  auto read_entries = first_segment->ReadEntries();
  ASSERT_OK(read_entries.status);
  ASSERT_EQ(1, read_entries.entries.size());
}

TEST_F(LogTest, TestOpIdUtils) {
  OpIdPB id = MakeOpId(1, 2);
  ASSERT_EQ("1.2", consensus::OpIdToString(id));
  ASSERT_EQ(1, id.term());
  ASSERT_EQ(2, id.index());
}

std::ostream& operator<<(std::ostream& os, const TestLogSequenceElem& elem) {
  switch (elem.type) {
    case TestLogSequenceElem::ROLL:
      os << "ROLL";
      break;
    case TestLogSequenceElem::REPLICATE:
      os << "R" << elem.id;
      break;
  }
  return os;
}

// Generates a plausible sequence of items in the log, including term changes, moving the
// index backwards, log rolls, etc.
//
// NOTE: this log sequence may contain some aberrations which would not occur in a real
// consensus log, but our API supports them. In the future we may want to add assertions
// to the Log implementation that prevent such aberrations, in which case we'd need to
// modify this.
void LogTest::GenerateTestSequence(size_t seq_len,
                                   vector<TestLogSequenceElem>* ops,
                                   vector<int64_t>* terms_by_index) {
  auto rng = &ThreadLocalRandom();
  terms_by_index->assign(seq_len + 1, -1);
  int64_t committed_index = 0;
  int64_t max_repl_index = 0;

  OpIdPB id = MakeOpId(1, 0);
  for (size_t i = 0; i < seq_len; i++) {
    if (RandomUniformInt(0, 4, rng) == 0) {
      // Reset term - it may stay the same, or go up/down
      id.set_term(std::max(static_cast<int64_t>(1), id.term() + RandomUniformInt(0, 4, rng) - 2));
    }

    // Advance index by exactly one
    id.set_index(id.index() + 1);

    if (RandomUniformInt(0, 4, rng) == 0) {
      // Move index backward a bit, but not past the committed index
      id.set_index(std::max(committed_index + 1, id.index() - RandomUniformInt(0, 4, rng)));
    }

    // Roll the log sometimes
    if (i != 0 && RandomUniformInt(0, 14, rng) == 0) {
      TestLogSequenceElem op;
      op.type = TestLogSequenceElem::ROLL;
      ops->push_back(op);
    }

    TestLogSequenceElem op;
    op.type = TestLogSequenceElem::REPLICATE;
    op.id = id;
    ops->push_back(op);
    (*terms_by_index)[id.index()] = id.term();
    max_repl_index = std::max(max_repl_index, id.index());
  }
  terms_by_index->resize(max_repl_index + 1);
}

void LogTest::AppendTestSequence(const vector<TestLogSequenceElem>& seq) {
  for (const TestLogSequenceElem& e : seq) {
    VLOG(1) << "Appending: " << e;
    switch (e.type) {
      case TestLogSequenceElem::REPLICATE:
      {
        OpIdPB id(e.id);
        ASSERT_OK(AppendNoOp(&id));
        break;
      }
      case TestLogSequenceElem::ROLL:
      {
        ASSERT_OK(RollLog());
      }
    }
  }
}

// Test that if multiple REPLICATE entries are written for the same index,
// that we read the latest one.
//
// This is a randomized test: we generate a plausible sequence of log messages,
// write it out, and then read random ranges of log indexes, making sure we
// always see the correct term for each REPLICATE message (i.e whichever term
// was the last to append it).
TEST_F(LogTest, TestReadLogWithReplacedReplicates) {
  const int kSequenceLength = AllowSlowTests() ? 1000 : 50;

  vector<int64_t> terms_by_index;
  vector<TestLogSequenceElem> seq;
  GenerateTestSequence(kSequenceLength, &seq, &terms_by_index);
  LOG(INFO) << "test sequence: " << seq;
  const int64_t max_repl_index = terms_by_index.size() - 1;
  LOG(INFO) << "max_repl_index: " << max_repl_index;

  // Write the test sequence to the log.
  // TODO: should consider adding batching here of multiple replicates
  BuildLog();
  AppendTestSequence(seq);

  const int kNumRandomReads = 100;

  // We'll advance 'gc_index' randomly through the log until we've gotten to
  // the end. This ensures that, when we GC, we don't ever remove the latest
  // version of a replicate message unintentionally.
  LogReader* reader = log_->GetLogReader();
  for (int gc_index = 1; gc_index < max_repl_index;) {
    SCOPED_TRACE(Substitute("after GCing $0", gc_index));

    // Test reading random ranges of indexes and verifying that we get back the
    // REPLICATE messages with the correct terms
    for (int random_read = 0; random_read < kNumRandomReads; random_read++) {
      auto start_index = RandomUniformInt<int64_t>(gc_index, max_repl_index - 1);
      auto end_index = RandomUniformInt<int64_t>(start_index, max_repl_index);
      int64_t starting_op_segment_seq_num;
      {
        SCOPED_TRACE(Substitute("Reading $0-$1", start_index, end_index));
        consensus::ReplicateMsgs repls;
        ASSERT_OK(reader->ReadReplicatesInRange(start_index, end_index,
                                                LogReader::kNoSizeLimit, &repls,
                                                &starting_op_segment_seq_num));
        ASSERT_EQ(end_index - start_index + 1, repls.size());
        auto expected_index = start_index;
        for (const auto& repl : repls) {
          ASSERT_EQ(expected_index, repl->id().index());
          ASSERT_EQ(terms_by_index[expected_index], repl->id().term());
          expected_index++;
        }
      }

      int64_t bytes_read = log_->reader_->bytes_read_->value();
      int64_t entries_read = log_->reader_->entries_read_->value();
      int64_t read_batch_count = log_->reader_->read_batch_latency_->TotalCount();
      EXPECT_GT(log_->reader_->bytes_read_->value(), 0);
      EXPECT_GT(log_->reader_->entries_read_->value(), 0);
      EXPECT_GT(log_->reader_->read_batch_latency_->TotalCount(), 0);

      // Test a size-limited read.
      int size_limit = RandomUniformInt(1, 1000);
      {
        SCOPED_TRACE(Substitute("Reading $0-$1 with size limit $2",
                                start_index, end_index, size_limit));
        ReplicateMsgs repls;
        ASSERT_OK(reader->ReadReplicatesInRange(start_index, end_index, size_limit, &repls,
                                                &starting_op_segment_seq_num));
        ASSERT_LE(repls.size(), end_index - start_index + 1);
        int total_size = 0;
        auto expected_index = start_index;
        for (const auto& repl : repls) {
          ASSERT_EQ(expected_index, repl->id().index());
          ASSERT_EQ(terms_by_index[expected_index], repl->id().term());
          expected_index++;
          total_size += repl->SerializedSize();
        }
        if (total_size > size_limit) {
          ASSERT_EQ(1, repls.size());
        } else {
          ASSERT_LE(total_size, size_limit);
        }
      }

      EXPECT_GT(log_->reader_->bytes_read_->value(), bytes_read);
      EXPECT_GT(log_->reader_->entries_read_->value(), entries_read);
      EXPECT_GT(log_->reader_->read_batch_latency_->TotalCount(), read_batch_count);
    }

    int num_gced = 0;
    ASSERT_OK(log_->GC(gc_index, &num_gced));
    gc_index += RandomUniformInt(0, 9);
  }
}

// Ensure that we can read replicate messages from the LogReader with a very
// high (> 32 bit) log index and term. Regression test for KUDU-1933.
TEST_F(LogTest, TestReadReplicatesHighIndex) {
  const int64_t first_log_index = std::numeric_limits<int32_t>::max() - 3;
  const int kSequenceLength = 10;

  BuildLog();
  OpIdPB op_id;
  op_id.set_term(first_log_index);
  op_id.set_index(first_log_index);
  ASSERT_OK(AppendNoOps(&op_id, kSequenceLength));

  auto* reader = log_->GetLogReader();
  ReplicateMsgs repls;
  int64_t starting_op_segment_seq_num;
  ASSERT_OK(reader->ReadReplicatesInRange(first_log_index, first_log_index + kSequenceLength - 1,
                                          LogReader::kNoSizeLimit, &repls,
                                          &starting_op_segment_seq_num));
  ASSERT_EQ(kSequenceLength, repls.size());
}

TEST_F(LogTest, AllocateSegmentAndRollOver) {
  constexpr auto kNumIters = 10;

  // Big enough to not trigger automated rollover and test manual one.
  options_.segment_size_bytes = 1_MB;

  BuildLog();

  ASSERT_EQ(log_->num_segments(), 1);
  ASSERT_EQ(log_->active_segment_sequence_number(), 1);

  for (auto i = 0; i < kNumIters; ++i) {
    AppendReplicateBatchToLog(1, AppendSync::kTrue);
    ASSERT_OK(log_->AllocateSegmentAndRollOver());
  }

  ASSERT_EQ(log_->num_segments(), kNumIters + 1);
  ASSERT_EQ(log_->active_segment_sequence_number(), kNumIters + 1);

  ASSERT_OK(log_->Close());
}

TEST_F(LogTest, ConcurrentAllocateSegmentAndRollOver) {
  constexpr auto kNumBatches = 10;
  constexpr auto kNumEntriesPerBatch = 10;

  // Trigger rollover aggressively during normal append.
  options_.segment_size_bytes = 1;

  BuildLog();

  for (auto i = 0; i < kNumBatches; ++i) {
    AppendReplicateBatchToLog(kNumEntriesPerBatch, AppendSync::kFalse);
    ASSERT_OK(log_->AllocateSegmentAndRollOver());
  }

  LOG(INFO) << "Log segments: " << log_->num_segments();
  ASSERT_GE(log_->num_segments(), kNumBatches);

  ASSERT_OK(log_->Close());
}

Result<std::vector<OpId>> LogTest::AppendAndCopy(size_t num_batches, size_t num_entries_per_batch) {
  std::vector<OpId> copied_up_to_op_id;
  copied_up_to_op_id.reserve(num_batches);
  for (size_t i = 0; i < num_batches; ++i) {
    AppendReplicateBatchToLog(
        num_entries_per_batch, i % 2 == 0 ? AppendSync::kFalse : AppendSync::kTrue);
    copied_up_to_op_id.push_back(log_->GetLatestEntryOpId());
    RETURN_NOT_OK(log_->CopyTo(GetLogCopyPath(i)));
  }
  return copied_up_to_op_id;
}

namespace {

Status CheckEntryEq(const LogEntries& lhs, const LogEntries& rhs, const size_t entry_idx) {
  SCHECK_EQ(
      lhs[entry_idx]->ShortDebugString(), rhs[entry_idx]->ShortDebugString(), InternalError,
      Format("entries[$0]", entry_idx));
  return Status::OK();
}

Status CheckReadEntriesResultEq(
    const ReadEntriesResult& lhs, const ReadEntriesResult& rhs) {
  SCHECK_EQ(lhs.committed_op_id, rhs.committed_op_id, InternalError, "committed_op_id");
  SCHECK_EQ(lhs.end_offset, rhs.end_offset, InternalError, "end_offset");
  SCHECK_EQ(lhs.entry_metadata, rhs.entry_metadata, InternalError, "entry_metadata");
  SCHECK_EQ(lhs.entries.size(), rhs.entries.size(), InternalError, "entries.size()");
  for (size_t entry_idx = 0; entry_idx < lhs.entries.size(); ++entry_idx) {
    RETURN_NOT_OK(CheckEntryEq(lhs.entries, rhs.entries, entry_idx));
  }
  return Status::OK();
}

// Checks that lhs is a prefix of rhs.
Status CheckReadEntriesResultIsCorrectPrefixOf(
    const ReadEntriesResult& lhs, const ReadEntriesResult& rhs) {
  SCHECK_LE(
      lhs.committed_op_id, rhs.committed_op_id, InternalError,
      "committed_op_id");
  if (!lhs.entries.empty()) {
    SCHECK_LE(
        lhs.committed_op_id, OpId::FromPB(lhs.entries.back()->replicate().id()), InternalError,
        "committed_op_id should be <= last op id");
  }

  const auto num_lhs_entries = lhs.entries.size();

  SCHECK_LE(num_lhs_entries, rhs.entries.size(), InternalError, "entries.size()");
  SCHECK_EQ(
      lhs.entry_metadata.size(), num_lhs_entries, InternalError,
      "entry_metadata.size()");
  for (size_t entry_idx = 0; entry_idx < num_lhs_entries; ++entry_idx) {
    SCHECK_EQ(
        lhs.entry_metadata[entry_idx].entry_time, rhs.entry_metadata[entry_idx].entry_time,
        InternalError, Format("entry_metadata[$0].entry_time", entry_idx));
    SCHECK_EQ(
        lhs.entry_metadata[entry_idx].active_segment_sequence_number,
        rhs.entry_metadata[entry_idx].active_segment_sequence_number, InternalError,
        Format("entry_metadata[$0].active_segment_sequence_number", entry_idx));
    RETURN_NOT_OK(CheckEntryEq(lhs.entries, rhs.entries, entry_idx));
  }
  return Status::OK();
}

} // namespace

// Verifies CopyTo works in parallel with rollovers triggered by concurrent
// log entries writes.
TEST_F(LogTest, CopyTo) {
  constexpr auto kNumBatches = 10;
  constexpr auto kNumEntriesPerBatch = 10;

  // Trigger rollover aggressively during normal append.
  options_.segment_size_bytes = 1;

  BuildLog();

  auto copied_up_to_op_id = ASSERT_RESULT(AppendAndCopy(kNumBatches, kNumEntriesPerBatch));

  SegmentSequence segments;
  ASSERT_OK(log_->GetSegmentsSnapshot(&segments));

  for (auto i = 0; i < kNumBatches; ++i) {
    auto copied_segments = ASSERT_RESULT(
        GetSegmentsFromLogCopyAndCheckMaxOpIndex(i, copied_up_to_op_id[i].index));
    ASSERT_LE(copied_segments.size(), segments.size());

    // Copied log segments should match log segments of the original log.
    for (auto segment_it = segments.begin(), segment_copy_it = copied_segments.begin();
         segment_it != segments.end() && segment_copy_it != copied_segments.end();
         ++segment_it, ++segment_copy_it) {
      auto& segment = *segment_it;
      auto& segment_copy = *segment_copy_it;

      auto entries_result = segment->ReadEntries();
      ASSERT_OK(entries_result.status);
      auto entries_copy_result = segment_copy->ReadEntries();
      ASSERT_OK(entries_copy_result.status);

      ASSERT_OK(CheckReadEntriesResultEq(entries_copy_result, entries_result));
    }
  }

  ASSERT_OK(log_->Close());
}

// Verifies CopyTo works in parallel with rollovers triggered by concurrent
// log entries writes and log GC.
TEST_F(LogTest, CopyToWithConcurrentGc) {
  constexpr auto kNumBatches = 20;
  constexpr auto kNumEntriesPerBatch = 10;

  // Trigger rollover aggressively during normal append.
  options_.segment_size_bytes = 1;

  BuildLog();

  log_->set_wal_retention_secs(0);
  std::atomic<bool> stop_gc{false};
  std::thread gc_thread([log = log_.get(), &stop_gc]{
    while (!stop_gc.load()) {
      auto gc_index = log->GetLatestEntryOpId().index;
      int num_gced = 0;
      ASSERT_OK(log->GC(gc_index, &num_gced));
    }
  });

  auto copied_up_to_op_id_result = AppendAndCopy(kNumBatches, kNumEntriesPerBatch);
  stop_gc = true;
  gc_thread.join();
  auto copied_up_to_op_id = ASSERT_RESULT(std::move(copied_up_to_op_id_result));

  for (auto i = 0; i < kNumBatches; ++i) {
    auto copied_segments = ASSERT_RESULT(
        GetSegmentsFromLogCopyAndCheckMaxOpIndex(i, copied_up_to_op_id[i].index));

    // Make sure copied log contains a sequence of entries without gaps in index.
    int64_t last_index = -1;
    for (const auto& segment_copy : copied_segments) {
      auto entries_copy_result = segment_copy->ReadEntries();
      ASSERT_OK(entries_copy_result.status);

      for (const auto& entry : entries_copy_result.entries) {
        const auto index = entry->replicate().id().index();
        if (last_index >= 0) {
          ASSERT_EQ(index, last_index + 1);
        }
        last_index = index;
      }
    }
  }

  ASSERT_OK(log_->Close());
}

Result<std::vector<LogTest::Op>> LogTest::GenerateOpsAndAppendToLog(
    const int num_segments, const int num_batches_per_segment, const int num_entries_per_batch,
    const double commit_probability, const int num_approx_term_changes,
    const int num_approx_term_changes_with_index_rollback) {
  const auto num_entries = num_segments * num_batches_per_segment * num_entries_per_batch;

  std::vector<Op> ops;
  auto current_op_id = OpId(1, 1);
  OpId last_committed_op_id;
  for (auto seg_idx = 0; seg_idx < num_segments; ++seg_idx) {
    for (auto batch_idx = 0; batch_idx < num_batches_per_segment; ++batch_idx) {
      ReplicateMsgs replicates;
      for (int i = 0; i < num_entries_per_batch; i++) {
        replicates.emplace_back(rpc::MakeSharedMessage<consensus::LWReplicateMsg>());
        auto& replicate = replicates.back();
        current_op_id.ToPB(replicate->mutable_id());
        replicate->set_op_type(NO_OP);
        replicate->set_hybrid_time(clock_->Now().ToUint64());
        ops.push_back({current_op_id, false});

        if (RandomActWithProbability(commit_probability)) {
          last_committed_op_id = current_op_id;
          // Commit not yet committed ops from this term.
          for (auto op = ops.end() - 1;
               op >= ops.begin() && op->id.term == current_op_id.term && !op->committed;
               op--) {
            op->committed = true;
          }
        }
        if (RandomActWithProbability(1.0 * num_approx_term_changes / num_entries)) {
          ++current_op_id.term;
          if (RandomActWithProbability(
                  1.0 * num_approx_term_changes_with_index_rollback / num_approx_term_changes)) {
            // Do not overwrite committed ops.
            current_op_id.index = std::max<int64_t>(
                std::max<int64_t>(
                    current_op_id.index - RandomUniformInt<int64_t>(1, 10),
                    last_committed_op_id.index),
                1);
          }
        }
        ++current_op_id.index;
      }
      Synchronizer s;
      RETURN_NOT_OK(log_->AsyncAppendReplicates(
          replicates, last_committed_op_id, restart_safe_coarse_mono_clock_.Now(),
          s.AsStatusCallback()));
      RETURN_NOT_OK(s.Wait());
    }
    RETURN_NOT_OK(log_->AllocateSegmentAndRollOver());
  }
  return ops;
}

namespace {

Status CheckLogIndex(
    LogReader* log_reader,
    const std::map<int64_t, std::pair<OpId, LogEntryMetadata>>& op_id_with_entry_meta_by_idx,
    const int64_t last_op_index) {
  if (op_id_with_entry_meta_by_idx.empty()) {
    // Nothing to verify.
    return Status::OK();
  }
  const auto min_op_idx = op_id_with_entry_meta_by_idx.begin()->first;
  const auto max_op_idx = op_id_with_entry_meta_by_idx.rbegin()->first;
  VLOG(1) << "op_id_with_min_index: " << op_id_with_entry_meta_by_idx.begin()->second.first
          << " op_id_with_max_index: " << op_id_with_entry_meta_by_idx.rbegin()->second.first
          << " entries: " << op_id_with_entry_meta_by_idx.size();

  // We iterate in reverse order on purpose in order to verify log index lazy loading logic (see
  // LogReader::GetIndexEntry).
  for (auto iter = op_id_with_entry_meta_by_idx.rbegin();
       iter != op_id_with_entry_meta_by_idx.rend(); ++iter) {
    const auto& op_id_with_entry_meta = *iter;
    const auto& op_id = op_id_with_entry_meta.second.first;
    const auto& entry_meta = op_id_with_entry_meta.second.second;
    VLOG(1) << "op_id_with_entry_meta: " << AsString(op_id_with_entry_meta);
    SCHECK_EQ(op_id_with_entry_meta.first, op_id.index, InternalError, "op index mismatch");
    const auto index_entry = log_reader->TEST_GetIndexEntry(op_id.index);
    if (!index_entry.ok()) {
      if (index_entry.status().IsNotFound() && op_id.index > last_op_index) {
        // This is OK to get NotFound for operation index that could be overwritten.
        continue;
      }
      return index_entry.status();
    }
    SCHECK_EQ(
        index_entry->op_id.index, op_id.index, InternalError, Format("index of op_id: $0", op_id));
    SCHECK_GE(
        index_entry->op_id.term, op_id.term, InternalError, Format("term of op_id: $0", op_id));
    // Operation could be rewritten in higher term, then we don't expect offset and segment
    // number to match, it will be checked once we get to ops with the same index from that
    // higher term.
    if (index_entry->op_id.term == op_id.term) {
      SCHECK_EQ(
          index_entry->segment_sequence_number, entry_meta.active_segment_sequence_number,
          InternalError, Format("segment_sequence_number for op_id: $0", op_id));
      SCHECK_EQ(
          index_entry->offset_in_segment, entry_meta.offset, InternalError,
          Format("offset_in_segment for op_id: $0", op_id));
    }
  }
  SCHECK_GT(min_op_idx, 0, InternalError, "min_op_idx");
  SCHECK_GT(max_op_idx, 0, InternalError, "max_op_idx");

  if (min_op_idx > 1) {
    // GetEntry is not supported for zero index, so only check for min_op_idx > 1.
    const auto result = log_reader->TEST_GetIndexEntry(min_op_idx - 1);
    SCHECK_FORMAT(
        !result.ok() && result.status().IsNotFound(), InternalError,
        "Expected NotFound error for getting op by index before the minimal one, but got: $0, "
        "min_op_idx: $1",
        result, min_op_idx);
  }
  return Status::OK();
}

} // namespace

TEST_F(LogTest, CopyUpTo) {
  constexpr auto kNumSegments = 5;
  constexpr auto kNumBatchesPerSegment = 5;
  constexpr auto kNumEntriesPerBatch = 5;
  constexpr auto kNumApproxTermChanges = 10;
  constexpr auto kNumApproxTermChangesWithIndexRollback = 3;
  constexpr auto kCommitProbability = 0.1;

  // We will rollover segments manually.
  options_.segment_size_bytes = std::numeric_limits<size_t>::max();

  BuildLog();

  auto ops = ASSERT_RESULT(GenerateOpsAndAppendToLog(
      kNumSegments, kNumBatchesPerSegment, kNumEntriesPerBatch, kCommitProbability,
      kNumApproxTermChanges, kNumApproxTermChangesWithIndexRollback));

  SegmentSequence segments;
  ASSERT_OK(log_->GetSegmentsSnapshot(&segments));

  constexpr auto kLogCopyIdx = 0;
  const auto log_copy_path = GetLogCopyPath(kLogCopyIdx);
  for (size_t copy_num_ops = 1; copy_num_ops <= ops.size(); ++copy_num_ops) {
    const auto& max_included_op = ops[copy_num_ops - 1];
    const auto& max_included_op_id = max_included_op.id;
    ASSERT_OK(options_.env->DeleteRecursively(log_copy_path));
    VLOG(1) << "max_included_op_id: " << AsString(max_included_op_id);
    ASSERT_OK(log_->CopyTo(log_copy_path, max_included_op_id));

    auto log_copy_reader = ASSERT_RESULT(GetLogCopyReader(kLogCopyIdx));
    auto copied_segments = ASSERT_RESULT(
        GetSegmentsAndCheckMaxOpIndex(log_copy_reader.get(), max_included_op_id.index));

    // Copied log segments (except might be the last one that could be truncated) should match log
    // segments of the original log.
    size_t num_ops_copied = 0;
    std::map<int64_t, std::pair<OpId, LogEntryMetadata>> op_id_with_entry_meta_by_idx;

    for (auto segment_it = segments.begin(), segment_copy_it = copied_segments.begin();
         segment_it != segments.end() && segment_copy_it != copied_segments.end();
         ++segment_it, ++segment_copy_it) {
      auto& segment = *segment_it;
      auto& segment_copy = *segment_copy_it;

      auto entries_result = segment->ReadEntries();
      ASSERT_OK(entries_result.status);
      if (!entries_result.entries.empty()) {
        ASSERT_LE(
            entries_result.committed_op_id,
            OpId::FromPB(entries_result.entries.back()->replicate().id()));
      }

      auto entries_copy_result = segment_copy->ReadEntries();
      ASSERT_OK(entries_copy_result.status);
      const auto num_copied_segment_entries = entries_copy_result.entries.size();

      if ((segment_copy_it + 1) == copied_segments.end() && copy_num_ops != ops.size()) {
        // Segment is the last segment and truncated.
        ASSERT_OK(CheckReadEntriesResultIsCorrectPrefixOf(entries_copy_result, entries_result));
      } else {
        ASSERT_OK(CheckReadEntriesResultEq(entries_copy_result, entries_result));
      }

      bool has_replicated_entries = false;
      for (size_t entry_idx = 0; entry_idx < num_copied_segment_entries; ++entry_idx) {
        const auto& copied_entry = entries_copy_result.entries[entry_idx];
        if (copied_entry->has_replicate()) {
          has_replicated_entries = true;
          const auto& op_id = OpId::FromPB(copied_entry->replicate().id());
          op_id_with_entry_meta_by_idx[op_id.index] =
              std::make_pair(op_id, entries_copy_result.entry_metadata[entry_idx]);
        }
      }
      num_ops_copied += num_copied_segment_entries;
      ASSERT_EQ(has_replicated_entries, segment_copy->footer().has_close_timestamp_micros());
    }

    if (num_ops_copied > copy_num_ops) {
      LOG(INFO) << "max_included_op_id: " << AsString(max_included_op_id) << ", copied ops tail:";
      for (auto i = copy_num_ops - 1; i < num_ops_copied; ++i) {
        LOG(INFO) << i << ": " << ops[i].id;
      }
    }
    ASSERT_EQ(num_ops_copied, copy_num_ops);

    ASSERT_OK(CheckLogIndex(
        log_copy_reader.get(), op_id_with_entry_meta_by_idx, max_included_op_id.index));
  }

  ASSERT_OK(log_->Close());
}

// This test generate segments with random commits, term changes and some empty segments. We should
// be able to read older ops that are not in the log cache after a log restart.
TEST_F(LogTest, TestLogIndex) {
  google::SetVLOGLevel("log*", 10);

  constexpr auto kNumSegments = 5;
  constexpr auto kNumBatchesPerSegment = 5;
  constexpr auto kNumEntriesPerBatch = 5;
  constexpr auto kNumApproxTermChanges = 10;
  constexpr auto kNumApproxTermChangesWithIndexRollback = 3;
  constexpr auto kCommitProbability = 0.1;

  // We will rollover segments manually.
  options_.segment_size_bytes = std::numeric_limits<size_t>::max();

  auto read_all_indexes = [this](int64_t last_index) -> Status {
    SCHECK_GT(last_index, 0, IllegalState, "last_index should be > 0");
    for (auto index = last_index; index > 0; --index) {
      auto log_index_entry = VERIFY_RESULT(log_->GetLogReader()->TEST_GetIndexEntry(index));
      SCHECK_EQ(log_index_entry.op_id.index, index, IllegalState, "index mismatch");
    }
    return Status::OK();
  };

  BuildLog();

  auto ops = ASSERT_RESULT(GenerateOpsAndAppendToLog(
      kNumSegments, kNumBatchesPerSegment, kNumEntriesPerBatch, kCommitProbability,
      kNumApproxTermChanges, kNumApproxTermChangesWithIndexRollback));
  ASSERT_GT(ops.size(), 0);

  SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), kNumSegments + 1);
  ASSERT_OK(read_all_indexes(ops.rbegin()->id.index));

  // Restart the log to generate an empty segment.
  // Closing the log will leave the last segment empty with no replicates and a footer that does not
  // have max_replicate_index.
  ASSERT_OK(log_->Close());
  BuildLog();
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), kNumSegments + 2);
  auto read_entries = segments.rbegin()->get()->ReadEntries();
  ASSERT_EQ(read_entries.entries.size(), 0);

  ASSERT_OK(read_all_indexes(ops.rbegin()->id.index));

  // Write more data so that the empty segment is somewhere in the middle.
  ops = ASSERT_RESULT(GenerateOpsAndAppendToLog(
      kNumSegments, kNumBatchesPerSegment, kNumEntriesPerBatch, kCommitProbability,
      kNumApproxTermChanges, kNumApproxTermChangesWithIndexRollback));
  ASSERT_GT(ops.size(), 0);

  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), 2 * kNumSegments + 2);
  ASSERT_OK(read_all_indexes(ops.rbegin()->id.index));

  // Restart the log again to generate empty segments at the end.
  ASSERT_OK(log_->Close());
  BuildLog();

  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(segments.size(), 2 * kNumSegments + 3);
  read_entries = segments.rbegin()->get()->ReadEntries();
  ASSERT_EQ(read_entries.entries.size(), 0);

  ASSERT_OK(read_all_indexes(ops.rbegin()->id.index));

  ASSERT_OK(log_->Close());
}

} // namespace log
} // namespace yb
