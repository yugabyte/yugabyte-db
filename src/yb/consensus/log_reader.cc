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

#include "yb/consensus/log_reader.h"

#include <algorithm>
#include <mutex>

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/log.messages.h"
#include "yb/consensus/log_index.h"
#include "yb/consensus/log_util.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/std_util.h"

using std::string;

DEFINE_RUNTIME_bool(enable_log_retention_by_op_idx, true,
            "If true, logs will be retained based on an op id passed by the cdc service");

DEFINE_UNKNOWN_int32(log_max_seconds_to_retain, 24 * 3600, "Log files that are older will be "
             "deleted even if they contain cdc unreplicated entries. If 0, this flag will be "
             "ignored. This flag is ignored if a log segment contains entries that haven't been"
             "flushed to RocksDB.");

DEFINE_UNKNOWN_int64(log_stop_retaining_min_disk_mb, 100 * 1024, "Stop retaining logs if the space "
             "available for the logs falls below this limit. This flag is ignored if a log segment "
             "contains unflushed entries.");

METRIC_DEFINE_counter(tablet, log_reader_bytes_read, "Bytes Read From Log",
                      yb::MetricUnit::kBytes,
                      "Data read from the WAL since tablet start");

METRIC_DEFINE_counter(tablet, log_reader_entries_read, "Entries Read From Log",
                      yb::MetricUnit::kEntries,
                      "Number of entries read from the WAL since tablet start");

METRIC_DEFINE_event_stats(table, log_reader_read_batch_latency, "Log Read Latency",
                        yb::MetricUnit::kBytes,
                        "Microseconds spent reading log entry batches");

DEFINE_test_flag(bool, record_segments_violate_max_time_policy, false,
    "If set, everytime GetSegmentPrefixNotIncluding runs, segments that violate the max time "
    "policy will be appended to LogReader::TEST_segments_violate_max_time_policy_.");

DEFINE_test_flag(bool, record_segments_violate_min_space_policy, false,
    "If set, everytime GetSegmentPrefixNotIncluding runs, segments that violate the max time "
    "policy will be appended to LogReader::TEST_segments_violate_min_space_policy_.");

DEFINE_UNKNOWN_bool(get_changes_honor_deadline, true,
            "Toggle whether to honor the deadline passed to log reader");

DEFINE_test_flag(int32, get_changes_read_loop_delay_ms, 0,
                 "Amount of time to sleep for between each iteration of the loop in "
                 "ReadReplicatesInRange. This is used to test the return of partial results.");

namespace yb {
namespace log {

namespace {
struct LogSegmentSeqnoComparator {
  bool operator() (const scoped_refptr<ReadableLogSegment>& a,
                   const scoped_refptr<ReadableLogSegment>& b) {
    return a->header().sequence_number() < b->header().sequence_number();
  }
};
}

using strings::Substitute;

const int64_t LogReader::kNoSizeLimit = -1;

Status LogReader::Open(Env *env,
                       const scoped_refptr<LogIndex>& index,
                       std::string log_prefix,
                       const std::string& tablet_wal_path,
                       const scoped_refptr<MetricEntity>& table_metric_entity,
                       const scoped_refptr<MetricEntity>& tablet_metric_entity,
                       std::unique_ptr<LogReader> *reader) {
  std::unique_ptr<LogReader> log_reader(new LogReader(
      env, index, std::move(log_prefix), table_metric_entity, tablet_metric_entity));

  RETURN_NOT_OK(log_reader->Init(tablet_wal_path));
  *reader = std::move(log_reader);
  return Status::OK();
}

LogReader::LogReader(Env* env,
                     const scoped_refptr<LogIndex>& index,
                     std::string log_prefix,
                     const scoped_refptr<MetricEntity>& table_metric_entity,
                     const scoped_refptr<MetricEntity>& tablet_metric_entity)
    : env_(env),
      log_index_(index),
      log_prefix_(std::move(log_prefix)),
      state_(kLogReaderInitialized) {
  if (table_metric_entity) {
    read_batch_latency_ = METRIC_log_reader_read_batch_latency.Instantiate(table_metric_entity);
  }
  if (tablet_metric_entity) {
    bytes_read_ = METRIC_log_reader_bytes_read.Instantiate(tablet_metric_entity);
    entries_read_ = METRIC_log_reader_entries_read.Instantiate(tablet_metric_entity);
  }
  if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_enable_log_retention_by_op_idx) &&
                        (FLAGS_TEST_record_segments_violate_max_time_policy ||
                         FLAGS_TEST_record_segments_violate_min_space_policy))) {
    TEST_segments_violate_max_time_policy_ = std::make_unique<std::vector<ReadableLogSegmentPtr>>();
    TEST_segments_violate_min_space_policy_ =
        std::make_unique<std::vector<ReadableLogSegmentPtr>>();
  }
}

