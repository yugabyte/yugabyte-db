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

#include <queue>

#include "yb/client/snapshot_test_util.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_vector_index.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/usearch_include_wrapper_internal.h"
#include "yb/vector_index/distance.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_skip_process_apply);
DECLARE_bool(TEST_use_custom_varz);
DECLARE_bool(TEST_usearch_exact);
DECLARE_bool(vector_index_disable_compactions);
DECLARE_bool(vector_index_no_deletions_skip_filter_check);
DECLARE_bool(vector_index_use_hnswlib);
DECLARE_bool(vector_index_use_yb_hnsw);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_uint32(vector_index_concurrent_reads);
DECLARE_uint32(vector_index_concurrent_writes);
DECLARE_uint64(vector_index_initial_chunk_size);
DECLARE_uint64(vector_index_max_insert_tasks);

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);

namespace yb::docdb {

extern bool TEST_vector_index_filter_allowed;
extern size_t TEST_vector_index_max_checked_entries;

}

namespace yb::tablet {

extern bool TEST_block_after_backfilling_first_vector_index_chunks;
extern bool TEST_fail_on_seq_scan_with_vector_indexes;

}

namespace yb::vector_index {

extern MonoDelta TEST_sleep_during_flush;

}

namespace yb::pgwrapper {

YB_STRONGLY_TYPED_BOOL(AddFilter);
YB_STRONGLY_TYPED_BOOL(Backfill);

using FloatVector = std::vector<float>;
const std::string kVectorIndexName = "vi";

const unum::usearch::byte_t* VectorToBytePtr(const FloatVector& vector) {
  return pointer_cast<const unum::usearch::byte_t*>(vector.data());
}

YB_DEFINE_ENUM(VectorIndexEngine, (kUsearch)(kYbHnsw)(kHnswlib));

using PgVectorIndexTestParams = std::tuple<bool, VectorIndexEngine>;

bool IsColocated(const PgVectorIndexTestParams& params) {
  return std::get<0>(params);
}

VectorIndexEngine Engine(const PgVectorIndexTestParams& params) {
  return std::get<1>(params);
}

class PgVectorIndexTest :
    public PgMiniTestBase, public testing::WithParamInterface<PgVectorIndexTestParams> {
 protected:
  void SetUp() override {
    FLAGS_TEST_use_custom_varz = true;
    FLAGS_TEST_usearch_exact = true;
    FLAGS_vector_index_disable_compactions = false;
    switch (Engine()) {
      case VectorIndexEngine::kUsearch:
        FLAGS_vector_index_use_hnswlib = false;
        FLAGS_vector_index_use_yb_hnsw = false;
        break;
      case VectorIndexEngine::kYbHnsw:
        FLAGS_vector_index_use_hnswlib = false;
        FLAGS_vector_index_use_yb_hnsw = true;
        break;
      case VectorIndexEngine::kHnswlib:
        FLAGS_vector_index_use_hnswlib = true;
        FLAGS_vector_index_use_yb_hnsw = false;
        break;
    }
    itest::SetupQuickSplit(1_KB);

    PgMiniTestBase::SetUp();

    tablet::TEST_fail_on_seq_scan_with_vector_indexes = true;
  }

  bool IsColocated() const {
    return pgwrapper::IsColocated(GetParam());
  }

  VectorIndexEngine Engine() const {
    return pgwrapper::Engine(GetParam());
  }

  std::string DbName() {
    return IsColocated() ? "colocated_db" : "yugabyte";
  }

  Result<PGConn> Connect() const override {
    return IsColocated() ? ConnectToDB("colocated_db") : PgMiniTestBase::Connect();
  }

  Result<PGConn> MakeTable(size_t dimensions = 3) {
    auto colocated = IsColocated();
    auto conn = VERIFY_RESULT(PgMiniTestBase::Connect());
    std::string create_suffix;
    if (colocated) {
      create_suffix = " WITH (COLOCATED = 1)";
      RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE colocated_db COLOCATION = true"));
      conn = VERIFY_RESULT(Connect());
    } else if (num_tablets_) {
      create_suffix += "SPLIT INTO 1 TABLETS";
    }
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector($0))$1",
        dimensions, create_suffix));

    return conn;
  }

  Status CreateIndex(
       PGConn& conn, const std::string& index_name = kVectorIndexName,
       std::optional<vector_index::DistanceKind> distance_kind = std::nullopt) {
    return conn.ExecuteFormat(
        "CREATE INDEX $1 ON test USING ybhnsw (embedding $0) "
            "WITH (ef_construction = 256, m = 32, m0 = 128)",
        VectorOpsName(distance_kind.value_or(distance_kind_)), index_name);
  }

  Result<PGConn> MakeIndex(size_t dimensions = 3, bool table_exists = false) {
    auto conn =  VERIFY_RESULT(table_exists ? Connect() : MakeTable(dimensions));
    RETURN_NOT_OK(CreateIndex(conn));
    return conn;
  }

  Result<PGConn> MakeIndexAndFill(size_t num_rows, Backfill backfill);
  Result<PGConn> MakeIndexAndFillRandom(size_t num_rows);
  Status InsertRows(PGConn& conn, size_t start_row, size_t end_row);
  Status InsertRandomRows(PGConn& conn, size_t num_rows);

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

  void TestSimple(bool table_exists = false);
  void TestManyRows(AddFilter add_filter, Backfill backfill = Backfill::kFalse);
  void TestRestart(tablet::FlushFlags flush_flags);
  void TestMetric(const std::string& expected);
  void TestRandom();

  FloatVector RandomVector() {
    if (real_dimensions_ == 0) {
      return RandomFloatVector(dimensions_, distribution_, &rng_);
    }

    if (shuffle_vector_.empty()) {
      shuffle_vector_.resize(dimensions_);
      for (size_t i = 0; i != shuffle_vector_.size(); ++i) {
        shuffle_vector_[i] = i % real_dimensions_;
      }
      std::shuffle(shuffle_vector_.begin(), shuffle_vector_.end(), rng_);
    }
    auto real_vector = RandomFloatVector(real_dimensions_, distribution_, &rng_);
    decltype(real_vector) result(dimensions_);
    for (size_t j = 0; j != dimensions_; ++j) {
      result[j] = real_vector[shuffle_vector_[j]];
    }
    return result;
  }

  const char* VectorOpsName() const {
    using vector_index::DistanceKind;
    return VectorOpsName(distance_kind_);
  }

  static const char* VectorOpsName(vector_index::DistanceKind distance_kind) {
    using vector_index::DistanceKind;
    switch (distance_kind) {
      case DistanceKind::kL2Squared:
        return "vector_l2_ops";
      case DistanceKind::kInnerProduct:
        return "vector_ip_ops";
      case DistanceKind::kCosine:
        return "vector_cosine_ops";
    }
    FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind);
  }

  const char* VectorOp() const {
    using vector_index::DistanceKind;
    switch (distance_kind_) {
      case DistanceKind::kL2Squared:
        return "<->";
      case DistanceKind::kInnerProduct:
        return "<#>";
      case DistanceKind::kCosine:
        return "<=>";
    }
    FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind_);
  }

  unum::usearch::metric_kind_t UsearchMetricKind() const {
    using vector_index::DistanceKind;
    switch (distance_kind_) {
      case DistanceKind::kL2Squared:
        return unum::usearch::metric_kind_t::l2sq_k;
      case DistanceKind::kInnerProduct:
        return unum::usearch::metric_kind_t::ip_k;
      case DistanceKind::kCosine:
        return unum::usearch::metric_kind_t::cos_k;
    }
    FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind_);
  }

  template <class Vector>
  std::string DistanceToQuery(const Vector& vector) const {
    return Format("embedding $0 '$1'", VectorOp(), vector);
  }

  template <class Vector>
  std::string IndexQuerySuffix(const Vector& vector, size_t limit) const {
    return Format(" ORDER BY $0 LIMIT $1", DistanceToQuery(vector), limit);
  }

  Status WaitNoBackgroundInserts();

  std::vector<FloatVector> vectors_;
  std::uniform_real_distribution<> distribution_;
  std::mt19937_64 rng_{42};
  vector_index::DistanceKind distance_kind_ = vector_index::DistanceKind::kL2Squared;
  size_t dimensions_;
  size_t real_dimensions_;
  std::vector<size_t> shuffle_vector_;
  int num_tablets_ = 0;
};

