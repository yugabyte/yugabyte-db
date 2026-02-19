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

#include <ranges>

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"

DECLARE_bool(vector_index_enable_compactions);
DECLARE_uint32(vector_index_num_compactions_limit);

using namespace std::literals;

namespace yb::pgwrapper {

constexpr auto kBackfillSleepSec = 10 * kTimeMultiplier;

class PgVectorIndexITest : public LibPqTestBase {
 public:
  void SetUp() override {
    FLAGS_vector_index_enable_compactions = true;
    FLAGS_vector_index_num_compactions_limit = 0;
    LibPqTestBase::SetUp();
  }

  Result<PGConn> ConnectAndInit(std::optional<int> num_tablets = std::nullopt) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));
    std::string stmt = "CREATE TABLE test (id INT PRIMARY KEY, embedding vector(1))";
    if (num_tablets) {
      stmt += Format(" SPLIT INTO $0 TABLETS", *num_tablets);
    }
    RETURN_NOT_OK(conn.ExecuteFormat(stmt));
    return conn;
  }

  Status CreateIndex(PGConn& conn) {
    // TODO(vector_index) Switch to using CONCURRENT index creation when it will be ready.
    return conn.Execute(
        "CREATE INDEX ON test USING ybhnsw (embedding vector_l2_ops)");
  }
};

struct VectorIndexWriter {
  static constexpr int kBig = 100000000;

  std::atomic<int> counter = 0;
  std::atomic<int> extra_values_counter = kBig * 2;
  std::atomic<CoarseTimePoint> last_write;
  std::atomic<MonoDelta> max_time_without_inserts = MonoDelta::FromNanoseconds(0);
  std::atomic<bool> failure = false;

  void Perform(PGConn& conn) {
    std::vector<int> values;
    for (int i = RandomUniformInt(3, 6); i > 0; --i) {
      values.push_back(++counter);
    }
    size_t keep_values = values.size();
    for (int i = RandomUniformInt(0, 2); i > 0; --i) {
      values.push_back(++extra_values_counter);
    }
    bool use_2_steps = RandomUniformBool();

    int offset = use_2_steps ? kBig : 0;
    ASSERT_NO_FATALS(Insert(conn, values, offset));
    if (use_2_steps || keep_values != values.size()) {
      ASSERT_NO_FATALS(UpdateAndDelete(conn, values, keep_values));
    }
  }

  void Insert(PGConn& conn, const std::vector<int>& values, int offset) {
    for (;;) {
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      bool failed = false;
      for (auto value : values) {
        auto res = conn.ExecuteFormat(
            "INSERT INTO test VALUES ($0, '[$1.0]')", value, value + offset);
        if (!res.ok()) {
          ASSERT_OK(conn.RollbackTransaction());
          LOG(INFO) << "Insert " << value << " failed: " << res;
          ASSERT_STR_CONTAINS(res.message().ToBuffer(), "schema version mismatch");
          failed = true;
          break;
        }
      }
      if (!failed) {
        ASSERT_OK(conn.CommitTransaction());
        auto now = CoarseMonoClock::Now();
        auto prev_last_write = last_write.exchange(now);
        if (prev_last_write != CoarseTimePoint()) {
          MonoDelta new_value(now - prev_last_write);
          if (MakeAtLeast(max_time_without_inserts, new_value)) {
            LOG(INFO) << "Update max time without inserts: " << new_value;
          }
        }
        std::this_thread::sleep_for(100ms);
        break;
      }
    }
  }

  void UpdateAndDelete(PGConn& conn, const std::vector<int>& values, size_t keep_values) {
    for (;;) {
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      bool failed = false;
      for (size_t i = 0; i != values.size(); ++i) {
        auto value = values[i];
        Status res;
        if (i < keep_values) {
          res = conn.ExecuteFormat(
              "UPDATE test SET embedding = '[$0.0]' WHERE id = $0", value);
        } else {
          res = conn.ExecuteFormat("DELETE FROM test WHERE id = $0", value);
        }
        if (!res.ok()) {
          ASSERT_OK(conn.RollbackTransaction());
          LOG(INFO) <<
              (i < keep_values ? "Update " : "Delete " ) << value << " failed: " << res;
          ASSERT_STR_CONTAINS(res.message().ToBuffer(), "schema version mismatch");
          failed = true;
          break;
        }
      }
      if (!failed) {
        ASSERT_OK(conn.CommitTransaction());
        std::this_thread::sleep_for(100ms);
        break;
      }
    }
  }

  void WaitWritten(int num_rows) {
    auto limit = counter.load() + num_rows;
    while (counter.load() < limit && !failure) {
      std::this_thread::sleep_for(10ms);
    }
  }

  void Verify(PGConn& conn) {
    int num_bad_results = 0;
    for (int i = 2; i < counter.load(); ++i) {
      auto rows = ASSERT_RESULT(conn.FetchAllAsString(Format(
          "SELECT id FROM test ORDER BY embedding <-> '[$0]' LIMIT 3", i * 1.0 - 0.01)));
      auto expected = Format("$0; $1; $2", i, i - 1, i + 1);
      if (rows != expected) {
        LOG(INFO) << "Bad result: " << rows << " vs " << expected;
        ++num_bad_results;
      }
    }
    // Expect recall 98% or better.
    ASSERT_LE(num_bad_results, counter.load() / 50);
  }
};

