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

#include "yb/util/flags.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"

using std::string;
using std::vector;

DECLARE_int32(log_min_segments_to_retain);
DECLARE_bool(TEST_disable_wal_retention_time);
DECLARE_bool(TEST_force_lazy_superblock_flush);

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
      ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex(&details));
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
  const int64_t earliest_needed = ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex());
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

}  // namespace tserver
}  // namespace yb
