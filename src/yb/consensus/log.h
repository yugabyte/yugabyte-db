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

#ifndef YB_CONSENSUS_LOG_H_
#define YB_CONSENSUS_LOG_H_

#include <pthread.h>
#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/atomic.hpp>
#include <glog/logging.h>

#include "yb/common/common_fwd.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/log_util.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/spinlock.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/opid.h"
#include "yb/util/promise.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_callback.h"
#include "yb/util/threadpool.h"

namespace yb {

class MetricEntity;
class ThreadPool;

namespace cdc {
class CDCServiceTestMaxRentionTime_TestLogRetentionByOpId_MaxRentionTime_Test;
class CDCServiceTestMinSpace_TestLogRetentionByOpId_MinSpace_Test;
}

namespace log {

YB_STRONGLY_TYPED_BOOL(CreateNewSegment);
YB_DEFINE_ENUM(
    SegmentAllocationState,
    (kAllocationNotStarted)  // No segment allocation requested
    (kAllocationInProgress)  // Next segment allocation started
    (kAllocationFinished)    // Next segment ready
);

YB_DEFINE_ENUM(
    SegmentOpIdRelation,
    // Segment is empty
    (kEmptySegment)
    // OpId is before the segment
    (kOpIdBeforeSegment)
    // OpId is inside the segment, but not the last
    (kOpIdIsInsideAndNotLast)
    // OpId corresponds to the last operation in the segment
    (kOpIdIsLast)
    // OpId is after the segment
    (kOpIdAfterSegment)
);

// Log interface, inspired by Raft's (logcabin) Log. Provides durability to YugaByte as a normal
// Write Ahead Log and also plays the role of persistent storage for the consensus state machine.
//
// Note: This class is not thread safe, the caller is expected to synchronize Log::Reserve() and
// Log::Append() calls.
//
// Log uses group commit to improve write throughput and latency without compromising ordering and
// durability guarantees.
//
// To add operations to the log, the caller must obtain the lock and call Reserve() with the
// collection of operations to be added. Then, the caller may release the lock and call
// AsyncAppend(). Reserve() reserves a slot on a queue for the log entry; AsyncAppend() indicates
// that the entry in the slot is safe to write to disk and adds a callback that will be invoked once
// the entry is written and synchronized to disk.
//
// For sample usage see mt-log-test.cc
//
// Methods on this class are _not_ thread-safe and must be externally synchronized unless otherwise
// noted.
//
// Note: The Log needs to be Close()d before any log-writing class is destroyed, otherwise the Log
// might hold references to these classes to execute the callbacks after each write.
class Log : public RefCountedThreadSafe<Log> {
 public:
  static const Status kLogShutdownStatus;

  // Opens or continues a log and sets 'log' to the newly built Log.
  // After a successful Open() the Log is ready to receive entries, if create_new_segment is true.
  static Status Open(const LogOptions &options,
                             const std::string& tablet_id,
                             const std::string& wal_dir,
                             const std::string& peer_uuid,
                             const Schema& schema,
                             uint32_t schema_version,
                             const scoped_refptr<MetricEntity>& table_metric_entity,
                             const scoped_refptr<MetricEntity>& tablet_metric_entity,
                             ThreadPool *append_thread_pool,
                             ThreadPool* allocation_thread_pool,
                             int64_t cdc_min_replicated_index,
                             scoped_refptr<Log> *log,
                             CreateNewSegment create_new_segment = CreateNewSegment::kTrue);

  ~Log();

  // Reserves a spot in the log's queue for 'entry_batch'.
  //
  // 'reserved_entry' is initialized by this method and any resources associated with it will be
  // released in AsyncAppend().  In order to ensure correct ordering of operations across multiple
  // threads, calls to this method must be externally synchronized.
  //
  // WARNING: the caller _must_ call AsyncAppend() or else the log will "stall" and will never be
  // able to make forward progress.
  void Reserve(LogEntryTypePB type, LogEntryBatchPB* entry_batch, LogEntryBatch** reserved_entry);

