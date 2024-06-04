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

#include "yb/consensus/log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <shared_mutex>

#include <boost/algorithm/string/predicate.hpp>

#include "yb/ash/wait_state.h"

#include "yb/common/opid.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"

#include "yb/consensus/consensus_util.h"
#include "yb/consensus/log.messages.h"
#include "yb/consensus/log_index.h"
#include "yb/consensus/log_metrics.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/walltime.h"

#include "yb/util/async_util.h"
#include "yb/util/callsite_profiling.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/crc.h"
#include "yb/util/debug-util.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/env_util.h"
#include "yb/util/fault_injection.h"
#include "yb/util/file_util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/operation_counter.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/random.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"
#include "yb/util/taskstream.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/unique_lock.h"

using namespace yb::size_literals;  // NOLINT.
using namespace std::literals;  // NOLINT.
using namespace std::placeholders;

// Log retention configuration.
// -----------------------------
DEFINE_RUNTIME_int32(log_min_segments_to_retain, 2,
    "The minimum number of past log segments to keep at all times,"
    " regardless of what is required for durability. "
    "Must be at least 1.");
TAG_FLAG(log_min_segments_to_retain, advanced);

DEFINE_RUNTIME_int32(log_min_seconds_to_retain, 900,
    "The minimum number of seconds for which to keep log segments to keep at all times, "
    "regardless of what is required for durability. Logs may be still retained for "
    "a longer amount of time if they are necessary for correct restart. This should be "
    "set long enough such that a tablet server which has temporarily failed can be "
    "restarted within the given time period. If a server is down for longer than this "
    "amount of time, it is possible that its tablets will be re-replicated on other "
    "machines.");
TAG_FLAG(log_min_seconds_to_retain, advanced);

// Flag to enable background log sync. When enabled, we DON'T wait for performing fsync until
// either
// 1. unsynced data reaches bytes_durable_wal_write_mb_ threshold OR
// 2. time since the oldest unsynced log entry has exceeded interval_durable_wal_write_ms
// Instead, fsync tasks are pushed to the log-sync queue using background_sync_threadpool_token_
// on either
// 1. reaching an unsynced data threshold of
//    (bytes_durable_wal_write_mb_ * FLAGS_log_background_sync_data_fraction) mb OR
// 2. when time passed since the oldest unsynced log entry has exceeded
//    (interval_durable_wal_write_ms * FLAGS_log_background_sync_interval_fraction) ms.
// This is only true when durable_wal_write_ is false. If true, fsync in performed in-line on
// every call to Log::Sync()
DEFINE_UNKNOWN_bool(log_enable_background_sync, true,
            "If true, log fsync operations in the aggresively performed in the background.");
DEFINE_UNKNOWN_double(log_background_sync_data_fraction, 0.5,
             "When log_enable_background_sync is enabled and periodic_sync_unsynced_bytes_ "
             "reaches bytes_durable_wal_write_mb_*log_background_sync_data_fraction, the fsync "
             "task is pushed to the log-sync queue.");
DEFINE_UNKNOWN_double(log_background_sync_interval_fraction, 0.6,
             "When log_enable_background_sync is enabled and time passed since insertion of log "
             "entry exceeds interval_durable_wal_write_ms*log_background_sync_interval_fraction "
             "the fsync task is pushed to the log-sync queue.");

// Flags for controlling kernel watchdog limits.
DEFINE_RUNTIME_int32(consensus_log_scoped_watch_delay_callback_threshold_ms, 1000,
    "If calling consensus log callback(s) take longer than this, the kernel watchdog "
    "will print out a stack trace.");
TAG_FLAG(consensus_log_scoped_watch_delay_callback_threshold_ms, advanced);
DEFINE_RUNTIME_int32(consensus_log_scoped_watch_delay_append_threshold_ms, 1000,
    "If consensus log append takes longer than this, the kernel watchdog "
    "will print out a stack trace.");
TAG_FLAG(consensus_log_scoped_watch_delay_append_threshold_ms, advanced);

// Fault/latency injection flags.
// -----------------------------
DEFINE_UNKNOWN_bool(log_inject_latency, false,
            "If true, injects artificial latency in log sync operations. "
            "Advanced option. Use at your own risk -- has a negative effect "
            "on performance for obvious reasons!");
DEFINE_UNKNOWN_int32(log_inject_latency_ms_mean, 100,
             "The number of milliseconds of latency to inject, on average. "
             "Only takes effect if --log_inject_latency is true");
DEFINE_UNKNOWN_int32(log_inject_latency_ms_stddev, 100,
             "The standard deviation of latency to inject in before log sync operations. "
             "Only takes effect if --log_inject_latency is true");
TAG_FLAG(log_inject_latency, unsafe);
TAG_FLAG(log_inject_latency_ms_mean, unsafe);
TAG_FLAG(log_inject_latency_ms_stddev, unsafe);

DEPRECATE_FLAG(int32, log_inject_append_latency_ms_max, "02_2024");

DEFINE_test_flag(bool, log_consider_all_ops_safe, false,
            "If true, we consider all operations to be safe and will not wait"
            "for the opId to apply to the local log. i.e. WaitForSafeOpIdToApply "
            "becomes a noop.");

DEFINE_test_flag(bool, simulate_abrupt_server_restart, false,
                 "If true, don't properly close the log segment.");

DEFINE_test_flag(bool, pause_before_wal_sync, false, "Pause before doing work in Log::Sync.");

DEFINE_test_flag(bool, set_pause_before_wal_sync, false,
                 "Set pause_before_wal_sync to true in Log::Sync.");
DEFINE_test_flag(bool, disable_wal_retention_time, false,
                 "If true, disables time-based wal retention.");

// TaskStream flags.
// We have to make the queue length really long.
// TODO: Create new flags log_taskstream_queue_max_size and log_taskstream_queue_max_wait_ms
// and deprecate these flags.
DEFINE_UNKNOWN_int32(taskstream_queue_max_size, 100000,
             "Maximum number of operations waiting in the taskstream queue.");

DEFINE_UNKNOWN_int32(taskstream_queue_max_wait_ms, 1000,
             "Maximum time in ms to wait for items in the taskstream queue to arrive.");

DEFINE_UNKNOWN_int32(wait_for_safe_op_id_to_apply_default_timeout_ms, 15000 * yb::kTimeMultiplier,
             "Timeout used by WaitForSafeOpIdToApply when it was not specified by caller.");

DEFINE_test_flag(int64, log_fault_after_segment_allocation_min_replicate_index, 0,
                 "Fault of segment allocation when min replicate index is at least specified. "
                 "0 to disable.");

DEFINE_test_flag(bool, crash_before_wal_header_is_written, false,
                 "Crash the server before WAL header is written");

DEFINE_UNKNOWN_int64(time_based_wal_gc_clock_delta_usec, 0,
             "A delta in microseconds to add to the clock value used to determine if a WAL "
             "segment is safe to be garbage collected. This is needed for clusters running with a "
             "skewed hybrid clock, because the clock used for time-based WAL GC is the wall clock, "
             "not hybrid clock.");

DEFINE_RUNTIME_int64(reuse_unclosed_segment_threshold_bytes, INT64_MAX,
            "If the last left in-progress segment size is smaller or equal to this threshold, "
            "Log will reuse this last segment as writable active_segment at tablet bootstrap. "
            "Otherwise, Log will create a new segment.");

DEFINE_RUNTIME_int32(min_segment_size_bytes_to_rollover_at_flush, 0,
                    "Only rotate wals at least of this size (in bytes) at tablet flush."
                    "-1 to disable WAL rollover at flush. 0 to always rollover WAL at flush.");

// Validate that log_min_segments_to_retain >= 1
static bool ValidateLogsToRetain(const char* flagname, int value) {
  if (value >= 1) {
    return true;
  }
  LOG(ERROR) << strings::Substitute("$0 must be at least 1, value $1 is invalid",
                                    flagname, value);
  return false;
}
DEFINE_validator(log_min_segments_to_retain, &ValidateLogsToRetain);

static std::string kSegmentPlaceholderFilePrefix = ".tmp.newsegment";
static std::string kSegmentPlaceholderFileTemplate = kSegmentPlaceholderFilePrefix + "XXXXXX";

namespace yb {
namespace log {

using env_util::OpenFileForRandom;
using std::shared_ptr;
using std::unique_ptr;
using std::string;
using strings::Substitute;

namespace {

bool IsRolloverMarkerType(LogEntryTypePB type) {
  return type == LogEntryTypePB::SYNC_ROLLOVER_MARKER ||
         type == LogEntryTypePB::ASYNC_ROLLOVER_AT_FLUSH_MARKER;
}

bool IsMarkerType(LogEntryTypePB type) {
  return IsRolloverMarkerType(type) ||
         type == LogEntryTypePB::FLUSH_MARKER;
}

} // namespace

// This class represents a batch of operations to be written and synced to the log. It is opaque to
// the user and is managed by the Log class.
class Log::LogEntryBatch {
 public:
  LogEntryBatch(LogEntryTypePB type, std::shared_ptr<LWLogEntryBatchPB> entry_batch_pb);
  ~LogEntryBatch();

  std::string ToString() const {
    return Format("{ type: $0 state: $1 max_op_id: $2 }", type_, state_, MaxReplicateOpId());
  }

  bool HasReplicateEntries() const {
    return type_ == LogEntryTypePB::REPLICATE && count() > 0;
  }

 private:
  friend class Log;
  friend class MultiThreadedLogTest;

  // Serializes contents of the entry to an internal buffer.
  Status Serialize();

  // Sets the callback that will be invoked after the entry is
  // appended and synced to disk
  void set_callback(const StatusCallback& cb) {
    callback_ = cb;
  }

  // Returns the callback that will be invoked after the entry is
  // appended and synced to disk.
  const StatusCallback& callback() {
    return callback_;
  }

  bool failed_to_append() const {
    return state_ == kEntryFailedToAppend;
  }

  void set_failed_to_append() {
    state_ = kEntryFailedToAppend;
  }

  // Mark the entry as reserved, but not yet ready to write to the log.
  void MarkReserved();

  // Mark the entry as ready to write to log.
  void MarkReady();

  // Returns a Slice representing the serialized contents of the entry.
  Slice data() const {
    DCHECK_EQ(state_, kEntrySerialized);
    return Slice(buffer_);
  }

  bool IsMarker() const;

  bool IsSingleEntryOfType(LogEntryTypePB type) const;

  bool IsRolloverMarker() const;

  size_t count() const { return count_; }

  // Returns the total size in bytes of the object.
  size_t total_size_bytes() const {
    return total_size_bytes_;
  }

