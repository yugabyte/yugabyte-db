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

#pragma once

#include <semaphore.h>

#include <boost/interprocess/sync/interprocess_semaphore.hpp>

#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

#if defined(BOOST_INTERPROCESS_POSIX_PROCESS_SHARED)
class InterprocessSemaphore {
 public:
  explicit InterprocessSemaphore(unsigned int initial_count) {
    int ret = sem_init(&impl_, 1, initial_count);
    CHECK_NE(ret, -1);
  }

  InterprocessSemaphore(const InterprocessSemaphore&) = delete;
  void operator=(const InterprocessSemaphore&) = delete;

  ~InterprocessSemaphore() {
    CHECK_EQ(sem_destroy(&impl_), 0);
  }

  Status Post() {
    return ResToStatus(sem_post(&impl_), "Post");
  }

  Status Wait() {
    return ResToStatus(sem_wait(&impl_), "Wait");
  }

  template<class TimePoint>
  Status TimedWait(const TimePoint &abs_time) {
    // Posix does not support infinity absolute time so handle it here
    if (boost::interprocess::ipcdetail::is_pos_infinity(abs_time)) {
      return Wait();
    }

    auto tspec = boost::interprocess::ipcdetail::timepoint_to_timespec(abs_time);
    int res = sem_timedwait(&impl_, &tspec);
    if (res == 0) {
      return Status::OK();
    }
    if (res > 0) {
      // buggy glibc, copy the returned error code to errno
      errno = res;
    }
    if (errno == ETIMEDOUT) {
      static const Status timed_out_status = STATUS(TimedOut, "Timed out waiting semaphore");
      return timed_out_status;
    }
    if (errno == EINTR) {
      return Status::OK();
    }
    return ResToStatus(res, "TimedWait");
  }

 private:
  static Status ResToStatus(int res, const char* op) {
    if (res == 0) {
      return Status::OK();
    }
    return STATUS_FORMAT(RuntimeError, "$0 on semaphore failed: $1", op, errno);
  }

  sem_t impl_;
};
#else
class InterprocessSemaphore {
 public:
  explicit InterprocessSemaphore(unsigned int initial_count) : impl_(initial_count) {
  }

  Status Post() {
    impl_.post();
    return Status::OK();
  }

  Status Wait() {
    impl_.wait();
    return Status::OK();
  }

  template<class TimePoint>
  Status TimedWait(const TimePoint &abs_time) {
    if (!impl_.timed_wait(abs_time)) {
      static const Status timed_out_status = STATUS(TimedOut, "Timed out waiting semaphore");
      return timed_out_status;
    }
    return Status::OK();
  }

 private:
  boost::interprocess::interprocess_semaphore impl_;
};
#endif

} // namespace yb
