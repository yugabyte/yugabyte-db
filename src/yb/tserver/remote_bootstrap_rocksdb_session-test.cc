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

#include "yb/rpc/proxy.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_anchor_registry.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/write_query.h"

#include "yb/tserver/remote_bootstrap_session-test.h"
#include "yb/tserver/tserver.messages.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

using std::string;
using std::vector;

DECLARE_int32(log_min_segments_to_retain);
DECLARE_int64(time_based_wal_gc_clock_delta_usec);
DECLARE_bool(TEST_disable_wal_retention_time);
DECLARE_bool(TEST_force_lazy_superblock_flush);
DECLARE_bool(enable_log_retention_by_op_idx);
DECLARE_bool(TEST_rbs_pause_after_wal_trim);

namespace yb {
namespace tserver {

class RemoteBootstrapRocksDBTest : public RemoteBootstrapSessionTest {
 public:
  RemoteBootstrapRocksDBTest() : RemoteBootstrapSessionTest(YQL_TABLE_TYPE) {}

  void SetUp() override {
    RemoteBootstrapSessionTest::SetUp();
  }

  void TearDown() override {
    RemoteBootstrapSessionTest::TearDown();
  }

 protected:
  // Writes a single row of test data via WriteAsync and blocks until it succeeds.
  void InsertOneRow(int32_t key) {
    WriteRequestPB req;
    req.set_tablet_id(tablet_peer_->tablet_id());
    AddTestRowInsert(key, key * 2, Substitute("k$0", key), &req);

    auto arena = SharedThreadSafeArena();
    auto* resp = arena->NewArenaObject<LWWriteResponsePB>();
    CountDownLatch latch(1);

    auto query = std::make_unique<tablet::WriteQuery>(
        kLeaderTerm, CoarseTimePoint::max() /* deadline */, tablet_peer_.get(),
        ASSERT_RESULT(tablet_peer_->shared_tablet()), /* rpc_context= */ nullptr, resp);
    query->set_client_request(*arena->NewArenaObject<LWWriteRequestPB>(req));
    query->set_callback(tablet::MakeLatchOperationCompletionCallback(&latch, resp));
    tablet_peer_->WriteAsync(std::move(query));
    latch.Wait();
    ASSERT_FALSE(resp->has_error()) << "Insert failed: " << resp->error().ShortDebugString();
  }

  // Writes `num_rolls` batches of `rows_per_roll` rows; each batch is flushed (so its data is
  // covered by an SST checkpoint at flush time) and then forces a WAL segment roll. Critically
  // does NOT call RunLogGC -- the resulting on-disk state has many closed segments whose ops are
  // already redundant with the SSTs but which the source's GC thread has not yet reclaimed.
  // Used to set up the "GC thread is lagging" condition that the RBS WAL-shipping logic must
  // handle gracefully.
  void RollSegmentsCoveredBySsts(int num_rolls, int rows_per_roll, int32_t starting_key) {
    auto* log = tablet_peer_->log();
    auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());

