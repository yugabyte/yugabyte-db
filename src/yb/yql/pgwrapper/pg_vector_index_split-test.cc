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

#include <algorithm>

#include "yb/client/meta_cache.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"

#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/docdb_util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/vector_lsm.h"
#include "yb/vector_index/vector_lsm_metadata.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(tasks_tracker_keep_time_multiplier);
DECLARE_bool(enable_table_owned_vector_reverse_mapping);
DECLARE_bool(enable_tablet_split_of_tables_with_vector_index);
DECLARE_bool(vector_index_require_parent_data_compacted_before_split);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(TEST_sleep_after_vector_index_backfill_chunk_ms);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_uint64(vector_index_initial_chunk_size);

namespace yb::tablet {

extern bool TEST_skip_vector_index_post_split_compaction;
extern bool TEST_vector_index_force_parent_data_not_compacted;

} // namespace yb::tablet

namespace yb::pgwrapper {

////////////////////////////////////////////////////////
// PgDistributedVectorIndexTest
////////////////////////////////////////////////////////

using PgDistributedVectorIndexTestParam = std::tuple<VectorIndexEngine, PackingMode>;

template <>
struct TestParamTraits<PgDistributedVectorIndexTestParam> {
  using ParamType = PgDistributedVectorIndexTestParam;

  static bool IsColocated(const ParamType& param) {
    return false;
  }

  static VectorIndexEngine Engine(const ParamType& param) {
    return std::get<0>(param);
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return std::get<1>(param);
  }

  static auto TestParamGenerator() {
    return testing::Combine(
        testing::ValuesIn(kVectorIndexEngineArray),
        testing::ValuesIn(kPackingModeArray));
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      return ParamsToString(
          std::nullopt, Engine(param_info.param), GetPackingMode(param_info.param));
    };
  }
};

template <typename TestClass>
using PgDistributedVectorIndexTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgDistributedVectorIndexTestParam>;

class PgDistributedVectorIndexTest
    : public PgDistributedVectorIndexTestParamsDecorator<PgVectorIndexTestBase> {
  using Base = PgDistributedVectorIndexTestParamsDecorator<PgVectorIndexTestBase>;

 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) =
        GetCleanupSplitTabletsIntervalSec();
    Base::SetUp();
  }

  virtual int GetCleanupSplitTabletsIntervalSec() const {
    return 1;
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgDistributedVectorIndexTest);

// A dropped vector index retained by a snapshot schedule must survive a split of its indexed
// table's tablet followed by a master restart. The reloaded master maps the index onto the split
// children too, which are not retained, and removing it from them must keep the retained split
// parent. Otherwise the index is deleted and PITR to before the drop crashes the master.
TEST_P(PgDistributedVectorIndexTest, DroppedIndexRetainedAcrossSplitAndMasterRestart) {
  constexpr size_t kNumRows = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  num_pre_split_tablets_ = 1;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  auto tablet_id = peers.front()->tablet_id();
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  auto vector_indexes = tablet->vector_indexes().List();
  ASSERT_EQ(vector_indexes->size(), 1);
  auto index_id = vector_indexes->front()->table_id();

  auto restore_at = cluster_->mini_master(0)->Now();
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));

  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), tablet_id));
  ASSERT_OK(RestartMaster());

  auto index_tablets = [this, &index_id]() -> Result<master::TabletInfos> {
    auto& catalog_manager = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
    auto index = catalog_manager.GetTableInfo(index_id);
    SCHECK_NOTNULL(index);
    SCHECK(!index->LockForRead()->is_deleted(), IllegalState, "Index table deleted");
    return index->GetTabletsIncludeInactive();
  };

  // Hidden table cleanup removes the index from the split children.
  ASSERT_OK(WaitFor(
      [&index_tablets, &tablet_id]() -> Result<bool> {
        return std::ranges::all_of(VERIFY_RESULT(index_tablets()), [&tablet_id](const auto& t) {
          return t->tablet_id() == tablet_id;
        });
      },
      30s * kTimeMultiplier, "Index removed from split children"));

  // The index stays on the retained split parent.
  auto tablets = ASSERT_RESULT(index_tablets());
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets.front()->tablet_id(), tablet_id);

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));

  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows, AddFilter::kFalse));
}

