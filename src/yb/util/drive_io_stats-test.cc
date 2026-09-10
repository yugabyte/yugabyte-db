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
//
// Tests for per-drive write-IO accounting. The design being tested is in drive_io_stats.h.
//
// Three groups, in file order: which drive a path is attributed to; the unsynced-bytes contract,
// which is the only part of the accounting a caller can get wrong; and the export onto the 'drive'
// metric entity. The first two are driven against DriveIoStats directly where a real file cannot
// reach the case - notably the O_DIRECT path, which would otherwise need a filesystem that
// supports O_DIRECT.
//
// Two things to know before adding a case here. The registry is process-global and entries are
// never removed, so each test takes roots of its own instead of resetting it (see UniqueRoot).
// And YBTest sets FLAGS_never_fsync, which makes DoSync() return and Flush() skip
// sync_file_range entirely - so a test that means to measure a real syscall has to clear it, and
// only the two that say so do.

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/drive_io_stats.h"
#include "yb/util/env.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

// FsManager owns this entity prototype in the real server; util tests do not link yb_fs, so
// define it here. The name has to match the entity the drive_* prototypes were defined against,
// which MetricEntity::FindOrCreateMetric checks.
METRIC_DEFINE_entity(drive);

METRIC_DECLARE_gauge_uint64(drive_bytes_written);
METRIC_DECLARE_gauge_uint64(drive_sync_count);
METRIC_DECLARE_gauge_uint64(drive_bytes_unsynced);
METRIC_DECLARE_event_stats(drive_sync_latency);

DECLARE_bool(never_fsync);

namespace yb {

class DriveIoStatsTest : public YBTest {
 protected:
  // The registry is process-global and entries are never removed, so every test uses roots unique
  // to itself rather than resetting shared state - a reset would dangle the DriveIoStats* that
  // open files cache for their whole lifetime.
  std::string UniqueRoot(const std::string& suffix) const {
    return JoinPathSegments(
        "/drive-io-stats-test",
        JoinPathSegments(
            ::testing::UnitTest::GetInstance()->current_test_info()->name(), suffix));
  }

  DriveIoStatsRegistry& registry() { return DriveIoStatsRegistry::Instance(); }