class PgVectorIndexBackfillITest : public PgVectorIndexITest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgVectorIndexITest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--ysql_disable_index_backfill=false");
    options->extra_tserver_flags.push_back(
        Format("--TEST_sleep_before_vector_index_backfill_seconds=$0", kBackfillSleepSec));
    // yb_hnsw wrapper currently does not support retrieving vectors by id.
    options->extra_master_flags.push_back("--vector_index_use_yb_hnsw=false");
  }
};

TEST_F_EX(PgVectorIndexITest, Backfill, PgVectorIndexBackfillITest) {
  auto conn = ASSERT_RESULT(ConnectAndInit());
  TestThreadHolder thread_holder;
  VectorIndexWriter writer;
  for (int i = 0; i != 8; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop_flag = thread_holder.stop_flag(), &writer] {
      auto se = CancelableScopeExit([&writer] {
        writer.failure = true;
      });
      auto conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load()) {
        ASSERT_NO_FATALS(writer.Perform(conn));
      }
      se.Cancel();
    });
  }
  writer.WaitWritten(32);
  LOG(INFO) << "Started to create index";
  ASSERT_OK(CreateIndex(conn));
  LOG(INFO) << "Finished to create index";
  writer.WaitWritten(32);
  thread_holder.Stop();
  LOG(INFO) << "Max time without inserts: " << writer.max_time_without_inserts;
  ASSERT_LT(writer.max_time_without_inserts, 1s * kBackfillSleepSec);
  SCOPED_TRACE(Format("Total rows: $0", writer.counter.load()));

  // VerifyVectorIndexes does not take intents into account, so could produce false failure.
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    tserver::VerifyVectorIndexesRequestPB req;
    tserver::VerifyVectorIndexesResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(30s);
    auto proxy = cluster_->tablet_server(i)->Proxy<tserver::TabletServerServiceProxy>();
    ASSERT_OK(proxy->VerifyVectorIndexes(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
  }
  writer.Verify(conn);
}

class PgVectorIndexRBSITest : public PgVectorIndexITest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--log_min_seconds_to_retain=0");
    options->extra_tserver_flags.push_back("--xcluster_checkpoint_max_staleness_secs=0");
  }
};

TEST_F_EX(PgVectorIndexITest, CrashAfterRBSDownload, PgVectorIndexRBSITest) {
  constexpr size_t kNumRows = 5;
  constexpr size_t kTsIndex = 2;

  auto conn = ASSERT_RESULT(ConnectAndInit(1));
  ASSERT_OK(CreateIndex(conn));
  auto* lagging_ts = cluster_->tablet_server(kTsIndex);
  lagging_ts->Shutdown();

  auto tablets = ASSERT_RESULT(cluster_->ListTablets(cluster_->tablet_server(0)));
  ASSERT_EQ(tablets.status_and_schema().size(), 1);
  auto tablet_id = tablets.status_and_schema()[0].tablet_status().tablet_id();

  for (size_t i = 1; i <= kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, '[$0.0]')", i));
    for (size_t j = 0; j != 2; ++j) {
      ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(j, {tablet_id}));
      ASSERT_OK(cluster_->LogGCOnSingleTServer(j, {tablet_id}, true));
    }
  }

  ASSERT_OK(lagging_ts->Start());

  auto good_tablets = ASSERT_RESULT(cluster_->ListTablets(cluster_->tablet_server(0)));
  LOG(INFO) << "Good tablets: " << AsString(good_tablets);
  ASSERT_EQ(good_tablets.status_and_schema().size(), 1);
  ASSERT_OK(WaitFor([this, &good_tablets, lagging_ts]() -> Result<bool> {
    auto lagging_tablets = VERIFY_RESULT(cluster_->ListTablets(lagging_ts));
    LOG(INFO) << "Lagging tablets: " << AsString(lagging_tablets);
    if (lagging_tablets.status_and_schema().size() != 1) {
      return false;
    }
    EXPECT_EQ(lagging_tablets.status_and_schema().size(), 1);
    return lagging_tablets.status_and_schema()[0].tablet_status().last_op_id().index() >=
           good_tablets.status_and_schema()[0].tablet_status().last_op_id().index();
  }, 5s, "Wait lagging TS to catch up"));
}

TEST_F(PgVectorIndexITest, Truncate) {
  constexpr size_t kNumIterations = 25;

  auto conn = ASSERT_RESULT(ConnectAndInit());
  ASSERT_OK(CreateIndex(conn));
  for (size_t i = 1; i <= kNumIterations; ++i) {
    LOG(INFO) << "Iteration: " << i;
    ASSERT_OK(conn.Execute("TRUNCATE test"));
  }
}

} // namespace yb::pgwrapper