// A vector index dropped after a split of its indexed table's tablet still lists the split parent.
// The snapshot schedule releases the parent first, and the master deletes it and drops it from its
// maps. That must also drop it from the hidden index, so the index is cleaned up once the schedule
// releases it too. See issue #35111.
TEST_P(PgDistributedVectorIndexTest, DroppedIndexCleanedUpAfterSplitParentDeleted) {
  constexpr size_t kNumRows = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  // The finished DeleteTablet tasks of the split parent hold its TabletInfo while they are kept by
  // the tasks tracker, which would hide the issue.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tasks_tracker_keep_time_multiplier) = 0;
  num_pre_split_tablets_ = 1;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  const auto retention = 30s * kTimeMultiplier;
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 1s * kTimeMultiplier,
      retention));

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  auto tablet_id = peers.front()->tablet_id();
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  auto table_id = tablet->metadata()->table_id();
  auto vector_indexes = tablet->vector_indexes().List();
  ASSERT_EQ(vector_indexes->size(), 1);
  auto index_id = vector_indexes->front()->table_id();
  tablet.reset();
  peers.clear();

  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), tablet_id));

  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  ASSERT_OK(WaitFor(
      [&catalog_manager, &tablet_id]() -> Result<bool> {
        return VERIFY_RESULT(catalog_manager.GetTabletInfo(tablet_id))->LockForRead()->is_hidden();
      },
      30s * kTimeMultiplier, "Split parent hidden"));
  // The schedule must release the split parent well before the index, so the parent is deleted and
  // dropped from the master's maps while the index still lists it.
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(
      schedule_id, cluster_->mini_master(0)->Now().AddDelta(5s * kTimeMultiplier)));

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));

  ASSERT_OK(WaitFor(
      [&catalog_manager, &table_id, &tablet_id] {
        return std::ranges::any_of(
            catalog_manager.GetDeletedSplitParents(table_id),
            [&tablet_id](const auto& entry) { return entry.first == tablet_id; });
      },
      retention + 30s * kTimeMultiplier, "Split parent dropped from the master's maps"));
  auto index = catalog_manager.GetTableInfo(index_id);
  ASSERT_NE(index, nullptr);
  ASSERT_TRUE(index->LockForRead()->is_hidden_but_not_deleting());

  ASSERT_OK(WaitFor(
      [&index] { return index->LockForRead()->is_deleted(); },
      retention + 30s * kTimeMultiplier, "Dropped index deleted"));
}

// Use `BaseTable` test name prefix to identify the tests where indexable table split happens
// before the vector index creation.
TEST_P(PgDistributedVectorIndexTest, BaseTableManualSplitSimple) {
  constexpr size_t kNumRows = 20;

  // Small data blocks so compact rows (and any reverse-mapping records) span multiple blocks;
  // otherwise GetMiddleKey's single-block fallback can pick an internal key and split fails.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));

  // Wait for all intents are applied and flush tablets.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Trigger tablet split.
  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.front()->tablet_id()));

  // Create vector index.
  ASSERT_OK(CreateIndex(conn));

  // Select whole set of vectors and verify.
  auto query_vector = Vector(RandomUniformInt(0UL, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));

  // It's OK if searching for all vectors returns less number of vectors due to
  // algorithm's recall factor. We can tolerate 90% of recall.
  ASSERT_GE(static_cast<float>(num_found), kNumRows * 0.9);
  ASSERT_LE(num_found, kNumRows);

  // Make sure drop table works fine.
  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

