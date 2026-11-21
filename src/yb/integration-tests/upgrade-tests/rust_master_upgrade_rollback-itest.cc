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

// R-1 to R upgrade and R to R-1 rollback with the Rust master: R-1 is the latest stable release,
// whose masters are C++, and R is this build, whose masters are Rust. Rollback to R-1 is the only
// way out of a Rust master problem in R, so it is checked from every mixed step of the upgrade.

#include <algorithm>

#include "yb/gutil/strings/join.h"

#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

namespace yb {

namespace {

// How far the masters are upgraded before the rollback: masters upgrade followers first, the
// leader last.
YB_DEFINE_ENUM(RollbackFrom, (kOneRustMaster)(kTwoRustMasters)(kAllRustMasters));

constexpr auto kTable = "upgrade_rollback";
constexpr auto kKeyspace = "upgrade_rollback_ks";

}  // namespace

class RustMasterUpgradeRollbackTest : public UpgradeTestBase {
 public:
  RustMasterUpgradeRollbackTest() : UpgradeTestBase(kBuild_2026_1_2_0) {}

  void TearDown() override {
    keep_running_ = false;
    test_thread_holder_.JoinAll();
    UpgradeTestBase::TearDown();
  }

 protected:
  // YSQL and YCQL rows written while whichever master leads; reads check that none is lost.
  Status StartWorkload() {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
    std::vector<std::string> hosts;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      hosts.push_back(cluster_->tablet_server(i)->bind_host());
    }
    cql_driver_ = std::make_unique<CppCassandraDriver>(
        hosts, cluster_->tablet_server(0)->cql_rpc_port(), UsePartitionAwareRouting::kTrue);
    auto cql = VERIFY_RESULT(cql_driver_->CreateSession());
    RETURN_NOT_OK(cql.ExecuteQueryFormat("CREATE KEYSPACE $0", kKeyspace));
    RETURN_NOT_OK(
        cql.ExecuteQueryFormat("CREATE TABLE $0.$1 (k INT PRIMARY KEY, v INT)", kKeyspace, kTable));
    test_thread_holder_.AddThreadFunctor([this] {
      int k = 0;
      while (keep_running_) {
        auto cql = cql_driver_->CreateSession();
        if (!cql.ok()) {
          SleepFor(100ms);
          continue;
        }
        while (keep_running_) {
          if (!cql->ExecuteQueryFormat(
                      "INSERT INTO $0.$1 (k, v) VALUES ($2, $2)", kKeyspace, kTable, k).ok()) {
            break;
          }
          cql_inserted_.store(++k);
          SleepFor(20ms);
        }
      }
    });
    test_thread_holder_.AddThreadFunctor([this] {
      int k = 0;
      while (keep_running_) {
        auto conn = cluster_->ConnectToDB();
        if (!conn.ok()) {
          SleepFor(100ms);
          continue;
        }
        while (keep_running_) {
          if (!conn->ExecuteFormat("INSERT INTO $0 VALUES ($1, $1)", kTable, k).ok()) {
            break;
          }
          inserted_.store(++k);
          SleepFor(20ms);
        }
      }
    });
    return Status::OK();
  }