uint64_t SumHistograms(const std::vector<const HdrHistogram*>& histograms) {
  uint64_t result = 0;
  for (const auto* histogram : histograms) {
    result += histogram->CurrentSum();
  }
  return result;
}

void PgVectorIndexTest::TestSimple(bool table_exists) {
  docdb::TEST_vector_index_filter_allowed = false;

  auto conn = ASSERT_RESULT(MakeIndex(3, table_exists));

  size_t num_found_peers = 0;
  auto check_tablets = [this, &num_found_peers]() -> Result<bool> {
    num_found_peers = 0;
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : peers) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      if (tablet->table_type() != TableType::PGSQL_TABLE_TYPE) {
        continue;
      }
      auto& metadata = *tablet->metadata();
      auto tables = metadata.GetAllColocatedTables();
      tablet::TableInfoPtr main_table_info;
      tablet::TableInfoPtr index_table_info;
      size_t num_indexes = 0;
      for (const auto& table_id : tables) {
        auto table_info = VERIFY_RESULT(metadata.GetTableInfo(table_id));
        LOG(INFO) << "Table: " << table_info->ToString();
        if (table_info->table_name == "test") {
          main_table_info = table_info;
        } else if (table_info->index_info) {
          ++num_indexes;
          index_table_info = table_info;
        }
      }
      if (!main_table_info) {
        continue;
      }
      ++num_found_peers;
      if (num_indexes != 1) {
        LOG(INFO) << "Wrong number of indexes " << num_indexes << " at " << tablet->LogPrefix();
        return false;
      }
      auto vector_indexes = tablet->vector_indexes().List();
      auto num_vector_indexes = vector_indexes ? vector_indexes->size() : 0;
      if (num_vector_indexes != 1) {
        LOG(INFO)
            << "Wrong number of vector indexes " << num_vector_indexes << " at "
            << tablet->LogPrefix();
        return false;
      }
      SCHECK_EQ(
        index_table_info->index_info->indexed_table_id(), main_table_info->table_id,
        IllegalState, "Wrong indexed table");
    }
    return true;
  };

  ASSERT_OK(WaitFor(check_tablets, 10s * kTimeMultiplier, "Index created on all tablets"));
  ASSERT_NE(num_found_peers, 0);

  if (!table_exists) {
    ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1.0, 0.5, 0.25]')"));
    ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[0.125, 0.375, 0.25]')"));
  }

  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test" + IndexQuerySuffix("[1.0, 0.4, 0.3]", 5)));
  ASSERT_EQ(result, "1, [1, 0.5, 0.25]; 2, [0.125, 0.375, 0.25]");

  LOG(INFO) << "Memory usage:\n" << DumpMemoryUsage();
}