TEST_P(PgDistributedVectorIndexTest, ManualSplitSimple) {
  constexpr size_t kNumRows = 80;
  num_pre_split_tablets_ = 2;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 0, kNumRows - 1, /* keep_vectors = */ true));

  // Wait for all intents are applied and flush tablets.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Trigger tablet split for the second tablet.
  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 2);
  std::ranges::sort(peers, [](const auto& peer1, const auto& peer2) {
    const auto& part1 = peer1->tablet_metadata()->partition()->partition_key_start();
    const auto& part2 = peer2->tablet_metadata()->partition()->partition_key_start();
    return part1 < part2;
  });

  LOG(INFO) << "Splitting tablet " << peers.back()->tablet_id();
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
     cluster_.get(), peers.back()->tablet_id()));

  // Make sure parent tablet got cleaned up.
  SleepFor(MonoDelta::FromSeconds(
      2 * ANNOTATE_UNPROTECTED_READ(FLAGS_cleanup_split_tablets_interval_sec)));

  const auto unsplit_tablet = peers.front()->tablet_id();
  peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 3);

  // Verify tablet dirs exist.
  auto* env = Env::Default();
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    DCHECK_ONLY_NOTNULL(tablet.get());
    const auto* meta = tablet->metadata();
    ASSERT_TRUE(env->DirExists(meta->rocksdb_dir()));
    ASSERT_TRUE(env->DirExists(meta->intents_rocksdb_dir()));
    ASSERT_TRUE(env->DirExists(meta->snapshots_dir()));
    auto indexes = tablet->vector_indexes().List();
    ASSERT_TRUE(indexes);
    ASSERT_FALSE(indexes->empty());
    for (const auto& vi : *indexes) {
      const auto vi_dir = meta->vector_index_dir(vi->options());
      ASSERT_TRUE(env->DirExists(vi_dir));
      if (unsplit_tablet == tablet->tablet_id()) {
        const auto files = AsString(
            ASSERT_RESULT(vector_index::ListVectorLSMFiles(env, vi_dir)).manifest_and_chunk_files);
        const auto expected_files = Format(
            "[0.meta, vectorindex_1$0]",
            docdb::GetVectorIndexChunkFileExtension(vi->options()));
        ASSERT_STR_EQ(files, expected_files);
      } else {
        // Wait for compaction is done.
        // InitFrontiers flushes the frontier (creating manifest 1), then compaction
        // creates manifest 2 and the compacted chunk file.
        ASSERT_OK(
            LoggedWaitFor([vi]() -> Result<bool> {
              return vi->TEST_NextManifestFileNo() > 2;
            }, MonoDelta::FromSeconds(10),
            Format("Vector index compaction,tablet $0", tablet->tablet_id()))
        );
        const auto expected_files = Format(
            "[0.meta, 1.meta, 2.meta, vectorindex_2$0]",
            docdb::GetVectorIndexChunkFileExtension(vi->options()));
        // The compacted chunk file is removed in background, after the compaction is done.
        std::string files;
        auto status = LoggedWaitFor([env, &vi_dir, &expected_files, &files]() -> Result<bool> {
          files = AsString(VERIFY_RESULT(vector_index::ListVectorLSMFiles(env, vi_dir))
              .manifest_and_chunk_files);
          return files == expected_files;
        }, 10s * kTimeMultiplier, "Compacted chunk file removed");
        ASSERT_TRUE(status.ok())
            << status << ", files: " << files << ", expected: " << expected_files;
      }
    }
  }

  // Select whole set of vectors and verify.
  auto query_vector = Vector(RandomUniformInt(0UL, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));

  // It's OK if searching for all vectors returns less number of vectors due to
  // algorithm's recall factor. We can tolerate 90% of recall.
  ASSERT_GE(static_cast<float>(num_found), kNumRows * 0.9);
  ASSERT_LE(num_found, kNumRows);

  // TODO(vector_index): Verify compacted tablets content once GH29378 is fixed.
}

