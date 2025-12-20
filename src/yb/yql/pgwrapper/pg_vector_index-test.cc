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

#include "yb/docdb/docdb_util.h"
#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/sst_dump_tool.h"

#include "yb/tablet/kv_formatter.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/path_util.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/usearch_include_wrapper_internal.h"
#include "yb/vector_index/distance.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_skip_process_apply);
DECLARE_bool(TEST_use_custom_varz);
DECLARE_bool(TEST_usearch_exact);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(vector_index_no_deletions_skip_filter_check);
DECLARE_bool(vector_index_use_hnswlib);
DECLARE_bool(vector_index_use_yb_hnsw);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_auto_analyze_infra);
DECLARE_bool(ysql_use_packed_row_v2);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_uint32(vector_index_concurrent_reads);
DECLARE_uint32(vector_index_concurrent_writes);
DECLARE_uint32(vector_index_num_compactions_limit);
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
YB_DEFINE_ENUM(PackingMode, (kNone)(kV1)(kV2));

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

  void SetUp() override {
    FLAGS_TEST_use_custom_varz = true;
    FLAGS_TEST_usearch_exact = true;
    FLAGS_vector_index_enable_compactions = true;
    FLAGS_vector_index_num_compactions_limit = 0;
    auto packing_mode = GetPackingMode();
    FLAGS_ysql_enable_packed_row = packing_mode != PackingMode::kNone;
    FLAGS_ysql_use_packed_row_v2 = packing_mode == PackingMode::kV2;
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

    // Make sure compaction has predictable trigger threshold.
    FLAGS_rocksdb_level0_file_num_compaction_trigger = GetFileNumCompactionTrigger();

    // Disable auto analyze in this test suite because auto analyze runs
    // analyze which can violate the check used in this test suite:
    // !TEST_fail_on_seq_scan_with_vector_indexes || pgsql_read_request.has_ybctid_column_value()
    FLAGS_ysql_enable_auto_analyze = false;
    // (Auto-Analyze #28666)
    FLAGS_ysql_enable_auto_analyze_infra = false;
    itest::SetupQuickSplit(1_KB);

    PgMiniTestBase::SetUp();

    tablet::TEST_fail_on_seq_scan_with_vector_indexes = true;
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
    } else if (num_pre_split_tablets_) {
      create_suffix += Format("SPLIT INTO $0 TABLETS", num_pre_split_tablets_);
    }
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector($0))$1",
        dimensions, create_suffix));

    dimensions_ = dimensions;
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

  Result<PGConn> MakeIndexAndFill(
      size_t num_rows, Backfill backfill = Backfill::kFalse, bool keep_vectors = false);
  Result<PGConn> MakeIndexAndFillRandom(size_t num_rows);
  Status InsertRows(PGConn& conn, size_t start_row, size_t end_row, bool keep_vectors = false);
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

  // Returns number of vectors actually returned by the search.
  template <typename IdxExtractor>
  Result<size_t> FetchAndVerifyOrder(
      PGConn& conn, const FloatVector& query_vector, size_t limit, IdxExtractor&& extractor);

  Result<size_t> FetchAndVerifyOrder(PGConn& conn, const FloatVector& query_vector, size_t limit) {
    return FetchAndVerifyOrder(
        conn, query_vector, limit, [](auto key) -> Result<size_t> { return key; });
  }

  FloatVector Vector(int64_t id) {
    CHECK_GT(dimensions_, 0);
    FloatVector vector(dimensions_);
    std::ranges::generate(vector, [id, n{1LL}]() mutable { return id * n++; });
    return vector;
  }

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

  Status RunSstDump(tablet::KVFormatter& formatter, const std::string& db_path) {
    SCHECK(!db_path.empty(), InvalidArgument, "");

    std::vector<std::string> input_args = {
      "./sst_dump", Format("--file=$0", db_path),
      "--output_format=decoded_regulardb", "--command=scan",
    };

    std::vector<char*> args;
    for (auto& arg : input_args) {
      args.push_back(arg.data());
    }

    rocksdb::SSTDumpTool tool(&formatter);
    testing::internal::CaptureStdout();
    auto ret = tool.Run(narrow_cast<int>(args.size()), args.data());
    testing::internal::GetCapturedStdout();
    return !ret ? Status::OK() : STATUS(RuntimeError, Format("sst_dump failed with $0", ret));
  }

  Status WaitNoBackgroundInserts();

  std::vector<FloatVector> vectors_;
  std::uniform_real_distribution<> distribution_;
  std::mt19937_64 rng_{42};
  vector_index::DistanceKind distance_kind_ = vector_index::DistanceKind::kL2Squared;
  size_t dimensions_ = 0;
  size_t real_dimensions_ = 0;
  std::vector<size_t> shuffle_vector_;
  size_t num_pre_split_tablets_ = 0;
};