  // The highest OpId of a REPLICATE message in this batch.
  OpId MaxReplicateOpId() const {
    DCHECK_EQ(REPLICATE, type_);
    if (entry_batch_pb_->entry().empty()) {
      return OpId::Invalid();
    }
    return OpId::FromPB(entry_batch_pb_->entry().back().replicate().id());
  }

  void SetReplicates(const ReplicateMsgs& replicates) {
    replicates_ = replicates;
  }

  // The type of entries in this batch.
  const LogEntryTypePB type_;

  // Contents of the log entries that will be written to disk.
  std::shared_ptr<LWLogEntryBatchPB> entry_batch_pb_;

  // Total size in bytes of all entries
  size_t total_size_bytes_ = 0;

  // Number of entries in 'entry_batch_pb_'
  const size_t count_;

  // The vector of refcounted replicates.  This makes sure there's at least a reference to each
  // replicate message until we're finished appending.
  ReplicateMsgs replicates_;

  // Callback to be invoked upon the entries being written and synced to disk.
  StatusCallback callback_;

  // Buffer to which 'phys_entries_' are serialized by call to 'Serialize()'
  faststring buffer_;

  // Offset into the log file for this entry batch.
  int64_t offset_;

  // Segment sequence number for this entry batch.
  int64_t active_segment_sequence_number_;

  enum LogEntryState {
    kEntryInitialized,
    kEntryReserved,
    kEntryReady,
    kEntrySerialized,
    kEntryFailedToAppend
  };
  LogEntryState state_ = kEntryInitialized;

  DISALLOW_COPY_AND_ASSIGN(LogEntryBatch);
};

// This class is responsible for managing the task that appends to the log file.
// This task runs in a common thread pool with append tasks from other tablets.
// A token is used to ensure that only one append task per tablet is executed concurrently.
class Log::Appender {
 public:
  explicit Appender(Log* log, ThreadPool* append_thread_pool);

  // Initializes the objects and starts the task.
  Status Init();

  Status Submit(LogEntryBatch* item) {
    ScopedRWOperation operation(&task_stream_counter_);
    RETURN_NOT_OK(operation);
    if (!task_stream_) {
      return STATUS(IllegalState, "Appender stopped");
    }
    return task_stream_->Submit(item);
  }

  Status TEST_SubmitFunc(const std::function<void()>& func) {
    return task_stream_->TEST_SubmitFunc(func);
  }

  // Waits until the last enqueued elements are processed, sets the appender_ to closing
  // state. If any entries are added to the queue during the process, invoke their callbacks'
  // 'OnFailure()' method.
  void Shutdown();

  const std::string& LogPrefix() const {
    return log_->LogPrefix();
  }

  std::string GetRunThreadStack() const {
    return task_stream_->GetRunThreadStack();
  }

  std::string ToString() const {
    return task_stream_->ToString();
  }

  const yb::ash::WaitStateInfoPtr& wait_state() const {
    return wait_state_;
  }

 private:
  // Process the given log entry batch or does a sync if a null is passed.
  void ProcessBatch(LogEntryBatch* entry_batch);
  void GroupWork();

  Log* const log_;

  // Lock to protect access to thread_ during shutdown.
  RWOperationCounter task_stream_counter_;
  unique_ptr<TaskStream<LogEntryBatch>> task_stream_;

  // vector of entry batches in group, to execute callbacks after call to Sync.
  std::vector<std::unique_ptr<LogEntryBatch>> sync_batch_;

  // Time at which current group was started
  MonoTime time_started_;

  const yb::ash::WaitStateInfoPtr wait_state_;
};

Log::Appender::Appender(Log* log, ThreadPool* append_thread_pool)
    : log_(log),
      task_stream_counter_(Format("Appender for $0", log->tablet_id())),
      task_stream_(new TaskStream<LogEntryBatch>(
          std::bind(&Log::Appender::ProcessBatch, this, _1), append_thread_pool,
          FLAGS_taskstream_queue_max_size,
          MonoDelta::FromMilliseconds(FLAGS_taskstream_queue_max_wait_ms))),
      wait_state_(ash::WaitStateInfo::CreateIfAshIsEnabled<ash::WaitStateInfo>()) {
  if (wait_state_) {
    wait_state_->set_root_request_id(yb::Uuid::Generate());
    wait_state_->set_query_id(yb::to_underlying(yb::ash::FixedQueryId::kQueryIdForLogAppender));
    wait_state_->UpdateAuxInfo({.tablet_id = log_->tablet_id(), .method = "RaftWAL"});
    SET_WAIT_STATUS_TO(wait_state_, Idle);
    yb::ash::RaftLogAppenderWaitStatesTracker().Track(wait_state_);
  }
  DCHECK(log_min_segments_to_retain_validator_registered);
}

Status Log::Appender::Init() {
  VLOG_WITH_PREFIX(1) << "Starting log task stream";
  return Status::OK();
}

// Note on the order of operations here.
// periodic_sync_needed_ and  periodic_sync_unsynced_bytes_ need to be set ONLY AFTER call to
// log_->DoAppend(), which in turn calls active_segment_->WriteEntryBatch(). Since DoSync fn
// operates on these variables in parallel (when FLAGS_log_enable_background_sync is set), it
// is necessary for us to set these counters after call to DoAppend(). That way, we ensure that
// fsync will eventually be called. [if there is a background thread executing DoSync in parallel,
// it might OR might not flush the new dirty data in the current iteration due to race condition]
void Log::Appender::ProcessBatch(LogEntryBatch* entry_batch) {
  // A callback function to TaskStream is expected to process the accumulated batch of entries.
  if (entry_batch == nullptr) {
    // Here, we do sync and call callbacks.
    GroupWork();
    return;
  }

  if (sync_batch_.empty()) { // Start of batch.
    // Used in tests to delay writing log entries.
    auto sleep_duration = log_->sleep_duration_.load(std::memory_order_acquire);
    if (sleep_duration.count() > 0) {
      std::this_thread::sleep_for(sleep_duration);
    }
    time_started_ = MonoTime::Now();
  }
  TRACE_EVENT_FLOW_END0("log", "Batch", entry_batch);
  Status s = log_->DoAppend(entry_batch);

  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(DFATAL) << "Error appending to the log: " << s;
    entry_batch->set_failed_to_append();
    // TODO If a single operation fails to append, should we abort all subsequent operations
    // in this batch or allow them to be appended? What about operations in future batches?
    if (!entry_batch->callback().is_null()) {
      entry_batch->callback().Run(s);
    }
    return;
  }
  if (!log_->sync_disabled_) {
    bool expected = false;
    if (log_->periodic_sync_needed_.compare_exchange_strong(expected, true,
                                                            std::memory_order_acq_rel)) {
      log_->periodic_sync_earliest_unsync_entry_time_ = MonoTime::Now();
    }
    log_->periodic_sync_unsynced_bytes_ += entry_batch->total_size_bytes();
  }
  sync_batch_.emplace_back(entry_batch);
}

void Log::Appender::GroupWork() {
  if (sync_batch_.empty()) {
    Status s = log_->Sync();
    return;
  }
  if (log_->metrics_) {
    log_->metrics_->entry_batches_per_group->Increment(sync_batch_.size());
  }
  TRACE_EVENT1("log", "batch", "batch_size", sync_batch_.size());

  auto se = ScopeExit([this] {
    if (log_->metrics_) {
      MonoTime time_now = MonoTime::Now();
      log_->metrics_->group_commit_latency->Increment(
          time_now.GetDeltaSince(time_started_).ToMicroseconds());
    }
    sync_batch_.clear();
  });

  Status s = log_->Sync();
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(DFATAL) << "Error syncing log: " << s;
    for (std::unique_ptr<LogEntryBatch>& entry_batch : sync_batch_) {
      if (!entry_batch->callback().is_null()) {
        entry_batch->callback().Run(s);
      }
    }
  } else {
    TRACE_EVENT0("log", "Callbacks");
    VLOG_WITH_PREFIX(2) << "Synchronized " << sync_batch_.size() << " entry batches";
    LongOperationTracker long_operation_tracker(
        "Log callback", FLAGS_consensus_log_scoped_watch_delay_callback_threshold_ms * 1ms);
    for (std::unique_ptr<LogEntryBatch>& entry_batch : sync_batch_) {
      if (PREDICT_TRUE(!entry_batch->failed_to_append() && !entry_batch->callback().is_null())) {
        entry_batch->callback().Run(Status::OK());
      }
      // It's important to delete each batch as we see it, because deleting it may free up memory
      // from memory trackers, and the callback of a later batch may want to use that memory.
      entry_batch.reset();
    }
    sync_batch_.clear();
  }
  VLOG_WITH_PREFIX(1) << "Exiting AppendTask for tablet " << log_->tablet_id();
}

void Log::Appender::Shutdown() {
  ScopedRWOperationPause pause(&task_stream_counter_, CoarseMonoClock::now() + 15s, Stop::kTrue);
  if (!pause.ok()) {
    LOG(DFATAL) << "Failed to stop appender";
    return;
  }
  if (task_stream_) {
    VLOG_WITH_PREFIX(1) << "Shutting down log task stream";
    task_stream_->Stop();
    VLOG_WITH_PREFIX(1) << "Log append task stream is shut down";
    task_stream_.reset();
  }
  if (wait_state_) {
    yb::ash::RaftLogAppenderWaitStatesTracker().Untrack(wait_state_);
  }
}

// This task is submitted to allocation_pool_ in order to asynchronously pre-allocate new log
// segments.
void Log::SegmentAllocationTask() {
  allocation_status_.Set(PreAllocateNewSegment());
}

const Status Log::kLogShutdownStatus(
    STATUS(ServiceUnavailable, "WAL is shutting down", "", Errno(ESHUTDOWN)));

Status Log::Open(const LogOptions &options,
                 const std::string& tablet_id,
                 const std::string& wal_dir,
                 const std::string& peer_uuid,
                 const Schema& schema,
                 uint32_t schema_version,
                 const scoped_refptr<MetricEntity>& table_metric_entity,
                 const scoped_refptr<MetricEntity>& tablet_metric_entity,
                 ThreadPool* append_thread_pool,
                 ThreadPool* allocation_thread_pool,
                 ThreadPool* background_sync_threadpool,
                 int64_t cdc_min_replicated_index,
                 scoped_refptr<Log>* log,
                 const PreLogRolloverCallback& pre_log_rollover_callback,
                 NewSegmentAllocationCallback callback,
                 CreateNewSegment create_new_segment) {
  RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(options.env, DirName(wal_dir)),
                        Substitute("Failed to create table wal dir $0", DirName(wal_dir)));

  RETURN_NOT_OK_PREPEND(env_util::CreateDirIfMissing(options.env, wal_dir),
                        Substitute("Failed to create tablet wal dir $0", wal_dir));

  scoped_refptr<Log> new_log(new Log(options,
                                     wal_dir,
                                     tablet_id,
                                     peer_uuid,
                                     schema,
                                     schema_version,
                                     table_metric_entity,
                                     tablet_metric_entity,
                                     append_thread_pool,
                                     allocation_thread_pool,
                                     background_sync_threadpool,
                                     callback,
                                     pre_log_rollover_callback,
                                     create_new_segment));
  RETURN_NOT_OK(new_log->Init());
  log->swap(new_log);
  return Status::OK();
}

