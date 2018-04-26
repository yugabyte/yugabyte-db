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

#include "yb/util/mem_tracker.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <list>
#include <memory>
#include <mutex>

#ifdef TCMALLOC_ENABLED
#include <gperftools/malloc_extension.h>
#endif

#include "yb/gutil/map-util.h"
#include "yb/gutil/once.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/debug-util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/env.h"
#include "yb/util/flag_tags.h"
#include "yb/util/mutex.h"
#include "yb/util/random_util.h"
#include "yb/util/status.h"
#include "yb/util/logging.h"

DEFINE_int64(memory_limit_hard_bytes, 0,
             "Maximum amount of memory this daemon should use, in bytes. "
             "A value of 0 autosizes based on the total system memory. "
             "A value of -1 disables all memory limiting.");
TAG_FLAG(memory_limit_hard_bytes, stable);
DEFINE_double(default_memory_limit_to_ram_ratio, 0.85,
              "If memory_limit_hard_bytes is left unspecified, then it is "
              "set to default_memory_limit_to_ram_ratio * Available RAM.");
TAG_FLAG(default_memory_limit_to_ram_ratio, advanced);
TAG_FLAG(default_memory_limit_to_ram_ratio, hidden);

DEFINE_int32(memory_limit_soft_percentage, 85,
             "Percentage of the hard memory limit that this daemon may "
             "consume before memory throttling of writes begins. The greater "
             "the excess, the higher the chance of throttling. In general, a "
             "lower soft limit leads to smoother write latencies but "
             "decreased throughput, and vice versa for a higher soft limit.");
TAG_FLAG(memory_limit_soft_percentage, advanced);

DEFINE_int32(memory_limit_warn_threshold_percentage, 98,
             "Percentage of the hard memory limit that this daemon may "
             "consume before WARNING level messages are periodically logged.");
TAG_FLAG(memory_limit_warn_threshold_percentage, advanced);

#ifdef TCMALLOC_ENABLED
DEFINE_int32(tcmalloc_max_free_bytes_percentage, 10,
             "Maximum percentage of the RSS that tcmalloc is allowed to use for "
             "reserved but unallocated memory.");
TAG_FLAG(tcmalloc_max_free_bytes_percentage, advanced);
#endif

DEFINE_bool(mem_tracker_logging, false,
            "Enable logging of memory tracker consume/release operations");

DEFINE_bool(mem_tracker_log_stack_trace, false,
            "Enable logging of stack traces on memory tracker consume/release operations. "
            "Only takes effect if mem_tracker_logging is also enabled.");