Status PgVectorIndexTestBase::WaitNoBackgroundInserts() {
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

Status PgVectorIndexTestBase::InsertRows(
    PGConn& conn, size_t start_row, size_t end_row, bool keep_vectors) {
  SCHECK_GE(end_row, start_row, InvalidArgument, "");

  if (keep_vectors) {
    vectors_.reserve(vectors_.capacity() + end_row - start_row + 1);
  }

  RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (auto i = start_row; i <= end_row; ++i) {
    auto vector = Vector(i);
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO test VALUES ($0, '$1')", i, AsString(vector)));
    VLOG_WITH_FUNC(2) << "Inserted: " << AsString(vector);
    if (keep_vectors) {
      vectors_.push_back(std::move(vector));
    }
  }
  return conn.CommitTransaction();
}

Status PgVectorIndexTestBase::InsertRandomRows(PGConn& conn, size_t num_rows) {
  RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (size_t i = 0; i != num_rows; ++i) {
    auto vector = RandomVector();
    RETURN_NOT_OK(conn.ExecuteFormat(
       "INSERT INTO test VALUES ($0, '$1')", vectors_.size(), AsString(vector)));
    vectors_.push_back(std::move(vector));
  }
  return conn.CommitTransaction();
}

Result<PGConn> PgVectorIndexTestBase::MakeIndexAndFill(
    size_t num_rows, Backfill backfill, bool keep_vectors) {
  auto conn = VERIFY_RESULT(MakeTable());
  if (backfill) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = num_rows / 5 + 1;
    RETURN_NOT_OK(InsertRows(conn, 1, num_rows, keep_vectors));
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
    RETURN_NOT_OK(InsertRows(conn, 1, num_rows, keep_vectors));
  }
  return conn;
}

Result<PGConn> PgVectorIndexTestBase::MakeIndexAndFillRandom(size_t num_rows) {
  auto conn = VERIFY_RESULT(MakeIndex(dimensions_));
  RETURN_NOT_OK(InsertRandomRows(conn, num_rows));
  return conn;
}

bool PgVectorIndexTestBase::RowsMatch(
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

void PgVectorIndexTestBase::VerifyRows(
    PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
    int64_t limit) {
  ASSERT_TRUE(RowsMatch(conn, filter, expected, limit));
}

void PgVectorIndexTestBase::VerifyRows(
    PGConn& conn, AddFilter add_filter, const std::vector<std::string>& expected, int64_t limit) {
  VerifyRows(conn, add_filter ? "WHERE id + 3 <= 5" : "", expected, limit);
}

void PgVectorIndexTestBase::VerifyRead(PGConn& conn, size_t limit, AddFilter add_filter) {
  VerifyRows(conn, add_filter, ExpectedRows(limit));
}

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
    std::optional<bool> is_colocated, VectorIndexEngine engine, PackingMode packing_mode) {
  return Format(
      "$0$1$2",
      !is_colocated.has_value() ? "" : *is_colocated ? "Colocated" : "Distributed",
      engine == VectorIndexEngine::kUsearch ? "" : ToString(engine).substr(1),
      packing_mode == PackingMode::kNone ? "" : "Packing" + ToString(packing_mode).substr(1));
}

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

class PgVectorIndexTest : public PgVectorIndexTestParamsDecorator<PgVectorIndexTestBase> {
 protected:
  void TestSimple(bool table_exists = false);
  void TestManyRows(AddFilter add_filter, Backfill backfill = Backfill::kFalse);
  void TestRestart(tablet::FlushFlags flush_flags);
  void TestMetric(const std::string& expected);
  void TestRandom();
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexTest);

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

  DumpMemoryUsage();
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

