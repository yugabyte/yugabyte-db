// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the
// License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
// either express or implied. See the License for the specific language governing permissions and
// limitations under the License.

#include "yb/tablet/tablet_snapshots.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/wire_protocol-test-util.h"

#include "yb/rpc/messenger.h"

#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet-test-harness.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/tserver/backup.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/format.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/threadpool.h"

DECLARE_bool(enable_async_snapshot_directory_cleanup);
DECLARE_int32(TEST_snapshot_cleanup_retry_delay_ms);

METRIC_DECLARE_counter(snapshot_cleanup_failures);
METRIC_DECLARE_counter(snapshot_cleanup_retries);
METRIC_DECLARE_counter(snapshot_cleanup_successes);
METRIC_DECLARE_counter(snapshot_parent_sync_failures);
METRIC_DECLARE_counter(snapshot_tombstone_failures);
METRIC_DECLARE_counter(snapshot_tombstone_successes);
METRIC_DECLARE_gauge_uint64(snapshot_oldest_pending_deletion_age_ms);
METRIC_DECLARE_gauge_uint64(snapshot_pending_logical_deletions);
METRIC_DECLARE_gauge_uint64(snapshot_pending_physical_deletions);

namespace yb::tablet {
namespace {

using namespace std::literals;

class SnapshotCleanupTestEnv : public EnvWrapper {
 public:
  SnapshotCleanupTestEnv() : EnvWrapper(Env::Default()) {}

  void FailDirectoryChecks(int count) {
    directory_check_failures_.store(count, std::memory_order_release);
  }

  void FailRenames(int count) { rename_failures_.store(count, std::memory_order_release); }

  void FailSyncs(int count) { sync_failures_.store(count, std::memory_order_release); }

  void FailDeletes(int count) { delete_failures_.store(count, std::memory_order_release); }

  void BlockDeletes() {
    std::lock_guard lock(mutex_);
    block_deletes_ = true;
  }

  void ReleaseDeletes() {
    {
      std::lock_guard lock(mutex_);
      block_deletes_ = false;
    }
    condition_.notify_all();
  }

  int active_deletes() const {
    std::lock_guard lock(mutex_);
    return active_deletes_;
  }

  int max_active_deletes() const {
    std::lock_guard lock(mutex_);
    return max_active_deletes_;
  }

  int delete_calls() const { return delete_calls_.load(std::memory_order_acquire); }

  Status IsDirectory(const std::string& path, bool* is_dir) override {
    if (IsSnapshotPath(path) && ConsumeFailure(&directory_check_failures_)) {
      return STATUS(IOError, "Injected snapshot directory check failure");
    }
    return target()->IsDirectory(path, is_dir);
  }

  Status RenameFile(const std::string& source, const std::string& target_path) override {
    if (IsSnapshotPath(source) && ConsumeFailure(&rename_failures_)) {
      return STATUS(IOError, "Injected snapshot rename failure");
    }
    return target()->RenameFile(source, target_path);
  }

  Status SyncDir(const std::string& path) override {
    if (IsSnapshotPath(path) && ConsumeFailure(&sync_failures_)) {
      return STATUS(IOError, "Injected snapshot parent sync failure");
    }
    return target()->SyncDir(path);
  }

  Status DeleteRecursively(const std::string& path) override {
    if (!TabletSnapshots::IsDeletedSnapshotDir(path)) {
      return target()->DeleteRecursively(path);
    }

    delete_calls_.fetch_add(1, std::memory_order_acq_rel);
    {
      std::unique_lock lock(mutex_);
      ++active_deletes_;
      max_active_deletes_ = std::max(max_active_deletes_, active_deletes_);
      condition_.notify_all();
      condition_.wait(lock, [this] { return !block_deletes_; });
    }
    auto decrement_active = ScopeExit([this] {
      std::lock_guard lock(mutex_);
      --active_deletes_;
      condition_.notify_all();
    });

    if (ConsumeFailure(&delete_failures_)) {
      return STATUS(IOError, "Injected recursive snapshot deletion failure");
    }
    return target()->DeleteRecursively(path);
  }