    int32_t next_key = starting_key;
    for (int roll = 0; roll < num_rolls; ++roll) {
      for (int i = 0; i < rows_per_roll; ++i, ++next_key) {
        ASSERT_NO_FATALS(InsertOneRow(next_key));
      }
      ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
      ASSERT_OK(log->AllocateSegmentAndRollOver());
    }
  }
};

TEST_F(RemoteBootstrapRocksDBTest, TestCheckpointDirectory) {
  string checkpoint_dir;
  {
    auto temp_session = make_scoped_refptr<RemoteBootstrapSession>(
        tablet_peer_, "TestTempSession", "FakeUUID", nullptr /* nsessions */);
    CHECK_OK(temp_session->InitBootstrapSession());
    checkpoint_dir = temp_session->checkpoint_dir_;
    ASSERT_FALSE(checkpoint_dir.empty());
    ASSERT_TRUE(env_->FileExists(checkpoint_dir));
    bool is_dir = false;
    ASSERT_OK(env_->IsDirectory(checkpoint_dir, &is_dir));
    ASSERT_TRUE(is_dir);
    vector<string> rocksdb_files;
    ASSERT_OK(env_->GetChildren(checkpoint_dir, &rocksdb_files));
    // Ignore "." and ".." entries.
    ASSERT_GT(rocksdb_files.size(), 2);
  }
  // Verify that destructor deleted the checkpoint directory.
  ASSERT_FALSE(env_->FileExists(checkpoint_dir));
}

TEST_F(RemoteBootstrapRocksDBTest, CheckSuperBlockHasRocksDBFields) {
  auto superblock = session_->tablet_superblock();
  const auto& kv_store = superblock.kv_store();
  LOG(INFO) << superblock.ShortDebugString();
  ASSERT_EQ(1, kv_store.tables_size());
  ASSERT_EQ(YQL_TABLE_TYPE, kv_store.tables(0).table_type());
  ASSERT_TRUE(kv_store.has_rocksdb_dir());

  const auto& checkpoint_dir = session_->checkpoint_dir_;
  vector<string> checkpoint_files;
  ASSERT_OK(env_->GetChildren(checkpoint_dir, &checkpoint_files));

  // Ignore "." and ".." entries in session_->checkpoint_dir_.
  ASSERT_EQ(kv_store.rocksdb_files().size(), checkpoint_files.size() - 2);
  for (int i = 0; i < kv_store.rocksdb_files().size(); ++i) {
    const auto& rocksdb_file_name = kv_store.rocksdb_files(i).name();
    auto rocksdb_file_size_bytes = kv_store.rocksdb_files(i).size_bytes();
    auto file_path = JoinPathSegments(checkpoint_dir, rocksdb_file_name);
    ASSERT_TRUE(env_->FileExists(file_path));
    uint64 file_size_bytes = ASSERT_RESULT(env_->GetFileSize(file_path));
    ASSERT_EQ(rocksdb_file_size_bytes, file_size_bytes);
  }
}

TEST_F(RemoteBootstrapRocksDBTest, TestNonExistentRocksDBFile) {
  GetDataPieceInfo info;
  auto status = session_->GetRocksDBFilePiece("SomeNonExistentFile", &info);
  ASSERT_TRUE(status.IsNotFound());
}

// Reproduces the "GC-lag bloat" condition that previously formed a vicious cycle on slow remote
// bootstraps: when many WAL segments on the source are already covered by RocksDB SSTs (i.e. they
// are reclaimable) but the GC thread has not yet trimmed them, the old InitBootstrapSession()
// would treat every on-disk segment as something the destination must download.
//
// The pre-fix behavior was:
//   1. Register a log anchor at op id 0, pinning every on-disk segment in place.
//   2. Snapshot LogReader::segments_ verbatim into log_segments_.
//   3. Bump the anchor up to the first segment with a valid footer -- which, because pre-fix
//      kept every on-disk segment, was usually the oldest available segment.
//   4. Tell the destination to fetch every one of those segments, even though their data is
//      already in the SST checkpoint.
//
// On a long/failed RBS the source's GC stays paralyzed at the first op of the segment picked
// in (3) for the entire session, which only deepened the bloat for retries. After the fix,
// InitBootstrapSession() anchors at
// GetEarliestNeededLogIndex and scans its local segment snapshot for the contiguous prefix of
// closed segments whose footer max_replicate_index sits strictly below that floor, dropping them
// from the WAL plan. The scan is the same index-based predicate that LogReader's GC predicate
// uses, but driven off the frozen snapshot rather than the live LogReader state -- so it is not
// affected by GC running concurrently between the snapshot and the trim. In the controlled test
// setup below (FLAGS_log_min_segments_to_retain=1, FLAGS_TEST_disable_wal_retention_time=true)
// the source-side GetSegmentsToGC additions (min-segments retention, time retention, xCluster
// retention) are all disabled, so the predicate matches what GetSegmentsToGC returns; we use that
// equivalence to assert the trim size.
TEST_F(RemoteBootstrapRocksDBTest, InitDoesNotShipAlreadyFlushedSegmentsWhenGcLags) {
  // Default is 2; lower it so a 1-segment "kept" set is achievable in the test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  // Disable time-based retention -- otherwise FLAGS_log_min_seconds_to_retain (default 900s)
  // would keep all just-rolled segments alive in GC's view, masking the bloat we want to test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;

  // The fixture's auto-initialized session_ holds a log anchor. With the fix that anchor is at
  // GetEarliestNeededLogIndex (good), but for this test we want a clean registry so we can
  // measure the new session's behavior in isolation.
  session_.reset();

  auto* log = tablet_peer_->log();

  // Roll several WAL segments, each of whose ops are already covered by SSTs at flush time, and
  // crucially DO NOT call TabletPeer::RunLogGC -- this is the "GC thread is lagging" condition.
  constexpr int kAdditionalRolls = 5;
  // Fixture's PopulateTablet wrote keys 0..999.
  ASSERT_NO_FATALS(RollSegmentsCoveredBySsts(kAdditionalRolls, /*rows_per_roll=*/50,
                                             /*starting_key=*/1000));

  // Sanity-check the test setup: the on-disk WAL has many closed segments, and the GC predicate
  // would consider most of them reclaimable.
  log::SegmentSequence on_disk_segments;
  ASSERT_OK(log->GetSegmentsSnapshot(&on_disk_segments));
  ASSERT_GE(on_disk_segments.size(), kAdditionalRolls + 1)
      << "Expected at least " << (kAdditionalRolls + 1)
      << " on-disk segments (closed rolls + active); got " << on_disk_segments.size();

  std::string details;
  const int64_t earliest_needed =
      ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex(&details)).earliest_needed_log_index;
  log::SegmentSequence reclaimable;
  ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable));
  ASSERT_GT(reclaimable.size(), 0u)
      << "Test setup did not produce any GC-redundant segments. on_disk_segments="
      << on_disk_segments.size() << ", earliest_needed=" << earliest_needed
      << "\nGetEarliestNeededLogIndex details:\n" << details;

  // Fresh RBS session over the same tablet peer. No log anchors are outstanding.
  auto fresh_session = make_scoped_refptr<RemoteBootstrapSession>(
      tablet_peer_, "TestGcLagSession", "FakeUUID", /*nsessions=*/nullptr);
  ASSERT_OK(fresh_session->InitBootstrapSession());

  const auto kept_count = fresh_session->log_segments().size();
  const auto skipped_count = on_disk_segments.size() - kept_count;

  // Post-fix invariants:
  //   1. We must have skipped exactly the GC-redundant prefix, nothing more, nothing less.
  ASSERT_EQ(skipped_count, reclaimable.size())
      << "RBS dropped " << skipped_count << " WAL segments but Log::GetSegmentsToGC reported "
      << reclaimable.size() << " as redundant for op_idx " << earliest_needed << ".";

  //   2. Strictly fewer segments shipped than exist on disk -- proves the bug that motivated the
  //      fix is gone (pre-fix this would equal on_disk_segments.size()).
  ASSERT_LT(kept_count, on_disk_segments.size())
      << "Pre-fix would have shipped all " << on_disk_segments.size()
      << " segments. Found that we still ship " << kept_count << " of them.";

  //   3. The first WAL seqno the destination is told to fetch is exactly one past the last
  //      reclaimable segment -- under this test's retention setup (no time/xCluster/min-segments
  //      bumps), the trim is the exact contiguous prefix, so adjacency must hold. A future change
  //      that introduces a gap (e.g. a non-monotone retention policy) would trip this.
  ASSERT_FALSE(fresh_session->log_segments().empty());
  const auto& first_kept = ASSERT_RESULT_REF(fresh_session->log_segments().front());
  const auto& last_skipped = ASSERT_RESULT_REF(reclaimable.back());
  ASSERT_EQ(first_kept->header().sequence_number(),
            last_skipped->header().sequence_number() + 1)
      << "First kept segment seqno " << first_kept->header().sequence_number()
      << " must be exactly one after last reclaimable segment seqno "
      << last_skipped->header().sequence_number();

  //   4. The active segment is still in the kept set: it has no footer.
  const auto& last_kept = ASSERT_RESULT_REF(fresh_session->log_segments().back());
  ASSERT_FALSE(last_kept->HasFooter())
      << "Active segment (no footer) must remain in the WAL plan; "
      << "back of log_segments_ has seqno " << last_kept->header().sequence_number();

  //   5. The session's log anchor is now strictly above 0. Pre-fix it was registered at
  //      MinimumOpId().index() == 0 and would paralyze the source's GC for the entire session.
  int64_t min_anchor = -1;
  ASSERT_OK(tablet_peer_->log_anchor_registry()->GetEarliestRegisteredLogIndex(&min_anchor));
  ASSERT_GT(min_anchor, 0)
      << "Fresh RBS session anchored at op 0; this is the bug that paralyzes the source's GC "
      << "and forms a vicious cycle on slow / retried sessions.";
}

