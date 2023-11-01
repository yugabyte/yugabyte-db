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

#include "yb/util/debug/long_operation_tracker.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "yb/util/debug-util.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

namespace yb {

struct LongOperationTracker::TrackedOperation {
  ThreadIdForStack thread_id;
  const char* message;
  CoarseTimePoint start;
  // time when we should log warning
  CoarseTimePoint time;
  bool complete = false;

  TrackedOperation(
      ThreadIdForStack thread_id_, const char* message_, CoarseTimePoint start_,
      CoarseTimePoint time_)
      : thread_id(thread_id_), message(message_), start(start_), time(time_) {
  }
};

namespace {

typedef std::shared_ptr<LongOperationTracker::TrackedOperation> TrackedOperationPtr;

struct TrackedOperationComparer {
  // Order is reversed, because priority_queue keeps track of the "largest" element.
  bool operator()(const TrackedOperationPtr& lhs, const TrackedOperationPtr& rhs) {
    return lhs->time > rhs->time;
  }
};

// Singleton that maintains queue of tracked operation and runs thread that checks for expired
// operations.
class LongOperationTrackerHelper {
 public:
  LongOperationTrackerHelper() {
    CHECK_OK(Thread::Create(
        "long_operation_tracker", "tracker", &LongOperationTrackerHelper::Execute, this, &thread_));
  }

  LongOperationTrackerHelper(const LongOperationTrackerHelper&) = delete;
  void operator=(const LongOperationTrackerHelper&) = delete;

  ~LongOperationTrackerHelper() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cond_.notify_one();
    if (thread_) {
      thread_->Join();
    }
  }

  static LongOperationTrackerHelper& Instance() {
    static LongOperationTrackerHelper result;
    return result;
  }

  TrackedOperationPtr Register(const char* message, MonoDelta duration) {
    auto start = CoarseMonoClock::now();
    auto result = std::make_shared<LongOperationTracker::TrackedOperation>(
        Thread::CurrentThreadIdForStack(), message, start, start + duration * kTimeMultiplier);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(result);
    }

    cond_.notify_one();

    return result;
  }

 private:
  void Execute() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
      if (queue_.empty()) {
        cond_.wait(lock);
        continue;
      }

      const CoarseTimePoint first_entry_time = queue_.top()->time;

      auto now = CoarseMonoClock::now();
      if (now < first_entry_time) {
        if (cond_.wait_for(lock, first_entry_time - now) != std::cv_status::timeout) {
          continue;
        }
        now = CoarseMonoClock::now();
      }

      TrackedOperationPtr operation = queue_.top();
      queue_.pop();
      if (!operation.unique()) {
        lock.unlock();
        auto stack = DumpThreadStack(operation->thread_id);
        // Make sure the task did not complete while we were dumping the stack. Else we could get
        // some other innocent stack.
        if (!operation.unique()) {
          LOG(WARNING) << operation->message << " running for " << MonoDelta(now - operation->start)
                       << " in thread " << operation->thread_id << ":\n"
                       << stack;
        }
        lock.lock();
      }
    }
  }

  std::priority_queue<
      TrackedOperationPtr, std::vector<TrackedOperationPtr>, TrackedOperationComparer> queue_;

  std::mutex mutex_;
  std::condition_variable cond_;
  bool stop_;
  scoped_refptr<Thread> thread_;
};

} // namespace

LongOperationTracker::LongOperationTracker(const char* message, MonoDelta duration)
    : tracked_operation_(LongOperationTrackerHelper::Instance().Register(message, duration)) {
}

LongOperationTracker::~LongOperationTracker() {
  if (!tracked_operation_) {
    return;
  }
  auto now = CoarseMonoClock::now();
  if (now > tracked_operation_->time) {
    LOG(WARNING) << tracked_operation_->message << " took a long time: "
                 << MonoDelta(now - tracked_operation_->start);
  }
}

} // namespace yb