void PgVectorIndexTest::TestManyRows(AddFilter add_filter, Backfill backfill) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(2000, 64);

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows, backfill));
  ASSERT_NO_FATALS(VerifyRows(conn, add_filter, ExpectedRows(add_filter ? 2 : 5), /* limit= */ 5));
  if (add_filter) {
    ASSERT_NO_FATALS(VerifyRead(conn, /* limit= */ 1, add_filter));
  }
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
  num_pre_split_tablets_ = 1;
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
        auto vector = Vector(id);
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

uint64_t SumHistograms(const std::vector<const HdrHistogram*>& histograms) {
  uint64_t result = 0;
  for (const auto* histogram : histograms) {
    result += histogram->CurrentSum();
  }
  return result;
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

  num_pre_split_tablets_ = 1;
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
      expected.push_back(Format("$0, $1, $0", k, AsString(Vector(k))));
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
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    std::unordered_map<TabletId, size_t> checked_tablets;
    for (const auto& peer : peers) {
      auto tablet = peer->shared_tablet_maybe_null();
      auto vector_indexes = tablet->vector_indexes().List();
      if (!vector_indexes) {
        continue;
      }
      if (checked_tablets[peer->tablet_id()] != 0) {
        continue;
      }
      LOG(INFO) << "Vector indexes: " << AsString(vector_indexes);
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
      checked_tablets[peer->tablet_id()] = num_new_indexes;
    }
    for (const auto& [tablet_id, num_new_indexes] : checked_tablets) {
      ASSERT_EQ(num_new_indexes, 1) << "Wrong number of new indexes for: " << tablet_id;
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
      auto engine = Engine(param_info.param);
      auto packing_mode = GetPackingMode(param_info.param);
      if (engine == VectorIndexEngine::kUsearch && packing_mode == PackingMode::kNone) {
        return "None";
      }
      return ParamsToString(std::nullopt, engine, packing_mode);
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
    FLAGS_cleanup_split_tablets_interval_sec = GetCleanupSplitTabletsIntervalSec();
    Base::SetUp();
  }

  virtual int GetCleanupSplitTabletsIntervalSec() const {
    return 1;
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgDistributedVectorIndexTest);

// Use `BaseTable` test name prefix to identify the tests where indexable table split happens
// before the vector index creation.
TEST_P(PgDistributedVectorIndexTest, BaseTableManualSplitSimple) {
  constexpr size_t kNumRows = 20;

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
    ASSERT_ONLY_NOTNULL(indexes.get());
    ASSERT_FALSE(indexes->empty());
    for (const auto& vi : *indexes) {
      const auto vi_dir = meta->vector_index_dir(vi->options());
      ASSERT_TRUE(env->DirExists(vi_dir));
      const auto files = AsString(ASSERT_RESULT(path_utils::GetVectorIndexFiles(*env, vi_dir)));
      const auto expected_files = Format(
          "[0.meta, vectorindex_1$0]", docdb::GetVectorIndexChunkFileExtension(vi->options()));
      ASSERT_STR_EQ(files, expected_files);
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

////////////////////////////////////////////////////////
// PgVectorIndexSingleServerTest
////////////////////////////////////////////////////////

class PgVectorIndexSingleServerTest
    : public PgVectorIndexTestParamsDecorator<PgVectorIndexSingleServerTestBase> {
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexSingleServerTest);

// Expected Key -> Value format:
// 1) MetaKey(VectorId(uuid), [HT{ ... }]) -> DocKey(...)
// 2) MetaKey(VectorId(uuid), [HT{ ... }]) -> DEL
// Value contains only unsigned integer.
class TestKVFormatter : public tablet::KVFormatter {
  const std::string kKVDelimiter = " -> ";

 public:
  std::string Format(
      const Slice& key, const Slice& value, docdb::StorageDbType type) const override {
    auto result = tablet::KVFormatter::Format(key, value, type);
    if (!key.starts_with(dockv::KeyEntryTypeAsChar::kVectorIndexMetadata)) {
      return result;
    }

    // Parse result into segments and collect entries by HT order.
    static const std::string kMetaPrefix = "MetaKey(VectorId(";
    CHECK(result.starts_with(kMetaPrefix));

    // 1. Extract VectorId substring.
    auto id_end = result.find(")", kMetaPrefix.length());
    CHECK_NE(id_end, std::string::npos);
    auto id = result.substr(kMetaPrefix.length(), id_end - kMetaPrefix.length());

    // 2. Extract HT substing.
    static const std::string kHTPrefix = "HT{";
    auto ht_start = result.find(kHTPrefix, id_end + 1);
    CHECK_NE(ht_start, std::string::npos);
    ht_start += kHTPrefix.length();
    CHECK_LT(ht_start, result.size());
    auto ht_end = result.find("}", ht_start + 1);
    CHECK_NE(ht_end, std::string::npos);
    auto ht = result.substr(ht_start, ht_end - ht_start);

    // 3. Extract Ybctid or DEL.
    auto delim_pos = result.find(kKVDelimiter, ht_end + 1);
    CHECK_NE(delim_pos, std::string::npos);
    auto ybctid = result.substr(delim_pos + kKVDelimiter.length());

    // 4. Keep inserted data.
    entries_.emplace_back(
        Entry{ .vector_id = std::move(id), .ht = std::move(ht), .ybctid = std::move(ybctid) });

    return result;
  }

  std::string ExtractIdx(const std::string& ybctid) const {
    // Expected formats of ybctid: "DocKey([], [1])" or "DocKey(0xeda9, [1], [])".
    static const std::string kDocKeyPrefix = "DocKey(";
    static const std::string kIdxDigits = "0123456789";

    if (ybctid.rfind(kDocKeyPrefix, 0) != 0) {
        return {};
    }

    // Find the first '[' to skip hash part.
    auto start = ybctid.find('[', kDocKeyPrefix.length() - 1);
    if (start == std::string::npos) {
      return {};
    }

    // Find the first digit after '['.
    start = ybctid.find_first_of(kIdxDigits, start);
    if (start == std::string::npos) {
      return {};
    }

    // Find where digits stop.
    auto end = ybctid.find_first_not_of(kIdxDigits, start);
    return ybctid.substr(start, end - start);
  }

  std::string FormatYbctid(const std::string& ybctid) const {
    auto idx = ExtractIdx(ybctid);
    CHECK(!idx.empty());
    return yb::Format("ybctid_$0", idx);
  }

  std::string FormatVectorsMeta() const {
    // 1. Sort all entries to have a consistent ordered.
    std::ranges::sort(entries_, Entry::LessByHtAndYbctid);

    // 2. Build vector id to vector label mapping, collecting all unique vector ids keeping
    //    the order for a particular ybctid.
    static const std::string kTombstone = "DEL";
    for (const auto& entry : entries_) {
      if (entry.ybctid == kTombstone) {
        continue;
      }

      auto& vectors = ybctid_vectors_[entry.ybctid];
      if (!vectors.insert(entry.vector_id).second) {
        continue;
      }

      vector_labels_.insert({
        entry.vector_id,
        yb::Format("$0_vector_$1", FormatYbctid(entry.ybctid), vectors.size())
      });
    }

    // 3. Build output excluding HT.
    std::stringstream ss;
    for (const auto& entry : entries_) {
      ss << vector_labels_.at(entry.vector_id);
      ss << kKVDelimiter;
      ss << (entry.ybctid == kTombstone ? entry.ybctid : FormatYbctid(entry.ybctid));
      ss << std::endl;
    }
    return ss.str();
  }

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

    static bool LessByHtAndYbctid(const Entry& a, const Entry& b) {
      if (&a == &b) {
        return false; // The same entry.
      }

      if (a.ht == b.ht) {
        // Sanity check: some entries may have same HT but vector_id and ybctid should be different.
        CHECK_NE(a.vector_id, b.vector_id) << "a: " << a.ToString() << ", b: " << b.ToString();
        CHECK_NE(a.ybctid, b.ybctid) << "a: " << a.ToString() << ", b: " << b.ToString();

        return a.ybctid < b.ybctid;
      }

      return a.ht < b.ht;
    }
  };

  // All vector index reverse mapping entries.
  mutable std::vector<Entry> entries_;

  // Mapping between vector id and vector label.
  mutable std::unordered_map<std::string, std::string> vector_labels_;

  // Collection of all vectors per ybctid.
  mutable std::unordered_map<std::string, std::unordered_set<std::string>> ybctid_vectors_;
};

TEST_P(PgVectorIndexSingleServerTest, ReverseMappingCleanup) {
  // Set number of files for background compaction explicitly.
  constexpr auto kRetentionIntervalSec = 4;
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_timestamp_history_retention_interval_sec) = kRetentionIntervalSec;

  auto conn = ASSERT_RESULT(MakeIndex());

  // Get tablet and corresponding rocksdb dir.
  auto table_peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(table_peers.size(), 1);
  auto tablet = ASSERT_RESULT(table_peers.front()->shared_tablet());
  const auto rocksdb_dir = tablet->metadata()->rocksdb_dir();
  auto* db = tablet->regular_db();
  auto* db_impl = down_cast<rocksdb::DBImpl*>(db);

  // Setup helpers.
  auto flush_tablet_and_wait = [&tablet, db_impl, cluster = cluster_.get()](
      const std::string& description) -> Status {
    RETURN_NOT_OK(WaitForAllIntentsApplied(cluster, 10s));
    RETURN_NOT_OK(tablet->Flush(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs));

    // Wait for the files are really being flushed.
    SleepFor(MonoDelta::FromMilliseconds(200));
    return LoggedWaitFor([db_impl]() -> Result<bool> {
      return db_impl->TEST_NumRunningFlushes() == 0;
    }, MonoDelta::FromSeconds(4 * kRetentionIntervalSec), description);
  };

  auto compact_tablet = [&tablet] {
    return tablet->ForceManualRocksDBCompact(docdb::SkipFlush::kTrue);
  };

  auto wait_for_compaction_done = [db_impl](const std::string& description) -> Status {
    return LoggedWaitFor([db_impl]() -> Result<bool> {
      return db_impl->TEST_NumBackgroundCompactionsScheduled() == 0 &&
             db_impl->TEST_NumTotalRunningCompactions() == 0;
      }, MonoDelta::FromSeconds(4 * kRetentionIntervalSec), description);
  };

  TestKVFormatter formatter;
  auto run_sst_dump = [this, db, &formatter] -> Status {
    formatter.Clear();
    for (const auto& live_file : db->GetLiveFilesMetaData()) {
      RETURN_NOT_OK(RunSstDump(formatter, live_file.BaseFilePath()));
    }
    return Status::OK();
  };

  // Initial insert.
  ASSERT_OK(InsertRows(conn, /* start_row = */ 1, /* end_row = */ 5));
  ASSERT_OK(flush_tablet_and_wait("Initial flush"));

  // Make some changes to a next SST file.
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 4"));
  ASSERT_OK(flush_tablet_and_wait("Flush for inital updates"));

  // Wait less than retention period and make sure no tombstoned reverse mapping records deleted.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec / 4.0));
  ASSERT_OK(compact_tablet());
  ASSERT_OK(wait_for_compaction_done("First compaction"));
  ASSERT_EQ(1, db->GetLiveFilesMetaData().size());

  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_2_vector_1 -> DEL
          ybctid_4_vector_1 -> DEL
          ybctid_4_vector_2 -> ybctid_4
      )#",
      formatter.FormatVectorsMeta());

  // Wait enough time to make sure tombstoned records are deleted during full compactions if
  // they are outside retention period.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  ASSERT_OK(compact_tablet());
  ASSERT_OK(wait_for_compaction_done("Second compaction"));
  ASSERT_EQ(1, db->GetLiveFilesMetaData().size());
  const size_t oldest_file = db->GetLiveFilesMetaData().front().name_id;
  LOG(INFO) << "Oldest file number [" << oldest_file << "] to be excluded from compaction";

  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
      )#",
      formatter.FormatVectorsMeta());

  // Let's produce more files.
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[11, 21, 31]' WHERE id = 4"));
  ASSERT_OK(InsertRows(conn, /* start_row = */ 6, /* end_row = */ 7));
  ASSERT_OK(flush_tablet_and_wait("Flush after update"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 3"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 6"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[12, 22, 32]' WHERE id = 4"));
  ASSERT_OK(InsertRows(conn, /* start_row = */ 8, /* end_row = */ 8));
  ASSERT_OK(flush_tablet_and_wait("Flush after deletes"));

  // Keep the number of files to understand how many additional files should be produced to let
  // a background compaction automatically happen. It is expected to have 3 files at this point,
  // one - after comaction plus two - after two flushes, but in some cases an additional flush of
  // an unknown nature may happen, producing one more file. That does not break the logic, but it's
  // good to make additional research to understand where that flush comes from.
  constexpr size_t kNumFilesExpected = 3;
  const size_t num_files = db->GetLiveFilesMetaData().size();
  if (num_files == kNumFilesExpected) {
    LOG(INFO) << "Current number of files: " << num_files;
  } else {
    LOG(WARNING) << "Current number of files: " << num_files << ", expected: " << kNumFilesExpected;
    ASSERT_GE(num_files, kNumFilesExpected);
  }

  // Make sure everything expected is seen after the flush.
  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
          ybctid_4_vector_2 -> DEL
          ybctid_4_vector_3 -> ybctid_4
          ybctid_6_vector_1 -> ybctid_6
          ybctid_7_vector_1 -> ybctid_7
          ybctid_3_vector_1 -> DEL
          ybctid_6_vector_1 -> DEL
          ybctid_4_vector_3 -> DEL
          ybctid_4_vector_4 -> ybctid_4
          ybctid_8_vector_1 -> ybctid_8
      )#",
      formatter.FormatVectorsMeta());

  // Make deletes pass the retention period.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec + 1));
  LOG(INFO) << "Passed enough time to make tombstones be outside retention period";

  // Update exclude SST file functor to exclude exactly the oldest file, to simulate
  // background compaction for N-1 latest files.
  auto excluder = std::make_shared<rocksdb::CompactionFileExcluder>(
      [oldest_file](const rocksdb::FileMetaData& file){
        bool need_exclude = oldest_file == file.fd.GetNumber();
        LOG(INFO) << (need_exclude ? "Excluding" : "Keeping") << " file: " << file.fd.ToString();
        return need_exclude;
      });
  db_impl->TEST_SetExcludeFromCompaction(excluder);

  // Need to add more files to trigger background compaction, adding 1 for the excluded file.
  const size_t need_files = GetFileNumCompactionTrigger() - num_files + 1;
  LOG(INFO) << "Need " << need_files << " files to trigger background compaction";
  std::stringstream expected_tail;
  for (size_t i = 0; i < need_files; ++i) {
    const size_t key_idx = 9 + i;
    ASSERT_OK(InsertRows(conn, /* start_row = */ key_idx, /* end_row = */ key_idx));
    ASSERT_OK(flush_tablet_and_wait(Format("Flush $0 done", i)));
    expected_tail << Format("ybctid_$0_vector_1 -> ybctid_$0\n", key_idx);
    LOG(INFO) << "Flushed data to " << i << " out of " << need_files << " files";
  }

  // Give some time for background compaction to start.
  SleepFor(MonoDelta::FromSeconds(1));
  LOG(INFO) << "Background compaction should have been started";
  ASSERT_OK(wait_for_compaction_done("Final compaction"));
  ASSERT_EQ(2, db->GetLiveFilesMetaData().size());

  // Check the final state in SST files.
  ASSERT_OK(run_sst_dump());
  auto output = formatter.FormatVectorsMeta();
  LOG(INFO) << "Parsed SST dump output:\n" << output;

  // Data from excluded file
  // ybctid_1_vector_1 -> ybctid_1
  // ybctid_3_vector_1 -> ybctid_3
  // ybctid_5_vector_1 -> ybctid_5
  // ybctid_4_vector_2 -> ybctid_4
  //
  // Other old files:
  // ybctid_4_vector_2 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_4_vector_3 -> ybctid_4  => outside retention, should be filtered due to newer value
  // ybctid_6_vector_1 -> ybctid_6  => outside retention, should be filtered due to delete
  // ybctid_7_vector_1 -> ybctid_7  => visible
  // ybctid_3_vector_1 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_6_vector_1 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_4_vector_3 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_4_vector_4 -> ybctid_4  => visible
  // ybctid_8_vector_1 -> ybctid_8  => outside retention, but should be kept as updates too fresh.
  //
  // Fresh files: => fresh data, within retention period.
  // ybctid_9_vector_1 -> ybctid_9
  // ...
  // ybctid_N_vector_3 -> ybctid_N
  ASSERT_STR_EQ_VERBOSE_TRIMMED(util::TrimWhitespaceFromEveryLine(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
          ybctid_4_vector_2 -> DEL
          ybctid_7_vector_1 -> ybctid_7
          ybctid_3_vector_1 -> DEL
          ybctid_6_vector_1 -> DEL
          ybctid_4_vector_3 -> DEL
          ybctid_4_vector_4 -> ybctid_4
          ybctid_8_vector_1 -> ybctid_8
      )#" + expected_tail.str()),
      output);
}

