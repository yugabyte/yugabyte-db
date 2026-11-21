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

#include <gtest/gtest.h>

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_client.pb.h"

#include "yb/util/format.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

namespace yb {
namespace master {

class CatalogEntityInfoTest : public YBTest {};

// Verify that data mutations are not available from metadata() until commit.
TEST_F(CatalogEntityInfoTest, TestNamespaceInfoCommit) {
  scoped_refptr<NamespaceInfo> ns(
      new NamespaceInfo("deadbeafdeadbeafdeadbeafdeadbeaf", /*tasks_tracker=*/nullptr));

  // Mutate the namespace, under the write lock.
  auto writer_lock = ns->LockForWrite();
  writer_lock.mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    auto reader_lock = ns->LockForRead();
    ASSERT_NE("foo", reader_lock->name());
  }

  // Commit the changes
  writer_lock.Commit();

  // Verify that the data is visible
  {
    auto reader_lock = ns->LockForRead();
    ASSERT_EQ("foo", reader_lock->name());
  }
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(CatalogEntityInfoTest, TestTableInfoCommit) {
  scoped_refptr<TableInfo> table =
      make_scoped_refptr<TableInfo>("123" /* table_id */, false /* colocated */);

  // Mutate the table, under the write lock.
  auto writer_lock = table->LockForWrite();
  writer_lock.mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    auto reader_lock = table->LockForRead();
    ASSERT_NE("foo", reader_lock->name());
  }
  writer_lock.mutable_data()->set_state(SysTablesEntryPB::RUNNING, "running");

  {
    auto reader_lock = table->LockForRead();
    ASSERT_NE("foo", reader_lock->pb.name());
    ASSERT_NE("running", reader_lock->pb.state_msg());
    ASSERT_NE(SysTablesEntryPB::RUNNING, reader_lock->pb.state());
  }

  // Commit the changes
  writer_lock.Commit();

  // Verify that the data is visible
  {
    auto reader_lock = table->LockForRead();
    ASSERT_EQ("foo", reader_lock->pb.name());
    ASSERT_EQ("running", reader_lock->pb.state_msg());
    ASSERT_EQ(SysTablesEntryPB::RUNNING, reader_lock->pb.state());
  }
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(CatalogEntityInfoTest, TestTabletInfoCommit) {
  auto tablet = std::make_shared<TabletInfo>(nullptr, "123");

  // Mutate the tablet, the changes should not be visible
  auto l = tablet->LockForWrite();
  PartitionPB* partition = l.mutable_data()->pb.mutable_partition();
  partition->set_partition_key_start("a");
  partition->set_partition_key_end("b");
  l.mutable_data()->set_state(SysTabletsEntryPB::RUNNING, "running");
  {
    // Changes shouldn't be visible, and lock should still be
    // acquired even though the mutation is under way.
    auto read_lock = tablet->LockForRead();
    ASSERT_NE("a", read_lock->pb.partition().partition_key_start());
    ASSERT_NE("b", read_lock->pb.partition().partition_key_end());
    ASSERT_NE("running", read_lock->pb.state_msg());
    ASSERT_NE(SysTabletsEntryPB::RUNNING,
              read_lock->pb.state());
  }

  // Commit the changes
  l.Commit();

  // Verify that the data is visible
  {
    auto read_lock = tablet->LockForRead();
    ASSERT_EQ("a", read_lock->pb.partition().partition_key_start());
    ASSERT_EQ("b", read_lock->pb.partition().partition_key_end());
    ASSERT_EQ("running", read_lock->pb.state_msg());
    ASSERT_EQ(SysTabletsEntryPB::RUNNING,
              read_lock->pb.state());
  }
}

class MockMonitoredTask : public server::MonitoredTask {
 public:
  explicit MockMonitoredTask(server::MonitoredTaskType type) : type_(type) {}
  virtual ~MockMonitoredTask() {}