LogReader::~LogReader() {
}

Status LogReader::Init(const string& tablet_wal_path) {
  {
    std::lock_guard lock(lock_);
    CHECK_EQ(state_, kLogReaderInitialized) << "bad state for Init(): " << state_;
  }
  VLOG_WITH_PREFIX(1) << "Reading wal from path:" << tablet_wal_path;

  if (!env_->FileExists(tablet_wal_path)) {
    return STATUS(IllegalState, "Cannot find wal location at", tablet_wal_path);
  }

  VLOG_WITH_PREFIX(1) << "Parsing segments from path: " << tablet_wal_path;

  std::vector<string> files_from_log_directory;
  RETURN_NOT_OK_PREPEND(env_->GetChildren(tablet_wal_path, &files_from_log_directory),
                        "Unable to read children from path");

  std::vector<ReadableLogSegmentPtr> read_segments;

  // Build a log segment from log files, ignoring non log files.
  for (const string &potential_log_file : files_from_log_directory) {
    if (!IsLogFileName(potential_log_file)) {
      continue;
    }

    string fqp = JoinPathSegments(tablet_wal_path, potential_log_file);
    auto segment = VERIFY_RESULT_PREPEND(ReadableLogSegment::Open(env_, fqp),
                                         Format("Unable to open readable log segment: $0", fqp));
    if (!segment) {
      LOG_WITH_PREFIX(INFO) << "Log segment w/o header: " << fqp << ", skipping";
      continue;
    }
    CHECK(segment->IsInitialized()) << "Uninitialized segment at: " << segment->path();

    read_segments.push_back(segment);
  }

  // Sort the segments by sequence number.
  std::sort(read_segments.begin(), read_segments.end(), LogSegmentSeqnoComparator());

  {
    std::lock_guard lock(lock_);

    string previous_seg_path;
    int64_t previous_seg_seqno = -1;
    for (const auto& segment : read_segments) {
      // Check that the log segments are in sequence.
      const auto current_seg_seqno = segment->header().sequence_number();
      if (previous_seg_seqno != -1 &&
          current_seg_seqno != previous_seg_seqno + 1) {
        return STATUS(Corruption, Substitute("Segment sequence numbers are not consecutive. "
            "Previous segment: seqno $0, path $1; Current segment: seqno $2, path $3",
            previous_seg_seqno, previous_seg_path, current_seg_seqno,
                segment->path()));
      }
      previous_seg_seqno = current_seg_seqno;
      previous_seg_path = segment->path();

      if (!segment->HasFooter()) {
        const auto no_footer_warning_prefix = Format("Log segment $0 was likely left in-progress"
                                        "after a previous crash.", segment->path());
        if(current_seg_seqno == read_segments.back()->header().sequence_number()) {
          LOG_WITH_PREFIX(WARNING) << no_footer_warning_prefix
                                   << " Will try to reuse this segment as writable active segment";
          RETURN_NOT_OK(AppendUnclosedSegmentUnlocked(segment));
          continue;
        }
        LOG_WITH_PREFIX(WARNING) << no_footer_warning_prefix
                                 << " Will try to rebuild footer by scanning data.";
        RETURN_NOT_OK(segment->RebuildFooterByScanning());
        VLOG_WITH_PREFIX(1) << " Log Reader Indexed: " << segment->footer().ShortDebugString();
      }

      RETURN_NOT_OK(AppendSegmentUnlocked(segment));
    }

    state_ = kLogReaderReading;
  }
  return Status::OK();
}