class PgVectorIndexUtilTest : public PgVectorIndexSingleServerTestBase {
 protected:
  bool IsColocated() const override {
    return false;
  }

  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kUsearch;
  }

  size_t NumTabletServers() override {
    return 1;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV1;
  }
};

TEST_F(PgVectorIndexUtilTest, SstDump) {
  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 4"));
  ASSERT_OK(cluster_->FlushTablets());

  auto table_peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(table_peers.size(), 1);
  auto tablet = ASSERT_RESULT(table_peers.front()->shared_tablet());
  auto rocksdb_dir = tablet->metadata()->rocksdb_dir();
  ASSERT_FALSE(rocksdb_dir.empty());
  LOG(INFO) << "RocksDB dir: " << rocksdb_dir;

  TestKVFormatter formatter;
  ASSERT_OK(RunSstDump(formatter, rocksdb_dir));

  auto output = formatter.FormatVectorsMeta();
  LOG(INFO) << "Parsed SST dump output:\n" << output;

  // The entires order is different from what sst_dump really prints, it's required to re-sort
  // to be able to compare the expected results.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_2_vector_1 -> DEL
          ybctid_4_vector_1 -> DEL
          ybctid_4_vector_2 -> ybctid_4
      )#",
      output);
}

TEST_F(PgVectorIndexUtilTest, DeleteTabletDirs) {
  constexpr size_t kNumRows = 10;
  num_pre_split_tablets_ = 2; // To have test both types of delete_state.
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows, AddFilter::kFalse));

  auto tablet_peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(tablet_peers.size(), 2);

  for (auto delete_state : { tablet::TABLET_DATA_DELETED, tablet::TABLET_DATA_TOMBSTONED }) {
    ASSERT_FALSE(tablet_peers.empty());
    auto tablet = ASSERT_RESULT(tablet_peers.back()->shared_tablet());
    auto vector_indexes = tablet->vector_indexes().List();
    ASSERT_EQ(vector_indexes->size(), 1);

    const auto vector_index_dir = vector_indexes->front()->path();
    const auto table_dir = DirName(vector_index_dir);
    const auto tablet_id = tablet->tablet_id();

    // Sanity check to make sure table dir contains vector index.
    ASSERT_TRUE(vector_index_dir.starts_with(table_dir));

    auto content = ASSERT_RESULT(Env::Default()->GetChildren(table_dir, ExcludeDots::kTrue));
    LOG(INFO) << Format("Table dirs before tablet $0 deletion: $1", tablet_id, AsString(content));

    ASSERT_OK(cluster_->mini_tablet_server(0)->DeleteTablet(tablet_id, delete_state));

    // Make sure all tablet directories have been deleted (including vector index dir).
    content = ASSERT_RESULT(Env::Default()->GetChildren(table_dir, ExcludeDots::kTrue));
    LOG(INFO) << Format("Table dirs after tablet $0 deletion: $1", tablet_id, AsString(content));
    const auto num_tablet_dirs = std::ranges::count_if(
          content, [&tablet_id](const std::string& subdir) { return subdir.contains(tablet_id); });
    ASSERT_EQ(num_tablet_dirs, 0);

    tablet_peers.pop_back();
  }
}

}  // namespace yb::pgwrapper
