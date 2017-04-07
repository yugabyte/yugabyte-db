// Copyright (c} YugaByte, Inc.

#include <algorithm>
#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/docdb_test_util.h"

constexpr int kDefaultSnapshotVerificationTestNumIter = 15000;

DEFINE_int32(snapshot_verification_test_num_iter, kDefaultSnapshotVerificationTestNumIter,
             "Number iterations for randomized history cleanup DocDB tests.");

// Use a lower default number of tests when running on ASAN/TSAN so as not to exceed the test time
// limit.
#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
static constexpr int kDefaultTestNumIter = 2000;
#else
static constexpr int kDefaultTestNumIter = 20000;
#endif

DEFINE_int32(test_num_iter, kDefaultTestNumIter,
             "Number iterations for randomized DocDB tests, except those involving logical DocDB "
             "snapshots.");

constexpr int kNumDocKeys = 50;
constexpr int kNumUniqueSubKeys = 500;

using std::vector;
using std::pair;
using std::sort;

namespace yb {
namespace docdb {

namespace {

void RemoveEntriesWithSecondComponentHigherThan(vector<pair<int, int>> *v,
                                                int max_second_component) {
  // See https://en.wikipedia.org/wiki/Erase-remove_idiom.
  v->erase(
      std::remove_if(v->begin(), v->end(), [&](const pair<int, int>& p) {
          return p.second > max_second_component;
      }),
      v->end());
}

}  // anonymous namespace

class RandomizedDocDBTest : public DocDBTestBase {
 protected:
  RandomizedDocDBTest() : load_gen_(this, kNumDocKeys, kNumUniqueSubKeys) {
    SeedRandom();
  }

  ~RandomizedDocDBTest() override {}
  void RunWorkloadWithSnaphots(bool enable_history_cleanup);

