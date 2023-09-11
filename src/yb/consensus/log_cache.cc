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

#include "yb/consensus/log_cache.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/opid_util.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/human_readable.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"

using std::vector;
using std::string;

using namespace std::literals;

DEFINE_UNKNOWN_int32(log_cache_size_limit_mb, 128,
             "The total per-tablet size of consensus entries which may be kept in memory. "
             "The log cache attempts to keep all entries which have not yet been replicated "
             "to all followers in memory, but if the total size of those entries exceeds "
             "this limit within an individual tablet, the oldest will be evicted.");
TAG_FLAG(log_cache_size_limit_mb, advanced);

DEFINE_UNKNOWN_int32(global_log_cache_size_limit_mb, 1024,
             "Server-wide version of 'log_cache_size_limit_mb'. The total memory used for "
             "caching log entries across all tablets is kept under this threshold.");
TAG_FLAG(global_log_cache_size_limit_mb, advanced);

DEFINE_UNKNOWN_int32(global_log_cache_size_limit_percentage, 5,
             "The maximum percentage of root process memory that can be used for caching log "
             "entries across all tablets. Default is 5.");
TAG_FLAG(global_log_cache_size_limit_percentage, advanced);

DEFINE_test_flag(bool, log_cache_skip_eviction, false,
                 "Don't evict log entries in tests.");

using strings::Substitute;

METRIC_DEFINE_gauge_int64(tablet, log_cache_num_ops, "Log Cache Operation Count",
                          yb::MetricUnit::kOperations,
                          "Number of operations in the log cache.");
METRIC_DEFINE_gauge_int64(tablet, log_cache_size, "Log Cache Memory Usage",
                          yb::MetricUnit::kBytes,
                          "Amount of memory in use for caching the local log.");
METRIC_DEFINE_counter(tablet, log_cache_disk_reads, "Log Cache Disk Reads",
                      yb::MetricUnit::kEntries,
                      "Amount of operations read from disk.");

DECLARE_bool(get_changes_honor_deadline);

