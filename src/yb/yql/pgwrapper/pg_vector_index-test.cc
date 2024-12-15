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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_thread_holder.h"

DECLARE_bool(TEST_skip_process_apply);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_uint32(vector_index_concurrent_reads);
DECLARE_uint32(vector_index_concurrent_writes);

namespace yb::pgwrapper {

class PgVectorIndexTest : public PgMiniTestBase, public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    itest::SetupQuickSplit(1_KB);
    PgMiniTestBase::SetUp();
  }

  bool IsColocated() const {
    return GetParam();
  }

  Result<PGConn> Connect() const override {
    return IsColocated() ? ConnectToDB("colocated_db") : PgMiniTestBase::Connect();
  }

  Result<PGConn> MakeIndex() {
    auto colocated = IsColocated();
    auto conn = VERIFY_RESULT(PgMiniTestBase::Connect());
    std::string create_suffix;
    if (colocated) {
      create_suffix = " WITH (COLOCATED = 1)";
      RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE colocated_db COLOCATION = true"));
      conn = VERIFY_RESULT(Connect());
    }
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector(3))" + create_suffix));

    RETURN_NOT_OK(conn.Execute("CREATE INDEX ON test USING ybhnsw (embedding vector_l2_ops)"));

    return conn;
  }

  Result<PGConn> MakeIndexAndFill(int num_rows);

  Status VerifyRead(PGConn& conn, int limit, bool add_filter);

  void TestSimple();
  void TestManyRows(bool add_filter);
};

void PgVectorIndexTest::TestSimple() {
  auto conn = ASSERT_RESULT(MakeIndex());

  size_t num_found_peers = 0;
  auto check_tablets = [this, &num_found_peers]() -> Result<bool> {
    num_found_peers = 0;
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : peers) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet_safe());
      if (tablet->table_type() != TableType::PGSQL_TABLE_TYPE) {
        continue;
      }
      auto& metadata = *tablet->metadata();
      auto tables = metadata.GetAllColocatedTables();
      tablet::TableInfoPtr main_table_info;
      tablet::TableInfoPtr index_table_info;
      for (const auto& table_id : tables) {
        auto table_info = VERIFY_RESULT(metadata.GetTableInfo(table_id));
        LOG(INFO) << "Table: " << table_info->ToString();
        if (table_info->table_name == "test") {
          main_table_info = table_info;
        } else if (table_info->index_info) {
          index_table_info = table_info;
        }
      }
      if (!main_table_info) {
        continue;
      }
      ++num_found_peers;
      if (!index_table_info) {
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

  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1.0, 0.5, 0.25]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[0.125, 0.375, 0.25]')"));

  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5"));
  ASSERT_EQ(result, "1, [1, 0.5, 0.25]; 2, [0.125, 0.375, 0.25]");
}

TEST_P(PgVectorIndexTest, Simple) {
  TestSimple();
}

TEST_P(PgVectorIndexTest, NotApplied) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  TestSimple();
}

std::string VectorAsString(int64_t id) {
  return Format("[$0, $1, $2]", id, id * 2, id * 3);
}

std::string ExpectedRow(int64_t id) {
  return Format("$0, $1", id, VectorAsString(id));
}

Result<PGConn> PgVectorIndexTest::MakeIndexAndFill(int num_rows) {
  auto conn = VERIFY_RESULT(MakeIndex());

  RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (int i = 1; i <= num_rows; ++i) {
    RETURN_NOT_OK(conn.ExecuteFormat(
       "INSERT INTO test VALUES ($0, '$1')", i, VectorAsString(i)));
  }
  RETURN_NOT_OK(conn.CommitTransaction());
  return conn;
}

Status PgVectorIndexTest::VerifyRead(PGConn& conn, int limit, bool add_filter) {
  auto result = VERIFY_RESULT((conn.FetchRows<RowAsString>(Format(
      "SELECT * FROM test $0 ORDER BY embedding <-> '[0.0, 0.0, 0.0]' LIMIT $1",
      add_filter ? "WHERE id + 3 <= 5" : "",
      limit))));
  SCHECK_EQ(result.size(), limit, IllegalState, "Wrong number of rows");
  for (size_t i = 0; i != result.size(); ++i) {
    SCHECK_EQ(
        result[i], ExpectedRow(i + 1), IllegalState, Format("Wrong value for row: $0", i + 1));
  }
  return Status::OK();
}

void PgVectorIndexTest::TestManyRows(bool add_filter) {
  constexpr int kNumRows = RegularBuildVsSanitizers(2000, 64);
  const int query_limit = add_filter ? 1 : 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(VerifyRead(conn, query_limit, add_filter));
}

TEST_P(PgVectorIndexTest, Split) {
  constexpr int kNumRows = RegularBuildVsSanitizers(500, 64);
  constexpr int kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  // Give some time for split to happen.
  std::this_thread::sleep_for(2s * kTimeMultiplier);

  ASSERT_OK(VerifyRead(conn, kQueryLimit, false));
}

TEST_P(PgVectorIndexTest, ManyRows) {
  TestManyRows(false);
}

TEST_P(PgVectorIndexTest, ManyRowsWithFilter) {
  TestManyRows(true);
}

TEST_P(PgVectorIndexTest, ManyReads) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_reads) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_writes) = 1;

  constexpr int kNumRows = 64;
  constexpr int kNumReads = 16;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  TestThreadHolder threads;
  for (int i = 1; i <= kNumReads; ++i) {
    threads.AddThreadFunctor([this, &stop_flag = threads.stop_flag()] {
      auto conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load()) {
        auto id = RandomUniformInt(1, kNumRows);
        auto vector = VectorAsString(id);
        auto rows = ASSERT_RESULT(conn.FetchAllAsString(Format(
            "SELECT * FROM test ORDER BY embedding <-> '$0' LIMIT 1", vector)));
        ASSERT_EQ(rows, ExpectedRow(id));
      }
    });
  }

  threads.WaitAndStop(5s);
}

TEST_P(PgVectorIndexTest, Restart) {
  constexpr int kNumRows = 64;
  constexpr int kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(VerifyRead(conn, kQueryLimit, false));
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(VerifyRead(conn, kQueryLimit, false));
}

std::string ColocatedToString(const testing::TestParamInfo<bool>& param_info) {
  return param_info.param ? "Colocated" : "Distributed";
}

INSTANTIATE_TEST_SUITE_P(, PgVectorIndexTest, ::testing::Bool(), ColocatedToString);

}  // namespace yb::pgwrapper
