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

#include "yb/util/debug.h"
#include "yb/util/shmem/annotations.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {

namespace {

void ParentGuard() {
  ParentProcessGuard g;
}

} // namespace

class SharedMemoryAnnotationsTest : public YBTest {};

TEST_F(SharedMemoryAnnotationsTest, TestAnnotations) {
  ASSERT_DEBUG_DEATH(
      [&] { MarkChildProcess(); ParentGuard(); }(),
      "Access from child process not allowed");

  ChildProcessForbidden<int> forbidden = 1;
  ChildProcessRO<int> readonly = 1;
  ChildProcessRW<int> readwrite = 1;

  {
    ParentProcessGuard g;
    ASSERT_EQ(forbidden.Get(), 1);
    forbidden.Get() = 2;
    ASSERT_EQ(forbidden.Get(), 2);
  }

  {
    ParentProcessGuard g;
    ASSERT_EQ(readonly.Get(), 1);
    readonly.Get() = 2;
    ASSERT_EQ(readonly.Get(), 2);
    SHARED_MEMORY_STORE(readonly, 3);
    ASSERT_EQ(readonly.Get(), 3);
  }
  ASSERT_EQ(SHARED_MEMORY_LOAD(readonly), 3);

  {
    ParentProcessGuard g;
    ASSERT_EQ(readwrite.Get(), 1);
    SHARED_MEMORY_STORE(readwrite, 2);
    ASSERT_EQ(readwrite.Get(), 2);
  }
  ASSERT_EQ(SHARED_MEMORY_LOAD(readwrite), 2);
  SHARED_MEMORY_STORE(readwrite, 3);
  ASSERT_EQ(SHARED_MEMORY_LOAD(readwrite), 3);
}

} // namespace yb