Log::Log(
    LogOptions options,
    string wal_dir,
    string tablet_id,
    string peer_uuid,
    const Schema& schema,
    uint32_t schema_version,
    const scoped_refptr<MetricEntity>& table_metric_entity,
    const scoped_refptr<MetricEntity>& tablet_metric_entity,
    ThreadPool* append_thread_pool,
    ThreadPool* allocation_thread_pool,
    ThreadPool* background_sync_threadpool,
    NewSegmentAllocationCallback callback,
    const PreLogRolloverCallback& pre_log_rollover_callback,
    CreateNewSegment create_new_segment)
    : options_(std::move(options)),
      wal_dir_(std::move(wal_dir)),
      tablet_id_(std::move(tablet_id)),
      peer_uuid_(std::move(peer_uuid)),
      schema_(std::make_unique<Schema>(schema)),
      schema_version_(schema_version),
      active_segment_sequence_number_(options_.initial_active_segment_sequence_number),
      log_state_(kLogInitialized),
      max_segment_size_(options_.segment_size_bytes),
      // We halve the initial log segment size here because we double it for every new segment,
      // including the very first segment.
      cur_max_segment_size_((options_.initial_segment_size_bytes + 1) / 2),
      appender_(new Appender(this, append_thread_pool)),
      allocation_token_(allocation_thread_pool->NewToken(ThreadPool::ExecutionMode::SERIAL)),
      background_sync_threadpool_token_(
          background_sync_threadpool->NewToken(ThreadPool::ExecutionMode::SERIAL)),
      durable_wal_write_(options_.durable_wal_write),
      interval_durable_wal_write_(options_.interval_durable_wal_write),
      bytes_durable_wal_write_mb_(options_.bytes_durable_wal_write_mb),
      sync_disabled_(false),
      allocation_state_(SegmentAllocationState::kAllocationNotStarted),
      table_metric_entity_(table_metric_entity),
      tablet_metric_entity_(tablet_metric_entity),
      on_disk_size_(0),
      log_prefix_(consensus::MakeTabletLogPrefix(tablet_id_, peer_uuid_)),
      create_new_segment_at_start_(create_new_segment),
      new_segment_allocation_callback_(callback),
      pre_log_rollover_callback_(pre_log_rollover_callback) {
  set_wal_retention_secs(options_.retention_secs);
  if (table_metric_entity_ && tablet_metric_entity_) {
    metrics_.reset(new LogMetrics(table_metric_entity_, tablet_metric_entity_));
  }
}

Status Log::Init() {
  std::lock_guard write_lock(state_lock_);
  CHECK_EQ(kLogInitialized, log_state_);
  // Init the index
  log_index_ = VERIFY_RESULT(LogIndex::NewLogIndex(wal_dir_));
  // Reader for previous segments.
  RETURN_NOT_OK(LogReader::Open(get_env(),
                                log_index_,
                                log_prefix_,
                                wal_dir_,
                                table_metric_entity_.get(),
                                tablet_metric_entity_.get(),
                                &reader_));

  // The case where we are continuing an existing log.  We must pick up where the previous WAL left
  // off in terms of sequence numbers.
  if (reader_->num_segments() != 0) {
    VLOG_WITH_PREFIX(1) << "Using existing " << reader_->num_segments()
                        << " segments from path: " << wal_dir_;

    SegmentSequence segments;
    RETURN_NOT_OK(reader_->GetSegmentsSnapshot(&segments));
    const ReadableLogSegmentPtr& active_segment = VERIFY_RESULT(segments.back());
    active_segment_sequence_number_ = active_segment->header().sequence_number();
    LOG_WITH_PREFIX(INFO) << "Opened existing logs. Last segment is " << active_segment->path();

    // In case where TServer process reboots, we need to reload the wal file size into the metric,
    // otherwise we do nothing
    if (metrics_ && metrics_->wal_size->value() == 0) {
      std::for_each(segments.begin(), segments.end(),
                    [this](const auto& segment) {
                    this->metrics_->wal_size->IncrementBy(segment->file_size());});
    }

  }

  if (durable_wal_write_) {
    YB_LOG_FIRST_N(INFO, 1) << "durable_wal_write is turned on.";
  } else if (interval_durable_wal_write_) {
    YB_LOG_FIRST_N(INFO, 1) << "interval_durable_wal_write_ms is turned on to sync every "
                            << interval_durable_wal_write_.ToMilliseconds() << " ms.";
  } else if (bytes_durable_wal_write_mb_ > 0) {
    YB_LOG_FIRST_N(INFO, 1) << "bytes_durable_wal_write_mb is turned on to sync every "
     << bytes_durable_wal_write_mb_ << " MB of data.";
  } else {
    YB_LOG_FIRST_N(INFO, 1) << "durable_wal_write is turned off. Buffered IO will be used for WAL.";
  }

  if (create_new_segment_at_start_) {
    RETURN_NOT_OK(EnsureSegmentInitializedUnlocked());
  }
  return Status::OK();
}

Status Log::AsyncAllocateSegment() {
  SCHECK_EQ(
      allocation_state_.load(std::memory_order_acquire),
      SegmentAllocationState::kAllocationNotStarted, AlreadyPresent, "Allocation already running");
  allocation_status_.Reset();
  allocation_state_.store(SegmentAllocationState::kAllocationInProgress, std::memory_order_release);
  VLOG_WITH_PREFIX(1) << "Active segment: " << active_segment_sequence_number_
                      << ". Starting new segment allocation.";
  return allocation_token_->SubmitClosure(Bind(&Log::SegmentAllocationTask, Unretained(this)));
}

Status Log::CloseCurrentSegment() {
  if (!footer_builder_.has_min_replicate_index()) {
    VLOG_WITH_PREFIX(1) << "Writing a segment without any REPLICATE message. Segment: "
                        << active_segment_->path();
  }
  VLOG_WITH_PREFIX(2) << "Segment footer for " << active_segment_->path()
                      << ": " << footer_builder_.ShortDebugString();

  auto close_timestamp_micros = GetCurrentTimeMicros();

  if (FLAGS_time_based_wal_gc_clock_delta_usec != 0) {
    auto unadjusted_close_timestamp_micros = close_timestamp_micros;
    close_timestamp_micros += FLAGS_time_based_wal_gc_clock_delta_usec;
    LOG_WITH_PREFIX(INFO)
        << "Adjusting log segment closing timestamp by "
        << FLAGS_time_based_wal_gc_clock_delta_usec << " usec from "
        << unadjusted_close_timestamp_micros << " usec to " << close_timestamp_micros << " usec";
  }

  footer_builder_.set_close_timestamp_micros(close_timestamp_micros);
  Status status;
  {
    std::lock_guard lock(active_segment_mutex_);
    status = active_segment_->WriteIndexWithFooterAndClose(log_index_.get(),
                                                           &footer_builder_);
  }

  if (status.ok() && metrics_) {
      metrics_->wal_size->IncrementBy(active_segment_->Size());
  }
  return status;
}

Status Log::RollOver() {
  if (pre_log_rollover_callback_) {
    pre_log_rollover_callback_();
  }

  LOG_SLOW_EXECUTION(WARNING, 50, LogPrefix() + "Log roll took a long time") {
    SCOPED_LATENCY_METRIC(metrics_, roll_latency);
    RSTATUS_DCHECK(active_segment_, InternalError, "Called RollOver without active segment.");

    // Check if any errors have occurred during allocation
    RETURN_NOT_OK(allocation_status_.Get());

    DCHECK_EQ(allocation_state(), SegmentAllocationState::kAllocationFinished);

    LOG_WITH_PREFIX(INFO) << Format(
        "Last appended OpId in segment $0: $1", active_segment_->path(),
        last_appended_entry_op_id_.ToString());

    RETURN_NOT_OK(DoSync());
    RETURN_NOT_OK(CloseCurrentSegment());

    RETURN_NOT_OK(SwitchToAllocatedSegment());

    LOG_WITH_PREFIX(INFO) << "Rolled over to a new segment: " << active_segment_->path();
  }
  return Status::OK();
}

Result<std::unique_ptr<Log::LogEntryBatch>> Log::Reserve(
    LogEntryTypePB type, std::shared_ptr<LWLogEntryBatchPB> entry_batch) {
  TRACE_EVENT0("log", "Log::Reserve");
  {
    PerCpuRwSharedLock read_lock(state_lock_);
    if (log_state_ != kLogWriting) {
      return STATUS_FORMAT(IllegalState, "Invalid log state $0, expected $1",
          log_state_, kLogWriting);
    }
  }

  // In DEBUG builds, verify that all of the entries in the batch match the specified type.  In
  // non-debug builds the foreach loop gets optimized out.
#ifndef NDEBUG
  for (const auto& entry : entry_batch->entry()) {
    DCHECK_EQ(entry.type(), type) << "Bad batch: " << entry_batch->ShortDebugString();
  }
#endif

  auto new_entry_batch = std::make_unique<LogEntryBatch>(type, std::move(entry_batch));
  new_entry_batch->MarkReserved();

  // Release the memory back to the caller: this will be freed when
  // the entry is removed from the queue.
  //
  // TODO (perf) Use a ring buffer instead of a blocking queue and set
  // 'reserved_entry' to a pre-allocated slot in the buffer.
  return new_entry_batch;
}

Status Log::TEST_ReserveAndAppend(
    std::shared_ptr<LWLogEntryBatchPB> batch, const ReplicateMsgs& replicates,
    const StatusCallback& callback) {
  auto entry = VERIFY_RESULT(Reserve(REPLICATE, std::move(batch)));
  entry->SetReplicates(replicates);
  return AsyncAppend(std::move(entry), callback);
}

Status Log::AsyncAppend(
    std::unique_ptr<LogEntryBatch> entry_batch, const StatusCallback& callback) {
  {
    PerCpuRwSharedLock read_lock(state_lock_);
    if (log_state_ != kLogWriting) {
      return STATUS_FORMAT(IllegalState, "Invalid log state $0, expected $1",
          log_state_, kLogWriting);
    }
  }

  entry_batch->set_callback(callback);
  entry_batch->MarkReady();

  if (entry_batch->HasReplicateEntries()) {
    last_submitted_op_id_ = entry_batch->MaxReplicateOpId();
  }

  auto submit_status = appender_->Submit(entry_batch.get());
  if (PREDICT_FALSE(!submit_status.ok())) {
    LOG_WITH_PREFIX(WARNING)
        << "Failed to submit batch " << entry_batch->MaxReplicateOpId() << ": " << submit_status;
    return kLogShutdownStatus;
  }

  entry_batch.release();

  return Status::OK();
}

