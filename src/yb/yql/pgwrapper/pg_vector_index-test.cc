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

#include "yb/client/snapshot_test_util.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

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

  std::string DbName() {
    return IsColocated() ? "colocated_db" : "yugabyte";
  }

  Result<PGConn> Connect() const override {
    return IsColocated() ? ConnectToDB("colocated_db") : PgMiniTestBase::Connect();
  }

  Result<PGConn> MakeIndex(int num_tablets = 0) {
    auto colocated = IsColocated();
    auto conn = VERIFY_RESULT(PgMiniTestBase::Connect());
    std::string create_suffix;
    if (colocated) {
      create_suffix = " WITH (COLOCATED = 1)";
      RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE colocated_db COLOCATION = true"));
      conn = VERIFY_RESULT(Connect());
    } else if (num_tablets) {
      create_suffix = Format(" SPLIT INTO $0 TABLETS", num_tablets);
    }
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector(3))" + create_suffix));

    RETURN_NOT_OK(conn.Execute("CREATE INDEX ON test USING ybhnsw (embedding vector_l2_ops)"));

    return conn;
  }

  Status WaitForLoadBalance(int num_tablet_servers) {
    return WaitFor(
      [&]() -> Result<bool> { return client_->IsLoadBalanced(num_tablet_servers); },
      60s * kTimeMultiplier,
      Format("Wait for load balancer to balance to $0 tservers.", num_tablet_servers));
  }

  Result<PGConn> MakeIndexAndFill(int num_rows, int num_tablets = 0);
  Status InsertRows(PGConn& conn, int start_row, int end_row);

  void VerifyRead(PGConn& conn, int limit, bool add_filter);
  void VerifyRows(
      PGConn& conn, bool add_filter, const std::vector<std::string>& expected, int limit = -1);

  void TestSimple();
  void TestManyRows(bool add_filter);
  void TestRestart(tablet::FlushFlags flush_flags);
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

std::string BuildRow(int64_t id, const std::string& value) {
  return Format("$0, $1", id, value);
}

std::string ExpectedRow(int64_t id) {
  return BuildRow(id, VectorAsString(id));
}

Status PgVectorIndexTest::InsertRows(PGConn& conn, int start_row, int end_row) {
  RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (int i = start_row; i <= end_row; ++i) {
    RETURN_NOT_OK(conn.ExecuteFormat(
       "INSERT INTO test VALUES ($0, '$1')", i, VectorAsString(i)));
  }
  return conn.CommitTransaction();
}

Result<PGConn> PgVectorIndexTest::MakeIndexAndFill(int num_rows, int num_tablets) {
  auto conn = VERIFY_RESULT(MakeIndex(num_tablets));
  RETURN_NOT_OK(InsertRows(conn, 1, num_rows));
  return conn;
}

void PgVectorIndexTest::VerifyRows(
    PGConn& conn, bool add_filter, const std::vector<std::string>& expected, int limit) {
  auto result = ASSERT_RESULT((conn.FetchRows<RowAsString>(Format(
      "SELECT * FROM test $0 ORDER BY embedding <-> '[0.0, 0.0, 0.0]' LIMIT $1",
      add_filter ? "WHERE id + 3 <= 5" : "",
      limit == -1 ? expected.size() : make_unsigned(limit)))));
  EXPECT_EQ(result.size(), expected.size());
  for (size_t i = 0; i != std::min(result.size(), expected.size()); ++i) {
    SCOPED_TRACE(Format("Row $0", i));
    EXPECT_EQ(result[i], expected[i]);
  }
}

void PgVectorIndexTest::VerifyRead(PGConn& conn, int limit, bool add_filter) {
  std::vector<std::string> expected;
  for (int i = 1; i <= limit; ++i) {
    expected.push_back(ExpectedRow(i));
  }
  VerifyRows(conn, add_filter, expected);
}

void PgVectorIndexTest::TestManyRows(bool add_filter) {
  constexpr int kNumRows = RegularBuildVsSanitizers(2000, 64);
  const int query_limit = add_filter ? 1 : 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, query_limit, add_filter));
}

TEST_P(PgVectorIndexTest, Split) {
  constexpr int kNumRows = RegularBuildVsSanitizers(500, 64);
  constexpr int kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  // Give some time for split to happen.
  std::this_thread::sleep_for(2s * kTimeMultiplier);

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, false));
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

void PgVectorIndexTest::TestRestart(tablet::FlushFlags flush_flags) {
  constexpr int kNumRows = 64;
  constexpr int kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, false));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, flush_flags));
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, false));
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

TEST_P(PgVectorIndexTest, DeleteAndUpdate) {
  constexpr int kNumRows = 64;
  const std::string kDistantVector = "[100, 500, 9000]";
  const std::string kCloseVector = "[0.125, 0.25, 0.375]";

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 2", kDistantVector));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 10", kCloseVector));

  std::vector<std::string> expected = {
    BuildRow(10, kCloseVector),
    ExpectedRow(3),
    ExpectedRow(4),
    ExpectedRow(5),
    ExpectedRow(6),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, false, expected));

  std::vector<std::string> expected_filtered = {
    BuildRow(2, kDistantVector),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, true, expected_filtered));
}

TEST_P(PgVectorIndexTest, RemoteBootstrap) {
  constexpr int kNumRows = 64;
  constexpr int kQueryLimit = 5;

  auto* mts = cluster_->mini_tablet_server(2);
  mts->Shutdown();
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows, 3));
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
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, false));
}

TEST_P(PgVectorIndexTest, SnapshotSchedule) {
  constexpr int kNumRows = 128;
  constexpr int kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, false));

  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  ASSERT_OK(conn.Execute("DELETE FROM test"));
  ASSERT_NO_FATALS(VerifyRows(conn, false, {}, 10));

  auto snapshot_id = ASSERT_RESULT(snapshot_util.PickSuitableSnapshot(
      schedule_id, hybrid_time));
  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id, hybrid_time));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, false));
}

std::string ColocatedToString(const testing::TestParamInfo<bool>& param_info) {
  return param_info.param ? "Colocated" : "Distributed";
}

INSTANTIATE_TEST_SUITE_P(, PgVectorIndexTest, ::testing::Bool(), ColocatedToString);

}  // namespace yb::pgwrapper
