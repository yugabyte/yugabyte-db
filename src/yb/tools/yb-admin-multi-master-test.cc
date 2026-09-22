// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
// Tests for the yb-admin command-line tool with multiple masters.

#include <map>
#include <regex>
#include <unordered_set>

#include <gtest/gtest.h>

#include "yb/client/client.h"

#include "yb/integration-tests/external_mini_cluster-itest-base.h"

#include "yb/tools/admin-test-base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_trim.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"

namespace yb {
namespace tools {

namespace {

static const char* const kAdminToolName = "yb-admin";

// Parses tabular yb-admin output (a header row followed by one row per entry) into one
// map per row, keyed by column header. Keying by header name rather than by position
// means adding a column to the output does not silently change which column a comparison
// ignores.
using OutputRow = std::map<std::string, std::string>;

std::vector<OutputRow> ParseTabularOutput(const std::string& output) {
  std::vector<OutputRow> rows;
  std::vector<std::string> headers;
  for (const auto& line : StringSplit(output, '\n')) {
    if (util::TrimStr(line).empty()) {
      continue;
    }
    std::vector<std::string> fields;
    for (const auto& field : StringSplit(line, '\t')) {
      fields.push_back(util::TrimStr(field));
    }
    if (headers.empty()) {
      headers = std::move(fields);
      continue;
    }
    OutputRow row;
    for (size_t i = 0; i < fields.size() && i < headers.size(); ++i) {
      row[headers[i]] = fields[i];
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<OutputRow> DropColumns(
    std::vector<OutputRow> rows, const std::unordered_set<std::string>& columns) {
  for (auto& row : rows) {
    for (const auto& col : columns) {
      row.erase(col);
    }
  }
  return rows;
}

// yb-admin talks to the current leader, so Lag(ms) is N/A for the leader's own row (the
// leader does not track itself) and a non-negative integer for every other master, which
// the leader does track.
Status CheckListAllMastersLagSemantics(
    const std::string& output, size_t expected_num_masters) {
  const auto rows = ParseTabularOutput(output);
  SCHECK_EQ(rows.size(), expected_num_masters, IllegalState, "Unexpected number of master rows");
  size_t na_count = 0;
  for (const auto& row : rows) {
    const auto lag_it = row.find("Lag(ms)");
    const auto role_it = row.find("Role");
    SCHECK(
        lag_it != row.end() && role_it != row.end(), IllegalState,
        "Missing Role or Lag(ms) column");
    if (lag_it->second == "N/A") {
      SCHECK_EQ(role_it->second, "LEADER", IllegalState, "Non-leader master reports no lag");
      ++na_count;
    } else {
      const auto lag_ms = VERIFY_RESULT(CheckedStoll(lag_it->second));
      SCHECK_GE(lag_ms, int64_t{0}, IllegalState, "Negative lag");
    }
  }
  SCHECK_EQ(
      na_count, size_t{1}, IllegalState, "Expected only the leader's own row to report no lag");
  return Status::OK();
}

// Re-runs list_all_masters until the Lag(ms) contract above holds. ListMasters iterates the
// committed Raft config while the lag values come from the leader's tracked-peer map, so
// around a config change or an election the two can briefly disagree and a master can be
// listed before the leader tracks it (reporting a second N/A). Polling keeps the assertion
// strict without depending on that timing.
void AssertListAllMastersLagSemantics(
    const std::string& master_addrs, size_t expected_num_masters) {
  std::string output;
  Status check_status;
  ASSERT_OK_PREPEND(
      LoggedWaitFor(
          [&]() -> Result<bool> {
            output = VERIFY_RESULT(RunAdminToolCommand(master_addrs, "list_all_masters"));
            check_status = CheckListAllMastersLagSemantics(output, expected_num_masters);
            return check_status.ok();
          },
          30s * kTimeMultiplier, "list_all_masters reports the expected Lag(ms) values"),
      Format("Last check: $0. Last output:\n$1", check_status, output));
}

} // namespace

class YBAdminMultiMasterTest : public ExternalMiniClusterITestBase {
 protected:
  YB_STRONGLY_TYPED_BOOL(UseUUID);

  void TestRemoveDownMaster(UseUUID use_uuid);
};

TEST_F(YBAdminMultiMasterTest, InitialMasterAddresses) {
  auto admin_path = GetToolPath(kAdminToolName);
  ASSERT_NO_FATALS(StartCluster({}, {}, 1/*num tservers*/, 3/*num masters*/));

  // Verify that yb-admin query results with --init_master_addrs match
  // query results with the full master addresses
  auto non_leader_idx = ASSERT_RESULT(cluster_->GetFirstNonLeaderMasterIndex());
  auto non_leader = cluster_->master(non_leader_idx);
  HostPort non_leader_hp = non_leader->bound_rpc_hostport();
  std::string output1;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--init_master_addrs", non_leader_hp.ToString(),
      "list_all_masters"), &output1));
  LOG(INFO) << "init_master_addrs: list_all_masters: " << output1;
  std::string output2;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "list_all_masters"), &output2));
  LOG(INFO) << "full master_addresses: list_all_masters: " << output2;
  // Lag(ms) is wall-clock based and differs between the two invocations, so compare
  // every other column by name. Adding a column after Lag(ms) will not silently
  // start comparing the volatile values again.
  const std::unordered_set<std::string> kVolatileColumns = {"Lag(ms)"};
  ASSERT_EQ(DropColumns(ParseTabularOutput(output1), kVolatileColumns),
            DropColumns(ParseTabularOutput(output2), kVolatileColumns));
  ASSERT_NO_FATALS(AssertListAllMastersLagSemantics(cluster_->GetMasterAddresses(), 3));

  output1.clear();
  output2.clear();

  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--init_master_addrs", non_leader_hp.ToString(),
      "get_universe_config"), &output1));
  // Remove the time output from list_all_tablet_servers since it doesn't match
  LOG(INFO) << "init_master_addrs: get_universe_config: " << output1;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "get_universe_config"), &output2));
  LOG(INFO) << "full master_addresses: get_universe_config: " << output2;
  ASSERT_EQ(output1, output2);
}

