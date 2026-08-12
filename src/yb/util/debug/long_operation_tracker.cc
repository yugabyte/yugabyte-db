// Copyright (c) YugabyteDB, Inc.
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

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

#include "yb/util/debug-util.h"
#include "yb/util/lockfree.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

namespace yb {

// While an operation is running, one reference is held by the LongOperationTracker returned to
// the caller and one is transferred through LongOperationTrackerHelper's lock-free intake queue
// to the checker thread. So HasOneRef() held by the checker thread means the operation has
// completed.
struct LongOperationTracker::TrackedOperation :
    public RefCountedThreadSafe<LongOperationTracker::TrackedOperation>,
    public MPSCQueueEntry<LongOperationTracker::TrackedOperation> {
  ThreadIdForStack thread_id;
  const char* message;
  CoarseTimePoint start;
  // time when we should log warning
  CoarseTimePoint time;

  TrackedOperation(
      ThreadIdForStack thread_id_, const char* message_, CoarseTimePoint start_,
      CoarseTimePoint time_)
      : thread_id(thread_id_), message(message_), start(start_), time(time_) {
  }
};

namespace {

typedef scoped_refptr<LongOperationTracker::TrackedOperation> TrackedOperationPtr;

struct TrackedOperationComparer {
  // Order is reversed, because priority_queue keeps track of the "largest" element.
  bool operator()(const TrackedOperationPtr& lhs, const TrackedOperationPtr& rhs) {
    return lhs->time > rhs->time;
  }
};

// Upper bound on how long the checker thread sleeps between scans of the intake queue. A newly
// registered operation is noticed within this interval, so a warning could be logged at most
// this much later than the operation deadline. Since tracked durations are typically much
// larger than this bound, the delay is not observable in practice.
constexpr std::chrono::milliseconds kMaxWaitTime(100);

// Deadlines below this threshold cannot rely on the periodic scan alone: the operation could
// expire and complete entirely within one kMaxWaitTime sleep, losing the stack trace warning.
// Registration of such operations additionally records a wakeup for the checker thread.
constexpr auto kShortDeadlineThreshold = 2 * kMaxWaitTime;

// Upper bound on the number of registrations adopted from the intake queue in one iteration of
// the checker loop, so that expired operations and stop_ are still checked periodically while
// producers are registering at a high rate.
constexpr size_t kMaxDrainPerIteration = 1000;

// Singleton that maintains queue of tracked operation and runs thread that checks for expired
// operations.
//
// Synchronization strategy: operations are registered through the lock-free intake_ queue, so
// typical registrations do not contend on any mutex. The checker thread is the only consumer
// of intake_ and exclusively owns the priority queue of pending operations (local to Execute).
// mutex_ and cond_ are used to sleep in and wake up the checker thread: the destructor and
// registrations with unusually short deadlines record their wakeup condition under the mutex
// before notifying, so wakeups cannot be lost.
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
      // The lock prevents a lost wakeup: the checker thread cannot be between its stop_ check
      // and cond_.wait_for while we hold the mutex.
      std::lock_guard lock(mutex_);
      stop_.store(true, std::memory_order_release);
    }
    cond_.notify_one();
    if (thread_) {
      thread_->Join();
    }
    // Release references of operations that were registered after the checker thread performed
    // its final drain.
    ReleaseIntakeRefs();
  }

  static LongOperationTrackerHelper& Instance() {
    static LongOperationTrackerHelper result;
    return result;
  }

  TrackedOperationPtr Register(const char* message, MonoDelta duration) {
    if (IsSanitizer()) {
      return TrackedOperationPtr();
    }
    auto start = CoarseMonoClock::now();
    const CoarseTimePoint deadline = start + duration * kTimeMultiplier;
    TrackedOperationPtr result(new LongOperationTracker::TrackedOperation(
        Thread::CurrentThreadIdForStack(), message, start, deadline));
    // Transfer one reference through the intake queue as a raw pointer, keeping registration
    // lock-free. The checker thread adopts it back into a TrackedOperationPtr.
    result->AddRef();
    intake_.Push(result.get());
    if (deadline - start < kShortDeadlineThreshold) {
      // The periodic scan is not frequent enough for this deadline. Record the wakeup under
      // the mutex so that it cannot be lost, then wake the checker thread. Only these unusually
      // short deadlines pay for the mutex.
      {
        std::lock_guard lock(mutex_);
        short_deadline_pending_ = true;
      }
      cond_.notify_one();
    }
    return result;
  }

 private:
  // Wraps a raw pointer popped from intake_ back into a TrackedOperationPtr, dropping the extra
  // reference that was added in Register.
  static TrackedOperationPtr AdoptIntakeRef(LongOperationTracker::TrackedOperation* operation) {
    TrackedOperationPtr result(operation);
    operation->Release();
    return result;
  }

  void ReleaseIntakeRefs() {
    while (auto* operation = intake_.Pop()) {
      operation->Release();
    }
  }

  void Execute() {
    // Owned exclusively by this thread, so no synchronization is needed.
    std::priority_queue<
        TrackedOperationPtr, std::vector<TrackedOperationPtr>, TrackedOperationComparer> queue;

    while (!stop_.load(std::memory_order_acquire)) {
      // Adopt a bounded number of registrations, so that this loop terminates even when
      // producers are registering operations faster than we drain them.
      size_t drained = 0;
      while (drained < kMaxDrainPerIteration) {
        auto* operation = intake_.Pop();
        if (!operation) {
          break;
        }
        queue.push(AdoptIntakeRef(operation));
        ++drained;
      }

      while (!stop_.load(std::memory_order_acquire)) {
        auto now = CoarseMonoClock::now();
        if (queue.empty() || queue.top()->time > now) {
          break;
        }
        TrackedOperationPtr operation = queue.top();
        queue.pop();
        // If we hold the last reference, then the operation has already completed.
        if (!operation->HasOneRef()) {
          auto stack = DumpThreadStack(operation->thread_id);
          // Make sure the task did not complete while we were dumping the stack. Else we could
          // get some other innocent stack.
          if (!operation->HasOneRef()) {
            LOG(WARNING) << operation->message << " running for "
                         << MonoDelta(now - operation->start)
                         << " in thread " << operation->thread_id << ":\n" << stack;
          }
        }
      }

      if (drained == kMaxDrainPerIteration) {
        // The intake queue could have more pending registrations, process them before sleeping.
        continue;
      }

      CoarseDuration wait_time = kMaxWaitTime;
      if (!queue.empty()) {
        wait_time = std::min<CoarseDuration>(
            wait_time, queue.top()->time - CoarseMonoClock::now());
      }

      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_.load(std::memory_order_acquire)) {
          break;
        }
        cond_.wait_for(lock, wait_time, [this] {
          return stop_.load(std::memory_order_acquire) || short_deadline_pending_;
        });
        short_deadline_pending_ = false;
      }
    }

    // References held by queue are released by its destructor.
    ReleaseIntakeRefs();
  }

  MPSCQueue<LongOperationTracker::TrackedOperation> intake_;

  // Used to sleep in and wake up the checker thread, see the synchronization strategy in the
  // class comment. stop_ is atomic so that the checker loops can poll it without the mutex.
  // short_deadline_pending_ is guarded by mutex_ so that short deadline wakeups cannot be lost.
  std::mutex mutex_;
  std::condition_variable cond_;
  std::atomic<bool> stop_{false};
  bool short_deadline_pending_ = false;
  scoped_refptr<Thread> thread_;
};

} // namespace

LongOperationTracker::LongOperationTracker() = default;

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

LongOperationTracker::LongOperationTracker(LongOperationTracker&& rhs) = default;

LongOperationTracker& LongOperationTracker::operator=(LongOperationTracker&& rhs) {
  if (this != &rhs) {
    // scoped_refptr move assignment does not clear the source, so move through a temporary that
    // steals rhs's pointer, then let it release our previous operation.
    scoped_refptr<TrackedOperation> temp(std::move(rhs.tracked_operation_));
    tracked_operation_.swap(temp);
  }
  return *this;
}

void LongOperationTracker::Swap(LongOperationTracker* rhs) {
  tracked_operation_.swap(rhs->tracked_operation_);
}

} // namespace yb
