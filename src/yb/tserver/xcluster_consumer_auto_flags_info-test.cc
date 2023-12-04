// Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/xcluster_consumer_auto_flags_info.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

using namespace std::chrono_literals;

namespace yb::tserver::xcluster {

const cdc::ReplicationGroupId kReplicationGroupId1("Group1");
const cdc::ReplicationGroupId kReplicationGroupId2("Group2");

class AutoFlagsVersionHandlerTest : public AutoFlagsVersionHandler {
 public:
  AutoFlagsVersionHandlerTest() : AutoFlagsVersionHandler(nullptr) {}

  std::function<Status()> report_auto_flags_version_callback_ = {};

 protected:
  Status XClusterReportNewAutoFlagConfigVersion(
      const cdc::ReplicationGroupId& replication_group_id, uint32 new_version) const override {
    if (report_auto_flags_version_callback_) {
      return report_auto_flags_version_callback_();
    }
    return Status::OK();
  }
};

// Validate the InsertOrUpdate and Delete function.
TEST(AutoFlagsVersionHandlerTest, TestInsertAndUpdate) {
  AutoFlagsVersionHandlerTest handler;

  // Empty map.
  ASSERT_EQ(handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId1), nullptr);
  ASSERT_EQ(handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId2), nullptr);

  // Insert one replication group.
  handler.InsertOrUpdate(kReplicationGroupId1, 1, 1);
  auto auto_flags_info = handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId1);
  ASSERT_EQ(auto_flags_info->GetCompatibleVersion(), 1);

  // Inserting a different replication group should not affect the first one.
  handler.InsertOrUpdate(kReplicationGroupId2, 3, 3);
  ASSERT_EQ(auto_flags_info->GetCompatibleVersion(), 1);
  ASSERT_EQ(handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId2)->GetCompatibleVersion(), 3);

  // Update the first replication group.
  handler.InsertOrUpdate(kReplicationGroupId1, 2, 4);
  ASSERT_EQ(auto_flags_info->GetCompatibleVersion(), 2);

  // Delete the second group.
  handler.Delete(kReplicationGroupId2);
  ASSERT_EQ(handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId2), nullptr);

  // First group should still exist and remain the same.
  auto auto_flags_info_temp = handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId1);
  ASSERT_NE(auto_flags_info_temp, nullptr);
  ASSERT_EQ(auto_flags_info_temp.get(), auto_flags_info.get());
  ASSERT_EQ(auto_flags_info->GetCompatibleVersion(), 2);

  // Delete the first group.
  handler.Delete(kReplicationGroupId1);
  ASSERT_EQ(handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId1), nullptr);
}

// Make sure we only report the new version to master only once.
TEST(AutoFlagsVersionHandlerTest, DuplicateReporting) {
  AutoFlagsVersionHandlerTest handler;
  int count = 0;
  handler.report_auto_flags_version_callback_ = [&count]() -> Status {
    count++;
    return Status::OK();
  };

  handler.InsertOrUpdate(kReplicationGroupId1, 1, 1);
  auto auto_flags_info = handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId1);
  ASSERT_EQ(count, 0);

  // Report a new version on a replication group that does not exist.
  ASSERT_NOK(handler.ReportNewAutoFlagConfigVersion(kReplicationGroupId2, 2));
  ASSERT_EQ(count, 0);

  // Report a new version on a replication group that exists.
  ASSERT_OK(handler.ReportNewAutoFlagConfigVersion(kReplicationGroupId1, 2));
  ASSERT_EQ(count, 1);
  ASSERT_EQ(auto_flags_info->GetCompatibleVersion(), 1);
  ASSERT_EQ(auto_flags_info->max_reported_version_, 2);

  // Report the same version again should not increase the count.
  ASSERT_OK(handler.ReportNewAutoFlagConfigVersion(kReplicationGroupId1, 2));
  ASSERT_EQ(count, 1);
  ASSERT_EQ(auto_flags_info->GetCompatibleVersion(), 1);
  ASSERT_EQ(auto_flags_info->max_reported_version_, 2);

  // Higher compatible version and lower max reported version should not trigger the callback.
  handler.InsertOrUpdate(kReplicationGroupId1, 3, 1);
  ASSERT_OK(handler.ReportNewAutoFlagConfigVersion(kReplicationGroupId1, 2));
  ASSERT_EQ(count, 1);
  ASSERT_EQ(auto_flags_info->GetCompatibleVersion(), 3);
  ASSERT_EQ(auto_flags_info->max_reported_version_, 1);

  // Higher max reported and lower compatible version version should not trigger the callback.
  handler.InsertOrUpdate(kReplicationGroupId1, 1, 3);
  ASSERT_OK(handler.ReportNewAutoFlagConfigVersion(kReplicationGroupId1, 2));
  ASSERT_EQ(count, 1);
  ASSERT_EQ(auto_flags_info->GetCompatibleVersion(), 1);
  ASSERT_EQ(auto_flags_info->max_reported_version_, 3);
}