  MetricRegistry metric_registry_;
};

TEST_F(DriveIoStatsTest, AttributesByPathBoundary) {
  const auto d1 = UniqueRoot("mnt/d1");
  const auto d10 = UniqueRoot("mnt/d10");
  auto* s1 = &registry().Register(d1, nullptr);
  auto* s10 = &registry().Register(d10, nullptr);
  ASSERT_NE(s1, s10);

  // The prefix match is path-boundary aligned, so ".../d1" must not swallow ".../d10".
  ASSERT_EQ(s1, registry().Find(d1 + "/yb-data/tserver/wals/tablet/wal-000001"));
  ASSERT_EQ(s10, registry().Find(d10 + "/yb-data/tserver/wals/tablet/wal-000001"));
  ASSERT_EQ(s1, registry().Find(d1));

  // Nothing outside a registered root is attributed, and there is no partial-component match.
  ASSERT_EQ(nullptr, registry().Find(UniqueRoot("mnt/d2") + "/yb-data"));
  ASSERT_EQ(nullptr, registry().Find(d1 + "x/yb-data"));
  ASSERT_EQ(nullptr, registry().Find(UniqueRoot("mnt")));

  ASSERT_EQ(d10, registry().FindRoot(d10 + "/yb-data/tserver"));
  ASSERT_EQ("", registry().FindRoot(UniqueRoot("mnt/d2") + "/yb-data/tserver"));
}

// A WAL directory pointed at a subdirectory of a data root is a legitimate configuration, so the
// longest registered root has to win rather than whichever was registered first.
TEST_F(DriveIoStatsTest, LongestRootWins) {
  const auto outer_root = UniqueRoot("mnt/d1");
  auto* outer = &registry().Register(outer_root, nullptr);
  auto* inner = &registry().Register(outer_root + "/wals", nullptr);

  ASSERT_EQ(inner, registry().Find(outer_root + "/wals/tablet/wal-000001"));
  ASSERT_EQ(outer, registry().Find(outer_root + "/yb-data/tserver/data/rocksdb/x.sst"));
}

TEST_F(DriveIoStatsTest, RegisterIsIdempotent) {
  const auto root = UniqueRoot("mnt/d1");
  ASSERT_EQ(&registry().Register(root, nullptr), &registry().Register(root, nullptr));
}

// The unsynced gauge is an upper bound maintained from two directions, so the contract of each
// entry point matters more than any single number. Exercised directly, because the O_DIRECT path
// that motivates RecordDirectWrite needs a filesystem that supports O_DIRECT to reach through a
// real file.
TEST_F(DriveIoStatsTest, UnsyncedBytesAccounting) {
  auto* stats = &registry().Register(UniqueRoot("mnt/d1"), nullptr);
  const auto elapsed = MonoDelta::FromMicroseconds(10);

  // Buffered writes owe an unsynced debt...
  stats->RecordBufferedWrite(1000, elapsed);
  ASSERT_EQ(1000, stats->bytes_written());
  ASSERT_EQ(1000, stats->bytes_unsynced());
  // ...which a sync settles.
  stats->RecordSync(1000, elapsed);
  ASSERT_EQ(0, stats->bytes_unsynced());
  ASSERT_EQ(1, stats->sync_count());

  // O_DIRECT writes owe nothing: the bytes are already on the device and this class's Sync() is
  // the write itself, so no RecordSync will ever arrive to settle a debt. If RecordDirectWrite
  // bumped the gauge, it would climb to equal bytes_written and never come down.
  stats->RecordDirectWrite(4096, elapsed);
  ASSERT_EQ(5096, stats->bytes_written());
  ASSERT_EQ(0, stats->bytes_unsynced());

  // A file closed without syncing releases its residual rather than leaking it.
  stats->RecordBufferedWrite(700, elapsed);
  ASSERT_EQ(700, stats->bytes_unsynced());
  stats->ReleaseUnsyncedBytes(700);
  ASSERT_EQ(0, stats->bytes_unsynced());

  // Over-settling saturates at zero instead of wrapping.
  stats->RecordSync(999999, elapsed);
  ASSERT_EQ(0, stats->bytes_unsynced());

  ASSERT_GT(stats->write_micros(), 0);
  ASSERT_GT(stats->sync_micros(), 0);
}

// One DriveIoStats is shared by every tablet's WAL and every RocksDB flush on the mount, so
// concurrent updates are the normal case. All of these counters are updated relaxed; this pins
// what that does and does not give up. Each update is a read-modify-write on a single atomic, so
// none is lost, and the exact totals below hold no matter how the threads interleave. What
// relaxed gives up is any ordering between the counters, which is why no assertion here reads two
// of them together while writers are running.
TEST_F(DriveIoStatsTest, ConcurrentUpdatesLoseNothing) {
  auto* stats = &registry().Register(UniqueRoot("mnt/d1"), nullptr);

  constexpr int kThreads = 8;
  // Scaled down under sanitizers: TSAN's race detection is happens-before-based, so it does not
  // need volume, and the exact-totals check needs collisions only in regular builds.
  constexpr int kIterations = RegularBuildVsSanitizers(5000, 500);
  constexpr uint64_t kBytes = 7;
  constexpr int64_t kMicros = 10;
  const auto elapsed = MonoDelta::FromMicroseconds(kMicros);

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([stats, elapsed]() {
      for (int j = 0; j < kIterations; ++j) {
        // Each thread settles its own debt, so cumulative syncs never run ahead of cumulative
        // writes and the gauge's saturation at zero is deliberately not in play here.
        stats->RecordBufferedWrite(kBytes, elapsed);
        stats->RecordSync(kBytes, elapsed);
        stats->RecordDirectWrite(kBytes, elapsed);
        stats->RecordRangeSync(elapsed);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  constexpr uint64_t kOps = kThreads * kIterations;
  ASSERT_EQ(2 * kOps * kBytes, stats->bytes_written());
  ASSERT_EQ(2 * kOps * kMicros, stats->write_micros());
  ASSERT_EQ(kOps, stats->sync_count());
  ASSERT_EQ(kOps * kMicros, stats->sync_micros());
  ASSERT_EQ(kOps, stats->range_sync_count());
  ASSERT_EQ(kOps * kMicros, stats->range_sync_micros());
  ASSERT_EQ(0, stats->bytes_unsynced());
}

TEST_F(DriveIoStatsTest, CountsWritesAndSyncs) {
  const auto root = GetTestPath("drive");
  ASSERT_OK(env_->CreateDir(root));
  auto* stats = &registry().Register(root, nullptr);

  const std::string payload(4096, 'x');
  {
    std::unique_ptr<WritableFile> file;
    ASSERT_OK(env_->NewWritableFile(JoinPathSegments(root, "file"), &file));

    ASSERT_OK(file->Append(Slice(payload)));
    ASSERT_EQ(payload.size(), stats->bytes_written());
    // Written but not yet synced: this is the volume the next fsync will be paying for.
    ASSERT_EQ(payload.size(), stats->bytes_unsynced());
    ASSERT_EQ(0, stats->sync_count());

    ASSERT_OK(file->Append(Slice(payload)));
    ASSERT_EQ(2 * payload.size(), stats->bytes_written());
    ASSERT_EQ(2 * payload.size(), stats->bytes_unsynced());

    ASSERT_OK(file->Sync());
    ASSERT_EQ(1, stats->sync_count());
    ASSERT_EQ(0, stats->bytes_unsynced());
    // A second Sync with nothing pending must not be counted as a sync of the device.
    ASSERT_OK(file->Sync());
    ASSERT_EQ(1, stats->sync_count());

    ASSERT_OK(file->Close());
  }

  // Bytes are cumulative for the drive, not per file, so a second file adds to the same counter.
  {
    std::unique_ptr<WritableFile> file;
    ASSERT_OK(env_->NewWritableFile(JoinPathSegments(root, "file2"), &file));
    ASSERT_OK(file->Append(Slice(payload)));
    ASSERT_OK(file->Close());
  }
  ASSERT_EQ(3 * payload.size(), stats->bytes_written());
}

// Closing a file that was never synced must not leave its bytes on the drive gauge forever.
// WritableFileOptions::sync_on_close defaults to false and RocksDB's writer does not sync on
// close, so this is the common path, not an edge case.
TEST_F(DriveIoStatsTest, CloseWithoutSyncDoesNotLeakUnsyncedBytes) {
  const auto root = GetTestPath("drive");
  ASSERT_OK(env_->CreateDir(root));
  auto* stats = &registry().Register(root, nullptr);

  const std::string payload(4096, 'x');
  {
    std::unique_ptr<WritableFile> file;
    ASSERT_OK(env_->NewWritableFile(JoinPathSegments(root, "file"), &file));
    ASSERT_OK(file->Append(Slice(payload)));
    ASSERT_EQ(payload.size(), stats->bytes_unsynced());
    ASSERT_OK(file->Close());
  }
  ASSERT_EQ(payload.size(), stats->bytes_written());
  ASSERT_EQ(0, stats->bytes_unsynced());

  // Same when the file is destroyed without an explicit Close().
  {
    std::unique_ptr<WritableFile> file;
    ASSERT_OK(env_->NewWritableFile(JoinPathSegments(root, "file2"), &file));
    ASSERT_OK(file->Append(Slice(payload)));
    ASSERT_EQ(payload.size(), stats->bytes_unsynced());
  }
  ASSERT_EQ(0, stats->bytes_unsynced());
}

// sync_file_range is where SST write cost actually lands, so it gets its own counters.
TEST_F(DriveIoStatsTest, CountsRangeSync) {
  // Flush() returns before issuing sync_file_range when never_fsync is set, and YBTest sets it.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_never_fsync) = false;

  const auto root = GetTestPath("drive");
  ASSERT_OK(env_->CreateDir(root));
  auto* stats = &registry().Register(root, nullptr);

  std::unique_ptr<WritableFile> file;
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(root, "file"), &file));
  ASSERT_OK(file->Append(Slice(std::string(64 * 1024, 'x'))));
  ASSERT_EQ(0, stats->range_sync_count());

  ASSERT_OK(file->Flush(WritableFile::FLUSH_ASYNC));
  ASSERT_EQ(1, stats->range_sync_count());
  ASSERT_OK(file->Flush(WritableFile::FLUSH_SYNC));
  ASSERT_EQ(2, stats->range_sync_count());
  // Range sync is writeback, not durability, so it must not be counted as an fsync.
  ASSERT_EQ(0, stats->sync_count());

  ASSERT_OK(file->Close());
}

// Every other test in this file runs with YBTest's default FLAGS_never_fsync=true, which makes
// DoSync() return without touching the device - so nothing else here proves the instrumentation
// ever times a real fsync. This one turns that off.
TEST_F(DriveIoStatsTest, MeasuresRealFsyncLatency) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_never_fsync) = false;

