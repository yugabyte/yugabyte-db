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
#ifndef YB_CONSENSUS_LOG_CACHE_H
#define YB_CONSENSUS_LOG_CACHE_H

#include <pthread.h>
#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/log_fwd.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_types.pb.h"

#include "yb/gutil/macros.h"

#include "yb/util/metrics_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/mutex.h"
#include "yb/util/opid.h"
#include "yb/util/trace.h"
#include "yb/util/restart_safe_clock.h"
#include "yb/util/status_callback.h"

YB_STRONGLY_TYPED_BOOL(HaveMoreMessages);

namespace yb {

class MetricEntity;
class MemTracker;
class OpIdPB;

namespace consensus {

class ReplicateMsg;

struct ReadOpsResult {
  ReplicateMsgs messages;
  yb::OpId preceding_op;
  yb::SchemaPB header_schema;
  uint32_t header_schema_version;
  HaveMoreMessages have_more_messages = HaveMoreMessages::kFalse;
  int64_t read_from_disk_size = 0;
};

// Write-through cache for the log.
//
// This stores a set of log messages by their index. New operations can be appended to the end as
// they are written to the log. Readers fetch entries that were explicitly appended, or they can
// fetch older entries which are asynchronously fetched from the disk.
class LogCache {
 public:
  LogCache(const scoped_refptr<MetricEntity>& metric_entity,
           const log::LogPtr& log,
           const std::shared_ptr<MemTracker>& server_tracker,
           const std::string& local_uuid,
           const std::string& tablet_id);
  ~LogCache();

  static std::shared_ptr<MemTracker> GetServerMemTracker(
      const std::shared_ptr<MemTracker>& server_tracker);

  // Initialize the cache.
  //
  // 'preceding_op' is the current latest op. The next AppendOperation() call must follow this op.
  //
  // Requires that the cache is empty.
  void Init(const OpIdPB& preceding_op);

  // Read operations from the log, following 'after_op_index'.
  // If such an op exists in the log, an OK result will always include at least one operation.
  //
  // The result will be limited such that the total ByteSize() of the returned ops is less than
  // max_size_bytes, unless that would result in an empty result, in which case exactly one op is
  // returned.
  //
  // The OpId which precedes the returned ops is returned in *preceding_op.  The index of this OpId
  // will match 'after_op_index'.
  //
  // If the ops being requested are not available in the log, this will synchronously read these ops
  // from disk. Therefore, this function may take a substantial amount of time and should not be
  // called with important locks held, etc.
  Result<ReadOpsResult> ReadOps(int64_t after_op_index, size_t max_size_bytes);

  // Same as above but also includes a 'to_op_index' parameter which will be used to limit results
  // until 'to_op_index' (inclusive).
  //
  // If 'to_op_index' is 0, then all operations after 'after_op_index' will be included.
  Result<ReadOpsResult> ReadOps(int64_t after_op_index,
                                int64_t to_op_index,
                                size_t max_size_bytes,
                                CoarseTimePoint deadline = CoarseTimePoint::max());

  // Append the operations into the log and the cache.  When the messages have completed writing
  // into the on-disk log, fires 'callback'.
  //
  // If the cache memory limit is exceeded, the entries may no longer be in the cache when the
  // callback fires.
  //
  // Returns non-OK if the Log append itself fails.
  Status AppendOperations(const ReplicateMsgs& msgs, const yb::OpId& committed_op_id,
                                  RestartSafeCoarseTimePoint batch_mono_time,
                                  const StatusCallback& callback);

  // Return true if an operation with the given index has been written through the cache. The
  // operation may not necessarily be durable yet -- it could still be en route to the log.
  bool HasOpBeenWritten(int64_t log_index) const;

  // Evict any operations with op index <= 'index'.
  size_t EvictThroughOp(
      int64_t index, int64_t bytes_to_evict = std::numeric_limits<int64_t>::max());

  // Return the number of bytes of memory currently in use by the cache.
  int64_t BytesUsed() const;

  int64_t num_cached_ops() const;

  int64_t earliest_op_index() const;

  // Dump the current contents of the cache to the log.
  void DumpToLog() const;

  // Dumps the contents of the cache to the provided string vector.
  void DumpToStrings(std::vector<std::string>* lines) const;

  void DumpToHtml(std::ostream& out) const;

  std::string StatsString() const;

  std::string ToString() const;

