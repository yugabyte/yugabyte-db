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

#pragma once

#include <algorithm>
#include <numeric>
#include <random>

#include "yb/client/client_fwd.h"

#include "yb/common/column_id.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tablet/kv_formatter.h"
#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/enums.h"
#include "yb/util/mem_tracker_fwd.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/strongly_typed_bool.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pgvector_utils.h"

namespace yb::pgwrapper {

YB_STRONGLY_TYPED_BOOL(AddFilter);
YB_STRONGLY_TYPED_BOOL(Backfill);
YB_STRONGLY_TYPED_BOOL(CrashAfterRestoreReopen);
YB_STRONGLY_TYPED_BOOL(NonTransactionalWrites);
YB_STRONGLY_TYPED_BOOL(NullVectorBeforeSchemaChange);
YB_STRONGLY_TYPED_BOOL(StorePayload);
YB_STRONGLY_TYPED_BOOL(UseSharedMemory);
YB_STRONGLY_TYPED_BOOL(WaitForIntents);

using FloatVector = std::vector<float>;
inline const std::string kVectorIndexName = "vi";

// Default HNSW build parameters used by CreateIndex/MakeIndex. Tests that build large indexes and
// care about build time (not recall) can pass cheaper parameters instead.
inline const std::string kDefaultIndexBuildOptions = "ef_construction = 256, m = 32, m0 = 128";

// Cheapest parameters the reloption bounds allow (m >= 5, ef_construction >= 50), for tests that
// care about the number of indexes rather than recall.
inline const std::string kCheapIndexBuildOptions = "ef_construction = 50, m = 5, m0 = 8";

const unum::usearch::byte_t* VectorToBytePtr(const FloatVector& vector);

YB_DEFINE_ENUM(VectorIndexEngine, (kYbHnswHnswlib));
YB_DEFINE_ENUM(PackingMode, (kNone)(kV1)(kV2));

// Returns the tablet peers matching `filter` that currently host at least one vector index.
std::vector<tablet::TabletPeerPtr> ListTabletPeersWithVectorIndexes(
    MiniCluster* cluster, ListPeersFilter filter = ListPeersFilter::kAll);

// Waits for every replica to apply all the ops its leader has committed. The leader must be
// LEADER_AND_READY, since a new leader's committed op id may lag writes already acknowledged.
Status WaitAllReplicasAppliedLeaderCommitted(MiniCluster* cluster, CoarseDuration timeout);

uint64_t SumHistograms(const std::vector<const HdrHistogram*>& histograms);

// Creates echo_int(INT), a volatile function returning its argument.
Status CreateEchoFunction(PGConn& conn);

// Consumption of the named direct child of `parent`, or 0 if `parent` or the child is absent.
int64_t ChildConsumption(const MemTrackerPtr& parent, const std::string& child);

MemTrackerPtr ServerMemTracker(MiniCluster* cluster);

int64_t BlockCacheConsumption(MiniCluster* cluster);

// Peak heap footprint of all vector indexes on the server, summed across tablets. The hierarchy is
// server -> "Tablets_overhead" -> "tablet-<id>" -> "vector_indexes". We use the peak rather than
// the current consumption because the index is built on the heap (usearch/hnswlib) but a memtable
// flush can later seal the chunk and convert it to the on-disk yb_hnsw block-cache form, which
// frees the heap tracker; the peak still reflects the footprint the build reserved cache space for.
int64_t VectorIndexPeakConsumption(MiniCluster* cluster);

// kNoTypePrefix marks packed V2 column values, which do not carry a ValueEntryType prefix byte.
constexpr char kNoTypePrefix = '\0';

char ParseTypePrefixFromValueDump(const std::string& value_dump, PackingMode packing_mode);

std::optional<std::string> ExtractPackedColumnValueDump(
    const std::string& value_dump, ColumnId column_id);

Status CheckTypePrefixes(const std::vector<char>& prefixes, char expected, size_t expected_count);

////////////////////////////////////////////////////////
// PgVectorIndexTestBase
////////////////////////////////////////////////////////

class PgVectorIndexTestBase : public PgMiniTestBase {
 protected:
  virtual bool IsColocated() const = 0;
  virtual VectorIndexEngine Engine() const = 0;
  virtual PackingMode GetPackingMode() const = 0;

  virtual int GetFileNumCompactionTrigger() {
    return 5;
  }

  void SetUp() override;

  std::string DbName() {
    return IsColocated() ? "colocated_db" : "yugabyte";
  }

