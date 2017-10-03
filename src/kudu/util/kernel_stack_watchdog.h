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
// This class defines a singleton thread which manages a map of other thread IDs to
// watch. Before performing some operation which may stall (eg IO) or which we expect
// should be short (e.g. a callback on a critical thread that should not block), threads
// may mark themselves as "watched", with a threshold beyond which they would like
// warnings to be emitted including their stack trace at that time.
//
// In the background, a separate watchdog thread periodically wakes up, and if a thread
// has been marked longer than its provided threshold, it will dump the stack trace
// of that thread (both kernel-mode and user-mode stacks).
//
// This can be useful for diagnosing I/O stalls coming from the kernel, for example.
//
// Users will typically use the macro SCOPED_WATCH_STACK. Example usage:
//
//   // We expect the Write() to return in <100ms. If it takes longer than that
//   // we'll see warnings indicating why it is stalled.
//   {
//     SCOPED_WATCH_STACK(100);
//     file->Write(...);
//   }
//
// If the Write call takes too long, a stack trace will be logged at WARNING level.
// Note that the threshold time parameter is not a guarantee that a stall will be
// caught by the watchdog thread. The watchdog only wakes up periodically to look
// for threads that have been stalled too long. For example, if the threshold is 10ms
// and the thread blocks for only 20ms, it's quite likely that the watchdog will
// have missed the event.
//
// The SCOPED_WATCH_STACK macro is designed to have minimal overhead: approximately
// equivalent to a clock_gettime() and a single 'mfence' instruction. Micro-benchmarks
// measure the cost at about 50ns per call. Thus, it may safely be used in hot code
// paths.
//
// Scopes with SCOPED_WATCH_STACK may be nested, but only up to a hard-coded limited depth
// (currently 8).
#ifndef KUDU_UTIL_KERNEL_STACK_WATCHDOG_H
#define KUDU_UTIL_KERNEL_STACK_WATCHDOG_H

#include <string>
#include <unordered_map>
#include <vector>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/singleton.h"
#include "kudu/gutil/walltime.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/mutex.h"
#include "kudu/util/monotime.h"
#include "kudu/util/threadlocal.h"

#define SCOPED_WATCH_STACK(threshold_ms) \
  ScopedWatchKernelStack _stack_watcher(__FILE__ ":" AS_STRING(__LINE__), threshold_ms)

namespace kudu {

class Thread;

// Singleton thread which implements the watchdog.
class KernelStackWatchdog {
 public:
  static KernelStackWatchdog* GetInstance() {
    return Singleton<KernelStackWatchdog>::get();
  }

  // Instead of logging through glog, log warning messages into a vector.
  //
  // If 'save_logs' is true, will start saving to the vector, and forget any
  // previously logged messages.
  // If 'save_logs' is false, disables this functionality.
  void SaveLogsForTests(bool save_logs);

  // Return any log messages saved since the last call to SaveLogsForTests(true).
  std::vector<std::string> LoggedMessagesForTests() const;

 private:
  friend class Singleton<KernelStackWatchdog>;
  friend class ScopedWatchKernelStack;

  // The thread-local state which captures whether a thread should be watched by
  // the watchdog. This structure is constructed as a thread-local on first use
  // and destructed when the thread exits. Upon construction, the TLS structure
  // registers itself with the WatchDog, and on destruction, unregisters itself.
  //
  // See 'seq_lock_' below for details on thread-safe operation.
  struct TLS {
    TLS();
    ~TLS();

    enum Constants {
      // The maximum nesting depth of SCOPED_WATCH_STACK() macros.
      kMaxDepth = 8
    };

    // Because we support nested SCOPED_WATCH_STACK() macros, we need to capture
    // multiple active frames within the TLS.
    struct Frame {
      // The time at which this frame entered the SCOPED_WATCH_STACK section.
      // We use MicrosecondsInt64 instead of MonoTime because it inlines a bit
      // better.
      MicrosecondsInt64 start_time_;
      // The threshold of time beyond which the watchdog should emit warnings.
      int threshold_ms_;
      // A string explaining the state that the thread is in (typically a file:line
      // string). This is expected to be static storage and is not freed.
      const char* status_;
    };

    // The data within the TLS. This is a POD type so that the watchdog can easily
    // copy data out of a thread's TLS.
    struct Data {
      Frame frames_[kMaxDepth];
      Atomic32 depth_;