Status PgVectorIndexTest::WaitNoBackgroundInserts() {
  auto cond = [this]() -> Result<bool> {
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : peers) {
      auto list = VERIFY_RESULT(peer->shared_tablet())->vector_indexes().List();
      if (!list) {
        continue;
      }
      for (const auto& index : *list) {
        if (index->TEST_HasBackgroundInserts()) {
          LOG(INFO) << "Index " << index->ToString() << " has background inserts";
          return false;
        }
      }
    }
    return true;
  };
  return WaitFor(cond, 30s * kTimeMultiplier, "Wait no background inserts");
}

TEST_P(PgVectorIndexTest, Simple) {
  TestSimple();
}

TEST_P(PgVectorIndexTest, NotApplied) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  TestSimple();
}

TEST_P(PgVectorIndexTest, Drop) {
  TestSimple();
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  TestSimple(true);
}

TEST_P(PgVectorIndexTest, DropWithFlush) {
  ANNOTATE_UNPROTECTED_WRITE(vector_index::TEST_sleep_during_flush) = 250ms * kTimeMultiplier;
  TestSimple();
  auto conn = ASSERT_RESULT(Connect());
  ThreadHolder threads;
  threads.AddThreadFunctor([this] {
    ASSERT_OK(cluster_->FlushTablets(
        tablet::FlushMode::kAsync, tablet::FlushFlags::kVectorIndexes));
  });
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
}

std::string VectorAsString(int64_t id) {
  return Format("[$0, $1, $2]", id, id * 2, id * 3);
}

std::string BuildRow(int64_t id, const std::string& value) {
  return Format("$0, $1", id, value);
}

std::string ExpectedRow(int64_t id) {
  return BuildRow(id, VectorAsString(id));
}

std::vector<std::string> ExpectedRows(size_t limit) {
  std::vector<std::string> expected;
  for (size_t i = 1; i <= limit; ++i) {
    expected.push_back(ExpectedRow(i));
  }
  return expected;
}