// GH#32321: a tablet split should be postponed until its vector index backfill has finished.
// During backfill the reverse mapping records are written into the tablet's regular RocksDB. That
// growth pushes the tablet past the split threshold, so without this enhancement the split starts
// while the backfill is still running.
TEST_P(PgDistributedVectorIndexTest, AutoSplitDuringBackfill) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(500, 200);

  // Split threshold is sized for backfill-written reverse mappings; table-owned skips that path.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

  // Allow splitting of a table that has a vector index; otherwise the split is rejected outright.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_tables_with_vector_index) = true;

  // Do not split while the base table is being populated; the split should only be considered once
  // the vector index backfill starts writing reverse mapping records.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  // Backfill the index in many small chunks and slow each chunk down, so the reverse mapping
  // records accumulate and the backfill stays in progress long enough for the master to trigger
  // the split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kNumRows / 20 + 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sleep_after_vector_index_backfill_chunk_ms) =
      100 * kTimeMultiplier;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 0, kNumRows - 1, /* keep_vectors = */ true));

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Counts the distinct tablets of the "test" table. Grows above one once the tablet splits.
  auto num_tablets = [this]() -> Result<size_t> {
    auto peers = VERIFY_RESULT(
        ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kAll));
    std::unordered_set<TabletId> tablet_ids;
    for (const auto& peer : peers) {
      tablet_ids.insert(peer->tablet_id());
    }
    return tablet_ids.size();
  };

  // Pick a split threshold that the base table alone does not reach, but that the extra reverse
  // mapping records written during the backfill will.
  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  const auto base_size =
      ASSERT_RESULT(peers.front()->shared_tablet())->GetCurrentVersionSstFilesSize();
  LOG(INFO) << "Base table SST size: " << base_size;
  ASSERT_EQ(ASSERT_RESULT(num_tablets()), 1);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = base_size + 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

  std::atomic<bool> backfill_done{false};

  TestThreadHolder threads;
  // Keep flushing the regular RocksDB so the reverse mapping records written during backfill land
  // in SST files and get reported to the master while the backfill is still running.
  threads.AddThreadFunctor([this, &backfill_done] {
    while (!backfill_done.load(std::memory_order_acquire)) {
      ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
      std::this_thread::sleep_for(100ms * kTimeMultiplier);
    }
  });
  // Watch for the tablet split. When it happens, the vector index backfill must already be done:
  // the split has to wait until the backfill completes.
  threads.AddThreadFunctor([&backfill_done, &num_tablets, &stop = threads.stop_flag()] {
    while (!stop.load(std::memory_order_acquire)) {
      if (ASSERT_RESULT(num_tablets()) > 1) {
        ASSERT_TRUE(backfill_done.load(std::memory_order_acquire))
            << "Tablet split started before the vector index backfill finished";
        break;
      }
      std::this_thread::sleep_for(50ms * kTimeMultiplier);
    }
  });

  // Backfill the vector index. The reverse mapping growth pushes the tablet over the split
  // threshold.
  ASSERT_OK(CreateIndex(conn));
  backfill_done.store(true, std::memory_order_release);
  threads.Stop();

  // Consolidate the many small SST files produced by the periodic flushes above into a single SST,
  // so the tablet has a findable split key.
  ASSERT_OK(cluster_->CompactTablets());

  // The split should still happen once the backfill completes, confirming the tablet actually
  // exceeded the split threshold (i.e. the scenario was exercised, not just avoided).
  ASSERT_OK(WaitFor([&num_tablets]() -> Result<bool> {
    return VERIFY_RESULT(num_tablets()) > 1;
  }, 60s * kTimeMultiplier, "Tablet split after backfill"));

  // The index must still return all rows. TEST_vector_index_exact makes the search exact, so every
  // row must be found.
  auto query_vector = Vector(RandomUniformInt<size_t>(0, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));
  ASSERT_EQ(num_found, kNumRows);
}

