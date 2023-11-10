// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
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
#include "yb/util/condition_variable.h"

#include "yb/util/logging.h"

#include "yb/util/errno.h"
#include "yb/util/monotime.h"
#include "yb/util/thread_restrictions.h"

using std::string;

namespace yb {

ConditionVariable::ConditionVariable(Mutex* user_lock)
    : user_mutex_(&user_lock->native_handle_)
#if !defined(NDEBUG)
    , user_lock_(user_lock)
#endif
{
  int rv = 0;
  // http://crbug.com/293736
  // NaCl doesn't support monotonic clock based absolute deadlines.
  // On older Android platform versions, it's supported through the
  // non-standard pthread_cond_timedwait_monotonic_np. Newer platform
  // versions have pthread_condattr_setclock.
  // Mac can use relative time deadlines.
#if !defined(__APPLE__) && !defined(OS_NACL) && \
      !(defined(OS_ANDROID) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC))
  pthread_condattr_t attrs;
  rv = pthread_condattr_init(&attrs);
  DCHECK_EQ(0, rv);
  pthread_condattr_setclock(&attrs, CLOCK_MONOTONIC);
  rv = pthread_cond_init(&condition_, &attrs);
  pthread_condattr_destroy(&attrs);
#else
  rv = pthread_cond_init(&condition_, nullptr);
#endif
  DCHECK_EQ(0, rv) << ErrnoToString(rv);
}

ConditionVariable::~ConditionVariable() {
#if defined(OS_MACOSX)
  // This hack is necessary to avoid a fatal pthreads subsystem bug in the
  // Darwin kernel. https://codereview.chromium.org/1323293005/
  {
    Mutex lock;
    MutexLock l(lock);
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1;
    pthread_cond_timedwait_relative_np(&condition_, &lock.native_handle_, &ts);
  }
#endif
  int rv = pthread_cond_destroy(&condition_);
  DCHECK_EQ(0, rv) << ErrnoToString(rv);
}

void ConditionVariable::Wait() const {
  ThreadRestrictions::AssertWaitAllowed();
#if !defined(NDEBUG)
  user_lock_->CheckHeldAndUnmark();
#endif
  int rv = pthread_cond_wait(&condition_, user_mutex_);
  DCHECK_EQ(0, rv) << ErrnoToString(rv);
#if !defined(NDEBUG)
  user_lock_->CheckUnheldAndMark();
#endif
}

bool ConditionVariable::WaitUntil(const MonoTime& wait_timeout_deadline) const {
  return TimedWait(wait_timeout_deadline - MonoTime::Now());
}

bool ConditionVariable::TimedWait(const MonoDelta& max_time) const {
  ThreadRestrictions::AssertWaitAllowed();

  // Negative delta means we've already timed out.
  int64 nsecs = max_time.ToNanoseconds();
  if (nsecs < 0) {
    return false;
  }

  struct timespec relative_time;
  max_time.ToTimeSpec(&relative_time);

#if !defined(NDEBUG)
  user_lock_->CheckHeldAndUnmark();
#endif

#if defined(__APPLE__)
  int rv = pthread_cond_timedwait_relative_np(
      &condition_, user_mutex_, &relative_time);
#else
  // The timeout argument to pthread_cond_timedwait is in absolute time.
  struct timespec absolute_time;
#if defined(OS_NACL)
  // See comment in constructor for why this is different in NaCl.
  struct timeval now;
  gettimeofday(&now, NULL);
  absolute_time.tv_sec = now.tv_sec;
  absolute_time.tv_nsec = now.tv_usec * MonoTime::kNanosecondsPerMicrosecond;
#else
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  absolute_time.tv_sec = now.tv_sec;
  absolute_time.tv_nsec = now.tv_nsec;
#endif

  absolute_time.tv_sec += relative_time.tv_sec;
  absolute_time.tv_nsec += relative_time.tv_nsec;
  absolute_time.tv_sec += absolute_time.tv_nsec / MonoTime::kNanosecondsPerSecond;
  absolute_time.tv_nsec %= MonoTime::kNanosecondsPerSecond;
  DCHECK_GE(absolute_time.tv_sec, now.tv_sec);  // Overflow paranoia

#if defined(OS_ANDROID) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC)
  int rv = pthread_cond_timedwait_monotonic_np(
      &condition_, user_mutex_, &absolute_time);
#else
  int rv = pthread_cond_timedwait(&condition_, user_mutex_, &absolute_time);
#endif  // OS_ANDROID && HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC
#endif  // __APPLE__

  DCHECK(rv == 0 || rv == ETIMEDOUT)
    << "unexpected pthread_cond_timedwait return value: " << rv;
#if !defined(NDEBUG)
  user_lock_->CheckUnheldAndMark();
#endif
  return rv == 0;
}

void ConditionVariable::Broadcast() {
  int rv = pthread_cond_broadcast(&condition_);
  DCHECK_EQ(0, rv) << ErrnoToString(rv);
}

void ConditionVariable::Signal() {
  int rv = pthread_cond_signal(&condition_);
  DCHECK_EQ(0, rv) << ErrnoToString(rv);
}

}  // namespace yb