// Companion to InitDoesNotShipAlreadyFlushedSegmentsWhenGcLags above. That test proves RBS trims
// the GC-redundant WAL prefix; this one proves the trim still honors the CDCSDK/xCluster retention
// barrier, which GetEarliestNeededLogIndex() alone does not account for.
//
// Scenario: every write is already flushed to RocksDB SSTs (so GetEarliestNeededLogIndex sits well
// above the oldest segments and they look reclaimable), but a CDC stream is lagging -- the Log's
// cdc_min_replicated_index sits BELOW the earliest-needed op index. Real log GC honors that barrier
// separately (the cdc_max_replicated_index arg of LogReader::GetSegmentPrefixNotIncluding, gated by
// FLAGS_enable_log_retention_by_op_idx), so it does NOT reclaim a segment CDC still needs. The
// fixed InitBootstrapSession() must mirror that: it lowers rbs_min_op_idx to
// min(GetEarliestNeededLogIndex(), Log::GetXReplMinReplicatedIndex()) and keeps every segment whose
// max_replicate_index is at/above that floor.
//
// We pin the barrier at the oldest on-disk segment's max_replicate_index, which is strictly below
// earliest_needed (the segment is otherwise reclaimable). Post-fix rbs_min_op_idx == the barrier,
// so the oldest segment is kept and nothing is skipped -- exactly what GC would now reclaim
// (nothing). Pre-fix RBS ignored the barrier, used earliest_needed as the floor, and dropped the
// oldest segment(s) -- so the bootstrapped peer, once leader, could not serve CDC GetChanges for
// those ops. Hence pre-fix this test fails (oldest segment skipped); post-fix it passes.
TEST_F(RemoteBootstrapRocksDBTest, InitShipsSegmentsStillNeededByCdc) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;
  // The op-idx retention path this test exercises is gated on this flag (default true); set it
  // explicitly so the test is self-contained and order-independent.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_log_retention_by_op_idx) = true;

  // Drop the fixture's auto-session so its anchor doesn't perturb GetEarliestNeededLogIndex.
  session_.reset();

  auto* log = tablet_peer_->log();

  // Same GC-lag setup as the sibling test: several closed, fully-flushed segments, no RunLogGC.
  constexpr int kAdditionalRolls = 5;
  ASSERT_NO_FATALS(RollSegmentsCoveredBySsts(kAdditionalRolls, /*rows_per_roll=*/50,
                                             /*starting_key=*/1000));

  log::SegmentSequence on_disk_segments;
  ASSERT_OK(log->GetSegmentsSnapshot(&on_disk_segments));
  ASSERT_GE(on_disk_segments.size(), kAdditionalRolls + 1)
      << "Expected at least " << (kAdditionalRolls + 1)
      << " on-disk segments (closed rolls + active); got " << on_disk_segments.size();

  // The oldest on-disk segment is closed (has a footer); a lagging CDC stream is most likely to
  // still need it. Pin the CDC barrier at its max_replicate_index.
  const auto& oldest_segment = ASSERT_RESULT_REF(on_disk_segments.front());
  ASSERT_TRUE(oldest_segment->HasFooter())
      << "Oldest on-disk segment (seqno " << oldest_segment->header().sequence_number()
      << ") unexpectedly has no footer.";
  const int64_t oldest_seqno = oldest_segment->header().sequence_number();
  const int64_t cdc_barrier = oldest_segment->footer().max_replicate_index();

  std::string details;
  const int64_t earliest_needed =
      ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex(&details)).earliest_needed_log_index;

  // Precondition: with no CDC barrier the oldest segment IS reclaimable, i.e. the barrier sits
  // strictly below the earliest-needed floor (otherwise it would not lower the RBS floor and the
  // test would not exercise the CDC-vs-earliest-needed divergence). Confirm GC's own predicate
  // (CDC-ignorant here) would reclaim a prefix starting at the oldest segment -- exactly the skip
  // set pre-fix RBS used.
  ASSERT_LT(cdc_barrier, earliest_needed)
      << "Test setup invalid: oldest segment max_replicate_index " << cdc_barrier
      << " is not below GetEarliestNeededLogIndex " << earliest_needed
      << ".\nGetEarliestNeededLogIndex details:\n" << details;
  log::SegmentSequence reclaimable_without_barrier;
  ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable_without_barrier));
  ASSERT_GT(reclaimable_without_barrier.size(), 0u)
      << "Test setup did not produce a GC-redundant prefix; on_disk=" << on_disk_segments.size()
      << ", earliest_needed=" << earliest_needed;
  const auto& first_reclaimable = ASSERT_RESULT_REF(reclaimable_without_barrier.front());
  ASSERT_EQ(first_reclaimable->header().sequence_number(), oldest_seqno)
      << "Expected the oldest on-disk segment to be the first one pre-fix RBS would skip.";

  // Simulate the lagging CDC stream pinning retention at the oldest segment's max_replicate_index.
  // This is the field Log::GetXReplMinReplicatedIndex() (and GC's GetSegmentsToGC) consult.
  log->set_cdc_min_replicated_index(cdc_barrier);

  // With the barrier in place GC itself reclaims NOTHING (the oldest segment, and hence every later
  // one, is at/above the CDC floor). The fixed RBS trim must produce the same outcome.
  log::SegmentSequence reclaimable_with_barrier;
  ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable_with_barrier));
  ASSERT_EQ(reclaimable_with_barrier.size(), 0u)
      << "With the CDC barrier at " << cdc_barrier << ", GC should reclaim no segments, but "
      << "TEST_GetSegmentsToGC reported " << reclaimable_with_barrier.size() << ".";

  // Fresh RBS session over the same tablet peer; no log anchors outstanding.
  auto fresh_session = make_scoped_refptr<RemoteBootstrapSession>(
      tablet_peer_, "TestCdcLagSession", "FakeUUID", /*nsessions=*/nullptr);
  ASSERT_OK(fresh_session->InitBootstrapSession());

  const auto kept_count = fresh_session->log_segments().size();
  const auto skipped_count = on_disk_segments.size() - kept_count;

  // Post-fix invariants:
  //   1. The CDC-required prefix is NOT skipped. The barrier lowers rbs_min_op_idx to cdc_barrier,
  //      every on-disk segment is at/above it, so RBS skips nothing (pre-fix skipped >= 1).
  ASSERT_EQ(skipped_count, 0u)
      << "RBS skipped " << skipped_count << " WAL segment(s) the lagging CDC stream still needs "
      << "(barrier=" << cdc_barrier << ", earliest_needed=" << earliest_needed << ").";
  ASSERT_EQ(kept_count, on_disk_segments.size())
      << "RBS kept " << kept_count << " of " << on_disk_segments.size()
      << " on-disk segments; the CDC barrier should keep all of them.";

  //   2. Concretely, the first segment the destination is told to fetch is the oldest on-disk
  //      segment -- the one pre-fix RBS dropped.
  ASSERT_FALSE(fresh_session->log_segments().empty());
  const auto& first_kept = ASSERT_RESULT_REF(fresh_session->log_segments().front());
  ASSERT_EQ(first_kept->header().sequence_number(), oldest_seqno)
      << "First shipped segment seqno " << first_kept->header().sequence_number()
      << " should equal the oldest on-disk seqno " << oldest_seqno
      << " (pre-fix RBS skipped it because " << cdc_barrier << " < " << earliest_needed << ").";

  //   3. The active (footer-less) segment is still in the kept set.
  const auto& last_kept = ASSERT_RESULT_REF(fresh_session->log_segments().back());
  ASSERT_FALSE(last_kept->HasFooter())
      << "Active segment (no footer) must remain in the WAL plan; "
      << "back of log_segments_ has seqno " << last_kept->header().sequence_number();
}