void YBAdminMultiMasterTest::TestRemoveDownMaster(UseUUID use_uuid) {
  const int kNumInitMasters = 3;
  const auto admin_path = GetToolPath(kAdminToolName);
  ASSERT_NO_FATALS(StartCluster({}, {}, 1/*num tservers*/, kNumInitMasters));
  const auto master_addrs = cluster_->GetMasterAddresses();
  auto idx = ASSERT_RESULT(cluster_->GetFirstNonLeaderMasterIndex());
  const auto addr = cluster_->master(idx)->bound_rpc_addr();
  const auto uuid = cluster_->master(idx)->uuid();
  ASSERT_OK(cluster_->master(idx)->Pause());

  std::string output2;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "list_all_masters"), &output2));
  LOG(INFO) << "list_all_masters \n" << output2;
  const auto lines2 = StringSplit(output2, '\n');
  ASSERT_EQ(lines2.size(), kNumInitMasters + 1);
  ASSERT_NO_FATALS(AssertListAllMastersLagSemantics(master_addrs, kNumInitMasters));

  std::string output3;
  auto args = ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(), "change_master_config",
      "REMOVE_SERVER", addr.host(), addr.port());
  if (use_uuid) {
    args.push_back(uuid);
  }
  ASSERT_OK(Subprocess::Call(args, &output3));
  LOG(INFO) << "change_master_config: REMOVE_SERVER\n" << output3;

  std::string output4;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "list_all_masters"), &output4));
  LOG(INFO) << "list_all_masters \n" << output4;
  const auto lines4 = StringSplit(output4, '\n');
  ASSERT_EQ(lines4.size(), kNumInitMasters);
  ASSERT_NO_FATALS(AssertListAllMastersLagSemantics(master_addrs, kNumInitMasters - 1));
}

TEST_F(YBAdminMultiMasterTest, RemoveDownMaster) {
  TestRemoveDownMaster(UseUUID::kFalse);
}

TEST_F(YBAdminMultiMasterTest, RemoveDownMasterByUuid) {
  TestRemoveDownMaster(UseUUID::kTrue);
}