// Validate that only one thread reports the new version to master.
TEST(AutoFlagsVersionHandlerTest, ConcurrentReporting) {
  AutoFlagsVersionHandlerTest handler;
  handler.InsertOrUpdate(kReplicationGroupId1, 1, 1);

  int count = 0;
  handler.report_auto_flags_version_callback_ = [&count]() -> Status {
    count++;
    return Status::OK();
  };

  TestThreadHolder thread_holder;
  const int kNumThreads = 20;

  for (int i = 0; i < kNumThreads; ++i) {
    if (i % 2) {
      thread_holder.AddThreadFunctor([&handler]() {
        ASSERT_OK(handler.ReportNewAutoFlagConfigVersion(kReplicationGroupId1, 2));
      });
    } else {
      thread_holder.AddThreadFunctor(
          [&handler]() { handler.InsertOrUpdate(kReplicationGroupId1, 2, 2); });
    }
  }

  thread_holder.JoinAll();
  ASSERT_LE(count, 1);
  ASSERT_EQ(handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId1)->max_reported_version_, 2);
}

// Validate that when we report a new version to master none of the other functions get blocked.
TEST(AutoFlagsVersionHandlerTest, NonBlockingReporting) {
  AutoFlagsVersionHandlerTest handler;
  handler.InsertOrUpdate(kReplicationGroupId1, 1, 1);

  int count = 0;
  std::mutex mutex;
  handler.report_auto_flags_version_callback_ = [&mutex, &count]() -> Status {
    std::lock_guard l(mutex);
    count++;
    return Status::OK();
  };

  // Get the lock to block the callback.
  std::unique_lock ul(mutex);
  scoped_refptr<Thread> thread;
  ASSERT_OK(Thread::Create(
      "TestThread", "TestThread",
      [&handler]() { ASSERT_OK(handler.ReportNewAutoFlagConfigVersion(kReplicationGroupId1, 2)); },
      &thread));

  // Make sure thread is blocked.
  ASSERT_NOK(ThreadJoiner(thread.get()).give_up_after(1s).Join());
  ASSERT_EQ(count, 0);

  // We should be able to add a new replication group.
  handler.InsertOrUpdate(kReplicationGroupId2, 1, 1);

  // We should be able to update the first replication group.
  handler.InsertOrUpdate(kReplicationGroupId1, 2, 3);

  // Unblock the callback.
  ul.unlock();
  ASSERT_OK(ThreadJoiner(thread.get()).give_up_after(1s).Join());
  ASSERT_EQ(count, 1);

  // It should not lower the max reported version.
  ASSERT_EQ(handler.GetAutoFlagsCompatibleVersion(kReplicationGroupId1)->max_reported_version_, 3);
}

}  // namespace yb::tserver::xcluster