  // Abort this task and return its value before it was successfully aborted. If the task entered
  // a different terminal state before we were able to abort it, return that state.
  server::MonitoredTaskState AbortAndReturnPrevState(
      const Status& status, bool call_task_finisher) override {
    auto prev_state = state_.load();
    state_ = server::MonitoredTaskState::kAborted;
    return prev_state;
  }
  server::MonitoredTaskType type() const override { return type_; }
  std::string type_name() const override { return "dummy"; }
  std::string description() const override { return "dummy"; }

  server::MonitoredTaskState State() const { return state_.load(); }

  const server::MonitoredTaskType type_;
};

TEST_F(CatalogEntityInfoTest, TestTasks) {
  scoped_refptr<TasksTracker> tasks_tracker(new TasksTracker);
  CatalogEntityWithTasks entity(tasks_tracker);

  ASSERT_EQ(entity.NumTasks(), 0);

  const auto type1 = server::MonitoredTaskType::kAddServer;
  const auto type2 = server::MonitoredTaskType::kAddTableToTablet;

  auto task1 = std::shared_ptr<server::MonitoredTask>(new MockMonitoredTask(type1));

  entity.AddTask(task1);
  ASSERT_TRUE(entity.HasTasks());
  ASSERT_EQ(entity.NumTasks(), 1);
  ASSERT_EQ(entity.HasTasks(type1), 1);
  ASSERT_FALSE(entity.HasTasks(type2));
  ASSERT_EQ(tasks_tracker->GetTasks().size(), 1);
  ASSERT_TRUE(entity.GetTasks().contains(task1));

  // Add more tasks.
  auto task2 = std::shared_ptr<server::MonitoredTask>(new MockMonitoredTask(type2));
  auto task3 = std::shared_ptr<server::MonitoredTask>(new MockMonitoredTask(type1));
  entity.AddTask(task2);
  entity.AddTask(task3);
  ASSERT_TRUE(entity.HasTasks());
  ASSERT_EQ(entity.NumTasks(), 3);
  ASSERT_TRUE(entity.HasTasks(type1));
  ASSERT_TRUE(entity.HasTasks(type2));
  ASSERT_EQ(tasks_tracker->GetTasks().size(), 3);
  ASSERT_TRUE(entity.GetTasks().contains(task1));
  ASSERT_TRUE(entity.GetTasks().contains(task2));
  ASSERT_TRUE(entity.GetTasks().contains(task3));

  // Remove 1 task.
  ASSERT_FALSE(entity.RemoveTask(task1));
  // Remove is idempotent.
  ASSERT_FALSE(entity.RemoveTask(task1));
  ASSERT_TRUE(entity.HasTasks());
  ASSERT_EQ(entity.NumTasks(), 2);
  ASSERT_TRUE(entity.HasTasks(type1));
  ASSERT_TRUE(entity.HasTasks(type2));
  ASSERT_FALSE(entity.GetTasks().contains(task1));
  ASSERT_TRUE(entity.GetTasks().contains(task2));
  ASSERT_TRUE(entity.GetTasks().contains(task3));

  // Abort all tasks but dont close.
  entity.AbortTasks();
  ASSERT_EQ(entity.NumTasks(), 2);
  ASSERT_EQ(task2->state(), server::MonitoredTaskState::kAborted);
  ASSERT_EQ(task3->state(), server::MonitoredTaskState::kAborted);
  // Removed task should not get affected.
  ASSERT_EQ(task1->state(), server::MonitoredTaskState::kWaiting);

  const auto kDelay = MonoDelta::FromSeconds(2);
  {
    TestThreadHolder thread_holder;
    thread_holder.AddThreadFunctor([&kDelay, &entity, &task2, &task3]() {
      entity.RemoveTask(task2);
      SleepFor(kDelay * 2);
      entity.RemoveTask(task3);
    });

    auto start = MonoTime::Now();
    // We should have waited for atleast kDelay before completion.
    entity.WaitTasksCompletion();
    auto elapsed = MonoTime::Now() - start;
    ASSERT_GT(elapsed, kDelay);
    ASSERT_EQ(entity.NumTasks(), 0);
    ASSERT_FALSE(entity.HasTasks());

    thread_holder.JoinAll();
  }

  // Enqueue more tasks.
  auto task4 = std::shared_ptr<server::MonitoredTask>(new MockMonitoredTask(type1));
  auto task5 = std::shared_ptr<server::MonitoredTask>(new MockMonitoredTask(type2));
  entity.AddTask(task4);
  entity.AddTask(task5);
  ASSERT_EQ(entity.NumTasks(), 2);

  // Abort all tasks and close.
  entity.AbortTasksAndClose(/* call_task_finisher */ true);
  ASSERT_EQ(task4->state(), server::MonitoredTaskState::kAborted);
  ASSERT_EQ(task5->state(), server::MonitoredTaskState::kAborted);

  {
    TestThreadHolder thread_holder;
    thread_holder.AddThreadFunctor([&kDelay, &entity, &task4, &task5]() {
      SleepFor(kDelay * 2);
      entity.RemoveTask(task4);
      entity.RemoveTask(task5);
    });

    auto start = MonoTime::Now();
    // We should have waited for atleast kDelay before completion.
    entity.WaitTasksCompletion();
    auto elapsed = MonoTime::Now() - start;
    ASSERT_GT(elapsed, kDelay);

    thread_holder.JoinAll();
  }

  // We should no longer be able to add tasks.
  auto task6 = std::shared_ptr<server::MonitoredTask>(new MockMonitoredTask(type1));
  entity.AddTask(task6);
  ASSERT_FALSE(entity.HasTasks());
  ASSERT_EQ(task6->state(), server::MonitoredTaskState::kAborted);
  entity.WaitTasksCompletion();
  ASSERT_EQ(tasks_tracker->GetTasks().size(), 5);
}

// Tablets created at table creation time own a closed interval on their start key and an open one
// on their end key.
TEST_F(CatalogEntityInfoTest, TestAssignmentRanges) {
  const std::string table_id = CURRENT_TEST_NAME();
  scoped_refptr<TableInfo> table(new TableInfo(table_id, /* colocated */ false));
  std::vector<TabletInfoPtr> tablets;

  const std::vector<std::string> split_keys = {"a", "b", "c"};
  const size_t kNumSplits = split_keys.size();
  for (size_t i = 0; i <= kNumSplits; i++) {
    const std::string& start_key = (i == 0) ? "" : split_keys[i - 1];
    const std::string& end_key = (i == kNumSplits) ? "" : split_keys[i];
    auto tablet = std::make_shared<TabletInfo>(table, Format("tablet-$0-$1", start_key, end_key));
    auto l = tablet->LockForWrite();
    auto* partition = l.mutable_data()->pb.mutable_partition();
    partition->set_partition_key_start(start_key);
    partition->set_partition_key_end(end_key);
    l.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);
    ASSERT_OK(table->AddTablet(tablet));
    l.Commit();
    tablets.push_back(tablet);
  }

  for (size_t i = 0; i <= kNumSplits; i++) {
    const std::string& start_key = (i == 0) ? "" : split_keys[i - 1];
    const std::string& end_key = (i == kNumSplits) ? "" : split_keys[i];
    const auto tablet_id = Format("tablet-$0-$1", start_key, end_key);

    GetTableLocationsRequestPB req;
    req.set_max_returned_locations(1);
    req.mutable_table()->mutable_table_name()->assign(table_id);
    req.mutable_partition_key_start()->assign(start_key);
    auto tablets_in_range = ASSERT_RESULT(table->GetTabletsInRange(&req));

    // Only the tablet whose range starts at the key owns it.
    ASSERT_EQ(1, tablets_in_range.size());
    ASSERT_EQ(tablet_id, (*tablets_in_range.begin())->tablet_id());
  }

  for (const auto& tablet : tablets) {
    auto lock = tablet->LockForWrite();
    ASSERT_TRUE(ASSERT_RESULT(table->RemoveTablet(tablet->id())));
  }
}

} // namespace master
} // namespace yb