TEST_P(PgDistributedVectorIndexTest, AnotherVectorIndexCreationAfterManualSplit) {
  constexpr size_t kNumRows = 80;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1));

  const auto main_table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto main_table = ASSERT_RESULT(client_->OpenTable(main_table_id));

  // Warm meta cache with pre-split layout.
  auto count = ASSERT_RESULT(conn.FetchAllAsString("SELECT COUNT(*) FROM test"));
  ASSERT_EQ(kNumRows, std::stoi(count));

  // Keep a stale base-table schema snapshot (without vector indexes) so that
  // RefreshTablePartitions(base) does not erase vector-index entries from MetaCache.
  ASSERT_TRUE(main_table->index_map().empty());

  ASSERT_OK(CreateIndex(conn));

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.front()->tablet_id()));

  LOG(INFO) << "Creating another index";
  ASSERT_OK(CreateIndex(conn, "another"));

  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

TEST_P(PgDistributedVectorIndexTest, MetaCacheBaseTableStaleLookupAfterSplit) {
  constexpr size_t kNumRows = 40;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));

  const auto main_table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto main_table = ASSERT_RESULT(client_->OpenTable(main_table_id));

  // Warm up cache with pre-split tablet layout.
  ASSERT_EQ(ASSERT_RESULT(LookupNumTablets(main_table)), 1);

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.front()->tablet_id()));

  ASSERT_OK(CreateIndex(conn));

  const auto vector_table_id = ASSERT_RESULT(GetTableIDFromTableName(kVectorIndexName));
  auto vector_table = ASSERT_RESULT(client_->OpenTable(vector_table_id));

  // Vector index lookup should observe post-split partitions.
  ASSERT_GE(ASSERT_RESULT(LookupNumTabletsWithRetry(vector_table)), 2);

  // The expectation here is to have main table still see stale partition list, because
  // nothing was triggered for the main table since the split.
  ASSERT_EQ(1, ASSERT_RESULT(LookupNumTablets(main_table)));

  auto query_vector = Vector(RandomUniformInt(0UL, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));
  ASSERT_GE(static_cast<float>(num_found), kNumRows * 0.9);
  ASSERT_LE(num_found, kNumRows);

  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

TEST_P(PgDistributedVectorIndexTest, MetaCacheLookupAfterDropWithoutReads) {
  constexpr size_t kNumRows = 20;

  // Small data blocks so compact rows (and any reverse-mapping records) span multiple blocks;
  // otherwise GetMiddleKey's single-block fallback can pick an internal key and split fails.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));

  // Warm up main table cache before split.
  const auto main_table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto main_table = ASSERT_RESULT(client_->OpenTable(main_table_id));
  ASSERT_EQ(ASSERT_RESULT(LookupNumTablets(main_table)), 1);

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.front()->tablet_id()));

  // Create vector index after the base table split and then drop the table
  // without running any vector query. The main table may still have stale
  // partition information in cache at this point.
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

// After a network cut, other requests leave the tablet leaders marked as failed in the meta cache.
// The readiness poll should refresh tablet locations instead of polling until the timeout.
TEST_P(PgDistributedVectorIndexTest, CreateIndexWithFailedLeadersInMetaCache) {
  constexpr size_t kNumRows = 64;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  // So the test fails instead of hanging if CREATE INDEX never returns.
  ASSERT_OK(conn.ExecuteFormat(
      "SET statement_timeout = $0", ToMilliseconds(60s * kTimeMultiplier)));

  std::atomic<bool> poll_started{false};
  std::atomic<size_t> num_marked_failed{0};
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "PgClientService::PollVectorIndexReady:TabletsLookedUp",
      [&poll_started, &num_marked_failed](void* arg) {
        if (poll_started.exchange(true)) {
          return;
        }
        auto& tablets = *static_cast<std::vector<client::internal::RemoteTabletPtr>*>(arg);
        for (const auto& tablet : tablets) {
          auto* leader = tablet->LeaderTServer();
          if (leader &&
              tablet->MarkReplicaFailed(leader, STATUS(NetworkError, "Simulated network cut"))) {
            ++num_marked_failed;
          }
        }
      });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(CreateIndex(conn));
  ASSERT_GT(num_marked_failed.load(), 0);
}