// Regression test for GH #32740: the RBS WAL trim must honor time-based retention
// (Log::wal_retention_secs(), i.e. FLAGS_log_min_seconds_to_retain / the per-table
// wal_retention_secs property), not just op-index reachability. The window is an operator-visible
// retention guarantee: it lets a temporarily-down peer catch up from a future leader via the WAL
// instead of a full remote bootstrap, and it is the time-based safety margin CDCSDK/xCluster layer
// over the xrepl index barrier. A re-bootstrapped replica must come back holding the window, or
// the tablet's effective retention silently drops to the weakest replica's tail after a leader
// change.
//
// Phase 1: with time-based retention ACTIVE (the default), a GC-lag prefix that is index-wise
// redundant but still inside the retention window must be shipped, not skipped. Pre-fix, RBS
// skipped it (this is exactly the "ship from segment #57, skip the first 56" QA observation that
// filed the issue), so pre-fix this test fails here.
// Phase 2: age every closed segment out of the window via FLAGS_time_based_wal_gc_clock_delta_usec
// (the same knob GC tests use to fake WAL age; it shifts "now" in the age computation) and verify
// the same prefix is now skipped -- proving phase 1's keep decision came from time retention and
// not from some other floor.
// Both phases cross-check equality with Log::TEST_GetSegmentsToGC: the trim must always match what
// the source's own GC would (not) reclaim.
//
// Flakiness note: the default 900s window dwarfs the test's runtime, and the phase-2 delta (+24h)
// dwarfs the window, so no segment sits anywhere near the age boundary when the trim and the
// cross-checks run at slightly different times.
TEST_F(RemoteBootstrapRocksDBTest, InitShipsSegmentsWithinTimeRetentionWindow) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  // Time-based retention is the subject of this test; enable it explicitly since sibling tests
  // in this binary disable it and test flags leak across TEST_Fs in the same process.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = false;
  auto reset_clock_delta = ScopeExit([]() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_based_wal_gc_clock_delta_usec) = 0;
  });

  // Drop the fixture's auto-session so its anchor doesn't perturb GetEarliestNeededLogIndex.
  session_.reset();

  auto* log = tablet_peer_->log();

  // Same GC-lag setup as the sibling tests: several closed, fully-flushed segments, no RunLogGC.
  constexpr int kAdditionalRolls = 5;
  ASSERT_NO_FATALS(RollSegmentsCoveredBySsts(kAdditionalRolls, /*rows_per_roll=*/50,
                                             /*starting_key=*/1000));

  log::SegmentSequence on_disk_segments;
  ASSERT_OK(log->GetSegmentsSnapshot(&on_disk_segments));
  ASSERT_GE(on_disk_segments.size(), kAdditionalRolls + 1)
      << "Expected at least " << (kAdditionalRolls + 1)
      << " on-disk segments (closed rolls + active); got " << on_disk_segments.size();

  std::string details;
  const int64_t earliest_needed =
      ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex(&details)).earliest_needed_log_index;

  // Setup-sanity trigger guard: index-wise (i.e. with time retention temporarily disabled) there
  // IS a reclaimable prefix. Without this, phase 1's "skip nothing" assertion could pass
  // vacuously; with it, we know time retention is the ONLY thing protecting that prefix.
  size_t index_wise_reclaimable = 0;
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;
    auto reenable_time_retention = ScopeExit([]() {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = false;
    });
    log::SegmentSequence reclaimable_index_wise;
    ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable_index_wise));
    index_wise_reclaimable = reclaimable_index_wise.size();
    ASSERT_GT(index_wise_reclaimable, 0u)
        << "Test setup did not produce an index-wise GC-redundant prefix; on_disk="
        << on_disk_segments.size() << ", earliest_needed=" << earliest_needed
        << "\nGetEarliestNeededLogIndex details:\n" << details;
  }

  // Phase 1: every closed segment was rolled seconds ago, well inside the default 900s window,
  // so GC itself reclaims nothing...
  log::SegmentSequence reclaimable_in_window;
  ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable_in_window));
  ASSERT_EQ(reclaimable_in_window.size(), 0u)
      << "All closed segments are inside the retention window; GC should reclaim none.";

  // ... and RBS must ship all of them (pre-fix it skipped the index-wise-redundant prefix).
  {
    auto in_window_session = make_scoped_refptr<RemoteBootstrapSession>(
        tablet_peer_, "TestTimeRetentionInWindowSession", "FakeUUID", /*nsessions=*/nullptr);
    ASSERT_OK(in_window_session->InitBootstrapSession());
    ASSERT_EQ(in_window_session->log_segments().size(), on_disk_segments.size())
        << "RBS skipped " << on_disk_segments.size() - in_window_session->log_segments().size()
        << " WAL segment(s) still inside the time-based retention window (GH #32740); "
        << "index-wise-redundant prefix was " << index_wise_reclaimable << " segment(s).";
    const auto& first_kept = ASSERT_RESULT_REF(in_window_session->log_segments().front());
    const auto& oldest_on_disk = ASSERT_RESULT_REF(on_disk_segments.front());
    ASSERT_EQ(first_kept->header().sequence_number(),
              oldest_on_disk->header().sequence_number())
        << "The first shipped segment must be the oldest on-disk segment.";
  }  // Session destroyed here so its log anchor doesn't influence phase 2.

  // Phase 2: fake-age every closed segment far past the window. GC would now reclaim exactly the
  // index-wise prefix, and RBS must skip exactly the same segments.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_based_wal_gc_clock_delta_usec) =
      24LL * 60 * 60 * 1000000;  // +24h dwarfs the 900s default window.

  log::SegmentSequence reclaimable_aged;
  ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable_aged));
  ASSERT_EQ(reclaimable_aged.size(), index_wise_reclaimable)
      << "With every segment aged out, GC's reclaimable prefix should equal the index-wise one.";

  auto aged_session = make_scoped_refptr<RemoteBootstrapSession>(
      tablet_peer_, "TestTimeRetentionAgedSession", "FakeUUID", /*nsessions=*/nullptr);
  ASSERT_OK(aged_session->InitBootstrapSession());

  const auto kept_count = aged_session->log_segments().size();
  const auto skipped_count = on_disk_segments.size() - kept_count;
  ASSERT_EQ(skipped_count, reclaimable_aged.size())
      << "Once aged out of the window, RBS should skip exactly the GC-redundant prefix; skipped "
      << skipped_count << ", GC reports " << reclaimable_aged.size() << ".";

  ASSERT_FALSE(aged_session->log_segments().empty());
  const auto& first_kept = ASSERT_RESULT_REF(aged_session->log_segments().front());
  const auto& last_skipped = ASSERT_RESULT_REF(reclaimable_aged.back());
  ASSERT_EQ(first_kept->header().sequence_number(),
            last_skipped->header().sequence_number() + 1)
      << "First kept segment must be exactly one after the last reclaimable segment.";

  const auto& last_kept = ASSERT_RESULT_REF(aged_session->log_segments().back());
  ASSERT_FALSE(last_kept->HasFooter())
      << "Active segment (no footer) must remain in the WAL plan.";
}

