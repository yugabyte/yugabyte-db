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
#pragma once

#include <map>
#include <shared_mutex>
#include <string>

#include "yb/util/flags.h"
#include <gtest/gtest_prod.h>

#include "yb/consensus/log_fwd.h"

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"

namespace yb {
namespace log {

// This class allows callers to register their interest in (anchor) a particular
// log index. The primary use case for this is to prevent the deletion of segments of
// the WAL that reference as-yet unflushed in-memory operations.
//
// This class is thread-safe.
class LogAnchorRegistry : public RefCountedThreadSafe<LogAnchorRegistry> {
 public:
  LogAnchorRegistry();

  // Register interest for a particular log index.
  // log_index: The log index the caller wishes to anchor.
  // owner: String to describe who is registering the anchor. Used in assert
  //        messages for debugging purposes.
  // anchor: Pointer to LogAnchor structure that will be populated on registration.
  void Register(int64_t log_index, const std::string& owner, LogAnchor* anchor);

  // Atomically update the registration of an anchor to a new log index.
  // Before: anchor must be registered with some log index.
  // After: anchor is now registered using index 'log_index'.
  // See Register().
  Status UpdateRegistration(int64_t log_index, LogAnchor* anchor);

  // Release the anchor on a log index.
  // Note: anchor must be the original pointer passed to Register().
  Status Unregister(LogAnchor* anchor);

  // Release the anchor on a log index if it is registered.
  // Otherwise, do nothing.
  Status UnregisterIfAnchored(LogAnchor* anchor);

  // Query the registry to find the earliest anchored log index in the registry.
  // Returns Status::NotFound if no anchors are currently active.
  Status GetEarliestRegisteredLogIndex(int64_t* op_id);

  // Simply returns the number of active anchors for use in debugging / tests.
  // This is _not_ a constant-time operation.
  size_t GetAnchorCountForTests() const;

  // Dumps information about registered anchors to a string.
  std::string DumpAnchorInfo() const;

 private:
  friend class RefCountedThreadSafe<LogAnchorRegistry>;
  ~LogAnchorRegistry();

  typedef std::multimap<int64_t, LogAnchor*> AnchorMultiMap;

  // Register a new anchor after taking the lock. See Register().
  void RegisterUnlocked(int64_t log_index, const std::string& owner, LogAnchor* anchor);

  // Unregister an anchor after taking the lock. See Unregister().
  Status UnregisterUnlocked(LogAnchor* anchor);

  AnchorMultiMap anchors_;
  mutable simple_spinlock lock_;

  DISALLOW_COPY_AND_ASSIGN(LogAnchorRegistry);
};

// A simple struct that allows us to keep track of which log segments we want
// to anchor (prevent log GC on).
struct LogAnchor {
 public:
  LogAnchor();
  ~LogAnchor();

  int64_t index() const {
    return log_index;
  }

 private:
  FRIEND_TEST(LogTest, TestGCWithLogRunning);
  FRIEND_TEST(LogAnchorRegistryTest, TestUpdateRegistration);
  friend class LogAnchorRegistry;

  // Whether any log index is currently registered with this anchor.
  bool is_registered;

  // When this anchor was last registered or updated.
  MonoTime when_registered;

  // The index of the log entry we are anchoring on.
  int64_t log_index;

  // An arbitrary string containing details of the subsystem holding the
  // anchor, and any relevant information about it that should be displayed in
  // the log or the web UI.
  std::string owner;

  DISALLOW_COPY_AND_ASSIGN(LogAnchor);
};

} // namespace log
} // namespace yb
