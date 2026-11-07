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

// The Rust master (yb-master-rs) in a quorum of C++ masters. As a follower it joins, keeps the same
// sys catalog and the same in-memory catalog as the C++ leader through DDL, snapshots and a PITR
// restore, answers the follower-servable RPCs, and never claims to lead. With
// --TEST_rust_master_allow_leadership it leads and serves the core cluster, client and heartbeat
// RPCs (src/yb/rust/master/docs/testing.md). YSQL is off: a layer-2 Rust leader does not serve it.

#include <algorithm>
#include <map>
#include <random>
#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/table.h"
#include "yb/client/tablet_server.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/gutil/strings/escaping.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_rust_peer.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/path_util.h"
#include "yb/util/status_log.h"
#include "yb/util/string_case.h"
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
    // These tests start from a C++ quorum, whatever YB_RUST_MASTER says.
    opts.rust_masters = RustMasters::kNone;
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

  Result<size_t> RestartFollowerAsRust(
      const std::vector<std::pair<std::string, std::string>>& extra_flags = {}) {
    const auto leader = VERIFY_RESULT(cluster_->GetLeaderMasterIndex());
    const size_t index = (leader + 1) % cluster_->num_masters();
    auto* master = cluster_->master(index);
    master->Shutdown();
    master->SetExe(rust_master_exe_);
    for (const auto& [flag, value] : extra_flags) {
      master->AddExtraFlag(flag, value);
    }
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

  // Waits for a master to lead and report its leader services ready.
  Status WaitForLeaderReady() {
    return WaitFor(
        [&]() -> Result<bool> {
          auto leader = cluster_->GetLeaderMasterIndex();
          if (!leader.ok()) {
            return false;
          }
          auto proxy = cluster_->GetProxy<master::MasterClusterProxy>(cluster_->master(*leader));
          master::IsMasterLeaderReadyRequestPB req;
          master::IsMasterLeaderReadyResponsePB resp;
          rpc::RpcController controller;
          controller.set_timeout(10s);
          return proxy.IsMasterLeaderServiceReady(req, &resp, &controller).ok() &&
                 !resp.has_error();
        },
        kTimeout, "a ready master leader");
  }

  // Waits until every master has committed everything the leader wrote. Committed is not yet
  // applied: to compare catalogs, use DumpQuietCatalog.
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

  // The page at `path` on the master's web port, or the error for a status other than 200.
  Result<std::string> FetchPage(ExternalMaster* master, const std::string& path) {
    EasyCurl curl;
    faststring buf;
    RETURN_NOT_OK(curl.FetchURL(
        Format("http://$0:$1$2", master->bound_rpc_hostport().host(), master->http_port(), path),
        &buf));
    return buf.ToString();
  }

  // The Rust master's in-memory catalog, once it has loaded one.
  Result<master::DumpCatalogResponsePB> DumpLoadedCatalog(ExternalMaster* master) {
    master::DumpCatalogResponsePB result;
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto catalog = DumpCatalog(master);
          if (!catalog.ok() || catalog->rows().empty()) {
            return false;
          }
          result = std::move(*catalog);
          return true;
        },
        kTimeout, "the Rust master to load its catalog"));
    return result;
  }

  // The Rust master's in-memory catalog.
  Result<master::DumpCatalogResponsePB> DumpCatalog(ExternalMaster* master) {
    auto proxy = cluster_->GetProxy<master::MasterRustPeerProxy>(master);
    master::DumpCatalogRequestPB req;
    master::DumpCatalogResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(30s);
    RETURN_NOT_OK(proxy.DumpCatalog(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp;
  }

  // Rows as `sys-catalog-tool dump-rows` writes them, without its last column.
  static std::vector<std::string> RowLines(const master::DumpCatalogResponsePB& dump) {
    std::vector<std::string> rows;
    for (const auto& row : dump.rows()) {
      rows.push_back(Format(
          "$0\t$1\t$2", master::SysRowEntryType_Name(row.entry_type()), b2a_hex(row.id()),
          b2a_hex(row.data())));
    }
    std::sort(rows.begin(), rows.end());
    return rows;
  }

  // The Rust master's catalog at a point where the leader wrote nothing between before and after
  // the dump, so it is the leader's catalog as long as nothing writes until shutdown.
  Result<master::DumpCatalogResponsePB> DumpQuietCatalog(ExternalMaster* rust_master) {
    for (int attempt = 0; attempt != 30; ++attempt) {
      OpIdPB before;
      RETURN_NOT_OK(cluster_->GetLastOpIdForLeader(&before));
      RETURN_NOT_OK(cluster_->WaitForMastersToCommitUpTo(before.index()));
      // Committed is not yet applied.
      SleepFor(1s);
      auto dump = VERIFY_RESULT(DumpCatalog(rust_master));
      OpIdPB after;
      RETURN_NOT_OK(cluster_->GetLastOpIdForLeader(&after));
      if (before.index() == after.index()) {
        return dump;
      }
    }
    return STATUS(TimedOut, "The leader kept writing");
  }

  // Hands sys catalog leadership to `master` and waits until it reports LEADER.
  Status TransferLeadershipTo(ExternalMaster* master) {
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          tserver::TabletServerErrorPB::Code error_code;
          auto status = cluster_->StepDownMasterLeader(&error_code, master->uuid());
          if (status.ok()) {
            return true;
          }
          if (status.ToString().find("not caught up") != std::string::npos ||
              status.IsLeaderNotReadyToServe() || status.IsServiceUnavailable()) {
            return false;
          }
          return status;
        },
        kTimeout, "the leader to hand over to " + master->uuid()));
    return WaitForRole(master, PeerRole::LEADER);
  }

  // Creates, drops and alters YCQL tables through the leader, choosing with `seed`.
  void RandomDdlWorkload(uint32_t seed, int ops, const std::string& prefix) {
    LOG(INFO) << "Random DDL workload, seed " << seed;
    std::mt19937 rng(seed);
    std::vector<client::YBTableName> tables;
    for (int i = 0; i != ops; ++i) {
      const auto op = rng() % 4;
      if (op == 1 && !tables.empty()) {
        const auto victim = rng() % tables.size();
        ASSERT_OK(client_->DeleteTable(tables[victim], /* wait= */ true));
        tables.erase(tables.begin() + victim);
      } else if (op == 2 && !tables.empty()) {
        auto alterer = client_->NewTableAlterer(tables[rng() % tables.size()]);
        alterer->AddColumn(Format("c$0", i))->Type(DataType::INT32);
        ASSERT_OK(alterer->Alter());
      } else {
        client::TableHandle table;
        const client::YBTableName name(
            YQL_DATABASE_CQL, client::kTableName.namespace_name(),
            Format("$0_$1", prefix, i));
        client::kv_table_test::CreateTable(
            client::Transactional(rng() % 2), /* num_tablets= */ 1 + rng() % 3, client_.get(),
            &table, name);
        tables.push_back(name);
      }
    }
  }

  // The leader's tables' locations, from the given master.
  Result<master::GetTableLocationsResponsePB> TableLocations(
      ExternalMaster* master, const client::YBTableName& table) {
    auto proxy = cluster_->GetProxy<master::MasterClientProxy>(master);
    master::GetTableLocationsRequestPB req;
    table.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(100);
    master::GetTableLocationsResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    RETURN_NOT_OK(proxy.GetTableLocations(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp;
  }

  Result<master::ListTabletServersResponsePB> ListTabletServers(ExternalMaster* master) {
    auto proxy = cluster_->GetProxy<master::MasterClusterProxy>(master);
    master::ListTabletServersRequestPB req;
    master::ListTabletServersResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    RETURN_NOT_OK(proxy.ListTabletServers(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp;
  }

  // Waits until every tablet of `table` has a leader in `master`'s locations.
  Status WaitForLeaders(ExternalMaster* master, const client::YBTableName& table, int tablets) {
    return WaitFor(
        [&]() -> Result<bool> {
          auto resp = TableLocations(master, table);
          if (!resp.ok() || resp->tablet_locations_size() != tablets) {
            return false;
          }
          for (const auto& tablet : resp->tablet_locations()) {
            if (tablet.stale() || tablet.replicas_size() != 3) {
              return false;
            }
            if (std::none_of(
                    tablet.replicas().begin(), tablet.replicas().end(),
                    [](const auto& r) { return r.role() == PeerRole::LEADER; })) {
              return false;
            }
          }
          return true;
        },
        kTimeout, "tablet leaders in the locations of " + master->uuid());
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
  // The leader refuses until the restarted peer has caught up.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        tserver::TabletServerErrorPB::Code error_code;
        auto status = cluster_->StepDownMasterLeader(&error_code, rust_master->uuid());
        if (status.ok()) {
          return true;
        }
        if (status.ToString().find("not caught up") != std::string::npos) {
          return false;
        }
        return status;
      },
      kTimeout, "the leader to hand over to the Rust master"));
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

  // Served on every master, as AreNodesSafeToTakeDown expects.
  {
    auto proxy = cluster_->GetProxy<master::MasterClusterProxy>(new_master.get());
    master::CheckMasterTabletHealthRequestPB req;
    master::CheckMasterTabletHealthResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    ASSERT_OK(proxy.CheckMasterTabletHealth(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    ASSERT_EQ(resp.role(), PeerRole::FOLLOWER);
  }

  // The leader tells the removed master (RemovedMasterUpdate), which goes into shell mode.
  ASSERT_OK(cluster_->ChangeConfig(new_master, consensus::REMOVE_SERVER));
  ASSERT_OK(WaitForRole(new_master.get(), PeerRole::NON_PARTICIPANT));
}

namespace {

// A page without what differs between two fetches: uptimes and the footer, which has each
// binary's build time.
std::string ComparablePage(const std::string& page) {
  static const std::regex kUptime("<td> [^<]*[0-9]+:[0-9]{2}:[0-9]{2}</td>");
  return std::regex_replace(page.substr(0, page.find("<footer")), kUptime, "<td>uptime</td>");
}

}  // namespace

// The Rust master serves the web port; the pages are those a C++ follower serves.
TEST_F(RustMasterTest, WebPages) {
  const auto rust_index = ASSERT_RESULT(RestartFollowerAsRust());
  const auto leader_index = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  auto* rust_master = cluster_->master(rust_index);
  auto* cpp_follower = cluster_->master(3 - rust_index - leader_index);
  client::TableHandle table;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /* num_tablets= */ 1, client_.get(), &table);

  // Served by both followers from the master list, or fetched from the leader.
  for (const auto& path : {
           "/api/v1/masters"s, "/api/v1/version"s, "/api/v1/tables"s, "/api/v1/namespaces"s,
           "/dump-entities"s, "/"s, "/tables"s}) {
    SCOPED_TRACE(path);
    auto rust_page = FetchPage(rust_master, path);
    auto cpp_page = FetchPage(cpp_follower, path);
    ASSERT_OK(rust_page);
    ASSERT_OK(cpp_page);
    ASSERT_EQ(ComparablePage(*rust_page), ComparablePage(*cpp_page));
  }

  // Each master shows itself in bold.
  auto unbold = [](std::string page) {
    for (const auto& tag : {"<b>"s, "</b>"s}) {
      for (auto pos = page.find(tag); pos != std::string::npos; pos = page.find(tag, pos)) {
        page.erase(pos, tag.size());
      }
    }
    return page;
  };
  const auto rust_masters = ASSERT_RESULT(FetchPage(rust_master, "/masters"));
  ASSERT_STR_CONTAINS(
      rust_masters, Format("<b><a href=\"http://localhost:$0/\">", rust_master->http_port()));
  ASSERT_EQ(
      ComparablePage(unbold(rust_masters)),
      ComparablePage(unbold(ASSERT_RESULT(FetchPage(cpp_follower, "/masters")))));

  // Heartbeat times differ between fetches.
  ASSERT_STR_CONTAINS(
      ASSERT_RESULT(FetchPage(rust_master, "/api/v1/tablet-servers")), "\"status\":\"ALIVE\"");

  // A follower answers 503.
  auto is_leader = FetchPage(rust_master, "/api/v1/is-leader");
  ASSERT_NOK(is_leader);
  ASSERT_STR_CONTAINS(is_leader.status().ToString(), "HTTP 503");

  // The host's own pages, through the proxy.
  ASSERT_STR_CONTAINS(ASSERT_RESULT(FetchPage(rust_master, "/varz?raw")), "--webserver_port");
  ASSERT_STR_CONTAINS(
      ASSERT_RESULT(FetchPage(rust_master, "/prometheus-metrics")), "handler_latency");

  // The registration advertises the Rust web port.
  auto registration = ASSERT_RESULT(Registration(rust_master));
  ASSERT_EQ(registration.registration().http_addresses_size(), 1);
  ASSERT_EQ(registration.registration().http_addresses(0).port(), rust_master->http_port());
}

// The Rust master serves HTTPS with the C++ web server's TLS flags.
TEST_F(RustMasterTest, WebServerHttps) {
  const auto certs_dir = GetCertsDir();
  const auto rust_index = ASSERT_RESULT(RestartFollowerAsRust({
      {"webserver_interface", "127.0.0.2"},
      {"webserver_certificate_file", JoinPathSegments(certs_dir, "node.127.0.0.2.crt")},
      {"webserver_private_key_file", JoinPathSegments(certs_dir, "node.127.0.0.2.key")},
      {"webserver_strict_transport_security", "true"},
  }));
  auto* rust_master = cluster_->master(rust_index);
  const auto url = Format("https://127.0.0.2:$0", rust_master->http_port());

  EasyCurl curl;
  curl.set_ca_cert(JoinPathSegments(certs_dir, "ca.crt"));
  curl.set_return_headers(true);
  faststring buf;
  ASSERT_OK(curl.FetchURL(url + "/api/v1/version", &buf));
  ASSERT_STR_CONTAINS(ToLowerCase(buf.ToString()), "strict-transport-security: max-age=31536000");
  // Fetched from the C++ leader, which serves HTTP.
  ASSERT_OK(curl.FetchURL(url + "/api/v1/tables", &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), "\"tables\"");

  EasyCurl plain;
  ASSERT_NOK(plain.FetchURL(Format("http://127.0.0.2:$0/api/v1/version", rust_master->http_port()),
                            &buf));

  auto registration = ASSERT_RESULT(Registration(rust_master));
  ASSERT_EQ(registration.registration().http_addresses_size(), 1);
  ASSERT_EQ(registration.registration().http_addresses(0).host(), "127.0.0.2");
}

// master-tserver-sim registers fake tservers with the leader of a quorum that has a Rust
// follower. Only registration and heartbeats: fake replicas cannot share Raft groups with the
// cluster's real tservers, so the run creates no tables.
TEST_F(RustMasterTest, TserverSimulator) {
  ASSERT_RESULT(RestartFollowerAsRust());
  constexpr int kFakeTservers = 10;
  std::string output;
  ASSERT_OK(Subprocess::Call(
      {BinaryPath("master-tserver-sim"),
       "--tserver_sim_master_addresses=" + cluster_->GetMasterAddresses(),
       "--tserver_sim_first_ip=127.250.0.1",
       Format("--tserver_sim_rpc_port=$0", cluster_->AllocateFreePort()),
       Format("--tserver_sim_num_tservers=$0", kFakeTservers),
       "--tserver_sim_heartbeat_interval_ms=100", "--tserver_sim_steady_state_sec=1",
       "--tserver_sim_num_tables=0"},
      &output));
  ASSERT_STR_CONTAINS(output, Format("\"num_tservers\": $0", kFakeTservers));
  const auto tservers = ASSERT_RESULT(client_->ListTabletServers());
  ASSERT_EQ(tservers.size(), cluster_->num_tablet_servers() + kFakeTservers);
}

// The Rust follower's in-memory catalog, built from the apply stream, equals the C++ leader's sys
// catalog row for row after randomized DDL, a snapshot schedule and a PITR restore (which reloads
// the catalog).
TEST_F(RustMasterTest, FollowerCatalogMatchesRandomWorkload) {
  const auto rust_index = ASSERT_RESULT(RestartFollowerAsRust(
      {{"rust_master_catalog_self_check_interval_ms", "1000"}}));
  auto* rust_master = cluster_->master(rust_index);

  const auto seed = static_cast<uint32_t>(GetCurrentTimeMicros());
  ASSERT_NO_FATALS(RandomDdlWorkload(seed, 20, "before"));
  ASSERT_NO_FATALS(RunWorkload());
  // Other names: the restore may have brought back tables of the first pass.
  ASSERT_NO_FATALS(RandomDdlWorkload(seed + 1, 10, "after"));

  // Tablet reports and snapshot schedules keep the leader writing; stop the tservers so it goes
  // quiet.
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    cluster_->tablet_server(i)->Shutdown();
  }
  const auto dump = ASSERT_RESULT(DumpQuietCatalog(rust_master));
  // The restore reloaded the catalog; the self-check never disagreed with the tablet.
  ASSERT_GE(dump.generation(), 2);
  ASSERT_EQ(dump.self_check_mismatches(), 0);

  const auto leader_index = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  auto* leader = cluster_->master(leader_index);
  cluster_->Shutdown();
  const auto leader_rows = ASSERT_RESULT(DumpRows(leader));
  ASSERT_EQ(RowLines(dump), leader_rows) << "seed " << seed;
  ASSERT_EQ(ASSERT_RESULT(DumpRows(rust_master)), leader_rows);
}

// With leadership allowed, the Rust master leads and serves the core cluster, client and heartbeat
// RPCs to real tservers; its writes read back the same on C++ masters after a failback.
TEST_F(RustMasterTest, RustLeaderServesCoreRpcs) {
  client::TableHandle table;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /* num_tablets= */ 3, client_.get(), &table);
  const auto rust_index = ASSERT_RESULT(RestartFollowerAsRust(
      {{"TEST_rust_master_allow_leadership", "true"}}));
  auto* rust_master = cluster_->master(rust_index);
  ASSERT_OK(WaitForMastersToCatchUp());
  ASSERT_OK(TransferLeadershipTo(rust_master));

  // Heartbeats: every tserver registers with the Rust leader and sends a full report.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto resp = ListTabletServers(rust_master);
        if (!resp.ok()) {
          return false;
        }
        return std::count_if(
                   resp->servers().begin(), resp->servers().end(),
                   [](const auto& s) { return s.alive(); }) ==
               static_cast<ssize_t>(cluster_->num_tablet_servers());
      },
      kTimeout, "the tservers to register with the Rust leader"));
  ASSERT_OK(WaitForLeaders(rust_master, table.name(), 3));

  // Tablets of user tables are mapped by parent id: their table ids come from the catalog, not the
  // row, and clients need them to cache the locations.
  {
    auto locations = ASSERT_RESULT(TableLocations(rust_master, table.name()));
    for (const auto& tablet : locations.tablet_locations()) {
      ASSERT_TRUE(std::ranges::find(tablet.table_ids(), table->id()) != tablet.table_ids().end())
          << tablet.ShortDebugString();
    }
  }

  // Web pages only the leader serves answer at once instead of fetching from this master again.
  ASSERT_OK(FetchPage(rust_master, "/api/v1/is-leader"));
  ASSERT_NOK(FetchPage(rust_master, "/tablet-servers"));

  {
    auto proxy = cluster_->GetProxy<master::MasterClusterProxy>(rust_master);
    master::IsMasterLeaderReadyRequestPB req;
    master::IsMasterLeaderReadyResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    ASSERT_OK(proxy.IsMasterLeaderServiceReady(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
  }

  // A cluster config change through the Rust leader.
  int32_t new_version;
  {
    auto proxy = cluster_->GetProxy<master::MasterClusterProxy>(rust_master);
    master::GetMasterClusterConfigRequestPB get_req;
    master::GetMasterClusterConfigResponsePB get_resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    ASSERT_OK(proxy.GetMasterClusterConfig(get_req, &get_resp, &controller));
    ASSERT_FALSE(get_resp.has_error()) << get_resp.ShortDebugString();
    master::ChangeMasterClusterConfigRequestPB req;
    *req.mutable_cluster_config() = get_resp.cluster_config();
    auto* host = req.mutable_cluster_config()->mutable_server_blacklist()->add_hosts();
    host->set_host("127.0.0.250");
    host->set_port(9100);
    master::ChangeMasterClusterConfigResponsePB resp;
    controller.Reset();
    ASSERT_OK(proxy.ChangeMasterClusterConfig(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    new_version = get_resp.cluster_config().version() + 1;
  }

  // The catalog writer's metrics, registered from Rust, are in the server's metric entity.
  {
    const auto metrics = ASSERT_RESULT(FetchPage(rust_master, "/prometheus-metrics"));
    for (const auto* name :
         {"rust_master_catalog_writer_queue_wait_us",
          "rust_master_catalog_writer_commit_latency_us",
          "rust_master_catalog_writer_batch_rows",
          "rust_master_catalog_writer_queue_depth",
          "rust_master_catalog_writer_conflicts",
          "rust_master_catalog_writer_expired"}) {
      ASSERT_STR_CONTAINS(metrics, name);
    }
  }

  // A leader method of a later layer is refused, not forwarded.
  {
    auto proxy = cluster_->GetProxy<master::MasterDdlProxy>(rust_master);
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    ASSERT_OK(proxy.ListTables(req, &resp, &controller));
    ASSERT_EQ(resp.error().status().code(), AppStatusPB::NOT_SUPPORTED)
        << resp.ShortDebugString();
  }

  // The tservers keep serving through the Rust leader; then fail back to C++.
  const auto leader_index = (rust_index + 1) % cluster_->num_masters();
  auto* cpp_master = cluster_->master(leader_index);
  ASSERT_OK(TransferLeadershipTo(cpp_master));
  ASSERT_OK(WaitForLeaders(cpp_master, table.name(), 3));
  {
    auto proxy = cluster_->GetProxy<master::MasterClusterProxy>(cpp_master);
    master::GetMasterClusterConfigRequestPB req;
    master::GetMasterClusterConfigResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s);
    ASSERT_OK(proxy.GetMasterClusterConfig(req, &resp, &controller));
    ASSERT_EQ(resp.cluster_config().version(), new_version);
    ASSERT_EQ(resp.cluster_config().server_blacklist().hosts_size(), 1);
  }

  ASSERT_OK(WaitForMastersToCatchUp());
  cluster_->Shutdown();
  const auto cpp_rows = ASSERT_RESULT(DumpRows(cpp_master));
  ASSERT_EQ(ASSERT_RESULT(DumpRows(rust_master)), cpp_rows);
}

// Two Rust masters: the leader pushes locations and tservers to the other, which serves them
// right after a failover, before any tserver reports to it.
TEST_F(RustMasterTest, WarmFailover) {
  client::TableHandle table;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /* num_tablets= */ 2, client_.get(), &table);
  const auto leader_index = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  std::vector<ExternalMaster*> rust_masters;
  for (size_t i = 0; i != cluster_->num_masters(); ++i) {
    if (i == leader_index) {
      continue;
    }
    auto* master = cluster_->master(i);
    master->Shutdown();
    master->SetExe(rust_master_exe_);
    master->AddExtraFlag("TEST_rust_master_allow_leadership", "true");
    ASSERT_OK(master->Restart());
    ASSERT_OK(WaitForRole(master, PeerRole::FOLLOWER));
    rust_masters.push_back(master);
  }
  ASSERT_OK(WaitForMastersToCatchUp());
  ASSERT_OK(TransferLeadershipTo(rust_masters[0]));
  ASSERT_OK(WaitForLeaders(rust_masters[0], table.name(), 2));

  // The follower holds the leader's locations and tservers.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto dump = DumpCatalog(rust_masters[1]);
        return dump.ok() && dump->num_tablet_locations() >= 2 &&
               dump->num_tablet_replicas() >= 3 * dump->num_tablet_locations() &&
               dump->num_live_tservers() == cluster_->num_tablet_servers();
      },
      kTimeout, "the location stream to reach the Rust follower"));

  // Stop the tservers so nothing reports to the new leader, then fail over.
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    cluster_->tablet_server(i)->Shutdown();
  }
  ASSERT_OK(TransferLeadershipTo(rust_masters[1]));
  auto locations = ASSERT_RESULT(TableLocations(rust_masters[1], table.name()));
  ASSERT_EQ(locations.tablet_locations_size(), 2);
  for (const auto& tablet : locations.tablet_locations()) {
    ASSERT_FALSE(tablet.stale()) << tablet.ShortDebugString();
    ASSERT_EQ(tablet.replicas_size(), 3);
  }
  auto servers = ASSERT_RESULT(ListTabletServers(rust_masters[1]));
  ASSERT_EQ(servers.servers_size(), cluster_->num_tablet_servers());
}

