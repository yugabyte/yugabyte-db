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
#include <gtest/gtest.h>

#include "yb/gutil/spinlock.h"
#include "yb/util/spinlock_profiling.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/trace.h"

using std::string;

// Can't include gutil/synchronization_profiling.h directly as it'll
// declare a weak symbol directly in this unit test, which the runtime
// linker will prefer over equivalent strong symbols for some reason. By
// declaring the symbol without providing an empty definition, the strong
// symbols are chosen when provided via shared libraries.
//
// Further reading:
// - http://stackoverflow.com/questions/20658809/dynamic-loading-and-weak-symbol-resolution
// - http://notmysock.org/blog/php/weak-symbols-arent.html
namespace gutil {
extern void SubmitSpinLockProfileData(const void *, int64);
} // namespace gutil

namespace yb {

class SpinLockProfilingTest : public YBTest {};

TEST_F(SpinLockProfilingTest, TestSpinlockProfiling) {
  scoped_refptr<Trace> t(new Trace);
  base::SpinLock lock;
  {
    ADOPT_TRACE(t.get());
    gutil::SubmitSpinLockProfileData(&lock, 4000000);
  }
  string result = t->DumpToString(true);
  LOG(INFO) << "trace: " << result;
  // We can't assert more specifically because the CyclesPerSecond
  // on different machines might be different.
  ASSERT_STR_CONTAINS(result, "Waited ");
  ASSERT_STR_CONTAINS(result, "on lock ");

  ASSERT_GT(GetSpinLockContentionMicros(), 0);
}

TEST_F(SpinLockProfilingTest, TestStackCollection) {
  StartSynchronizationProfiling();
  base::SpinLock lock;
  gutil::SubmitSpinLockProfileData(&lock, 12345);
  StopSynchronizationProfiling();
  std::stringstream str;
  int64_t dropped = 0;
  FlushSynchronizationProfile(&str, &dropped);
  string s = str.str();
  ASSERT_STR_CONTAINS(s, "12345\t1 @ ");
  ASSERT_EQ(0, dropped);
}

} // namespace yb