  // Asynchronously appends 'entry' to the log. Once the append completes and is synced, 'callback'
  // will be invoked.
  Status AsyncAppend(LogEntryBatch* entry,
                             const StatusCallback& callback);

  Status TEST_AsyncAppendWithReplicates(
      LogEntryBatch* entry, const ReplicateMsgs& replicates, const StatusCallback& callback);

  // Synchronously append a new entry to the log.  Log does not take ownership of the passed
  // 'entry'. If skip_wal_write is true, only update consensus metadata and LogIndex, skip write
  // to wal.
  // TODO get rid of this method, transition to the asynchronous API.
  Status Append(LogEntryPB* entry, LogEntryMetadata entry_metadata, bool skip_wal_write = false);

  // Append the given set of replicate messages, asynchronously.  This requires that the replicates
  // have already been assigned OpIds.
  Status AsyncAppendReplicates(const ReplicateMsgs& replicates, const OpId& committed_op_id,
                                       RestartSafeCoarseTimePoint batch_mono_time,
                                       const StatusCallback& callback);

  // Blocks the current thread until all the entries in the log queue are flushed and fsynced (if
  // fsync of log entries is enabled).
  Status WaitUntilAllFlushed();

  // The closure submitted to allocation_pool_ to allocate a new segment.
  void SegmentAllocationTask();

  // Syncs all state and closes the log.
  Status Close();

  // Return true if there is any on-disk data for the given tablet.
  static bool HasOnDiskData(FsManager* fs_manager, const std::string& tablet_id);

  // Delete all WAL data from the log associated with this tablet.
  // REQUIRES: The Log must be closed.
  static Status DeleteOnDiskData(Env* env,
                                         const std::string& tablet_id,
                                         const std::string& wal_dir,
                                         const std::string& peer_uuid);

  // Returns a reader that is able to read through the previous segments. The reader pointer is
  // guaranteed to be live as long as the log itself is initialized and live.
  LogReader* GetLogReader() const;

  Status GetSegmentsSnapshot(SegmentSequence* segments) const;

  void SetMaxSegmentSizeForTests(uint64_t max_segment_size) {
    max_segment_size_ = max_segment_size;
  }

  void DisableSync() {
    sync_disabled_ = true;
  }

  // If we previous called DisableSync(), we should restore the default behavior and then call
  // Sync() which will perform the actual syncing if required.
  Status ReEnableSyncIfRequired() {
    sync_disabled_ = false;
    return Sync();
  }

  // Get ID of tablet.
  const std::string& tablet_id() const {
    return tablet_id_;
  }

  // Gets the last-used OpId written to the log.  If no entry has ever been written to the log,
  // returns (0, 0)
  yb::OpId GetLatestEntryOpId() const;

  int64_t GetMinReplicateIndex() const;

  // Runs the garbage collector on the set of previous segments. Segments that only refer to in-mem
  // state that has been flushed are candidates for garbage collection.
  //
  // 'min_op_idx' is the minimum operation index required to be retained.  If successful, num_gced
  // is set to the number of deleted log segments.
  //
  // This method is thread-safe.
  Status GC(int64_t min_op_idx, int* num_gced);

  // Computes the amount of bytes that would have been GC'd if Log::GC had been called.
  Status GetGCableDataSize(int64_t min_op_idx, int64_t* total_size) const;

  // Returns the file system location of the currently active WAL segment.
  const WritableLogSegment* ActiveSegmentForTests() const {
    return active_segment_.get();
  }

  // If active segment is not empty, forces the Log to allocate a new segment and roll over.
  // This can be used to make sure all entries appended up to this point are available in closed,
  // readable segments. Note that this assumes there is already a valid active_segment_.
  Status AllocateSegmentAndRollOver();

  // For a log created with CreateNewSegment::kFalse, this is used to finish log initialization by
  // allocating a new segment.
  Status EnsureInitialNewSegmentAllocated();

  // Returns the total size of the current segments, in bytes.
  // Returns 0 if the log is shut down.
  uint64_t OnDiskSize();

  // Set the schema for the _next_ log segment.
  //
  // This method is thread-safe.
  void SetSchemaForNextLogSegment(const Schema& schema, uint32_t version);

