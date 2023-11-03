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

#include "yb/util/stack_trace.h"

#include <execinfo.h>
#include <signal.h>

#ifdef __linux__
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

#include <mutex>

#include "yb/gutil/casts.h"
#include "yb/gutil/hash/city.h"
#include "yb/gutil/linux_syscall_support.h"

#include "yb/util/flags.h"
#include "yb/util/libbacktrace_util.h"
#include "yb/util/lockfree.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/symbolize.h"
#include "yb/util/thread.h"

#if YB_GOOGLE_TCMALLOC
#include <tcmalloc/malloc_extension.h>
#endif

using std::string;

using namespace std::literals;

#if defined(__APPLE__)
typedef sig_t sighandler_t;
#endif

DEFINE_test_flag(bool, disable_thread_stack_collection_wait, false,
    "When set to true, ThreadStacks() will not wait for threads to respond");

// A hack to grab a function from glog.
// For source see e.g. https://github.com/yugabyte/glog/blob/v0.4.0-yb-5/src/stacktrace.h#L57
namespace google {
extern int GetStackTrace(void** result, int max_depth, int skip_count);
}  // namespace google

namespace yb {

namespace {

YB_DEFINE_ENUM(ThreadStackState, (kNone)(kSendFailed)(kReady));

struct ThreadStackEntry : public MPSCQueueEntry<ThreadStackEntry> {
  ThreadIdForStack tid;
  StackTrace stack;
};

class CompletionFlag {
 public:
  void Signal() {
    complete_.store(1, std::memory_order_release);
#ifndef __APPLE__
    sys_futex(reinterpret_cast<int32_t*>(&complete_),
              FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
              INT_MAX, // wake all
              nullptr, nullptr,
              0 /* ignored */);
#endif
  }

  bool TimedWait(MonoDelta timeout) {
    if (complete()) {
      return true;
    }

    auto now = MonoTime::Now();
    auto deadline = now + timeout;
    while (now < deadline) {
#ifndef __APPLE__
      MonoDelta rem = deadline - now;
      struct timespec ts;
      rem.ToTimeSpec(&ts);
      sys_futex(reinterpret_cast<int32_t*>(&complete_),
                FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
                0, // wait if value is still 0
                reinterpret_cast<struct kernel_timespec *>(&ts), nullptr, 0);
#else
      sched_yield();
#endif
      if (complete()) {
        return true;
      }
      now = MonoTime::Now();
    }

    return complete();
  }

  void Reset() {
    complete_.store(0, std::memory_order_release);
  }

  bool complete() const {
    return complete_.load(std::memory_order_acquire);
  }
 private:
  std::atomic<int32_t> complete_ { 0 };
};

// Global structure used to communicate between the signal handler
// and a dumping thread.
struct ThreadStackHelper {
  std::mutex mutex; // Locked by ThreadStacks, so only one could be executed in parallel.

  LockFreeStack<ThreadStackEntry> collected;
  // Reuse previously allocated memory. We expect this size to be merely small, near 152 bytes
  // per application thread.
  LockFreeStack<ThreadStackEntry> allocated;
  CompletionFlag completion_flag;

  // Could be modified only by ThreadStacks.
  CoarseTimePoint deadline;
  size_t allocated_entries = 0;

  // Incremented by each signal handler.
  std::atomic<int64_t> left_to_collect{0};

  std::vector<std::unique_ptr<ThreadStackEntry[]>> allocated_chunks;

  void SetNumEntries(size_t len) {
    len += 5; // We reserve several entries, because threads from previous request could still be
              // processing signal and write their results.
    if (len <= allocated_entries) {
      return;
    }

    size_t new_chunk_size = std::max<size_t>(len - allocated_entries, 0x10);
    allocated_chunks.emplace_back(new ThreadStackEntry[new_chunk_size]);
    allocated_entries += new_chunk_size;

    for (auto entry = allocated_chunks.back().get(), end = entry + new_chunk_size; entry != end;
         ++entry) {
      allocated.Push(entry);
    }
  }

  void StoreResult(
      const std::vector<ThreadIdForStack>& tids, std::vector<Result<StackTrace>>* out) {
    // We give the thread ~1s to respond. In testing, threads typically respond within
    // a few iterations of the loop, so this timeout is very conservative.
    //
    // The main reason that a thread would not respond is that it has blocked signals. For
    // example, we may be creating a new thread, or glibc's timer_thread doesn't respond to our
    // signal.
    if (left_to_collect.load(std::memory_order_acquire) > 0 &&
        !FLAGS_TEST_disable_thread_stack_collection_wait) {
      completion_flag.TimedWait(1s);
    }

    while (auto entry = collected.Pop()) {
      auto it = std::lower_bound(tids.begin(), tids.end(), entry->tid);
      if (it != tids.end() && *it == entry->tid) {
        auto& entry_out = (*out)[it - tids.begin()];

        if (entry->stack) {
          entry_out = entry->stack;
        } else {
          // If the thread is in the middle of collecting stack trace for any other reason then it
          // will return an empty output.
          static const Status status = STATUS(
              TryAgain,
              "Thread did not respond: maybe it was in the middle of a stack trace collection");
          entry_out = status;
        }
      }
      allocated.Push(entry);
    }
  }