 private:
  static bool ConsumeFailure(std::atomic<int>* failures) {
    int value = failures->load(std::memory_order_acquire);
    while (value > 0) {
      if (failures->compare_exchange_weak(
              value, value - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return true;
      }
    }
    return false;
  }

  static bool IsSnapshotPath(const std::string& path) {
    return path.find(".snapshots") != std::string::npos;
  }

  std::atomic<int> directory_check_failures_{0};
  std::atomic<int> rename_failures_{0};
  std::atomic<int> sync_failures_{0};
  std::atomic<int> delete_failures_{0};
  std::atomic<int> delete_calls_{0};

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  // Protected by mutex_. Thread-safety annotations are omitted because libc++'s condition-variable
  // wait does not preserve the lock capability through its predicate.
  bool block_deletes_ = false;
  int active_deletes_ = 0;
  int max_active_deletes_ = 0;
};

class TabletSnapshotsTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_async_snapshot_directory_cleanup) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_snapshot_cleanup_retry_delay_ms) = 10;

    schema_ = GetSimpleTestSchema();
    schema_.InitColumnIdsByDefault();
    test_env_ = std::make_unique<SnapshotCleanupTestEnv>();
    TabletTestHarness::Options options(GetTestPath("tablet"));
    options.env = test_env_.get();
    options.table_type = TableType::YQL_TABLE_TYPE;
    harness_ = std::make_unique<TabletTestHarness>(schema_, std::move(options));
    ASSERT_OK(harness_->Create(/* first_time = */ true));
    ASSERT_OK(harness_->Open());

    messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("snapshot-cleanup-test").Build());
    ASSERT_OK(ThreadPoolBuilder("snapshot-cleanup-test").set_max_threads(2).Build(&cleanup_pool_));
  }

  void TearDown() override {
    test_env_->ReleaseDeletes();
    if (harness_) {
      harness_->tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
      harness_->tablet()->CompleteShutdown();
      harness_.reset();
    }
    cleanup_pool_->Shutdown();
    messenger_->Shutdown();
    YBTest::TearDown();
  }

 protected:
  struct SnapshotPaths {
    std::string active;
    std::string tombstone;
  };

  void InstallCleanupPool() {
    harness_->tablet()->snapshots().SetCleanupPool(cleanup_pool_.get(), &messenger_->scheduler());
  }

  SnapshotPaths CreateSnapshotDirectory(const std::string& snapshot_id, int64_t op_index) {
    const auto snapshots_dir = harness_->tablet()->metadata()->snapshots_dir();
    CHECK_OK(test_env_->CreateDirs(snapshots_dir));
    SnapshotPaths result = {
        .active = JoinPathSegments(snapshots_dir, snapshot_id),
        .tombstone = {},
    };
    result.tombstone = TabletSnapshots::DeletedSnapshotDir(result.active, OpId(1, op_index));
    CHECK_OK(test_env_->CreateDir(result.active));
    return result;
  }

  Status DeleteSnapshot(const std::string& snapshot_id, int64_t op_index) {
    tserver::TabletSnapshotOpRequestPB request;
    request.set_snapshot_id(snapshot_id);
    SnapshotOperation operation(harness_->tablet());
    operation.AllocateRequest()->CopyFrom(request);
    operation.set_op_id(OpId(1, op_index));
    return harness_->tablet()->snapshots().Delete(operation);
  }

  template <class Metric, class Prototype>
  uint64_t MetricValue(const Prototype& prototype) {
    return harness_->tablet()->GetTabletMetricsEntity()->FindOrNull<Metric>(prototype)->value();
  }

  Status WaitForRemoved(const SnapshotPaths& paths) {
    return WaitFor(
        [this, &paths] {
          return !test_env_->FileExists(paths.active) && !test_env_->FileExists(paths.tombstone);
        },
        10s, "Wait for snapshot cleanup");
  }

  Schema schema_;
  std::unique_ptr<SnapshotCleanupTestEnv> test_env_;
  std::unique_ptr<ThreadPool> cleanup_pool_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<TabletTestHarness> harness_;
};