Status PgVectorIndexTest::InsertRows(PGConn& conn, size_t start_row, size_t end_row) {
  RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (auto i = start_row; i <= end_row; ++i) {
    RETURN_NOT_OK(conn.ExecuteFormat(
       "INSERT INTO test VALUES ($0, '$1')", i, VectorAsString(i)));
  }
  return conn.CommitTransaction();
}

Status PgVectorIndexTest::InsertRandomRows(PGConn& conn, size_t num_rows) {
  RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (size_t i = 0; i != num_rows; ++i) {
    auto vector = RandomVector();
    RETURN_NOT_OK(conn.ExecuteFormat(
       "INSERT INTO test VALUES ($0, '$1')", vectors_.size(), AsString(vector)));
    vectors_.push_back(std::move(vector));
  }
  return conn.CommitTransaction();
}

Result<PGConn> PgVectorIndexTest::MakeIndexAndFill(
    size_t num_rows, Backfill backfill = Backfill::kFalse) {
  auto conn = VERIFY_RESULT(MakeTable());
  if (backfill) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = num_rows / 5 + 1;
    RETURN_NOT_OK(InsertRows(conn, 1, num_rows));
    std::future<void> future;
    if (tablet::TEST_block_after_backfilling_first_vector_index_chunks) {
      future = std::async([this] {
        std::this_thread::sleep_for(1s);
        CHECK_OK(cluster_->mini_tablet_server(1)->Restart());
        ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_chunks)
            = false;
        std::this_thread::sleep_for(5s * kTimeMultiplier);
      });
    }
    RETURN_NOT_OK(CreateIndex(conn));
    if (future.valid()) {
      future.get();
    }
  } else {
    RETURN_NOT_OK(CreateIndex(conn));
    RETURN_NOT_OK(InsertRows(conn, 1, num_rows));
  }
  return conn;
}

Result<PGConn> PgVectorIndexTest::MakeIndexAndFillRandom(size_t num_rows) {
  auto conn = VERIFY_RESULT(MakeIndex(dimensions_));
  RETURN_NOT_OK(InsertRandomRows(conn, num_rows));
  return conn;
}

bool PgVectorIndexTest::RowsMatch(
    PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
    int64_t limit) {
  if (limit >= 0 && make_unsigned(limit) < expected.size()) {
    std::vector<std::string> new_expected(expected.begin(), expected.begin() + limit);
    return RowsMatch(conn, filter, new_expected, limit);
  }
  auto query = Format(
      "SELECT * FROM test AS t $0$1", filter,
      IndexQuerySuffix("[0.0, 0.0, 0.0]", limit < 0 ? expected.size() : make_unsigned(limit)));
  LOG_WITH_FUNC(INFO) << "   Query: " << AsString(query);
  auto result = CHECK_RESULT((conn.FetchRows<RowAsString>(query)));
  LOG_WITH_FUNC(INFO) << "  Result: " << AsString(result);
  LOG_WITH_FUNC(INFO) << "Expected: " << AsString(expected);
  bool ok = true;
  if (result.size() != expected.size()) {
    LOG_WITH_FUNC(INFO)
        << "Wrong number of results: " << result.size() << ", while " << expected.size()
        << " expected";
    ok = false;
  }
  for (size_t i = 0; i != std::min(result.size(), expected.size()); ++i) {
    if (result[i] != expected[i]) {
      LOG_WITH_FUNC(INFO)
          << "Wrong row " << i << ": " << result[i] << " instead of " << expected[i];
      ok = false;
    }
  }
  return ok;
}

void PgVectorIndexTest::VerifyRows(
    PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
    int64_t limit) {
  ASSERT_TRUE(RowsMatch(conn, filter, expected, limit));
}

void PgVectorIndexTest::VerifyRows(
    PGConn& conn, AddFilter add_filter, const std::vector<std::string>& expected, int64_t limit) {
  VerifyRows(conn, add_filter ? "WHERE id + 3 <= 5" : "", expected, limit);
}

void PgVectorIndexTest::VerifyRead(PGConn& conn, size_t limit, AddFilter add_filter) {
  VerifyRows(conn, add_filter, ExpectedRows(limit));
}