// Companion to the time-retention test above for the other retention floor GC applies:
// FLAGS_log_min_segments_to_retain. RBS mirrors GC's max_to_delete clamp so a re-bootstrapped
// replica comes back with at least as many trailing WAL segments as the source's GC is required
// to leave behind. Setup mirrors InitKeepsMinSegmentsWhenLazySuperblockFlushEnabled: the last
// logged op lives in the ACTIVE segment, so the unclamped plan would keep only 1 segment and the
// clamp (set to 3 here) is observable.
TEST_F(RemoteBootstrapRocksDBTest, InitKeepsMinSegmentsToRetain) {
  // static so the ScopeExit lambda below can use it without capture: under TSAN,
  // ANNOTATE_UNPROTECTED_WRITE's assignment takes the RHS by const-ref (an odr-use).
  static constexpr int32_t kMinSegmentsToRetain = 3;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = kMinSegmentsToRetain;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;

  // Drop the fixture's auto-session so its anchor doesn't perturb GetEarliestNeededLogIndex.
  session_.reset();

  auto* log = tablet_peer_->log();
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());

  ASSERT_NO_FATALS(RollSegmentsCoveredBySsts(/*num_rolls=*/5, /*rows_per_roll=*/50,
                                             /*starting_key=*/1000));
  ASSERT_NO_FATALS(InsertOneRow(/*key=*/9999));
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  log::SegmentSequence on_disk_segments;
  ASSERT_OK(log->GetSegmentsSnapshot(&on_disk_segments));
  ASSERT_GT(on_disk_segments.size(), static_cast<size_t>(kMinSegmentsToRetain))
      << "Test needs strictly more on-disk segments than the clamp to make it observable.";

  const int64_t earliest_needed =
      ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex()).earliest_needed_log_index;

  // Precondition: without the clamp the plan would keep only the active segment, so the clamp is
  // what this test exercises (mirrors the lazy-superblock-flush test's structure).
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
    auto restore_flag = ScopeExit([]() {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = kMinSegmentsToRetain;
    });
    log::SegmentSequence reclaimable_unclamped;
    ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable_unclamped));
    ASSERT_EQ(on_disk_segments.size() - reclaimable_unclamped.size(), 1u)
        << "Test setup doesn't exercise the clamp: the unclamped plan keeps "
        << on_disk_segments.size() - reclaimable_unclamped.size()
        << " segment(s), expected only the active one.";
  }

  // GC's own answer with the clamp active, for the equality cross-check.
  log::SegmentSequence reclaimable_clamped;
  ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable_clamped));
  ASSERT_EQ(on_disk_segments.size() - reclaimable_clamped.size(),
            static_cast<size_t>(kMinSegmentsToRetain))
      << "GC's clamped answer should leave exactly " << kMinSegmentsToRetain << " segments.";

  auto fresh_session = make_scoped_refptr<RemoteBootstrapSession>(
      tablet_peer_, "TestMinSegmentsSession", "FakeUUID", /*nsessions=*/nullptr);
  ASSERT_OK(fresh_session->InitBootstrapSession());

  const auto kept_count = fresh_session->log_segments().size();
  ASSERT_EQ(kept_count, static_cast<size_t>(kMinSegmentsToRetain))
      << "RBS must keep exactly FLAGS_log_min_segments_to_retain=" << kMinSegmentsToRetain
      << " trailing segments (the clamped GC answer); kept " << kept_count << " of "
      << on_disk_segments.size() << ".";

  const auto& last_kept = ASSERT_RESULT_REF(fresh_session->log_segments().back());
  ASSERT_FALSE(last_kept->HasFooter())
      << "Active segment (no footer) must remain in the WAL plan.";
}

