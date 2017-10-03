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
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <vector>

#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/hdr_histogram.h"
#include "kudu/util/status.h"
#include "kudu/util/test_util.h"
#include "kudu/util/thread.h"

DEFINE_int32(histogram_test_num_threads, 16,
    "Number of threads to spawn for mt-hdr_histogram test");
DEFINE_uint64(histogram_test_num_increments_per_thread, 100000LU,
    "Number of times to call Increment() per thread in mt-hdr_histogram test");

using std::vector;

namespace kudu {

class MtHdrHistogramTest : public KuduTest {
 public:
  MtHdrHistogramTest() {
    num_threads_ = FLAGS_histogram_test_num_threads;
    num_times_ = FLAGS_histogram_test_num_increments_per_thread;
  }

 protected:
  int num_threads_;
  uint64_t num_times_;
};

// Increment a counter a bunch of times in the same bucket
static void IncrementSameHistValue(HdrHistogram* hist, uint64_t value, uint64_t times) {
  for (uint64_t i = 0; i < times; i++) {
    hist->Increment(value);
  }
}

TEST_F(MtHdrHistogramTest, ConcurrentWriteTest) {
  const uint64_t kValue = 1LU;

  HdrHistogram hist(100000LU, 3);

  auto threads = new scoped_refptr<kudu::Thread>[num_threads_];
  for (int i = 0; i < num_threads_; i++) {
    CHECK_OK(kudu::Thread::Create("test", strings::Substitute("thread-$0", i),
        IncrementSameHistValue, &hist, kValue, num_times_, &threads[i]));
  }
  for (int i = 0; i < num_threads_; i++) {
    CHECK_OK(ThreadJoiner(threads[i].get()).Join());
  }

  HdrHistogram snapshot(hist);
  ASSERT_EQ(num_threads_ * num_times_, snapshot.CountInBucketForValue(kValue));

  delete[] threads;
}

// Copy while writing, then iterate to ensure copies are consistent.
TEST_F(MtHdrHistogramTest, ConcurrentCopyWhileWritingTest) {
  const int kNumCopies = 10;
  const uint64_t kValue = 1;

  HdrHistogram hist(100000LU, 3);

  auto threads = new scoped_refptr<kudu::Thread>[num_threads_];
  for (int i = 0; i < num_threads_; i++) {
    CHECK_OK(kudu::Thread::Create("test", strings::Substitute("thread-$0", i),
        IncrementSameHistValue, &hist, kValue, num_times_, &threads[i]));
  }

  // This is somewhat racy but the goal is to catch this issue at least
  // most of the time. At the time of this writing, before fixing a bug where
  // the total count stored in a copied histogram may not match its internal
  // counts (under concurrent writes), this test fails for me on 100/100 runs.
  vector<HdrHistogram *> snapshots;
  ElementDeleter deleter(&snapshots);
  for (int i = 0; i < kNumCopies; i++) {
    snapshots.push_back(new HdrHistogram(hist));
    SleepFor(MonoDelta::FromMicroseconds(100));
  }
  for (int i = 0; i < kNumCopies; i++) {
    snapshots[i]->MeanValue(); // Will crash if underlying iterator is inconsistent.
  }

  for (int i = 0; i < num_threads_; i++) {
    CHECK_OK(ThreadJoiner(threads[i].get()).Join());
  }

  delete[] threads;
}

} // namespace kudu