void PgVectorIndexTest::TestManyRows(AddFilter add_filter, Backfill backfill) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(2000, 64);
  const size_t query_limit = add_filter ? 1 : 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows, backfill));
  ASSERT_NO_FATALS(VerifyRead(conn, query_limit, add_filter));
}

TEST_P(PgVectorIndexTest, Split) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(500, 64);
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  // Give some time for split to happen.
  std::this_thread::sleep_for(2s * kTimeMultiplier);

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, ManyRows) {
  TestManyRows(AddFilter::kFalse);
}

TEST_P(PgVectorIndexTest, ManyRowsWithBackfill) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_writes) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_insert_tasks) = 1;
  TestManyRows(AddFilter::kFalse, Backfill::kTrue);
}

TEST_P(PgVectorIndexTest, ManyRowsWithBackfillAndRestart) {
  num_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_chunks) = true;
  TestManyRows(AddFilter::kFalse, Backfill::kTrue);
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  size_t tablet_entries = 0;
  for (const auto& peer : peers) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet) {
      continue;
    }
    auto list = tablet->vector_indexes().List();
    if (!list) {
      continue;
    }
    auto current_entries = ASSERT_RESULT((*list)[0]->TotalEntries());
    LOG(INFO) << "P " << peer->permanent_uuid() << ": Total entries: " << current_entries;
    if (tablet_entries) {
      ASSERT_EQ(tablet_entries, current_entries);
    } else {
      tablet_entries = current_entries;
    }
  }
}

TEST_P(PgVectorIndexTest, ManyRowsWithFilter) {
  TestManyRows(AddFilter::kTrue);
}

TEST_P(PgVectorIndexTest, ManyReads) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_reads) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_writes) = 1;

  constexpr size_t kNumRows = 64;
  constexpr size_t kNumReads = 16;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  TestThreadHolder threads;
  for (size_t i = 1; i <= kNumReads; ++i) {
    threads.AddThreadFunctor([this, &stop_flag = threads.stop_flag()] {
      auto conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load()) {
        auto id = RandomUniformInt<size_t>(1, kNumRows);
        auto vector = VectorAsString(id);
        auto rows = ASSERT_RESULT(conn.FetchAllAsString(
            "SELECT * FROM test" + IndexQuerySuffix(vector, 1)));
        ASSERT_EQ(rows, ExpectedRow(id));
      }
    });
  }

  threads.WaitAndStop(5s);
}

void PgVectorIndexTest::TestRestart(tablet::FlushFlags flush_flags) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndex());
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kNonLeaders);
  for (const auto& peer : peers) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (tablet->vector_indexes().TEST_HasIndexes()) {
      tablet->TEST_SleepBeforeApplyIntents(5s * kTimeMultiplier);
      break;
    }
  }
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, flush_flags));
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, Restart) {
  TestRestart(tablet::FlushFlags::kAllDbs);
}

TEST_P(PgVectorIndexTest, Bootstrap) {
  TestRestart(tablet::FlushFlags::kRegular);
}

TEST_P(PgVectorIndexTest, BootstrapFlushedIntentsDB) {
  TestRestart(tablet::FlushFlags::kIntents);
}

TEST_P(PgVectorIndexTest, BootstrapFlushedVectorIndexes) {
  TestRestart(tablet::FlushFlags::kVectorIndexes);
}

TEST_P(PgVectorIndexTest, DeleteAndUpdate) {
  constexpr size_t kNumRows = 64;
  const std::string kDistantVector = "[100, 500, 9000]";
  const std::string kCloseVector = "[0.125, 0.25, 0.375]";

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 2", kDistantVector));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 10", kCloseVector));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 20"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES(1000, NULL)"));

  std::vector<std::string> expected = {
    BuildRow(10, kCloseVector),
    ExpectedRow(3),
    ExpectedRow(4),
    ExpectedRow(5),
    ExpectedRow(6),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, expected));

  std::vector<std::string> expected_filtered = {
    BuildRow(2, kDistantVector),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kTrue, expected_filtered));
}