  void set_wal_retention_secs(uint32_t wal_retention_secs);

  uint32_t wal_retention_secs() const;

  // Waits until specified op id is added to log.
  // Returns current op id after waiting, which could be greater than or equal to specified op id.
  //
  // On timeout returns default constructed OpId.
  yb::OpId WaitForSafeOpIdToApply(const yb::OpId& op_id, MonoDelta duration = MonoDelta());

  // Return a readable segment with the given sequence number, or NotFound error if it
  // cannot be found (e.g. if it has already been GCed).
  Result<scoped_refptr<ReadableLogSegment>> GetSegmentBySequenceNumber(int64_t seq) const;

  void TEST_SetSleepDuration(const std::chrono::nanoseconds& duration) {
    sleep_duration_.store(duration, std::memory_order_release);
  }

  void TEST_SetAllOpIdsSafe(bool value) {
    all_op_ids_safe_ = value;
  }

  uint64_t active_segment_sequence_number() const;

  Status TEST_SubmitFuncToAppendToken(const std::function<void()>& func);

  // Returns the number of segments.
  size_t num_segments() const;

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  std::string wal_dir() const {
    return wal_dir_;
  }

  void set_cdc_min_replicated_index(int64_t cdc_min_replicated_index) {
    // TODO: check that the passed index is greater than the current index.
    cdc_min_replicated_index_.store(cdc_min_replicated_index, std::memory_order_release);
  }

  int64_t cdc_min_replicated_index() {
    return cdc_min_replicated_index_.load(std::memory_order_acquire);
  }

  // Copies log to a new dir. Expects dest_wal_dir to be absent.
  // If max_included_op_id is specified - only part of the log up to and including
  // max_included_op_id is copied.
  // Flushes necessary files and uses hard links where it is safe.
  Status CopyTo(const std::string& dest_wal_dir, OpId max_included_op_id = OpId());

  // Waits until all entries flushed, then reset last received op id to specified one.
  Status ResetLastSyncedEntryOpId(const OpId& op_id);

 private:
  friend class LogTest;
  friend class LogTestBase;

  FRIEND_TEST(LogTest, TestMultipleEntriesInABatch);
  FRIEND_TEST(LogTest, TestReadLogWithReplacedReplicates);
  FRIEND_TEST(LogTest, TestWriteAndReadToAndFromInProgressSegment);
  FRIEND_TEST(LogTest, TestLogMetrics);

  FRIEND_TEST(cdc::CDCServiceTestMaxRentionTime, TestLogRetentionByOpId_MaxRentionTime);
  FRIEND_TEST(cdc::CDCServiceTestMinSpace, TestLogRetentionByOpId_MinSpace);

  class Appender;

  // Log state.
  enum LogState {
    kLogInitialized,
    kLogWriting,
    kLogClosed
  };

  Log(LogOptions options,
      std::string wal_dir,
      std::string tablet_id,
      std::string peer_uuid,
      const Schema& schema,
      uint32_t schema_version,
      const scoped_refptr<MetricEntity>& table_metric_entity,
      const scoped_refptr<MetricEntity>& tablet_metric_entity,
      ThreadPool* append_thread_pool,
      ThreadPool* allocation_thread_pool,
      CreateNewSegment create_new_segment = CreateNewSegment::kTrue);

  Env* get_env() {
    return options_.env;
  }

  // Initializes a new one or continues an existing log.
  Status Init();

  // Make segments roll over. Note this assumes there was an existing valid active_segment_ we are
  // rolling over from.
  Status RollOver();

  // Writes the footer and closes the current segment.
  Status CloseCurrentSegment();

  // Sets 'out' to a newly created temporary file (see Env::NewTempWritableFile()) for a placeholder
  // segment. Sets 'result_path' to the fully qualified path to the unique filename created for the
  // segment.
  Status CreatePlaceholderSegment(const WritableFileOptions& opts,
                                          std::string* result_path,
                                          std::shared_ptr<WritableFile>* out);