// Verifies the defensive lazy-superblock-flush clamp added to RBS WAL planning. When lazy SB
// flush is enabled on a tablet, local bootstrap on the destination walks back at least
// kMinSegmentsToReplayWithLazySuperblockFlush trailing WAL segments to pick up any
// committed-but-unflushed CHANGE_METADATA_OPs (see the long comment near that constant in
// tablet_bootstrap.cc). InitBootstrapSession mirrors that invariant so the source never trims
// the WAL plan below it; this test reproduces the GC-lag setup of the test above and asserts
// the trim is clamped accordingly.
//
// Note: we use FLAGS_TEST_force_lazy_superblock_flush here purely because TabletTestHarness
// cannot currently construct a colocated tablet (the only configuration where
// IsLazySuperblockFlushEnabled() naturally returns true). When the harness gains a `colocated`
// option, this test should be rewritten to use it and the test flag retired.
TEST_F(RemoteBootstrapRocksDBTest, InitKeepsMinSegmentsWhenLazySuperblockFlushEnabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;

  // Force IsLazySuperblockFlushEnabled() == true even though the fixture's tablet is not
  // colocated. The clamp in InitBootstrapSession is what we want to exercise.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_force_lazy_superblock_flush) = true;
  auto reset_force_lazy = ScopeExit([]() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_force_lazy_superblock_flush) = false;
  });

  // Reset the fixture session so its log anchor doesn't influence GetEarliestNeededLogIndex.
  session_.reset();

  auto* log = tablet_peer_->log();
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());

  // Roll some segments worth of flushed writes. After this loop the last logged op lives in the
  // LAST CLOSED segment (since the final iteration ends with a rollover), so that closed
  // segment's max_replicate_index equals last_logged_opid.index. To make the lazy-SB-flush
  // clamp observable, we need the last logged op to instead live in the ACTIVE segment, so that
  // every closed segment is strictly below earliest_needed and the unclamped plan would keep
  // just 1 (the active). We achieve that by appending one extra write+flush AFTER the loop
  // without forcing another roll.
  ASSERT_NO_FATALS(RollSegmentsCoveredBySsts(/*num_rolls=*/5, /*rows_per_roll=*/50,
                                             /*starting_key=*/1000));
  ASSERT_NO_FATALS(InsertOneRow(/*key=*/9999));
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  log::SegmentSequence on_disk_segments;
  ASSERT_OK(log->GetSegmentsSnapshot(&on_disk_segments));
  ASSERT_GE(on_disk_segments.size(), tablet::kMinSegmentsToReplayWithLazySuperblockFlush + 1u)
      << "Test needs strictly more on-disk segments than the lazy-SB-flush minimum to make the "
      << "clamp observable; got " << on_disk_segments.size();

  // What the unclamped path would do, for comparison. We expect the unclamped plan to keep
  // exactly 1 segment (the active one), so the clamp must bump that to
  // kMinSegmentsToReplayWithLazySuperblockFlush.
  const int64_t earliest_needed =
      ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex()).earliest_needed_log_index;
  log::SegmentSequence reclaimable;
  ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable));
  const size_t unclamped_kept = on_disk_segments.size() - reclaimable.size();
  ASSERT_LT(unclamped_kept, tablet::kMinSegmentsToReplayWithLazySuperblockFlush)
      << "Test setup doesn't actually exercise the clamp: the unclamped plan would already keep "
      << unclamped_kept << " segments, which is >= the lazy-SB-flush minimum ("
      << tablet::kMinSegmentsToReplayWithLazySuperblockFlush
      << "). on_disk=" << on_disk_segments.size() << ", reclaimable=" << reclaimable.size()
      << ", earliest_needed=" << earliest_needed;

  auto fresh_session = make_scoped_refptr<RemoteBootstrapSession>(
      tablet_peer_, "TestLazySbFlushSession", "FakeUUID", /*nsessions=*/nullptr);
  ASSERT_OK(fresh_session->InitBootstrapSession());

  const auto kept_count = fresh_session->log_segments().size();

  // The clamp must force kept_count up to the lazy-SB-flush minimum, exactly matching the
  // bound (not more) since unclamped_kept < K.
  ASSERT_EQ(kept_count, tablet::kMinSegmentsToReplayWithLazySuperblockFlush)
      << "Lazy-SB-flush clamp incorrect: kept " << kept_count << " segments, expected exactly "
      << tablet::kMinSegmentsToReplayWithLazySuperblockFlush
      << ". on_disk=" << on_disk_segments.size()
      << ", unclamped_kept=" << unclamped_kept
      << ", earliest_needed=" << earliest_needed;

  // The active (footer-less) segment is still in the kept set as before.
  ASSERT_FALSE(fresh_session->log_segments().empty());
  const auto& last_kept = ASSERT_RESULT_REF(fresh_session->log_segments().back());
  ASSERT_FALSE(last_kept->HasFooter())
      << "Active segment (no footer) must remain in the WAL plan; back of log_segments_ has "
      << "seqno " << last_kept->header().sequence_number();
}