namespace yb {

// NOTE: this class has been adapted from Impala, so the code style varies
// somewhat from yb.

using std::deque;
using std::list;
using std::string;
using std::stringstream;
using std::shared_ptr;
using std::vector;

using strings::Substitute;

// The ancestor for all trackers. Every tracker is visible from the root down.
static shared_ptr<MemTracker> root_tracker;
static GoogleOnceType root_tracker_once = GOOGLE_ONCE_INIT;

// Total amount of memory from calls to Release() since the last GC. If this
// is greater than GC_RELEASE_SIZE, this will trigger a tcmalloc gc.
static Atomic64 released_memory_since_gc;

// Validate that various flags are percentages.
static bool ValidatePercentage(const char* flagname, int value) {
  if (value >= 0 && value <= 100) {
    return true;
  }
  LOG(ERROR) << Substitute("$0 must be a percentage, value $1 is invalid",
                           flagname, value);
  return false;
}

// Marked as unused because this is not referenced in release mode.
static bool dummy[] __attribute__((unused)) = {
  google::RegisterFlagValidator(&FLAGS_memory_limit_soft_percentage, &ValidatePercentage),
  google::RegisterFlagValidator(&FLAGS_memory_limit_warn_threshold_percentage, &ValidatePercentage)
#ifdef TCMALLOC_ENABLED
  , google::RegisterFlagValidator(&FLAGS_tcmalloc_max_free_bytes_percentage, &ValidatePercentage)
#endif
};

void MemTracker::CreateRootTracker() {
  DCHECK_ONLY_NOTNULL(dummy);
  int64_t limit = FLAGS_memory_limit_hard_bytes;
  if (limit == 0) {
    // If no limit is provided, we'll use
    // - 85% of the RAM for tservers.
    // - 10% of the RAM for masters.
    int64_t total_ram;
    CHECK_OK(Env::Default()->GetTotalRAMBytes(&total_ram));
    limit = total_ram * FLAGS_default_memory_limit_to_ram_ratio;
  }

  ConsumptionFunction f;
#ifdef TCMALLOC_ENABLED
  f = &GetTCMallocCurrentAllocatedBytes;
#endif
  root_tracker.reset(new MemTracker(f, limit, "root",
                                    shared_ptr<MemTracker>()));
  LOG(INFO) << StringPrintf("MemTracker: hard memory limit is %.6f GB",
                            (static_cast<float>(limit) / (1024.0 * 1024.0 * 1024.0)));
  LOG(INFO) << StringPrintf("MemTracker: soft memory limit is %.6f GB",
                            (static_cast<float>(root_tracker->soft_limit_) /
                                (1024.0 * 1024.0 * 1024.0)));
}

shared_ptr<MemTracker> MemTracker::CreateTracker(int64_t byte_limit,
                                                 const string& id,
                                                 const shared_ptr<MemTracker>& parent) {
  shared_ptr<MemTracker> real_parent = parent ? parent : GetRootTracker();
  return real_parent->CreateChild(byte_limit, id, false);
}

shared_ptr<MemTracker> MemTracker::CreateChild(int64_t byte_limit,
                                               const string& id,
                                               bool may_exist) {
  std::lock_guard<std::mutex> lock(child_trackers_mutex_);
  if (may_exist) {
    auto result = FindChildUnlocked(id);
    if (result) {
      return result;
    }
  }
  auto result = std::make_shared<MemTracker>(
      ConsumptionFunction(), byte_limit, id, shared_from_this());
  auto p = child_trackers_.emplace(id, result);
  if (!p.second) {
    auto existing = p.first->second.lock();
    if (existing) {
      LOG(DFATAL) << Format("Duplicate memory tracker (id $0) on parent $1", id, ToString());
      return existing;
    }
    p.first->second = result;
  }

  return result;
}

MemTracker::MemTracker(ConsumptionFunction consumption_func, int64_t byte_limit,
                       const string& id, shared_ptr<MemTracker> parent)
    : limit_(byte_limit),
      id_(id),
      descr_(Substitute("memory consumption for $0", id)),
      parent_(std::move(parent)),
      consumption_(0),
      consumption_func_(std::move(consumption_func)),
      rand_(GetRandomSeed32()),
      enable_logging_(FLAGS_mem_tracker_logging),
      log_stack_(FLAGS_mem_tracker_log_stack_trace) {
  VLOG(1) << "Creating tracker " << ToString();
  if (consumption_func_) {
    UpdateConsumption();
  }
  soft_limit_ = (limit_ == -1)
      ? -1 : (limit_ * FLAGS_memory_limit_soft_percentage) / 100;

  all_trackers_.push_back(this);
  if (has_limit()) {
    limit_trackers_.push_back(this);
  }
  if (parent_) {
    all_trackers_.insert(
        all_trackers_.end(), parent_->all_trackers_.begin(), parent_->all_trackers_.end());
    limit_trackers_.insert(
        limit_trackers_.end(), parent_->limit_trackers_.begin(), parent_->limit_trackers_.end());
  }
}

MemTracker::~MemTracker() {
  VLOG(1) << "Destroying tracker " << ToString();
  if (parent_) {
    DCHECK_EQ(consumption(), 0) << "Memory tracker " << ToString();
    parent_->Release(consumption());
  }
}

void MemTracker::UnregisterFromParent() {
  DCHECK(parent_);
  parent_->UnregisterChild(id_);
}

void MemTracker::UnregisterChild(const std::string& id) {
  std::lock_guard<std::mutex> lock(child_trackers_mutex_);
  child_trackers_.erase(id);
}

string MemTracker::ToString() const {
  string s;
  const MemTracker* tracker = this;
  while (tracker) {
    if (s != "") {
      s += "->";
    }
    s += tracker->id();
    tracker = tracker->parent_.get();
  }
  return s;
}

MemTrackerPtr MemTracker::FindTracker(const std::string& id,
                                      const MemTrackerPtr& parent) {
  shared_ptr<MemTracker> real_parent = parent ? parent : GetRootTracker();
  return real_parent->FindChild(id);
}

MemTrackerPtr MemTracker::FindChild(const std::string& id) {
  std::lock_guard<std::mutex> lock(child_trackers_mutex_);
  return FindChildUnlocked(id);
}

MemTrackerPtr MemTracker::FindChildUnlocked(const std::string& id) {
  auto it = child_trackers_.find(id);
  if (it != child_trackers_.end()) {
    auto result = it->second.lock();
    if (!result) {
      child_trackers_.erase(it);
    }
    return result;
  }
  return MemTrackerPtr();
}

shared_ptr<MemTracker> MemTracker::FindOrCreateTracker(int64_t byte_limit,
                                                       const string& id,
                                                       const shared_ptr<MemTracker>& parent) {
  shared_ptr<MemTracker> real_parent = parent ? parent : GetRootTracker();
  return real_parent->CreateChild(byte_limit, id, true);
}

void MemTracker::ListDescendantTrackers(std::vector<MemTrackerPtr>* out) {
  size_t begin = out->size();
  {
    std::lock_guard<std::mutex> lock(child_trackers_mutex_);
    for (auto it = child_trackers_.begin(); it != child_trackers_.end();) {
      auto child = it->second.lock();
      if (child) {
        out->push_back(std::move(child));
        ++it;
      } else {
        it = child_trackers_.erase(it);
      }
    }
  }
  size_t end = out->size();
  for (size_t i = begin; i != end; ++i) {
    (*out)[i]->ListDescendantTrackers(out);
  }
}

void MemTracker::UpdateConsumption() {
  DCHECK(consumption_func_);
  DCHECK(parent_.get() == NULL);
  consumption_.set_value(consumption_func_());
}

void MemTracker::Consume(int64_t bytes) {
  if (bytes < 0) {
    Release(-bytes);
    return;
  }

  if (consumption_func_) {
    UpdateConsumption();
    return;
  }
  if (bytes == 0) {
    return;
  }
  if (PREDICT_FALSE(enable_logging_)) {
    LogUpdate(true, bytes);
  }
  for (auto& tracker : all_trackers_) {
    tracker->consumption_.IncrementBy(bytes);
    if (tracker->consumption_func_) {
      DCHECK_GE(tracker->consumption_.current_value(), 0);
    }
  }
}

bool MemTracker::TryConsume(int64_t bytes) {
  if (consumption_func_) {
    UpdateConsumption();
  }
  if (bytes <= 0) {
    return true;
  }
  if (PREDICT_FALSE(enable_logging_)) {
    LogUpdate(true, bytes);
  }

  int i = 0;
  // Walk the tracker tree top-down, to avoid expanding a limit on a child whose parent
  // won't accommodate the change.
  for (i = all_trackers_.size() - 1; i >= 0; --i) {
    MemTracker *tracker = all_trackers_[i];
    if (tracker->limit_ < 0) {
      tracker->consumption_.IncrementBy(bytes);
    } else {
      if (!tracker->consumption_.TryIncrementBy(bytes, tracker->limit_)) {
        // One of the trackers failed, attempt to GC memory or expand our limit. If that
        // succeeds, TryUpdate() again. Bail if either fails.
        if (!tracker->GcMemory(tracker->limit_ - bytes) ||
            tracker->ExpandLimit(bytes)) {
          if (!tracker->consumption_.TryIncrementBy(
                  bytes, tracker->limit_)) {
            break;
          }
        } else {
          break;
        }
      }
    }
  }
  // Everyone succeeded, return.
  if (i == -1) {
    return true;
  }

  // Someone failed, roll back the ones that succeeded.
  // TODO: this doesn't roll it back completely since the max values for
  // the updated trackers aren't decremented. The max values are only used
  // for error reporting so this is probably okay. Rolling those back is
  // pretty hard; we'd need something like 2PC.
  //
  // TODO: This might leave us with an allocated resource that we can't use. Do we need
  // to adjust the consumption of the query tracker to stop the resource from never
  // getting used by a subsequent TryConsume()?
  for (int j = all_trackers_.size() - 1; j > i; --j) {
    all_trackers_[j]->consumption_.IncrementBy(-bytes);
  }
  return false;
}

void MemTracker::Release(int64_t bytes) {
  if (bytes < 0) {
    Consume(-bytes);
    return;
  }

  if (PREDICT_FALSE(base::subtle::Barrier_AtomicIncrement(&released_memory_since_gc, bytes) >
                    GC_RELEASE_SIZE)) {
    GcTcmalloc();
  }

  if (consumption_func_) {
    UpdateConsumption();
    return;
  }

  if (bytes == 0) {
    return;
  }
  if (PREDICT_FALSE(enable_logging_)) {
    LogUpdate(false, bytes);
  }

  for (auto& tracker : all_trackers_) {
    tracker->consumption_.IncrementBy(-bytes);
    // If a UDF calls FunctionContext::TrackAllocation() but allocates less than the
    // reported amount, the subsequent call to FunctionContext::Free() may cause the
    // process mem tracker to go negative until it is synced back to the tcmalloc
    // metric. Don't blow up in this case. (Note that this doesn't affect non-process
    // trackers since we can enforce that the reported memory usage is internally
    // consistent.)
    if (tracker->consumption_func_) {
      DCHECK_GE(tracker->consumption_.current_value(), 0) << "Tracker: " << tracker->ToString();
    }
  }
}

bool MemTracker::AnyLimitExceeded() {
  for (const auto& tracker : limit_trackers_) {
    if (tracker->LimitExceeded()) {
      return true;
    }
  }
  return false;
}

bool MemTracker::LimitExceeded() {
  if (PREDICT_FALSE(CheckLimitExceeded())) {
    return GcMemory(limit_);
  }
  return false;
}

bool MemTracker::SoftLimitExceeded(double* current_capacity_pct) {
  // Did we exceed the actual limit?
  if (LimitExceeded()) {
    if (current_capacity_pct) {
      *current_capacity_pct =
          static_cast<double>(consumption()) / limit() * 100;
    }
    return true;
  }

  // No soft limit defined.
  if (!has_limit() || limit_ == soft_limit_) {
    return false;
  }

  // Are we under the soft limit threshold?
  int64_t usage = consumption();
  if (usage < soft_limit_) {
    return false;
  }

  // We're over the threshold; were we randomly chosen to be over the soft limit?
  if (usage + rand_.Uniform64(limit_ - soft_limit_) > limit_) {
    bool exceeded = GcMemory(soft_limit_);
    if (exceeded && current_capacity_pct) {
      *current_capacity_pct =
          static_cast<double>(consumption()) / limit() * 100;
    }
    return exceeded;
  }
  return false;
}

bool MemTracker::AnySoftLimitExceeded(double* current_capacity_pct) {
  for (MemTracker* t : limit_trackers_) {
    if (t->SoftLimitExceeded(current_capacity_pct)) {
      return true;
    }
  }
  return false;
}

int64_t MemTracker::SpareCapacity() const {
  int64_t result = std::numeric_limits<int64_t>::max();
  for (const auto& tracker : limit_trackers_) {
    int64_t mem_left = tracker->limit() - tracker->consumption();
    result = std::min(result, mem_left);
  }
  return result;
}

bool MemTracker::GcMemory(int64_t max_consumption) {
  if (max_consumption < 0) {
    // Impossible to GC enough memory to reach the goal.
    return true;
  }

  {
    std::lock_guard<simple_spinlock> l(gc_lock_);
    if (consumption_func_) {
      UpdateConsumption();
    }
    uint64_t pre_gc_consumption = consumption();
    // Check if someone gc'd before us
    if (pre_gc_consumption < max_consumption) {
      return false;
    }

    // Try to free up some memory
    for (const auto& gc_function : gc_functions_) {
      gc_function();
      if (consumption_func_) {
        UpdateConsumption();
      }
      if (consumption() <= max_consumption) {
        break;
      }
    }
  }

  if (consumption() > max_consumption) {
    std::vector<MemTrackerPtr> children;
    {
      std::lock_guard<std::mutex> lock(child_trackers_mutex_);
      for (auto it = child_trackers_.begin(); it != child_trackers_.end();) {
        auto child = it->second.lock();
        if (child) {
          children.push_back(std::move(child));
          ++it;
        } else {
          it = child_trackers_.erase(it);
        }
      }
    }

    for (const auto& child : children) {
      if (child->GcMemory(max_consumption) && consumption() <= max_consumption) {
        return true;
      }
    }
  }

  return consumption() > max_consumption;
}

void MemTracker::GcTcmalloc() {
#ifdef TCMALLOC_ENABLED
  released_memory_since_gc = 0;
  TRACE_EVENT0("process", "MemTracker::GcTcmalloc");

  // Number of bytes in the 'NORMAL' free list (i.e reserved by tcmalloc but
  // not in use).
  int64_t bytes_overhead = GetTCMallocProperty("tcmalloc.pageheap_free_bytes");
  // Bytes allocated by the application.
  int64_t bytes_used = GetTCMallocCurrentAllocatedBytes();

  int64_t max_overhead = bytes_used * FLAGS_tcmalloc_max_free_bytes_percentage / 100.0;
  if (bytes_overhead > max_overhead) {
    int64_t extra = bytes_overhead - max_overhead;
    while (extra > 0) {
      // Release 1MB at a time, so that tcmalloc releases its page heap lock
      // allowing other threads to make progress. This still disrupts the current
      // thread, but is better than disrupting all.
      MallocExtension::instance()->ReleaseToSystem(1024 * 1024);
      extra -= 1024 * 1024;
    }
  }

#else
  // Nothing to do if not using tcmalloc.
#endif
}

string MemTracker::LogUsage(const string& prefix) const {
  stringstream ss;
  ss << prefix << id_ << ":";
  if (CheckLimitExceeded()) {
    ss << " memory limit exceeded.";
  }
  if (limit_ > 0) {
    ss << " Limit=" << HumanReadableNumBytes::ToString(limit_);
  }
  ss << " Consumption=" << HumanReadableNumBytes::ToString(consumption());

  stringstream prefix_ss;
  prefix_ss << prefix << "  ";
  string new_prefix = prefix_ss.str();
  std::lock_guard<std::mutex> lock(child_trackers_mutex_);
  for (const auto& p : child_trackers_) {
    auto child = p.second.lock();
    if (child) {
      ss << std::endl;
      ss << child->LogUsage(prefix);
    }
  }
  return ss.str();
}

void MemTracker::LogUpdate(bool is_consume, int64_t bytes) const {
  stringstream ss;
  ss << this << " " << (is_consume ? "Consume: " : "Release: ") << bytes
     << " Consumption: " << consumption() << " Limit: " << limit_;
  if (log_stack_) {
    ss << std::endl << GetStackTrace();
  }
  LOG(ERROR) << ss.str();
}

shared_ptr<MemTracker> MemTracker::GetRootTracker() {
  GoogleOnceInit(&root_tracker_once, &MemTracker::CreateRootTracker);
  return root_tracker;
}

} // namespace yb