// Tservers echo the version of the live tserver list they hold, and the Rust leader answers
// "unchanged" instead of the list until membership changes.
TEST_F(RustMasterTest, LiveTserverListVersion) {
  const auto rust_index = ASSERT_RESULT(RestartFollowerAsRust({
      {"TEST_rust_master_allow_leadership", "true"},
      {"tserver_unresponsive_timeout_ms", "5000"},
  }));
  auto* rust_master = cluster_->master(rust_index);
  ASSERT_OK(WaitForMastersToCatchUp());
  ASSERT_OK(TransferLeadershipTo(rust_master));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto dump = DumpCatalog(rust_master);
        return dump.ok() && dump->num_live_tservers() == cluster_->num_tablet_servers() &&
               dump->live_tservers_unchanged_responses() > 10;
      },
      kTimeout, "heartbeats answered with an unchanged list"));

  // A tserver goes away: the others get the new list and keep heartbeating.
  cluster_->tablet_server(0)->Shutdown();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto dump = DumpCatalog(rust_master);
        return dump.ok() && dump->num_live_tservers() == cluster_->num_tablet_servers() - 1;
      },
      kTimeout, "the stopped tserver to become unresponsive"));
  const auto before = ASSERT_RESULT(DumpCatalog(rust_master)).live_tservers_unchanged_responses();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto dump = DumpCatalog(rust_master);
        return dump.ok() && dump->live_tservers_unchanged_responses() > before + 4;
      },
      kTimeout, "the remaining tservers to hold the new list"));
}