  // Look up the OpId for the given operation index.  If it is not in the cache, this consults the
  // on-disk log index and thus may take a non-trivial amount of time due to IO.
  //
  // Returns "Incomplete" if the op has not yet been written.
  // Returns "NotFound" if the op has been GCed.
  // Returns another bad Status if the log index fails to load (eg. due to an IO error).
  Result<yb::OpId> LookupOpId(int64_t op_index) const;

  // Start memory tracking of following operations in case they are still present in cache.
  void TrackOperationsMemory(const OpIds& op_ids);

  Status FlushIndex();

  Result<OpId> TEST_GetLastOpIdWithType(int64_t max_allowed_index, OperationType op_type);

 private:
  FRIEND_TEST(LogCacheTest, TestAppendAndGetMessages);
  FRIEND_TEST(LogCacheTest, TestGlobalMemoryLimitMB);
  FRIEND_TEST(LogCacheTest, TestGlobalMemoryLimitPercentage);
  FRIEND_TEST(LogCacheTest, TestReplaceMessages);
  friend class LogCacheTest;

  // An entry in the cache.
  struct CacheEntry {
    ReplicateMsgPtr msg;
    // The cached value of msg->SpaceUsedLong(). This method is expensive
    // to compute, so we compute it only once upon insertion.
    int64_t mem_usage = 0;

    // Did we start memory tracking for this entry.
    bool tracked = false;
  };

  // Try to evict the oldest operations from the queue, stopping either when
  // 'bytes_to_evict' bytes have been evicted, or the op with index
  // 'stop_after_index' has been evicted, whichever comes first.
  size_t EvictSomeUnlocked(int64_t stop_after_index, int64_t bytes_to_evict);

  // Update metrics and MemTracker to account for the removal of the
  // given message.
  void AccountForMessageRemovalUnlocked(const CacheEntry& entry);

  // Return a string with stats
  std::string StatsStringUnlocked() const;

  std::string ToStringUnlocked() const;

  std::string LogPrefixUnlocked() const;

  void LogCallback(int64_t last_idx_in_batch,
                   yb::TracePtr trace,
                   const StatusCallback& user_callback,
                   const Status& log_status);

  struct PrepareAppendResult {
    // Mem required to store provided operations.
    int64_t mem_required = 0;
    // Last idx in batch of provided operations.
    int64_t last_idx_in_batch = -1;
  };

  PrepareAppendResult PrepareAppendOperations(const ReplicateMsgs& msgs);

  log::LogPtr const log_;

  // The UUID of the local peer.
  const std::string local_uuid_;

  // The id of the tablet.
  const std::string tablet_id_;

  mutable simple_spinlock lock_;

  // An ordered map that serves as the buffer for the cached messages.
  // Maps from log index -> ReplicateMsg
  // An ordered map that serves as the buffer for the cached messages.  Maps from log index ->
  // CacheEntry
  typedef std::map<int64_t, CacheEntry> MessageCache;
  MessageCache cache_;

  // The next log index to append. Each append operation must either start with this log index, or
  // go backward (but never skip forward).
  int64_t next_sequential_op_index_;

  // Any operation with an index >= min_pinned_op_ may not be evicted from the cache. This is used
  // to prevent ops from being evicted until they successfully have been appended to the underlying
  // log.  Protected by lock_.
  int64_t min_pinned_op_index_;

  // Pointer to a parent memtracker for all log caches. This exists to compute server-wide cache
  // size and enforce a server-wide memory limit.  When the first instance of a log cache is
  // created, a new entry is added to MemTracker's static map; subsequent entries merely increment
  // the refcount, so that the parent tracker can be deleted if all log caches are deleted (e.g., if
  // all tablets are deleted from a server, or if the server is shutdown).
  std::shared_ptr<MemTracker> parent_tracker_;

  // A MemTracker for this instance.
  std::shared_ptr<MemTracker> tracker_;

  struct Metrics {
    explicit Metrics(const scoped_refptr<MetricEntity>& metric_entity);

    // Keeps track of the total number of operations in the cache.
    scoped_refptr<AtomicGauge<int64_t>> num_ops;

    // Keeps track of the memory consumed by the cache, in bytes.
    scoped_refptr<AtomicGauge<int64_t>> size;

    scoped_refptr<Counter> disk_reads;
  };
  Metrics metrics_;

  DISALLOW_COPY_AND_ASSIGN(LogCache);
};

} // namespace consensus
} // namespace yb
#endif /* YB_CONSENSUS_LOG_CACHE_H */