  Result<PGConn> Connect() const override {
    return IsColocated() ? ConnectToDB("colocated_db") : PgMiniTestBase::Connect();
  }

  Status WaitForTabletSplit(const TableId& table_id, size_t num_splits = 1);

  Result<PGConn> MakeTable(size_t dimensions = 3);

  Status CreateIndex(
       PGConn& conn, const std::string& index_name = kVectorIndexName,
       std::optional<vector_index::DistanceKind> distance_kind = std::nullopt,
       const std::string& column = std::string(),
       const std::string& build_options = kDefaultIndexBuildOptions) {
    return conn.ExecuteFormat(
        "CREATE INDEX $1 ON test USING ybhnsw ($2 $0) WITH ($3)",
        pgwrapper::VectorOpsName(distance_kind.value_or(distance_kind_)), index_name,
        column.empty() ? "embedding" : column, build_options);
  }

  Result<PGConn> MakeIndex(
      size_t dimensions = 3, bool table_exists = false,
      const std::string& build_options = kDefaultIndexBuildOptions) {
    auto conn =  VERIFY_RESULT(table_exists ? Connect() : MakeTable(dimensions));
    RETURN_NOT_OK(CreateIndex(conn, kVectorIndexName, std::nullopt, std::string(), build_options));
    return conn;
  }

  Result<PGConn> MakeIndexAndFill(
      size_t num_rows, Backfill backfill = Backfill::kFalse, bool keep_vectors = false);
  Result<PGConn> MakeIndexAndFillRandom(size_t num_rows);
  Status InsertRows(
      PGConn& conn, size_t start_row, size_t end_row, bool keep_vectors = false,
      NonTransactionalWrites non_transactional_writes = NonTransactionalWrites::kFalse);
  Status InsertRandomRows(PGConn& conn, size_t num_rows);

  // Inserts `count` rows with ids [start_id, start_id + count) as a single multi-row statement, so
  // they reach VectorLSM::Insert as one batch that fans out into per-vector insert tasks.
  Status InsertBatch(PGConn& conn, int64_t start_id, int64_t count);

  // Waits until all `expected_num_indexes` vector indexes of the cluster report their backfill as
  // finished. Fails right away if a backfill failed.
  Status WaitForVectorIndexBackfills(size_t expected_num_indexes, const std::string& description);

  // Waits until all `expected_num_indexes` vector indexes of the cluster have inserted the first
  // batch of their backfill.
  // The backfill_inserted_entries metric is per table and tserver, so this needs one tablet per
  // tserver.
  Status WaitForVectorIndexBackfillsStarted(size_t expected_num_indexes);

  void VerifyRead(PGConn& conn, size_t limit, AddFilter add_filter);
  void VerifyRows(
      PGConn& conn, AddFilter add_filter, const std::vector<std::string>& expected,
      int64_t limit = -1);
  void VerifyRows(
      PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
      int64_t limit = -1);
  [[nodiscard]] bool RowsMatch(
      PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
      int64_t limit = -1);

  // Returns number of vectors actually returned by the search.
  template <typename IdxExtractor>
  Result<size_t> FetchAndVerifyOrder(
      PGConn& conn, const FloatVector& query_vector, size_t limit, IdxExtractor&& extractor);

  Result<size_t> FetchAndVerifyOrder(PGConn& conn, const FloatVector& query_vector, size_t limit);

  FloatVector Vector(int64_t id) {
    CHECK_GT(dimensions_, 0);
    FloatVector vector(dimensions_);
    std::ranges::generate(vector, [id, n{1LL}]() mutable { return id * n++; });
    return vector;
  }

  FloatVector RandomVector();

  std::string BuildRow(int64_t id, const std::string& value) {
    return Format("$0, $1", id, value);
  }

  std::string ExpectedRow(int64_t id) {
    return BuildRow(id, AsString(Vector(id)));
  }

  std::vector<std::string> ExpectedRows(size_t limit) {
    std::vector<std::string> expected;
    for (size_t i = 1; i <= limit; ++i) {
      expected.push_back(ExpectedRow(i));
    }
    return expected;
  }

  unum::usearch::metric_kind_t UsearchMetricKind() const;

  template <class Vector>
  std::string DistanceToQuery(const Vector& vector) const {
    return Format("embedding $0 '$1'", pgwrapper::DistanceOpName(distance_kind_), vector);
  }

  template <class Vector>
  std::string IndexQuerySuffix(const Vector& vector, size_t limit) const {
    return Format(" ORDER BY $0 LIMIT $1", DistanceToQuery(vector), limit);
  }