Status LogReader::InitEmptyReaderForTests() {
  std::lock_guard lock(lock_);
  state_ = kLogReaderReading;
  return Status::OK();
}

bool LogReader::ViolatesMaxTimePolicy(const scoped_refptr<ReadableLogSegment>& segment) const {
  if (FLAGS_log_max_seconds_to_retain <= 0) {
    return false;
  }

  if (!segment->HasFooter()) {
    return false;
  }

  int64_t now = GetCurrentTimeMicros();
  int64_t age_seconds = (now - segment->footer().close_timestamp_micros()) / 1000000;
  if (age_seconds > FLAGS_log_max_seconds_to_retain) {
    YB_LOG_EVERY_N_SECS(WARNING, 300)
        << "Segment " << segment->path() << " violates max retention time policy. "
        << "Segment age: " << age_seconds << " seconds. "
        << "log_max_seconds_to_retain: " << FLAGS_log_max_seconds_to_retain;
    if (PREDICT_FALSE(FLAGS_TEST_record_segments_violate_max_time_policy)) {
      TEST_segments_violate_max_time_policy_->push_back(segment);
    }
    return true;
  }
  return false;
}

bool LogReader::ViolatesMinSpacePolicy(const scoped_refptr<ReadableLogSegment>& segment,
                                       int64_t *potential_reclaimed_space) const {
  if (FLAGS_log_stop_retaining_min_disk_mb <= 0) {
    return false;
  }
  auto free_space_result = env_->GetFreeSpaceBytes(segment->path());
  if (!free_space_result.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 300) << "Unable to get free space: " << free_space_result;
    return false;
  } else {
    uint64_t free_space = *free_space_result;
    if (free_space + *potential_reclaimed_space <
            implicit_cast<size_t>(FLAGS_log_stop_retaining_min_disk_mb) * 1024) {
      YB_LOG_EVERY_N_SECS(WARNING, 300)
          << "Segment " << segment->path() << " violates minimum free space policy "
          << "specified by log_stop_retaining_min_disk_mb: "
          << FLAGS_log_stop_retaining_min_disk_mb;
      *potential_reclaimed_space += segment->file_size();
      if (PREDICT_FALSE(FLAGS_TEST_record_segments_violate_min_space_policy)) {
        TEST_segments_violate_min_space_policy_->push_back(segment);
      }
      return true;
    }
  }
  return false;
}

Status LogReader::GetSegmentPrefixNotIncluding(int64_t index, SegmentSequence* segments) const {
  return GetSegmentPrefixNotIncluding(index, index, segments);
}

Status LogReader::GetSegmentPrefixNotIncluding(int64_t index, int64_t cdc_max_replicated_index,
                                               SegmentSequence* segments) const {
  DCHECK_GE(index, 0);
  DCHECK(segments);
  segments->clear();

  std::lock_guard lock(lock_);
  CHECK_EQ(state_, kLogReaderReading);

  int64_t reclaimed_space = 0;
  for (const ReadableLogSegmentPtr& segment : segments_) {
    // The last segment doesn't have a footer. Never include that one.
    if (!segment->HasFooter()) {
      break;
    }

    // Never garbage collect log segments with unflushed entries.
    if (segment->footer().max_replicate_index() >= index) {
      break;
    }

    // This log segment contains cdc unreplicated entries. Don't GC it unless the file is too old
    // (controlled by flag FLAGS_log_max_seconds_to_retain) or we don't have enough space for the
    // logs (controlled by flag FLAGS_log_stop_retaining_min_disk_mb).
    if (GetAtomicFlag(&FLAGS_enable_log_retention_by_op_idx) &&
        segment->footer().max_replicate_index() >= cdc_max_replicated_index) {
      // Since this log file contains cdc unreplicated entries, we don't want to GC it unless
      // it's too old, or we don't have enough space to store log files.
      if (!ViolatesMaxTimePolicy(segment) && !ViolatesMinSpacePolicy(segment, &reclaimed_space)) {
        // We exit the loop since this log segment already contains cdc unreplicated entries and so
        // do all subsequent files.
        break;
      }
    }

    // TODO: tests for edge cases here with backwards ordered replicates.
    RETURN_NOT_OK(segments->push_back(segment->header().sequence_number(), segment));
  }

  return Status::OK();
}