  // Every inserted row is there, and DDL works on the current leader.
  Status CheckDataAndDdl(const std::string& step) {
    const auto inserted = inserted_.load();
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    auto count = VERIFY_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT COUNT(*) FROM $0 WHERE k < $1", kTable, inserted)));
    SCHECK_EQ(count, inserted, IllegalState, Format("Rows lost at $0", step));
    const auto table = Format("ddl_$0", ddl_count_++);
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY)", table));
    RETURN_NOT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", table));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE INDEX ON $0 (k)", table));
    RETURN_NOT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c INT", table));

    // The YCQL writer keeps going, so the table holds at least the rows written before the read.
    const auto cql_inserted = cql_inserted_.load();
    auto cql = VERIFY_RESULT(cql_driver_->CreateSession());
    auto cql_count = VERIFY_RESULT(
        cql.FetchValue<int64_t>(Format("SELECT COUNT(*) FROM $0.$1", kKeyspace, kTable)));
    SCHECK_GE(cql_count, cql_inserted, IllegalState, Format("YCQL rows lost at $0", step));
    RETURN_NOT_OK(
        cql.ExecuteQueryFormat("CREATE TABLE $0.$1 (k INT PRIMARY KEY)", kKeyspace, table));
    RETURN_NOT_OK(cql.ExecuteQueryFormat("ALTER TABLE $0.$1 ADD c INT", kKeyspace, table));
    LOG(INFO) << step << ": " << inserted << " YSQL and " << cql_inserted
              << " YCQL rows, and a DDL round on leader " << cluster_->GetLeaderMaster()->uuid();
    return Status::OK();
  }

  // Moves leadership to each master in turn, checking data and DDL under each leader.
  Status LeadOnEachMaster(const std::string& step) {
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      auto* master = cluster_->master(i);
      if (cluster_->GetLeaderMaster() != master) {
        RETURN_NOT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader(master->uuid()));
      }
      RETURN_NOT_OK(WaitForClusterToStabilize());
      RETURN_NOT_OK(CheckDataAndDdl(Format("$0, leader $1", step, i)));
    }
    return Status::OK();
  }

  // The masters' followers first, then the leader, as the upgrade-test framework orders them.
  Result<std::vector<ExternalMaster*>> MastersInUpgradeOrder() {
    auto* leader = cluster_->GetLeaderMaster();
    std::vector<ExternalMaster*> order;
    for (auto* master : cluster_->master_daemons()) {
      if (master != leader) {
        order.push_back(master);
      }
    }
    order.push_back(leader);
    return order;
  }

  // The sys catalog of each stopped master, as sys-catalog-tool dump-rows writes it.
  Result<std::vector<std::string>> DumpRows(ExternalMaster* master) {
    const auto file = JoinPathSegments(GetTestDataDirectory(), master->uuid() + "-rows.tsv");
    std::vector<std::string> roots;
    for (const auto& dir : master->GetDataDirs()) {
      roots.push_back(DirName(DirName(dir)));
    }
    RETURN_NOT_OK(Subprocess::Call(
        {GetToolPath("sys-catalog-tool"), "--fs_data_dirs", JoinStrings(roots, ","), "dump-rows",
         "--file", file},
        /* log_stdout_and_stderr= */ true));
    faststring content;
    RETURN_NOT_OK(ReadFileToString(Env::Default(), file, &content));
    auto lines = StringSplit(content.ToString(), '\n');
    std::sort(lines.begin(), lines.end());
    return lines;
  }

  // After the masters stop, every master holds the same rows.
  Status CheckMastersHoldSameRows() {
    cluster_->Shutdown();
    std::optional<std::vector<std::string>> first;
    for (auto* master : cluster_->master_daemons()) {
      auto rows = VERIFY_RESULT(DumpRows(master));
      SCHECK(!rows.empty(), IllegalState, Format("No rows in master $0", master->uuid()));
      if (!first) {
        first = std::move(rows);
      } else {
        SCHECK(rows == *first, IllegalState,
               Format("Master $0 holds different sys catalog rows", master->uuid()));
      }
    }
    return Status::OK();
  }

  Status RollbackFromMixedMasters(RollbackFrom from) {
    RETURN_NOT_OK(StartClusterInOldVersion());
    RETURN_NOT_OK(StartWorkload());
    RETURN_NOT_OK(CheckDataAndDdl("R-1"));

    const auto masters = VERIFY_RESULT(MastersInUpgradeOrder());
    const size_t num_rust = std::to_underlying(from) + 1;
    for (size_t i = 0; i < num_rust; ++i) {
      RETURN_NOT_OK(RestartMasterInCurrentVersion(*masters[i]));
      RETURN_NOT_OK(LeadOnEachMaster(Format("$0 Rust master(s)", i + 1)));
    }

    // Rollback restarts the masters that run R in R-1.
    for (size_t i = num_rust; i-- > 0;) {
      RETURN_NOT_OK(RestartMasterInOldVersion(*masters[i]));
    }
    RETURN_NOT_OK(LeadOnEachMaster("rolled back to R-1"));
    keep_running_ = false;
    test_thread_holder_.JoinAll();
    return CheckMastersHoldSameRows();
  }

  std::atomic<bool> keep_running_{true};
  std::atomic<int> inserted_{0};
  std::atomic<int> cql_inserted_{0};
  std::unique_ptr<CppCassandraDriver> cql_driver_;
  int ddl_count_ = 0;
};

TEST_F(RustMasterUpgradeRollbackTest, RollbackFromOneRustMaster) {
  ASSERT_OK(RollbackFromMixedMasters(RollbackFrom::kOneRustMaster));
}

TEST_F(RustMasterUpgradeRollbackTest, RollbackFromTwoRustMasters) {
  ASSERT_OK(RollbackFromMixedMasters(RollbackFrom::kTwoRustMasters));
}

TEST_F(RustMasterUpgradeRollbackTest, RollbackFromAllRustMasters) {
  ASSERT_OK(RollbackFromMixedMasters(RollbackFrom::kAllRustMasters));
}

// The whole release upgraded, tservers included, but not finalized: the documented rollback
// (volatile AutoFlags demoted by the Rust leader, tservers, then masters) brings R-1 back with
// every row.
TEST_F(RustMasterUpgradeRollbackTest, RollbackFullUpgrade) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(StartWorkload());
  ASSERT_OK(UpgradeClusterToCurrentVersion(3s, /* auto_finalize= */ false));
  ASSERT_OK(LeadOnEachMaster("R"));
  ASSERT_OK(RollbackClusterToOldVersion());
  ASSERT_OK(LeadOnEachMaster("rolled back to R-1"));
  keep_running_ = false;
  test_thread_holder_.JoinAll();
  ASSERT_OK(CheckMastersHoldSameRows());
}

// A finalized upgrade: R's AutoFlags promoted by the Rust leader, and every master leads.
TEST_F(RustMasterUpgradeRollbackTest, FinalizedUpgrade) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(StartWorkload());
  ASSERT_OK(UpgradeClusterToCurrentVersion(3s, /* auto_finalize= */ true));
  ASSERT_OK(LeadOnEachMaster("R finalized"));
}

}  // namespace yb
