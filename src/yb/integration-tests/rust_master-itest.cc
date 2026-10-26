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

// The Rust master (yb-master-rs) as a follower in a quorum of C++ masters. In layer 1 it never
// leads, so these tests check that it joins, keeps the same sys catalog as a C++ follower through
// DDL, snapshots and a PITR restore, answers the follower-servable RPCs, and never claims to lead.

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/path_util.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {

namespace {

constexpr auto kTimeout = 60s;

}  // namespace

class RustMasterTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    ExternalMiniClusterOptions opts;
    opts.num_masters = 3;
    opts.num_tablet_servers = 3;
    opts.replication_factor = 3;
    opts.enable_ysql = false;
    cluster_ = std::make_unique<ExternalMiniCluster>(opts);
    ASSERT_OK(cluster_->Start());
    rust_master_exe_ = BinaryPath("yb-master-rs");
    if (!Env::Default()->FileExists(rust_master_exe_)) {
      GTEST_SKIP() << "yb-master-rs is not built (yb_build.sh --skip-rust?)";
    }
    ASSERT_OK(cluster_->WaitForLeaderCommitTermAdvance());
    client_ = ASSERT_RESULT(cluster_->CreateClient());
  }

  void TearDown() override {
    client_.reset();
    if (cluster_) {
      cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

  // Restarts a follower master as the Rust master, on its own data. Returns its index.
  // Executables next to yb-master in the build.
  std::string BinaryPath(const std::string& name) const {
    return JoinPathSegments(DirName(cluster_->master(0)->exe()), name);
  }

  Result<size_t> RestartFollowerAsRust() {
    const auto leader = VERIFY_RESULT(cluster_->GetLeaderMasterIndex());
    const size_t index = (leader + 1) % cluster_->num_masters();
    auto* master = cluster_->master(index);
    master->Shutdown();
    master->SetExe(rust_master_exe_);
    RETURN_NOT_OK(master->Restart());
    RETURN_NOT_OK(WaitForRole(master, PeerRole::FOLLOWER));
    return index;
  }

  Result<master::GetMasterRegistrationResponsePB> Registration(ExternalMaster* master) {
    auto proxy = cluster_->GetProxy<master::MasterClusterProxy>(master);
    master::GetMasterRegistrationRequestPB req;
    master::GetMasterRegistrationResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    RETURN_NOT_OK(proxy.GetMasterRegistration(req, &resp, &controller));
    return resp;
  }

  Status WaitForRole(ExternalMaster* master, PeerRole role) {
    return WaitFor(
        [&]() -> Result<bool> {
          auto resp = Registration(master);
          return resp.ok() && !resp->has_error() && resp->role() == role;
        },
        kTimeout, Format("$0 to report role $1", master->uuid(), PeerRole_Name(role)));
  }

  // Waits until every master has applied everything the leader committed.
  Status WaitForMastersToCatchUp() {
    OpIdPB op_id;
    RETURN_NOT_OK(cluster_->GetLastOpIdForLeader(&op_id));
    return cluster_->WaitForMastersToCommitUpTo(op_id.index());
  }

  // Creates YCQL keyspaces, tables, an index, a snapshot schedule, schema changes and a PITR
  // restore through the C++ leader.
  void RunWorkload() {
    client::TableHandle table;
    client::kv_table_test::CreateTable(
        client::Transactional::kTrue, /* num_tablets= */ 3, client_.get(), &table);
    client::TableHandle index;
    client::kv_table_test::CreateIndex(
        client::Transactional::kTrue, /* indexed_column_index= */ 1,
        /* use_mangled_names= */ false, table, client_.get(), &index);

    client::SnapshotTestUtil snapshot_util;
    snapshot_util.SetProxy(&client_->proxy_cache());
    snapshot_util.SetCluster(cluster_.get());
    auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
        nullptr, YQL_DATABASE_CQL, table.name().namespace_name(), client::WaitSnapshot::kTrue,
        2s * kTimeMultiplier, 20s * kTimeMultiplier));
    const auto restore_at = HybridTime::FromMicros(GetCurrentTimeMicros());

    // Changes the restore undoes.
    client::TableHandle dropped;
    client::kv_table_test::CreateTable(
        client::Transactional::kTrue, /* num_tablets= */ 1, client_.get(), &dropped,
        client::YBTableName(YQL_DATABASE_CQL, table.name().namespace_name(), "dropped_later"));
    ASSERT_OK(client_->DeleteTable(table.name(), /* wait= */ true));

    ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, restore_at));
    ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));
  }

  // Dumps the sys catalog of a stopped master: one line per row, see sys-catalog-tool dump-rows.
  Result<std::vector<std::string>> DumpRows(ExternalMaster* master) {
    const auto file = JoinPathSegments(GetTestDataDirectory(), master->uuid() + "-rows.tsv");
    // GetDataDirs() lists <root>/yb-data/master; the tool takes the roots.
    std::vector<std::string> roots;
    for (const auto& dir : master->GetDataDirs()) {
      roots.push_back(DirName(DirName(dir)));
    }
    RETURN_NOT_OK(Subprocess::Call(
        {BinaryPath("sys-catalog-tool"), "--fs_data_dirs",
         JoinStrings(roots, ","), "dump-rows", "--file", file},
        /* log_stdout_and_stderr= */ true));
    faststring content;
    RETURN_NOT_OK(ReadFileToString(Env::Default(), file, &content));
    std::vector<std::string> rows;
    for (auto& line : StringSplit(content.ToString(), '\n')) {
      if (line.empty()) {
        continue;
      }
      // Drop the last column, the C++ re-serialization; the conformance tool checks that one.
      rows.push_back(line.substr(0, line.rfind('\t')));
    }
    std::sort(rows.begin(), rows.end());
    if (rows.empty()) {
      return STATUS_FORMAT(IllegalState, "No sys catalog rows in $0", file);
    }
    RETURN_NOT_OK(Subprocess::Call(
        {BinaryPath("master-conformance"), file}, /* log_stdout_and_stderr= */ true));
    return rows;
  }

  std::unique_ptr<ExternalMiniCluster> cluster_;
  std::unique_ptr<client::YBClient> client_;
  std::string rust_master_exe_;
};