TEST_F(YBAdminMultiMasterTest, AddShellMaster) {
  const auto admin_path = GetToolPath(kAdminToolName);
  const int kNumInitMasters = 2;
  ASSERT_NO_FATALS(StartCluster({}, {}, 1/*num tservers*/, kNumInitMasters));
  const auto master_addrs = cluster_->GetMasterAddresses();

  std::string output2;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "list_all_masters"), &output2));
  LOG(INFO) << "list_all_masters \n" << output2;
  const auto lines2 = StringSplit(output2, '\n');
  ASSERT_EQ(lines2.size(), kNumInitMasters + 1);
  ASSERT_NO_FATALS(AssertListAllMastersLagSemantics(master_addrs, kNumInitMasters));

  auto shell_master = ASSERT_RESULT(cluster_->StartShellMaster());
  ASSERT_NE(shell_master, nullptr);
  scoped_refptr<ExternalMaster> shell_master_ref(shell_master);
  const auto shell_addr = shell_master->bound_rpc_addr();

  std::string output3;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "change_master_config", "ADD_SERVER", shell_addr.host(), shell_addr.port()), &output3));
  LOG(INFO) << "change_master_config: ADD_SERVER\n" << output3;

  std::string output4;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "list_all_masters"), &output4));
  LOG(INFO) << "list_all_masters \n" << output4;
  const auto lines4 = StringSplit(output4, '\n');
  ASSERT_EQ(lines4.size(), kNumInitMasters + 2);
  ASSERT_NO_FATALS(AssertListAllMastersLagSemantics(master_addrs, kNumInitMasters + 1));
}

TEST_F(YBAdminMultiMasterTest, FlushSysCatalog) {
  const auto admin_path = GetToolPath(kAdminToolName);
  const int kNumMasters = 2;
  ASSERT_NO_FATALS(StartCluster({}, {}, 1 /* num tservers */, kNumMasters));

  LOG(INFO) << "FlushSysCatalog: master addresses: " << cluster_->GetMasterAddresses();

  // Set up a LogWaiter per master to verify the success log line.
  const auto kFlushLogMessage = "FlushSysCatalog completed: OK";
  std::vector<std::unique_ptr<LogWaiter>> log_waiters;
  for (int i = 0; i < kNumMasters; i++) {
    log_waiters.push_back(
        std::make_unique<LogWaiter>(cluster_->master(i), kFlushLogMessage));
  }

  // Default: run on every master; outputs "Successful" per master.
  std::string output;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "flush_sys_catalog"), &output));
  LOG(INFO) << "flush_sys_catalog (all masters):\n" << output;
  auto lines = StringSplit(output, '\n');
  int successful_count = 0;
  for (const auto& line : lines) {
    if (line.find("Successful") != std::string::npos) {
      successful_count++;
    }
  }
  ASSERT_EQ(successful_count, kNumMasters);

  // Verify the success log line appeared on every master.
  for (int i = 0; i < kNumMasters; i++) {
    ASSERT_OK(log_waiters[i]->WaitFor(10s));
  }

  // Set up LogWaiters again to look for future loglines.
  log_waiters.clear();
  for (int i = 0; i < kNumMasters; i++) {
    log_waiters.push_back(
        std::make_unique<LogWaiter>(cluster_->master(i), kFlushLogMessage));
  }

  // with 'leader_only': old behavior to only run on the leader.
  // No output => success.
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "flush_sys_catalog", "leader_only"), &output));
  ASSERT_TRUE(output.empty()) << "Expected empty output, got: " << output;

  // Verify the log line appeared only on the leader.
  auto leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  ASSERT_OK(log_waiters[leader_idx]->WaitFor(10s));
  for (int i = 0; i < kNumMasters; i++) {
    if (static_cast<size_t>(i) != leader_idx) {
      ASSERT_FALSE(log_waiters[i]->IsEventOccurred())
          << "FlushSysCatalog log unexpectedly found on follower master " << i;
    }
  }
}