// The test verifies that a split is blocked if parent tablet has uncompacted vector indexes data.
TEST_P(PgDistributedVectorIndexTest, SplitBlockedWithOrphanedPostSplitData) {
  constexpr size_t kNumRows = 80;

  // The knobs below are process-wide, restore them to not affect the following tests.
  auto flags_restorer = ScopeExit([
      require = FLAGS_vector_index_require_parent_data_compacted_before_split,
      force_not_compacted = tablet::TEST_vector_index_force_parent_data_not_compacted,
      skip_compaction = tablet::TEST_skip_vector_index_post_split_compaction] {
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_vector_index_require_parent_data_compacted_before_split) = require;
    ANNOTATE_UNPROTECTED_WRITE(
        tablet::TEST_vector_index_force_parent_data_not_compacted) = force_not_compacted;
    ANNOTATE_UNPROTECTED_WRITE(
        tablet::TEST_skip_vector_index_post_split_compaction) = skip_compaction;
  });

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 0, kNumRows - 1));

  // Wait for all intents are applied and flush tablets.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);

  // Trigger tablet split for the only tablet.
  LOG(INFO) << "Splitting tablet " << peers.back()->tablet_id();
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.back()->tablet_id()));

  // Make sure parent tablet got cleaned up.
  SleepFor(MonoDelta::FromSeconds(
      2 * ANNOTATE_UNPROTECTED_READ(FLAGS_cleanup_split_tablets_interval_sec)));

  // Make sure all peers meta data was updated.
  peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), 2 * cluster_->num_tablet_servers());
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    LOG(INFO) << "Checking parent data compaction status for peer "
              << peer->permanent_uuid() << " tablet " << tablet->tablet_id();
    ASSERT_TRUE(tablet->metadata()->rocksdb_parent_data_compacted());
    ASSERT_TRUE(tablet->vector_indexes().ParentDataCompacted());
  }

  // Let's be explicit on the exected parent data compaction state.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_require_parent_data_compacted_before_split) = true;

  // Simulate vector index post-split compaction still being in progress.
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  const auto tablet_ids = ListTabletIdsForTable(cluster_.get(), table_id);
  ASSERT_EQ(tablet_ids.size(), 2);
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_force_parent_data_not_compacted) = true;

  // Invoke split and check the required reason for the block is being logged. The logs scan
  // is required because the split is done asynchronously and has several steps.
  {
    auto log_waiter = StringWaiterLogSink("Tablet has orphaned post-split data");
    auto status = InvokeSplitTabletRpc(cluster_.get(), *tablet_ids.begin());
    ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(5 * kTimeMultiplier)));
  }

  // Validate that the split does not wait for vector index parent data to be compacted if the
  // flag is not set. Vector index post-split compaction is skipped, so the new children keep
  // their inherited parent data while RocksDB post-split compaction completes.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_force_parent_data_not_compacted) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_skip_vector_index_post_split_compaction) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_require_parent_data_compacted_before_split) = false;

  const auto tablet_ids_before_split = ListActiveTabletIdsForTable(cluster_.get(), table_id);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), *tablet_ids.begin()));

  // The wait above returns as soon as RocksDB post-split compaction is done, so the new children
  // must be RocksDB compacted with vector index parent data still in place. Only the new children
  // are inspected, hence there's no need to wait for the parent tablet cleanup.
  size_t new_tablet_peers = 0;
  for (const auto& peer : ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"))) {
    if (tablet_ids_before_split.contains(peer->tablet_id())) {
      continue;
    }
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    LOG(INFO) << "Checking parent data compaction status for peer "
              << peer->permanent_uuid() << " tablet " << tablet->tablet_id();
    ASSERT_TRUE(tablet->metadata()->rocksdb_parent_data_compacted());
    ASSERT_FALSE(tablet->vector_indexes().ParentDataCompacted());
    ++new_tablet_peers;
  }

  ASSERT_EQ(new_tablet_peers, 2 * cluster_->num_tablet_servers());
}

