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

#ifndef KUDU_CONSENSUS_LOG_H_
#define KUDU_CONSENSUS_LOG_H_

#include <boost/thread/shared_mutex.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/consensus/log_util.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/consensus/ref_counted_replicate.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/spinlock.h"
#include "kudu/util/async_util.h"
#include "kudu/util/blocking_queue.h"
#include "kudu/util/locks.h"
#include "kudu/util/promise.h"
#include "kudu/util/status.h"

namespace kudu {

class FsManager;
class MetricEntity;
class ThreadPool;

namespace log {

struct LogEntryBatchLogicalSize;
struct LogMetrics;
class LogEntryBatch;
class LogIndex;
class LogReader;

typedef BlockingQueue<LogEntryBatch*, LogEntryBatchLogicalSize> LogEntryBatchQueue;

// Log interface, inspired by Raft's (logcabin) Log. Provides durability to
// Kudu as a normal Write Ahead Log and also plays the role of persistent
// storage for the consensus state machine.
//
// Note: This class is not thread safe, the caller is expected to synchronize
// Log::Reserve() and Log::Append() calls.
//
// Log uses group commit to improve write throughput and latency
// without compromising ordering and durability guarantees.
//
// To add operations to the log, the caller must obtain the lock and
// call Reserve() with the collection of operations to be added. Then,
// the caller may release the lock and call AsyncAppend(). Reserve()
// reserves a slot on a queue for the log entry; AsyncAppend()
// indicates that the entry in the slot is safe to write to disk and
// adds a callback that will be invoked once the entry is written and
// synchronized to disk.
//
// For sample usage see local_consensus.cc and mt-log-test.cc
//
// Methods on this class are _not_ thread-safe and must be externally
// synchronized unless otherwise noted.
//
// Note: The Log needs to be Close()d before any log-writing class is
// destroyed, otherwise the Log might hold references to these classes
// to execute the callbacks after each write.
class Log : public RefCountedThreadSafe<Log> {
 public:
  class LogFaultHooks;

  static const Status kLogShutdownStatus;
  static const uint64_t kInitialLogSegmentSequenceNumber;

  // Opens or continues a log and sets 'log' to the newly built Log.
  // After a successful Open() the Log is ready to receive entries.
  static Status Open(const LogOptions &options,
                     FsManager *fs_manager,
                     const std::string& tablet_id,
                     const Schema& schema,
                     uint32_t schema_version,
                     const scoped_refptr<MetricEntity>& metric_entity,
                     scoped_refptr<Log> *log);

  ~Log();

  // Reserves a spot in the log's queue for 'entry_batch'.
  //
  // 'reserved_entry' is initialized by this method and any resources
  // associated with it will be released in AsyncAppend().  In order
  // to ensure correct ordering of operations across multiple threads,
  // calls to this method must be externally synchronized.
  //
  // WARNING: the caller _must_ call AsyncAppend() or else the log
  // will "stall" and will never be able to make forward progress.
  Status Reserve(LogEntryTypePB type,
                 gscoped_ptr<LogEntryBatchPB> entry_batch,
                 LogEntryBatch** reserved_entry);

  // Asynchronously appends 'entry' to the log. Once the append
  // completes and is synced, 'callback' will be invoked.
  Status AsyncAppend(LogEntryBatch* entry,
                     const StatusCallback& callback);

  // Synchronously append a new entry to the log.
  // Log does not take ownership of the passed 'entry'.
  // TODO get rid of this method, transition to the asynchronous API
  Status Append(LogEntryPB* entry);

  // Append the given set of replicate messages, asynchronously.
  // This requires that the replicates have already been assigned OpIds.
  Status AsyncAppendReplicates(const vector<consensus::ReplicateRefPtr>& replicates,
                               const StatusCallback& callback);

  // Append the given commit message, asynchronously.
  //
  // Returns a bad status if the log is already shut down.
  Status AsyncAppendCommit(gscoped_ptr<consensus::CommitMsg> commit_msg,
                           const StatusCallback& callback);


  // Blocks the current thread until all the entries in the log queue
  // are flushed and fsynced (if fsync of log entries is enabled).
  Status WaitUntilAllFlushed();

  // Kick off an asynchronous task that pre-allocates a new
  // log-segment, setting 'allocation_status_'. To wait for the
  // result of the task, use allocation_status_.Get().
  Status AsyncAllocateSegment();