int64_t LogReader::GetMinReplicateIndex() const {
  std::lock_guard lock(lock_);
  int64_t min_remaining_op_idx = -1;

  for (const scoped_refptr<ReadableLogSegment>& segment : segments_) {
    if (!segment->HasFooter()) continue;
    if (!segment->footer().has_min_replicate_index()) continue;
    if (min_remaining_op_idx == -1 ||
        segment->footer().min_replicate_index() < min_remaining_op_idx) {
      min_remaining_op_idx = segment->footer().min_replicate_index();
    }
  }
  return min_remaining_op_idx;
}

Result<scoped_refptr<ReadableLogSegment>> LogReader::GetSegmentBySequenceNumber(
    const int64_t seq) const {
  std::lock_guard lock(lock_);
  const scoped_refptr<ReadableLogSegment> segment = VERIFY_RESULT(segments_.Get(seq));
  SCHECK_FORMAT(
      segment->header().sequence_number() == seq, InternalError,
      "Expected segment to contain segment number $0 but got $1", seq,
      segment->header().sequence_number());

  return segment;
}

Result<std::shared_ptr<LWLogEntryBatchPB>> LogReader::ReadBatchUsingIndexEntry(
    const LogIndexEntry& index_entry) const {
  const int64_t index = index_entry.op_id.index;

  const auto segment = VERIFY_RESULT_PREPEND(
      GetSegmentBySequenceNumber(index_entry.segment_sequence_number),
      Format("Failed to get segment number for op_index: $0", index));

  CHECK_GT(index_entry.offset_in_segment, 0);
  int64_t offset = index_entry.offset_in_segment;
  ScopedLatencyMetric<EventStats> scoped(read_batch_latency_.get());
  auto result = segment->ReadEntryHeaderAndBatch(&offset);
  RETURN_NOT_OK_PREPEND(
      result,
      Format("Failed to read LogEntry for index $0 from log segment $1 offset $2",
             index, index_entry.segment_sequence_number, index_entry.offset_in_segment));

  if (bytes_read_) {
    bytes_read_->IncrementBy(offset - index_entry.offset_in_segment);
    entries_read_->IncrementBy((**result).entry().size());
  }

  return result;
}