namespace {

constexpr std::string_view kSystemNamespaceIds[] = {
    "00000000000000000000000000000001", "00000000000000000000000000000002",
    "00000000000000000000000000000003"};

template <class PB>
Result<PB> ParseRow(const master::DumpCatalogResponsePB::Row& row) {
  PB pb;
  SCHECK(pb.ParseFromString(row.data()), Corruption, "Unparsable row");
  return pb;
}

// The rows every new universe gets (K10), keyed so that two universes compare: random ids,
// uuids and the password salt are left out.
Result<std::map<std::string, std::string>> DefaultRows(
    const master::DumpCatalogResponsePB& catalog) {
  using master::SysRowEntryType;
  std::map<std::string, std::string> tables;  // table id -> "<namespace id>/<name>"
  for (const auto& row : catalog.rows()) {
    if (row.entry_type() == SysRowEntryType::TABLE) {
      auto table = VERIFY_RESULT(ParseRow<master::SysTablesEntryPB>(row));
      if (std::ranges::find(kSystemNamespaceIds, table.namespace_id()) !=
          std::end(kSystemNamespaceIds)) {
        tables[row.id()] = table.namespace_id() + "/" + table.name();
      }
    }
  }
  std::map<std::string, std::string> result;
  for (const auto& row : catalog.rows()) {
    const auto type = SysRowEntryType_Name(row.entry_type());
    switch (row.entry_type()) {
      case SysRowEntryType::SYS_CONFIG:
      case SysRowEntryType::XCLUSTER_CONFIG:
      case SysRowEntryType::PERSISTENCE_BLOCK_MAP:
        result[type + "/" + row.id()] = row.data();
        break;
      case SysRowEntryType::NAMESPACE:
        if (std::ranges::find(kSystemNamespaceIds, row.id()) != std::end(kSystemNamespaceIds)) {
          result[type + "/" + row.id()] = row.data();
        }
        break;
      case SysRowEntryType::TABLE:
        if (tables.contains(row.id())) {
          result[type + "/" + tables[row.id()]] = row.data();
        }
        break;
      case SysRowEntryType::TABLET: {
        auto tablet = VERIFY_RESULT(ParseRow<master::SysTabletsEntryPB>(row));
        if (!tables.contains(tablet.table_id())) {
          break;
        }
        const auto name = tables[tablet.table_id()];
        SCHECK_EQ(tablet.table_ids_size(), 1, IllegalState, tablet.ShortDebugString());
        SCHECK_EQ(tablet.table_ids(0), tablet.table_id(), IllegalState, tablet.ShortDebugString());
        tablet.clear_table_id();
        tablet.clear_table_ids();
        result[type + "/" + name] = tablet.ShortDebugString();
        break;
      }
      case SysRowEntryType::ROLE: {
        auto role = VERIFY_RESULT(ParseRow<master::SysRoleEntryPB>(row));
        SCHECK(!role.salted_hash().empty(), IllegalState, "Role without a password");
        role.clear_salted_hash();
        result[type + "/" + row.id()] = role.ShortDebugString();
        break;
      }
      case SysRowEntryType::CLUSTER_CONFIG: {
        auto config = VERIFY_RESULT(ParseRow<master::SysClusterConfigEntryPB>(row));
        SCHECK(!config.cluster_uuid().empty(), IllegalState, "No cluster uuid");
        config.clear_cluster_uuid();
        config.clear_universe_uuid();
        result[type] = config.ShortDebugString();
        break;
      }
      default:
        break;
    }
  }
  return result;
}

}  // namespace