TEST(TabletSnapshotPathTest, AsyncCleanupDisabledByDefault) {
  ASSERT_FALSE(FLAGS_enable_async_snapshot_directory_cleanup);
}

TEST(TabletSnapshotPathTest, RecoverActivePath) {
  ASSERT_EQ(
      ASSERT_RESULT(
          TabletSnapshots::ActiveSnapshotDirFromDeletedSnapshotDir(
              "/tmp/snapshots/snapshot.id.with.dots.7.1234.deleted.tmp")),
      "/tmp/snapshots/snapshot.id.with.dots");
  ASSERT_EQ(
      ASSERT_RESULT(
          TabletSnapshots::ActiveSnapshotDirFromDeletedSnapshotDir(
              "snapshot.id.7.1234.deleted.tmp")),
      "snapshot.id");
  ASSERT_NOK(TabletSnapshots::ActiveSnapshotDirFromDeletedSnapshotDir("snapshot.id.tmp"));
  ASSERT_NOK(
      TabletSnapshots::ActiveSnapshotDirFromDeletedSnapshotDir(
          "snapshot.id.invalid.1234.deleted.tmp"));
  ASSERT_NOK(TabletSnapshots::ActiveSnapshotDirFromDeletedSnapshotDir(".7.1234.deleted.tmp"));
}

TEST_F(TabletSnapshotsTest, RetainsDeletionUntilCleanupPoolIsInstalled) {
  const auto paths = CreateSnapshotDirectory("snapshot.before.pool", 1);
  ASSERT_OK(DeleteSnapshot("snapshot.before.pool", 1));
  ASSERT_FALSE(test_env_->FileExists(paths.active));
  ASSERT_TRUE(test_env_->FileExists(paths.tombstone));
  ASSERT_EQ(MetricValue<AtomicGauge<uint64_t>>(METRIC_snapshot_pending_physical_deletions), 1);

  test_env_->BlockDeletes();
  SleepFor(5ms);
  InstallCleanupPool();
  ASSERT_OK(WaitFor(
      [this] { return test_env_->active_deletes() == 1; }, 10s,
      "Wait for retained snapshot cleanup"));
  const auto pending_age =
      MetricValue<FunctionGauge<uint64_t>>(METRIC_snapshot_oldest_pending_deletion_age_ms);
  ASSERT_GT(pending_age, 0);
  SleepFor(5ms);
  ASSERT_GT(
      MetricValue<FunctionGauge<uint64_t>>(METRIC_snapshot_oldest_pending_deletion_age_ms),
      pending_age);
  test_env_->ReleaseDeletes();
  ASSERT_OK(WaitForRemoved(paths));
  ASSERT_EQ(MetricValue<AtomicGauge<uint64_t>>(METRIC_snapshot_pending_physical_deletions), 0);
}

TEST_F(TabletSnapshotsTest, RetriesDirectoryCheckAndRenameFailures) {
  InstallCleanupPool();
  const auto paths = CreateSnapshotDirectory("snapshot.rename.retry", 2);
  test_env_->FailDirectoryChecks(2);
  test_env_->FailRenames(2);
  test_env_->BlockDeletes();

  ASSERT_OK(DeleteSnapshot("snapshot.rename.retry", 2));
  ASSERT_OK(WaitFor(
      [this] { return test_env_->active_deletes() == 1; }, 10s,
      "Wait for retried snapshot deletion"));
  ASSERT_EQ(MetricValue<AtomicGauge<uint64_t>>(METRIC_snapshot_pending_logical_deletions), 0);
  ASSERT_EQ(MetricValue<AtomicGauge<uint64_t>>(METRIC_snapshot_pending_physical_deletions), 1);
  test_env_->ReleaseDeletes();
  ASSERT_OK(WaitForRemoved(paths));
  ASSERT_GE(MetricValue<Counter>(METRIC_snapshot_tombstone_failures), 4);
  ASSERT_GE(MetricValue<Counter>(METRIC_snapshot_cleanup_retries), 1);
  ASSERT_EQ(MetricValue<Counter>(METRIC_snapshot_tombstone_successes), 1);
}