Status LogReader::ReadReplicatesInRange(
    const int64_t starting_at,
    const int64_t up_to,
    int64_t max_bytes_to_read,
    ReplicateMsgs* replicates,
    int64_t* starting_op_segment_seq_num,
    CoarseTimePoint deadline) const {
  DCHECK_GT(starting_at, 0);
  DCHECK_GE(up_to, starting_at);
  DCHECK(log_index_) << "Require an index to random-read logs";
  ReplicateMsgs replicates_tmp;
  LogIndexEntry prev_index_entry;
  prev_index_entry.segment_sequence_number = -1;
  prev_index_entry.offset_in_segment = -1;

  // Remove the deadline if the GetChanges deadline feature is disabled.
  if (!ANNOTATE_UNPROTECTED_READ(FLAGS_get_changes_honor_deadline)) {
    deadline = CoarseTimePoint::max();
  }

  int64_t total_size = 0;
  bool limit_exceeded = false;
  std::shared_ptr<LWLogEntryBatchPB> batch;
  for (int64_t index = starting_at; index <= up_to && !limit_exceeded; index++) {
    // Stop reading if a deadline was specified and the deadline has been exceeded.
    if (deadline != CoarseTimePoint::max() && CoarseMonoClock::Now() >= deadline) {
      break;
    }

    if (PREDICT_FALSE(FLAGS_TEST_get_changes_read_loop_delay_ms > 0)) {
      SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_get_changes_read_loop_delay_ms));
    }

    auto index_entry = VERIFY_RESULT(GetIndexEntry(index));

    if (index == starting_at && starting_op_segment_seq_num != nullptr) {
      *starting_op_segment_seq_num = index_entry.segment_sequence_number;
    }
    // Since a given LogEntryBatch may contain multiple REPLICATE messages,
    // it's likely that this index entry points to the same batch as the previous
    // one. If that's the case, we've already read this REPLICATE and we can
    // skip reading the batch again.
    if (index == starting_at ||
        index_entry.segment_sequence_number != prev_index_entry.segment_sequence_number ||
        index_entry.offset_in_segment != prev_index_entry.offset_in_segment) {
      // Make read operation.
      batch = VERIFY_RESULT(ReadBatchUsingIndexEntry(index_entry));

      // Sanity-check the property that a batch should only have increasing indexes.
      int64_t prev_index = 0;
      for (const auto& entry : batch->entry()) {
        if (!entry.has_replicate()) continue;
        int64_t this_index = entry.replicate().id().index();
        CHECK_GT(this_index, prev_index)
          << "Expected that an entry batch should only include increasing log indexes: "
          << index_entry.ToString()
          << "\nBatch: " << batch->ShortDebugString();
        prev_index = this_index;
      }
    }

    bool found = false;
    for (auto& entry : *batch->mutable_entry()) {
      if (!entry.has_replicate() || entry.replicate().id().index() != index) {
        continue;
      }

      int64_t space_required = entry.replicate().SerializedSize();
      if (replicates_tmp.empty() ||
          max_bytes_to_read <= 0 ||
          total_size + space_required < max_bytes_to_read) {
        total_size += space_required;
        replicates_tmp.emplace_back(batch, entry.mutable_replicate());
      } else {
        limit_exceeded = true;
      }
      found = true;
      break;
    }
    CHECK(found) << "Incorrect index entry didn't yield expected log entry: "
                 << index_entry.ToString();

    prev_index_entry = index_entry;
  }

  replicates->swap(replicates_tmp);
  return Status::OK();
}

Result<yb::OpId> LogReader::LookupOpId(int64_t op_index) const {
  return VERIFY_RESULT(GetIndexEntry(op_index)).op_id;
}

Result<int64_t> LogReader::LookupOpWalSegmentNumber(int64_t op_index) const {
  return VERIFY_RESULT(GetIndexEntry(op_index)).segment_sequence_number;
}

Status LogReader::GetSegmentsSnapshot(SegmentSequence* segments) const {
  std::lock_guard lock(lock_);
  CHECK_EQ(state_, kLogReaderReading);
  segments->assign(segments_);
  return Status::OK();
}

Status LogReader::TrimSegmentsUpToAndIncluding(const int64_t segment_sequence_number) {
  std::lock_guard lock(lock_);
  CHECK_EQ(state_, kLogReaderReading);
  std::vector<int64_t> deleted_segments;

  while (!segments_.empty()) {
    const ReadableLogSegmentPtr& segment = VERIFY_RESULT(segments_.front());
    const auto current_seq_no = segment->header().sequence_number();
    if (current_seq_no > segment_sequence_number) {
      break;
    }
    RETURN_NOT_OK(segments_.pop_front());
    deleted_segments.push_back(current_seq_no);
  }
  LOG_WITH_PREFIX(INFO) << "Removed log segment sequence numbers from log reader: "
                        << yb::ToString(deleted_segments);
  return Status::OK();
}

Status LogReader::UpdateLastSegmentOffset(int64_t readable_to_offset) {
  std::lock_guard lock(lock_);
  CHECK_EQ(state_, kLogReaderReading);
  DCHECK(!segments_.empty());
  // Get the last segment
  const ReadableLogSegmentPtr& segment = VERIFY_RESULT(segments_.back());
  DCHECK(!segment->HasFooter());
  segment->UpdateReadableToOffset(readable_to_offset);
  return Status::OK();
}