Status Log::AsyncAppendReplicates(const ReplicateMsgs& msgs, const yb::OpId& committed_op_id,
                                  RestartSafeCoarseTimePoint batch_mono_time,
                                  const StatusCallback& callback) {
  auto batch = CreateBatchFromAllocatedOperations(msgs);
  if (!committed_op_id.empty()) {
    committed_op_id.ToPB(batch->mutable_committed_op_id());
  }
  // Set batch mono time if it was specified.
  if (batch_mono_time != RestartSafeCoarseTimePoint()) {
    batch->set_mono_time(batch_mono_time.ToUInt64());
  }

  auto reserved_entry_batch = VERIFY_RESULT(Reserve(LogEntryTypePB::REPLICATE, std::move(batch)));

  // If we're able to reserve, set the vector of replicate shared pointers in the LogEntryBatch.
  // This will make sure there's a reference for each replicate while we're appending.
  reserved_entry_batch->SetReplicates(msgs);

  return AsyncAppend(std::move(reserved_entry_batch), callback);
}

Status Log::DoAppend(LogEntryBatch* entry_batch, SkipWalWrite skip_wal_write) {
  ADOPT_WAIT_STATE(appender_->wait_state());
  SCOPED_WAIT_STATUS(WAL_Append);
  if (!skip_wal_write) {
    RETURN_NOT_OK(entry_batch->Serialize());
    Slice entry_batch_data = entry_batch->data();
    LOG_IF(DFATAL, entry_batch_data.size() <= 0 && !entry_batch->IsMarker())
        << "Cannot call DoAppend() with no data";

    if (entry_batch->IsRolloverMarker()) {
      VLOG_WITH_PREFIX_AND_FUNC(1) << "Got marker " << LogEntryTypePB_Name(entry_batch->type_);
      const auto has_entries =
          active_segment_ && footer_builder_.IsInitialized() && footer_builder_.num_entries() > 0;
      if (!has_entries) {
        // Do nothing.
      } else if (entry_batch->IsSingleEntryOfType(SYNC_ROLLOVER_MARKER)) {
        if (allocation_state() == SegmentAllocationState::kAllocationNotStarted) {
          RETURN_NOT_OK(AsyncAllocateSegment());
        }
        return RollOver();
      } else if (entry_batch->IsSingleEntryOfType(ASYNC_ROLLOVER_AT_FLUSH_MARKER)) {
        if (allocation_state() == SegmentAllocationState::kAllocationNotStarted) {
          const auto min_size_to_rollover =
              GetAtomicFlag(&FLAGS_min_segment_size_bytes_to_rollover_at_flush);
          if (min_size_to_rollover < 0 || active_segment_->Size() < min_size_to_rollover) {
            VLOG_WITH_PREFIX(1) << Format("Skipping async wal rotation at flush. "
                                          "segment_size: $0 min_size_to_rollover: $1",
                                          active_segment_->Size(), min_size_to_rollover);
            return Status::OK();
          }
          next_max_segment_size_ = std::max<uint64_t>(
              options_.initial_segment_size_bytes, cur_max_segment_size_ / 2);
          RETURN_NOT_OK(AsyncAllocateSegment());
        }
        if (!options_.async_preallocate_segments) {
          return RollOver();
        }
        return Status::OK();
      }
    }

    auto entry_batch_bytes = entry_batch->total_size_bytes();
    // If there is no data to write return OK.
    if (PREDICT_FALSE(entry_batch_bytes == 0)) {
      return Status::OK();
    }

    // If the size of this entry overflows the current segment, get a new one.
    if (allocation_state() == SegmentAllocationState::kAllocationNotStarted) {
      if (active_segment_->Size() + entry_batch_bytes + kEntryHeaderSize > cur_max_segment_size_) {
        LOG_WITH_PREFIX(INFO) << "Max segment size " << cur_max_segment_size_ << " reached. "
                              << "Starting new segment allocation.";
        RETURN_NOT_OK(AsyncAllocateSegment());
        if (!options_.async_preallocate_segments) {
          RETURN_NOT_OK(RollOver());
        }
      }
    } else if (allocation_state() == SegmentAllocationState::kAllocationFinished) {
      RETURN_NOT_OK(RollOver());
    } else {
      VLOG_WITH_PREFIX(1) << "Segment allocation already in progress...";
    }

    int64_t start_offset = active_segment_->written_offset();

    LOG_SLOW_EXECUTION(WARNING, 50, "Append to log took a long time") {
      SCOPED_LATENCY_METRIC(metrics_, append_latency);
      LongOperationTracker long_operation_tracker(
          "Log append", FLAGS_consensus_log_scoped_watch_delay_append_threshold_ms * 1ms);

      RETURN_NOT_OK(active_segment_->WriteEntryBatch(entry_batch_data));
    }

    if (metrics_) {
      metrics_->bytes_logged->IncrementBy(active_segment_->written_offset() - start_offset);
    }

    // Populate the offset and sequence number for the entry batch if we did a WAL write.
    entry_batch->offset_ = start_offset;
    entry_batch->active_segment_sequence_number_ = active_segment_sequence_number_;
  }

  // We keep track of the last-written OpId here. This is needed to initialize Consensus on
  // startup.
  if (entry_batch->HasReplicateEntries()) {
    last_appended_entry_op_id_ = entry_batch->MaxReplicateOpId();
  }

  CHECK_OK(UpdateIndexForBatch(*entry_batch));
  UpdateFooterForBatch(entry_batch);

  return Status::OK();
}

Status Log::UpdateIndexForBatch(const LogEntryBatch& batch) {
  if (batch.type_ != REPLICATE) {
    return Status::OK();
  }

  for (const auto& entry_pb : batch.entry_batch_pb_->entry()) {
    RETURN_NOT_OK(log_index_->AddEntry(LogIndexEntry {
      .op_id = OpId::FromPB(entry_pb.replicate().id()),
      .segment_sequence_number = batch.active_segment_sequence_number_,
      .offset_in_segment = batch.offset_,
    }));
  }
  return Status::OK();
}

void Log::UpdateFooterForBatch(LogEntryBatch* batch) {
  footer_builder_.set_num_entries(footer_builder_.num_entries() + batch->count());

  // We keep track of the last-written OpId here.  This is needed to initialize Consensus on
  // startup.  We also retrieve the OpId of the first operation in the batch so that, if we roll
  // over to a new segment, we set the first operation in the footer immediately.
  // Update the index bounds for the current segment.
  for (const auto& entry_pb : batch->entry_batch_pb_->entry()) {
    UpdateSegmentFooterIndexes(entry_pb.replicate(), &footer_builder_);
  }
  if (footer_builder_.has_min_replicate_index()) {
    min_replicate_index_.store(footer_builder_.min_replicate_index(), std::memory_order_release);
  }
}

Status Log::AllocateSegmentAndRollOver() {
  VLOG_WITH_PREFIX_AND_FUNC(1) << "Start";
  auto reserved_entry_batch = VERIFY_RESULT(ReserveMarker(SYNC_ROLLOVER_MARKER));
  Synchronizer s;
  RETURN_NOT_OK(AsyncAppend(std::move(reserved_entry_batch), s.AsStatusCallback()));
  return s.Wait();
}

Status Log::AsyncAllocateSegmentAndRollover() {
  VLOG_WITH_PREFIX_AND_FUNC(1) << "Start";
  auto reserved_entry_batch = VERIFY_RESULT(ReserveMarker(ASYNC_ROLLOVER_AT_FLUSH_MARKER));
  return AsyncAppend(std::move(reserved_entry_batch), {});
}

Result<bool> Log::ReuseAsActiveSegment(const scoped_refptr<ReadableLogSegment>& recover_segment) {
  auto read_entries = recover_segment->ReadEntries();
  RETURN_NOT_OK(read_entries.status);
  int64_t file_size = read_entries.end_offset;
  if (file_size > FLAGS_reuse_unclosed_segment_threshold_bytes) {
    VLOG_WITH_PREFIX(2)
        << "Cannot reuse last WAL segment " << recover_segment->path()
        << " as active_segment due to its actual file size " << file_size
        << " is greater than reuse threshold " << FLAGS_reuse_unclosed_segment_threshold_bytes;
    RETURN_NOT_OK(recover_segment->RebuildFooterByScanning(read_entries));
    return false;
  }
  next_segment_path_ = recover_segment->path();
  auto opts = GetNewSegmentWritableFileOptions();
  opts.mode = Env::OPEN_EXISTING;
  opts.initial_offset = file_size;
  // There are two reasons of why we want to set the initial offset:
  // 1. Overwrite corrupted entry, because last entry in the segment is possible to be corrupted
  //    under the case that server crashed in the middle of writing entry.
  // 2. Before server crash, file might get preallocated to certain size.
  //    This set intial offset option ensure we start with offset at last valid entry,
  //    instead of at the end of preallocated block.
  auto status = env_util::OpenFileForWrite(opts, get_env(), next_segment_path_,
                                           &next_segment_file_);
  if (!status.ok()) {
      VLOG_WITH_PREFIX(2)
          << "Cannot reuse last WAL segment " << next_segment_path_
          << " as active_segment due to file could not be reopened: " << status.ToString();
      RETURN_NOT_OK(recover_segment->RebuildFooterByScanning(read_entries));
      return false;
  }

  uint64_t real_size = recover_segment->file_size();
  if (options_.preallocate_segments) {
    // File was likely to be pre-allocated. In this case, real_size is the preallocation size.
    cur_max_segment_size_ = real_size;
  } else {
    cur_max_segment_size_ = options_.initial_segment_size_bytes;
    while (cur_max_segment_size_ <= real_size) {
      cur_max_segment_size_ *= 2;
    }
    cur_max_segment_size_ = std::min(cur_max_segment_size_, max_segment_size_);
  }
  // Restore footer_builder_ and log index for this segment first.
  footer_builder_.Clear();
  RETURN_NOT_OK(recover_segment->RestoreFooterBuilderAndLogIndex(&footer_builder_,
                                                                 log_index_.get(),
                                                                 read_entries));
  if (footer_builder_.has_min_replicate_index()) {
    min_replicate_index_.store(footer_builder_.min_replicate_index(),
                               std::memory_order_release);
  }
  std::unique_ptr<WritableLogSegment> new_segment(
      new WritableLogSegment(next_segment_path_, next_segment_file_));

  active_segment_sequence_number_ = recover_segment->header().sequence_number();
  RETURN_NOT_OK(new_segment->ReuseHeader(recover_segment->header(),
                                         recover_segment->first_entry_offset()));

  {
    std::lock_guard lock(active_segment_mutex_);
    active_segment_ = std::move(new_segment);
  }
  LOG(INFO) << "Successfully restored footer_builder_ and log_index_ for segment: "
            << recover_segment->path() << ". Reopen the file for write with starting offset: "
            << file_size;
  return true;
}

