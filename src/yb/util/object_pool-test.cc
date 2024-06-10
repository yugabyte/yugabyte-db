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

#include "yb/util/object_pool.h"

namespace yb {

// Simple class which maintains a count of how many objects
// are currently alive.
class MyClass {
 public:
  MyClass() {
    instance_count_++;
  }

  ~MyClass() {
    instance_count_--;
  }

  static int instance_count() {
    return instance_count_;
  }

  static void ResetCount() {
    instance_count_ = 0;
  }

 private:
  static int instance_count_;
};
int MyClass::instance_count_ = 0;

TEST(TestObjectPool, TestPooling) {
  MyClass::ResetCount();
  {
    ObjectPool<MyClass> pool;
    ASSERT_EQ(0, MyClass::instance_count());
    MyClass *a = pool.Construct();
    ASSERT_EQ(1, MyClass::instance_count());
    MyClass *b = pool.Construct();
    ASSERT_EQ(2, MyClass::instance_count());
    ASSERT_TRUE(a != b);
    pool.Destroy(b);
    ASSERT_EQ(1, MyClass::instance_count());
    MyClass *c = pool.Construct();
    ASSERT_EQ(2, MyClass::instance_count());
    ASSERT_TRUE(c == b) << "should reuse instance";
    pool.Destroy(c);

    ASSERT_EQ(1, MyClass::instance_count());
  }

  ASSERT_EQ(0, MyClass::instance_count())
    << "destructing pool should have cleared instances";
}

TEST(TestObjectPool, TestScopedPtr) {
  MyClass::ResetCount();
  ASSERT_EQ(0, MyClass::instance_count());
  ObjectPool<MyClass> pool;
  {
    ObjectPool<MyClass>::scoped_ptr sptr(
      pool.make_scoped_ptr(pool.Construct()));
    ASSERT_EQ(1, MyClass::instance_count());
  }
  ASSERT_EQ(0, MyClass::instance_count());
}

} // namespace yb