Status LogReader::ReplaceLastSegment(const scoped_refptr<ReadableLogSegment>& segment) {
  // This is used to replace the last segment once we close it properly so it must
  // have a footer.
  DCHECK(segment->HasFooter());

  std::lock_guard lock(lock_);
  CHECK_EQ(state_, kLogReaderReading);

  CHECK(!segments_.empty());
  RETURN_NOT_OK(segments_.pop_back());
  return segments_.push_back(segment->header().sequence_number(), segment);
}

Status LogReader::AppendSegment(const scoped_refptr<ReadableLogSegment>& segment) {
  DCHECK(segment->IsInitialized());
  if (PREDICT_FALSE(!segment->HasFooter())) {
    RETURN_NOT_OK(segment->RebuildFooterByScanning());
  }
  std::lock_guard lock(lock_);
  return AppendSegmentUnlocked(segment);
}

Status LogReader::AppendSegmentUnlocked(const scoped_refptr<ReadableLogSegment>& segment) {
  DCHECK(segment->IsInitialized());
  DCHECK(segment->HasFooter());

  return segments_.push_back(segment->header().sequence_number(), segment);
}

Status LogReader::AppendUnclosedSegmentUnlocked(const scoped_refptr<ReadableLogSegment>& segment) {
  DCHECK(segment->IsInitialized());
  SCHECK(!segment->HasFooter(), IllegalState, "unclosed segment shouldn't have a footer");

  return segments_.push_back(segment->header().sequence_number(), segment);
}

Status LogReader::AppendEmptySegment(const scoped_refptr<ReadableLogSegment>& segment) {
  DCHECK(segment->IsInitialized());
  std::lock_guard lock(lock_);
  CHECK_EQ(state_, kLogReaderReading);
  return segments_.push_back(segment->header().sequence_number(), segment);
}

size_t LogReader::num_segments() const {
  std::lock_guard lock(lock_);
  return segments_.size();
}

string LogReader::ToString() const {
  std::lock_guard lock(lock_);
  string ret = "Reader's SegmentSequence: \n";
  for (const SegmentSequence::value_type& entry : segments_) {
    ret.append(Substitute("Segment: $0 Footer: $1\n",
                          entry->header().sequence_number(),
                          !entry->HasFooter() ? "NONE" : entry->footer().ShortDebugString()));
  }
  return ret;
}

namespace {

// Logs message like:
// $log_prefix Requested op_index ... that is not in log index cache, loading index from WAL
// segment(s) ...
// or
// $log_prefix Requested op_index ... that is not in log index cache, no more WAL segments to
// load index from, min_indexed_segment_number: ...
void LogLoadingFromSegmentsMessage(
    const std::string& log_prefix, const SegmentSequence& segments, const int64_t op_index,
    const int64_t min_indexed_segment_number) {
  const auto msg_prefix = Format("Requested op_index $0 that is not in log index cache", op_index);

  if (segments.empty()) {
    VLOG(5) << log_prefix << Format(
        "$0, no more WAL segments to load index from, min_indexed_segment_number: $1", msg_prefix,
        min_indexed_segment_number);
    return;
  }

  const auto segment_to_str = [](const ReadableLogSegmentPtr& segment) {
    return Format(
        "#$0 (footer: $1)", segment->header().sequence_number(),
        segment->HasFooter() ? segment->footer().ShortDebugString() : "---");
  };

  // We've already checked that segments is not empty, so failure to get back/front might mean
  // memory corruption.
  const auto segments_range_str =
      segments.size() == 1
          ? Format("WAL segment $0", segment_to_str(CHECK_RESULT(segments.back())))
          : Format(
                "WAL segments $0 .. $1", segment_to_str(CHECK_RESULT(segments.back())),
                segment_to_str(CHECK_RESULT(segments.front())));

  LOG(INFO) << log_prefix << Format("$0, loading index from $1", msg_prefix, segments_range_str);
}

} // namespace

Result<LogIndexEntry> LogReader::GetIndexEntry(
    const int64_t op_index, ReadableLogSegment* segment) const {
  const auto index_entry = VERIFY_RESULT(DoGetIndexEntry(op_index, segment));
  RSTATUS_DCHECK(
      !segment || index_entry.segment_sequence_number == segment->header().sequence_number(),
      IllegalState,
      Format("The requested op $0 is expected in segment $1, actual segment: $2",
             op_index, segment->header().sequence_number(), index_entry.segment_sequence_number));
  return index_entry;
}

