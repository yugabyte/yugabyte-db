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

#include "kudu/consensus/log.h"

#include <algorithm>

#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/log_index.h"
#include "kudu/consensus/log_metrics.h"
#include "kudu/consensus/log_reader.h"
#include "kudu/consensus/log_util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/walltime.h"
#include "kudu/util/coding.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/env_util.h"
#include "kudu/util/fault_injection.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/kernel_stack_watchdog.h"
#include "kudu/util/logging.h"
#include "kudu/util/metrics.h"
#include "kudu/util/path_util.h"
#include "kudu/util/pb_util.h"
#include "kudu/util/random.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/thread.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/trace.h"

// Log retention configuration.
// -----------------------------
DEFINE_int32(log_min_segments_to_retain, 2,
             "The minimum number of past log segments to keep at all times,"
             " regardless of what is required for durability. "
             "Must be at least 1.");
TAG_FLAG(log_min_segments_to_retain, runtime);
TAG_FLAG(log_min_segments_to_retain, advanced);

DEFINE_int32(log_min_seconds_to_retain, 300,
             "The minimum number of seconds for which to keep log segments to keep at all times, "
             "regardless of what is required for durability. Logs may be still retained for "
             "a longer amount of time if they are necessary for correct restart. This should be "
             "set long enough such that a tablet server which has temporarily failed can be "
             "restarted within the given time period. If a server is down for longer than this "
             "amount of time, it is possible that its tablets will be re-replicated on other "
             "machines.");
TAG_FLAG(log_min_seconds_to_retain, runtime);
TAG_FLAG(log_min_seconds_to_retain, advanced);

// Group commit configuration.
// -----------------------------
DEFINE_int32(group_commit_queue_size_bytes, 4 * 1024 * 1024,
             "Maximum size of the group commit queue in bytes");
TAG_FLAG(group_commit_queue_size_bytes, advanced);

// Fault/latency injection flags.
// -----------------------------
DEFINE_bool(log_inject_latency, false,
            "If true, injects artificial latency in log sync operations. "
            "Advanced option. Use at your own risk -- has a negative effect "
            "on performance for obvious reasons!");
DEFINE_int32(log_inject_latency_ms_mean, 100,
             "The number of milliseconds of latency to inject, on average. "
             "Only takes effect if --log_inject_latency is true");
DEFINE_int32(log_inject_latency_ms_stddev, 100,
             "The standard deviation of latency to inject in the log. "
             "Only takes effect if --log_inject_latency is true");
DEFINE_double(fault_crash_before_append_commit, 0.0,
              "Fraction of the time when the server will crash just before appending a "
              "COMMIT message to the log. (For testing only!)");
TAG_FLAG(log_inject_latency, unsafe);
TAG_FLAG(log_inject_latency_ms_mean, unsafe);
TAG_FLAG(log_inject_latency_ms_stddev, unsafe);
TAG_FLAG(fault_crash_before_append_commit, unsafe);

// Validate that log_min_segments_to_retain >= 1
static bool ValidateLogsToRetain(const char* flagname, int value) {
  if (value >= 1) {
    return true;
  }
  LOG(ERROR) << strings::Substitute("$0 must be at least 1, value $1 is invalid",
                                    flagname, value);
  return false;
}
static bool dummy = google::RegisterFlagValidator(
    &FLAGS_log_min_segments_to_retain, &ValidateLogsToRetain);

static const char kSegmentPlaceholderFileTemplate[] = ".tmp.newsegmentXXXXXX";

namespace kudu {
namespace log {

using consensus::CommitMsg;
using consensus::OpId;
using consensus::ReplicateRefPtr;
using env_util::OpenFileForRandom;
using std::shared_ptr;
using strings::Substitute;

// This class is responsible for managing the thread that appends to
// the log file.
class Log::AppendThread {
 public:
  explicit AppendThread(Log* log);

  // Initializes the objects and starts the thread.
  Status Init();

  // Waits until the last enqueued elements are processed, sets the
  // Appender thread to closing state. If any entries are added to the
  // queue during the process, invoke their callbacks' 'OnFailure()'
  // method.
  void Shutdown();

 private:
  void RunThread();

  Log* const log_;

