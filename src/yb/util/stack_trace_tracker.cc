// Copyright (c) YugaByte, Inc.
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
#include "yb/util/stack_trace_tracker.h"

#include <mutex>
#include <thread>
#include <unordered_map>

#include "yb/gutil/stl_util.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/atomic.h"
#include "yb/util/debug.h"
#include "yb/util/flags.h"
#include "yb/util/flags/flags_callback.h"
#include "yb/util/stack_trace.h"
#include "yb/util/unique_lock.h"

DEFINE_RUNTIME_bool(track_stack_traces, yb::kIsDebug, "Whether to enable stack trace tracking");

namespace yb {

namespace {

class ThreadStackTraceTracker;

class GlobalStackTraceTracker {
 public:
  GlobalStackTraceTracker(): last_reset_(MonoTime::Now()) {}

  void RegisterThread(std::thread::id id, ThreadStackTraceTracker* tracker) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    thread_trackers_[id] = tracker;
  }

  Status UnregisterThread(std::thread::id id) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    auto itr = thread_trackers_.find(id);
    SCHECK(itr != thread_trackers_.end(), NotFound, "Tracker not registered for thread");
    MergeLocalTracker(itr->second);
    thread_trackers_.erase(itr);
    return Status::OK();
  }

  std::vector<StackTraceEntry> GetTrackedStackTraces() EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    for (auto& [_, tracker] : thread_trackers_) {
      MergeLocalTracker(tracker);
    }

    std::vector<StackTraceEntry> tracked;
    tracked.reserve(traces_.size());
    for (auto& [frames, entry] : traces_) {
      if (entry.symbolized_trace.empty()) {
        auto s = StackTrace::MakeStackTrace(frames);
        if (!s.ok()) {
          LOG(ERROR) << "Bad stack trace frames: "
                     << Slice(frames.data(), frames.size()).ToDebugString();
        }
        entry.symbolized_trace = s->Symbolize();
      }
      tracked.push_back(entry);
    }
    return tracked;
  }

  MonoTime GetLastStackTraceTrackerResetTime() EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return last_reset_;
  }

  void ResetTrackedStackTraces() EXCLUDES(mutex_);

 private:
  void MergeLocalTracker(ThreadStackTraceTracker* tracker) REQUIRES(mutex_);

  std::mutex mutex_;
  MonoTime last_reset_ GUARDED_BY(mutex_);
  UnorderedStringMap<StackTraceEntry> traces_ GUARDED_BY(mutex_);
  std::unordered_map<std::thread::id, ThreadStackTraceTracker*> thread_trackers_ GUARDED_BY(mutex_);
};

static GlobalStackTraceTracker global_tracker;

void TrackStackTraceToggleCallback() {
  if (GetAtomicFlag(&FLAGS_track_stack_traces)) {
    global_tracker.ResetTrackedStackTraces();
  }
}

REGISTER_CALLBACK(track_stack_traces, "Stack Trace Toggle Callback", TrackStackTraceToggleCallback);

// This class is thread local and accessed by one thread only, except for when we
// MergeToGlobal/Reset.
class ThreadStackTraceTracker {
 public:
  ThreadStackTraceTracker() {
    global_tracker.RegisterThread(std::this_thread::get_id(), this);
  }

  ~ThreadStackTraceTracker() {
    auto id = std::this_thread::get_id();
    auto s = global_tracker.UnregisterThread(id);
    if (!s.ok()) {
      LOG(WARNING) << s;
    }
  }

  void Trace(StackTraceTrackingGroup group, size_t weight) EXCLUDES(mutex_) {
    StackTrace trace;
    trace.Collect();

    auto key = trace.as_string_view();

    std::lock_guard lock(mutex_);
    auto itr = counts_.find(key);
    if (itr == counts_.end()) {
      itr = counts_.emplace(key, LocalCounts{ .group = group, .count = 0, .weight = 0 }).first;
    }
    itr->second.count += 1;
    itr->second.weight += weight;
  }

  void Reset() EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    for (auto& [_, counts] : counts_) {
      counts.count = 0;
      counts.weight = 0;
    }
  }

 private:
  friend class GlobalStackTraceTracker;

  struct LocalCounts {
    StackTraceTrackingGroup group;
    size_t count;
    size_t weight;
  };

  std::mutex mutex_;
  UnorderedStringMap<LocalCounts> counts_ GUARDED_BY(mutex_);
};

thread_local ThreadStackTraceTracker thread_tracker;

void GlobalStackTraceTracker::ResetTrackedStackTraces() {
  std::lock_guard lock(mutex_);
  for (auto& [_, tracker] : thread_trackers_) {
    tracker->Reset();
  }

  // We set count/weight to 0 instead of just clearing the traces_ map to avoid resymbolizing
  // traces, and because the entries are likely to be recreated soon after anyways.
  for (auto& [_, entry] : traces_) {
    entry.count = 0;
    entry.weight = 0;
  }

  last_reset_ = MonoTime::Now();
}

void GlobalStackTraceTracker::MergeLocalTracker(ThreadStackTraceTracker* tracker) {
  std::lock_guard tracker_lock(tracker->mutex_);
  for (auto& [trace, counts] : tracker->counts_) {
    auto itr = traces_.find(trace);
    if (itr == traces_.end()) {
      itr = traces_.emplace(trace, StackTraceEntry{
        .group = counts.group,
        .symbolized_trace = "",
        .count = 0,
        .weight = 0,
      }).first;
    }
    itr->second.count += std::exchange(counts.count, 0);
    itr->second.weight += std::exchange(counts.weight, 0);
  }
}

} // namespace

void TrackStackTrace(StackTraceTrackingGroup group, size_t weight) {
  if (GetAtomicFlag(&FLAGS_track_stack_traces)) {
    thread_tracker.Trace(group, weight);
  }
}

void ResetTrackedStackTraces() {
  global_tracker.ResetTrackedStackTraces();
}

MonoTime GetLastStackTraceTrackerResetTime() {
  return global_tracker.GetLastStackTraceTrackerResetTime();
}

std::vector<StackTraceEntry> GetTrackedStackTraces() {
  return global_tracker.GetTrackedStackTraces();
}

void DumpTrackedStackTracesToLog(StackTraceTrackingGroup group) {
  auto traces = GetTrackedStackTraces();
  std::sort(traces.begin(), traces.end(),
            [](const auto& left, const auto& right) { return left.count > right.count; });
  size_t count = 0;
  for (const auto& entry : traces) {
    if (entry.count == 0 || entry.group != group) {
      continue;
    }
    LOG(INFO) << "Tracked stack trace: count=" << entry.count << " weight=" << entry.weight
              << '\n' << entry.symbolized_trace;
    ++count;
  }
  if (!count) {
    LOG(INFO) << "No tracked stack traces in group: " << AsString(group);
  }
}

} // namespace yb