TEST_F(TabletSnapshotsTest, RetriesParentSyncFailure) {
  InstallCleanupPool();
  const auto paths = CreateSnapshotDirectory("snapshot.sync.retry", 3);
  test_env_->FailSyncs(2);

  ASSERT_OK(DeleteSnapshot("snapshot.sync.retry", 3));
  ASSERT_OK(WaitForRemoved(paths));
  ASSERT_EQ(MetricValue<Counter>(METRIC_snapshot_parent_sync_failures), 2);
  ASSERT_GE(MetricValue<Counter>(METRIC_snapshot_cleanup_retries), 1);
}

TEST_F(TabletSnapshotsTest, RetriesPhysicalDeletionFailure) {
  InstallCleanupPool();
  const auto paths = CreateSnapshotDirectory("snapshot.delete.retry", 4);
  test_env_->FailDeletes(1);

  ASSERT_OK(DeleteSnapshot("snapshot.delete.retry", 4));
  ASSERT_OK(WaitForRemoved(paths));
  ASSERT_EQ(MetricValue<Counter>(METRIC_snapshot_cleanup_failures), 1);
  ASSERT_EQ(MetricValue<Counter>(METRIC_snapshot_cleanup_successes), 1);
  ASSERT_GE(MetricValue<Counter>(METRIC_snapshot_cleanup_retries), 1);
}

TEST_F(TabletSnapshotsTest, CoalescesDuplicateDeletionRequests) {
  InstallCleanupPool();
  const auto paths = CreateSnapshotDirectory("snapshot.duplicate", 5);
  test_env_->BlockDeletes();
  ASSERT_OK(DeleteSnapshot("snapshot.duplicate", 5));
  ASSERT_OK(WaitFor(
      [this] { return test_env_->active_deletes() == 1; }, 10s,
      "Wait for first snapshot deletion"));
  ASSERT_OK(DeleteSnapshot("snapshot.duplicate", 5));
  ASSERT_EQ(MetricValue<AtomicGauge<uint64_t>>(METRIC_snapshot_pending_physical_deletions), 1);

  test_env_->ReleaseDeletes();
  ASSERT_OK(WaitForRemoved(paths));
  ASSERT_OK(WaitFor(
      [this] {
        return MetricValue<AtomicGauge<uint64_t>>(METRIC_snapshot_pending_physical_deletions) == 0;
      },
      10s, "Wait for duplicate snapshot deletion to complete"));
  ASSERT_EQ(test_env_->delete_calls(), 1);
}

TEST_F(TabletSnapshotsTest, RetriesFinalParentSyncFailure) {
  InstallCleanupPool();
  const auto paths = CreateSnapshotDirectory("snapshot.final.sync", 5);
  test_env_->BlockDeletes();
  ASSERT_OK(DeleteSnapshot("snapshot.final.sync", 5));
  ASSERT_OK(WaitFor(
      [this] { return test_env_->active_deletes() == 1; }, 10s,
      "Wait for snapshot physical deletion"));
  test_env_->FailSyncs(1);
  test_env_->ReleaseDeletes();

  ASSERT_OK(WaitForRemoved(paths));
  ASSERT_OK(WaitFor(
      [this] {
        return MetricValue<AtomicGauge<uint64_t>>(METRIC_snapshot_pending_physical_deletions) == 0;
      },
      10s, "Wait for final snapshot parent sync retry"));
  ASSERT_EQ(MetricValue<Counter>(METRIC_snapshot_parent_sync_failures), 1);
  ASSERT_GE(MetricValue<Counter>(METRIC_snapshot_cleanup_retries), 1);
}

TEST_F(TabletSnapshotsTest, ScanRecoversActiveAndTombstonePair) {
  const auto paths = CreateSnapshotDirectory("snapshot.recovered", 5);
  ASSERT_OK(test_env_->CreateDir(paths.tombstone));

  InstallCleanupPool();
  ASSERT_OK(WaitForRemoved(paths));
  ASSERT_EQ(MetricValue<Counter>(METRIC_snapshot_cleanup_successes), 2);
  ASSERT_EQ(MetricValue<Counter>(METRIC_snapshot_tombstone_successes), 1);
}