  // Lock to protect access to thread_ during shutdown.
  mutable boost::mutex lock_;
  scoped_refptr<Thread> thread_;
};


Log::AppendThread::AppendThread(Log *log)
  : log_(log) {
}

Status Log::AppendThread::Init() {
  DCHECK(!thread_) << "Already initialized";
  VLOG(1) << "Starting log append thread for tablet " << log_->tablet_id();
  RETURN_NOT_OK(kudu::Thread::Create("log", "appender",
      &AppendThread::RunThread, this, &thread_));
  return Status::OK();
}

void Log::AppendThread::RunThread() {
  bool shutting_down = false;
  while (PREDICT_TRUE(!shutting_down)) {
    std::vector<LogEntryBatch*> entry_batches;
    ElementDeleter d(&entry_batches);

    // We shut down the entry_queue when it's time to shut down the append
    // thread, which causes this call to return false, while still populating
    // the entry_batches vector with the final set of log entry batches that
    // were enqueued. We finish processing this last bunch of log entry batches
    // before exiting the main RunThread() loop.
    if (PREDICT_FALSE(!log_->entry_queue()->BlockingDrainTo(&entry_batches))) {
      shutting_down = true;
    }

    if (log_->metrics_) {
      log_->metrics_->entry_batches_per_group->Increment(entry_batches.size());
    }
    TRACE_EVENT1("log", "batch", "batch_size", entry_batches.size());

    SCOPED_LATENCY_METRIC(log_->metrics_, group_commit_latency);

    bool is_all_commits = true;
    for (LogEntryBatch* entry_batch : entry_batches) {
      entry_batch->WaitForReady();
      TRACE_EVENT_FLOW_END0("log", "Batch", entry_batch);
      Status s = log_->DoAppend(entry_batch);
      if (PREDICT_FALSE(!s.ok())) {
        LOG(ERROR) << "Error appending to the log: " << s.ToString();
        DLOG(FATAL) << "Aborting: " << s.ToString();
        entry_batch->set_failed_to_append();
        // TODO If a single transaction fails to append, should we
        // abort all subsequent transactions in this batch or allow
        // them to be appended? What about transactions in future
        // batches?
        if (!entry_batch->callback().is_null()) {
          entry_batch->callback().Run(s);
        }
      }
      if (is_all_commits && entry_batch->type_ != COMMIT) {
        is_all_commits = false;
      }
    }

    Status s;
    if (!is_all_commits) {
      s = log_->Sync();
    }
    if (PREDICT_FALSE(!s.ok())) {
      LOG(ERROR) << "Error syncing log" << s.ToString();
      DLOG(FATAL) << "Aborting: " << s.ToString();
      for (LogEntryBatch* entry_batch : entry_batches) {
        if (!entry_batch->callback().is_null()) {
          entry_batch->callback().Run(s);
        }
      }
    } else {
      TRACE_EVENT0("log", "Callbacks");
      VLOG(2) << "Synchronized " << entry_batches.size() << " entry batches";
      SCOPED_WATCH_STACK(100);
      for (LogEntryBatch* entry_batch : entry_batches) {
        if (PREDICT_TRUE(!entry_batch->failed_to_append()
                         && !entry_batch->callback().is_null())) {
          entry_batch->callback().Run(Status::OK());
        }
        // It's important to delete each batch as we see it, because
        // deleting it may free up memory from memory trackers, and the
        // callback of a later batch may want to use that memory.
        delete entry_batch;
      }
      entry_batches.clear();
    }
  }
  VLOG(1) << "Exiting AppendThread for tablet " << log_->tablet_id();
}

void Log::AppendThread::Shutdown() {
  log_->entry_queue()->Shutdown();
  boost::lock_guard<boost::mutex> lock_guard(lock_);
  if (thread_) {
    VLOG(1) << "Shutting down log append thread for tablet " << log_->tablet_id();
    CHECK_OK(ThreadJoiner(thread_.get()).Join());
    VLOG(1) << "Log append thread for tablet " << log_->tablet_id() << " is shut down";
    thread_.reset();
  }
}

// This task is submitted to allocation_pool_ in order to
// asynchronously pre-allocate new log segments.
void Log::SegmentAllocationTask() {
  allocation_status_.Set(PreAllocateNewSegment());
}

const Status Log::kLogShutdownStatus(
    Status::ServiceUnavailable("WAL is shutting down", "", ESHUTDOWN));

const uint64_t Log::kInitialLogSegmentSequenceNumber = 0L;

Status Log::Open(const LogOptions &options,
                 FsManager *fs_manager,
                 const std::string& tablet_id,
                 const Schema& schema,
                 uint32_t schema_version,
                 const scoped_refptr<MetricEntity>& metric_entity,
                 scoped_refptr<Log>* log) {

  string tablet_wal_path = fs_manager->GetTabletWalDir(tablet_id);
  RETURN_NOT_OK(fs_manager->CreateDirIfMissing(tablet_wal_path));

  scoped_refptr<Log> new_log(new Log(options,
                                     fs_manager,
                                     tablet_wal_path,
                                     tablet_id,
                                     schema,
                                     schema_version,
                                     metric_entity));
  RETURN_NOT_OK(new_log->Init());
  log->swap(new_log);
  return Status::OK();
}

Log::Log(LogOptions options, FsManager* fs_manager, string log_path,
         string tablet_id, const Schema& schema, uint32_t schema_version,
         const scoped_refptr<MetricEntity>& metric_entity)
    : options_(std::move(options)),
      fs_manager_(fs_manager),
      log_dir_(std::move(log_path)),
      tablet_id_(std::move(tablet_id)),
      schema_(schema),
      schema_version_(schema_version),
      active_segment_sequence_number_(0),
      log_state_(kLogInitialized),
      max_segment_size_(options_.segment_size_mb * 1024 * 1024),
      entry_batch_queue_(FLAGS_group_commit_queue_size_bytes),
      append_thread_(new AppendThread(this)),
      force_sync_all_(options_.force_fsync_all),
      sync_disabled_(false),
      allocation_state_(kAllocationNotStarted),
      metric_entity_(metric_entity) {
  CHECK_OK(ThreadPoolBuilder("log-alloc").set_max_threads(1).Build(&allocation_pool_));
  if (metric_entity_) {
    metrics_.reset(new LogMetrics(metric_entity_));
  }
}

Status Log::Init() {
  boost::lock_guard<percpu_rwlock> write_lock(state_lock_);
  CHECK_EQ(kLogInitialized, log_state_);

  // Init the index
  log_index_.reset(new LogIndex(log_dir_));

  // Reader for previous segments.
  RETURN_NOT_OK(LogReader::Open(fs_manager_,
                                log_index_,
                                tablet_id_,
                                metric_entity_.get(),
                                &reader_));

  // The case where we are continuing an existing log.
  // We must pick up where the previous WAL left off in terms of
  // sequence numbers.
  if (reader_->num_segments() != 0) {
    VLOG(1) << "Using existing " << reader_->num_segments()
            << " segments from path: " << fs_manager_->GetWalsRootDir();

    vector<scoped_refptr<ReadableLogSegment> > segments;
    RETURN_NOT_OK(reader_->GetSegmentsSnapshot(&segments));
    active_segment_sequence_number_ = segments.back()->header().sequence_number();
  }

  if (force_sync_all_) {
    KLOG_FIRST_N(INFO, 1) << "Log is configured to fsync() on all Append() calls";
  } else {
    KLOG_FIRST_N(INFO, 1) << "Log is configured to *not* fsync() on all Append() calls";
  }

  // We always create a new segment when the log starts.
  RETURN_NOT_OK(AsyncAllocateSegment());
  RETURN_NOT_OK(allocation_status_.Get());
  RETURN_NOT_OK(SwitchToAllocatedSegment());

  RETURN_NOT_OK(append_thread_->Init());
  log_state_ = kLogWriting;
  return Status::OK();
}

Status Log::AsyncAllocateSegment() {
  boost::lock_guard<boost::shared_mutex> lock_guard(allocation_lock_);
  CHECK_EQ(allocation_state_, kAllocationNotStarted);
  allocation_status_.Reset();
  allocation_state_ = kAllocationInProgress;
  RETURN_NOT_OK(allocation_pool_->SubmitClosure(
                  Bind(&Log::SegmentAllocationTask, Unretained(this))));
  return Status::OK();
}

Status Log::CloseCurrentSegment() {
  if (!footer_builder_.has_min_replicate_index()) {
    VLOG(1) << "Writing a segment without any REPLICATE message. "
        "Segment: " << active_segment_->path();
  }
  VLOG(2) << "Segment footer for " << active_segment_->path()
          << ": " << footer_builder_.ShortDebugString();

  footer_builder_.set_close_timestamp_micros(GetCurrentTimeMicros());
  RETURN_NOT_OK(active_segment_->WriteFooterAndClose(footer_builder_));

  return Status::OK();
}

Status Log::RollOver() {
  SCOPED_LATENCY_METRIC(metrics_, roll_latency);

  // Check if any errors have occurred during allocation
  RETURN_NOT_OK(allocation_status_.Get());

  DCHECK_EQ(allocation_state(), kAllocationFinished);

  RETURN_NOT_OK(Sync());
  RETURN_NOT_OK(CloseCurrentSegment());

  RETURN_NOT_OK(SwitchToAllocatedSegment());

  LOG(INFO) << "Rolled over to a new segment: " << active_segment_->path();
  return Status::OK();
}

Status Log::Reserve(LogEntryTypePB type,
                    gscoped_ptr<LogEntryBatchPB> entry_batch,
                    LogEntryBatch** reserved_entry) {
  TRACE_EVENT0("log", "Log::Reserve");
  DCHECK(reserved_entry != nullptr);
  {
    boost::shared_lock<rw_spinlock> read_lock(state_lock_.get_lock());
    CHECK_EQ(kLogWriting, log_state_);
  }

  // In DEBUG builds, verify that all of the entries in the batch match the specified type.
  // In non-debug builds the foreach loop gets optimized out.
  #ifndef NDEBUG
  for (const LogEntryPB& entry : entry_batch->entry()) {
    DCHECK_EQ(entry.type(), type) << "Bad batch: " << entry_batch->DebugString();
  }
  #endif

  int num_ops = entry_batch->entry_size();
  gscoped_ptr<LogEntryBatch> new_entry_batch(new LogEntryBatch(type, entry_batch.Pass(), num_ops));
  new_entry_batch->MarkReserved();

  if (PREDICT_FALSE(!entry_batch_queue_.BlockingPut(new_entry_batch.get()))) {
    return kLogShutdownStatus;
  }

  // Release the memory back to the caller: this will be freed when
  // the entry is removed from the queue.
  //
  // TODO (perf) Use a ring buffer instead of a blocking queue and set
  // 'reserved_entry' to a pre-allocated slot in the buffer.
  *reserved_entry = new_entry_batch.release();
  return Status::OK();
}

Status Log::AsyncAppend(LogEntryBatch* entry_batch, const StatusCallback& callback) {
  TRACE_EVENT0("log", "Log::AsyncAppend");
  {
    boost::shared_lock<rw_spinlock> read_lock(state_lock_.get_lock());
    CHECK_EQ(kLogWriting, log_state_);
  }

  RETURN_NOT_OK(entry_batch->Serialize());
  entry_batch->set_callback(callback);
  TRACE("Serialized $0 byte log entry", entry_batch->total_size_bytes());
  TRACE_EVENT_FLOW_BEGIN0("log", "Batch", entry_batch);
  entry_batch->MarkReady();

  return Status::OK();
}

Status Log::AsyncAppendReplicates(const vector<ReplicateRefPtr>& msgs,
                                  const StatusCallback& callback) {
  gscoped_ptr<LogEntryBatchPB> batch;
  CreateBatchFromAllocatedOperations(msgs, &batch);

  LogEntryBatch* reserved_entry_batch;
  RETURN_NOT_OK(Reserve(REPLICATE, batch.Pass(), &reserved_entry_batch));
  // If we're able to reserve set the vector of replicate scoped ptrs in
  // the LogEntryBatch. This will make sure there's a reference for each
  // replicate while we're appending.
  reserved_entry_batch->SetReplicates(msgs);

  RETURN_NOT_OK(AsyncAppend(reserved_entry_batch, callback));
  return Status::OK();
}

Status Log::AsyncAppendCommit(gscoped_ptr<consensus::CommitMsg> commit_msg,
                              const StatusCallback& callback) {
  MAYBE_FAULT(FLAGS_fault_crash_before_append_commit);

  gscoped_ptr<LogEntryBatchPB> batch(new LogEntryBatchPB);
  LogEntryPB* entry = batch->add_entry();
  entry->set_type(COMMIT);
  entry->set_allocated_commit(commit_msg.release());

  LogEntryBatch* reserved_entry_batch;
  RETURN_NOT_OK(Reserve(COMMIT, batch.Pass(), &reserved_entry_batch));

  RETURN_NOT_OK(AsyncAppend(reserved_entry_batch, callback));
  return Status::OK();
}

Status Log::DoAppend(LogEntryBatch* entry_batch, bool caller_owns_operation) {
  size_t num_entries = entry_batch->count();
  DCHECK_GT(num_entries, 0) << "Cannot call DoAppend() with zero entries reserved";

  Slice entry_batch_data = entry_batch->data();
  uint32_t entry_batch_bytes = entry_batch->total_size_bytes();
  // If there is no data to write return OK.
  if (PREDICT_FALSE(entry_batch_bytes == 0)) {
    return Status::OK();
  }

  // We keep track of the last-written OpId here.
  // This is needed to initialize Consensus on startup.
  if (entry_batch->type_ == REPLICATE) {
    // TODO Probably remove the code below as it looks suspicious: Tablet peer uses this
    // as 'safe' anchor as it believes it in the log, when it actually isn't, i.e. this
    // is not the last durable operation. Either move this to tablet peer (since we're
    // using in flights anyway no need to scan for ids here) or actually delay doing this
    // until fsync() has been done. See KUDU-527.
    boost::lock_guard<rw_spinlock> write_lock(last_entry_op_id_lock_);
    last_entry_op_id_.CopyFrom(entry_batch->MaxReplicateOpId());
  }

  // if the size of this entry overflows the current segment, get a new one
  if (allocation_state() == kAllocationNotStarted) {
    if ((active_segment_->Size() + entry_batch_bytes + 4) > max_segment_size_) {
      LOG(INFO) << "Max segment size reached. Starting new segment allocation. ";
      RETURN_NOT_OK(AsyncAllocateSegment());
      if (!options_.async_preallocate_segments) {
        LOG_SLOW_EXECUTION(WARNING, 50, "Log roll took a long time") {
          RETURN_NOT_OK(RollOver());
        }
      }
    }
  } else if (allocation_state() == kAllocationFinished) {
    LOG_SLOW_EXECUTION(WARNING, 50, "Log roll took a long time") {
      RETURN_NOT_OK(RollOver());
    }
  } else {
    VLOG(1) << "Segment allocation already in progress...";
  }

  int64_t start_offset = active_segment_->written_offset();

  LOG_SLOW_EXECUTION(WARNING, 50, "Append to log took a long time") {
    SCOPED_LATENCY_METRIC(metrics_, append_latency);
    SCOPED_WATCH_STACK(500);

    RETURN_NOT_OK(active_segment_->WriteEntryBatch(entry_batch_data));

    // Update the reader on how far it can read the active segment.
    reader_->UpdateLastSegmentOffset(active_segment_->written_offset());

    if (log_hooks_) {
      RETURN_NOT_OK_PREPEND(log_hooks_->PostAppend(), "PostAppend hook failed");
    }
  }

  if (metrics_) {
    metrics_->bytes_logged->IncrementBy(entry_batch_bytes);
  }

  CHECK_OK(UpdateIndexForBatch(*entry_batch, start_offset));
  UpdateFooterForBatch(entry_batch);

  // For REPLICATE batches, we expect the caller to free the actual entries if
  // caller_owns_operation is set.
  if (entry_batch->type_ == REPLICATE && caller_owns_operation) {
    for (int i = 0; i < entry_batch->entry_batch_pb_->entry_size(); i++) {
      LogEntryPB* entry_pb = entry_batch->entry_batch_pb_->mutable_entry(i);
      entry_pb->release_replicate();
    }
  }

  return Status::OK();
}

Status Log::UpdateIndexForBatch(const LogEntryBatch& batch,
                                int64_t start_offset) {
  if (batch.type_ != REPLICATE) {
    return Status::OK();
  }

  for (const LogEntryPB& entry_pb : batch.entry_batch_pb_->entry()) {
    LogIndexEntry index_entry;

    index_entry.op_id = entry_pb.replicate().id();
    index_entry.segment_sequence_number = active_segment_sequence_number_;
    index_entry.offset_in_segment = start_offset;
    RETURN_NOT_OK(log_index_->AddEntry(index_entry));
  }
  return Status::OK();
}

void Log::UpdateFooterForBatch(LogEntryBatch* batch) {
  footer_builder_.set_num_entries(footer_builder_.num_entries() + batch->count());

  // We keep track of the last-written OpId here.
  // This is needed to initialize Consensus on startup.
  // We also retrieve the opid of the first operation in the batch so that, if
  // we roll over to a new segment, we set the first operation in the footer
  // immediately.
  if (batch->type_ == REPLICATE) {
    // Update the index bounds for the current segment.
    for (const LogEntryPB& entry_pb : batch->entry_batch_pb_->entry()) {
      int64_t index = entry_pb.replicate().id().index();
      if (!footer_builder_.has_min_replicate_index() ||
          index < footer_builder_.min_replicate_index()) {
        footer_builder_.set_min_replicate_index(index);
      }
      if (!footer_builder_.has_max_replicate_index() ||
          index > footer_builder_.max_replicate_index()) {
        footer_builder_.set_max_replicate_index(index);
      }
    }
  }
}

Status Log::AllocateSegmentAndRollOver() {
  RETURN_NOT_OK(AsyncAllocateSegment());
  return RollOver();
}

FsManager* Log::GetFsManager() {
  return fs_manager_;
}

Status Log::Sync() {
  TRACE_EVENT0("log", "Sync");
  SCOPED_LATENCY_METRIC(metrics_, sync_latency);

  if (PREDICT_FALSE(FLAGS_log_inject_latency && !sync_disabled_)) {
    Random r(GetCurrentTimeMicros());
    int sleep_ms = r.Normal(FLAGS_log_inject_latency_ms_mean,
                            FLAGS_log_inject_latency_ms_stddev);
    if (sleep_ms > 0) {
      LOG(INFO) << "T " << tablet_id_ << ": Injecting "
                << sleep_ms << "ms of latency in Log::Sync()";
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
    }
  }

  if (force_sync_all_ && !sync_disabled_) {
    LOG_SLOW_EXECUTION(WARNING, 50, "Fsync log took a long time") {
      RETURN_NOT_OK(active_segment_->Sync());

      if (log_hooks_) {
        RETURN_NOT_OK_PREPEND(log_hooks_->PostSyncIfFsyncEnabled(),
                              "PostSyncIfFsyncEnabled hook failed");
      }
    }
  }

  if (log_hooks_) {
    RETURN_NOT_OK_PREPEND(log_hooks_->PostSync(), "PostSync hook failed");
  }
  return Status::OK();
}

Status Log::GetSegmentsToGCUnlocked(int64_t min_op_idx, SegmentSequence* segments_to_gc) const {
  // Find the prefix of segments in the segment sequence that is guaranteed not to include
  // 'min_op_idx'.
  RETURN_NOT_OK(reader_->GetSegmentPrefixNotIncluding(min_op_idx, segments_to_gc));

  int max_to_delete = std::max(reader_->num_segments() - FLAGS_log_min_segments_to_retain, 0);
  if (segments_to_gc->size() > max_to_delete) {
    VLOG(2) << "GCing " << segments_to_gc->size() << " in " << log_dir_
        << " would not leave enough remaining segments to satisfy minimum "
        << "retention requirement. Only considering "
        << max_to_delete << "/" << reader_->num_segments();
    segments_to_gc->resize(max_to_delete);
  } else if (segments_to_gc->size() < max_to_delete) {
    int extra_segments = max_to_delete - segments_to_gc->size();
    VLOG(2) << tablet_id_ << " has too many log segments, need to GC "
        << extra_segments << " more. ";
  }

  // Don't GC segments that are newer than the configured time-based retention.
  int64_t now = GetCurrentTimeMicros();
  for (int i = 0; i < segments_to_gc->size(); i++) {
    const scoped_refptr<ReadableLogSegment>& segment = (*segments_to_gc)[i];

    // Segments here will always have a footer, since we don't return the in-progress segment
    // up above. However, segments written by older Kudu builds may not have the timestamp
    // info. In that case, we're allowed to GC them.
    if (!segment->footer().has_close_timestamp_micros()) continue;

    int64_t age_seconds = (now - segment->footer().close_timestamp_micros()) / 1000000;
    if (age_seconds < FLAGS_log_min_seconds_to_retain) {
      VLOG(2) << "Segment " << segment->path() << " is only " << age_seconds << "s old: "
              << "cannot GC it yet due to configured time-based retention policy.";
      // Truncate the list of segments to GC here -- if this one is too new, then
      // all later ones are also too new.
      segments_to_gc->resize(i);
      break;
    }
  }

  return Status::OK();
}

Status Log::Append(LogEntryPB* phys_entry) {
  gscoped_ptr<LogEntryBatchPB> entry_batch_pb(new LogEntryBatchPB);
  entry_batch_pb->mutable_entry()->AddAllocated(phys_entry);
  LogEntryBatch entry_batch(phys_entry->type(), entry_batch_pb.Pass(), 1);
  entry_batch.state_ = LogEntryBatch::kEntryReserved;
  Status s = entry_batch.Serialize();
  if (s.ok()) {
    entry_batch.state_ = LogEntryBatch::kEntryReady;
    s = DoAppend(&entry_batch, false);
    if (s.ok()) {
      s = Sync();
    }
  }
  entry_batch.entry_batch_pb_->mutable_entry()->ExtractSubrange(0, 1, nullptr);
  return s;
}

Status Log::WaitUntilAllFlushed() {
  // In order to make sure we empty the queue we need to use
  // the async api.
  gscoped_ptr<LogEntryBatchPB> entry_batch(new LogEntryBatchPB);
  entry_batch->add_entry()->set_type(log::FLUSH_MARKER);
  LogEntryBatch* reserved_entry_batch;
  RETURN_NOT_OK(Reserve(FLUSH_MARKER, entry_batch.Pass(), &reserved_entry_batch));
  Synchronizer s;
  RETURN_NOT_OK(AsyncAppend(reserved_entry_batch, s.AsStatusCallback()));
  return s.Wait();
}

void Log::GetLatestEntryOpId(consensus::OpId* op_id) const {
  boost::shared_lock<rw_spinlock> read_lock(last_entry_op_id_lock_);
  if (last_entry_op_id_.IsInitialized()) {
    DCHECK_NOTNULL(op_id)->CopyFrom(last_entry_op_id_);
  } else {
    *op_id = consensus::MinimumOpId();
  }
}

Status Log::GC(int64_t min_op_idx, int32_t* num_gced) {
  CHECK_GE(min_op_idx, 0);

  VLOG(1) << "Running Log GC on " << log_dir_ << ": retaining ops >= " << min_op_idx;
  VLOG_TIMING(1, "Log GC") {
    SegmentSequence segments_to_delete;

    {
      boost::lock_guard<percpu_rwlock> l(state_lock_);
      CHECK_EQ(kLogWriting, log_state_);

      GetSegmentsToGCUnlocked(min_op_idx, &segments_to_delete);

      if (segments_to_delete.size() == 0) {
        VLOG(1) << "No segments to delete.";
        *num_gced = 0;
        return Status::OK();
      }
      // Trim the prefix of segments from the reader so that they are no longer
      // referenced by the log.
      RETURN_NOT_OK(reader_->TrimSegmentsUpToAndIncluding(
          segments_to_delete[segments_to_delete.size() - 1]->header().sequence_number()));
    }

    // Now that they are no longer referenced by the Log, delete the files.
    *num_gced = 0;
    for (const scoped_refptr<ReadableLogSegment>& segment : segments_to_delete) {
      LOG(INFO) << "Deleting log segment in path: " << segment->path()
                << " (GCed ops < " << min_op_idx << ")";
      RETURN_NOT_OK(fs_manager_->env()->DeleteFile(segment->path()));
      (*num_gced)++;
    }

    // Determine the minimum remaining replicate index in order to properly GC
    // the index chunks.
    int64_t min_remaining_op_idx = reader_->GetMinReplicateIndex();
    if (min_remaining_op_idx > 0) {
      log_index_->GC(min_remaining_op_idx);
    }
  }
  return Status::OK();
}

void Log::GetGCableDataSize(int64_t min_op_idx, int64_t* total_size) const {
  CHECK_GE(min_op_idx, 0);
  SegmentSequence segments_to_delete;
  *total_size = 0;
  {
    boost::shared_lock<rw_spinlock> read_lock(state_lock_.get_lock());
    CHECK_EQ(kLogWriting, log_state_);
    Status s = GetSegmentsToGCUnlocked(min_op_idx, &segments_to_delete);

    if (!s.ok() || segments_to_delete.size() == 0) {
      return;
    }
  }
  for (const scoped_refptr<ReadableLogSegment>& segment : segments_to_delete) {
    *total_size += segment->file_size();
  }
}

void Log::GetMaxIndexesToSegmentSizeMap(int64_t min_op_idx,
                                        std::map<int64_t, int64_t>* max_idx_to_segment_size)
                                        const {
  boost::shared_lock<rw_spinlock> read_lock(state_lock_.get_lock());
  CHECK_EQ(kLogWriting, log_state_);
  // We want to retain segments so we're only asking the extra ones.
  int segments_count = std::max(reader_->num_segments() - FLAGS_log_min_segments_to_retain, 0);
  if (segments_count == 0) {
    return;
  }

  int64_t now = GetCurrentTimeMicros();
  int64_t max_close_time_us = now - (FLAGS_log_min_seconds_to_retain * 1000000);
  reader_->GetMaxIndexesToSegmentSizeMap(min_op_idx, segments_count, max_close_time_us,
                                         max_idx_to_segment_size);
}

LogReader* Log::GetLogReader() const {
  return reader_.get();
}

void Log::SetSchemaForNextLogSegment(const Schema& schema,
                                     uint32_t version) {
  boost::lock_guard<rw_spinlock> l(schema_lock_);
  schema_ = schema;
  schema_version_ = version;
}

Status Log::Close() {
  allocation_pool_->Shutdown();
  append_thread_->Shutdown();

  boost::lock_guard<percpu_rwlock> l(state_lock_);
  switch (log_state_) {
    case kLogWriting:
      if (log_hooks_) {
        RETURN_NOT_OK_PREPEND(log_hooks_->PreClose(),
                              "PreClose hook failed");
      }
      RETURN_NOT_OK(Sync());
      RETURN_NOT_OK(CloseCurrentSegment());
      RETURN_NOT_OK(ReplaceSegmentInReaderUnlocked());
      log_state_ = kLogClosed;
      VLOG(1) << "Log closed";

      // Release FDs held by these objects.
      log_index_.reset();
      reader_.reset();

      if (log_hooks_) {
        RETURN_NOT_OK_PREPEND(log_hooks_->PostClose(),
                              "PostClose hook failed");
      }
      return Status::OK();

    case kLogClosed:
      VLOG(1) << "Log already closed";
      return Status::OK();

    default:
      return Status::IllegalState(Substitute("Bad state for Close() $0", log_state_));
  }
}

Status Log::DeleteOnDiskData(FsManager* fs_manager, const string& tablet_id) {
  string wal_dir = fs_manager->GetTabletWalDir(tablet_id);
  Env* env = fs_manager->env();
  if (!env->FileExists(wal_dir)) {
    return Status::OK();
  }
  RETURN_NOT_OK_PREPEND(env->DeleteRecursively(wal_dir),
                        "Unable to recursively delete WAL dir for tablet " + tablet_id);
  return Status::OK();
}

Status Log::PreAllocateNewSegment() {
  TRACE_EVENT1("log", "PreAllocateNewSegment", "file", next_segment_path_);
  CHECK_EQ(allocation_state(), kAllocationInProgress);

  WritableFileOptions opts;
  opts.sync_on_close = force_sync_all_;
  RETURN_NOT_OK(CreatePlaceholderSegment(opts, &next_segment_path_, &next_segment_file_));

  if (options_.preallocate_segments) {
    TRACE("Preallocating $0 byte segment in $1", max_segment_size_, next_segment_path_);
    // TODO (perf) zero the new segments -- this could result in
    // additional performance improvements.
    RETURN_NOT_OK(next_segment_file_->PreAllocate(max_segment_size_));
  }

  {
    boost::lock_guard<boost::shared_mutex> lock_guard(allocation_lock_);
    allocation_state_ = kAllocationFinished;
  }
  return Status::OK();
}

Status Log::SwitchToAllocatedSegment() {
  CHECK_EQ(allocation_state(), kAllocationFinished);

  // Increment "next" log segment seqno.
  active_segment_sequence_number_++;

  string new_segment_path = fs_manager_->GetWalSegmentFileName(tablet_id_,
                                                               active_segment_sequence_number_);

  RETURN_NOT_OK(fs_manager_->env()->RenameFile(next_segment_path_, new_segment_path));
  if (force_sync_all_) {
    RETURN_NOT_OK(fs_manager_->env()->SyncDir(log_dir_));
  }

  // Create a new segment.
  gscoped_ptr<WritableLogSegment> new_segment(
      new WritableLogSegment(new_segment_path, next_segment_file_));

  // Set up the new header and footer.
  LogSegmentHeaderPB header;
  header.set_major_version(kLogMajorVersion);
  header.set_minor_version(kLogMinorVersion);
  header.set_sequence_number(active_segment_sequence_number_);
  header.set_tablet_id(tablet_id_);

  // Set up the new footer. This will be maintained as the segment is written.
  footer_builder_.Clear();
  footer_builder_.set_num_entries(0);


  // Set the new segment's schema.
  {
    boost::shared_lock<rw_spinlock> l(schema_lock_);
    RETURN_NOT_OK(SchemaToPB(schema_, header.mutable_schema()));
    header.set_schema_version(schema_version_);
  }

  RETURN_NOT_OK(new_segment->WriteHeaderAndOpen(header));

  // Transform the currently-active segment into a readable one, since we
  // need to be able to replay the segments for other peers.
  {
    if (active_segment_.get() != nullptr) {
      boost::lock_guard<percpu_rwlock> l(state_lock_);
      CHECK_OK(ReplaceSegmentInReaderUnlocked());
    }
  }

  // Open the segment we just created in readable form and add it to the reader.
  gscoped_ptr<RandomAccessFile> readable_file;

  RandomAccessFileOptions opts;
  RETURN_NOT_OK(fs_manager_->env()->NewRandomAccessFile(opts, new_segment_path, &readable_file));
  scoped_refptr<ReadableLogSegment> readable_segment(
    new ReadableLogSegment(new_segment_path,
                           shared_ptr<RandomAccessFile>(readable_file.release())));
  RETURN_NOT_OK(readable_segment->Init(header, new_segment->first_entry_offset()));
  RETURN_NOT_OK(reader_->AppendEmptySegment(readable_segment));

  // Now set 'active_segment_' to the new segment.
  active_segment_.reset(new_segment.release());

  allocation_state_ = kAllocationNotStarted;

  return Status::OK();
}

Status Log::ReplaceSegmentInReaderUnlocked() {
  // We should never switch to a new segment if we wrote nothing to the old one.
  CHECK(active_segment_->IsClosed());
  shared_ptr<RandomAccessFile> readable_file;
  RETURN_NOT_OK(OpenFileForRandom(fs_manager_->env(), active_segment_->path(), &readable_file));
  scoped_refptr<ReadableLogSegment> readable_segment(
      new ReadableLogSegment(active_segment_->path(),
                             readable_file));
  // Note: active_segment_->header() will only contain an initialized PB if we
  // wrote the header out.
  RETURN_NOT_OK(readable_segment->Init(active_segment_->header(),
                                       active_segment_->footer(),
                                       active_segment_->first_entry_offset()));

  return reader_->ReplaceLastSegment(readable_segment);
}

Status Log::CreatePlaceholderSegment(const WritableFileOptions& opts,
                                     string* result_path,
                                     shared_ptr<WritableFile>* out) {
  string path_tmpl = JoinPathSegments(log_dir_, kSegmentPlaceholderFileTemplate);
  VLOG(2) << "Creating temp. file for place holder segment, template: " << path_tmpl;
  gscoped_ptr<WritableFile> segment_file;
  RETURN_NOT_OK(fs_manager_->env()->NewTempWritableFile(opts,
                                                        path_tmpl,
                                                        result_path,
                                                        &segment_file));
  VLOG(1) << "Created next WAL segment, placeholder path: " << *result_path;
  out->reset(segment_file.release());
  return Status::OK();
}

Log::~Log() {
  WARN_NOT_OK(Close(), "Error closing log");
}

LogEntryBatch::LogEntryBatch(LogEntryTypePB type,
                             gscoped_ptr<LogEntryBatchPB> entry_batch_pb, size_t count)
    : type_(type),
      entry_batch_pb_(entry_batch_pb.Pass()),
      total_size_bytes_(
          PREDICT_FALSE(count == 1 && entry_batch_pb_->entry(0).type() == FLUSH_MARKER) ?
          0 : entry_batch_pb_->ByteSize()),
      count_(count),
      state_(kEntryInitialized) {
}

LogEntryBatch::~LogEntryBatch() {
}

void LogEntryBatch::MarkReserved() {
  DCHECK_EQ(state_, kEntryInitialized);
  ready_lock_.Lock();
  state_ = kEntryReserved;
}

Status LogEntryBatch::Serialize() {
  DCHECK_EQ(state_, kEntryReserved);
  buffer_.clear();
  // FLUSH_MARKER LogEntries are markers and are not serialized.
  if (PREDICT_FALSE(count() == 1 && entry_batch_pb_->entry(0).type() == FLUSH_MARKER)) {
    state_ = kEntrySerialized;
    return Status::OK();
  }
  buffer_.reserve(total_size_bytes_);

  if (!pb_util::AppendToString(*entry_batch_pb_, &buffer_)) {
    return Status::IOError(Substitute("unable to serialize the entry batch, contents: $1",
                                      entry_batch_pb_->DebugString()));
  }

  state_ = kEntrySerialized;
  return Status::OK();
}

void LogEntryBatch::MarkReady() {
  DCHECK_EQ(state_, kEntrySerialized);
  state_ = kEntryReady;
  ready_lock_.Unlock();
}

void LogEntryBatch::WaitForReady() {
  ready_lock_.Lock();
  DCHECK_EQ(state_, kEntryReady);
  ready_lock_.Unlock();
}

}  // namespace log
}  // namespace kudu