Status Log::EnsureSegmentInitialized() {
  std::lock_guard write_lock(state_lock_);
  return EnsureSegmentInitializedUnlocked();
}

Status Log::EnsureSegmentInitializedUnlocked() {
  if (log_state_ == LogState::kLogWriting) {
    // New segment already created.
    return Status::OK();
  }
  if (log_state_ != LogState::kLogInitialized) {
    return STATUS_FORMAT(
        IllegalState, "Unexpected log state in EnsureSegmentInitialized(): $0", log_state_);
  }

  bool reuse_last_segment = false;
  // For last segment that doesn't have a footer, if its file size (last readable offset)
  // is within the reuse_unclosed_segment_threshold_bytes, we will reuse it as active_segment_.
  // Otherwise, close this segment by building a footer in memory.
  SegmentSequence segments;
  RETURN_NOT_OK(reader_->GetSegmentsSnapshot(&segments));
  if (!segments.empty()) {
    const scoped_refptr<ReadableLogSegment>& last_segment = VERIFY_RESULT(segments.back());
    if (!last_segment->HasFooter()) {
      // Reuse this segment as writable active_segment.
      reuse_last_segment = VERIFY_RESULT(ReuseAsActiveSegment(last_segment));
    }
  }
  // Allocate new segment as writable active_segment.
  if (!reuse_last_segment) {
    RETURN_NOT_OK(AsyncAllocateSegment());
    RETURN_NOT_OK(allocation_status_.Get());
    RETURN_NOT_OK(SwitchToAllocatedSegment());
  }
  log_index_->SetMinIndexedSegmentNumber(active_segment_sequence_number_);

  RETURN_NOT_OK(appender_->Init());
  log_state_ = LogState::kLogWriting;
  return Status::OK();
}

// DoSync is called either called from the from the background log-sync threadpool maintained at
// tserver [from ::DoSyncAndResetTaskInQueue] or in-line with the critical path from ::Sync().
// Refer the fn definition in the header file for details on when the function could be called.
// It operates on the following variables that are also operated on in the critical write path.
// 1. periodic_sync_unsynced_bytes_
// 2. periodic_sync_needed_
// 3. active_segment_
//
// The active_segment_mutex_ ensures that there is at most one fsync execution at a given time.
// If we don't do this, the later fsync could return immediately while the first fsync could still
// be synchronizing data to disk. active_segment_mutex_ also prevents the segment from being
// rolloved over to the next one during fsync execution.
//
// Note on the order of operations here.
// There is NO mutual exclusion to prevent append operations occuring concurrently. The sequence
// of operations always ensures that periodic_sync_needed_/periodic_sync_unsynced_bytes_ take the
// upper bound, and ensure that we don't end up skipping fsync op when it could have been actually
// required due to the time/data thresholds.
// Hence the order of operations in this function should always be as follows
// - reset periodic_sync_needed_ followed by periodic_sync_unsynced_bytes_
// - call active_segment_->Sync()
// - set periodic_sync_needed_ followed by periodic_sync_unsynced_bytes_ if sync fails
//
// ::DoSync should not be called directly, instead we should call ::Sync and that might execute
// this function in-line or as a task in the background or could just return because an fsync
// might not be necessary. We only call ::DoSync directly before we call ::CloseCurrentSegment
Status Log::DoSync() {
  // Acquire the lock over active_segment_ to prevent segment rollover in the interim.
  ADOPT_WAIT_STATE(appender_->wait_state());
  SCOPED_WAIT_STATUS(WAL_Sync);
  std::lock_guard lock(active_segment_mutex_);
  if (active_segment_->IsClosed()) {
    return Status::OK();
  }

  Status status;
  periodic_sync_needed_.store(0, std::memory_order_release);
  periodic_sync_unsynced_bytes_.store(0, std::memory_order_release);
  LOG_SLOW_EXECUTION_EVERY_N_SECS(INFO, /* log at most one slow execution every 1 sec */ 1,
                                  50, "Fsync log took a long time") {
    SCOPED_LATENCY_METRIC(metrics_, sync_latency);
    status = active_segment_->Sync();
  }

  return status;
}

// Important to note that there is at most one task queued/running ::DoSyncAndResetTaskInQueue
// at any given time.
//
// TODO: DoSyncAndResetTaskInQueue could perform one additional/unnecessary fsync operation when
// segment rollover happens before the background task calls ::DoSync. In that case, the call to
// ::DoSync operates on the new segment. We could use active_segment_sequence_number_ to determine
// if we are operating on current/new segment and skip calling fsync whenever unnecessary. We will
// have to use active_segment_sequence_number_ instead of fsync_task_in_queue_, in ::Sync(), to
// determine if there is a pending fsync task corresponding to the current active segment.
void Log::DoSyncAndResetTaskInQueue() {
  auto status = DoSync();
  if (!status.ok()) {
    // ensure that fsync gets called on the subsequent call to Log::Sync() function
    periodic_sync_needed_.store(true);
    periodic_sync_unsynced_bytes_.store(bytes_durable_wal_write_mb_ * 1_MB,
                                        std::memory_order_release);
    LOG_WITH_PREFIX(WARNING) << "Log fsync failed with status " << status;
  }
  fsync_task_in_queue_.store(false, std::memory_order_release);
}

// retuns
// - kForceFsync when
//   1. periodic_sync_needed_ is false, i.e no new appends happened since the last fsync.
// - kAsyncFsync when
//   1. time interval/unsynced data exceeds lower limits and FLAGS_log_enable_background_sync is set
//      time lower limit: (interval_durable_wal_write_ * FLAGS_log_background_sync_interval_fraction
//      data lower limit: bytes_durable_wal_write_mb_ * FLAGS_log_background_sync_data_fraction (MB)
// - kForceFsync when
//   1. durable_wal_write_ is set to true
//   2. time interval/unsynced data exceeds upper limits
//      time upper limit: interval_durable_wal_write_
//      data upper limit: bytes_durable_wal_write_mb_ (MB)
SyncType Log::FindSyncType() {
  if (durable_wal_write_) {
    return SyncType::kForceFsync;
  }

  if (!periodic_sync_needed_.load()) {
    return SyncType::kNoSync;
  }

  SyncType sync_type = SyncType::kNoSync;
  if (interval_durable_wal_write_) {
    MonoDelta interval_async_wal_write_ =
        MonoDelta::FromMilliseconds(interval_durable_wal_write_.ToMilliseconds() *
                                    FLAGS_log_background_sync_interval_fraction);
    auto time_now = MonoTime::Now();
    auto time_async_threshold =
        periodic_sync_earliest_unsync_entry_time_ + interval_async_wal_write_;
    if (time_now > time_async_threshold) {
      auto time_immediate_threshold =
          periodic_sync_earliest_unsync_entry_time_ + interval_durable_wal_write_;
      sync_type = time_now > time_immediate_threshold ? SyncType::kForceFsync :
                                                        SyncType::kAsyncFsync;
    }
  }

  if (sync_type != SyncType::kForceFsync && bytes_durable_wal_write_mb_ > 0) {
    auto data_async_threshold =
        bytes_durable_wal_write_mb_ * 1_MB * FLAGS_log_background_sync_data_fraction;
    auto unsynced_bytes = periodic_sync_unsynced_bytes_.load(std::memory_order_acquire);
    if (unsynced_bytes >= data_async_threshold) {
      auto data_immediate_threshold = bytes_durable_wal_write_mb_ * 1_MB;
      sync_type = unsynced_bytes >= data_immediate_threshold ? SyncType::kForceFsync :
                                                                SyncType::kAsyncFsync;
    }
  }

  if (sync_type == SyncType::kAsyncFsync && !FLAGS_log_enable_background_sync) {
    sync_type = SyncType::kNoSync;
  }

  return sync_type;
}

// Finds type of sync that needs to be done and either spawns a task to execute
// ::DoSyncAndResetTaskInQueue() or calls ::DoSync() in-line depending on the below conditions.
// Also ensures that there is at most one queued/running ::DoSyncAndResetTaskInQueue task submitted
// using background_sync_threadpool_token_ at any given time.
//
// if sync_type == kAsyncFsync:
//    returns if there is an existing task queued/running ::DoSyncAndResetTaskInQueue
//    else pushes a task onto the queue and returns. if it is not able to submit the task,
//    will fall back to kForceFsync mode.
// if sync_type == kForceFsync:
//    calls ::DoSync in-line and returns the status.
//
Status Log::Sync() {
  TRACE_EVENT0("log", "Sync");

  TEST_PAUSE_IF_FLAG(TEST_pause_before_wal_sync);
  if (PREDICT_FALSE(FLAGS_TEST_set_pause_before_wal_sync)) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_wal_sync) = true;
  }

  if (sync_disabled_) {
    return UpdateSegmentReadableOffset();
  }

  if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_log_inject_latency))) {
    Random r(static_cast<uint32_t>(GetCurrentTimeMicros()));
    int sleep_ms = r.Normal(GetAtomicFlag(&FLAGS_log_inject_latency_ms_mean),
                            GetAtomicFlag(&FLAGS_log_inject_latency_ms_stddev));
    if (sleep_ms > 0) {
      LOG_WITH_PREFIX(INFO) << "Injecting " << sleep_ms << "ms of latency in Log::Sync()";
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
    }
  }

  SyncType sync_type = FindSyncType();
  switch (sync_type) {
    case SyncType::kNoSync: {
      break;
    }
    case SyncType::kAsyncFsync: {
      // return if a sync task already exists in the queue.
      if (fsync_task_in_queue_.load(std::memory_order_acquire)) {
        break;
      }
      fsync_task_in_queue_.store(true, std::memory_order_release);
      auto status = background_sync_threadpool_token_->SubmitFunc(
          std::bind(&Log::DoSyncAndResetTaskInQueue, this));
      if (!status.ok()) {
        LOG_WITH_PREFIX(WARNING) << "Pushing sync operation to log-sync queue failed with "
                                 << "status " << status;
        fsync_task_in_queue_.store(false, std::memory_order_release);
      }
      break;
    }
    case SyncType::kForceFsync: {
      RETURN_NOT_OK(DoSync());
      break;
    }
  }

  return UpdateSegmentReadableOffset();
}