  const auto root = GetTestPath("drive");
  ASSERT_OK(env_->CreateDir(root));

  MetricEntity::AttributeMap attrs;
  attrs["drive_path"] = root;
  auto entity = METRIC_ENTITY_drive.Instantiate(&metric_registry_, "drive:" + root, attrs);
  RegisterDriveIoMetrics(entity, root);
  auto* stats = registry().Find(root);
  ASSERT_NE(stats, nullptr);

  std::unique_ptr<WritableFile> file;
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(root, "file"), &file));
  ASSERT_OK(file->Append(Slice(std::string(256 * 1024, 'x'))));
  ASSERT_OK(file->Sync());
  ASSERT_OK(file->Close());

  ASSERT_EQ(1, stats->sync_count());
  ASSERT_GT(stats->sync_micros(), 0) << "a real fsync should take measurable time";

  // The histogram must have received the same sample, not just the totals.
  auto latency = entity->FindOrNull<EventStats>(METRIC_drive_sync_latency);
  ASSERT_NE(latency, nullptr);
  ASSERT_EQ(1, latency->TotalCount());
  ASSERT_GT(latency->MeanValue(), 0);
}

TEST_F(DriveIoStatsTest, ExportsMetricsOnDriveEntity) {
  const auto root = GetTestPath("drive");
  ASSERT_OK(env_->CreateDir(root));

  MetricEntity::AttributeMap attrs;
  attrs["drive_path"] = root;
  auto entity = METRIC_ENTITY_drive.Instantiate(&metric_registry_, "drive:" + root, attrs);
  RegisterDriveIoMetrics(entity, root);

  const std::string payload(8192, 'x');
  std::unique_ptr<WritableFile> file;
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(root, "file"), &file));
  ASSERT_OK(file->Append(Slice(payload)));
  ASSERT_OK(file->Sync());
  ASSERT_OK(file->Close());

  auto bytes_written = entity->FindOrNull<FunctionGauge<uint64_t>>(METRIC_drive_bytes_written);
  ASSERT_NE(bytes_written, nullptr);
  ASSERT_EQ(payload.size(), bytes_written->value());

  auto sync_count = entity->FindOrNull<FunctionGauge<uint64_t>>(METRIC_drive_sync_count);
  ASSERT_NE(sync_count, nullptr);
  ASSERT_EQ(1, sync_count->value());

  auto unsynced = entity->FindOrNull<FunctionGauge<uint64_t>>(METRIC_drive_bytes_unsynced);
  ASSERT_NE(unsynced, nullptr);
  ASSERT_EQ(0, unsynced->value());

  // The metrics are declared counter-like so that a rate() over them is meaningful.
  ASSERT_EQ(MetricType::kCounter, METRIC_drive_bytes_written.type());
}