Result<LogIndexEntry> LogReader::DoGetIndexEntry(
    const int64_t op_index, ReadableLogSegment* segment) const {
  LogIndexEntry index_entry;

  if (!log_index_) {
    return STATUS(NotFound, "log_index_ is not initialized");
  }

  auto s = log_index_->GetEntry(op_index, &index_entry);
  if (PREDICT_TRUE(s.ok())) {
    return index_entry;
  }
  if (!s.IsIncomplete()) {
    // This is an actual failure rather than log index is not yet loaded into LogIndex cache.
    return s;
  }

  if (!segment) {
    VLOG_WITH_PREFIX(2) << "Trying to lazy load log index from WAL segments for op index "
                        << op_index;
    // Prevent concurrent lazy loading of the index.
    std::lock_guard lock(load_index_mutex_);

    SegmentSequence segments_to_load_index;
    // Detect segments to cover op_index for lazy loading index from them.
    {
      std::lock_guard lock(lock_);

      const auto min_indexed_segment_number = log_index_->GetMinIndexedSegmentNumber();
      SegmentSequence::const_reverse_iterator it;
      if (min_indexed_segment_number == LogIndex::kNoIndexForFullWalSegment) {
        // Load index starting from the latest available segment.
        it = segments_.rbegin();
      } else {
        const auto result = segments_.iter_at(min_indexed_segment_number);
        if (result.ok()) {
          // Load index starting from the next segment (in reverse order).
          // Note that std::make_reverse_iterator return reverse iterator to the element before
          // element at `it`.
          it = std::make_reverse_iterator(*result);
        } else if (result.status().IsNotFound()) {
          // Already GCed, nothing to load.
          it = segments_.rend();
        } else {
          // Unexpected error - return to the caller to handle.
          return result.status();
        }
      }

      for (; it != segments_.rend(); ++it) {
        const ReadableLogSegmentPtr segment = *it;
        const auto seg_num = segment->header().sequence_number();

        // Stop loading at the segment in which all operations have Raft indexes lower than
        // op_index.
        if (segment->HasFooter() && segment->footer().has_max_replicate_index() &&
            segment->footer().max_replicate_index() < op_index) {
          VLOG_WITH_PREFIX(2) << "Will stop loading at segment "
                              << seg_num << ", max_replicate_index: "
                              << segment->footer().max_replicate_index();
          break;
        }
        RETURN_NOT_OK(segments_to_load_index.push_front(seg_num, segment));
      }

      LogLoadingFromSegmentsMessage(
          LogPrefix(), segments_to_load_index, op_index, min_indexed_segment_number);
    }

    // Load index starting from latest WAL segment.
    for (auto it = segments_to_load_index.rbegin(); it != segments_to_load_index.rend(); ++it) {
      const auto stop_loading = !VERIFY_RESULT(log_index_->LazyLoadOneSegment(it->get()));
      if (stop_loading) {
        // Concurrent log GC happened and removed at least beginning of just loaded segment, no
        // need to continue loading.
        break;
      }
    }
  } else {
    const auto seq_no = segment->header().sequence_number();
    VLOG_WITH_PREFIX(2) << "Trying to load log index from segment " << seq_no
                        << " for op index " << op_index;
    // Prevent concurrent loading of the index.
    std::lock_guard lock(load_index_mutex_);
    int64_t first_op_index ATTRIBUTE_UNUSED = 0;
    RETURN_NOT_OK(log_index_->LoadFromSegment(segment, &first_op_index));
  }

  s = log_index_->GetEntry(op_index, &index_entry);
  if (s.IsIncomplete()) {
    s = s.CloneAndReplaceCode(Status::kNotFound);
  }
  RETURN_NOT_OK(s);
  return index_entry;
}

Result<LogIndexEntry> LogReader::TEST_GetIndexEntry(const int64_t index) const {
  return GetIndexEntry(index);
}

}  // namespace log
}  // namespace yb
