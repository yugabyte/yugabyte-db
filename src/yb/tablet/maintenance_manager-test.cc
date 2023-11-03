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
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.pb.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using yb::tablet::MaintenanceManagerStatusPB;
using std::shared_ptr;
using std::string;
using strings::Substitute;

METRIC_DEFINE_entity(test);
METRIC_DEFINE_gauge_uint32(test, maintenance_ops_running,
                           "Number of Maintenance Operations Running",
                           yb::MetricUnit::kMaintenanceOperations,
                           "The number of background maintenance operations currently running.");
METRIC_DEFINE_event_stats(test, maintenance_op_duration,
                        "Maintenance Operation Duration",
                        yb::MetricUnit::kSeconds, "");

namespace yb {

const int kHistorySize = 4;

class MaintenanceManagerTest : public YBTest {
 public:
  MaintenanceManagerTest() {
    test_tracker_ = MemTracker::CreateTracker(1000, "test");
    MaintenanceManager::Options options;
    options.num_threads = 2;
    options.polling_interval_ms = 1;
    options.history_size = kHistorySize;
    options.parent_mem_tracker = test_tracker_;
    manager_.reset(new MaintenanceManager(options));
    CHECK_OK(manager_->Init());
  }
  ~MaintenanceManagerTest() {
    manager_->Shutdown();
  }

 protected:
  shared_ptr<MemTracker> test_tracker_;
  shared_ptr<MaintenanceManager> manager_;
};

// Just create the MaintenanceManager and then shut it down, to make sure
// there are no race conditions there.
TEST_F(MaintenanceManagerTest, TestCreateAndShutdown) {
}

enum TestMaintenanceOpState {
  OP_DISABLED,
  OP_RUNNABLE,
  OP_RUNNING,
  OP_FINISHED,
};

class TestMaintenanceOp : public MaintenanceOp {
 public:
  TestMaintenanceOp(const std::string& name,
                    IOUsage io_usage,
                    TestMaintenanceOpState state,
                    const shared_ptr<MemTracker>& tracker)
    : MaintenanceOp(name, io_usage),
      state_change_cond_(&lock_),
      state_(state),
      consumption_(tracker, 500),
      logs_retained_bytes_(0),
      perf_improvement_(0),
      metric_entity_(METRIC_ENTITY_test.Instantiate(&metric_registry_, "test")),
      maintenance_op_duration_(METRIC_maintenance_op_duration.Instantiate(metric_entity_)),
      maintenance_ops_running_(METRIC_maintenance_ops_running.Instantiate(metric_entity_, 0)) {
  }

  virtual ~TestMaintenanceOp() {}

  bool Prepare() override {
    std::lock_guard guard(lock_);
    if (state_ != OP_RUNNABLE) {
      return false;
    }
    state_ = OP_RUNNING;
    state_change_cond_.Broadcast();
    DLOG(INFO) << "Prepared op " << name();
    return true;
  }

  void Perform() override {
    DLOG(INFO) << "Performing op " << name();
    std::lock_guard guard(lock_);
    CHECK_EQ(OP_RUNNING, state_);
    state_ = OP_FINISHED;
    state_change_cond_.Broadcast();
  }

  void UpdateStats(MaintenanceOpStats* stats) override {
    std::lock_guard guard(lock_);
    stats->set_runnable(state_ == OP_RUNNABLE);
    stats->set_ram_anchored(consumption_.consumption());
    stats->set_logs_retained_bytes(logs_retained_bytes_);
    stats->set_perf_improvement(perf_improvement_);
  }

  void Enable() {
    std::lock_guard guard(lock_);
    DCHECK((state_ == OP_DISABLED) || (state_ == OP_FINISHED));
    state_ = OP_RUNNABLE;
    state_change_cond_.Broadcast();
  }

  void WaitForState(TestMaintenanceOpState state) {
    std::lock_guard guard(lock_);
    while (true) {
      if (state_ == state) {
        return;
      }
      state_change_cond_.Wait();
    }
  }

  bool WaitForStateWithTimeout(TestMaintenanceOpState state, int ms) {
    MonoDelta to_wait = MonoDelta::FromMilliseconds(ms);
    std::lock_guard guard(lock_);
    while (true) {
      if (state_ == state) {
        return true;
      }
      if (!state_change_cond_.TimedWait(to_wait)) {
        return false;
      }
    }
  }

  void set_ram_anchored(uint64_t ram_anchored) {
    std::lock_guard guard(lock_);
    consumption_.Reset(ram_anchored);
  }

  void set_logs_retained_bytes(uint64_t logs_retained_bytes) {
    std::lock_guard guard(lock_);
    logs_retained_bytes_ = logs_retained_bytes;
  }

  void set_perf_improvement(uint64_t perf_improvement) {
    std::lock_guard guard(lock_);
    perf_improvement_ = perf_improvement;
  }

  scoped_refptr<EventStats> DurationHistogram() const override {
    return maintenance_op_duration_;
  }