// Simulates a binary rollback that dropped KvStoreInfo.split_generation (superblock 0) while the
// vector index still has the generation. Bootstrap must restore it so the next split increments
// past that value.
TEST_P(PgDistributedVectorIndexTest, SplitGenerationRestoredAfterSuperblockReset) {
  constexpr size_t kNumRows = 80;
  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 0, kNumRows - 1));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto leaders = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(leaders.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), leaders.front()->tablet_id()));
  SleepFor(MonoDelta::FromSeconds(
      2 * ANNOTATE_UNPROTECTED_READ(FLAGS_cleanup_split_tablets_interval_sec)));

  std::unordered_map<TabletId, uint64_t> expected_generation;
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_FALSE(peers.empty());
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    const auto gen = tablet->metadata()->split_generation();
    // Parent tablets keep generation 0; only split children have a value to restore.
    if (gen == 0) {
      continue;
    }
    ASSERT_EQ(tablet->vector_indexes().MaxPersistedSplitGeneration(), gen);
    expected_generation.emplace(tablet->tablet_id(), gen);
    tablet->metadata()->set_split_generation(0);
    ASSERT_OK(tablet->metadata()->Flush());
    ASSERT_EQ(tablet->metadata()->split_generation(), 0);
  }
  ASSERT_FALSE(expected_generation.empty());

  ASSERT_OK(RestartCluster());

  peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  size_t restored_replicas = 0;
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto it = expected_generation.find(tablet->tablet_id());
    if (it == expected_generation.end()) {
      continue;
    }
    ++restored_replicas;
    ASSERT_EQ(tablet->metadata()->split_generation(), it->second);
    ASSERT_EQ(tablet->vector_indexes().MaxPersistedSplitGeneration(), it->second);
  }
  ASSERT_EQ(restored_replicas, expected_generation.size() * cluster_->num_tablet_servers());

  // The next split must continue from the restored generation, otherwise the new children would
  // not treat their inherited vectors as parent data.
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  const auto [split_source_id, source_generation] = *expected_generation.begin();
  const auto tablet_ids_before_split = ListActiveTabletIdsForTable(cluster_.get(), table_id);

  // A freshly restarted master rejects split candidates until it has refreshed tablespace info.
  ASSERT_OK(WaitFor([this, id = split_source_id]() -> Result<bool> {
    auto status = InvokeSplitTabletRpc(cluster_.get(), id);
    if (!status.ok()) {
      LOG(INFO) << "Split RPC is not accepted yet: " << status;
    }
    return status.ok();
  }, 60s * kTimeMultiplier, "Split RPC accepted"));

  // The children's split generation is set on tablet open, so there's no need to wait for their
  // post-split compaction.
  ASSERT_OK(WaitForTableActiveTabletLeadersPeers(
      cluster_.get(), table_id, tablet_ids_before_split.size() + 1));
  ASSERT_OK(WaitAllReplicasReady(cluster_.get(), table_id, 30s * kTimeMultiplier));

  size_t next_generation_replicas = 0;
  for (const auto& peer : ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"))) {
    if (tablet_ids_before_split.contains(peer->tablet_id())) {
      continue;
    }
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    ASSERT_EQ(tablet->metadata()->split_generation(), source_generation + 1);
    ASSERT_EQ(tablet->vector_indexes().MaxPersistedSplitGeneration(), source_generation + 1);
    ++next_generation_replicas;
  }
  ASSERT_EQ(next_generation_replicas, 2 * cluster_->num_tablet_servers());
}

}  // namespace yb::pgwrapper
