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
// Tests for the WAL sync sweeper's per-drive throttle, with no server and no real fsyncs.
//
// The throttle is the whole of the sweeper's pacing -- it flushes without regard to either of the
// thresholds in Log::FindSyncType(), so if the throttle is wrong nothing else holds it back. That
// is why WalSyncSweeper::RefreshDriveState() takes a list of drives rather than a list of peers:
// its state machine can then be driven directly against synthetic DriveIoStats counters, which is
// the only way to test it deterministically.
//
// Why the coverage is split this way, and please keep it split. The per-drive counters aggregate
// every file under the mount -- other tablets' WALs, SSTs, tablet metadata -- so against a running
// server they can support "this happened" but never "this did not happen". A negative assertion on
// drive_sync_count from a mini-cluster is inherently flaky whatever the sweeper does. So:
//
//  - anything that has to assert a negative, or to control what the drive looks like, is tested
//    here or in consensus/log-test;
//  - the two mini-cluster tests in tablet_server-test assert positives, plus the sweeper's own
//    server-level counters, since a correct per-drive back-off skips every tablet on the server
//    and that is a statement those counters can make.
//
// One trap worth knowing before adding a test that tries to make a drive look busy: device time
// from a sync the sweeper itself started is labelled proactive and correctly subtracted back out of
// the duty cycle, so provoking a real sweeper-initiated fsync leaves the drive reading idle however
// much time it burned. That is the throttle working as designed. Synthesize the workload device
// time through DriveIoStats instead.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/tserver/wal_sync_sweeper.h"

#include "yb/util/drive_io_stats.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/path_util.h"
#include "yb/util/test_util.h"

METRIC_DECLARE_entity(server);

DECLARE_double(wal_sync_sweeper_drive_busy_fraction);

namespace yb::tserver {

// The throttle is the whole of the sweeper's pacing: it flushes without regard to either of Log's
// thresholds, so if the throttle is wrong there is nothing else holding it back. These tests drive
// its state machine against synthetic per-drive counters, with no server and no real fsyncs, which
// is the only way to make its arithmetic deterministic.
class WalSyncSweeperThrottleTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    entity_ = METRIC_ENTITY_server.Instantiate(&metric_registry_, "wal-sync-sweeper-test");
    // Null tablet manager is safe: these tests never call Sweep(), only the throttle seams.
    sweeper_ = std::make_unique<WalSyncSweeper>(/* tablet_manager= */ nullptr, entity_);
  }

  // Registry entries are process-global and never removed, so each test uses roots of its own.
  DriveIoStats* MakeDrive(const std::string& name) {
    return &DriveIoStatsRegistry::Instance().Register(
        JoinPathSegments(
            "/wal-sync-sweeper-test",
            JoinPathSegments(
                ::testing::UnitTest::GetInstance()->current_test_info()->name(), name)),
        nullptr);
  }

  void Sample(const std::vector<const DriveIoStats*>& drives) {
    sweeper_->TEST_RefreshDriveState(drives);
  }

  google::FlagSaver flag_saver_;
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> entity_;
  std::unique_ptr<WalSyncSweeper> sweeper_;
};

// A drive nobody has measured yet is presumed idle, not busy. Presuming busy would cost every
// drive on the node its first sweep after startup for no information gained.
TEST_F(WalSyncSweeperThrottleTest, FirstSightOfADriveIsNotBusy) {
  auto* drive = MakeDrive("d1");
  Sample({drive});
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(drive));
}

// The point of the per-drive unit of attribution: a saturated drive must not cost tablets on a
// healthy drive their flush. Both sweeper tests in tablet_server-test are single-drive, so this is
// the only coverage of the claim.
TEST_F(WalSyncSweeperThrottleTest, DrivesAreJudgedIndependently) {
  auto* busy = MakeDrive("busy");
  auto* quiet = MakeDrive("quiet");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_wal_sync_sweeper_drive_busy_fraction) = 0.5;

  Sample({busy, quiet});
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(busy));
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(quiet));

  // Clear the minimum sampling window, then hand only one of them a large amount of device time.
  SleepFor(MonoDelta::FromMilliseconds(50));
  busy->RecordSync(0, MonoDelta::FromMilliseconds(500));

  Sample({busy, quiet});
  ASSERT_TRUE(sweeper_->TEST_IsDriveBusy(busy));
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(quiet))
      << "a drive must not inherit its neighbour's verdict";
}