  Status RunSstDump(tablet::KVFormatter& formatter, const std::string& db_path);

  Status WaitNoBackgroundInserts(WaitForIntents wait_for_intents, MonoDelta timeout);

  // Runs VerifyVectorIndexes on every tablet server, which checks every replica of every tablet
  // with a vector index. Fails when some index could not be checked.
  Result<std::vector<tserver::VectorIndexReplicaCheckPB>> VerifyVectorIndexes();

  // Returns the inconsistencies found by VerifyVectorIndexes as sorted "<kind> <id>" entries,
  // with the id decoded from the ybctid of the reported row.
  Result<std::string> VectorIndexInconsistencies();

  Result<bool> HasVectorDeletion();

  // Restarts the cluster and waits until the tablets of test have loaded their transactions from
  // the intents DB.
  Status RestartAndWaitTransactionsLoaded();

  // Ids of the rows nearest to the origin.
  Result<std::string> NearestIds(PGConn& conn, size_t limit);

  static Status UpdateInTransaction(PGConn& conn, int64_t id, const std::string& vector);

  // Inserts rows first..last into test in one transaction.
  Status InsertInTransaction(PGConn& conn, size_t first, size_t last) {
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    for (size_t i = first; i <= last; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO test (id, embedding) VALUES ($0, '$1')", i, AsString(Vector(i))));
    }
    return conn.CommitTransaction();
  }

  // Ids that a search through the vector index of test returns, sorted. A seq scan would return
  // every row whatever the index holds, so the read must go through the index.
  Result<std::vector<int64_t>> FetchIdsThroughIndex(PGConn& conn, size_t limit);

  // Hides yb::FindTableId(MiniCluster*, ...) in derived classes.
  Result<TableId> FindTableId(const std::string& namespace_name, const std::string& table_name);

  Result<TableId> GetTableId(const std::string& db, const std::string& name);

  // Vector indexes on the replicas of table_id's tablets, only those on ts_uuid if it is set.
  std::vector<docdb::DocVectorIndexPtr> ListTableVectorIndexes(
      const TableId& table_id, const std::string& ts_uuid = {});

  // Moves the leadership of the tablet holding table_id until its term reaches min_term, never to
  // the tserver keep_off_uuid.
  Status RaiseTabletTerm(
      const TableId& table_id, int64_t min_term, const std::string& keep_off_uuid);

  // May return stale partitioning if the table was cached before the split.
  Result<std::vector<client::internal::RemoteTabletPtr>> LookupAllTabletsWithRetry(
      const client::YBTablePtr& table, size_t num_retries = 1);

  Result<size_t> LookupNumTabletsWithRetry(
      const client::YBTablePtr& table, size_t num_retries = 1);

  Result<size_t> LookupNumTablets(const client::YBTablePtr& table) {
    return LookupNumTabletsWithRetry(table, /* num_retries = */ 0);
  }

  // Sum and count of vector_index_partitions_queried over all tablet servers.
  std::pair<int64_t, uint64_t> PartitionsQueried();

  // Index of the partition of table_name that holds hash code hash.
  Result<size_t> HashPartition(const std::string& table_name, uint16_t hash);

  static std::vector<int64_t> Ids(size_t first, size_t last) {
    std::vector<int64_t> result(last - first + 1);
    std::iota(result.begin(), result.end(), first);
    return result;
  }