TEST_P(PgVectorIndexTest, RemoteBootstrap) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto* mts = cluster_->mini_tablet_server(2);
  mts->Shutdown();
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  ASSERT_OK(cluster_->FlushTablets());
  for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_id)) {
    ASSERT_OK(peer->log()->AllocateSegmentAndRollOver());
  }

  // Need at least one committed entry after segment to GC this segment.
  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));

  ASSERT_OK(cluster_->CleanTabletLogs());

  ASSERT_OK(mts->Start());
  ASSERT_OK(WaitFor([this, table_id, mts]() -> Result<bool> {
    auto peers = ListTableActiveTabletPeers(cluster_.get(), table_id);
    tablet::TabletPeerPtr leader;
    for (const auto& peer : peers) {
      bool is_leader =
          VERIFY_RESULT(peer->GetConsensus())->GetLeaderStatus() ==
              consensus::LeaderStatus::LEADER_AND_READY;
      if (peer->permanent_uuid() != mts->fs_manager().uuid()) {
        if (is_leader) {
          leader = peer;
        }
        continue;
      }
      if (is_leader) {
        return true;
      }
    }
    if (leader) {
      LOG(INFO) << "Step down: " << leader->permanent_uuid();
      WARN_NOT_OK(StepDown(leader, mts->fs_manager().uuid(), ForceStepDown::kTrue),
                  "StepDown failed");
    }
    return false;
  }, 60s * kTimeMultiplier, "Wait desired leader"));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, SnapshotSchedule) {
  constexpr size_t kNumRows = 128;
  constexpr size_t kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  ASSERT_OK(conn.Execute("DELETE FROM test"));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, {}, 10));

  auto snapshot_id = ASSERT_RESULT(snapshot_util.PickSuitableSnapshot(
      schedule_id, hybrid_time));
  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id, hybrid_time));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

void PgVectorIndexTest::TestRandom() {
  constexpr size_t kLimit = 10;
  constexpr size_t kDimensionMultiplier = RegularBuildVsDebugVsSanitizers(96, 4, 4);
  constexpr size_t kNumRows = RegularBuildVsDebugVsSanitizers(1000, 1000, 100);
  constexpr int kNumIterations = RegularBuildVsDebugVsSanitizers(2000, 20, 10);

  real_dimensions_ = 16;
  dimensions_ = real_dimensions_ * kDimensionMultiplier;

  unum::usearch::metric_punned_t metric(
      dimensions_, UsearchMetricKind(), unum::usearch::scalar_kind_t::f32_k);

  std::vector<const HdrHistogram*> read_histograms;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    read_histograms.push_back(
      cluster_->mini_tablet_server(i)->metric_entity().FindOrCreateMetric<Histogram>(
          &METRIC_handler_latency_yb_tserver_TabletServerService_Read)->underlying());
  }

  auto conn = ASSERT_RESULT(MakeIndexAndFillRandom(kNumRows));

  size_t sum_missing = 0;
  std::vector<size_t> counts;

  uint64_t total_read_time = 0;

  for (int i = 0; i != kNumIterations;) {
    auto query_vector = RandomVector();

    uint64_t read_sum_before = SumHistograms(read_histograms);

    auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(
        "SELECT id FROM test" + IndexQuerySuffix(query_vector, kLimit)));

    total_read_time += SumHistograms(read_histograms) - read_sum_before;
    std::vector<int64_t> expected(vectors_.size());
    std::generate(expected.begin(), expected.end(), [n{0LL}]() mutable { return n++; });
    std::sort(
        expected.begin(), expected.end(),
        [&metric, &query_vector, &vectors = vectors_](size_t li, size_t ri) {
      const auto& lhs = vectors[li];
      const auto& rhs = vectors[ri];
      return metric(VectorToBytePtr(query_vector), VectorToBytePtr(lhs)) <
             metric(VectorToBytePtr(query_vector), VectorToBytePtr(rhs));
    });
    size_t ep = 0;
    for (int64_t id : rows) {
      while (ep < expected.size() && id != expected[ep]) {
        ++ep;
      }
      if (ep == expected.size()) {
        break;
      }
      ++ep;
    }
    size_t missing = ep - kLimit;
    if (missing > counts.size()) {
      LOG(INFO)
          << "New max: " << missing << ", fetched: " << AsString(rows) << ", expected: "
          << AsString(boost::make_iterator_range(
                 expected.begin(), expected.begin() + kLimit + missing));
    }
    if (ep == expected.size()) {
      // It means that vectors were fetched in a different order.
      // Since it is infrequent case we could just retry this iteration.
      continue;
    }
    counts.resize(std::max(counts.size(), missing + 1));
    ++counts[missing];
    sum_missing += missing;
    if (++i >= kNumIterations && sum_missing * 50 <= kLimit * kNumIterations) {
      break;
    }
  }
  LOG(INFO)
      << "Counts: " << AsString(counts)
      << ", recall: " << 1.0 - sum_missing * 1.0 / (kLimit * kNumIterations)
      << ", average read time: " << (total_read_time / kNumIterations) << " us";
}