TEST_F(TabletSnapshotsTest, SharedPoolBoundsCleanupConcurrency) {
  constexpr size_t kNumTablets = 4;
  std::vector<std::unique_ptr<TabletTestHarness>> harnesses;
  std::vector<SnapshotPaths> paths;
  harnesses.reserve(kNumTablets);
  paths.reserve(kNumTablets);
  test_env_->BlockDeletes();
  auto release_deletes = ScopeExit([this] { test_env_->ReleaseDeletes(); });

  for (size_t i = 0; i != kNumTablets; ++i) {
    TabletTestHarness::Options options(GetTestPath(Format("tablet-$0", i)));
    options.env = test_env_.get();
    options.tablet_id = Format("snapshot-tablet-$0", i);
    options.table_type = TableType::YQL_TABLE_TYPE;
    auto tablet_harness = std::make_unique<TabletTestHarness>(schema_, std::move(options));
    ASSERT_OK(tablet_harness->Create(/* first_time = */ true));
    ASSERT_OK(tablet_harness->Open());
    tablet_harness->tablet()->snapshots().SetCleanupPool(
        cleanup_pool_.get(), &messenger_->scheduler());

    const auto snapshot_id = Format("snapshot-$0", i);
    const auto active =
        JoinPathSegments(tablet_harness->tablet()->metadata()->snapshots_dir(), snapshot_id);
    ASSERT_OK(test_env_->CreateDirs(active));
    const auto op_index = static_cast<int64_t>(i + 1);
    paths.push_back({
        .active = active,
        .tombstone = TabletSnapshots::DeletedSnapshotDir(active, OpId(1, op_index)),
    });

    tserver::TabletSnapshotOpRequestPB request;
    request.set_snapshot_id(snapshot_id);
    SnapshotOperation operation(tablet_harness->tablet());
    operation.AllocateRequest()->CopyFrom(request);
    operation.set_op_id(OpId(1, op_index));
    ASSERT_OK(tablet_harness->tablet()->snapshots().Delete(operation));
    harnesses.push_back(std::move(tablet_harness));
  }

  ASSERT_OK(WaitFor(
      [this] { return test_env_->active_deletes() == 2; }, 10s,
      "Wait for snapshot cleanup pool saturation"));
  ASSERT_EQ(test_env_->max_active_deletes(), 2);
  test_env_->ReleaseDeletes();
  for (const auto& path : paths) {
    ASSERT_OK(WaitFor(
        [this, &path] {
          return !test_env_->FileExists(path.active) && !test_env_->FileExists(path.tombstone);
        },
        10s, "Wait for bounded snapshot cleanup"));
  }

  for (auto& tablet_harness : harnesses) {
    tablet_harness->tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
    tablet_harness->tablet()->CompleteShutdown();
  }
}

TEST_F(TabletSnapshotsTest, ShutdownWaitsForRunningCleanup) {
  InstallCleanupPool();
  const auto paths = CreateSnapshotDirectory("snapshot.shutdown", 6);
  test_env_->BlockDeletes();
  ASSERT_OK(DeleteSnapshot("snapshot.shutdown", 6));
  ASSERT_OK(WaitFor(
      [this] { return test_env_->active_deletes() == 1; }, 10s,
      "Wait for blocked snapshot cleanup"));

  std::atomic<bool> shutdown_complete = false;
  TestThreadHolder shutdown_thread;
  shutdown_thread.AddThreadFunctor([this, &shutdown_complete] {
    harness_->tablet()->snapshots().StartShutdown();
    harness_->tablet()->snapshots().CompleteShutdown();
    shutdown_complete.store(true, std::memory_order_release);
  });
  SleepFor(50ms);
  ASSERT_FALSE(shutdown_complete.load(std::memory_order_acquire));

  test_env_->ReleaseDeletes();
  shutdown_thread.JoinAll();
  ASSERT_TRUE(shutdown_complete.load(std::memory_order_acquire));
  ASSERT_FALSE(test_env_->FileExists(paths.tombstone));
}

}  // namespace
}  // namespace yb::tablet