  std::vector<FloatVector> vectors_;
  std::uniform_real_distribution<> distribution_;
  std::mt19937_64 rng_{42};
  vector_index::DistanceKind distance_kind_ = vector_index::DistanceKind::kL2Squared;
  size_t dimensions_ = 0;
  size_t real_dimensions_ = 0;
  std::vector<size_t> shuffle_vector_;
  size_t num_pre_split_tablets_ = 0;
};

template <typename IdxExtractor>
Result<size_t> PgVectorIndexTestBase::FetchAndVerifyOrder(
    PGConn& conn, const FloatVector& query_vector, size_t limit, IdxExtractor&& extractor) {
  SCHECK_GT(vectors_.size(), 0, IllegalState, "Vectors must be filled");

  auto query = "SELECT id FROM test" + IndexQuerySuffix(query_vector, limit);
  auto result = VERIFY_RESULT(conn.FetchRows<int64_t>(query));

  unum::usearch::metric_punned_t metric(
      dimensions_, UsearchMetricKind(), unum::usearch::scalar_kind_t::f32_k);

  std::vector<typename unum::usearch::metric_punned_t::result_t> distances;
  distances.reserve(result.size());
  const auto* query_byte_vector = VectorToBytePtr(query_vector);
  for (auto key : result) {
    size_t idx = VERIFY_RESULT(extractor(key));
    const auto* byte_vector = VectorToBytePtr(vectors_[idx]);
    distances.push_back(metric(query_byte_vector, byte_vector));
  }

  LOG_WITH_FUNC(INFO) << "Requested: " << limit << ", found: " << result.size();
  VLOG_WITH_FUNC(2) << "   Result: " << AsString(result);
  VLOG_WITH_FUNC(2) << "Distances: " << AsString(distances);

  bool is_ordered = std::ranges::is_sorted(distances);
  SCHECK(is_ordered, InternalError, "Order does not match");

  return result.size();
}

////////////////////////////////////////////////////////
// PgVectorIndexTestParamsDecorator
////////////////////////////////////////////////////////

template <typename TestParam>
struct TestParamTraits {
  // Expected interface of specializations:
  using ParamType = TestParam;
  static bool IsColocated(const ParamType& param);
  static VectorIndexEngine Engine(const ParamType& param);
  static PackingMode GetPackingMode(const ParamType& param);
  static auto TestParamGenerator();
  static auto TestParamNameGenerator();

  // To prevent accidental creation.
  TestParamTraits() = delete;
};

template <typename TestClass, typename TestParam>
requires(std::is_base_of_v<PgVectorIndexTestBase, TestClass>)
class PgVectorIndexTestParamsDecoratorBase
    : public TestClass,
      public testing::WithParamInterface<TestParam> {
 public:
  using ParamTraits = TestParamTraits<TestParam>;

 protected:
  bool IsColocated() const override {
    return ParamTraits::IsColocated(this->GetParam());
  }

  VectorIndexEngine Engine() const override {
    return ParamTraits::Engine(this->GetParam());
  }

  PackingMode GetPackingMode() const override {
    return ParamTraits::GetPackingMode(this->GetParam());
  }
};

#define MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(test_suite_name) \
        INSTANTIATE_TEST_SUITE_P(, \
            test_suite_name, \
            test_suite_name::ParamTraits::TestParamGenerator(), \
            test_suite_name::ParamTraits::TestParamNameGenerator())

std::string ParamsToString(
    std::optional<bool> is_colocated, VectorIndexEngine engine, PackingMode packing_mode);

////////////////////////////////////////////////////////
// PgVectorIndexTest
////////////////////////////////////////////////////////

using PgVectorIndexTestParam = std::tuple<bool, VectorIndexEngine, PackingMode>;

template <>
struct TestParamTraits<PgVectorIndexTestParam> {
  using ParamType = PgVectorIndexTestParam;

  static bool IsColocated(const ParamType& param) {
    return std::get<0>(param);
  }

  static VectorIndexEngine Engine(const ParamType& param) {
    return std::get<1>(param);
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return std::get<2>(param);
  }

  static auto TestParamGenerator() {
    return testing::Combine(
        testing::Bool(),
        testing::ValuesIn(kVectorIndexEngineArray),
        testing::ValuesIn(kPackingModeArray));
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      auto engine = Engine(param_info.param);
      auto packing_mode = GetPackingMode(param_info.param);
      return ParamsToString(IsColocated(param_info.param), engine, packing_mode);
    };
  }
};

template <typename TestClass>
using PgVectorIndexTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgVectorIndexTestParam>;

// The Test* scenarios, each shared by several tests, are defined next to those tests.
class PgVectorIndexTest : public PgVectorIndexTestParamsDecorator<PgVectorIndexTestBase> {
 protected:
  void TestSimple(bool table_exists = false);
  void TestManyRows(AddFilter add_filter, Backfill backfill = Backfill::kFalse);
  void TestRestart(
      tablet::FlushFlags flush_flags,
      NonTransactionalWrites non_transactional_writes = NonTransactionalWrites::kFalse);
  void TestMetric(const std::string& expected);
  void TestRandom();
  void TestNullVectorsInIntents(bool commit);
  void TestBootstrapAfterSchemaChange(
      NullVectorBeforeSchemaChange null_vector, NonTransactionalWrites non_transactional_writes);
  void TestPerformDeadlineWithStuckLocalWrite(UseSharedMemory use_shared_memory);
  void TestSnapshotRestoreNonTransactionalWrites(CrashAfterRestoreReopen crash_after_reopen);
};

