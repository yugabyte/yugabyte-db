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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#include "yb/rocksdb/db/db_test_util.h"
#include "yb/rocksdb/port/stack_trace.h"

namespace rocksdb {

class DBTestInPlaceUpdate : public DBTestBase {
 public:
  DBTestInPlaceUpdate() : DBTestBase("/db_inplace_update_test") {}
};

TEST_F(DBTestInPlaceUpdate, InPlaceUpdate) {
  do {
    Options options;
    options.create_if_missing = true;
    options.inplace_update_support = true;
    options.env = env_;
    options.write_buffer_size = 100000;
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    // Update key with values of smaller size
    int numValues = 10;
    for (int i = numValues; i > 0; i--) {
      std::string value = DummyString(i, 'a');
      ASSERT_OK(Put(1, "key", value));
      ASSERT_EQ(value, Get(1, "key"));
    }

    // Only 1 instance for that key.
    validateNumberOfEntries(1, 1);
  } while (ChangeCompactOptions());
}

TEST_F(DBTestInPlaceUpdate, InPlaceUpdateLargeNewValue) {
  do {
    Options options;
    options.create_if_missing = true;
    options.inplace_update_support = true;
    options.env = env_;
    options.write_buffer_size = 100000;
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    // Update key with values of larger size
    int numValues = 10;
    for (int i = 0; i < numValues; i++) {
      std::string value = DummyString(i, 'a');
      ASSERT_OK(Put(1, "key", value));
      ASSERT_EQ(value, Get(1, "key"));
    }

    // All 10 updates exist in the internal iterator
    validateNumberOfEntries(numValues, 1);
  } while (ChangeCompactOptions());
}

TEST_F(DBTestInPlaceUpdate, InPlaceUpdateCallbackSmallerSize) {
  do {
    Options options;
    options.create_if_missing = true;
    options.inplace_update_support = true;

    options.env = env_;
    options.write_buffer_size = 100000;
    options.inplace_callback =
      rocksdb::DBTestInPlaceUpdate::updateInPlaceSmallerSize;
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    // Update key with values of smaller size
    int numValues = 10;
    ASSERT_OK(Put(1, "key", DummyString(numValues, 'a')));
    ASSERT_EQ(DummyString(numValues, 'c'), Get(1, "key"));

    for (int i = numValues; i > 0; i--) {
      ASSERT_OK(Put(1, "key", DummyString(i, 'a')));
      ASSERT_EQ(DummyString(i - 1, 'b'), Get(1, "key"));
    }

    // Only 1 instance for that key.
    validateNumberOfEntries(1, 1);
  } while (ChangeCompactOptions());
}

TEST_F(DBTestInPlaceUpdate, InPlaceUpdateCallbackSmallerVarintSize) {
  do {
    Options options;
    options.create_if_missing = true;
    options.inplace_update_support = true;

    options.env = env_;
    options.write_buffer_size = 100000;
    options.inplace_callback =
      rocksdb::DBTestInPlaceUpdate::updateInPlaceSmallerVarintSize;
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    // Update key with values of smaller varint size
    int numValues = 265;
    ASSERT_OK(Put(1, "key", DummyString(numValues, 'a')));
    ASSERT_EQ(DummyString(numValues, 'c'), Get(1, "key"));

    for (int i = numValues; i > 0; i--) {
      ASSERT_OK(Put(1, "key", DummyString(i, 'a')));
      ASSERT_EQ(DummyString(1, 'b'), Get(1, "key"));
    }

    // Only 1 instance for that key.
    validateNumberOfEntries(1, 1);
  } while (ChangeCompactOptions());
}

TEST_F(DBTestInPlaceUpdate, InPlaceUpdateCallbackLargeNewValue) {
  do {
    Options options;
    options.create_if_missing = true;
    options.inplace_update_support = true;

    options.env = env_;
    options.write_buffer_size = 100000;
    options.inplace_callback =
      rocksdb::DBTestInPlaceUpdate::updateInPlaceLargerSize;
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    // Update key with values of larger size
    int numValues = 10;
    for (int i = 0; i < numValues; i++) {
      ASSERT_OK(Put(1, "key", DummyString(i, 'a')));
      ASSERT_EQ(DummyString(i, 'c'), Get(1, "key"));
    }

    // No inplace updates. All updates are puts with new seq number
    // All 10 updates exist in the internal iterator
    validateNumberOfEntries(numValues, 1);
  } while (ChangeCompactOptions());
}

TEST_F(DBTestInPlaceUpdate, InPlaceUpdateCallbackNoAction) {
  do {
    Options options;
    options.create_if_missing = true;
    options.inplace_update_support = true;

    options.env = env_;
    options.write_buffer_size = 100000;
    options.inplace_callback =
      rocksdb::DBTestInPlaceUpdate::updateInPlaceNoAction;
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    // Callback function requests no actions from db
    ASSERT_OK(Put(1, "key", DummyString(1, 'a')));
    ASSERT_EQ(Get(1, "key"), "NOT_FOUND");
  } while (ChangeCompactOptions());
}
}  // namespace rocksdb

int main(int argc, char** argv) {
  rocksdb::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
