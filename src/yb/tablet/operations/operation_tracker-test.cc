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

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "yb/gutil/ref_counted.h"
#include "yb/tablet/operations/operation_driver.h"
#include "yb/tablet/operations/operation_tracker.h"
#include "yb/tablet/operations/operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

DECLARE_int64(tablet_operation_memory_limit_mb);

METRIC_DECLARE_entity(tablet);

METRIC_DECLARE_gauge_uint64(all_operations_inflight);
METRIC_DECLARE_gauge_uint64(write_operations_inflight);
METRIC_DECLARE_gauge_uint64(alter_schema_operations_inflight);
METRIC_DECLARE_gauge_uint64(snapshot_operations_inflight);
METRIC_DECLARE_counter(operation_memory_pressure_rejections);

using std::shared_ptr;
using std::vector;

namespace yb {
namespace tablet {

class OperationTrackerTest : public YBTest {
 public:
  class NoOpOperationState : public OperationState {
   public:
    NoOpOperationState() : OperationState(nullptr), req_(new consensus::ReplicateMsg()) {}
    const google::protobuf::Message* request() const override { return req_.get(); }
    void UpdateRequestFromConsensusRound() override {
      req_ = consensus_round()->replicate_msg();
    }
    std::string ToString() const override { return "NoOpOperation"; }
   private:
    std::shared_ptr<consensus::ReplicateMsg> req_;
  };

  class NoOpOperation : public Operation {
   public:
    explicit NoOpOperation(std::unique_ptr<NoOpOperationState> state)
      : Operation(std::move(state), consensus::LEADER, Operation::WRITE_TXN) {
    }

    consensus::ReplicateMsgPtr NewReplicateMsg() override {
      return std::make_shared<consensus::ReplicateMsg>();
    }

    Status Prepare(IsLeaderSide is_leader_side) override { return Status::OK(); }
    void Start() override {}
    Status Apply() override {
      return Status::OK();
    }
    std::string ToString() const override {
      return "NoOp";
    }
  };

  OperationTrackerTest()
      : entity_(METRIC_ENTITY_tablet.Instantiate(&registry_, "test")) {
    tracker_.StartInstrumentation(entity_);
  }

  void RunOperationsThread(CountDownLatch* finish_latch);

  Status AddDrivers(int num_drivers,
                    vector<scoped_refptr<OperationDriver> >* drivers) {
    vector<scoped_refptr<OperationDriver> > local_drivers;
    for (int i = 0; i < num_drivers; i++) {
      scoped_refptr<OperationDriver> driver(new OperationDriver(
          &tracker_,
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          nullptr,
          TableType::DEFAULT_TABLE_TYPE));
      auto tx = std::make_unique<NoOpOperation>(std::make_unique<NoOpOperationState>());
      RETURN_NOT_OK(driver->Init(std::move(tx), consensus::LEADER));
      local_drivers.push_back(driver);
    }

    for (const scoped_refptr<OperationDriver>& d : local_drivers) {
      drivers->push_back(d);
    }
    return Status::OK();
  }