// The property the subtraction exists for: if the throttle measured total fsync time it would be
// measuring the sweeper's own previous pass, and a pass that costs more than one window would make
// the next pass back off, the one after sweep, and so on - oscillation rather than pacing. Syncs
// labelled proactive have to come out of the numerator.
TEST_F(WalSyncSweeperThrottleTest, OwnProactiveSyncsDoNotCountTowardBusy) {
  auto* drive = MakeDrive("d1");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_wal_sync_sweeper_drive_busy_fraction) = 0.5;

  Sample({drive});
  SleepFor(MonoDelta::FromMilliseconds(50));

  // A large amount of device time, all of it ours: RecordSync is what the file records, and
  // RecordProactiveSync is the sweeper claiming that same sync.
  drive->RecordSync(0, MonoDelta::FromMilliseconds(500));
  drive->RecordProactiveSync(MonoDelta::FromMilliseconds(500));

  Sample({drive});
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(drive))
      << "the throttle must not throttle on its own footprint";

  // The same volume of device time, this time the workload's, must read as busy - otherwise the
  // test above would pass for the trivial reason that nothing ever reads as busy.
  SleepFor(MonoDelta::FromMilliseconds(50));
  drive->RecordSync(0, MonoDelta::FromMilliseconds(500));
  Sample({drive});
  ASSERT_TRUE(sweeper_->TEST_IsDriveBusy(drive));
}

// The two counters the subtraction uses are measured at different scopes - RecordSync times the
// fdatasync inside PosixWritableFile, RecordProactiveSync times the whole of Log::DoSync around it,
// and PosixWritableFile declines to record at all when it has nothing pending - so within a window
// the proactive delta can exceed the raw delta. It must floor at zero rather than wrap, and, more
// importantly, that window's discrepancy must not carry forward and mask the next window's real
// workload activity. Differencing cumulative workload-only totals would do exactly that.
TEST_F(WalSyncSweeperThrottleTest, ProactiveExceedingTotalDoesNotWrapOrCarryForward) {
  auto* drive = MakeDrive("d1");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_wal_sync_sweeper_drive_busy_fraction) = 0.5;

  Sample({drive});
  SleepFor(MonoDelta::FromMilliseconds(50));

  // Deliberately inconsistent in the direction the real code can produce: our label claims more
  // device time than the file recorded.
  drive->RecordSync(0, MonoDelta::FromMicroseconds(100));
  drive->RecordProactiveSync(MonoDelta::FromMilliseconds(500));

  Sample({drive});
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(drive))
      << "the workload share must floor at zero rather than wrap to a huge duty cycle";

  // The window that saturated must not poison the next one. A genuinely busy workload immediately
  // afterwards is still detected, which is what fails if the implementation carries a running
  // workload-only total instead of differencing both counters per window.
  SleepFor(MonoDelta::FromMilliseconds(50));
  drive->RecordSync(0, MonoDelta::FromMilliseconds(500));
  Sample({drive});
  ASSERT_TRUE(sweeper_->TEST_IsDriveBusy(drive));
}

// The floor that stops a near-zero denominator from calling every drive saturated. Worth pinning
// because the failure mode is silent: without the floor a short window turns any device time at
// all into a duty cycle over the threshold, and the sweeper just quietly stops flushing.
TEST_F(WalSyncSweeperThrottleTest, ShortWindowKeepsThePreviousVerdict) {
  auto* drive = MakeDrive("d1");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_wal_sync_sweeper_drive_busy_fraction) = 0.5;

  Sample({drive});
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(drive));

  // Real device time, but sampled again immediately. Divided by a window of almost zero this would
  // read as saturated; the floor must discard the measurement instead.
  drive->RecordSync(0, MonoDelta::FromMilliseconds(500));
  Sample({drive});
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(drive))
      << "a sub-floor window must be discarded, not divided by";

  // And the discarded sample must not have been consumed: once the window is long enough, the same
  // device time is still there to be measured and now does read as busy.
  SleepFor(MonoDelta::FromMilliseconds(50));
  Sample({drive});
  ASSERT_TRUE(sweeper_->TEST_IsDriveBusy(drive));
}

// A busy verdict is not sticky: a drive that goes quiet must become sweepable again on the next
// window, or one burst would disable proactive flushing for the life of the process.
TEST_F(WalSyncSweeperThrottleTest, BusyVerdictClearsWhenTheDriveGoesQuiet) {
  auto* drive = MakeDrive("d1");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_wal_sync_sweeper_drive_busy_fraction) = 0.5;

  Sample({drive});
  SleepFor(MonoDelta::FromMilliseconds(50));
  drive->RecordSync(0, MonoDelta::FromMilliseconds(500));
  Sample({drive});
  ASSERT_TRUE(sweeper_->TEST_IsDriveBusy(drive));

  SleepFor(MonoDelta::FromMilliseconds(50));
  Sample({drive});
  ASSERT_FALSE(sweeper_->TEST_IsDriveBusy(drive));
}

} // namespace yb::tserver