  // Creates a new WAL segment on disk, writes the next_segment_header_ to disk as the header, and
  // sets active_segment_ to point to this new segment.
  Status SwitchToAllocatedSegment();

  // Preallocates the space for a new segment.
  Status PreAllocateNewSegment();

  // Returns the desired size for the next log segment to be created.
  uint64_t NextSegmentDesiredSize();

  // Writes serialized contents of 'entry' to the log. Called inside AppenderThread. If
  // 'caller_owns_operation' is true, then the 'operation' field of the entry will be released after
  // the entry is appended. If skip_wal_write is true, only update consensus metadata and LogIndex,
  // skip WAL write.
  //
  // TODO once Append() is removed, 'caller_owns_operation' and associated logic will no longer be
  // needed.
  Status DoAppend(
      LogEntryBatch* entry, bool caller_owns_operation = true, bool skip_wal_write = false);

  // Update footer_builder_ to reflect the log indexes seen in 'batch'.
  void UpdateFooterForBatch(LogEntryBatch* batch);

  // Update the LogIndex to include entries for the replicate messages found in 'batch'. The index
  // entry points to the offset 'start_offset' in the current log segment.
  Status UpdateIndexForBatch(const LogEntryBatch& batch);

  // Replaces the last "empty" segment in 'log_reader_', i.e. the one currently being written to, by
  // the same segment once properly closed.
  Status ReplaceSegmentInReaderUnlocked();

  Status Sync();

  // Helper method to get the segment sequence to GC based on the provided min_op_idx.
  Status GetSegmentsToGCUnlocked(int64_t min_op_idx, SegmentSequence* segments_to_gc) const;

  // Kick off an asynchronous task that pre-allocates a new log-segment, setting
  // 'allocation_status_'. To wait for the result of the task, use allocation_status_.Get().
  Status AsyncAllocateSegment();

  SegmentAllocationState allocation_state() {
    return allocation_state_.load(std::memory_order_acquire);
  }

  LogEntryBatch* ReserveMarker(LogEntryTypePB type);

  // Returns WritableFileOptions for a new segment writable file.
  WritableFileOptions GetNewSegmentWritableFileOptions();

  // See SegmentOpIdRelation comments.
  // Returns SegmentOpIdRelation::kOpIdAfterSegment if op_id is not valid or empty.
  // Note: this function can potentially read all entries from WAL segment that might a large amount
  // of data.
  Result<SegmentOpIdRelation> GetSegmentOpIdRelation(
      ReadableLogSegment* segment, const OpId& op_id);

  // Copies (can use hardlink as an optimization) log segment up to and including
  // max_included_op_id.
  // If max_included_op_id is not in this log segment - copies (using hardlink) the whole
  // segment.
  // Returns true if max_included_op_id is before or inside the segment and false otherwise.

  Result<bool> CopySegmentUpTo(
      ReadableLogSegment* segment, const std::string& dest_wal_dir,
      const OpId& max_included_op_id);

  LogOptions options_;

  // The dir path where the write-ahead log for this tablet is stored.
  std::string wal_dir_;

  // The ID of the tablet this log is dedicated to.
  std::string tablet_id_;

  // Peer this log is dedicated to.
  std::string peer_uuid_;

  // Lock to protect modifications to schema_ and schema_version_.
  mutable rw_spinlock schema_lock_;

  // The current schema of the tablet this log is dedicated to.
  std::unique_ptr<Schema> schema_;

  // The schema version
  uint32_t schema_version_;

  // The currently active segment being written.
  std::unique_ptr<WritableLogSegment> active_segment_;

  // The current (active) segment sequence number. Initialized in the Log constructor based on
  // LogOptions.
  std::atomic<uint64_t> active_segment_sequence_number_;

  // The writable file for the next allocated segment
  std::shared_ptr<WritableFile> next_segment_file_;

  // The path for the next allocated segment.
  std::string next_segment_path_;

  // Lock to protect mutations to log_state_ and other shared state variables.
  mutable percpu_rwlock state_lock_;

  LogState log_state_;

