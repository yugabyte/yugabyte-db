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

#include <gflags/gflags.h>
#include <google/protobuf/wire_format_lite.h>
#include <google/protobuf/wire_format_lite_inl.h>

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/ref_counted_replicate.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/debug-util.h"
#include "yb/util/flag_tags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"

DEFINE_int32(log_cache_size_limit_mb, 128,
             "The total per-tablet size of consensus entries which may be kept in memory. "
             "The log cache attempts to keep all entries which have not yet been replicated "
             "to all followers in memory, but if the total size of those entries exceeds "
             "this limit within an individual tablet, the oldest will be evicted.");
TAG_FLAG(log_cache_size_limit_mb, advanced);

DEFINE_int32(global_log_cache_size_limit_mb, 1024,
             "Server-wide version of 'log_cache_size_limit_mb'. The total memory used for "
             "caching log entries across all tablets is kept under this threshold.");
TAG_FLAG(global_log_cache_size_limit_mb, advanced);

using strings::Substitute;

namespace yb {
namespace consensus {

METRIC_DEFINE_gauge_int64(tablet, log_cache_num_ops, "Log Cache Operation Count",
                          MetricUnit::kOperations,
                          "Number of operations in the log cache.");
METRIC_DEFINE_gauge_int64(tablet, log_cache_size, "Log Cache Memory Usage",
                          MetricUnit::kBytes,
                          "Amount of memory in use for caching the local log.");

static const char kParentMemTrackerId[] = "log_cache";

typedef vector<const ReplicateMsg*>::const_iterator MsgIter;

LogCache::LogCache(const scoped_refptr<MetricEntity>& metric_entity,
                   const scoped_refptr<log::Log>& log,
                   const string& local_uuid,
                   const string& tablet_id)
  : log_(log),
    local_uuid_(local_uuid),
    tablet_id_(tablet_id),
    next_sequential_op_index_(0),
    min_pinned_op_index_(0),
    metrics_(metric_entity) {


  const int64_t max_ops_size_bytes = FLAGS_log_cache_size_limit_mb * 1024 * 1024;
  const int64_t global_max_ops_size_bytes = FLAGS_global_log_cache_size_limit_mb * 1024 * 1024;

  // Set up (or reuse) a tracker with the global limit. It is parented directly to the root tracker
  // so that it's always global.
  parent_tracker_ = MemTracker::FindOrCreateTracker(global_max_ops_size_bytes,
                                                    kParentMemTrackerId);

  // And create a child tracker with the per-tablet limit.
  tracker_ = MemTracker::CreateTracker(
      max_ops_size_bytes, Substitute("$0:$1:$2", kParentMemTrackerId,
                                     local_uuid, tablet_id),
      parent_tracker_);

  // Put a fake message at index 0, since this simplifies a lot of our code paths elsewhere.
  auto zero_op = std::make_shared<ReplicateMsg>();
  *zero_op->mutable_id() = MinimumOpId();
  InsertOrDie(&cache_, 0, { zero_op, zero_op->SpaceUsed() });
}

LogCache::~LogCache() {
  tracker_->Release(tracker_->consumption());
  cache_.clear();

  tracker_->UnregisterFromParent();
}

void LogCache::Init(const OpId& preceding_op) {
  std::lock_guard<simple_spinlock> l(lock_);
  CHECK_EQ(cache_.size(), 1) << "Cache should have only our special '0' op";
  next_sequential_op_index_ = preceding_op.index() + 1;
  min_pinned_op_index_ = next_sequential_op_index_;
}

Result<LogCache::PrepareAppendResult> LogCache::PrepareAppendOperations(const ReplicateMsgs& msgs) {
  // SpaceUsed is relatively expensive, so do calculations outside the lock
  PrepareAppendResult result;
  std::vector<CacheEntry> entries_to_insert;
  entries_to_insert.reserve(msgs.size());
  for (const auto& msg : msgs) {
    CacheEntry e = { msg, static_cast<int64_t>(msg->SpaceUsedLong()) };
    result.mem_required += e.mem_usage;
    entries_to_insert.emplace_back(std::move(e));
  }

  int64_t first_idx_in_batch = msgs.front()->id().index();
  result.last_idx_in_batch = msgs.back()->id().index();

  std::unique_lock<simple_spinlock> lock(lock_);
  // If we're not appending a consecutive op we're likely overwriting and need to replace operations
  // in the cache.
  if (first_idx_in_batch != next_sequential_op_index_) {
    // If the index is not consecutive then it must be lower than or equal to the last index, i.e.
    // we're overwriting.
    CHECK_LE(first_idx_in_batch, next_sequential_op_index_);

    // Now remove the overwritten operations.
    for (int64_t i = first_idx_in_batch; i < next_sequential_op_index_; ++i) {
      auto it = cache_.find(i);
      if (it != cache_.end()) {
        AccountForMessageRemovalUnlocked(it->second);
        cache_.erase(it);
      }
    }
  }

  // Try to consume the memory. If it can't be consumed, we may need to evict.
  if (!tracker_->TryConsume(result.mem_required)) {
    int spare = tracker_->SpareCapacity();
    int need_to_free = result.mem_required - spare;
    VLOG_WITH_PREFIX_UNLOCKED(1) << "Memory limit would be exceeded trying to append "
                        << HumanReadableNumBytes::ToString(result.mem_required)
                        << " to log cache (available="
                        << HumanReadableNumBytes::ToString(spare)
                        << "): attempting to evict some operations...";

    // TODO: we should also try to evict from other tablets - probably better to evict really old
    // ops from another tablet than evict recent ops from this one.
    EvictSomeUnlocked(min_pinned_op_index_, need_to_free);

    // Force consuming, so that we don't refuse appending data. We might blow past our limit a
    // little bit (as much as the number of tablets times the amount of in-flight data in the log),
    // but until implementing the above TODO, it's difficult to solve this issue.
    tracker_->Consume(result.mem_required);

    result.borrowed_memory = parent_tracker_->LimitExceeded();
  }

  for (auto& e : entries_to_insert) {
    auto index = e.msg->id().index();
    EmplaceOrDie(&cache_, index, std::move(e));
    next_sequential_op_index_ = index + 1;
  }

  return result;
}

Status LogCache::AppendOperations(const ReplicateMsgs& msgs, const yb::OpId& committed_op_id,
                                  const StatusCallback& callback) {
  PrepareAppendResult prepare_result;
  if (!msgs.empty()) {
    prepare_result = VERIFY_RESULT(PrepareAppendOperations(msgs));
    metrics_.log_cache_size->IncrementBy(prepare_result.mem_required);
    metrics_.log_cache_num_ops->IncrementBy(msgs.size());
  }

  Status log_status = log_->AsyncAppendReplicates(
    msgs, committed_op_id,
    Bind(&LogCache::LogCallback, Unretained(this), prepare_result.last_idx_in_batch,
         prepare_result.borrowed_memory, callback));

  if (!log_status.ok()) {
    LOG_WITH_PREFIX_UNLOCKED(WARNING) << "Couldn't append to log: " << log_status.ToString();
    tracker_->Release(prepare_result.mem_required);
    return log_status;
  }

  return Status::OK();
}

void LogCache::LogCallback(int64_t last_idx_in_batch,
                           bool borrowed_memory,
                           const StatusCallback& user_callback,
                           const Status& log_status) {
  if (log_status.ok()) {
    std::lock_guard<simple_spinlock> l(lock_);
    if (min_pinned_op_index_ <= last_idx_in_batch) {
      VLOG_WITH_PREFIX_UNLOCKED(1) << "Updating pinned index to " << (last_idx_in_batch + 1);
      min_pinned_op_index_ = last_idx_in_batch + 1;
    }

    // If we went over the global limit in order to log this batch, evict some to get back down
    // under the limit.
    if (borrowed_memory) {
      int64_t spare_capacity = parent_tracker_->SpareCapacity();
      if (spare_capacity < 0) {
        EvictSomeUnlocked(min_pinned_op_index_, -spare_capacity);
      }
    }
  }
  user_callback.Run(log_status);
}

bool LogCache::HasOpBeenWritten(int64_t index) const {
  std::lock_guard<simple_spinlock> l(lock_);
  return index < next_sequential_op_index_;
}

Status LogCache::LookupOpId(int64_t op_index, OpId* op_id) const {
  // First check the log cache itself.
  {
    std::lock_guard<simple_spinlock> l(lock_);

    // We sometimes try to look up OpIds that have never been written on the local node. In that
    // case, don't try to read the op from the log reader, since it might actually race against the
    // writing of the op.
    if (op_index >= next_sequential_op_index_) {
      return STATUS(Incomplete, Substitute("Op with index $0 is ahead of the local log "
                                           "(next sequential op: $1)",
                                           op_index, next_sequential_op_index_));
    }
    auto iter = cache_.find(op_index);
    if (iter != cache_.end()) {
      *op_id = iter->second.msg->id();
      return Status::OK();
    }
  }

  // If it misses, read from the log.
  return log_->GetLogReader()->LookupOpId(op_index, op_id);
}

namespace {

// Calculate the total byte size that will be used on the wire to replicate this message as part of
// a consensus update request. This accounts for the length delimiting and tagging of the message.
int64_t TotalByteSizeForMessage(const ReplicateMsg& msg) {
  int msg_size = google::protobuf::internal::WireFormatLite::LengthDelimitedSize(
    msg.ByteSize());
  msg_size += 1; // for the type tag
  return msg_size;
}

} // anonymous namespace

Status LogCache::ReadOps(int64_t after_op_index,
                         int max_size_bytes,
                         ReplicateMsgs* messages,
                         OpId* preceding_op) {
  DCHECK_ONLY_NOTNULL(messages);
  DCHECK_ONLY_NOTNULL(preceding_op);
  DCHECK_GE(after_op_index, 0);
  RETURN_NOT_OK(LookupOpId(after_op_index, preceding_op));

  std::unique_lock<simple_spinlock> l(lock_);
  int64_t next_index = after_op_index + 1;

  // Return as many operations as we can, up to the limit
  int64_t remaining_space = max_size_bytes;
  while (remaining_space > 0 && next_index < next_sequential_op_index_) {

    // If the messages the peer needs haven't been loaded into the queue yet, load them.
    MessageCache::const_iterator iter = cache_.lower_bound(next_index);
    if (iter == cache_.end() || iter->first != next_index) {
      int64_t up_to;
      if (iter == cache_.end()) {
        // Read all the way to the current op.
        up_to = next_sequential_op_index_ - 1;
      } else {
        // Read up to the next entry that's in the cache.
        up_to = iter->first - 1;
      }

      l.unlock();

      ReplicateMsgs raw_replicate_ptrs;
      RETURN_NOT_OK_PREPEND(
        log_->GetLogReader()->ReadReplicatesInRange(
            next_index, up_to, remaining_space, &raw_replicate_ptrs),
        Substitute("Failed to read ops $0..$1", next_index, up_to));
      l.lock();
      LOG_WITH_PREFIX_UNLOCKED(INFO) << "Successfully read " << raw_replicate_ptrs.size() << " ops "
                            << "from disk.";

      for (auto& msg : raw_replicate_ptrs) {
        CHECK_EQ(next_index, msg->id().index());

        remaining_space -= TotalByteSizeForMessage(*msg);
        if (remaining_space > 0 || messages->empty()) {
          messages->push_back(msg);
          next_index++;
        }
      }

    } else {
      // Pull contiguous messages from the cache until the size limit is achieved.
      for (; iter != cache_.end(); ++iter) {
        const ReplicateMsgPtr& msg = iter->second.msg;
        int64_t index = msg->id().index();
        if (index != next_index) {
          continue;
        }

        remaining_space -= TotalByteSizeForMessage(*msg);
        if (remaining_space < 0 && !messages->empty()) {
          break;
        }

        messages->push_back(msg);
        next_index++;
      }
    }
  }
  return Status::OK();
}


void LogCache::EvictThroughOp(int64_t index) {
  std::lock_guard<simple_spinlock> lock(lock_);

  EvictSomeUnlocked(index, MathLimits<int64_t>::kMax);
}

void LogCache::EvictSomeUnlocked(int64_t stop_after_index, int64_t bytes_to_evict) {
  DCHECK(lock_.is_locked());
  VLOG_WITH_PREFIX_UNLOCKED(2) << "Evicting log cache index <= "
                      << stop_after_index
                      << " or " << HumanReadableNumBytes::ToString(bytes_to_evict)
                      << ": before state: " << ToStringUnlocked();

  int64_t bytes_evicted = 0;
  for (auto iter = cache_.begin(); iter != cache_.end();) {
    const CacheEntry& entry = iter->second;
    const ReplicateMsgPtr& msg = entry.msg;
    VLOG_WITH_PREFIX_UNLOCKED(2) << "considering for eviction: " << msg->id();
    int64_t msg_index = msg->id().index();
    if (msg_index == 0) {
      // Always keep our special '0' op.
      ++iter;
      continue;
    }

    if (msg_index > stop_after_index || msg_index >= min_pinned_op_index_) {
      break;
    }

    if (!msg.unique()) {
      VLOG_WITH_PREFIX_UNLOCKED(2) << "Evicting cache: cannot remove " << msg->id()
                                   << " because it is in-use by a peer.";
      ++iter;
      continue;
    }

    VLOG_WITH_PREFIX_UNLOCKED(2) << "Evicting cache. Removing: " << msg->id();
    AccountForMessageRemovalUnlocked(entry);
    bytes_evicted += entry.mem_usage;
    cache_.erase(iter++);

    if (bytes_evicted >= bytes_to_evict) {
      break;
    }
  }
  VLOG_WITH_PREFIX_UNLOCKED(1) << "Evicting log cache: after state: " << ToStringUnlocked();
}

void LogCache::AccountForMessageRemovalUnlocked(const CacheEntry& entry) {
  tracker_->Release(entry.mem_usage);
  metrics_.log_cache_size->DecrementBy(entry.mem_usage);
  metrics_.log_cache_num_ops->Decrement();
}

int64_t LogCache::BytesUsed() const {
  return tracker_->consumption();
}

string LogCache::StatsString() const {
  std::lock_guard<simple_spinlock> lock(lock_);
  return StatsStringUnlocked();
}

string LogCache::StatsStringUnlocked() const {
  return Substitute("LogCacheStats(num_ops=$0, bytes=$1)",
                    metrics_.log_cache_num_ops->value(),
                    metrics_.log_cache_size->value());
}

std::string LogCache::ToString() const {
  std::lock_guard<simple_spinlock> lock(lock_);
  return ToStringUnlocked();
}

std::string LogCache::ToStringUnlocked() const {
  return Substitute("Pinned index: $0, $1",
                    min_pinned_op_index_,
                    StatsStringUnlocked());
}

std::string LogCache::LogPrefixUnlocked() const {
  return Substitute("T $0 P $1: ",
                    tablet_id_,
                    local_uuid_);
}

void LogCache::DumpToLog() const {
  vector<string> strings;
  DumpToStrings(&strings);
  for (const string& s : strings) {
    LOG_WITH_PREFIX_UNLOCKED(INFO) << s;
  }
}

void LogCache::DumpToStrings(vector<string>* lines) const {
  std::lock_guard<simple_spinlock> lock(lock_);
  int counter = 0;
  lines->push_back(ToStringUnlocked());
  lines->push_back("Messages:");
  for (const auto& entry : cache_) {
    const ReplicateMsgPtr msg = entry.second.msg;
    lines->push_back(
      Substitute("Message[$0] $1.$2 : REPLICATE. Type: $3, Size: $4",
                 counter++, msg->id().term(), msg->id().index(),
                 OperationType_Name(msg->op_type()),
                 msg->ByteSize()));
  }
}

void LogCache::DumpToHtml(std::ostream& out) const {
  using std::endl;

  std::lock_guard<simple_spinlock> lock(lock_);
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
                      msg->ByteSize(), msg->id().ShortDebugString()) << endl;
  }
  out << "</table>";
}

#define INSTANTIATE_METRIC(x) \
  x.Instantiate(metric_entity, 0)
LogCache::Metrics::Metrics(const scoped_refptr<MetricEntity>& metric_entity)
  : log_cache_num_ops(INSTANTIATE_METRIC(METRIC_log_cache_num_ops)),
    log_cache_size(INSTANTIATE_METRIC(METRIC_log_cache_size)) {
}
#undef INSTANTIATE_METRIC

} // namespace consensus
} // namespace yb