// Regression test for the window between the WAL trim decision and the session's log-anchor
// update. Segments kept by time-based retention sit BELOW rbs_min_op_idx, where the anchor
// registered at the start of InitBootstrapSession does not reach, and the time policy that keeps
// them expires by definition -- so before InitBootstrapSession pinned the segment snapshot, one
// of them aging out of the window right after the trim scan could be GC'd while already part of
// the plan. That is not a survivable blip: the destination walks segment sequence numbers upward
// from the front of the plan and treats the first unresolvable one as end-of-log, so losing the
// FRONT means it downloads no WAL at all and still reports success.
//
// The test drives exactly that interleaving: pause the session inside the window, then age every
// closed segment far past the retention window and run a real GC pass. The pin must hold GC off
// completely; without it GC reclaims the aged prefix, which is precisely the front of the plan.
TEST_F(RemoteBootstrapRocksDBTest, InitPinsTimeKeptSegmentsAgainstConcurrentGc) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  // Time-based retention is what puts segments below rbs_min_op_idx into the plan; enable it
  // explicitly since sibling tests in this binary disable it and flags leak across TEST_Fs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = false;
  auto reset_clock_delta = ScopeExit([]() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_based_wal_gc_clock_delta_usec) = 0;
  });

  // Drop the fixture's auto-session so its anchor doesn't perturb GetEarliestNeededLogIndex.
  session_.reset();

  auto* log = tablet_peer_->log();

  // Same GC-lag setup as the sibling tests: several closed, fully-flushed segments, no RunLogGC.
  constexpr int kAdditionalRolls = 5;
  ASSERT_NO_FATALS(RollSegmentsCoveredBySsts(kAdditionalRolls, /*rows_per_roll=*/50,
                                             /*starting_key=*/1000));

  log::SegmentSequence on_disk_segments;
  ASSERT_OK(log->GetSegmentsSnapshot(&on_disk_segments));
  const auto on_disk_count = on_disk_segments.size();
  ASSERT_GE(on_disk_count, kAdditionalRolls + 1);

  std::string details;
  const int64_t earliest_needed =
      ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex(&details)).earliest_needed_log_index;

  // Setup-sanity trigger guard: index-wise there IS a reclaimable prefix, so the plan's front
  // really is a segment held only by time retention. Without that, the anchor registered at
  // session start would already cover the whole plan and this race could not arise at all.
  size_t index_wise_reclaimable = 0;
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;
    auto reenable_time_retention = ScopeExit([]() {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = false;
    });
    log::SegmentSequence reclaimable_index_wise;
    ASSERT_OK(log->TEST_GetSegmentsToGC(earliest_needed, &reclaimable_index_wise));
    index_wise_reclaimable = reclaimable_index_wise.size();
    ASSERT_GT(index_wise_reclaimable, 0u)
        << "Test setup did not produce an index-wise GC-redundant prefix; on_disk="
        << on_disk_count << ", earliest_needed=" << earliest_needed
        << "\nGetEarliestNeededLogIndex details:\n" << details;
  }

  // Hold the session open between its trim decision and its anchor update, so a GC pass can run
  // underneath it. Released explicitly once that GC has run, and by the release_pause ScopeExit
  // below on any early exit.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_rbs_pause_after_wal_trim) = true;

  auto session = make_scoped_refptr<RemoteBootstrapSession>(
      tablet_peer_, "TestAnchorRaceSession", "FakeUUID", /*nsessions=*/nullptr);
  Status init_status;
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&session, &init_status]() {
    init_status = session->InitBootstrapSession();
  });
  // Declared AFTER thread_holder so it runs BEFORE the join in ~TestThreadHolder: the paused
  // session thread exits only once this flag is cleared, so on an early assertion failure the
  // clear has to happen first or the join deadlocks.
  auto release_pause = ScopeExit([]() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_rbs_pause_after_wal_trim) = false;
  });

  // Wait until the session has pinned the snapshot. The pin is the only thing that takes the
  // registry's minimum below the index-based floor before the plan is final (the initial
  // Register() sits at rbs_min_op_idx, which equals earliest_needed here since no xrepl consumer
  // constrains retention).
  //
  // Deliberately non-fatal: if the pin is gone the interesting failure is the one the GC below
  // demonstrates, not this wait. Timing out here is also what makes that GC meaningful in that
  // case -- by then the session is long past the trim and sitting in the pause.
  WARN_NOT_OK(
      LoggedWaitFor(
          [this, earliest_needed]() -> Result<bool> {
            int64_t min_anchor_index = 0;
            if (!tablet_peer_->log_anchor_registry()->GetEarliestRegisteredLogIndex(
                    &min_anchor_index).ok()) {
              return false;
            }
            return min_anchor_index < earliest_needed;
          },
          MonoDelta::FromSeconds(30),
          "remote bootstrap session to pin its WAL segment snapshot"),
      "Session never pinned its WAL segment snapshot");

  // Age every closed segment far past the retention window and run a real GC pass (RunLogGC
  // recomputes the floors, so it sees both the aging and the pin). The session is somewhere
  // between the pin and the pause here; either side is fine, because the pin is what has to cover
  // the whole trim computation.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_based_wal_gc_clock_delta_usec) =
      24LL * 60 * 60 * 1000000;  // +24h dwarfs the 900s default window.
  ASSERT_OK(tablet_peer_->RunLogGC());

  log::SegmentSequence after_gc_segments;
  ASSERT_OK(log->GetSegmentsSnapshot(&after_gc_segments));
  ASSERT_EQ(after_gc_segments.size(), on_disk_count)
      << "GC running between the WAL trim decision and the session's anchor update reclaimed "
      << on_disk_count - after_gc_segments.size() << " segment(s). The segments the session has "
      << "already committed to shipping must be pinned for the whole session; losing the front of "
      << "the plan makes the destination download no WAL at all (GH #32740 follow-up).";

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_rbs_pause_after_wal_trim) = false;
  thread_holder.JoinAll();
  ASSERT_OK(init_status);

  // Every segment in the plan must still be fetchable now that the session has finished init: the
  // anchor has moved up to the front of the plan and pins it for the rest of the session.
  ASSERT_FALSE(session->log_segments().empty());
  for (const auto& planned : session->log_segments()) {
    const auto seqno = planned->header().sequence_number();
    ASSERT_OK_PREPEND(
        log->GetSegmentBySequenceNumber(seqno),
        Format("Planned WAL segment $0 is no longer resolvable after session init", seqno));
  }

  // The skipped prefix, by contrast, is released by that same anchor move and is now reclaimable.
  const auto planned_front =
      ASSERT_RESULT_REF(session->log_segments().front())->header().sequence_number();
  const auto oldest_on_disk =
      ASSERT_RESULT_REF(on_disk_segments.front())->header().sequence_number();
  if (planned_front > oldest_on_disk) {
    ASSERT_OK(tablet_peer_->RunLogGC());
    log::SegmentSequence after_release;
    ASSERT_OK(log->GetSegmentsSnapshot(&after_release));
    ASSERT_LT(after_release.size(), on_disk_count)
        << "Once the anchor moves up to the front of the plan, the skipped prefix must become "
        << "GC-able again; the pin is transient, not a session-long bottom anchor.";
    ASSERT_OK_PREPEND(
        log->GetSegmentBySequenceNumber(planned_front),
        Format("Front of the WAL plan (segment $0) was reclaimed", planned_front));
  }
}

