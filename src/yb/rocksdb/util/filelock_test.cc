//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/testharness.h"

#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

class LockTest : public RocksDBTest {
 public:
  static LockTest* current_;
  std::string file_;
  rocksdb::Env* env_;

  LockTest() : file_(test::TmpDir() + "/db_testlock_file"),
               env_(rocksdb::Env::Default()) {
    current_ = this;
  }

  ~LockTest() {
  }

  Status LockFile(FileLock** db_lock) {
    return env_->LockFile(file_, db_lock);
  }

  Status UnlockFile(FileLock* db_lock) {
    return env_->UnlockFile(db_lock);
  }
};
LockTest* LockTest::current_;

TEST_F(LockTest, LockBySameThread) {
  FileLock* lock1;
  FileLock* lock2;

  // acquire a lock on a file
  ASSERT_OK(LockFile(&lock1));

  // re-acquire the lock on the same file. This should fail.
  ASSERT_TRUE(LockFile(&lock2).IsIOError());

  // release the lock
  ASSERT_OK(UnlockFile(lock1));

}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
