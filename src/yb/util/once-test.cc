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
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/once.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread.h"

using std::vector;
using strings::Substitute;

namespace yb {

namespace {

struct Thing {
  explicit Thing(bool should_fail)
    : should_fail_(should_fail),
      value_(0) {
  }

  Status Init() {
    return once_.Init(&Thing::InitOnce, this);
  }

  Status InitOnce() {
    if (should_fail_) {
      return STATUS(IllegalState, "Whoops!");
    }
    value_ = 1;
    return Status::OK();
  }

  const bool should_fail_;
  int value_;
  YBOnceDynamic once_;
};

} // anonymous namespace

TEST(TestOnce, YBOnceDynamicTest) {
  {
    Thing t(false);
    ASSERT_EQ(0, t.value_);
    ASSERT_FALSE(t.once_.initted());

    for (int i = 0; i < 2; i++) {
      ASSERT_OK(t.Init());
      ASSERT_EQ(1, t.value_);
      ASSERT_TRUE(t.once_.initted());
    }
  }

  {
    Thing t(true);
    for (int i = 0; i < 2; i++) {
      ASSERT_TRUE(t.Init().IsIllegalState());
      ASSERT_EQ(0, t.value_);
      ASSERT_TRUE(t.once_.initted());
    }
  }
}

static void InitOrGetInitted(Thing* t, int i) {
  if (i % 2 == 0) {
    LOG(INFO) << "Thread " << i << " initting";
    ASSERT_OK(t->Init());
  } else {
    LOG(INFO) << "Thread " << i << " value: " << t->once_.initted();
  }
}

TEST(TestOnce, YBOnceDynamicThreadSafeTest) {
  Thing thing(false);

  // The threads will read and write to thing.once_.initted. If access to
  // it is not synchronized, TSAN will flag the access as data races.
  vector<scoped_refptr<Thread> > threads;
  for (int i = 0; i < 10; i++) {
    scoped_refptr<Thread> t;
    ASSERT_OK(Thread::Create("test", Substitute("thread $0", i),
                             &InitOrGetInitted, &thing, i, &t));
    threads.push_back(t);
  }

  for (const scoped_refptr<Thread>& t : threads) {
    t->Join();
  }
}

} // namespace yb