  void RecordStackTrace(const StackTrace& stack_trace) {
    auto* entry = allocated.Pop();
    // If entry is nullptr, that means there are not enough allocated entries. In that case, don't
    // write a log message since we are in a signal handler.
    if (entry) {
      entry->tid = Thread::CurrentThreadIdForStack();
      entry->stack = stack_trace;
      collected.Push(entry);
    }

    if (left_to_collect.fetch_sub(1, std::memory_order_acq_rel) - 1 <= 0) {
      completion_flag.Signal();
    }
  }
};

ThreadStackHelper thread_stack_helper;

// Signal handler for our stack trace signal.
// We expect that the signal is only sent from DumpThreadStack() -- not by a user.

void HandleStackTraceSignal(int signum) {
  int old_errno = errno;
  StackTrace stack_trace;
#if YB_GOOGLE_TCMALLOC
  // TODO(#17889): retry in this case. For now, just produce an empty stack trace.
  if (tcmalloc::MallocExtension::IsCurThreadInAllocDealloc()) {
    thread_stack_helper.RecordStackTrace(stack_trace);
    errno = old_errno;
    return;
  }
#endif
  stack_trace.Collect(2);

  thread_stack_helper.RecordStackTrace(stack_trace);
  errno = old_errno;
}

// The signal that we'll use to communicate with our other threads.
// This can't be in used by other libraries in the process.
int g_stack_trace_signum = SIGUSR2;

bool InitSignalHandlerUnlocked(int signum) {
  enum InitState {
    UNINITIALIZED,
    INIT_ERROR,
    INITIALIZED
  };
  static InitState state = UNINITIALIZED;

  // If we've already registered a handler, but we're being asked to
  // change our signal, unregister the old one.
  if (signum != g_stack_trace_signum && state == INITIALIZED) {
    struct sigaction old_act;
    PCHECK(sigaction(g_stack_trace_signum, nullptr, &old_act) == 0);
    if (old_act.sa_handler == &HandleStackTraceSignal) {
      signal(g_stack_trace_signum, SIG_DFL);
    }
  }

  // If we'd previously had an error, but the signal number
  // is changing, we should mark ourselves uninitialized.
  if (signum != g_stack_trace_signum) {
    g_stack_trace_signum = signum;
    state = UNINITIALIZED;
  }

  if (state == UNINITIALIZED) {
    struct sigaction old_act;
    PCHECK(sigaction(g_stack_trace_signum, nullptr, &old_act) == 0);
    if (old_act.sa_handler != SIG_DFL &&
        old_act.sa_handler != SIG_IGN) {
      state = INIT_ERROR;
      LOG(WARNING) << "signal handler for stack trace signal "
                   << g_stack_trace_signum
                   << " is already in use: "
                   << "YB will not produce thread stack traces.";
    } else {
      // No one appears to be using the signal. This is racy, but there is no
      // atomic swap capability.
      sighandler_t old_handler = signal(g_stack_trace_signum, HandleStackTraceSignal);
      if (old_handler != SIG_IGN &&
          old_handler != SIG_DFL) {
        LOG(FATAL) << "raced against another thread installing a signal handler";
      }
      state = INITIALIZED;
    }
  }
  return state == INITIALIZED;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// StackTrace class
// ------------------------------------------------------------------------------------------------

void StackTrace::Collect(int skip_frames) {
  static thread_local bool is_collecting_stack = false;

  if (is_collecting_stack) {
    // It is unsafe to call backtrace recursively. Return an empty stack trace.
    // A thread can get here if while it was collecting its own stack, it got interrupted by a
    // StackTraceSignal request from another thread.
    return;
  }

  is_collecting_stack = true;
  auto se = ScopeExit([]() { is_collecting_stack = false; });

#if THREAD_SANITIZER || ADDRESS_SANITIZER
  num_frames_ = google::GetStackTrace(frames_, arraysize(frames_), skip_frames);
#else
  int max_frames = skip_frames + arraysize(frames_);
  void** buffer = static_cast<void**>(alloca((max_frames) * sizeof(void*)));
  num_frames_ = backtrace(buffer, max_frames);
  if (num_frames_ > skip_frames) {
    num_frames_ -= skip_frames;
    memmove(frames_, buffer + skip_frames, num_frames_ * sizeof(void*));
  } else {
    num_frames_ = 0;
  }
#endif
}

void StackTrace::StringifyToHex(char* buf, size_t size, int flags) const {
  char* dst = buf;

  // Reserve kHexEntryLength for the first iteration of the loop, 1 byte for a
  // space (which we may not need if there's just one frame), and 1 for a nul
  // terminator.
  char* limit = dst + size - kHexEntryLength - 2;
  for (int i = 0; i < num_frames_ && dst < limit; i++) {
    if (i != 0) {
      *dst++ = ' ';
    }
    // See note in Symbolize() below about why we subtract 1 from each address here.
    uintptr_t addr = reinterpret_cast<uintptr_t>(frames_[i]);
    if (!(flags & NO_FIX_CALLER_ADDRESSES)) {
      addr--;
    }
    FastHex64ToBuffer(addr, dst);
    dst += kHexEntryLength;
  }
  *dst = '\0';
}

std::string StackTrace::ToHexString(int flags) const {
  // Each frame requires kHexEntryLength, plus a space
  // We also need one more byte at the end for '\0'
  char buf[kMaxFrames * (kHexEntryLength + 1) + 1];
  StringifyToHex(buf, arraysize(buf), flags);
  return std::string(buf);
}

// Symbolization function borrowed from glog and modified to use libbacktrace on Linux.
std::string StackTrace::Symbolize(
    const StackTraceLineFormat stack_trace_line_format, StackTraceGroup* group) const {
  std::string buf;
  auto* global_backtrace_state = libbacktrace::GetGlobalBacktraceState();

  if (group) {
    *group = StackTraceGroup::kActive;
  }

  for (int i = 0; i < num_frames_; i++) {
    void* const pc = frames_[i];

    SymbolizeAddress(stack_trace_line_format, pc, &buf, group, global_backtrace_state);
  }

  return buf;
}

string StackTrace::ToLogFormatHexString() const {
  string buf;
  for (int i = 0; i < num_frames_; i++) {
    void* pc = frames_[i];
    StringAppendF(&buf, "    @ %*p\n", kPrintfPointerFieldWidth, pc);
  }
  return buf;
}

uint64_t StackTrace::HashCode() const {
  return util_hash::CityHash64(reinterpret_cast<const char*>(frames_),
                               sizeof(frames_[0]) * num_frames_);
}

// ------------------------------------------------------------------------------------------------

Result<StackTrace> ThreadStack(ThreadIdForStack tid) {
  return ThreadStacks({tid}).front();
}

std::vector<Result<StackTrace>> ThreadStacks(const std::vector<ThreadIdForStack>& tids) {
  static const Status status = STATUS(
      RuntimeError, "Thread did not respond: maybe it is blocking signals");

  std::vector<Result<StackTrace>> result(tids.size(), status);
  std::lock_guard execution_lock(thread_stack_helper.mutex);

  // Ensure that our signal handler is installed. We don't need any fancy GoogleOnce here
  // because of the mutex above.
  if (!InitSignalHandlerUnlocked(g_stack_trace_signum)) {
    static const Status status = STATUS(
        RuntimeError, "Unable to take thread stack: signal handler unavailable");
    std::fill_n(result.begin(), tids.size(), status);
    return result;
  }

  thread_stack_helper.left_to_collect.store(tids.size(), std::memory_order_release);
  thread_stack_helper.SetNumEntries(tids.size());
  thread_stack_helper.completion_flag.Reset();

  for (size_t i = 0; i != tids.size(); ++i) {
    // We use the raw syscall here instead of kill() to ensure that we don't accidentally
    // send a signal to some other process in the case that the thread has exited and
    // the TID been recycled.
#if defined(__linux__)
    int res = narrow_cast<int>(syscall(SYS_tgkill, getpid(), tids[i], g_stack_trace_signum));
#else
    int res = pthread_kill(tids[i], g_stack_trace_signum);
#endif
    if (res != 0) {
      static const Status status = STATUS(
          RuntimeError, "Unable to deliver signal: process may have exited");
      result[i] = status;
      thread_stack_helper.left_to_collect.fetch_sub(1, std::memory_order_acq_rel);
    }
  }

  thread_stack_helper.StoreResult(tids, &result);

  return result;
}

std::string DumpThreadStack(ThreadIdForStack tid) {
  auto stack_trace = ThreadStack(tid);
  if (!stack_trace.ok()) {
    return stack_trace.status().message().ToBuffer();
  }
  return stack_trace->Symbolize();
}

Status SetStackTraceSignal(int signum) {
  std::lock_guard lock(thread_stack_helper.mutex);
  if (!InitSignalHandlerUnlocked(signum)) {
    return STATUS(InvalidArgument, "Unable to install signal handler");
  }
  return Status::OK();
}

int GetStackTraceSignal() {
  // Only tests modify this value when multiple threads run, so this is safe.
  return ANNOTATE_UNPROTECTED_READ(g_stack_trace_signum);
}

}  // namespace yb