// A second MetricRegistry for the same root happens when a mini-cluster restarts a server. The
// drive's histogram has to follow the new entity, or the new registry exports a metric that is
// never incremented while increments keep going to the retired one.
TEST_F(DriveIoStatsTest, ReRegistrationRepointsHistogram) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_never_fsync) = false;

  const auto root = GetTestPath("drive");
  ASSERT_OK(env_->CreateDir(root));

  MetricEntity::AttributeMap attrs;
  attrs["drive_path"] = root;
  auto first = METRIC_ENTITY_drive.Instantiate(&metric_registry_, "drive:" + root, attrs);
  RegisterDriveIoMetrics(first, root);

  MetricRegistry second_registry;
  auto second = METRIC_ENTITY_drive.Instantiate(&second_registry, "drive:" + root, attrs);
  RegisterDriveIoMetrics(second, root);

  std::unique_ptr<WritableFile> file;
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(root, "file"), &file));
  ASSERT_OK(file->Append(Slice(std::string(4096, 'x'))));
  ASSERT_OK(file->Sync());
  ASSERT_OK(file->Close());

  auto latency = second->FindOrNull<EventStats>(METRIC_drive_sync_latency);
  ASSERT_NE(latency, nullptr);
  ASSERT_EQ(1, latency->TotalCount()) << "increments must follow the most recent registration";
}

} // namespace yb