  // The closure submitted to allocation_pool_ to allocate a new segment.
  void SegmentAllocationTask();

  // Syncs all state and closes the log.
  Status Close();

  // Delete all WAL data from the log associated with this tablet.
  // REQUIRES: The Log must be closed.
  static Status DeleteOnDiskData(FsManager* fs_manager, const std::string& tablet_id);

  // Returns a reader that is able to read through the previous
  // segments. The reader pointer is guaranteed to be live as long
  // as the log itself is initialized and live.
  LogReader* GetLogReader() const;

  void SetMaxSegmentSizeForTests(uint64_t max_segment_size) {
    max_segment_size_ = max_segment_size;
  }

  void DisableAsyncAllocationForTests() {
    options_.async_preallocate_segments = false;
  }

  void DisableSync() {
    sync_disabled_ = true;
  }

  // If we previous called DisableSync(), we should restore the
  // default behavior and then call Sync() which will perform the
  // actual syncing if required.
  Status ReEnableSyncIfRequired() {
    sync_disabled_ = false;
    return Sync();
  }

  // Get ID of tablet.
  const std::string& tablet_id() const {
    return tablet_id_;
  }

  // Gets the last-used OpId written to the log.
  // If no entry has ever been written to the log, returns (0, 0)
  void GetLatestEntryOpId(consensus::OpId* op_id) const;

  // Runs the garbage collector on the set of previous segments. Segments that
  // only refer to in-mem state that has been flushed are candidates for
  // garbage collection.
  //
  // 'min_op_idx' is the minimum operation index required to be retained.
  // If successful, num_gced is set to the number of deleted log segments.
  //
  // This method is thread-safe.
  Status GC(int64_t min_op_idx, int* num_gced);

  // Computes the amount of bytes that would have been GC'd if Log::GC had been called.
  void GetGCableDataSize(int64_t min_op_idx, int64_t* total_size) const;

  // Returns a map of log index -> segment size, of all the segments that currently cannot be GCed
  // because in-memory structures have anchors in them.
  //
  // 'min_op_idx' is the minimum operation index to start looking from, meaning that we skip the
  // segment that contains it and then start recording segments.
  void GetMaxIndexesToSegmentSizeMap(int64_t min_op_idx,
                                     std::map<int64_t, int64_t>* max_idx_to_segment_size) const;

  // Returns the file system location of the currently active WAL segment.
  const std::string& ActiveSegmentPathForTests() const {
    return active_segment_->path();
  }

  // Forces the Log to allocate a new segment and roll over.
  // This can be used to make sure all entries appended up to this point are
  // available in closed, readable segments.
  Status AllocateSegmentAndRollOver();

  // Returns this Log's FsManager.
  FsManager* GetFsManager();

  void SetLogFaultHooksForTests(const std::shared_ptr<LogFaultHooks> &hooks) {
    log_hooks_ = hooks;
  }

  // Set the schema for the _next_ log segment.
  //
  // This method is thread-safe.
  void SetSchemaForNextLogSegment(const Schema& schema, uint32_t version);

 private:
  friend class LogTest;
  friend class LogTestBase;
  FRIEND_TEST(LogTest, TestMultipleEntriesInABatch);
  FRIEND_TEST(LogTest, TestReadLogWithReplacedReplicates);
  FRIEND_TEST(LogTest, TestWriteAndReadToAndFromInProgressSegment);

  class AppendThread;

  // Log state.
  enum LogState {
    kLogInitialized,
    kLogWriting,
    kLogClosed
  };

  // State of segment (pre-) allocation.
  enum SegmentAllocationState {
    kAllocationNotStarted, // No segment allocation requested
    kAllocationInProgress, // Next segment allocation started
    kAllocationFinished // Next segment ready
  };

  Log(LogOptions options, FsManager* fs_manager, std::string log_path,
      std::string tablet_id, const Schema& schema, uint32_t schema_version,
      const scoped_refptr<MetricEntity>& metric_entity);

  // Initializes a new one or continues an existing log.
  Status Init();

  // Make segments roll over.
  Status RollOver();

  // Writes the footer and closes the current segment.
  Status CloseCurrentSegment();