TEST_P(PgVectorIndexTest, Random) {
  TestRandom();
}

TEST_P(PgVectorIndexTest, RandomInnerProduct) {
  distance_kind_ = vector_index::DistanceKind::kInnerProduct;
  TestRandom();
}

TEST_P(PgVectorIndexTest, RandomCosine) {
  distance_kind_ = vector_index::DistanceKind::kCosine;
  TestRandom();
}

void PgVectorIndexTest::TestMetric(const std::string& expected) {
  auto kDimensions = 2;
  auto conn = ASSERT_RESULT(MakeIndex(kDimensions));

  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[0.0, 5.0]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[5.0, 5.0]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (3, '[9.0, 0.0]')"));

  const auto query_vector = "[2.0, 1.0]";
  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT id FROM test" + IndexQuerySuffix(query_vector, 5)));
  ASSERT_EQ(result, expected);
}

TEST_P(PgVectorIndexTest, L2) {
  distance_kind_ = vector_index::DistanceKind::kL2Squared;
  TestMetric("1; 2; 3");
}

TEST_P(PgVectorIndexTest, InnerProduct) {
  distance_kind_ = vector_index::DistanceKind::kInnerProduct;
  TestMetric("3; 2; 1");
}

TEST_P(PgVectorIndexTest, Cosine) {
  distance_kind_ = vector_index::DistanceKind::kCosine;
  TestMetric("2; 3; 1");
}

TEST_P(PgVectorIndexTest, EfSearch) {
  FLAGS_vector_index_no_deletions_skip_filter_check = false;

  constexpr size_t kNumRows = 1000;
  constexpr int kIterations = 10;
  constexpr int kSmallEf = 1;
  constexpr int kBigEf = 1000;

  FLAGS_TEST_usearch_exact = false;

  num_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, 1, AddFilter::kFalse));
  ASSERT_OK(WaitNoBackgroundInserts());

  for (int i = 0; i != kIterations; ++i) {
    for (int ef : {kSmallEf, kBigEf}) {
      ASSERT_OK(conn.ExecuteFormat("SET ybhnsw.ef_search = $0", ef));
      ANNOTATE_UNPROTECTED_WRITE(docdb::TEST_vector_index_max_checked_entries) = ef * 100;
      auto valid = RowsMatch(conn, "", ExpectedRows(1));
      ASSERT_TRUE(valid || ef == kSmallEf);
    }
  }
}

TEST_P(PgVectorIndexTest, Paging) {
  constexpr int kNumRows = 64;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("CREATE TABLE fk (id INT PRIMARY KEY)"));

  auto keys = {42, 3, 21, 10, 30};

  std::vector<int> available_keys;
  for (auto key : keys) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO fk VALUES ($0)", key));
    available_keys.push_back(key);
    std::ranges::sort(available_keys);
    std::vector<std::string> expected;
    for (auto k : available_keys) {
      expected.push_back(Format("$0, $1, $0", k, VectorAsString(k)));
    }
    for (int limit = 1; limit <= kNumRows; limit *= 2) {
      SCOPED_TRACE(Format("limit: $0", limit));
      ASSERT_NO_FATALS(VerifyRows(
          conn, "INNER JOIN fk as f ON t.id = f.id", expected, limit));
    }
  }
}

Status CreateEchoFunction(PGConn& conn) {
  return conn.Execute(R"EOF(
      CREATE OR REPLACE FUNCTION echo_int(i INT)
      RETURNS INT
      LANGUAGE plpgsql
      VOLATILE
      AS $$
      BEGIN
        RETURN i;
      END;
      $$;
  )EOF");
}

TEST_P(PgVectorIndexTest, PagingWithFunction) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateEchoFunction(conn));
  ASSERT_OK(conn.Execute(
      "INSERT INTO test SELECT i, vector('[1.0, 1.0, 1.' || lpad(i::text, 5, '0') || ']') "
      "FROM generate_series (1, 300) AS i"));
  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test WHERE id >= echo_int(0) "
      "ORDER BY embedding <-> '[1.0, 1.0, 1.0]'::vector LIMIT 100;"));
  LOG(INFO) << "Result: " << result;
}

