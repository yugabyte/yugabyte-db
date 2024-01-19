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
// Some portions: Copyright (c) 2012, The Chromium Authors.
#pragma once

#include "yb/gutil/macros.h"

#ifndef NDEBUG
#define ENABLE_THREAD_RESTRICTIONS 1
#endif

namespace yb {

// Certain behavior is disallowed on certain threads.  ThreadRestrictions helps
// enforce these rules.  Examples of such rules:
//
// * Do not do blocking IO
// * Do not wait on synchronization variables or sleep
//
// Here's more about how the protection works:
//
// 1) If a thread should not be allowed to make IO calls, mark it:
//      ThreadRestrictions::SetIOAllowed(false);
//    By default, threads *are* allowed to make IO calls.
//    In particular, threads like RPC reactors should never do blocking IO
//    because it may stall other unrelated requests.
//
// 2) If a function makes a call that will go out to disk, check whether the
//    current thread is allowed:
//      ThreadRestrictions::AssertIOAllowed();
//
//
// Style tip: where should you put AssertIOAllowed checks?  It's best
// if you put them as close to the disk access as possible, at the
// lowest level.  This rule is simple to follow and helps catch all
// callers.  For example, if your function GoDoSomeBlockingDiskCall()
// only calls other functions in YB and doesn't access the underlying
// disk, you should go add the AssertIOAllowed checks in the helper functions.
class ThreadRestrictions {
 public:
  // Constructing a ScopedAllowIO temporarily allows IO for the current
  // thread.  Doing this is almost certainly always incorrect, but sometimes
  // it makes more sense to allow an exception and file a bug in the backlog
  // to improve it later.
  class ScopedAllowIO {
   public:
    ScopedAllowIO() { previous_value_ = SetIOAllowed(true); }
    ~ScopedAllowIO() { SetIOAllowed(previous_value_); }
   private:
    // Whether IO is allowed when the ScopedAllowIO was constructed.
    bool previous_value_;

    DISALLOW_COPY_AND_ASSIGN(ScopedAllowIO);
  };

  // Constructing a ScopedAllowWait temporarily allows waiting on the current
  // thread.  Doing this is almost always incorrect: consider carefully whether
  // you should instead be deferring work to a different thread.
  class ScopedAllowWait {
   public:
    ScopedAllowWait() { previous_value_ = SetWaitAllowed(true); }
    ~ScopedAllowWait() { SetWaitAllowed(previous_value_); }
   private:
    // Whether singleton use is allowed when the ScopedAllowWait was
    // constructed.
    bool previous_value_;

    DISALLOW_COPY_AND_ASSIGN(ScopedAllowWait);
  };


#if ENABLE_THREAD_RESTRICTIONS
  // Set whether the current thread to make IO calls.
  // Threads start out in the *allowed* state.
  // Returns the previous value.
  static bool SetIOAllowed(bool allowed);

  static bool IsIOAllowed();

  // Check whether the current thread is allowed to make IO calls,
  // and FATALs if not.  See the block comment above the class for
  // a discussion of where to add these checks.
  static void AssertIOAllowed();

  // Set whether the current thread may wait/block.  Returns the previous
  // value.
  static bool SetWaitAllowed(bool allowed);

  static bool IsWaitAllowed();

  // Check whether the current thread is allowed to wait/block.
  // FATALs if not.
  static void AssertWaitAllowed();
#else
  // Inline the empty definitions of these functions so that they can be
  // compiled out.
  static bool SetIOAllowed(bool allowed) { return true; }
  static void AssertIOAllowed() {}
  static bool IsIOAllowed() { return true; }
  static bool SetWaitAllowed(bool allowed) { return true; }
  static void AssertWaitAllowed() {}
  static bool IsWaitAllowed() { return true; }
#endif

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ThreadRestrictions);
};

} // namespace yb