  // Sets 'out' to a newly created temporary file (see
  // Env::NewTempWritableFile()) for a placeholder segment. Sets
  // 'result_path' to the fully qualified path to the unique filename
  // created for the segment.
  Status CreatePlaceholderSegment(const WritableFileOptions& opts,
                                  std::string* result_path,
                                  std::shared_ptr<WritableFile>* out);

  // Creates a new WAL segment on disk, writes the next_segment_header_ to
  // disk as the header, and sets active_segment_ to point to this new segment.
  Status SwitchToAllocatedSegment();

  // Preallocates the space for a new segment.
  Status PreAllocateNewSegment();

  // Writes serialized contents of 'entry' to the log. Called inside
  // AppenderThread. If 'caller_owns_operation' is true, then the
  // 'operation' field of the entry will be released after the entry
  // is appended.
  // TODO once Append() is removed, 'caller_owns_operation' and
  // associated logic will no longer be needed.
  Status DoAppend(LogEntryBatch* entry, bool caller_owns_operation = true);

  // Update footer_builder_ to reflect the log indexes seen in 'batch'.
  void UpdateFooterForBatch(LogEntryBatch* batch);

  // Update the LogIndex to include entries for the replicate messages found in
  // 'batch'. The index entry points to the offset 'start_offset' in the current
  // log segment.
  Status UpdateIndexForBatch(const LogEntryBatch& batch,
                             int64_t start_offset);

  // Replaces the last "empty" segment in 'log_reader_', i.e. the one currently
  // being written to, by the same segment once properly closed.
  Status ReplaceSegmentInReaderUnlocked();

  Status Sync();

  // Helper method to get the segment sequence to GC based on the provided min_op_idx.
  Status GetSegmentsToGCUnlocked(int64_t min_op_idx, SegmentSequence* segments_to_gc) const;

  LogEntryBatchQueue* entry_queue() {
    return &entry_batch_queue_;
  }

  const SegmentAllocationState allocation_state() {
    boost::shared_lock<boost::shared_mutex> shared_lock(allocation_lock_);
    return allocation_state_;
  }

  LogOptions options_;
  FsManager *fs_manager_;
  std::string log_dir_;

  // The ID of the tablet this log is dedicated to.
  std::string tablet_id_;

  // Lock to protect modifications to schema_ and schema_version_.
  mutable rw_spinlock schema_lock_;

  // The current schema of the tablet this log is dedicated to.
  Schema schema_;
  // The schema version
  uint32_t schema_version_;

  // The currently active segment being written.
  gscoped_ptr<WritableLogSegment> active_segment_;

  // The current (active) segment sequence number.
  uint64_t active_segment_sequence_number_;

  // The writable file for the next allocated segment
  std::shared_ptr<WritableFile> next_segment_file_;

  // The path for the next allocated segment.
  std::string next_segment_path_;

  // Lock to protect mutations to log_state_ and other shared state variables.
  mutable percpu_rwlock state_lock_;

  LogState log_state_;

  // A reader for the previous segments that were not yet GC'd.
  gscoped_ptr<LogReader> reader_;

  // Index which translates between operation indexes and the position
  // of the operation in the log.
  scoped_refptr<LogIndex> log_index_;

  // Lock to protect last_entry_op_id_, which is constantly written but
  // read occasionally by things like consensus and log GC.
  mutable rw_spinlock last_entry_op_id_lock_;

  // The last known OpId for a REPLICATE message appended to this log
  // (any segment). NOTE: this op is not necessarily durable.
  consensus::OpId last_entry_op_id_;

  // A footer being prepared for the current segment.
  // When the segment is closed, it will be written.
  LogSegmentFooterPB footer_builder_;

  // The maximum segment size, in bytes.
  uint64_t max_segment_size_;

  // The queue used to communicate between the thread calling
  // Reserve() and the Log Appender thread
  LogEntryBatchQueue entry_batch_queue_;

  // Thread writing to the log
  gscoped_ptr<AppendThread> append_thread_;

  gscoped_ptr<ThreadPool> allocation_pool_;

  // If true, sync on all appends.
  bool force_sync_all_;

  // If true, ignore the 'force_sync_all_' flag above.
  // This is used to disable fsync during bootstrap.
  bool sync_disabled_;

  // The status of the most recent log-allocation action.
  Promise<Status> allocation_status_;