TEST_P(PgVectorIndexTest, Options) {
  auto conn = ASSERT_RESULT(MakeTable());
  std::unordered_map<TabletId, std::unordered_set<TableId>> checked_indexes;
  std::vector<std::string> option_names = {"m", "m0", "ef_construction"};
  // We need unique values for used params. Since m and ef has different allowed intervals,
  // use different counters for them. counters[0] for m/m0 and counters[1] for ef_construction.
  std::array<size_t, 2> counters = {32, 64};
  for (int i = 0; i != 1 << option_names.size(); ++i) {
    std::string expected_options;
    {
      std::string options;
      size_t prev_value = 0;
      for (size_t j = 0; j != option_names.size(); ++j) {
        if (!expected_options.empty()) {
          expected_options += " ";
        }
        size_t value;
        if ((i & (1 << j))) {
          value = ++counters[j >= 2];
          if (!options.empty()) {
            options += ", ";
          }
          options += Format("$0 = $1", option_names[j], value);
        } else {
          switch (j) {
            case 0:
              value = 32; // Default value for m
              break;
            case 1:
              // When not specified m0 uses value of m.
              value = prev_value;
              break;
            case 2:
              value = 200; // Default value for ef
              break;
            default:
              ASSERT_LT(j, 4U) << "Unexpected number of options";
              value = 0;
              break;
          }
        }
        expected_options += Format("$0: $1", option_names[j], value);
        prev_value = value;
      }
      switch (Engine()) {
        case VectorIndexEngine::kUsearch:
          break;
        case VectorIndexEngine::kYbHnsw:
          expected_options += " backend: YB_HNSW";
          break;
        case VectorIndexEngine::kHnswlib:
          expected_options += " backend: HNSWLIB";
          break;
      }
      if (!options.empty()) {
        options = " WITH (" + options + ")";
      }
      auto query = "CREATE INDEX ON test USING ybhnsw (embedding vector_l2_ops)" + options;
      LOG(INFO) << "Query: " << query;
      ASSERT_OK(conn.Execute(query));
    }
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    for (const auto& peer : peers) {
      auto tablet = peer->shared_tablet_maybe_null();
      auto vector_indexes = tablet->vector_indexes().List();
      if (!vector_indexes) {
        continue;
      }
      auto& tablet_indexes = checked_indexes[peer->tablet_id()];
      size_t num_new_indexes = 0;
      for (const auto& vector_index : *vector_indexes) {
        if (!tablet_indexes.insert(vector_index->table_id()).second) {
          continue;
        }
        ++num_new_indexes;
        auto doc_read_context = ASSERT_RESULT(
            tablet->metadata()->GetTableInfo(vector_index->table_id()))->doc_read_context;
        const auto& hnsw_options = doc_read_context->vector_idx_options->hnsw();
        LOG(INFO)
            << "Vector index: " << AsString(vector_index) << ", options: "
            << AsString(hnsw_options);
        ASSERT_EQ(AsString(hnsw_options), expected_options);
      }
      ASSERT_EQ(num_new_indexes, 1);
    }
  }
}

TEST_P(PgVectorIndexTest, Backup) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  ASSERT_OK(cluster_->StartYbControllerServers());

  TestManyRows(AddFilter::kFalse);

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateIndex(conn, "vi_cos", vector_index::DistanceKind::kCosine));

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, "ysql." + DbName()));

  // Restore the backup.
  const std::string kRestoreDb = "restored_db";
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, "ysql." + kRestoreDb));

  auto restore_conn = ASSERT_RESULT(ConnectToDB(kRestoreDb));
  VerifyRead(restore_conn, 10, AddFilter::kFalse);
}

std::string TestParamToString(const testing::TestParamInfo<PgVectorIndexTestParams>& param_info) {
  auto engine = Engine(param_info.param);
  return Format(
      "$0$1",
      IsColocated(param_info.param) ? "Colocated" : "Distributed",
      engine == VectorIndexEngine::kUsearch ? "" : ToString(engine).substr(1));
}

INSTANTIATE_TEST_SUITE_P(
    , PgVectorIndexTest,
    testing::Combine(testing::Bool(), testing::ValuesIn(kVectorIndexEngineArray)),
    TestParamToString);

}  // namespace yb::pgwrapper
