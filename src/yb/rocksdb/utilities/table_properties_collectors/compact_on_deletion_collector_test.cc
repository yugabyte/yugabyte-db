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

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/utilities/table_properties_collectors.h"

#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

namespace rocksdb {

class CompactOnDeletionCollectorTest : public yb::YBTest {
};

TEST_F(CompactOnDeletionCollectorTest, TestCompactOnDeletionCollector) {
  const int kWindowSizes[] =
      {1000, 10000, 10000, 127, 128, 129, 255, 256, 257, 2, 10000};
  const int kDeletionTriggers[] =
      {500, 9500, 4323, 47, 61, 128, 250, 250, 250, 2, 2};
  TablePropertiesCollectorFactory::Context context;
  context.column_family_id =
      TablePropertiesCollectorFactory::Context::kUnknownColumnFamily;

  std::vector<int> window_sizes;
  std::vector<int> deletion_triggers;
  // deterministic tests
  for (int test = 0; test < 9; ++test) {
    window_sizes.emplace_back(kWindowSizes[test]);
    deletion_triggers.emplace_back(kDeletionTriggers[test]);
  }


  // Lower number of runs for tsan due to low perf.
  constexpr int kNumRandomTests = yb::NonTsanVsTsan(100, 20);

  // randomize tests
  Random rnd(301);
  const int kMaxTestSize = 100000l;
  for (int random_test = 0; random_test < kNumRandomTests; random_test++) {
    int window_size = rnd.Uniform(kMaxTestSize) + 1;
    int deletion_trigger = rnd.Uniform(window_size);
    window_sizes.emplace_back(window_size);
    deletion_triggers.emplace_back(deletion_trigger);
  }

  assert(window_sizes.size() == deletion_triggers.size());

  for (size_t test = 0; test < window_sizes.size(); ++test) {
    const int kBucketSize = 128;
    const int kWindowSize = window_sizes[test];
    const int kPaddedWindowSize =
        kBucketSize * ((window_sizes[test] + kBucketSize - 1) / kBucketSize);
    const int kNumDeletionTrigger = deletion_triggers[test];
    const int kBias = (kNumDeletionTrigger + kBucketSize - 1) / kBucketSize;
    // Simple test
    LOG_TIMING(INFO, "TEST1") {
      std::unique_ptr<TablePropertiesCollector> collector;
      auto factory = NewCompactOnDeletionCollectorFactory(
          kWindowSize, kNumDeletionTrigger);
      collector.reset(factory->CreateTablePropertiesCollector(context));
      const int kSample = 10;
      for (int delete_rate = 0; delete_rate <= kSample; ++delete_rate) {
        int deletions = 0;
        for (int i = 0; i < kPaddedWindowSize; ++i) {
          if (i % kSample < delete_rate) {
            CHECK_OK(collector->AddUserKey(
                "hello", "rocksdb", kEntryDelete, 0, 0));
            deletions++;
          } else {
            CHECK_OK(collector->AddUserKey(
                "hello", "rocksdb", kEntryPut, 0, 0));
          }
        }
        if (collector->NeedCompact() !=
            (deletions >= kNumDeletionTrigger) &&
            std::abs(deletions - kNumDeletionTrigger) > kBias) {
          fprintf(stderr, "[Error] collector->NeedCompact() != (%d >= %d)"
                  " with kWindowSize = %d and kNumDeletionTrigger = %d\n",
                  deletions, kNumDeletionTrigger,
                  kWindowSize, kNumDeletionTrigger);
          assert(false);
        }
        CHECK_OK(collector->Finish(nullptr));
      }
    }

    // Only one section of a file satisfies the compaction trigger
    LOG_TIMING(INFO, "TEST2") {
      std::unique_ptr<TablePropertiesCollector> collector;
      auto factory = NewCompactOnDeletionCollectorFactory(
          kWindowSize, kNumDeletionTrigger);
      collector.reset(factory->CreateTablePropertiesCollector(context));
      const int kSample = 10;
      for (int delete_rate = 0; delete_rate <= kSample; ++delete_rate) {
        int deletions = 0;
        for (int section = 0; section < 5; ++section) {
          int initial_entries = rnd.Uniform(kWindowSize) + kWindowSize;
          for (int i = 0; i < initial_entries; ++i) {
            CHECK_OK(collector->AddUserKey(
                "hello", "rocksdb", kEntryPut, 0, 0));
          }
        }
        for (int i = 0; i < kPaddedWindowSize; ++i) {
          if (i % kSample < delete_rate) {
            CHECK_OK(collector->AddUserKey(
                "hello", "rocksdb", kEntryDelete, 0, 0));
            deletions++;
          } else {
            CHECK_OK(collector->AddUserKey(
                "hello", "rocksdb", kEntryPut, 0, 0));
          }
        }
        for (int section = 0; section < 5; ++section) {
          int ending_entries = rnd.Uniform(kWindowSize) + kWindowSize;
          for (int i = 0; i < ending_entries; ++i) {
            CHECK_OK(collector->AddUserKey(
               "hello", "rocksdb", kEntryPut, 0, 0));
          }
        }
        if (collector->NeedCompact() != (deletions >= kNumDeletionTrigger) &&
            std::abs(deletions - kNumDeletionTrigger) > kBias) {
          fprintf(stderr, "[Error] collector->NeedCompact() %d != (%d >= %d)"
                  " with kWindowSize = %d, kNumDeletionTrigger = %d\n",
                  collector->NeedCompact(),
                  deletions, kNumDeletionTrigger, kWindowSize,
                  kNumDeletionTrigger);
          assert(false);
        }
        CHECK_OK(collector->Finish(nullptr));
      }
    }

    // TEST 3:  Issues a lots of deletes, but their density is not
    // high enough to trigger compaction.
    LOG_TIMING(INFO, "TEST3") {
      std::unique_ptr<TablePropertiesCollector> collector;
      auto factory = NewCompactOnDeletionCollectorFactory(
          kWindowSize, kNumDeletionTrigger);
      collector.reset(factory->CreateTablePropertiesCollector(context));
      assert(collector->NeedCompact() == false);
      // Insert "kNumDeletionTrigger * 0.95" deletions for every
      // "kWindowSize" and verify compaction is not needed.
      const int kDeletionsPerSection = kNumDeletionTrigger * 95 / 100;
      if (kDeletionsPerSection >= 0) {
        for (int section = 0; section < 200; ++section) {
          for (int i = 0; i < kPaddedWindowSize; ++i) {
            if (i < kDeletionsPerSection) {
              CHECK_OK(collector->AddUserKey(
                  "hello", "rocksdb", kEntryDelete, 0, 0));
            } else {
              CHECK_OK(collector->AddUserKey(
                  "hello", "rocksdb", kEntryPut, 0, 0));
            }
          }
        }
        if (collector->NeedCompact() &&
            std::abs(kDeletionsPerSection - kNumDeletionTrigger) > kBias) {
          fprintf(stderr, "[Error] collector->NeedCompact() != false"
                  " with kWindowSize = %d and kNumDeletionTrigger = %d\n",
                  kWindowSize, kNumDeletionTrigger);
          assert(false);
        }
        CHECK_OK(collector->Finish(nullptr));
      }
    }
  }
}

}  // namespace rocksdb
