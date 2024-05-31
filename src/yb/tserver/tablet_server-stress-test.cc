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

#include "yb/gutil/strings/substitute.h"

#include "yb/tserver/tablet_server-test-base.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/thread.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_int32(num_inserter_threads, 8, "Number of inserter threads to run");
DEFINE_NON_RUNTIME_int32(num_inserts_per_thread, 0, "Number of inserts from each thread");
DECLARE_bool(enable_maintenance_manager);

METRIC_DEFINE_event_stats(test, insert_latency,
                        "Insert Latency",
                        yb::MetricUnit::kMicroseconds,
                        "TabletServer single threaded insert latency.");

namespace yb {
namespace tserver {

class TSStressTest : public TabletServerTestBase {
 public:
  TSStressTest()
    : start_latch_(FLAGS_num_inserter_threads) {

    if (FLAGS_num_inserts_per_thread == 0) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_inserts_per_thread) = AllowSlowTests() ? 100000 : 1000;
    }

    // Re-enable the maintenance manager which is disabled by default
    // in TS tests. We want to stress the whole system including
    // flushes, etc.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_maintenance_manager) = true;
  }

  void SetUp() override {
    TabletServerTestBase::SetUp();
    StartTabletServer();

    stats_ = METRIC_insert_latency.Instantiate(ts_test_metric_entity_);
  }

  void StartThreads() {
    for (int i = 0; i < FLAGS_num_inserter_threads; i++) {
      scoped_refptr<yb::Thread> new_thread;
      CHECK_OK(yb::Thread::Create("test", strings::Substitute("test$0", i),
                                    &TSStressTest::InserterThread, this, i, &new_thread));
      threads_.push_back(new_thread);
    }
  }

  void JoinThreads() {
    for (scoped_refptr<yb::Thread> thr : threads_) {
     CHECK_OK(ThreadJoiner(thr.get()).Join());
    }
  }

  void InserterThread(int thread_idx);

 protected:
  scoped_refptr<EventStats> stats_;
  CountDownLatch start_latch_;
  std::vector<scoped_refptr<yb::Thread> > threads_;
};

void TSStressTest::InserterThread(int thread_idx) {
  // Wait for all the threads to be ready before we start.
  start_latch_.CountDown();
  start_latch_.Wait();
  LOG(INFO) << "Starting inserter thread " << thread_idx << " complete";

  uint32_t max_rows = FLAGS_num_inserts_per_thread;
  auto start_row = thread_idx * max_rows;
  for (auto i = start_row; i < start_row + max_rows ; i++) {
    MonoTime before = MonoTime::Now();
    InsertTestRowsRemote(thread_idx, i, 1);
    MonoTime after = MonoTime::Now();
    MonoDelta delta = after.GetDeltaSince(before);
    stats_->Increment(delta.ToMicroseconds());
  }
  LOG(INFO) << "Inserter thread " << thread_idx << " complete";
}

TEST_F(TSStressTest, TestMTInserts) {
  StartThreads();
  Stopwatch s(Stopwatch::ALL_THREADS);
  s.start();
  JoinThreads();
  s.stop();
  int num_rows = (FLAGS_num_inserter_threads * FLAGS_num_inserts_per_thread);
  LOG(INFO) << "Inserted " << num_rows << " rows in " << s.elapsed().wall_millis() << " ms";
  LOG(INFO) << "Throughput: " << (num_rows * 1000 / s.elapsed().wall_millis()) << " rows/sec";
  LOG(INFO) << "CPU efficiency: " << (num_rows / s.elapsed().user_cpu_seconds()) << " rows/cpusec";


  // Generate the JSON.
  std::stringstream out;
  JsonWriter writer(&out, JsonWriter::PRETTY);
  ASSERT_OK(stats_->WriteAsJson(&writer, MetricJsonOptions()));

  LOG(INFO) << out.str();
}

} // namespace tserver
} // namespace yb