// A quorum of Rust masters creates a universe with the same default rows as C++ masters: the
// system namespaces and tables with their tablets, the default role and the configs.
TEST_F(RustMasterTest, CreatesUniverseLikeCpp) {
  client_.reset();
  cluster_->Shutdown();
  auto new_cluster = [this](RustMasters rust_masters) -> Status {
    ExternalMiniClusterOptions opts;
    opts.num_masters = 3;
    opts.num_tablet_servers = 0;
    opts.replication_factor = 3;
    opts.enable_ysql = false;
    opts.rust_masters = rust_masters;
    opts.cluster_id = Format("cluster-$0", ToString(rust_masters));
    cluster_ = std::make_unique<ExternalMiniCluster>(opts);
    return cluster_->Start();
  };

  // The C++ masters' rows, as a Rust follower holds them.
  ASSERT_OK(new_cluster(RustMasters::kNone));
  ASSERT_OK(WaitForLeaderReady());
  const auto follower = ASSERT_RESULT(RestartFollowerAsRust());
  ASSERT_OK(DumpLoadedCatalog(cluster_->master(follower)));
  const auto cpp_rows = ASSERT_RESULT(DefaultRows(
      ASSERT_RESULT(DumpQuietCatalog(cluster_->master(follower)))));
  cluster_->Shutdown();

  ASSERT_OK(new_cluster(RustMasters::kAll));
  ASSERT_OK(WaitForLeaderReady());
  const auto leader = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  ASSERT_OK(WaitForMastersToCatchUp());
  const auto leader_catalog = ASSERT_RESULT(DumpLoadedCatalog(cluster_->master(leader)));
  const auto rust_rows = ASSERT_RESULT(DefaultRows(leader_catalog));
  ASSERT_EQ(rust_rows.size(), 3 + 3 + 2 * 17 + 4);
  ASSERT_EQ(rust_rows, cpp_rows);

  // Written once, through Raft: every master holds the same rows.
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    ASSERT_OK(DumpLoadedCatalog(cluster_->master(i)));
    const auto catalog = ASSERT_RESULT(DumpQuietCatalog(cluster_->master(i)));
    ASSERT_EQ(catalog.rows_size(), leader_catalog.rows_size()) << "master " << i;
  }
}

}  // namespace yb