////////////////////////////////////////////////////////
// PgVectorIndexColocationOnlyTest
////////////////////////////////////////////////////////

// Colocation only; engine and packing stay kYbHnswHnswlib / kNone.
using PgVectorIndexColocationOnlyParam = bool;

template <>
struct TestParamTraits<PgVectorIndexColocationOnlyParam> {
  using ParamType = PgVectorIndexColocationOnlyParam;

  static bool IsColocated(const ParamType& param) {
    return param;
  }

  static VectorIndexEngine Engine(const ParamType&) {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  static PackingMode GetPackingMode(const ParamType&) {
    return PackingMode::kNone;
  }

  static auto TestParamGenerator() {
    return testing::Bool();
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      return param_info.param ? "Colocated" : "Distributed";
    };
  }
};

class PgVectorIndexColocationOnlyTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexTestBase, PgVectorIndexColocationOnlyParam> {
 protected:
  static constexpr auto kSourceDb = "source_db";
  static constexpr auto kCloneDb = "clone_db";

  void TestSkippedIndexStorageDestroyedOnRemove(bool with_kept_index);

  // Creates kSourceDb holding table test with rows 1..num_rows, one statement per row. The vector
  // index is left to the caller.
  Result<PGConn> CreateSourceTable(size_t num_rows);

  Status CreateSourceSnapshotSchedule();

  // Clones kSourceDb into kCloneDb through a schedule that CreateSourceSnapshotSchedule created.
  Status CloneSourceDbWithExistingSchedule() {
    auto admin_conn = VERIFY_RESULT(PgMiniTestBase::Connect());
    return admin_conn.ExecuteFormat("CREATE DATABASE $0 TEMPLATE $1", kCloneDb, kSourceDb);
  }

  Status CloneSourceDb() {
    RETURN_NOT_OK(CreateSourceSnapshotSchedule());
    return CloneSourceDbWithExistingSchedule();
  }

  void TestDroppedIndexKeepsUnflushedRows(bool skip_deactivation_flush);
};

////////////////////////////////////////////////////////
// PgVectorIndexSingleServerTestBase
////////////////////////////////////////////////////////

class PgVectorIndexSingleServerTestBase : public PgVectorIndexTestBase {
 public:
  PgVectorIndexSingleServerTestBase() {
    num_pre_split_tablets_ = 1;
  }

 protected:
  size_t NumTabletServers() override {
    return 1;
  }
};

// Expected Key -> Value format:
// 1) MetaKey(VectorId(uuid), [HT{ ... }]) -> DocKey(...)
// 2) MetaKey(VectorId(uuid), [HT{ ... }]) -> SubDocKey(DocKey(...), [ColumnId(...)])
// 3) MetaKey(VectorId(uuid), [HT{ ... }]) -> DEL
// Value contains only unsigned integer.
class TestKVFormatter : public tablet::KVFormatter {
  const std::string kKVDelimiter = " -> ";

 public:
  std::string Format(
      Slice key, Slice value, docdb::StorageDbType type, const std::string& key_suffix,
      docdb::AllowEmptyValue allow_empty_value) const override;

  std::string ExtractIdx(const std::string& ybctid) const;

  std::string FormatYbctid(const std::string& ybctid) const;

  std::string FormatVectorsMeta() const;

  void Clear(bool clean_vectors = false) {
    entries_.clear();
    if (clean_vectors) {
      vector_labels_.clear();
      ybctid_vectors_.clear();
    }
  }

 private:
  struct Entry {
    std::string vector_id;
    std::string ht;
    std::string ybctid;

    std::string ToString() const {
      return yb::Format("{ $0 [$1] => $2 }", vector_id, ht, ybctid);
    }

    static bool LessByHtAndYbctid(const Entry& a, const Entry& b);
  };

  // All vector index reverse mapping entries.
  mutable std::vector<Entry> entries_;

  // Mapping between vector id and vector label.
  mutable std::unordered_map<std::string, std::string> vector_labels_;

  // Collection of all vectors per ybctid.
  mutable std::unordered_map<std::string, std::unordered_set<std::string>> ybctid_vectors_;
};

class PgVectorIndexSingleServerDumpTestBase : public PgVectorIndexSingleServerTestBase {
 protected:
  // Flushes the single tablet and returns the vector index reverse mapping entries currently
  // persisted in the Regular DB.
  Result<std::string> DumpSingleTabletReverseMapping(const std::string& table_name = "test");

  Status CompactTabletForTable(const std::string& table_name = "test");
};

}  // namespace yb::pgwrapper