TEST_F(YBAdminMultiMasterTest, CompactSysCatalog) {
  const auto admin_path = GetToolPath(kAdminToolName);
  const int kNumMasters = 2;
  ASSERT_NO_FATALS(StartCluster({}, {}, 1 /* num tservers */, kNumMasters));

  LOG(INFO) << "CompactSysCatalog: master addresses: " << cluster_->GetMasterAddresses();

  // Set up a LogWaiter per master to verify the success log line.
  const auto kCompactLogMessage = "CompactSysCatalog completed: OK";
  std::vector<std::unique_ptr<LogWaiter>> log_waiters;
  for (int i = 0; i < kNumMasters; i++) {
    log_waiters.push_back(
        std::make_unique<LogWaiter>(cluster_->master(i), kCompactLogMessage));
  }

  // Default: run on every master; outputs "Successful" per master.
  std::string output;
  ASSERT_OK(Subprocess::Call(ToStringVector(
      admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
      "compact_sys_catalog"), &output));
  LOG(INFO) << "compact_sys_catalog (all masters):\n" << output;
  auto lines = StringSplit(output, '\n');
  int successful_count = 0;
  for (const auto& line : lines) {
    if (line.find("Successful") != std::string::npos) {
      successful_count++;
    }
  }
  ASSERT_EQ(successful_count, kNumMasters);

  // Verify the success log line appeared on every master.
  for (int i = 0; i < kNumMasters; i++) {
    ASSERT_OK(log_waiters[i]->WaitFor(10s));
  }

  // Set up LogWaiters again to look for future loglines.
  log_waiters.clear();
  for (int i = 0; i < kNumMasters; i++) {
    log_waiters.push_back(
        std::make_unique<LogWaiter>(cluster_->master(i), kCompactLogMessage));
  }

  // with 'leader_only': old behavior to only run on the leader.
  // No output => success.
  ASSERT_OK(Subprocess::Call(ToStringVector(
    admin_path, "--master_addresses", cluster_->GetMasterAddresses(),
    "compact_sys_catalog", "leader_only"), &output));
  ASSERT_TRUE(output.empty()) << "Expected empty output, got: " << output;

  // Verify the log line appeared only on the leader.
  auto leader_idx = ASSERT_RESULT(cluster_->GetLeaderMasterIndex());
  ASSERT_OK(log_waiters[leader_idx]->WaitFor(10s));
  for (int i = 0; i < kNumMasters; i++) {
    if (static_cast<size_t>(i) != leader_idx) {
      ASSERT_FALSE(log_waiters[i]->IsEventOccurred())
          << "CompactSysCatalog log unexpectedly found on follower master " << i;
    }
  }
}

TEST_F(YBAdminMultiMasterTest, TestMasterLeaderStepdown) {
  const int kNumInitMasters = 3;
  ASSERT_NO_FATALS(StartCluster({}, {}, 1/*num tservers*/, kNumInitMasters));
  std::string out;
  auto call_admin = [
      &out,
      admin_path = GetToolPath(kAdminToolName),
      master_address = ToString(cluster_->GetMasterAddresses())] (
      const std::initializer_list<std::string>& args) mutable {
    auto cmds = ToStringVector(admin_path, "--master_addresses", master_address);
    std::copy(args.begin(), args.end(), std::back_inserter(cmds));
    return Subprocess::Call(cmds, &out);
  };
  auto regex_fetch_first = [&out](const std::string& exp) -> Result<std::string> {
    std::smatch match;
    if (!std::regex_search(out.cbegin(), out.cend(), match, std::regex(exp)) || match.size() != 2) {
      return STATUS_FORMAT(NotFound, "No pattern in '$0'", out);
    }
    return match[1];
  };

  ASSERT_OK(call_admin({"list_all_masters"}));
  const auto new_leader_id = ASSERT_RESULT(
      regex_fetch_first(R"(\s+([a-z0-9]{32})\s+\S+\s+\S+\s+FOLLOWER)"));
  ASSERT_OK(call_admin({"master_leader_stepdown", new_leader_id}));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    RETURN_NOT_OK(call_admin({"list_all_masters"}));
    return new_leader_id ==
        VERIFY_RESULT(regex_fetch_first(R"(\s+([a-z0-9]{32})\s+\S+\s+\S+\s+LEADER)"));
  }, 5s, "Master leader stepdown"));

  ASSERT_OK(call_admin({"master_leader_stepdown"}));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    RETURN_NOT_OK(call_admin({"list_all_masters"}));
    return new_leader_id !=
      VERIFY_RESULT(regex_fetch_first(R"(\s+([a-z0-9]{32})\s+\S+\s+\S+\s+LEADER)"));
  }, 5s, "Master leader stepdown"));
}

}  // namespace tools
}  // namespace yb