  // Read-write lock to protect 'allocation_state_'.
  mutable boost::shared_mutex allocation_lock_;
  SegmentAllocationState allocation_state_;

  scoped_refptr<MetricEntity> metric_entity_;
  gscoped_ptr<LogMetrics> metrics_;

  std::shared_ptr<LogFaultHooks> log_hooks_;

  DISALLOW_COPY_AND_ASSIGN(Log);
};

// This class represents a batch of operations to be written and
// synced to the log. It is opaque to the user and is managed by the
// Log class.
// A single batch must have only one type of entries in it (eg only
// REPLICATEs or only COMMITs).
class LogEntryBatch {
 public:
  ~LogEntryBatch();

 private:
  friend class Log;
  friend struct LogEntryBatchLogicalSize;
  friend class MultiThreadedLogTest;

  LogEntryBatch(LogEntryTypePB type,
                gscoped_ptr<LogEntryBatchPB> entry_batch_pb, size_t count);

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

  // Wait (currently, by spinning on ready_lock_) until ready.
  void WaitForReady();

  // Returns a Slice representing the serialized contents of the
  // entry.
  Slice data() const {
    DCHECK_EQ(state_, kEntryReady);
    return Slice(buffer_);
  }

  size_t count() const { return count_; }

  // Returns the total size in bytes of the object.
  size_t total_size_bytes() const {
    return total_size_bytes_;
  }

  // The highest OpId of a REPLICATE message in this batch.
  // Requires that this be a REPLICATE batch.
  consensus::OpId MaxReplicateOpId() const {
    DCHECK_EQ(REPLICATE, type_);
    int idx = entry_batch_pb_->entry_size() - 1;
    DCHECK(entry_batch_pb_->entry(idx).replicate().IsInitialized());
    return entry_batch_pb_->entry(idx).replicate().id();
  }

  void SetReplicates(const vector<consensus::ReplicateRefPtr>& replicates) {
    replicates_ = replicates;
  }

  // The type of entries in this batch.
  const LogEntryTypePB type_;

  // Contents of the log entries that will be written to disk.
  gscoped_ptr<LogEntryBatchPB> entry_batch_pb_;

   // Total size in bytes of all entries
  const uint32_t total_size_bytes_;

  // Number of entries in 'entry_batch_pb_'
  const size_t count_;

  // The vector of refcounted replicates.
  // Used only when type is REPLICATE, this makes sure there's at
  // least a reference to each replicate message until we're finished
  // appending.
  vector<consensus::ReplicateRefPtr> replicates_;

  // Callback to be invoked upon the entries being written and
  // synced to disk.
  StatusCallback callback_;

  // Used to coordinate the synchronizer thread and the caller
  // thread: this lock starts out locked, and is unlocked by the
  // caller thread (i.e., inside AppendThread()) once the entry is
  // fully initialized (once the callback is set and data is
  // serialized)
  base::SpinLock ready_lock_;

  // Buffer to which 'phys_entries_' are serialized by call to
  // 'Serialize()'
  faststring buffer_;

  enum LogEntryState {
    kEntryInitialized,
    kEntryReserved,
    kEntrySerialized,
    kEntryReady,
    kEntryFailedToAppend
  };
  LogEntryState state_;

  DISALLOW_COPY_AND_ASSIGN(LogEntryBatch);
};

// Used by 'Log::queue_' to determine logical size of a LogEntryBatch.
struct LogEntryBatchLogicalSize {
  static size_t logical_size(const LogEntryBatch* batch) {
    return batch->total_size_bytes();
  }
};

class Log::LogFaultHooks {
 public:

  // Executed immediately before returning from Log::Sync() at *ALL*
  // times.
  virtual Status PostSync() { return Status::OK(); }

  // Iff fsync is enabled, executed immediately after call to fsync.
  virtual Status PostSyncIfFsyncEnabled() { return Status::OK(); }

  // Emulate a slow disk where the filesystem has decided to synchronously
  // flush a full buffer.
  virtual Status PostAppend() { return Status::OK(); }

  virtual Status PreClose() { return Status::OK(); }
  virtual Status PostClose() { return Status::OK(); }

  virtual ~LogFaultHooks() {}
};

}  // namespace log
}  // namespace kudu
#endif /* KUDU_CONSENSUS_LOG_H_ */
