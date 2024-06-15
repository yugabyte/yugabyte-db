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

#include "yb/util/logging.h"

#include "yb/util/thread.h"
#include "yb/util/threadlocal.h"
#include "yb/util/thread_restrictions.h"

#ifdef ENABLE_THREAD_RESTRICTIONS

namespace yb {

namespace {

struct LocalThreadRestrictions {
  LocalThreadRestrictions()
    : io_allowed(true),
      wait_allowed(true),
      singleton_allowed(true) {
  }

  bool io_allowed;
  bool wait_allowed;
  bool singleton_allowed;
};

LocalThreadRestrictions* LoadTLS() {
  BLOCK_STATIC_THREAD_LOCAL(LocalThreadRestrictions, local_thread_restrictions);
  return local_thread_restrictions;
}

} // anonymous namespace

bool ThreadRestrictions::SetIOAllowed(bool allowed) {
  bool previous_allowed = LoadTLS()->io_allowed;
  LoadTLS()->io_allowed = allowed;
  return previous_allowed;
}

bool ThreadRestrictions::IsIOAllowed() {
  return LoadTLS()->io_allowed;
}

void ThreadRestrictions::AssertIOAllowed() {
  CHECK(LoadTLS()->io_allowed)
    << "Function marked as IO-only was called from a thread that "
    << "disallows IO!  If this thread really should be allowed to "
    << "make IO calls, adjust the call to "
    << "yb::ThreadRestrictions::SetIOAllowed() in this thread's "
    << "startup. "
    << (Thread::current_thread() ? Thread::current_thread()->ToString() : "(not a yb::Thread)");
}

bool ThreadRestrictions::SetWaitAllowed(bool allowed) {
  bool previous_allowed = LoadTLS()->wait_allowed;
  LoadTLS()->wait_allowed = allowed;
  return previous_allowed;
}

bool ThreadRestrictions::IsWaitAllowed() {
  return LoadTLS()->wait_allowed;
}

void ThreadRestrictions::AssertWaitAllowed() {
  CHECK(LoadTLS()->wait_allowed)
    << "Waiting is not allowed to be used on this thread to prevent "
    << "server-wide latency aberrations and deadlocks. "
    << (Thread::current_thread() ? Thread::current_thread()->ToString() : "(not a yb::Thread)");
}

} // namespace yb

#endif