  scoped_refptr<AtomicGauge<uint32_t> > RunningGauge() const override {
    return maintenance_ops_running_;
  }

 private:
  Mutex lock_;
  ConditionVariable state_change_cond_;
  enum TestMaintenanceOpState state_;
  ScopedTrackedConsumption consumption_;
  uint64_t logs_retained_bytes_;
  uint64_t perf_improvement_;
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
  scoped_refptr<EventStats> maintenance_op_duration_;
  scoped_refptr<AtomicGauge<uint32_t> > maintenance_ops_running_;
};

// Create an op and wait for it to start running.  Unregister it while it is
// running and verify that UnregisterOp waits for it to finish before
// proceeding.
TEST_F(MaintenanceManagerTest, TestRegisterUnregister) {
  TestMaintenanceOp op1("1", MaintenanceOp::HIGH_IO_USAGE, OP_DISABLED, test_tracker_);
  op1.set_ram_anchored(1001);
  manager_->RegisterOp(&op1);
  scoped_refptr<yb::Thread> thread;
  CHECK_OK(Thread::Create(
      "TestThread", "TestRegisterUnregister", std::bind(&TestMaintenanceOp::Enable, &op1),
      &thread));
  op1.WaitForState(OP_FINISHED);
  manager_->UnregisterOp(&op1);
  CHECK_OK(ThreadJoiner(thread.get()).Join());
}

// Test that we'll run an operation that doesn't improve performance when memory
// pressure gets high.
TEST_F(MaintenanceManagerTest, TestMemoryPressure) {
  TestMaintenanceOp op("op", MaintenanceOp::HIGH_IO_USAGE, OP_RUNNABLE, test_tracker_);
  op.set_ram_anchored(100);
  manager_->RegisterOp(&op);

  // At first, we don't want to run this, since there is no perf_improvement.
  CHECK_EQ(false, op.WaitForStateWithTimeout(OP_FINISHED, 20));

  // set the ram_anchored by the high mem op so high that we'll have to run it.
  scoped_refptr<yb::Thread> thread;
  CHECK_OK(Thread::Create(
      "TestThread", "MaintenanceManagerTest",
      std::bind(&TestMaintenanceOp::set_ram_anchored, &op, 1100), &thread));
  op.WaitForState(OP_FINISHED);
  manager_->UnregisterOp(&op);
  CHECK_OK(ThreadJoiner(thread.get()).Join());
}

// Test that ops are prioritized correctly when we add log retention.
TEST_F(MaintenanceManagerTest, TestLogRetentionPrioritization) {
  manager_->Shutdown();

  TestMaintenanceOp op1("op1", MaintenanceOp::LOW_IO_USAGE, OP_RUNNABLE, test_tracker_);
  op1.set_ram_anchored(0);
  op1.set_logs_retained_bytes(100);

  TestMaintenanceOp op2("op2", MaintenanceOp::HIGH_IO_USAGE, OP_RUNNABLE, test_tracker_);
  op2.set_ram_anchored(100);
  op2.set_logs_retained_bytes(100);

  TestMaintenanceOp op3("op3", MaintenanceOp::HIGH_IO_USAGE, OP_RUNNABLE, test_tracker_);
  op3.set_ram_anchored(200);
  op3.set_logs_retained_bytes(100);

  manager_->RegisterOp(&op1);
  manager_->RegisterOp(&op2);
  manager_->RegisterOp(&op3);

  // We want to do the low IO op first since it clears up some log retention, i.e. - op1
  // Then we find the op clears the most log retention and ram, i.e. - op3
  for (auto* op : { &op1, &op3, &op2 }) {
    {
      std::lock_guard lock(manager_->mutex_);

      ASSERT_EQ(op, manager_->FindBestOp());
    }

    manager_->UnregisterOp(op);
  }
}

// Test adding operations and make sure that the history of recently completed operations
// is correct in that it wraps around and doesn't grow.
TEST_F(MaintenanceManagerTest, TestCompletedOpsHistory) {
  for (int i = 0; i < 5; i++) {
    string name = Substitute("op$0", i);
    TestMaintenanceOp op(name, MaintenanceOp::HIGH_IO_USAGE, OP_RUNNABLE, test_tracker_);
    op.set_perf_improvement(1);
    op.set_ram_anchored(100);
    manager_->RegisterOp(&op);

    ASSERT_TRUE(op.WaitForStateWithTimeout(OP_FINISHED, 200));
    manager_->UnregisterOp(&op);

    MaintenanceManagerStatusPB status_pb;
    manager_->GetMaintenanceManagerStatusDump(&status_pb);
    // The size should be at most the history_size.
    ASSERT_GE(kHistorySize, status_pb.completed_operations_size());
    // See that we have the right name, even if we wrap around.
    ASSERT_EQ(name, status_pb.completed_operations(i % 4).name());
  }
}

} // namespace yb