  DocDBLoadGenerator load_gen_;
};

void RandomizedDocDBTest::RunWorkloadWithSnaphots(bool enable_history_cleanup) {
  // We start doing snapshots every other iterations, but make it less frequent after a number of
  // iterations (kIterationToSwitchToInfrequentSnapshots to be precise, see the loop below).
  int snapshot_frequency = 2;
  int verification_frequency = 1;

  constexpr int kEventualSnapshotFrequency = 1000;
  constexpr int kEventualVerificationFrequency = 250;
  constexpr int kFlushFrequency = 100;
  constexpr int kIterationToSwitchToInfrequentSnapshots = 300;

  constexpr int kHistoryCleanupChance = 500;

  vector<pair<int, int>> cleanup_ht_and_iteration;

  HybridTime max_history_cleanup_ht(0);

  while (load_gen_.next_iteration() <= FLAGS_snapshot_verification_test_num_iter) {
    const int current_iteration = load_gen_.next_iteration();
    if (current_iteration == kIterationToSwitchToInfrequentSnapshots) {
      // This is where we make snapshot/verification less frequent so the test can actually finish.
      snapshot_frequency = kEventualSnapshotFrequency;
      verification_frequency = kEventualVerificationFrequency;
    }
    NO_FATALS(load_gen_.PerformOperation()) << "at iteration " << current_iteration;
    if (current_iteration % kFlushFrequency == 0) {
      NO_FATALS(FlushRocksDB());
    }
    if (current_iteration % snapshot_frequency == 0) {
      load_gen_.CaptureDocDbSnapshot();
    }
    if (current_iteration % verification_frequency == 0) {
      NO_FATALS(load_gen_.VerifyRandomDocDbSnapshot());
    }

    if (enable_history_cleanup && load_gen_.NextRandomInt(kHistoryCleanupChance) == 0) {
      // Pick a random cleanup hybrid_time from 0 to the last operation hybrid_time inclusively.
      const HybridTime cleanup_ht = HybridTime(
          load_gen_.NextRandom() % (load_gen_.last_operation_ht().value() + 1));
      if (cleanup_ht.CompareTo(max_history_cleanup_ht) <= 0) {
        // We are performing cleanup at an old hybrid_time, and don't expect it to have any effect.
        InMemDocDbState snapshot_before_cleanup;
        snapshot_before_cleanup.CaptureAt(rocksdb(), HybridTime::kMax);
        NO_FATALS(CompactHistoryBefore(cleanup_ht));

        InMemDocDbState snapshot_after_cleanup;
        snapshot_after_cleanup.CaptureAt(rocksdb(), HybridTime::kMax);
        ASSERT_TRUE(snapshot_after_cleanup.EqualsAndLogDiff(snapshot_before_cleanup));
      } else {
        max_history_cleanup_ht = cleanup_ht;
        cleanup_ht_and_iteration.emplace_back(cleanup_ht.value(),
                                              load_gen_.last_operation_ht().value());
        NO_FATALS(CompactHistoryBefore(cleanup_ht));

        // We expect some snapshots at hybrid_times earlier than cleanup_ht to no longer be
        // recoverable.
        NO_FATALS(load_gen_.CheckIfOldestSnapshotIsStillValid(cleanup_ht));

        load_gen_.RemoveSnapshotsBefore(cleanup_ht);

        // Now that we're removed all snapshots that could have been affected by history cleanup,
        // we expect the oldest remaining snapshot to match the RocksDB-backed DocDB state.
        NO_FATALS(load_gen_.VerifyOldestSnapshot());
      }
    }
  }

  LOG(INFO) << "Finished the primary part of the randomized DocDB test.\n"
            << "  enable_history_cleanup: " << enable_history_cleanup << "\n"
            << "  last_operation_ht: " << load_gen_.last_operation_ht() << "\n"
            << "  max_history_cleanup_ht: " << max_history_cleanup_ht.value();

  if (!enable_history_cleanup) return;

  if (FLAGS_snapshot_verification_test_num_iter > kDefaultSnapshotVerificationTestNumIter) {
    LOG(WARNING)
        << "Number of iterations specified for the history cleanup test is greater than "
        << kDefaultSnapshotVerificationTestNumIter << ", and therefore this test is "
        << "NOT CHECKING THAT OLD SNAPSHOTS ARE INVALIDATED BY HISTORY CLEANUP.";
    return;
  }

  // Verify that some old snapshots got invalidated by history cleanup at a higher hybrid_time.

  // First we verify that history cleanup is happening at expected times, so that we can validate
  // that the maximum history cleanup hybrid_time (max_history_cleanup_ht) is as expected.

  // An entry (t, i) here says that after iteration i there was a history cleanup with a history
  // cutoff hybrid_time of t. The iteration here corresponds one to one to the operation
  // hybrid_time. We always have t < i because we perform cleanup at a past hybrid_time,
  // not a future one.
  //                                                        cleanup_ht | iteration (last op. ts.)
  //
  // These numbers depend on DocDB load generator parameters (random seed, frequencies of various
  // events) and will need to be replaced in such cases. Ideally, we should come up with a way to
  // either re-generate those quickly, or not rely on hard-coded expected results for validation.
  // However, we do handle variations in the number of iterations here, up a certain limit.
  vector<pair<int, int>> expected_cleanup_ht_and_iteration{{1,           85},
                                                           {40,          121},
                                                           {46,          255},
                                                           {245,         484},
                                                           {774,         2246},
                                                           {2341,        3417},
                                                           {2741,        5248},
                                                           {4762,        5652},
                                                           {5049,        6377},
                                                           {6027,        7573},
                                                           {8423,        9531},
                                                           {8829,        10413},
                                                           {10061,       10610},
                                                           {13137,       13920}};

  // Remove expected (cleanup_hybrid_time, iteration) entries that don't apply to our test run in
  // case we did fewer than 15000 iterations.
  RemoveEntriesWithSecondComponentHigherThan(
      &expected_cleanup_ht_and_iteration,
      load_gen_.last_operation_ht().value());

  ASSERT_FALSE(expected_cleanup_ht_and_iteration.empty());
  ASSERT_EQ(expected_cleanup_ht_and_iteration, cleanup_ht_and_iteration);

  ASSERT_GT(load_gen_.num_divergent_old_snapshot(), 0);

  // Expected hybrid_times of snapshots invalidated by history cleanup, and actual history cutoff
  // hybrid_times at which that happened. This is deterministic, but highly dependent on the
  // parameters at the top of this test.
  vector<pair<int, int>> expected_divergent_snapshot_and_cleanup_ht{
      {298,   774},
      {2000,  2341},
      {4000,  4762},
      {5000,  5049},
      {6000,  6027},
      {8000,  8423},
      {10000, 10061},
      {13000, 13137}
  };

  // Remove entries that don't apply to us because we did not get to do a cleanup at that
  // hybrid_time.
  RemoveEntriesWithSecondComponentHigherThan(&expected_divergent_snapshot_and_cleanup_ht,
                                             max_history_cleanup_ht.value());

  ASSERT_EQ(expected_divergent_snapshot_and_cleanup_ht,
            load_gen_.divergent_snapshot_ht_and_cleanup_ht());
}

TEST_F(RandomizedDocDBTest, TestNoFlush) {
  while (load_gen_.next_iteration() <= FLAGS_test_num_iter) {
    NO_FATALS(load_gen_.PerformOperation()) << "at iteration " << load_gen_.next_iteration();
  }
}

TEST_F(RandomizedDocDBTest, TestWithFlush) {
  while (load_gen_.next_iteration() <= FLAGS_test_num_iter) {
    NO_FATALS(load_gen_.PerformOperation()) << "at iteration " << load_gen_.next_iteration();
    if (load_gen_.next_iteration() % 250 == 0) {
      NO_FATALS(load_gen_.FlushRocksDB());
    }
  }
}

TEST_F(RandomizedDocDBTest, Snapshots) {
  RunWorkloadWithSnaphots(/* enable_history_cleanup = */ false);
}

TEST_F(RandomizedDocDBTest, SnapshotsWithHistoryCleanup) {
  RunWorkloadWithSnaphots(/* enable_history_cleanup = */ true);
}

// This is a bit different from SnapshotsWithHistoryCleanup. Here, we perform history cleanup within
// DocDBLoadGenerator::PerformOperation itself, reading the document being modified both before
// and after the history cleanup.
TEST_F(RandomizedDocDBTest, ImmediateHistoryCleanup) {
  while (load_gen_.next_iteration() <= FLAGS_test_num_iter) {
    if (load_gen_.next_iteration() % 250 == 0) {
      NO_FATALS(load_gen_.FlushRocksDB());
      NO_FATALS(load_gen_.PerformOperation(/* history_cleanup = */ true));
    } else {
      NO_FATALS(load_gen_.PerformOperation());
    }
  }
}

}  // namespace docdb
}  // namespace yb