// The destination treats NotFound from a WAL segment fetch as its end-of-log terminator: it stops
// downloading and reports success (RemoteBootstrapClient::DownloadWALs). So a segment that was
// GC'd out from under a live session must NOT answer NotFound, or the destination silently
// truncates its WAL download at that hole -- downloading nothing at all when the hole is at the
// front of the plan. Below-active seqnos therefore fail with a distinguishable status, while
// above-active seqnos must keep terminating the walk exactly as before.
TEST_F(RemoteBootstrapRocksDBTest, FetchOfGcedSegmentIsDistinguishableFromEndOfLog) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;

  // Drop the fixture's auto-session so its anchor doesn't hold back the GC below.
  session_.reset();

  auto* log = tablet_peer_->log();

  ASSERT_NO_FATALS(RollSegmentsCoveredBySsts(/*num_rolls=*/5, /*rows_per_roll=*/50,
                                             /*starting_key=*/1000));

  log::SegmentSequence before_gc;
  ASSERT_OK(log->GetSegmentsSnapshot(&before_gc));
  const auto gced_seqno = ASSERT_RESULT_REF(before_gc.front())->header().sequence_number();

  // Actually reclaim the redundant prefix, so `gced_seqno` is below the live range for real.
  ASSERT_OK(tablet_peer_->RunLogGC());
  log::SegmentSequence after_gc;
  ASSERT_OK(log->GetSegmentsSnapshot(&after_gc));
  ASSERT_LT(after_gc.size(), before_gc.size())
      << "Test setup needs GC to actually reclaim a prefix; nothing was reclaimed.";
  ASSERT_LT(gced_seqno, ASSERT_RESULT_REF(after_gc.front())->header().sequence_number());

  auto session = make_scoped_refptr<RemoteBootstrapSession>(
      tablet_peer_, "TestGcedSegmentFetchSession", "FakeUUID", /*nsessions=*/nullptr);
  ASSERT_OK(session->InitBootstrapSession());

  auto make_info = []() {
    return GetDataPieceInfo {
      .offset = 0,
      .client_maxlen = 0,
      .data = std::string(),
      .data_size = 0,
      .error_code = RemoteBootstrapErrorPB::UNKNOWN_ERROR,
    };
  };
  auto fetch_segment = [&session, &make_info](uint64_t seqno, GetDataPieceInfo* info) {
    *info = make_info();
    DataIdPB data_id;
    data_id.set_type(DataIdPB::LOG_SEGMENT);
    data_id.set_wal_segment_seqno(seqno);
    return session->GetDataPiece(data_id, info);
  };

  // A segment below the active one that no longer resolves was GC'd, not walked past.
  {
    GetDataPieceInfo info;
    const auto s = fetch_segment(gced_seqno, &info);
    ASSERT_NOK(s);
    ASSERT_FALSE(s.IsNotFound())
        << "Fetching GC'd segment " << gced_seqno << " returned NotFound, which the destination "
        << "reads as end-of-log and answers by silently stopping its WAL download: " << s;
    ASSERT_EQ(info.error_code, RemoteBootstrapErrorPB::WAL_SEGMENT_NOT_FOUND)
        << "Error code should be unchanged so the protocol is untouched; got "
        << RemoteBootstrapErrorPB::Code_Name(info.error_code);
  }

  // Walking past the end of the log must still terminate the destination's walk with NotFound.
  {
    const auto beyond_active_seqno = log->active_segment_sequence_number() + 100;
    GetDataPieceInfo info;
    const auto s = fetch_segment(beyond_active_seqno, &info);
    ASSERT_NOK(s);
    ASSERT_TRUE(s.IsNotFound())
        << "Fetching seqno " << beyond_active_seqno << " past the active segment must stay "
        << "NotFound; it is how the destination knows it has downloaded the whole plan: " << s;
  }
}

}  // namespace tserver
}  // namespace yb