TEST_F(RustMasterTest, FollowerKeepsSameCatalog) {
  const auto rust_index = ASSERT_RESULT(RestartFollowerAsRust());
  auto* rust_master = cluster_->master(rust_index);

  ASSERT_NO_FATALS(RunWorkload());
  ASSERT_OK(WaitForMastersToCatchUp());

  // The Rust master answers the follower-servable RPCs...
  auto registration = ASSERT_RESULT(Registration(rust_master));
  ASSERT_EQ(registration.role(), PeerRole::FOLLOWER);
  ASSERT_EQ(registration.instance_id().permanent_uuid(), rust_master->uuid());
  {
    auto proxy = cluster_->GetProxy<master::MasterClusterProxy>(rust_master);
    master::ListMastersRequestPB req;
    master::ListMastersResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    ASSERT_OK(proxy.ListMasters(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    ASSERT_EQ(resp.masters_size(), cluster_->num_masters());
    for (const auto& entry : resp.masters()) {
      ASSERT_FALSE(entry.has_error()) << entry.ShortDebugString();
    }
  }
  // ...and sends leader-only ones to the leader.
  {
    auto proxy = cluster_->GetProxy<master::MasterDdlProxy>(rust_master);
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    ASSERT_OK(proxy.ListTables(req, &resp, &controller));
    ASSERT_EQ(resp.error().code(), master::MasterErrorPB::NOT_THE_LEADER);
  }
  // The C++ leader sees it as a healthy peer.
  {
    auto proxy = cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>();
    master::ListMastersRequestPB req;
    master::ListMastersResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    ASSERT_OK(proxy.ListMasters(req, &resp, &controller));
    bool found = false;
    for (const auto& entry : resp.masters()) {
      if (entry.instance_id().permanent_uuid() == rust_master->uuid()) {
        found = true;
        ASSERT_FALSE(entry.has_error()) << entry.ShortDebugString();
        ASSERT_EQ(entry.role(), PeerRole::FOLLOWER);
      }
    }
    ASSERT_TRUE(found);
  }

  // Same sys catalog, row for row, as a C++ follower.
  const auto leader_index = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  size_t cpp_follower_index = 0;
  while (cpp_follower_index == leader_index || cpp_follower_index == rust_index) {
    ++cpp_follower_index;
  }
  auto* cpp_follower = cluster_->master(cpp_follower_index);
  cluster_->Shutdown();
  const auto rust_rows = ASSERT_RESULT(DumpRows(rust_master));
  const auto cpp_rows = ASSERT_RESULT(DumpRows(cpp_follower));
  ASSERT_EQ(rust_rows, cpp_rows);

  faststring rust_flags, cpp_flags;
  const auto auto_flags_path = [](ExternalMaster* master) {
    return JoinPathSegments(master->GetDataDirs()[0], "auto_flags_config");
  };
  ASSERT_OK(ReadFileToString(Env::Default(), auto_flags_path(rust_master), &rust_flags));
  ASSERT_OK(ReadFileToString(Env::Default(), auto_flags_path(cpp_follower), &cpp_flags));
  ASSERT_EQ(rust_flags.ToString(), cpp_flags.ToString());
}

TEST_F(RustMasterTest, NeverLeads) {
  const auto rust_index = ASSERT_RESULT(RestartFollowerAsRust());
  auto* rust_master = cluster_->master(rust_index);

  // Ask the leader to hand leadership to the Rust master. It wins the election and steps down.
  tserver::TabletServerErrorPB::Code error_code;
  ASSERT_OK(cluster_->StepDownMasterLeader(&error_code, rust_master->uuid()));
  const auto deadline = CoarseMonoClock::Now() + 10s;
  while (CoarseMonoClock::Now() < deadline) {
    auto registration = Registration(rust_master);
    if (registration.ok()) {
      ASSERT_NE(registration->role(), PeerRole::LEADER);
    }
    SleepFor(100ms);
  }
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto leader = cluster_->GetLeaderMasterIndex();
        return leader.ok() && *leader != rust_index;
      },
      kTimeout, "a C++ master to lead"));
  ASSERT_OK(WaitForRole(rust_master, PeerRole::FOLLOWER));
}

TEST_F(RustMasterTest, AddedThroughRemoteBootstrap) {
  auto new_master = ASSERT_RESULT(cluster_->StartShellMaster(rust_master_exe_));
  ASSERT_OK(WaitForRole(new_master.get(), PeerRole::NON_PARTICIPANT));

  ASSERT_OK(cluster_->ChangeConfig(new_master, consensus::ADD_SERVER));
  ASSERT_OK(WaitForRole(new_master.get(), PeerRole::FOLLOWER));
  ASSERT_NO_FATALS(RunWorkload());
  ASSERT_OK(WaitForMastersToCatchUp());

  ASSERT_OK(cluster_->ChangeConfig(new_master, consensus::REMOVE_SERVER));
}

}  // namespace yb