Status Log::UpdateSegmentReadableOffset() {
  // Update the reader on how far it can read the active segment.
  RETURN_NOT_OK(reader_->UpdateLastSegmentOffset(active_segment_->written_offset()));
  {
    std::lock_guard write_lock(last_synced_entry_op_id_mutex_);
    last_synced_entry_op_id_.store(last_appended_entry_op_id_, boost::memory_order_release);
    YB_PROFILE(last_synced_entry_op_id_cond_.notify_all());
  }
  return Status::OK();
}

Status Log::GetSegmentsToGCUnlocked(int64_t min_op_idx, SegmentSequence* segments_to_gc) const {
  // For the lifetime of a Log::CopyTo call, log_copy_min_index_ may be set to something
  // other than std::numeric_limits<int64_t>::max(). This value will correspond to the
  // minimum op_idx which is currently being copied and must be retained. In order to
  // avoid concurrently deleting those ops, we bump min_op_idx here to be at-least as
  // low as log_copy_min_index_.
  min_op_idx = std::min(log_copy_min_index_, min_op_idx);
  // Find the prefix of segments in the segment sequence that is guaranteed not to include
  // 'min_op_idx'.
  RETURN_NOT_OK(reader_->GetSegmentPrefixNotIncluding(
      min_op_idx, cdc_min_replicated_index_.load(std::memory_order_acquire), segments_to_gc));

  const auto max_to_delete =
      std::max<ssize_t>(reader_->num_segments() - FLAGS_log_min_segments_to_retain, 0);
  ssize_t segments_to_gc_size = segments_to_gc->size();
  if (segments_to_gc_size > max_to_delete) {
    VLOG_WITH_PREFIX(2)
        << "GCing " << segments_to_gc_size << " in " << wal_dir_
        << " would not leave enough remaining segments to satisfy minimum "
        << "retention requirement. Only considering "
        << max_to_delete << "/" << reader_->num_segments();
    segments_to_gc->truncate(max_to_delete);
  } else if (segments_to_gc_size < max_to_delete) {
    auto extra_segments = max_to_delete - segments_to_gc_size;
    VLOG_WITH_PREFIX(2) << "Too many log segments, need to GC " << extra_segments << " more.";
  }

  if PREDICT_TRUE(!FLAGS_TEST_disable_wal_retention_time) {
    ApplyTimeRetentionPolicy(segments_to_gc);
  }

  return Status::OK();
}

Status Log::GetSegmentsToGC(int64_t min_op_idx, SegmentSequence* segments_to_gc) const {
  PerCpuRwSharedLock read_lock(state_lock_);
  return GetSegmentsToGCUnlocked(min_op_idx, segments_to_gc);
}

void Log::ApplyTimeRetentionPolicy(SegmentSequence* segments_to_gc) const {
  // Don't GC segments that are newer than the configured time-based retention.
  int64_t now = GetCurrentTimeMicros() + FLAGS_time_based_wal_gc_clock_delta_usec;

  for (auto iter = segments_to_gc->begin(); iter != segments_to_gc->end(); ++iter) {
    const auto& segment = *iter;
    // Segments here will always have a footer, since we don't return the in-progress segment up
    // above. However, segments written by older YB builds may not have the timestamp info (TODO:
    // make sure we indeed care about these old builds). In that case, we're allowed to GC them.
    if (!segment->footer().has_close_timestamp_micros()) continue;

    int64_t age_seconds = (now - segment->footer().close_timestamp_micros()) / 1000000;
    if (age_seconds < wal_retention_secs()) {
      VLOG_WITH_PREFIX(2)
          << "Segment " << segment->path() << " is only " << age_seconds << "s old: "
          << "cannot GC it yet due to configured time-based retention policy.";
      // Truncate the list of segments to GC here -- if this one is too new, then all later ones are
      // also too new.
      segments_to_gc->truncate(iter);
      break;
    }
  }
}

Status Log::Append(
     const std::shared_ptr<LWLogEntryPB>& phys_entry, LogEntryMetadata entry_metadata,
     SkipWalWrite skip_wal_write) {
  auto& entry_batch_pb = *phys_entry->arena().NewObject<LWLogEntryBatchPB>(&phys_entry->arena());
  if (entry_metadata.entry_time != RestartSafeCoarseTimePoint()) {
    entry_batch_pb.set_mono_time(entry_metadata.entry_time.ToUInt64());
  }

  entry_batch_pb.mutable_entry()->push_back_ref(phys_entry.get());
  LogEntryBatch entry_batch(phys_entry->type(), {phys_entry, &entry_batch_pb});
  // Mark this as reserved, as we're building it from preallocated data.
  entry_batch.state_ = LogEntryBatch::kEntryReserved;
  // Ready assumes the data is reserved before it is ready.
  entry_batch.MarkReady();
  if (skip_wal_write) {
    // Get the LogIndex entry from read path metadata.
    entry_batch.offset_ = entry_metadata.offset;
    entry_batch.active_segment_sequence_number_ = entry_metadata.active_segment_sequence_number;
  }
  Status s = DoAppend(&entry_batch, skip_wal_write);
  if (s.ok() && !skip_wal_write) {
    // Only sync if we actually performed a wal write.
    s = Sync();
  }
  return s;
}

Result<std::unique_ptr<Log::LogEntryBatch>> Log::ReserveMarker(LogEntryTypePB type) {
  auto entry_batch = rpc::MakeSharedMessage<LWLogEntryBatchPB>();
  entry_batch->add_entry()->set_type(type);
  return Reserve(type, std::move(entry_batch));
}

Status Log::WaitUntilAllFlushed() {
  // In order to make sure we empty the queue we need to use the async API.
  auto reserved_entry_batch = VERIFY_RESULT(ReserveMarker(FLUSH_MARKER));
  Synchronizer s;
  RETURN_NOT_OK(AsyncAppend(std::move(reserved_entry_batch), s.AsStatusCallback()));
  return s.Wait();
}

void Log::set_wal_retention_secs(uint32_t wal_retention_secs) {
  LOG_WITH_PREFIX(INFO) << "Setting log wal retention time to " << wal_retention_secs << " seconds";
  wal_retention_secs_.store(wal_retention_secs, std::memory_order_release);
}

uint32_t Log::wal_retention_secs() const {
  uint32_t wal_retention_secs = wal_retention_secs_.load(std::memory_order_acquire);
  auto flag_wal_retention = ANNOTATE_UNPROTECTED_READ(FLAGS_log_min_seconds_to_retain);
  return flag_wal_retention > 0 ?
      std::max(wal_retention_secs, static_cast<uint32_t>(flag_wal_retention)) :
      wal_retention_secs;
}

yb::OpId Log::GetLatestEntryOpId() const {
  return last_synced_entry_op_id_.load(boost::memory_order_acquire);
}

int64_t Log::GetMinReplicateIndex() const {
  return min_replicate_index_.load(std::memory_order_acquire);
}

yb::OpId Log::WaitForSafeOpIdToApply(const yb::OpId& min_allowed, MonoDelta duration) {
  if (FLAGS_TEST_log_consider_all_ops_safe || all_op_ids_safe_) {
    return min_allowed;
  }

  auto result = last_synced_entry_op_id_.load(boost::memory_order_acquire);

  if (result < min_allowed) {
    auto start = CoarseMonoClock::Now();
    std::unique_lock<std::mutex> lock(last_synced_entry_op_id_mutex_);
    auto wait_time = duration ? duration.ToSteadyDuration()
                              : FLAGS_wait_for_safe_op_id_to_apply_default_timeout_ms * 1ms;
    for (;;) {
      if (last_synced_entry_op_id_cond_.wait_for(
              lock, wait_time, [this, min_allowed, &result] {
            result = last_synced_entry_op_id_.load(boost::memory_order_acquire);
            return result >= min_allowed;
      })) {
        break;
      }
      if (duration) {
        return yb::OpId();
      }
      // TODO(bogdan): If the log is closed at this point, consider refactoring to return status
      // and fail cleanly.
      LOG_WITH_PREFIX(ERROR) << "Appender stack: " << appender_->GetRunThreadStack();
      LOG_WITH_PREFIX(DFATAL)
          << "Long wait for safe op id: " << min_allowed
          << ", current: " << GetLatestEntryOpId()
          << ", last appended: " << last_appended_entry_op_id_
          << ", last submitted: " << last_submitted_op_id_
          << ", appender: " << appender_->ToString()
          << ", passed: " << MonoDelta(CoarseMonoClock::Now() - start);
    }
  }

  DCHECK_GE(result.term, min_allowed.term)
      << "result: " << result << ", min_allowed: " << min_allowed;
  return result;
}

Status Log::GC(int64_t min_op_idx, int32_t* num_gced) {
  CHECK_GE(min_op_idx, 0);

  LOG_WITH_PREFIX(INFO) << "Running Log GC on " << wal_dir_ << ": retaining ops >= " << min_op_idx
                        << ", log segment size = " << options_.segment_size_bytes;
  VLOG_TIMING(1, "Log GC") {
    SegmentSequence segments_to_delete;

    {
      std::lock_guard l(state_lock_);
      CHECK_EQ(kLogWriting, log_state_);

      RETURN_NOT_OK(GetSegmentsToGCUnlocked(min_op_idx, &segments_to_delete));

      if (segments_to_delete.empty()) {
        VLOG_WITH_PREFIX(1) << "No segments to delete.";
        *num_gced = 0;
        return Status::OK();
      }
      // Trim the prefix of segments from the reader so that they are no longer referenced by the
      // log.
      const ReadableLogSegmentPtr& last_to_delete = VERIFY_RESULT(segments_to_delete.back());
      RETURN_NOT_OK(
          reader_->TrimSegmentsUpToAndIncluding(last_to_delete->header().sequence_number()));
    }

    // Now that they are no longer referenced by the Log, delete the files.
    *num_gced = 0;
    for (const scoped_refptr<ReadableLogSegment>& segment : segments_to_delete) {
      LOG_WITH_PREFIX(INFO) << "Deleting log segment in path: " << segment->path()
                            << " (GCed ops < " << segment->footer().max_replicate_index() + 1
                            << ")";
      RETURN_NOT_OK(get_env()->DeleteFile(segment->path()));
      (*num_gced)++;

      if (metrics_) {
        metrics_->wal_size->IncrementBy(-1 * segment->file_size());
      }
    }

    // Determine the minimum remaining replicate index in order to properly GC the index chunks.
    int64_t min_remaining_op_idx = reader_->GetMinReplicateIndex();
    if (min_remaining_op_idx > 0) {
      log_index_->GC(min_remaining_op_idx);
    }
  }

  return Status::OK();
}