  MetricRegistry registry_;
  scoped_refptr<MetricEntity> entity_;
  OperationTracker tracker_;
};

TEST_F(OperationTrackerTest, TestGetPending) {
  ASSERT_EQ(0, tracker_.TEST_GetNumPending());
  vector<scoped_refptr<OperationDriver> > drivers;
  ASSERT_OK(AddDrivers(1, &drivers));
  scoped_refptr<OperationDriver> driver = drivers[0];
  ASSERT_EQ(1, tracker_.TEST_GetNumPending());

  auto pending_operations = tracker_.GetPendingOperations();
  ASSERT_EQ(1, pending_operations.size());
  ASSERT_EQ(driver.get(), pending_operations.front().get());

  // And mark the operation as failed, which will cause it to unregister itself.
  driver->TEST_Abort(STATUS(Aborted, ""));

  ASSERT_EQ(0, tracker_.TEST_GetNumPending());
}

// Thread which starts a bunch of operations and later stops them all.
void OperationTrackerTest::RunOperationsThread(CountDownLatch* finish_latch) {
  const int kNumOperations = 100;
  // Start a bunch of operations.
  vector<scoped_refptr<OperationDriver> > drivers;
  ASSERT_OK(AddDrivers(kNumOperations, &drivers));

  // Wait for the main thread to tell us to proceed.
  finish_latch->Wait();

  // Sleep a tiny bit to give the main thread a chance to get into the
  // WaitForAllToFinish() call.
  SleepFor(MonoDelta::FromMilliseconds(1));

  // Finish all the operations
  for (const scoped_refptr<OperationDriver>& driver : drivers) {
    // And mark the operation as failed, which will cause it to unregister itself.
    driver->TEST_Abort(STATUS(Aborted, ""));
  }
}

// Regression test for KUDU-384 (thread safety issue with TestWaitForAllToFinish)
TEST_F(OperationTrackerTest, TestWaitForAllToFinish) {
  CountDownLatch finish_latch(1);
  scoped_refptr<Thread> thr;
  CHECK_OK(Thread::Create("test", "txn-thread",
                          &OperationTrackerTest::RunOperationsThread, this, &finish_latch,
                          &thr));

  // Wait for the txns to start.
  while (tracker_.TEST_GetNumPending() == 0) {
    SleepFor(MonoDelta::FromMilliseconds(1));
  }

  // Allow the thread to proceed, and then wait for it to abort all the operations.
  finish_latch.CountDown();
  tracker_.WaitForAllToFinish();

  CHECK_OK(ThreadJoiner(thr.get()).Join());
  ASSERT_EQ(tracker_.TEST_GetNumPending(), 0);
}

static void CheckMetrics(const scoped_refptr<MetricEntity>& entity,
                         int expected_num_writes,
                         int expected_num_alters,
                         int expected_num_rejections) {
  ASSERT_EQ(expected_num_writes + expected_num_alters, down_cast<AtomicGauge<uint64_t>*>(
      entity->FindOrNull(METRIC_all_operations_inflight).get())->value());
  ASSERT_EQ(expected_num_writes, down_cast<AtomicGauge<uint64_t>*>(
      entity->FindOrNull(METRIC_write_operations_inflight).get())->value());
  ASSERT_EQ(expected_num_alters, down_cast<AtomicGauge<uint64_t>*>(
      entity->FindOrNull(METRIC_alter_schema_operations_inflight).get())->value());
  ASSERT_EQ(expected_num_rejections, down_cast<Counter*>(
      entity->FindOrNull(METRIC_operation_memory_pressure_rejections).get())->value());
}

// Basic testing for metrics. Note that the NoOpOperations we use in this
// test are all write operations.
TEST_F(OperationTrackerTest, TestMetrics) {
  ASSERT_NO_FATALS(CheckMetrics(entity_, 0, 0, 0));

  vector<scoped_refptr<OperationDriver> > drivers;
  ASSERT_OK(AddDrivers(3, &drivers));
  ASSERT_NO_FATALS(CheckMetrics(entity_, 3, 0, 0));

  drivers[0]->TEST_Abort(STATUS(Aborted, ""));
  ASSERT_NO_FATALS(CheckMetrics(entity_, 2, 0, 0));

  drivers[1]->TEST_Abort(STATUS(Aborted, ""));
  drivers[2]->TEST_Abort(STATUS(Aborted, ""));
  ASSERT_NO_FATALS(CheckMetrics(entity_, 0, 0, 0));
}

// Check that the tracker's consumption is very close (but not quite equal to)
// the defined operation memory limit.
static void CheckMemTracker(const shared_ptr<MemTracker>& t) {
  int64_t val = t->consumption();
  uint64_t defined_limit =
      FLAGS_tablet_operation_memory_limit_mb * 1024 * 1024;
  ASSERT_GT(val, (defined_limit * 99) / 100);
  ASSERT_LE(val, defined_limit);
}

// Test that if too many operations are added, eventually the tracker starts
// rejecting new ones.
TEST_F(OperationTrackerTest, TestTooManyOperations) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_operation_memory_limit_mb) = 1;
  shared_ptr<MemTracker> t = MemTracker::CreateTracker("test");
  tracker_.StartMemoryTracking(t);

  // Fill up the tracker.
  //
  // It's difficult to anticipate exactly how many drivers we can add (each
  // carries an empty ReplicateMsg), so we'll just add as many as possible
  // and check that when we fail, it's because we've hit the limit.
  Status s;
  vector<scoped_refptr<OperationDriver> > drivers;
  for (int i = 0; s.ok(); i++) {
    s = AddDrivers(1, &drivers);
  }

  LOG(INFO) << "Added " << drivers.size() << " drivers";
  ASSERT_TRUE(s.IsServiceUnavailable());
  ASSERT_STR_CONTAINS(s.ToString(), "exceeded its limit");
  ASSERT_NO_FATALS(CheckMetrics(entity_, drivers.size(), 0, 1));
  ASSERT_NO_FATALS(CheckMemTracker(t));

  ASSERT_TRUE(AddDrivers(1, &drivers).IsServiceUnavailable());
  ASSERT_NO_FATALS(CheckMetrics(entity_, drivers.size(), 0, 2));
  ASSERT_NO_FATALS(CheckMemTracker(t));

  // If we abort one operation, we should be able to add one more.
  drivers.back()->TEST_Abort(STATUS(Aborted, ""));
  drivers.pop_back();
  ASSERT_NO_FATALS(CheckMemTracker(t));
  ASSERT_OK(AddDrivers(1, &drivers));
  ASSERT_NO_FATALS(CheckMemTracker(t));

  // Clean up.
  for (const scoped_refptr<OperationDriver>& driver : drivers) {
    driver->TEST_Abort(STATUS(Aborted, ""));
  }
}

} // namespace tablet
} // namespace yb