  // A reader for the previous segments that were not yet GC'd.
  std::unique_ptr<LogReader> reader_;

  // Index which translates between operation indexes and the position of the operation in the log.
  scoped_refptr<LogIndex> log_index_;

  // Lock for notification of last_synced_entry_op_id_ changes.
  mutable std::mutex last_synced_entry_op_id_mutex_;
  mutable std::condition_variable last_synced_entry_op_id_cond_;

  // The last known OpId for a REPLICATE message appended and synced to this log (any segment).
  // NOTE: this op is not necessarily durable unless gflag durable_wal_write is true.
  boost::atomic<yb::OpId> last_synced_entry_op_id_{yb::OpId()};

  // The last know OpId for a REPLICATE message appended to this log (any segment).
  // This variable is not accessed concurrently.
  yb::OpId last_appended_entry_op_id_;

  yb::OpId last_submitted_op_id_;

  // A footer being prepared for the current segment.  When the segment is closed, it will be
  // written.
  LogSegmentFooterPB footer_builder_;

  // The maximum segment size, in bytes.
  uint64_t max_segment_size_;

  // The maximum segment size we want for the current WAL segment, in bytes.  This value keeps
  // doubling (for each subsequent WAL segment) till it gets to max_segment_size_.
  uint64_t cur_max_segment_size_;

  // Appender manages a TaskStream writing to the log. We will use one taskstream per tablet.
  std::unique_ptr<Appender> appender_;

  // A thread pool for asynchronously pre-allocating new log segments.
  std::unique_ptr<ThreadPoolToken> allocation_token_;

  // If true, sync on all appends.
  bool durable_wal_write_;

  // If non-zero, sync every interval of time.
  MonoDelta interval_durable_wal_write_;

  // If non-zero, sync if more than given amount of data to sync.
  int32_t bytes_durable_wal_write_mb_;

  // Keeps track of oldest entry which needs to be synced.
  MonoTime periodic_sync_earliest_unsync_entry_time_ = MonoTime::kMin;

  // For periodic sync, indicates if there are entries to be sync'ed.
  std::atomic<bool> periodic_sync_needed_ = {false};

  // For periodic sync, indicates number of bytes which need to be sync'ed.
  size_t periodic_sync_unsynced_bytes_ = 0;

  // If true, ignore the 'durable_wal_write_' flags above.  This is used to disable fsync during
  // bootstrap.
  bool sync_disabled_;

  // The status of the most recent log-allocation action.
  Promise<Status> allocation_status_;

  std::atomic<SegmentAllocationState> allocation_state_;

  scoped_refptr<MetricEntity> table_metric_entity_;
  scoped_refptr<MetricEntity> tablet_metric_entity_;
  std::unique_ptr<LogMetrics> metrics_;

  // The cached on-disk size of the log, used to track its size even if it has been closed.
  std::atomic<uint64_t> on_disk_size_;

  // Listener that will be invoked after new entry was appended to the log.
  std::function<void()> post_append_listener_;

  // Used in tests delay writing log entries.
  std::atomic<std::chrono::nanoseconds> sleep_duration_{std::chrono::nanoseconds(0)};

  // Used in tests to declare all operations as safe.
  bool all_op_ids_safe_ = false;

  const std::string log_prefix_;

  std::atomic<uint32_t> wal_retention_secs_{0};

  // Minimum replicate index for the current log being written. Used for CDC read initialization.
  std::atomic<int64_t> min_replicate_index_{-1};

  // The current replicated index that CDC has read.  Used for CDC read cache optimization.
  std::atomic<int64_t> cdc_min_replicated_index_{std::numeric_limits<int64_t>::max()};

  std::mutex log_copy_mutex_;

  // Used by GetSegmentsToGCUnlocked() as an anchor.
  int64_t log_copy_min_index_ GUARDED_BY(state_lock_) = std::numeric_limits<int64_t>::max();

  CreateNewSegment create_new_segment_at_start_;

  DISALLOW_COPY_AND_ASSIGN(Log);
};

}  // namespace log
}  // namespace yb
#endif /* YB_CONSENSUS_LOG_H_ */