Status Log::GetGCableDataSize(int64_t min_op_idx, int64_t* total_size) const {
  if (min_op_idx < 0) {
    return STATUS_FORMAT(InvalidArgument, "Invalid min op index $0", min_op_idx);
  }

  SegmentSequence segments_to_delete;
  *total_size = 0;
  {
    PerCpuRwSharedLock read_lock(state_lock_);
    if (log_state_ != kLogWriting) {
      return STATUS_FORMAT(IllegalState, "Invalid log state $0, expected $1",
          log_state_, kLogWriting);
    }
    Status s = GetSegmentsToGCUnlocked(min_op_idx, &segments_to_delete);

    if (!s.ok() || segments_to_delete.empty()) {
      return Status::OK();
    }
  }
  for (const scoped_refptr<ReadableLogSegment>& segment : segments_to_delete) {
    *total_size += segment->file_size();
  }
  return Status::OK();
}

LogReader* Log::GetLogReader() const {
  return reader_.get();
}

Status Log::GetSegmentsSnapshot(SegmentSequence* segments) const {
  PerCpuRwSharedLock read_lock(state_lock_);
  if (!reader_) {
    return STATUS(IllegalState, "Log already closed");
  }

  return reader_->GetSegmentsSnapshot(segments);
}

uint64_t Log::OnDiskSize() {
  SegmentSequence segments;
  {
    PerCpuRwSharedLock l(state_lock_);
    // If the log is closed, the tablet is either being deleted or tombstoned,
    // so we don't count the size of its log anymore as it should be deleted.
    if (log_state_ == kLogClosed || !reader_->GetSegmentsSnapshot(&segments).ok()) {
      return on_disk_size_.load();
    }
  }
  uint64_t ret = 0;
  for (const auto& segment : segments) {
    ret += segment->file_size();
  }

  on_disk_size_.store(ret, std::memory_order_release);
  return ret;
}

void Log::SetSchemaForNextLogSegment(const Schema& schema,
                                     uint32_t version) {
  std::lock_guard l(schema_lock_);
  *schema_ = schema;
  schema_version_ = version;
}

Status Log::TEST_WriteCorruptedEntryBatchAndSync() {
  return active_segment_->TEST_WriteCorruptedEntryBatchAndSync();
}

Status Log::Close() {
  // Allocation pool is used from appender pool, so we should shutdown appender first.
  appender_->Shutdown();
  allocation_token_.reset();

  if (PREDICT_FALSE(FLAGS_TEST_simulate_abrupt_server_restart)) {
    return Status::OK();
  }
  std::lock_guard l(state_lock_);
  switch (log_state_) {
    case kLogWriting:
      // Appender uses background_sync_threadpool_token_, so we should reset it
      // post shutting down appender_.
      background_sync_threadpool_token_.reset();
      // Now that we have shut background_sync_threadpool_token_, don't call ::Sync.
      // call ::DoSync instead.
      RETURN_NOT_OK(DoSync());
      RETURN_NOT_OK(CloseCurrentSegment());
      RETURN_NOT_OK(ReplaceSegmentInReaderUnlocked());
      log_state_ = kLogClosed;
      VLOG_WITH_PREFIX(1) << "Log closed";

      // Release FDs held by these objects.
      log_index_.reset();
      reader_.reset();

      return Status::OK();

    case kLogClosed:
      VLOG_WITH_PREFIX(1) << "Log already closed";
      return Status::OK();

    default:
      return STATUS(IllegalState, Substitute("Bad state for Close() $0", log_state_));
  }
}

size_t Log::num_segments() const {
  PerCpuRwSharedLock read_lock(state_lock_);
  return reader_ ? reader_->num_segments() : 0;
}

Result<scoped_refptr<ReadableLogSegment>> Log::GetSegmentBySequenceNumber(const int64_t seq) const {
  PerCpuRwSharedLock read_lock(state_lock_);
  if (!reader_) {
    return STATUS(NotFound, "LogReader is not initialized");
  }

  return reader_->GetSegmentBySequenceNumber(seq);
}

bool Log::HasOnDiskData(FsManager* fs_manager, const string& wal_dir) {
  return fs_manager->env()->FileExists(wal_dir);
}

Status Log::DeleteOnDiskData(Env* env,
                             const string& tablet_id,
                             const string& wal_dir,
                             const string& peer_uuid) {
  if (!env->FileExists(wal_dir)) {
    return Status::OK();
  }
  LOG(INFO) << "T " << tablet_id << " P " << peer_uuid
            << ": Deleting WAL dir " << wal_dir;
  RETURN_NOT_OK_PREPEND(env->DeleteRecursively(wal_dir),
                        "Unable to recursively delete WAL dir for tablet " + tablet_id);
  return Status::OK();
}

Result<SegmentOpIdRelation> Log::GetSegmentOpIdRelation(
    ReadableLogSegment* segment, const OpId& op_id) {
  if (!op_id.is_valid_not_empty()) {
    return SegmentOpIdRelation::kOpIdAfterSegment;
  }

  const auto& footer = segment->footer();
  VLOG_WITH_PREFIX_AND_FUNC(2) << "footer.has_max_replicate_index(): "
                               << footer.has_max_replicate_index()
                               << " footer.max_replicate_index(): "
                               << footer.max_replicate_index();

  if (footer.has_max_replicate_index() && op_id.index > footer.max_replicate_index()) {
    return SegmentOpIdRelation::kOpIdAfterSegment;
  }

  auto read_entries = segment->ReadEntries();
  RETURN_NOT_OK(read_entries.status);

  const auto has_replicate = [](const auto& entry) {
    return entry->has_replicate();
  };
  const auto first_replicate =
      std::find_if(read_entries.entries.cbegin(), read_entries.entries.cend(), has_replicate);

  if (first_replicate == read_entries.entries.cend()) {
    return SegmentOpIdRelation::kEmptySegment;
  }

  const auto first_op_id = OpId::FromPB((*first_replicate)->replicate().id());
  if (op_id < first_op_id) {
    return SegmentOpIdRelation::kOpIdBeforeSegment;
  }

  RSTATUS_DCHECK_LE(first_op_id, op_id, InternalError, "Expected first_op_id <= op_id");

  const auto last_replicate = std::find_if(
      read_entries.entries.crbegin(), read_entries.entries.crend(), has_replicate);
  const auto last_op_id = OpId::FromPB((*last_replicate)->replicate().id());

  if (op_id > last_op_id) {
    return SegmentOpIdRelation::kOpIdAfterSegment;
  }

  if (op_id == last_op_id) {
    return SegmentOpIdRelation::kOpIdIsLast;
  }

  RSTATUS_DCHECK_LE(first_op_id, op_id, InternalError, "Expected first_op_id <= op_id");
  RSTATUS_DCHECK_LT(op_id, last_op_id, InternalError, "Expected op_id < last_op_id");
  return SegmentOpIdRelation::kOpIdIsInsideAndNotLast;
}

Result<bool> Log::CopySegmentUpTo(
    ReadableLogSegment* segment, const std::string& dest_wal_dir,
    const OpId& max_included_op_id) {
  SegmentOpIdRelation relation = VERIFY_RESULT(GetSegmentOpIdRelation(segment, max_included_op_id));
  auto* const env = options_.env;
  const auto sequence_number = segment->header().sequence_number();
  const auto file_name = FsManager::GetWalSegmentFileName(sequence_number);
  const auto src_path = JoinPathSegments(wal_dir_, file_name);
  SCHECK_EQ(src_path, segment->path(), InternalError, "Log segment path does not match");
  const auto dest_path = JoinPathSegments(dest_wal_dir, file_name);

  bool stop = false;
  switch (relation) {
    case SegmentOpIdRelation::kOpIdBeforeSegment:
      // Stop copying segments.
      return true;

    case SegmentOpIdRelation::kOpIdIsLast:
      // Stop after copying the current segment.
      stop = true;
      FALLTHROUGH_INTENDED;
    case SegmentOpIdRelation::kEmptySegment:
      FALLTHROUGH_INTENDED;
    case SegmentOpIdRelation::kOpIdAfterSegment:
      // Copy (hard-link) the whole segment.
      RETURN_NOT_OK(env->LinkFile(src_path, dest_path));
      VLOG_WITH_PREFIX(1) << Format("Hard linked $0 to $1", src_path, dest_path);
      return stop;

    case SegmentOpIdRelation::kOpIdIsInsideAndNotLast:
      // Copy part of the segment up to and including max_included_op_id.
      RETURN_NOT_OK(segment->CopyTo(
          env, GetNewSegmentWritableFileOptions(), dest_path, max_included_op_id));

      VLOG_WITH_PREFIX(1) << Format(
          "Copied $0 to $1, up to $2", src_path, dest_path, max_included_op_id);
      return true;
  }
  FATAL_INVALID_ENUM_VALUE(SegmentOpIdRelation, relation);
}

Status Log::CopyTo(const std::string& dest_wal_dir, const OpId max_included_op_id) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << "dest_wal_dir: " << dest_wal_dir
                               << " max_included_op_id: " << AsString(max_included_op_id);
  // We mainly need log_copy_mutex_ to simplify managing of log_copy_min_index_.
  std::lock_guard log_copy_lock(log_copy_mutex_);
  auto se = ScopeExit([this]() {
    std::lock_guard l(state_lock_);
    log_copy_min_index_ = std::numeric_limits<int64_t>::max();
  });

  SegmentSequence segments;
  scoped_refptr<LogIndex> log_index;
  {
    UniqueLock<PerCpuRwMutex> l(state_lock_);
    if (log_state_ != kLogInitialized) {
      SCHECK_EQ(log_state_, kLogWriting, IllegalState, Format("Invalid log state: $0", log_state_));
      ReverseLock<decltype(l)> rlock(l);
      // Rollover current active segment if it is not empty.
      RETURN_NOT_OK(AllocateSegmentAndRollOver());
    }

    SCHECK(
        log_state_ == kLogInitialized || log_state_ == kLogWriting, IllegalState,
        Format("Invalid log state: $0", log_state_));
    // Remember log_index, because it could be reset if someone closes the log after we release
    // state_lock_.
    log_index = log_index_;
    RETURN_NOT_OK(reader_->GetSegmentsSnapshot(&segments));

    // We skip the last snapshot segment because it might be mutable and is either:
    // 1) A segment that was empty when we tried to rollover at the beginning of the
    // function.
    // 2) A segment that was created after calling AllocateSegmentAndRollOver above (or
    // created even after by concurrent operations).
    // In both cases segments in snapshot prior to the last one contain all operations that
    // were present in log before calling Log::CopyTo and not yet GCed.
    RETURN_NOT_OK(segments.pop_back());

    // At this point all segments in `segments` are closed and immutable.
    // Looking for first op index.
    for (auto& segment : segments) {
      if (segment->readable_to_offset() <= segment->first_entry_offset()) {
        // Segment definitely has no entries.
        continue;
      }
      auto result = segment->ReadFirstReplicateEntryOpId();
      if (result.ok()) {
        // We've found first non-empty segment to copy, set an anchor for Log GC.
        // Note that concurrent modifications to log_copy_min_index_ are not possible, because the
        // whole function holds log_copy_mutex_ lock.
        log_copy_min_index_ = result->index;
        break;
      }
      if (result.status().IsNotFound()) {
        // No entries.
        continue;
      }
      // Failure.
      return result.status();
    }
  }

  RETURN_NOT_OK_PREPEND(
      options_.env->CreateDir(dest_wal_dir),
      Format("Failed to create tablet WAL dir $0", dest_wal_dir));

  for (const auto& segment : segments) {
    if (VERIFY_RESULT(CopySegmentUpTo(segment.get(), dest_wal_dir, max_included_op_id))) {
      break;
    }
  }

  return Status::OK();
}

