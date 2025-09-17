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

#include <string_view>

#include "yb/util/crash_point.h"
#include "yb/util/shared_mem.h"
#include "yb/util/shmem/robust_intrusive_list.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {

class RobustIntrusiveListTest : public YBTest {};

struct Node : public RobustIntrusiveListNode {
  size_t data = 0;
};

TEST_F(RobustIntrusiveListTest, TestSimple) {
  Node n1, n2;
  n1.data = 1;
  n2.data = 2;
  RobustIntrusiveList<Node> list;

  LOG(INFO) << &n1 << " " << &n2 << " " << &list;

  list.push_front(n2);
  list.push_front(n1);

  auto itr = list.before_begin();
  ASSERT_EQ(&*std::next(itr), &n1);
  RobustIntrusiveList<Node>::s_erase_after(itr);
  ASSERT_EQ(&*std::next(itr), &n2);
  RobustIntrusiveList<Node>::s_erase_after(itr);
  ASSERT_TRUE(list.empty());
}

TEST_F(RobustIntrusiveListTest, YB_DEBUG_ONLY_TEST(TestCrashAfterFirstStore)) {
  struct SharedState {
    std::array<Node, 3> nodes;
    RobustIntrusiveList<Node> list;
  };

  auto shared = ASSERT_RESULT(SharedMemoryObject<SharedState>::Create());
  shared->nodes[0].data = shared->nodes[1].data = 1;
  shared->list.push_front(shared->nodes[0]);
  shared->list.push_front(shared->nodes[1]);

  ASSERT_OK(ForkAndRunToCrashPoint([&] {
    shared->nodes[2].data = 2;
    shared->list.push_front(shared->nodes[2]);
  }, "RobustIntrusiveList::set_next:1"));

  auto itr = shared->list.begin();
  ASSERT_EQ(&*itr, &shared->nodes[1]);
  ASSERT_EQ(itr->data, 1);
  ++itr;
  ASSERT_EQ(&*itr, &shared->nodes[0]);
  ASSERT_EQ(itr->data, 1);
  ++itr;
  ASSERT_EQ(itr, shared->list.end());
}

} // namespace yb
