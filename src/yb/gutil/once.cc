// Copyright 2008 Google Inc. All Rights Reserved.
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

#include "yb/util/logging.h"
#include "yb/gutil/logging-inl.h"
#include "yb/gutil/once.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/spinlock_internal.h"

// All modifications to a GoogleOnceType occur inside GoogleOnceInternalInit.
// The fast path reads the variable with an acquire-load..
// This is safe provided we always perform a memory barrier
// immediately before setting the value to GOOGLE_ONCE_INTERNAL_DONE.

void GoogleOnceInternalInit(Atomic32 *control, void (*func)(),
                            void (*func_with_arg)(void*), void* arg) {
  if (DEBUG_MODE) {
    int64 old_control = base::subtle::Acquire_Load(control);
    if (old_control != GOOGLE_ONCE_INTERNAL_INIT &&
        old_control != GOOGLE_ONCE_INTERNAL_RUNNING &&
        old_control != GOOGLE_ONCE_INTERNAL_WAITER &&
        old_control != GOOGLE_ONCE_INTERNAL_DONE) {
      LOG(FATAL) << "Either GoogleOnceType is used in non-static storage "
                    "(where GoogleOnceDynamic might be appropriate), "
                    "or there's a memory corruption.";
    }
  }
  static const yb::base::internal::SpinLockWaitTransition trans[] = {
    { GOOGLE_ONCE_INTERNAL_INIT, GOOGLE_ONCE_INTERNAL_RUNNING, true },
    { GOOGLE_ONCE_INTERNAL_RUNNING, GOOGLE_ONCE_INTERNAL_WAITER, false },
    { GOOGLE_ONCE_INTERNAL_DONE, GOOGLE_ONCE_INTERNAL_DONE, true }
  };
  // Short circuit the simplest case to avoid procedure call overhead.
  if (::base::subtle::Acquire_CompareAndSwap(control, GOOGLE_ONCE_INTERNAL_INIT,
          GOOGLE_ONCE_INTERNAL_RUNNING) == GOOGLE_ONCE_INTERNAL_INIT ||
      yb::base::internal::SpinLockWait(control, ARRAYSIZE(trans), trans) ==
      GOOGLE_ONCE_INTERNAL_INIT) {
    if (func != nullptr) {
      (*func)();
    } else {
      (*func_with_arg)(arg);
    }
    ANNOTATE_HAPPENS_BEFORE(control);
    int64 old_control = base::subtle::NoBarrier_Load(control);
    ::base::subtle::Release_Store(control, GOOGLE_ONCE_INTERNAL_DONE);
    if (old_control == GOOGLE_ONCE_INTERNAL_WAITER) {
      yb::base::internal::SpinLockWake(control, true);
    }
  } // else *control is already GOOGLE_ONCE_INTERNAL_DONE
}