uint64_t Log::NextSegmentDesiredSize() {
  return next_max_segment_size_.value_or(std::min(cur_max_segment_size_ * 2, max_segment_size_));
}

WritableFileOptions Log::GetNewSegmentWritableFileOptions() {
  WritableFileOptions opts;
  // We always want to sync on close: https://github.com/yugabyte/yugabyte-db/issues/3490
  opts.sync_on_close = true;
  opts.o_direct = durable_wal_write_;
  return opts;
}

Status Log::PreAllocateNewSegment() {
  TRACE_EVENT1("log", "PreAllocateNewSegment", "file", next_segment_path_);
  CHECK_EQ(allocation_state(), SegmentAllocationState::kAllocationInProgress);

  auto opts = GetNewSegmentWritableFileOptions();
  RETURN_NOT_OK(CreatePlaceholderSegment(opts, &next_segment_path_, &next_segment_file_));

  if (options_.preallocate_segments) {
    uint64_t next_segment_size = NextSegmentDesiredSize();
    TRACE("Preallocating $0 byte segment in $1", next_segment_size, next_segment_path_);
    // TODO (perf) zero the new segments -- this could result in additional performance
    // improvements.
    RETURN_NOT_OK(next_segment_file_->PreAllocate(next_segment_size));
  }

  if (new_segment_allocation_callback_) {
    RETURN_NOT_OK(new_segment_allocation_callback_());
  }

  allocation_state_.store(SegmentAllocationState::kAllocationFinished, std::memory_order_release);
  return Status::OK();
}

Status Log::SwitchToAllocatedSegment() {
  CHECK_EQ(allocation_state(), SegmentAllocationState::kAllocationFinished);
  // Increment "next" log segment seqno.
  active_segment_sequence_number_++;

  int64_t fault_after_min_replicate_index =
      FLAGS_TEST_log_fault_after_segment_allocation_min_replicate_index;
  if (PREDICT_FALSE(fault_after_min_replicate_index)) {
    if (reader_->GetMinReplicateIndex() >= fault_after_min_replicate_index) {
      MAYBE_FAULT(1.0);
    }
  }

  // Create a new segment.
  std::unique_ptr<WritableLogSegment> new_segment(
      new WritableLogSegment(next_segment_path_, next_segment_file_));

  // Set up the new header and footer.
  LogSegmentHeaderPB header;
  header.set_major_version(kLogMajorVersion);
  header.set_minor_version(kLogMinorVersion);
  header.set_sequence_number(active_segment_sequence_number_);
  header.set_unused_tablet_id(tablet_id_);

  // Set up the new footer. This will be maintained as the segment is written.
  footer_builder_.Clear();
  footer_builder_.set_num_entries(0);

  // Set the new segment's schema.
  {
    SharedLock<decltype(schema_lock_)> l(schema_lock_);
    SchemaToPB(*schema_, header.mutable_deprecated_schema());
    header.set_deprecated_schema_version(schema_version_);
  }

  if (PREDICT_FALSE(FLAGS_TEST_crash_before_wal_header_is_written)) {
    LOG_WITH_PREFIX(FATAL) << "Crash before wal header is written";
  }
  RETURN_NOT_OK(new_segment->WriteHeader(header));
  // Calling Sync() here is important because it ensures the file has a complete WAL header
  // on disk before renaming the file.
  {
    ADOPT_WAIT_STATE(appender_->wait_state());
    SCOPED_WAIT_STATUS(WAL_Sync);
    RETURN_NOT_OK(new_segment->Sync());
  }

  const auto new_segment_path =
      FsManager::GetWalSegmentFilePath(wal_dir_, active_segment_sequence_number_);
  // Rename should happen after writing the header and sync. Otherwise, if the server crashes
  // immediately after the rename, an incomplete WAL headers could cause future tablet bootstrap
  // to fail repeatedly.
  RETURN_NOT_OK(get_env()->RenameFile(next_segment_path_, new_segment_path));
  RETURN_NOT_OK(get_env()->SyncDir(wal_dir_));
  new_segment->set_path(new_segment_path);
  // Transform the currently-active segment into a readable one, since we need to be able to replay
  // the segments for other peers.
  {
    if (active_segment_.get() != nullptr) {
      std::lock_guard l(state_lock_);
      CHECK_OK(ReplaceSegmentInReaderUnlocked());
    }
  }

  // Open the segment we just created in readable form and add it to the reader.
  std::unique_ptr<RandomAccessFile> readable_file;
  RETURN_NOT_OK(get_env()->NewRandomAccessFile(new_segment_path, &readable_file));

  scoped_refptr<ReadableLogSegment> readable_segment(
    new ReadableLogSegment(new_segment_path,
                           shared_ptr<RandomAccessFile>(readable_file.release())));
  RETURN_NOT_OK(readable_segment->Init(header, new_segment->first_entry_offset()));
  RETURN_NOT_OK(reader_->AppendEmptySegment(readable_segment));
  // Now set 'active_segment_' to the new segment.
  {
    std::lock_guard lock(active_segment_mutex_);
    active_segment_ = std::move(new_segment);
  }

  cur_max_segment_size_ = NextSegmentDesiredSize();
  next_max_segment_size_.reset();

  allocation_state_.store(
      SegmentAllocationState::kAllocationNotStarted, std::memory_order_release);

  return Status::OK();
}

Status Log::ReplaceSegmentInReaderUnlocked() {
  // We should never switch to a new segment if we wrote nothing to the old one.
  CHECK(active_segment_->IsClosed());
  shared_ptr<RandomAccessFile> readable_file;
  RETURN_NOT_OK(OpenFileForRandom(
      get_env(), active_segment_->path(), &readable_file));

  scoped_refptr<ReadableLogSegment> readable_segment(
      new ReadableLogSegment(active_segment_->path(), readable_file));
  // Note: active_segment->header() will only contain an initialized PB if we wrote the header out.
  RETURN_NOT_OK(readable_segment->Init(active_segment_->header(),
                                       active_segment_->footer(),
                                       active_segment_->first_entry_offset()));

  return reader_->ReplaceLastSegment(readable_segment);
}

Status Log::CreatePlaceholderSegment(const WritableFileOptions& opts,
                                     string* result_path,
                                     shared_ptr<WritableFile>* out) {
  string path_tmpl = JoinPathSegments(wal_dir_, kSegmentPlaceholderFileTemplate);
  VLOG_WITH_PREFIX(2) << "Creating temp. file for place holder segment, template: " << path_tmpl;
  std::unique_ptr<WritableFile> segment_file;
  RETURN_NOT_OK(get_env()->NewTempWritableFile(opts,
                                               path_tmpl,
                                               result_path,
                                               &segment_file));
  VLOG_WITH_PREFIX(1) << "Created next WAL segment, placeholder path: " << *result_path;
  out->reset(segment_file.release());
  return Status::OK();
}

uint64_t Log::active_segment_sequence_number() const {
  return active_segment_sequence_number_;
}

Status Log::TEST_SubmitFuncToAppendToken(const std::function<void()>& func) {
  return appender_->TEST_SubmitFunc(func);
}

Status Log::ResetLastSyncedEntryOpId(const OpId& op_id) {
  RETURN_NOT_OK(WaitUntilAllFlushed());

  OpId old_value;
  {
    std::lock_guard write_lock(last_synced_entry_op_id_mutex_);
    old_value = last_synced_entry_op_id_.load(boost::memory_order_acquire);
    last_synced_entry_op_id_.store(op_id, boost::memory_order_release);
    YB_PROFILE(last_synced_entry_op_id_cond_.notify_all());
  }
  LOG_WITH_PREFIX(INFO) << "Reset last synced entry op id from " << old_value << " to " << op_id;

  return Status::OK();
}

Log::~Log() {
  WARN_NOT_OK(Close(), "Error closing log");
}

// ------------------------------------------------------------------------------------------------
// LogEntryBatch

Log::LogEntryBatch::LogEntryBatch(
    LogEntryTypePB type, std::shared_ptr<LWLogEntryBatchPB> entry_batch_pb)
    : type_(type),
      entry_batch_pb_(std::move(entry_batch_pb)),
      count_(entry_batch_pb_->entry().size()) {
  if (!IsMarkerType(type_)) {
    DCHECK_NE(entry_batch_pb_->mono_time(), 0);
  }
}

Log::LogEntryBatch::~LogEntryBatch() = default;

void Log::LogEntryBatch::MarkReserved() {
  DCHECK_EQ(state_, kEntryInitialized);
  state_ = kEntryReserved;
}

bool Log::LogEntryBatch::IsMarker() const {
  return count() == 1 && IsMarkerType(entry_batch_pb_->entry().front().type());
}

bool Log::LogEntryBatch::IsSingleEntryOfType(LogEntryTypePB type) const {
  return count() == 1 && entry_batch_pb_->entry().front().type() == type;
}

bool Log::LogEntryBatch::IsRolloverMarker() const {
  return count() == 1 && IsRolloverMarkerType(entry_batch_pb_->entry().front().type());
}

Status Log::LogEntryBatch::Serialize() {
  DCHECK_EQ(state_, kEntryReady);
  buffer_.clear();
  // *_MARKER LogEntries are markers and are not serialized.
  if (PREDICT_FALSE(IsMarker())) {
    total_size_bytes_ = 0;
    state_ = kEntrySerialized;
    return Status::OK();
  }
  SCHECK_NE(entry_batch_pb_->mono_time(), 0ULL, IllegalState, "Mono time should be specified");
  total_size_bytes_ = entry_batch_pb_->SerializedSize();
  buffer_.resize(total_size_bytes_);
  entry_batch_pb_->SerializeToArray(buffer_.data());

  state_ = kEntrySerialized;
  return Status::OK();
}

void Log::LogEntryBatch::MarkReady() {
  DCHECK_EQ(state_, kEntryReserved);
  state_ = kEntryReady;
}

}  // namespace log
}  // namespace yb
