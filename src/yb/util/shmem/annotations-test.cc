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

void ParentOnlyNoop() {
  DCHECK_PARENT_PROCESS();
}

template<typename T, typename U>
void Write(T&& x, U y) {
  SHARED_MEMORY_STORE(x, y);
}

template<typename T>
auto Read(T&& x) {
  return SHARED_MEMORY_LOAD(x);
}

} // namespace

class SharedMemoryAnnotationsTest : public YBTest {};

TEST_F(SharedMemoryAnnotationsTest, TestAnnotations) {
#define ASSERT_CHILD_DEATH(expr) \
    ASSERT_DEBUG_DEATH([&] { MarkChildProcess(); expr; }(), "Access from child process not allowed")

#define ASSERT_CHILD_SUCCESS(expr) \
    ASSERT_OK(ForkAndRunToCompletion([&] { MarkChildProcess(); expr; }));

  ASSERT_CHILD_DEATH(ParentOnlyNoop());

  ChildProcessForbidden<int> forbid{0};
  ASSERT_CHILD_DEATH(Read(forbid));
  ASSERT_CHILD_DEATH(Write(forbid, 1));

  ChildProcessRO<int> readonly{0};
  ASSERT_CHILD_SUCCESS(Read(readonly));
  ASSERT_CHILD_DEATH(Write(readonly, 1));

  ChildProcessRW<int> readwrite{0};
  ASSERT_CHILD_SUCCESS(Read(readwrite));
  ASSERT_CHILD_SUCCESS(Write(readwrite, 1));
}

} // namespace yb