      // Counter implementing a simple "sequence lock".
      //
      // Before modifying any data inside its TLS, the watched thread increments this value so it is
      // odd. When the modifications are complete, it increments it again, making it even.
      //
      // To read the TLS data from a target thread, the watchdog thread waits for the value
      // to become even, indicating that no write is in progress. Then, it does a potentially
      // racy copy of the entire 'Data' structure. Then, it validates the value again.
      // If it is has not changed, then the snapshot is guaranteed to be consistent.
      //
      // We use this type of locking to ensure that the watched thread is as fast as possible,
      // allowing us to use SCOPED_WATCH_STACK even in hot code paths. In particular,
      // the watched thread is wait-free, since it doesn't need to loop or retry. In addition, the
      // memory is only written by that thread, eliminating any cache-line bouncing. The watchdog
      // thread may have to loop multiple times to see a consistent snapshot, but we're OK delaying
      // the watchdog arbitrarily since it isn't on any critical path.
      Atomic32 seq_lock_;

      // Take a consistent snapshot of this data into 'dst'. This may block if the target thread
      // is currently modifying its TLS.
      void SnapshotCopy(Data* dst) const;
    };
    Data data_;
  };

  KernelStackWatchdog();
  ~KernelStackWatchdog();

  // Get or create the TLS for the current thread.
  static TLS* GetTLS();

  // Register a new thread's TLS with the watchdog.
  // Called by any thread the first time it enters a watched section, when its TLS
  // is constructed.
  void Register(TLS* tls);

  // Called when a thread's TLS is destructed (i.e. when the thread exits).
  void Unregister(TLS* tls);

  // The actual watchdog loop that the watchdog thread runs.
  void RunThread();

  DECLARE_STATIC_THREAD_LOCAL(TLS, tls_);

  typedef std::unordered_map<pid_t, TLS*> TLSMap;
  TLSMap tls_by_tid_;

  // If non-NULL, warnings will be emitted into this vector instead of glog.
  // Used by tests.
  gscoped_ptr<std::vector<std::string> > log_collector_;

  // Lock protecting tls_by_tid_ and log_collector_.
  mutable Mutex lock_;

  // The watchdog thread itself.
  scoped_refptr<Thread> thread_;

  // Signal to stop the watchdog.
  CountDownLatch finish_;

  DISALLOW_COPY_AND_ASSIGN(KernelStackWatchdog);
};

// Scoped object which marks the current thread for watching.
class ScopedWatchKernelStack {
 public:
  // If the current scope is active more than 'threshold_ms' milliseconds, the
  // watchdog thread will log a warning including the message 'label'. 'label'
  // is not copied or freed.
  ScopedWatchKernelStack(const char* label, int threshold_ms) {
    // Rather than just using the lazy GetTLS() method, we'll first try to load
    // the TLS ourselves. This is usually successful, and avoids us having to inline
    // the TLS construction path at call sites.
    KernelStackWatchdog::TLS* tls = KernelStackWatchdog::tls_;
    if (PREDICT_FALSE(tls == NULL)) {
      tls = KernelStackWatchdog::GetTLS();
    }
    KernelStackWatchdog::TLS::Data* tls_data = &tls->data_;

    // "Acquire" the sequence lock. While the lock value is odd, readers will block.
    // TODO: technically this barrier is stronger than we need: we are the only writer
    // to this data, so it's OK to allow loads from within the critical section to
    // reorder above this next line. All we need is a "StoreStore" barrier (i.e.
    // prevent any stores in the critical section from getting reordered above the
    // increment of the counter). However, atomicops.h doesn't provide such a barrier
    // as of yet, so we'll do the slightly more expensive one for now.
    base::subtle::Acquire_Store(&tls_data->seq_lock_, tls_data->seq_lock_ + 1);

    KernelStackWatchdog::TLS::Frame* frame = &tls_data->frames_[tls_data->depth_++];
    DCHECK_LE(tls_data->depth_, KernelStackWatchdog::TLS::kMaxDepth);
    frame->start_time_ = GetMonoTimeMicros();
    frame->threshold_ms_ = threshold_ms;
    frame->status_ = label;

    // "Release" the sequence lock. This resets the lock value to be even, so readers
    // will proceed.
    base::subtle::Release_Store(&tls_data->seq_lock_, tls_data->seq_lock_ + 1);
  }

  ~ScopedWatchKernelStack() {
    KernelStackWatchdog::TLS::Data* tls = &DCHECK_NOTNULL(KernelStackWatchdog::tls_)->data_;
    int d = tls->depth_;
    DCHECK_GT(d, 0);

    // We don't bother with a lock/unlock, because the change we're making here is atomic.
    // If we race with the watchdog, either they'll see the old depth_ or the new depth_,
    // but in either case the underlying data is perfectly valid.
    base::subtle::NoBarrier_Store(&tls->depth_, d - 1);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ScopedWatchKernelStack);
};

} // namespace kudu
#endif /* KUDU_UTIL_KERNEL_STACK_WATCHDOG_H */
