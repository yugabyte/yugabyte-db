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

#include <atomic>
#include <string>

#include <gtest/gtest.h>

#include "yb/gutil/ref_counted.h"

#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread.h"

DECLARE_bool(TEST_enable_sync_points);

using std::string;
using std::vector;

namespace yb {

static void RunThread(bool *var) {
  *var = true;
  TEST_SYNC_POINT("first");
}

TEST(SyncPointTest, TestSyncPoint) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;

  // Set up a sync point "second" that depends on "first".
  vector<SyncPoint::Dependency> dependencies;
  dependencies.push_back({"first", "second"});
  SyncPoint::GetInstance()->LoadDependency(dependencies);
  SyncPoint::GetInstance()->EnableProcessing();

  // Kick off a thread that'll process "first", but not before
  // setting 'var' to true, which unblocks the main thread.
  scoped_refptr<Thread> thread;
  bool var = false;
  ASSERT_OK(yb::Thread::Create("test", "test", &RunThread, &var, &thread));

  // Blocked on RunThread to process "first".
  TEST_SYNC_POINT("second");
  ASSERT_TRUE(var);

  thread->Join();
}

} // namespace yb