namespace yb {
namespace consensus {

namespace {

const std::string kParentMemTrackerId = "LogCache"s;

}

typedef vector<const ReplicateMsg*>::const_iterator MsgIter;

LogCache::LogCache(const scoped_refptr<MetricEntity>& metric_entity,
                   const log::LogPtr& log,
                   const MemTrackerPtr& server_tracker,
                   const string& local_uuid,
                   const string& tablet_id)
  : log_(log),
    local_uuid_(local_uuid),
    tablet_id_(tablet_id),
    log_prefix_(MakeTabletLogPrefix(tablet_id_, local_uuid_)),
    next_sequential_op_index_(0),
    min_pinned_op_index_(0),
    num_batches_overwritten_cache_(0),
    metrics_(metric_entity) {

  const int64_t max_ops_size_bytes = FLAGS_log_cache_size_limit_mb * 1_MB;

  // Set up (or reuse) a tracker with the global limit. It is parented directly to the root tracker
  // so that it's always global.
  parent_tracker_ = GetServerMemTracker(server_tracker);

  // And create a child tracker with the per-tablet limit.
  tracker_ = MemTracker::CreateTracker(
      max_ops_size_bytes, Format("$0-$1", kParentMemTrackerId, tablet_id),
          /* metric_name */ "PerLogCache", parent_tracker_, AddToParent::kTrue,
              CreateMetrics::kFalse);
  tracker_->SetMetricEntity(metric_entity);

  // Put a fake message at index 0, since this simplifies a lot of our code paths elsewhere.
  auto zero_op = rpc::MakeSharedMessage<LWReplicateMsg>();
  *zero_op->mutable_id() = MinimumOpId();
  InsertOrDie(&cache_, 0, { zero_op, zero_op->SpaceUsedLong() });
}

MemTrackerPtr LogCache::GetServerMemTracker(const MemTrackerPtr& server_tracker) {
  CHECK(FLAGS_global_log_cache_size_limit_percentage > 0 &&
        FLAGS_global_log_cache_size_limit_percentage <= 100)
    << Substitute("Flag FLAGS_global_log_cache_size_limit_percentage must be between 0 and 100. ",
                  "Current value: $0",
                  FLAGS_global_log_cache_size_limit_percentage);

  int64_t global_max_ops_size_bytes = FLAGS_global_log_cache_size_limit_mb * 1_MB;
  int64_t root_mem_limit = MemTracker::GetRootTracker()->limit();
  global_max_ops_size_bytes = std::min(
      global_max_ops_size_bytes,
      root_mem_limit * FLAGS_global_log_cache_size_limit_percentage / 100);
  return MemTracker::FindOrCreateTracker(
      global_max_ops_size_bytes, kParentMemTrackerId, server_tracker);
}

LogCache::~LogCache() {
  tracker_->Release(tracker_->consumption());
  {
    std::lock_guard l(lock_);
    cache_.clear();
  }

  tracker_->UnregisterFromParent();
}

void LogCache::Init(const OpIdPB& preceding_op) {
  std::lock_guard l(lock_);
  CHECK_EQ(cache_.size(), 1) << "Cache should have only our special '0' op";
  next_sequential_op_index_ = preceding_op.index() + 1;
  min_pinned_op_index_ = next_sequential_op_index_;
}

LogCache::PrepareAppendResult LogCache::PrepareAppendOperations(const ReplicateMsgs& msgs) {
  // SpaceUsed is relatively expensive, so do calculations outside the lock
  PrepareAppendResult result;
  std::vector<CacheEntry> entries_to_insert;
  entries_to_insert.reserve(msgs.size());
  for (const auto& msg : msgs) {
    CacheEntry e = { msg, msg->SpaceUsedLong() };
    result.mem_required += e.mem_usage;
    entries_to_insert.emplace_back(std::move(e));
  }

  int64_t first_idx_in_batch = msgs.front()->id().index();
  result.last_idx_in_batch = msgs.back()->id().index();

  // Capture the evicted messages and release the memory outside of lock.
  ReplicateMsgVector evicted_messages;

  std::lock_guard lock(lock_);
  // If we're not appending a consecutive op we're likely overwriting and need to replace operations
  // in the cache.
  if (first_idx_in_batch != next_sequential_op_index_) {
    // If the index is not consecutive then it must be lower than or equal to the last index, i.e.
    // we're overwriting.
    CHECK_LT(first_idx_in_batch, next_sequential_op_index_);

    // Now remove the overwritten operations.
    for (int64_t i = first_idx_in_batch; i < next_sequential_op_index_; ++i) {
      auto it = cache_.find(i);
      if (it != cache_.end()) {
        AccountForMessageRemovalUnlocked(it->second);
        evicted_messages.push_back(it->second.msg);
        cache_.erase(it);
      }
    }

    if (min_pinned_op_index_ < next_sequential_op_index_) {
      // There are ops in progress of flushing, increment the counter to avoid ops in the
      // current batch evicted before being flushed.
      result.overwritten_cache = true;
      num_batches_overwritten_cache_++;
    }

    // Set back min_pinned_op_index_ in case the newly inserted ops are evicted.
    if (first_idx_in_batch < min_pinned_op_index_) {
      LOG_WITH_PREFIX_UNLOCKED(INFO) << Format(
          "Updating min_pinned_op_index_ from $0 to $1", min_pinned_op_index_, first_idx_in_batch);
      min_pinned_op_index_ = first_idx_in_batch;
    }
  }

  for (auto& e : entries_to_insert) {
    auto index = e.msg->id().index();
    EmplaceOrDie(&cache_, index, std::move(e));
  }

  next_sequential_op_index_ = result.last_idx_in_batch + 1;

  return result;
}

Status LogCache::AppendOperations(const ReplicateMsgs& msgs, const OpId& committed_op_id,
                                  RestartSafeCoarseTimePoint batch_mono_time,
                                  const StatusCallback& callback) {
  PrepareAppendResult prepare_result;
  if (!msgs.empty()) {
    prepare_result = PrepareAppendOperations(msgs);
  }

  Status log_status = log_->AsyncAppendReplicates(
    msgs, committed_op_id, batch_mono_time,
    Bind(&LogCache::LogCallback,
         Unretained(this),
         prepare_result.overwritten_cache,
         prepare_result.last_idx_in_batch,
         callback));

  if (!log_status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Couldn't append to log: " << log_status;
    return log_status;
  }

  metrics_.size->IncrementBy(prepare_result.mem_required);
  metrics_.num_ops->IncrementBy(msgs.size());

  return Status::OK();
}

void LogCache::LogCallback(bool overwritten_cache,
                           int64_t last_idx_in_batch,
                           const StatusCallback& user_callback,
                           const Status& log_status) {
  if (overwritten_cache || log_status.ok()) {
    std::lock_guard l(lock_);
    if (overwritten_cache) {
      CHECK_GT(num_batches_overwritten_cache_, 0)
          << "num_batches_overwritten_cache_ is expected to be greater than 0, but actually is "
          << num_batches_overwritten_cache_;
      num_batches_overwritten_cache_--;
    }
    if (log_status.ok() && num_batches_overwritten_cache_ == 0 &&
        min_pinned_op_index_ <= last_idx_in_batch) {
      VLOG_WITH_PREFIX_UNLOCKED(1) << "Updating pinned index to " << (last_idx_in_batch + 1);
      min_pinned_op_index_ = last_idx_in_batch + 1;
    }
  }
  user_callback.Run(log_status);
}

int64_t LogCache::earliest_op_index() const {
  auto ret = log_->GetLogReader()->GetMinReplicateIndex();
  if (ret == -1) { // No finalized log files yet.  Query the active log.
    ret = log_->GetMinReplicateIndex();
  }
  return ret;
}

bool LogCache::HasOpBeenWritten(int64_t index) const {
  std::lock_guard l(lock_);
  return index < next_sequential_op_index_;
}

Result<yb::OpId> LogCache::LookupOpId(int64_t op_index) const {
  // First check the log cache itself.
  {
    std::lock_guard l(lock_);

    // We sometimes try to look up OpIds that have never been written on
    // the local node. In that case, don't try to read the op from the
    // log reader, since it might actually race against the writing of the op.
    if (op_index >= next_sequential_op_index_) {
      return STATUS(Incomplete, Substitute("Op with index $0 is ahead of the local log "
                                          "(next sequential op: $1)",
                                          op_index, next_sequential_op_index_));
    }
    auto iter = cache_.find(op_index);
    if (iter != cache_.end()) {
      return yb::OpId::FromPB(iter->second.msg->id());
    }
  }

  // If it misses, read from the log.
  return log_->GetLogReader()->LookupOpId(op_index);
}

namespace {

// Calculate the total byte size that will be used on the wire to replicate this message as part of
// a consensus update request. This accounts for the length delimiting and tagging of the message.
int64_t TotalByteSizeForMessage(const LWReplicateMsg& msg) {
  auto msg_size = google::protobuf::internal::WireFormatLite::LengthDelimitedSize(
      msg.SerializedSize());
  msg_size += 1; // for the type tag
  return msg_size;
}

} // anonymous namespace

Result<ReadOpsResult> LogCache::ReadOps(int64_t after_op_index, size_t max_size_bytes) {
  return ReadOps(after_op_index, 0 /* to_op_index */, max_size_bytes);
}

// Disabled thread safety analysis because the locking seems to be inconsistent, we capture
// the cache iterator under lock, then unlock it to call some underlying functions, and then
// reacquire lock and continue using the previous iterator. This is error prone, since before
// reacquiring the lock, its possible that cache_ has been updated which invalidate the iterator.
// Created GH-13934 (https://github.com/yugabyte/yugabyte-db/issues/13934) to track this.
Result<ReadOpsResult> LogCache::ReadOps(
    int64_t after_op_index,
    int64_t to_op_index,
    size_t max_size_bytes,
    CoarseTimePoint deadline,
    bool fetch_single_entry) NO_THREAD_SAFETY_ANALYSIS {
  DCHECK_GE(after_op_index, 0);

  VLOG_WITH_PREFIX(4) << "ReadOps, after_op_index: " << after_op_index
                               << ", to_op_index: " << to_op_index
                               << ", max_size_bytes: " << max_size_bytes;
  ReadOpsResult result;
  int64_t starting_op_segment_seq_num;
  int64_t next_index;
  int64_t to_index;
  result.preceding_op = VERIFY_RESULT(LookupOpId(after_op_index));

  std::unique_lock<simple_spinlock> l(lock_);
  next_index = after_op_index + 1;
  to_index = to_op_index > 0 ? std::min(to_op_index + 1, next_sequential_op_index_)
                             : next_sequential_op_index_;

  if (fetch_single_entry) {
    next_index = to_index = to_op_index = after_op_index;
  }

  // Remove the deadline if the GetChanges deadline feature is disabled.
  if (!ANNOTATE_UNPROTECTED_READ(FLAGS_get_changes_honor_deadline)) {
    deadline = CoarseTimePoint::max();
  }

  // Return as many operations as we can, up to the limit.
  int64_t remaining_space = max_size_bytes;
  while (remaining_space >= 0 &&
         (fetch_single_entry ? next_index == to_index : next_index < to_index)) {
    // Stop reading if a deadline was specified and the deadline has been exceeded.
    if (deadline != CoarseTimePoint::max() && CoarseMonoClock::Now() >= deadline) {
      break;
    }

    // If the messages the peer needs haven't been loaded into the queue yet, load them.
    MessageCache::const_iterator iter = cache_.lower_bound(next_index);
    if (iter == cache_.end() || iter->first != next_index) {
      int64_t up_to;
      if (fetch_single_entry) {
        up_to = to_index;
      } else {
        if (iter == cache_.end()) {
          // Read all the way to the current op.
          up_to = to_index - 1;
        } else {
          // Read up to the next entry that's in the cache or to_index whichever is lesser.
          up_to = std::min(iter->first - 1, to_index - 1);
        }
      }

      l.unlock();

      ReplicateMsgs raw_replicate_ptrs;
      RETURN_NOT_OK_PREPEND(
          log_->GetLogReader()->ReadReplicatesInRange(
              next_index, up_to, remaining_space, &raw_replicate_ptrs, &starting_op_segment_seq_num,
              deadline),
          Substitute("Failed to read ops $0..$1", next_index, up_to));

      metrics_.disk_reads->IncrementBy(raw_replicate_ptrs.size());
      LOG_WITH_PREFIX(INFO)
          << "Successfully read " << raw_replicate_ptrs.size() << " ops from disk.";
      l.lock();

      for (auto& msg : raw_replicate_ptrs) {
        CHECK_EQ(next_index, msg->id().index());

        auto current_message_size = TotalByteSizeForMessage(*msg);
        remaining_space -= current_message_size;
        if (remaining_space < 0 && !result.messages.empty()) {
          break;
        }
        result.messages.push_back(msg);
        result.read_from_disk_size += current_message_size;
        next_index++;
      }
    } else {
      const auto seg_num_result = log_->GetLogReader()->LookupOpWalSegmentNumber(next_index);
      if (seg_num_result.ok()) {
        starting_op_segment_seq_num = *seg_num_result;
      } else if (!seg_num_result.status().IsNotFound()) {
        // Unexpected error - to be handled by the caller.
        return seg_num_result.status();
      }

      // Pull contiguous messages from the cache until the size limit is achieved.
      for (; iter != cache_.end(); ++iter) {
        if (to_op_index > 0 && next_index > to_op_index) {
          break;
        }
        const ReplicateMsgPtr& msg = iter->second.msg;
        int64_t index = msg->id().index();
        if (index != next_index) {
          continue;
        }

        auto current_message_size = TotalByteSizeForMessage(*msg);
        remaining_space -= current_message_size;
        if (remaining_space < 0 && !result.messages.empty()) {
          break;
        }

        result.messages.push_back(msg);
        next_index++;
      }
    }
  }
  result.have_more_messages = HaveMoreMessages(remaining_space < 0);
  return result;
}

size_t LogCache::EvictThroughOp(int64_t index, int64_t bytes_to_evict) {
  // Capture the evicted messages and release the memory outside of lock.
  ReplicateMsgVector evicted_messages;
  size_t bytes_evicted = 0;
  {
    std::lock_guard lock(lock_);
    bytes_evicted = EvictSomeUnlocked(index, bytes_to_evict, &evicted_messages);
  }

  return bytes_evicted;
}

size_t LogCache::EvictSomeUnlocked(int64_t stop_after_index, int64_t bytes_to_evict,
    ReplicateMsgVector* evicted_messages) REQUIRES(lock_) {
  DCHECK(lock_.is_locked());
  VLOG_WITH_PREFIX_UNLOCKED(2) << "Evicting log cache index <= "
                      << stop_after_index
                      << " or " << HumanReadableNumBytes::ToString(bytes_to_evict)
                      << ": before state: " << ToStringUnlocked();

  if (ANNOTATE_UNPROTECTED_READ(FLAGS_TEST_log_cache_skip_eviction)) {
    return 0;
  }

  int64_t bytes_evicted = 0;
  for (auto iter = cache_.begin(); iter != cache_.end();) {
    const CacheEntry& entry = iter->second;
    const ReplicateMsgPtr& msg = entry.msg;
    VLOG_WITH_PREFIX_UNLOCKED(2) << "considering for eviction: " << OpId::FromPB(msg->id());
    int64_t msg_index = msg->id().index();
    if (msg_index == 0) {
      // Always keep our special '0' op.
      ++iter;
      continue;
    }

    if (msg_index > stop_after_index || msg_index >= min_pinned_op_index_) {
      break;
    }

    VLOG_WITH_PREFIX_UNLOCKED(2) << "Evicting cache. Removing: " << OpId::FromPB(msg->id());
    AccountForMessageRemovalUnlocked(entry);
    bytes_evicted += entry.mem_usage;

    evicted_messages->push_back(msg);
    cache_.erase(iter++);

    if (bytes_evicted >= bytes_to_evict) {
      break;
    }
  }
  VLOG_WITH_PREFIX_UNLOCKED(1) << "Evicting log cache: after state: " << ToStringUnlocked();

  return bytes_evicted;
}

void LogCache::AccountForMessageRemovalUnlocked(const CacheEntry& entry) REQUIRES(lock_) {
  if (entry.tracked) {
    tracker_->Release(entry.mem_usage);
  }
  metrics_.size->DecrementBy(entry.mem_usage);
  metrics_.num_ops->Decrement();
}

int64_t LogCache::BytesUsed() const {
  return tracker_->consumption();
}

Result<OpId> LogCache::TEST_GetLastOpIdWithType(int64_t max_allowed_index, OperationType op_type) {
  constexpr int kStepSize = 20;
  for (auto end = max_allowed_index; end > 0; end -= kStepSize) {
    auto result = VERIFY_RESULT(ReadOps(
        std::max<int64_t>(0, end - kStepSize), end, std::numeric_limits<int>::max()));
    for (auto it = result.messages.end(); it != result.messages.begin();) {
      --it;
      if ((**it).op_type() == op_type) {
        return OpId::FromPB((**it).id());
      }
    }
  }
  return STATUS_FORMAT(NotFound, "Operation of type $0 not found before $1",
                       OperationType_Name(op_type), max_allowed_index);
}

string LogCache::StatsString() const {
  std::lock_guard lock(lock_);
  return StatsStringUnlocked();
}

string LogCache::StatsStringUnlocked() const REQUIRES(lock_) {
  return Substitute("LogCacheStats(num_ops=$0, bytes=$1, disk_reads=$2)",
                    metrics_.num_ops->value(),
                    metrics_.size->value(),
                    metrics_.disk_reads->value());
}

std::string LogCache::ToString() const {
  std::lock_guard lock(lock_);
  return ToStringUnlocked();
}

std::string LogCache::ToStringUnlocked() const REQUIRES(lock_) {
  return Substitute("Pinned index: $0, $1",
                    min_pinned_op_index_,
                    StatsStringUnlocked());
}

std::string LogCache::LogPrefix() const {
  return log_prefix_;
}

std::string LogCache::LogPrefixUnlocked() const REQUIRES(lock_) {
  return log_prefix_;
}

void LogCache::DumpToLog() const {
  vector<string> strings;
  DumpToStrings(&strings);
  for (const string& s : strings) {
    LOG_WITH_PREFIX(INFO) << s;
  }
}

void LogCache::DumpToStrings(vector<string>* lines) const {
  std::lock_guard lock(lock_);
  int counter = 0;
  lines->push_back(ToStringUnlocked());
  lines->push_back("Messages:");
  for (const auto& entry : cache_) {
    const ReplicateMsgPtr msg = entry.second.msg;
    lines->push_back(
      Substitute("Message[$0] $1.$2 : REPLICATE. Type: $3, Size: $4",
                 counter++, msg->id().term(), msg->id().index(),
                 OperationType_Name(msg->op_type()),
                 msg->SerializedSize()));
  }
}

void LogCache::DumpToHtml(std::ostream& out) const {
  using std::endl;

  std::lock_guard lock(lock_);
  out << "<h3>Messages:</h3>" << endl;
  out << "<table>" << endl;
  out << "<tr><th>Entry</th><th>OpId</th><th>Type</th><th>Size</th><th>Status</th></tr>" << endl;

  int counter = 0;
  for (const auto& entry : cache_) {
    const ReplicateMsgPtr msg = entry.second.msg;
    out << Substitute("<tr><th>$0</th><th>$1.$2</th><td>REPLICATE $3</td>"
                      "<td>$4</td><td>$5</td></tr>",
                      counter++, msg->id().term(), msg->id().index(),
                      OperationType_Name(msg->op_type()),
                      msg->SerializedSize(), msg->id().ShortDebugString()) << endl;
  }
  out << "</table>";
}

void LogCache::TrackOperationsMemory(const OpIds& op_ids) {
  if (op_ids.empty()) {
    return;
  }

  // Capture the evicted messages and release the memory outside of lock.
  ReplicateMsgVector evicted_messages;

  {
    std::lock_guard lock(lock_);

    size_t mem_required = 0;
    for (const auto& op_id : op_ids) {
      auto it = cache_.find(op_id.index);
      if (it != cache_.end() && it->second.msg->id().term() == op_id.term) {
        mem_required += it->second.mem_usage;
        it->second.tracked = true;
      }
    }

    if (mem_required == 0) {
      return;
    }

    // Try to consume the memory. If it can't be consumed, we may need to evict.
    if (!tracker_->TryConsume(mem_required)) {
      auto spare = tracker_->SpareCapacity();
      auto need_to_free = mem_required - spare;
      VLOG_WITH_PREFIX_UNLOCKED(1)
          << "Memory limit would be exceeded trying to append "
          << HumanReadableNumBytes::ToString(mem_required)
          << " to log cache (available="
          << HumanReadableNumBytes::ToString(spare)
          << "): attempting to evict some operations...";

      tracker_->Consume(mem_required);

      // TODO: we should also try to evict from other tablets - probably better to evict really old
      // ops from another tablet than evict recent ops from this one.
      EvictSomeUnlocked(min_pinned_op_index_, need_to_free, &evicted_messages);
    }
  }
}

int64_t LogCache::num_cached_ops() const {
  return metrics_.num_ops->value();
}

#define INSTANTIATE_METRIC(x, ...) \
  x(BOOST_PP_CAT(METRIC_log_cache_, x).Instantiate(metric_entity, ## __VA_ARGS__))
LogCache::Metrics::Metrics(const scoped_refptr<MetricEntity>& metric_entity)
  : INSTANTIATE_METRIC(num_ops, 0),
    INSTANTIATE_METRIC(size, 0),
    INSTANTIATE_METRIC(disk_reads) {
}
#undef INSTANTIATE_METRIC

} // namespace consensus
} // namespace yb
