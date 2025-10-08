// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
/* Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The following only applies to changes made to this file as part of YugabyteDB development.
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 *
 * ---
 * This file is a Linux-specific part of spinlock_internal.cc
 */

#include <sched.h>
#include <time.h>
#include <limits.h>
#include "yb/gutil/linux_syscall_support.h"

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128

static bool have_futex;
static int futex_private_flag = FUTEX_PRIVATE_FLAG;

namespace {
static struct InitModule {
  InitModule() {
    int x = 0;
    // futexes are ints, so we can use them only when
    // that's the same size as the lockword_ in SpinLock.
    have_futex = (sizeof (Atomic32) == sizeof (int) &&
                  sys_futex(&x, FUTEX_WAKE, 1, nullptr, nullptr, 0) >= 0);
    if (have_futex &&
        sys_futex(&x, FUTEX_WAKE | futex_private_flag, 1, nullptr, nullptr, 0) < 0) {
      futex_private_flag = 0;
    }
  }
} init_module;

}  // anonymous namespace


namespace yb {
namespace base {
namespace internal {

void SpinLockDelay(volatile Atomic32 *w, int32 value, int loop) {
  if (loop != 0) {
    int save_errno = errno;
    struct timespec tm;
    tm.tv_sec = 0;
    if (have_futex) {
      tm.tv_nsec = yb::base::internal::SuggestedDelayNS(loop);
    } else {
      tm.tv_nsec = 2000001;   // above 2ms so linux 2.4 doesn't spin
    }
    if (have_futex) {
      tm.tv_nsec *= 16;  // increase the delay; we expect explicit wakeups
      sys_futex(reinterpret_cast<int *>(const_cast<Atomic32 *>(w)),
                FUTEX_WAIT | futex_private_flag,
                value, reinterpret_cast<struct kernel_timespec *>(&tm),
                nullptr, 0);
    } else {
      nanosleep(&tm, nullptr);
    }
    errno = save_errno;
  }
}

void SpinLockWake(volatile Atomic32 *w, bool all) {
  if (have_futex) {
    sys_futex(reinterpret_cast<int *>(const_cast<Atomic32 *>(w)),
              FUTEX_WAKE | futex_private_flag, all? INT_MAX : 1,
              nullptr, nullptr, 0);
  }
}

} // namespace internal
} // namespace base
} // namespace yb
