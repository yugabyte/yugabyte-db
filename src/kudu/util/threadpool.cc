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

#include <boost/function.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <limits>
#include <string>

#include "kudu/gutil/callback.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/sysinfo.h"
#include "kudu/util/metrics.h"
#include "kudu/util/thread.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/trace.h"

namespace kudu {

using strings::Substitute;

////////////////////////////////////////////////////////
// FunctionRunnable
////////////////////////////////////////////////////////

class FunctionRunnable : public Runnable {
 public:
  explicit FunctionRunnable(boost::function<void()> func) : func_(std::move(func)) {}

  void Run() OVERRIDE {
    func_();
  }

 private:
  boost::function<void()> func_;
};

////////////////////////////////////////////////////////
// ThreadPoolBuilder
////////////////////////////////////////////////////////

ThreadPoolBuilder::ThreadPoolBuilder(std::string name)
    : name_(std::move(name)),
      min_threads_(0),
      max_threads_(base::NumCPUs()),
      max_queue_size_(std::numeric_limits<int>::max()),
      idle_timeout_(MonoDelta::FromMilliseconds(500)) {}

ThreadPoolBuilder& ThreadPoolBuilder::set_min_threads(int min_threads) {
  CHECK_GE(min_threads, 0);
  min_threads_ = min_threads;
  return *this;
}

ThreadPoolBuilder& ThreadPoolBuilder::set_max_threads(int max_threads) {
  CHECK_GT(max_threads, 0);
  max_threads_ = max_threads;
  return *this;
}

ThreadPoolBuilder& ThreadPoolBuilder::set_max_queue_size(int max_queue_size) {
  CHECK_GT(max_queue_size, 0);
  max_queue_size_ = max_queue_size;
  return *this;
}

ThreadPoolBuilder& ThreadPoolBuilder::set_idle_timeout(const MonoDelta& idle_timeout) {
  idle_timeout_ = idle_timeout;
  return *this;
}

Status ThreadPoolBuilder::Build(gscoped_ptr<ThreadPool>* pool) const {
  pool->reset(new ThreadPool(*this));
  RETURN_NOT_OK((*pool)->Init());
  return Status::OK();
}

////////////////////////////////////////////////////////
// ThreadPool
////////////////////////////////////////////////////////

ThreadPool::ThreadPool(const ThreadPoolBuilder& builder)
  : name_(builder.name_),
    min_threads_(builder.min_threads_),
    max_threads_(builder.max_threads_),
    max_queue_size_(builder.max_queue_size_),
    idle_timeout_(builder.idle_timeout_),
    pool_status_(Status::Uninitialized("The pool was not initialized.")),
    idle_cond_(&lock_),
    no_threads_cond_(&lock_),
    not_empty_(&lock_),
    num_threads_(0),
    active_threads_(0),
    queue_size_(0) {
}

ThreadPool::~ThreadPool() {
  Shutdown();
}

Status ThreadPool::Init() {
  MutexLock unique_lock(lock_);
  if (!pool_status_.IsUninitialized()) {
    return Status::NotSupported("The thread pool is already initialized");
  }
  pool_status_ = Status::OK();
  for (int i = 0; i < min_threads_; i++) {
    Status status = CreateThreadUnlocked();
    if (!status.ok()) {
      Shutdown();
      return status;
    }
  }
  return Status::OK();
}

void ThreadPool::ClearQueue() {
  for (QueueEntry& e : queue_) {
    if (e.trace) {
      e.trace->Release();
    }
  }
  queue_.clear();
  queue_size_ = 0;
}

void ThreadPool::Shutdown() {
  MutexLock unique_lock(lock_);
  pool_status_ = Status::ServiceUnavailable("The pool has been shut down.");
  ClearQueue();
  not_empty_.Broadcast();

  // The Runnable doesn't have Abort() so we must wait
  // and hopefully the abort is done outside before calling Shutdown().
  while (num_threads_ > 0) {
    no_threads_cond_.Wait();
  }
}

Status ThreadPool::SubmitClosure(const Closure& task) {
  // TODO: once all uses of boost::bind-based tasks are dead, implement this
  // in a more straight-forward fashion.
  return SubmitFunc(boost::bind(&Closure::Run, task));
}

Status ThreadPool::SubmitFunc(const boost::function<void()>& func) {
  return Submit(std::shared_ptr<Runnable>(new FunctionRunnable(func)));
}

Status ThreadPool::Submit(const std::shared_ptr<Runnable>& task) {
  MonoTime submit_time = MonoTime::Now(MonoTime::FINE);

  MutexLock guard(lock_);
  if (PREDICT_FALSE(!pool_status_.ok())) {
    return pool_status_;
  }

  // Size limit check.
  if (queue_size_ == max_queue_size_) {
    return Status::ServiceUnavailable(Substitute("Thread pool queue is full ($0 items)",
                                                 queue_size_));
  }

  // Should we create another thread?
  // We assume that each current inactive thread will grab one item from the
  // queue.  If it seems like we'll need another thread, we create one.
  // In theory, a currently active thread could finish immediately after this
  // calculation.  This would mean we created a thread we didn't really need.
  // However, this race is unavoidable, since we don't do the work under a lock.
  // It's also harmless.
  //
  // Of course, we never create more than max_threads_ threads no matter what.
  int inactive_threads = num_threads_ - active_threads_;
  int additional_threads = (queue_size_ + 1) - inactive_threads;
  if (additional_threads > 0 && num_threads_ < max_threads_) {
    Status status = CreateThreadUnlocked();
    if (!status.ok()) {
      if (num_threads_ == 0) {
        // If we have no threads, we can't do any work.
        return status;
      } else {
        // If we failed to create a thread, but there are still some other
        // worker threads, log a warning message and continue.
        LOG(WARNING) << "Thread pool failed to create thread: "
                     << status.ToString();
      }
    }
  }

  QueueEntry e;
  e.runnable = task;
  e.trace = Trace::CurrentTrace();
  // Need to AddRef, since the thread which submitted the task may go away,
  // and we don't want the trace to be destructed while waiting in the queue.
  if (e.trace) {
    e.trace->AddRef();
  }
  e.submit_time = submit_time;

  queue_.push_back(e);
  int length_at_submit = queue_size_++;

  guard.Unlock();
  not_empty_.Signal();

  if (queue_length_histogram_) {
    queue_length_histogram_->Increment(length_at_submit);
  }

  return Status::OK();
}

void ThreadPool::Wait() {
  MutexLock unique_lock(lock_);
  while ((!queue_.empty()) || (active_threads_ > 0)) {
    idle_cond_.Wait();
  }
}

bool ThreadPool::WaitUntil(const MonoTime& until) {
  MonoDelta relative = until.GetDeltaSince(MonoTime::Now(MonoTime::FINE));
  return WaitFor(relative);
}

bool ThreadPool::WaitFor(const MonoDelta& delta) {
  MutexLock unique_lock(lock_);
  while ((!queue_.empty()) || (active_threads_ > 0)) {
    if (!idle_cond_.TimedWait(delta)) {
      return false;
    }
  }
  return true;
}


void ThreadPool::SetQueueLengthHistogram(const scoped_refptr<Histogram>& hist) {
  queue_length_histogram_ = hist;
}

void ThreadPool::SetQueueTimeMicrosHistogram(const scoped_refptr<Histogram>& hist) {
  queue_time_us_histogram_ = hist;
}

void ThreadPool::SetRunTimeMicrosHistogram(const scoped_refptr<Histogram>& hist) {
  run_time_us_histogram_ = hist;
}


void ThreadPool::DispatchThread(bool permanent) {
  MutexLock unique_lock(lock_);
  while (true) {
    // Note: Status::Aborted() is used to indicate normal shutdown.
    if (!pool_status_.ok()) {
      VLOG(2) << "DispatchThread exiting: " << pool_status_.ToString();
      break;
    }

    if (queue_.empty()) {
      if (permanent) {
        not_empty_.Wait();
      } else {
        if (!not_empty_.TimedWait(idle_timeout_)) {
          // After much investigation, it appears that pthread condition variables have
          // a weird behavior in which they can return ETIMEDOUT from timed_wait even if
          // another thread did in fact signal. Apparently after a timeout there is some
          // brief period during which another thread may actually grab the internal mutex
          // protecting the state, signal, and release again before we get the mutex. So,
          // we'll recheck the empty queue case regardless.
          if (queue_.empty()) {
            VLOG(3) << "Releasing worker thread from pool " << name_ << " after "
                    << idle_timeout_.ToMilliseconds() << "ms of idle time.";
            break;
          }
        }
      }
      continue;
    }

    // Fetch a pending task
    QueueEntry entry = queue_.front();
    queue_.pop_front();
    queue_size_--;
    ++active_threads_;

    unique_lock.Unlock();

    // Update metrics
    if (queue_time_us_histogram_) {
      MonoTime now(MonoTime::Now(MonoTime::FINE));
      queue_time_us_histogram_->Increment(now.GetDeltaSince(entry.submit_time).ToMicroseconds());
    }

    ADOPT_TRACE(entry.trace);
    // Release the reference which was held by the queued item.
    if (entry.trace) {
      entry.trace->Release();
    }
    // Execute the task
    {
      ScopedLatencyMetric m(run_time_us_histogram_.get());
      entry.runnable->Run();
    }
    unique_lock.Lock();

    if (--active_threads_ == 0) {
      idle_cond_.Broadcast();
    }
  }

  // It's important that we hold the lock between exiting the loop and dropping
  // num_threads_. Otherwise it's possible someone else could come along here
  // and add a new task just as the last running thread is about to exit.
  CHECK(unique_lock.OwnsLock());

  if (--num_threads_ == 0) {
    no_threads_cond_.Broadcast();

    // Sanity check: if we're the last thread exiting, the queue ought to be
    // empty. Otherwise it will never get processed.
    CHECK(queue_.empty());
    DCHECK_EQ(0, queue_size_);
  }
}

Status ThreadPool::CreateThreadUnlocked() {
  // The first few threads are permanent, and do not time out.
  bool permanent = (num_threads_ < min_threads_);
  Status s = kudu::Thread::Create("thread pool", strings::Substitute("$0 [worker]", name_),
                                  &ThreadPool::DispatchThread, this, permanent, nullptr);
  if (s.ok()) {
    num_threads_++;
  }
  return s;
}

} // namespace kudu
