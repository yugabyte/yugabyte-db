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
// Tests for the yb-admin command-line tool.

#include <regex>

#include <boost/algorithm/string.hpp>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table_creator.h"

#include "yb/common/json_util.h"
#include "yb/common/transaction.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/test_workload.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster_client.h"
#include "yb/master/master_defaults.h"

#include "yb/tools/admin-test-base.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/date_time.h"
#include "yb/util/format.h"
#include "yb/util/json_document.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_format.h"
#include "yb/util/subprocess.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

using yb::common::GetMemberAsArray;
using yb::common::GetMemberAsStr;
using yb::common::PrettyWriteRapidJsonToString;

namespace yb {
namespace tools {

using client::YBClientBuilder;
using client::YBTableName;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTableCreator;
using client::YBTableType;
using std::shared_ptr;
using std::vector;
using std::string;
using std::unordered_map;
using itest::TabletServerMap;
using itest::TServerDetails;
using strings::Substitute;

namespace {

//  Helper to check hosts list by requesting cluster config via yb-admin and parse its output:
//
//  Config:
//  version: 1
//  server_blacklist {
//    hosts {
//      host: "node1"
//      port: 9100
//    }
//    hosts {
//      host: "node2"
//      port: 9100
//    }
//    initial_replica_load: 0
//  }
//
class BlacklistChecker {
 public:
  BlacklistChecker(const string& yb_admin_exe, const string& master_address) :
      args_{yb_admin_exe, "--master_addresses", master_address, "get_universe_config"} {
  }

  Status operator()(const vector<HostPort>& servers) const {
    string out;
    RETURN_NOT_OK(Subprocess::Call(args_, &out));
    boost::erase_all(out, "\n");
    JsonDocument doc;
    auto root = VERIFY_RESULT(doc.Parse(out));

    auto blacklistEntries = VERIFY_RESULT(root["serverBlacklist"]["hosts"].GetArray());

    for (const auto &entry : blacklistEntries) {
      auto host = VERIFY_RESULT(entry["host"].GetString());
      auto port = VERIFY_RESULT(entry["port"].GetInt32());
      HostPort blacklistServer(host, port);
      if (std::find(servers.begin(), servers.end(), blacklistServer) ==
          servers.end()) {
        return STATUS_FORMAT(NotFound,
                             "Item $0 not found in list of expected hosts $1",
                             blacklistServer, servers);
      }
    }

    if (blacklistEntries.size() != servers.size()) {
      return STATUS_FORMAT(NotFound, "$0 items expected but $1 found",
                           servers.size(), blacklistEntries.size());
    }

    return Status::OK();
  }

 private:
  vector<string> args_;
};

Result<std::string> ReadFileToString(const std::string& file_path) {
  faststring contents;
  RETURN_NOT_OK(yb::ReadFileToString(Env::Default(), file_path, &contents));
  return contents.ToString();
}

} // namespace

const auto kRollbackAutoFlagsCmd = "rollback_auto_flags";
const auto kPromoteAutoFlagsCmd = "promote_auto_flags";
const auto kClusterConfigEntryTypeName =
    master::SysRowEntryType_Name(master::SysRowEntryType::CLUSTER_CONFIG);
const auto kTableEntryTypeName = master::SysRowEntryType_Name(master::SysRowEntryType::TABLE);

YB_STRONGLY_TYPED_BOOL(EmergencyRepairMode);

class AdminCliTest : public AdminTestBase {
 public:
  Result<std::string> GetClusterUuid() {
    master::GetMasterClusterConfigRequestPB config_req;
    master::GetMasterClusterConfigResponsePB config_resp;
    rpc::RpcController rpc;
    rpc.set_timeout(30s);
    RETURN_NOT_OK(
        cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>().GetMasterClusterConfig(
            config_req, &config_resp, &rpc));
    if (config_resp.has_error()) {
      return StatusFromPB(config_resp.error().status());
    }
    return config_resp.cluster_config().cluster_uuid();
  }

  Status RestartMaster(EmergencyRepairMode mode) {
    const auto kEmergencyRepairModeFlag = "emergency_repair_mode";
    auto* leader_master = cluster_->GetLeaderMaster();
    if (mode) {
      leader_master->AddExtraFlag(kEmergencyRepairModeFlag, "true");
    } else {
      leader_master->RemoveExtraFlag(kEmergencyRepairModeFlag);
    }

    leader_master->Shutdown();
    return leader_master->Restart();
  }

  std::string GetTempDir() { return *tmp_dir_; }

  // Dump the cluster config catalog_entry and verify that the dump contains the correct
  // cluster_uuid.
  Status TestDumpSysCatalogEntry(const std::string& cluster_uuid);

  TmpDirProvider tmp_dir_;
};

// Test yb-admin config change while running a workload.
// 1. Instantiate external mini cluster with 3 TS.
// 2. Create table with 2 replicas.
// 3. Invoke yb-admin CLI to invoke a config change.
// 4. Wait until the new server bootstraps.
// 5. Profit!
TEST_F(AdminCliTest, TestChangeConfig) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 3;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 2;

  std::vector<std::string> master_flags = {
    "--catalog_manager_wait_for_new_tablets_to_elect_leader=false"s,
    "--use_create_table_leader_hint=false"s,
  };
  std::vector<std::string> ts_flags = {
    "--enable_leader_failure_detection=false"s,
  };
  BuildAndStart(ts_flags, master_flags);

  vector<TServerDetails*> tservers = TServerDetailsVector(tablet_servers_);
  ASSERT_EQ(FLAGS_num_tablet_servers, tservers.size());

  itest::TabletServerMapUnowned active_tablet_servers;
  auto iter = tablet_replicas_.find(tablet_id_);
  TServerDetails* leader = iter->second;
  TServerDetails* follower = (++iter)->second;
  InsertOrDie(&active_tablet_servers, leader->uuid(), leader);
  InsertOrDie(&active_tablet_servers, follower->uuid(), follower);

  TServerDetails* new_node = nullptr;
  for (TServerDetails* ts : tservers) {
    if (!ContainsKey(active_tablet_servers, ts->uuid())) {
      new_node = ts;
      break;
    }
  }
  ASSERT_TRUE(new_node != nullptr);

  int cur_log_index = 0;
  // Elect the leader (still only a consensus config size of 2).
  ASSERT_OK(StartElection(leader, tablet_id_, MonoDelta::FromSeconds(10)));
  ASSERT_OK(WaitUntilCommittedOpIdIndexIs(++cur_log_index, leader, tablet_id_,
                                          MonoDelta::FromSeconds(30)));
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(30), active_tablet_servers,
                                  tablet_id_, 1));

  TestYcqlWorkload workload(cluster_.get());
  workload.set_table_name(kTableName);
  workload.set_timeout_allowed(true);
  workload.set_write_timeout_millis(10000);
  workload.set_num_write_threads(1);
  workload.set_write_batch_size(1);
  workload.set_sequential_write(true);
  workload.Setup();
  workload.Start();

  // Wait until the Master knows about the leader tserver.
  TServerDetails* master_observed_leader;
  ASSERT_OK(GetLeaderReplicaWithRetries(tablet_id_, &master_observed_leader));
  ASSERT_EQ(leader->uuid(), master_observed_leader->uuid());

  LOG(INFO) << "Adding tserver with uuid " << new_node->uuid() << " as PRE_VOTER ...";
  string exe_path = GetAdminToolPath();
  ASSERT_OK(CallAdmin("change_config", tablet_id_, "ADD_SERVER", new_node->uuid(), "PRE_VOTER"));

  InsertOrDie(&active_tablet_servers, new_node->uuid(), new_node);
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(active_tablet_servers.size(),
                                                leader, tablet_id_,
                                                MonoDelta::FromSeconds(10)));

  workload.StopAndJoin();
  auto num_batches = workload.batches_completed();

  LOG(INFO) << "Waiting for replicas to agree...";
  // Wait for all servers to replicate everything up through the last write op.
  // Since we don't batch, there should be at least # rows inserted log entries,
  // plus the initial leader's no-op, plus 1 for
  // the added replica for a total == #rows + 2.
  auto min_log_index = num_batches + 2;
  ASSERT_OK(WaitForServersToAgree(MonoDelta::FromSeconds(30),
                                  active_tablet_servers, tablet_id_,
                                  min_log_index));

  auto rows_inserted = workload.rows_inserted();
  LOG(INFO) << "Number of rows inserted: " << rows_inserted;

  ClusterVerifier cluster_verifier(cluster_.get());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(
      kTableName, ClusterVerifier::AT_LEAST, rows_inserted));

  // Now remove the server once again.
  LOG(INFO) << "Removing tserver with uuid " << new_node->uuid() << " from the config...";
  ASSERT_OK(CallAdmin("change_config", tablet_id_, "REMOVE_SERVER", new_node->uuid()));

  ASSERT_EQ(1, active_tablet_servers.erase(new_node->uuid()));
  ASSERT_OK(WaitUntilCommittedConfigNumVotersIs(active_tablet_servers.size(),
                                                leader, tablet_id_,
                                                MonoDelta::FromSeconds(10)));
}

TEST_F(AdminCliTest, TestDeleteTable) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 1;

  vector<string> ts_flags, master_flags;
  BuildAndStart(ts_flags, master_flags);
  string master_address = ToString(cluster_->master()->bound_rpc_addr());

  auto client = ASSERT_RESULT(YBClientBuilder()
      .add_master_server_addr(master_address)
      .Build());

  // Default table that gets created;
  string table_name = kTableName.table_name();
  string keyspace = kTableName.namespace_name();

  string exe_path = GetAdminToolPath();
  ASSERT_OK(CallAdmin("delete_table", keyspace, table_name));

  const auto tables = ASSERT_RESULT(client->ListTables(/* filter */ "", /* exclude_ysql */ true));
  ASSERT_EQ(master::kNumSystemTables, tables.size());
}

TEST_F(AdminCliTest, TestDeleteIndex) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 1;

  vector<string> ts_flags, master_flags;
  ts_flags.push_back("--index_backfill_upperbound_for_user_enforced_txn_duration_ms=12000");
  BuildAndStart(ts_flags, master_flags);
  string master_address = ToString(cluster_->master()->bound_rpc_addr());

  auto client = ASSERT_RESULT(YBClientBuilder()
      .add_master_server_addr(master_address)
      .Build());

  // Default table that gets created;
  string table_name = kTableName.table_name();
  string keyspace = kTableName.namespace_name();
  string index_name = table_name + "-index";

  auto tables = ASSERT_RESULT(client->ListTables(/* filter */ table_name));
  ASSERT_EQ(1, tables.size());
  const auto table_id = tables.front().table_id();

  YBSchema index_schema;
  YBSchemaBuilder b;
  b.AddColumn("C$_key")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
  ASSERT_OK(b.Build(&index_schema));

  // Create index.
  shared_ptr<YBTableCreator> table_creator(client->NewTableCreator());

  IndexInfoPB *index_info = table_creator->mutable_index_info();
  index_info->set_indexed_table_id(table_id);
  index_info->set_is_local(false);
  index_info->set_is_unique(false);
  index_info->set_hash_column_count(1);
  index_info->set_range_column_count(0);
  index_info->set_use_mangled_column_name(true);
  index_info->add_indexed_hash_column_ids(10);

  auto *col = index_info->add_columns();
  col->set_column_name("C$_key");
  col->set_indexed_column_id(10);

  Status s = table_creator->table_name(YBTableName(YQL_DATABASE_CQL, keyspace, index_name))
                 .table_type(YBTableType::YQL_TABLE_TYPE)
                 .schema(&index_schema)
                 .indexed_table_id(table_id)
                 .is_local_index(false)
                 .is_unique_index(false)
                 .timeout(MonoDelta::FromSeconds(60))
                 .Create();
  ASSERT_OK(s);

  tables = ASSERT_RESULT(client->ListTables(/* filter */ "", /* exclude_ysql */ true));
  ASSERT_EQ(2 + master::kNumSystemTables, tables.size());

  // Delete index.
  string exe_path = GetAdminToolPath();
  LOG(INFO) << "Delete index via yb-admin: " << keyspace << "." << index_name;
  ASSERT_OK(CallAdmin("delete_index", keyspace, index_name));

  tables = ASSERT_RESULT(client->ListTables(/* filter */ "", /* exclude_ysql */ true));
  ASSERT_EQ(1 + master::kNumSystemTables, tables.size());

  // Delete table.
  LOG(INFO) << "Delete table via yb-admin: " << keyspace << "." << table_name;
  ASSERT_OK(CallAdmin("delete_table", keyspace, table_name));

  tables = ASSERT_RESULT(client->ListTables(/* filter */ "", /* exclude_ysql */ true));
  ASSERT_EQ(master::kNumSystemTables, tables.size());
}

TEST_F(AdminCliTest, BlackList) {
  BuildAndStart();
  const auto master_address = ToString(cluster_->master()->bound_rpc_addr());
  const auto exe_path = GetAdminToolPath();
  const auto default_port = 9100;
  vector<HostPort> hosts{{"node1", default_port}, {"node2", default_port}, {"node3", default_port}};
  ASSERT_OK(CallAdmin("change_blacklist", "ADD", unpack(hosts)));
  const BlacklistChecker checker(exe_path, master_address);
  ASSERT_OK(checker(hosts));
  ASSERT_OK(CallAdmin("change_blacklist", "REMOVE", hosts.back()));
  hosts.pop_back();
  ASSERT_OK(checker(hosts));
}

TEST_F(AdminCliTest, InvalidMasterAddresses) {
  int port = AllocateFreePort();
  string unreachable_host = Substitute("127.0.0.1:$0", port);
  std::string error_string;
  ASSERT_NOK(Subprocess::Call(ToStringVector(
      GetAdminToolPath(), "--master_addresses", unreachable_host,
      "--timeout_ms", "1000", "list_tables"), /* output */ nullptr, &error_string));
  ASSERT_STR_CONTAINS(error_string, "verify the addresses");
}

TEST_F(AdminCliTest, CheckTableIdUsage) {
  BuildAndStart();
  const auto master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder().add_master_server_addr(master_address).Build());
  const auto tables = ASSERT_RESULT(client->ListTables(kTableName.table_name(),
                                                       /* exclude_ysql */ true));
  ASSERT_EQ(1, tables.size());
  const auto exe_path = GetAdminToolPath();
  const auto table_id = tables.front().table_id();
  const auto table_id_arg = Format("tableid.$0", table_id);
  auto args = ToStringVector(
      exe_path, "--master_addresses", master_address, "list_tablets", table_id_arg);
  const auto args_size = args.size();
  ASSERT_OK(Subprocess::Call(args));
  // Check good optional integer argument.
  args.push_back("1");
  ASSERT_OK(Subprocess::Call(args));
  // Check bad optional integer argument.
  args.resize(args_size);
  args.push_back("bad");
  std::string error;
  ASSERT_NOK(Subprocess::Call(args, /* output */ nullptr, &error));
  // Due to greedy algorithm all bad arguments are treated as table identifier.
  ASSERT_NE(error.find("Namespace 'bad' of type 'ycql' not found"), std::string::npos);
  // Check multiple tables when single one is expected.
  args.resize(args_size);
  args.push_back(table_id_arg);
  ASSERT_NOK(Subprocess::Call(args, /* output */ nullptr, &error));
  ASSERT_NE(error.find("Single table expected, 2 found"), std::string::npos);
  // Check wrong table id.
  args.resize(args_size - 1);
  const auto bad_table_id = table_id + "_bad";
  args.push_back(Format("tableid.$0", bad_table_id));
  ASSERT_NOK(Subprocess::Call(args, /*output*/ nullptr, &error));
  ASSERT_NE(error.find(Format("Table with id '$0' not found", bad_table_id)), std::string::npos);
}

TEST_F(AdminCliTest, TestSnapshotCreation) {
  BuildAndStart();
  const auto extra_table = YBTableName(YQLDatabase::YQL_DATABASE_CQL,
                                       kTableName.namespace_name(),
                                       "extra-table");
  YBSchemaBuilder schema_builder;
  schema_builder.AddColumn("k")->HashPrimaryKey()->Type(DataType::BINARY)->NotNull();
  schema_builder.AddColumn("v")->Type(DataType::BINARY)->NotNull();
  YBSchema schema;
  ASSERT_OK(schema_builder.Build(&schema));
  ASSERT_OK(client_->NewTableCreator()->table_name(extra_table)
      .schema(&schema).table_type(yb::client::YBTableType::YQL_TABLE_TYPE).Create());
  const auto tables = ASSERT_RESULT(client_->ListTables(kTableName.table_name(),
      /* exclude_ysql */ true));
  ASSERT_EQ(1, tables.size());
  std::string output = ASSERT_RESULT(CallAdmin(
      "create_snapshot", Format("tableid.$0", tables.front().table_id()),
      extra_table.namespace_name(), extra_table.table_name()));
  ASSERT_NE(output.find("Started snapshot creation"), string::npos);

  output = ASSERT_RESULT(CallAdmin("list_snapshots", "SHOW_DETAILS"));
  ASSERT_NE(output.find(extra_table.table_name()), string::npos);
  ASSERT_NE(output.find(kTableName.table_name()), string::npos);

  // Snapshot creation should be blocked for CQL system tables (which are virtual) but not for
  // redis system tables (which are not).
  const auto result = CallAdmin("create_snapshot", "system", "peers");
  ASSERT_FALSE(result.ok());
  ASSERT_TRUE(result.status().IsRuntimeError());
  ASSERT_NE(result.status().ToUserMessage().find(
      "Error running create_snapshot: Invalid argument"), std::string::npos);
  ASSERT_NE(result.status().ToUserMessage().find(
      "Cannot create snapshot of YCQL system table: peers"), std::string::npos);

  ASSERT_OK(CallAdmin("setup_redis_table"));
  ASSERT_OK(CallAdmin("create_snapshot", "system_redis", "redis"));
}

TEST_F(AdminCliTest, GetIsLoadBalancerIdle) {
  const MonoDelta kWaitTime = 20s;
  std::string output;
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  const std::string master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder()
      .add_master_server_addr(master_address)
      .Build());

  // Cluster balancer IsIdle() logic has been changed to the following - unless a task was
  // explicitly triggered by the cluster balancer (AsyncAddServerTask / AsyncRemoveServerTask /
  // AsyncTryStepDown) then the task does not count towards determining whether the cluster balancer
  // is active. If no pending cluster balancer tasks of the aforementioned types exist, the cluster
  // balancer will report idle.

  // Delete table should not activate the cluster balancer.
  ASSERT_OK(client->DeleteTable(kTableName, false /* wait */));
  // This should timeout.
  Status s = WaitFor(
      [&]() -> Result<bool> {
        auto output = VERIFY_RESULT(CallAdmin("get_is_load_balancer_idle"));
        return output.compare("Idle = 0\n") == 0;
      },
      kWaitTime,
      "wait for cluster balancer to stay idle");

  ASSERT_FALSE(s.ok());
}

TEST_F(AdminCliTest, TestLeaderStepdown) {
  BuildAndStart();
  std::string out;
  auto regex_fetch_first = [&out](const std::string& exp) -> Result<std::string> {
    std::smatch match;
    if (!std::regex_search(out.cbegin(), out.cend(), match, std::regex(exp)) || match.size() != 2) {
      return STATUS_FORMAT(NotFound, "No pattern in '$0'", out);
    }
    return match[1];
  };

  out = ASSERT_RESULT(CallAdmin(
      "list_tablets", kTableName.namespace_name(), kTableName.table_name()));
  const auto tablet_id = ASSERT_RESULT(regex_fetch_first(R"(\s+([a-z0-9]{32})\s+)"));
  out = ASSERT_RESULT(CallAdmin("list_tablet_servers", tablet_id));
  const auto tserver_id = ASSERT_RESULT(regex_fetch_first(R"(\s+([a-z0-9]{32})\s+\S+\s+FOLLOWER)"));
  ASSERT_OK(CallAdmin("leader_stepdown", tablet_id, tserver_id));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    out = VERIFY_RESULT(CallAdmin("list_tablet_servers", tablet_id));
    return tserver_id == VERIFY_RESULT(regex_fetch_first(R"(\s+([a-z0-9]{32})\s+\S+\s+LEADER)"));
  }, 5s, "Leader stepdown"));
}

namespace {
Result<string> RegexFetchFirst(const string out, const string &exp) {
  std::smatch match;
  if (!std::regex_search(out.cbegin(), out.cend(), match, std::regex(exp)) || match.size() != 2) {
    return STATUS_FORMAT(NotFound, "No pattern in '$0'", out);
  }
  return match[1];
}
} // namespace

class AdminCliTestForTableLocks : public AdminCliTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->enable_ysql = true;
    options->extra_tserver_flags.push_back("--enable_object_locking_for_table_locks=true");
    options->extra_tserver_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=true");
  }

 protected:
  Result<std::pair<std::string, std::string>> ExtractTxnAndSubtxnIdFrom(
      const HostPort& addr, const std::string& page) {
    EasyCurl curl;
    const std::regex txn_regex(
        "\\{txn: ([a-z0-9]{8}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{12}) subtxn_id: "
        "([0-9]+)\\}");
    faststring buf;
    auto url = strings::Substitute("http://$0/$1", ToString(addr), page);
    RETURN_NOT_OK(curl.FetchURL(url, &buf));
    auto lt_out = buf.ToString();
    VLOG(1) << "Response from url: " << url << " :\n" << lt_out;
    std::smatch match;
    if (!std::regex_search(lt_out.cbegin(), lt_out.cend(), match, txn_regex)) {
      return STATUS(NotFound, "Pattern not found");
    }
    for (std::size_t i = 0; i < match.size(); ++i) {
      VLOG(1) << i << ": " << match[i] << '\n';
    }
    auto txn_id = match[1];
    auto subtxn_id = match[2];
    VLOG(1) << "Transaction ID: " << txn_id;
    VLOG(1) << "Subtransaction ID: " << subtxn_id;
    return std::make_pair(txn_id, subtxn_id);
  }

  Result<std::pair<std::string, std::string>> ExtractTxnAndSubtxnIdFromMaster() {
    return ExtractTxnAndSubtxnIdFrom(
        cluster_->GetLeaderMaster()->bound_http_hostport(), "ObjectLockManager");
  }

  Result<std::pair<std::string, std::string>> ExtractTxnAndSubtxnIdFromTServer(int index) {
    return ExtractTxnAndSubtxnIdFrom(
        cluster_->tablet_server(index)->bound_http_hostport(), "TSLocalLockManager");
  }

  Result<bool> HasLocksTServer(int index) {
    return HasLocks(cluster_->tablet_server(index)->bound_http_hostport(), "TSLocalLockManager");
  }

  Result<bool> HasLocksMaster() {
    return HasLocks(cluster_->GetLeaderMaster()->bound_http_hostport(), "ObjectLockManager");
  }

  Result<bool> HasLocks(const HostPort& addr, const std::string& page) {
    auto res = ExtractTxnAndSubtxnIdFrom(addr, page);
    if (res.ok()) {
      return true;
    } else if (res.status().IsNotFound()) {
      return false;
    } else {
      return res.status();
    }
  }
};

TEST_F(AdminCliTestForTableLocks, ReleaseExclusiveLocksUsingTxnIdAndSubtxnId) {
  BuildAndStart();
  const std::string db_name{"yugabyte"};
  const int kTServerIndex = 0;
  auto conn1 = ASSERT_RESULT(cluster_->ConnectToDB(db_name, kTServerIndex));

  const std::string table_name = "test_table";
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE $0 (id INT PRIMARY KEY, value TEXT)", table_name));

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return !VERIFY_RESULT(HasLocksMaster());
      },
      10s * kTimeMultiplier, "Wait for master to be release locks asynchronously"));

  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksMaster()));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));
  ASSERT_OK(conn1.ExecuteFormat("LOCK TABLE $0 IN ACCESS EXCLUSIVE MODE", table_name));
  ASSERT_TRUE(ASSERT_RESULT(HasLocksMaster()));
  ASSERT_TRUE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));
  std::string txn_id;
  std::string subtxn_id;
  std::tie(txn_id, subtxn_id) = ASSERT_RESULT(ExtractTxnAndSubtxnIdFromMaster());

  ASSERT_OK(CallAdmin("unsafe_release_object_locks_global", txn_id, subtxn_id));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksMaster()));

  // Sanity check that other tranactions can acquire locks.
  auto conn2 = ASSERT_RESULT(cluster_->ConnectToDB(db_name, kTServerIndex));
  ASSERT_OK(conn2.Execute("BEGIN"));
  ASSERT_OK(conn2.ExecuteFormat("LOCK TABLE $0 IN ACCESS EXCLUSIVE MODE", table_name));
  ASSERT_OK(conn2.Execute("COMMIT"));
}

TEST_F(AdminCliTestForTableLocks, ReleaseExclusiveLocksUsingTxnId) {
  BuildAndStart();
  const std::string db_name{"yugabyte"};
  const int kTServerIndex = 0;
  auto conn1 = ASSERT_RESULT(cluster_->ConnectToDB(db_name, kTServerIndex));

  const std::string table_name = "test_table";
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE $0 (id INT PRIMARY KEY, value TEXT)", table_name));

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return !VERIFY_RESULT(HasLocksMaster());
      },
      10s * kTimeMultiplier, "Wait for master to be release locks asynchronously"));

  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksMaster()));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));
  ASSERT_OK(conn1.ExecuteFormat("LOCK TABLE $0 IN ACCESS EXCLUSIVE MODE", table_name));
  ASSERT_TRUE(ASSERT_RESULT(HasLocksMaster()));
  ASSERT_TRUE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));
  std::string txn_id;
  std::tie(txn_id, std::ignore) = ASSERT_RESULT(ExtractTxnAndSubtxnIdFromMaster());

  ASSERT_OK(CallAdmin("unsafe_release_object_locks_global", txn_id));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksMaster()));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));

  // Sanity check that other tranactions can acquire locks.
  auto conn2 = ASSERT_RESULT(cluster_->ConnectToDB(db_name, kTServerIndex));
  ASSERT_OK(conn2.Execute("BEGIN"));
  ASSERT_OK(conn2.ExecuteFormat("LOCK TABLE $0 IN ACCESS EXCLUSIVE MODE", table_name));
  ASSERT_OK(conn2.Execute("COMMIT"));
}

TEST_F(AdminCliTestForTableLocks, ReleaseSharedLocksAtTServer) {
  BuildAndStart();
  const std::string db_name{"yugabyte"};
  const int kTServerIndex = 0;
  auto conn1 = ASSERT_RESULT(cluster_->ConnectToDB(db_name, kTServerIndex));

  const std::string table_name = "test_table";
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE $0 (id INT PRIMARY KEY, value TEXT)", table_name));

  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(conn1.ExecuteFormat("LOCK TABLE $0 IN ACCESS SHARE MODE", table_name));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksMaster()));
  ASSERT_TRUE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));
  std::string txn_id;
  std::tie(txn_id, std::ignore) = ASSERT_RESULT(ExtractTxnAndSubtxnIdFromTServer(kTServerIndex));

  // Having this test flag allows us to release locks for unknown transactions at the master.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_allow_unknown_txn_release_request", "false"));
  ASSERT_NOK(CallAdmin("unsafe_release_object_locks_global", txn_id));
  ASSERT_TRUE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));

  ASSERT_OK(CallTSCli("unsafe_release_all_locks_for_txn", txn_id));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));

  // Sanity check that other tranactions can acquire locks.
  auto conn2 = ASSERT_RESULT(cluster_->ConnectToDB(db_name, kTServerIndex));
  ASSERT_OK(conn2.Execute("BEGIN"));
  ASSERT_OK(conn2.ExecuteFormat("LOCK TABLE $0 IN ACCESS EXCLUSIVE MODE", table_name));
  ASSERT_OK(conn2.Execute("COMMIT"));
}

TEST_F(AdminCliTestForTableLocks, ReleaseSharedLocksThroughMaster) {
  BuildAndStart();
  const std::string db_name{"yugabyte"};
  const int kTServerIndex = 0;
  auto conn1 = ASSERT_RESULT(cluster_->ConnectToDB(db_name, kTServerIndex));

  const std::string table_name = "test_table";
  ASSERT_OK(conn1.ExecuteFormat("CREATE TABLE $0 (id INT PRIMARY KEY, value TEXT)", table_name));

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return !VERIFY_RESULT(HasLocksMaster());
      },
      10s * kTimeMultiplier, "Wait for master to be release locks asynchronously"));

  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(conn1.ExecuteFormat("LOCK TABLE $0 IN ACCESS SHARE MODE", table_name));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksMaster()));
  ASSERT_TRUE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));
  std::string txn_id = ASSERT_RESULT(ExtractTxnAndSubtxnIdFromTServer(kTServerIndex)).first;

  // Having this test flag allows us to release locks for unknown transactions at the master.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_allow_unknown_txn_release_request", "true"));

  ASSERT_OK(CallAdmin("unsafe_release_object_locks_global", txn_id));
  ASSERT_FALSE(ASSERT_RESULT(HasLocksTServer(kTServerIndex)));

  // Sanity check that other tranactions can acquire locks.
  auto conn2 = ASSERT_RESULT(cluster_->ConnectToDB(db_name, kTServerIndex));
  ASSERT_OK(conn2.Execute("BEGIN"));
  ASSERT_OK(conn2.ExecuteFormat("LOCK TABLE $0 IN ACCESS EXCLUSIVE MODE", table_name));
  ASSERT_OK(conn2.Execute("COMMIT"));
}

// Use list_tablets to list one single tablet for the table.
// Parse out the tablet from the output.
// Use list_tablet_servers to list the tablets leader and follower details.
// Get the leader uuid and host:port from this.
// Also create an unordered_map of all followers host_port to uuid.
// Verify that these are present in the appropriate locations in the output of list_tablets.
// Use list_tablets with the json option to get the json output.
// Parse the json output, extract data for the specific tablet and compare the leader and
// follower details with the values extraced previously.
TEST_F(AdminCliTest, TestFollowersTableList) {
  BuildAndStart();

  string lt_out = ASSERT_RESULT(CallAdmin(
      "list_tablets", kTableName.namespace_name(), kTableName.table_name(),
      "1", "include_followers"));
  const auto tablet_id = ASSERT_RESULT(RegexFetchFirst(lt_out, R"(\s+([a-z0-9]{32})\s+)"));
  ASSERT_FALSE(tablet_id.empty());

  master::TabletLocationsPB locs_pb;
  ASSERT_OK(itest::GetTabletLocations(
        cluster_.get(), tablet_id, MonoDelta::FromSeconds(10), &locs_pb));
  ASSERT_EQ(3, locs_pb.replicas().size());
  string leader_uuid;
  string leader_host_port;
  unordered_map<string, string> follower_hp_to_uuid_map;
  for (const auto& replica : locs_pb.replicas()) {
    if (replica.role() == PeerRole::LEADER) {
      leader_host_port = HostPortPBToString(replica.ts_info().private_rpc_addresses(0));
      leader_uuid = replica.ts_info().permanent_uuid();
    } else {
      follower_hp_to_uuid_map[HostPortPBToString(replica.ts_info().private_rpc_addresses(0))] =
          replica.ts_info().permanent_uuid();
    }
  }
  ASSERT_FALSE(leader_uuid.empty());
  ASSERT_FALSE(leader_host_port.empty());
  ASSERT_EQ(2, follower_hp_to_uuid_map.size());

  const auto follower_list_str = ASSERT_RESULT(RegexFetchFirst(
        lt_out, R"(\s+)" + leader_host_port + R"(\s+)" + leader_uuid + R"(\s+(\S+))"));

  vector<string> followers;
  boost::split(followers, follower_list_str, boost::is_any_of(","));
  ASSERT_EQ(2, followers.size());
  for (const string &follower_host_port : followers) {
      auto got = follower_hp_to_uuid_map.find(follower_host_port);
      ASSERT_TRUE(got != follower_hp_to_uuid_map.end());
  }

  string lt_json_out = ASSERT_RESULT(CallAdmin(
      "list_tablets", kTableName.namespace_name(), kTableName.table_name(),
      "json", "include_followers"));
  boost::erase_all(lt_json_out, "\n");
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(lt_json_out));

  for (const auto& entry : ASSERT_RESULT(root["tablets"].GetArray())) {
    auto tid = ASSERT_RESULT(entry["id"].GetString());
    // Testing only for the tablet received in list_tablets <table id> 1 include_followers.
    if (tid != tablet_id) {
      continue;
    }
    auto leader = entry["leader"];
    auto lhp = ASSERT_RESULT(leader["endpoint"].GetString());
    auto luuid = ASSERT_RESULT(leader["uuid"].GetString());
    auto role = ASSERT_RESULT(leader["role"].GetString());
    ASSERT_STR_EQ(lhp, leader_host_port);
    ASSERT_STR_EQ(luuid, leader_uuid);
    ASSERT_STR_EQ(role, PeerRole_Name(PeerRole::LEADER));

    for (const auto& f : ASSERT_RESULT(entry["followers"].GetArray())) {
      auto fhp = ASSERT_RESULT(f["endpoint"].GetString());
      auto fuuid = ASSERT_RESULT(f["uuid"].GetString());
      auto frole = ASSERT_RESULT(f["role"].GetString());
      auto got = follower_hp_to_uuid_map.find(fhp);
      ASSERT_TRUE(got != follower_hp_to_uuid_map.end());
      ASSERT_STR_EQ(got->second, fuuid);
      ASSERT_STR_EQ(frole, PeerRole_Name(PeerRole::FOLLOWER));
    }
  }
}

class AdminCliTestWithYSQL : public AdminCliTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->enable_ysql = true;
  }
};

// Test that partition ranges are displayed in correct format for both hash and range partitioning
// (similar to the :9000/tablets endpoint format)
TEST_F(AdminCliTestWithYSQL, TestPartitionRangeFormat) {
  BuildAndStart();

  // Test 1: Hash partitioned table (default CQL table)
  string hash_table_out = ASSERT_RESULT(
      CallAdmin("list_tablets", kTableName.namespace_name(), kTableName.table_name()));

  LOG(INFO) << "Hash partitioned table output: " << hash_table_out;

  // Verify hash partition format: hash_split: [0x0000, 0x3332]
  ASSERT_NE(hash_table_out.find("hash_split:"), string::npos)
      << "Expected hash_split format for hash table, got: " << hash_table_out;

  ASSERT_NE(hash_table_out.find("0x"), string::npos)
      << "Expected hex format ranges for hash table, got: " << hash_table_out;

  // Verify that octal escape sequences are no longer present
  ASSERT_EQ(hash_table_out.find("\\"), string::npos)
      << "Expected no octal escape sequences in hash table, got: " << hash_table_out;

  // Verify that all hex values follow the 0x0000 pattern (4 digits after 0x)
  std::regex hex_pattern(R"(0x[0-9A-Fa-f]{4})");
  size_t hex_count = 0;
  std::string::const_iterator search_start(hash_table_out.cbegin());
  std::smatch match;
  while (std::regex_search(search_start, hash_table_out.cend(), match, hex_pattern)) {
    hex_count++;
    search_start = match.suffix().first;
  }

  // Should have at least 2 hex values (start and end for at least one tablet)
  ASSERT_GE(hex_count, 2) << "Expected at least 2 hex values in 0x0000 format for hash table, got: "
                          << hex_count;

  // Test 2: Range partitioned table (YSQL table with range partitioning)
  // Create a range partitioned table using YSQL
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte"));

  // Create a range partitioned table with explicit splits (using ASC to force range partitioning)
  ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS range_test_table"));
  ASSERT_OK(
      conn.Execute("CREATE TABLE range_test_table (k int, v text, PRIMARY KEY (k ASC)) SPLIT AT "
                   "VALUES ((10), (20), (30))"));

  string range_table_out =
      ASSERT_RESULT(CallAdmin("list_tablets", "ysql.yugabyte", "range_test_table"));

  LOG(INFO) << "Range partitioned table output: " << range_table_out;

  // Verify range partition format: range: [<start>, DocKey([], [20])) or range: [DocKey([], [20]),
  // DocKey([], [40]))
  ASSERT_NE(range_table_out.find("range:"), string::npos)
      << "Expected range format for range table, got: " << range_table_out;

  ASSERT_TRUE(
      range_table_out.find("<start>") != string::npos ||
      range_table_out.find("DocKey") != string::npos)
      << "Expected <start> or DocKey format for range table, got: " << range_table_out;

  ASSERT_TRUE(
      range_table_out.find("<end>") != string::npos ||
      range_table_out.find("DocKey") != string::npos)
      << "Expected <end> or DocKey format for range table, got: " << range_table_out;

  // Verify that range table doesn't have hex format (should have DocKey format instead)
  ASSERT_EQ(range_table_out.find("0x"), string::npos)
      << "Range table should not have hex format, got: " << range_table_out;

  // Clean up
  ASSERT_OK(conn.Execute("DROP TABLE range_test_table"));

  // Test 3: YSQL index formatting
  ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS prf_ysql_idx_tbl"));
  ASSERT_OK(
      conn.Execute("CREATE TABLE prf_ysql_idx_tbl (k int, v text, PRIMARY KEY (k ASC)) SPLIT AT "
                   "VALUES ((10), (20))"));
  ASSERT_OK(conn.Execute("CREATE INDEX prf_ysql_idx ON prf_ysql_idx_tbl (v)"));

  const string ysql_idx_out =
      ASSERT_RESULT(CallAdmin("list_tablets", "ysql.yugabyte", "prf_ysql_idx"));
  LOG(INFO) << "YSQL index partition output: " << ysql_idx_out;
  const bool ysql_idx_has_hash = ysql_idx_out.find("hash_split:") != string::npos;
  const bool ysql_idx_has_range = ysql_idx_out.find("range:") != string::npos;
  ASSERT_TRUE(ysql_idx_has_hash || ysql_idx_has_range)
      << "Expected either hash_split or range format for YSQL index, got: " << ysql_idx_out;
  if (ysql_idx_has_hash) {
    ASSERT_NE(ysql_idx_out.find("0x"), string::npos)
        << "Expected hex ranges for hash partitioned YSQL index, got: " << ysql_idx_out;
    ASSERT_EQ(ysql_idx_out.find("\\"), string::npos)
        << "Expected no octal escapes for hash partitioned YSQL index, got: " << ysql_idx_out;
  } else {
    ASSERT_TRUE(
        ysql_idx_out.find("<start>") != string::npos || ysql_idx_out.find("DocKey") != string::npos)
        << "Expected <start>/<end> or DocKey for range partitioned YSQL index, got: "
        << ysql_idx_out;
    ASSERT_EQ(ysql_idx_out.find("0x"), string::npos)
        << "Range partitioned YSQL index should not have hex ranges, got: " << ysql_idx_out;
  }

  // Cleanup YSQL objects
  ASSERT_OK(conn.Execute("DROP TABLE prf_ysql_idx_tbl"));

  // Test 3b: YSQL non-range table index formatting (default hash partitioned table)
  ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS prf_ysql_idx_tbl2"));
  ASSERT_OK(conn.Execute("CREATE TABLE prf_ysql_idx_tbl2 (k int PRIMARY KEY, v int)"));
  ASSERT_OK(conn.Execute("CREATE INDEX prf_ysql_idx2 ON prf_ysql_idx_tbl2 (v)"));

  const string ysql_idx2_out =
      ASSERT_RESULT(CallAdmin("list_tablets", "ysql.yugabyte", "prf_ysql_idx2"));
  LOG(INFO) << "YSQL non-range index partition output: " << ysql_idx2_out;
  ASSERT_NE(ysql_idx2_out.find("hash_split:"), string::npos)
      << "Expected hash_split for default-partitioned YSQL index, got: " << ysql_idx2_out;
  ASSERT_NE(ysql_idx2_out.find("0x"), string::npos)
      << "Expected hex ranges for default-partitioned YSQL index, got: " << ysql_idx2_out;
  ASSERT_EQ(ysql_idx2_out.find("\\"), string::npos)
      << "Expected no octal escapes for default-partitioned YSQL index, got: " << ysql_idx2_out;

  ASSERT_OK(conn.Execute("DROP TABLE prf_ysql_idx_tbl2"));

  // Test 4: YCQL table and YCQL secondary index formatting
  auto session = ASSERT_RESULT(CqlConnect());
  const std::string ks = "ks_prf";
  ASSERT_OK(session.ExecuteQueryFormat("CREATE KEYSPACE IF NOT EXISTS $0", ks));
  ASSERT_OK(session.ExecuteQueryFormat("USE $0", ks));

  // Create YCQL table with 5 tablets using YB client API.
  // Create YCQL table with transactions enabled via CQL (tablet count may be defaulted).
  ASSERT_OK(
      session.ExecuteQuery("CREATE TABLE IF NOT EXISTS t1 (k INT PRIMARY KEY, v TEXT) WITH "
                           "transactions = { 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery("CREATE INDEX IF NOT EXISTS t1_idx ON t1 (v)"));

  const string ycql_tbl_out = ASSERT_RESULT(CallAdmin("list_tablets", ks, "t1"));
  LOG(INFO) << "YCQL table partition output: " << ycql_tbl_out;
  ASSERT_NE(ycql_tbl_out.find("hash_split:"), string::npos)
      << "Expected hash_split for YCQL table, got: " << ycql_tbl_out;
  ASSERT_NE(ycql_tbl_out.find("0x"), string::npos)
      << "Expected hex ranges for YCQL table, got: " << ycql_tbl_out;
  ASSERT_EQ(ycql_tbl_out.find("\\"), string::npos)
      << "Expected no octal escapes for YCQL table, got: " << ycql_tbl_out;

  const string ycql_idx_out = ASSERT_RESULT(CallAdmin("list_tablets", ks, "t1_idx"));
  LOG(INFO) << "YCQL index partition output: " << ycql_idx_out;
  ASSERT_NE(ycql_idx_out.find("hash_split:"), string::npos)
      << "Expected hash_split for YCQL index, got: " << ycql_idx_out;
  ASSERT_NE(ycql_idx_out.find("0x"), string::npos)
      << "Expected hex ranges for YCQL index, got: " << ycql_idx_out;
  ASSERT_EQ(ycql_idx_out.find("\\"), string::npos)
      << "Expected no octal escapes for YCQL index, got: " << ycql_idx_out;
}

TEST_F(AdminCliTest, TestGetClusterLoadBalancerState) {
  std::string output;
  std::vector<std::string> master_flags;
  std::vector<std::string> ts_flags;
  master_flags.push_back("--enable_load_balancing=true");
  BuildAndStart(ts_flags, master_flags);

  const std::string master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder()
                                  .add_master_server_addr(master_address)
                                  .Build());
  output = ASSERT_RESULT(CallAdmin("get_load_balancer_state"));

  ASSERT_NE(output.find("ENABLED"), std::string::npos);

  output = ASSERT_RESULT(CallAdmin("set_load_balancer_enabled", "0"));

  ASSERT_EQ(output.find("Unable to change cluster balancer state"), std::string::npos);

  output = ASSERT_RESULT(CallAdmin("get_load_balancer_state"));

  ASSERT_NE(output.find("DISABLED"), std::string::npos);

  output = ASSERT_RESULT(CallAdmin("set_load_balancer_enabled", "1"));

  ASSERT_EQ(output.find("Unable to change cluster balancer state"), std::string::npos);

  output = ASSERT_RESULT(CallAdmin("get_load_balancer_state"));

  ASSERT_NE(output.find("ENABLED"), std::string::npos);
}

TEST_F(AdminCliTest, TestModifyPlacementPolicy) {
  BuildAndStart();

  // Modify the cluster placement policy to consist of 2 zones.
  ASSERT_OK(CallAdmin("modify_placement_info", "c.r.z0,c.r.z1:2,c.r.z0:2", 5, ""));

  auto output = ASSERT_RESULT(CallAdmin("get_universe_config"));

  std::string expected_placement_blocks =
      "[{\"cloudInfo\":{\"placementCloud\":\"c\",\"placementRegion\":\"r\","
      "\"placementZone\":\"z1\"},\"minNumReplicas\":2},{\"cloudInfo\":{\"placementCloud\":\"c\","
      "\"placementRegion\":\"r\",\"placementZone\":\"z0\"},\"minNumReplicas\":3}]";

  ASSERT_NE(output.find(expected_placement_blocks), string::npos);
}

TEST_F(AdminCliTest, TestModifyTablePlacementPolicy) {
  // Start a cluster with 3 tservers, each corresponding to a different zone.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 3;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 2;
  std::vector<std::string> master_flags;
  master_flags.push_back("--enable_load_balancing=true");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  std::vector<std::string> ts_flags;
  ts_flags.push_back("--placement_cloud=c");
  ts_flags.push_back("--placement_region=r");
  ts_flags.push_back("--placement_zone=z${index}");
  BuildAndStart(ts_flags, master_flags);

  const std::string& master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder()
      .add_master_server_addr(master_address)
      .Build());

  // Modify the cluster placement policy to consist of 2 zones.
  ASSERT_OK(CallAdmin("modify_placement_info", "c.r.z0,c.r.z1", 2, ""));

  // Create a new table.
  const auto extra_table = YBTableName(YQLDatabase::YQL_DATABASE_CQL,
                                       kTableName.namespace_name(),
                                       "extra-table");
  // Start a workload.
  TestYcqlWorkload workload(cluster_.get());
  workload.set_table_name(extra_table);
  workload.set_timeout_allowed(true);
  workload.set_sequential_write(true);
  workload.Setup();
  workload.Start();

  // Verify that the table has no custom placement policy set for it.
  std::shared_ptr<client::YBTable> table;
  ASSERT_OK(client->OpenTable(extra_table, &table));
  ASSERT_FALSE(table->replication_info());

  // Use yb-admin_cli to set a custom placement policy different from that of
  // the cluster placement policy for the new table.
  ASSERT_OK(CallAdmin(
      "modify_table_placement_info", kTableName.namespace_name(), "extra-table",
      "c.r.z0,c.r.z1,c.r.z2", 3, ""));

  // Verify that changing the placement _uuid for a table fails if the
  // placement_uuid does not match the cluster live placement_uuid.
  const string& random_placement_uuid = "19dfa091-2b53-434f-b8dc-97280a5f8831";
  ASSERT_NOK(CallAdmin(
      "modify_table_placement_info", kTableName.namespace_name(), "extra-table",
      "c.r.z0,c.r.z1,c.r.z2", 3, random_placement_uuid));

  ASSERT_OK(client->OpenTable(extra_table, &table));
  ASSERT_TRUE(table->replication_info()->live_replicas().placement_uuid().empty());

  // Fetch the placement policy for the table and verify that it matches
  // the custom info set previously.
  ASSERT_OK(client->OpenTable(extra_table, &table));
  vector<bool> found_zones;
  found_zones.assign(3, false);
  ASSERT_EQ(table->replication_info()->live_replicas().placement_blocks_size(), 3);
  for (int ii = 0; ii < 3; ++ii) {
    auto pb = table->replication_info()->live_replicas().placement_blocks(ii).cloud_info();
    ASSERT_EQ(pb.placement_cloud(), "c");
    ASSERT_EQ(pb.placement_region(), "r");
    if (pb.placement_zone() == "z0") {
      found_zones[0] = true;
    } else if (pb.placement_zone() == "z1") {
      found_zones[1] = true;
    } else {
      ASSERT_EQ(pb.placement_zone(), "z2");
      found_zones[2] = true;
    }
  }
  for (const bool found : found_zones) {
    ASSERT_TRUE(found);
  }

  // Perform the same test, but use the table-id instead of table name to set the
  // custom placement policy.
  std::string table_id = "tableid." + table->id();
  ASSERT_OK(CallAdmin("modify_table_placement_info", table_id, "c.r.z1", 1, ""));

  // Verify that changing the placement _uuid for a table fails if the
  // placement_uuid does not match the cluster live placement_uuid.
  ASSERT_NOK(
      CallAdmin("modify_table_placement_info", table_id, "c.r.z1", 1, random_placement_uuid));

  ASSERT_OK(client->OpenTable(extra_table, &table));
  ASSERT_TRUE(table->replication_info()->live_replicas().placement_uuid().empty());

  // Fetch the placement policy for the table and verify that it matches
  // the custom info set previously.
  ASSERT_OK(client->OpenTable(extra_table, &table));
  ASSERT_EQ(table->replication_info()->live_replicas().placement_blocks_size(), 1);
  auto pb = table->replication_info()->live_replicas().placement_blocks(0).cloud_info();
  ASSERT_EQ(pb.placement_cloud(), "c");
  ASSERT_EQ(pb.placement_region(), "r");
  ASSERT_EQ(pb.placement_zone(), "z1");

  // Stop the workload.
  workload.StopAndJoin();
  auto rows_inserted = workload.rows_inserted();
  LOG(INFO) << "Number of rows inserted: " << rows_inserted;

  sleep(5);

  // Verify that there was no data loss.
  ClusterVerifier cluster_verifier(cluster_.get());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(
    extra_table, ClusterVerifier::EXACTLY, rows_inserted));
}


TEST_F(AdminCliTest, TestCreateTransactionStatusTablesWithPlacements) {
  // Start a cluster with 3 tservers, each corresponding to a different zone.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 3;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 3;
  std::vector<std::string> master_flags;
  master_flags.push_back("--enable_load_balancing=true");
  master_flags.push_back("--catalog_manager_wait_for_new_tablets_to_elect_leader=false");
  std::vector<std::string> ts_flags;
  ts_flags.push_back("--placement_cloud=c");
  ts_flags.push_back("--placement_region=r");
  ts_flags.push_back("--placement_zone=z${index}");
  BuildAndStart(ts_flags, master_flags);

  // Create a new table.
  const auto extra_table = YBTableName(YQLDatabase::YQL_DATABASE_CQL,
                                       kTableName.namespace_name(),
                                       "extra-table");
  // Start a workload.
  TestYcqlWorkload workload(cluster_.get());
  workload.set_table_name(extra_table);
  workload.set_timeout_allowed(true);
  workload.set_sequential_write(true);
  workload.Setup();
  workload.Start();

  const std::string& master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder()
      .add_master_server_addr(master_address)
      .Build());

  // Create transaction tables for each zone.
  for (int i = 0; i < 3; ++i) {
    string table_name = Substitute("transactions_z$0", i);
    string placement = Substitute("c.r.z$0", i);
    ASSERT_OK(CallAdmin("create_transaction_table", table_name));
    ASSERT_OK(CallAdmin("modify_table_placement_info", "system", table_name, placement, 1));
  }

  // Verify that the tables are all in transaction status tables in the right zone.
  std::shared_ptr<client::YBTable> table;
  for (int i = 0; i < 3; ++i) {
    const auto table_name =
        YBTableName(YQLDatabase::YQL_DATABASE_CQL, "system", Substitute("transactions_z$0", i));
    ASSERT_OK(client->OpenTable(table_name, &table));
    ASSERT_EQ(table->table_type(), YBTableType::TRANSACTION_STATUS_TABLE_TYPE);
    ASSERT_EQ(table->replication_info()->live_replicas().placement_blocks_size(), 1);
    auto pb = table->replication_info()->live_replicas().placement_blocks(0).cloud_info();
    ASSERT_EQ(pb.placement_zone(), Substitute("z$0", i));
  }

  // Add two new tservers, to zone3 and an unused zone.
  std::vector<std::string> existing_zone_ts_flags;
  existing_zone_ts_flags.push_back("--placement_cloud=c");
  existing_zone_ts_flags.push_back("--placement_region=r");
  existing_zone_ts_flags.push_back("--placement_zone=z3");
  ASSERT_OK(cluster_->AddTabletServer(true, existing_zone_ts_flags));

  std::vector<std::string> new_zone_ts_flags;
  new_zone_ts_flags.push_back("--placement_cloud=c");
  new_zone_ts_flags.push_back("--placement_region=r");
  new_zone_ts_flags.push_back("--placement_zone=z4");
  ASSERT_OK(cluster_->AddTabletServer(true, new_zone_ts_flags));

  ASSERT_OK(cluster_->WaitForTabletServerCount(5, 5s));

  // Blacklist the original zone3 tserver.
  ASSERT_OK(cluster_->AddTServerToBlacklist(cluster_->master(), cluster_->tablet_server(2)));

  // Stop the workload.
  workload.StopAndJoin();
  auto rows_inserted = workload.rows_inserted();
  LOG(INFO) << "Number of rows inserted: " << rows_inserted;

  sleep(5);

  // Verify that there was no data loss.
  ClusterVerifier cluster_verifier(cluster_.get());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCountWithRetries(
    extra_table, ClusterVerifier::EXACTLY, rows_inserted, 20s));
}

TEST_F(AdminCliTest, TestClearPlacementPolicy) {
  // Start a cluster with 3 tservers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 3;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 2;
  std::vector<std::string> master_flags;
  master_flags.push_back("--enable_load_balancing=true");
  std::vector<std::string> ts_flags;
  ts_flags.push_back("--placement_cloud=c");
  ts_flags.push_back("--placement_region=r");
  ts_flags.push_back("--placement_zone=z");
  BuildAndStart(ts_flags, master_flags);

  // Create the placement config.
  ASSERT_OK(CallAdmin("modify_placement_info", "c.r.z", 3, ""));

  // Ensure that the universe config has placement information.
  auto output = ASSERT_RESULT(CallAdmin("get_universe_config"));
  ASSERT_TRUE(output.find("replicationInfo") != std::string::npos);

  // Clear the placement config.
  ASSERT_OK(CallAdmin("clear_placement_info"));

  // Ensure that the placement config is absent.
  output = ASSERT_RESULT(CallAdmin("get_universe_config"));
  ASSERT_TRUE(output.find("replicationInfo") == std::string::npos);
}

TEST_F(AdminCliTest, DdlLog) {
  const std::string kNamespaceName = "test_namespace";
  const std::string kTableName = "test_table";
  BuildAndStart({}, {});

  auto session = ASSERT_RESULT(CqlConnect());
  ASSERT_OK(session.ExecuteQueryFormat(
      "CREATE KEYSPACE IF NOT EXISTS $0", kNamespaceName));

  ASSERT_OK(session.ExecuteQueryFormat("USE $0", kNamespaceName));

  ASSERT_OK(session.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, text_column TEXT) "
      "WITH transactions = { 'enabled' : true }", kTableName));

  ASSERT_OK(session.ExecuteQueryFormat(
      "CREATE INDEX test_idx ON $0 (text_column)", kTableName));

  ASSERT_OK(session.ExecuteQueryFormat(
      "ALTER TABLE $0 ADD int_column INT", kTableName));

  ASSERT_OK(session.ExecuteQuery("DROP INDEX test_idx"));

  ASSERT_OK(session.ExecuteQueryFormat("ALTER TABLE $0 DROP text_column", kTableName));

  auto document = ASSERT_RESULT(CallJsonAdmin("ddl_log"));

  auto log = ASSERT_RESULT(GetMemberAsArray(document, "log"));
  ASSERT_EQ(log.Size(), 3);
  std::vector<std::string> actions;
  actions.reserve(log.Size());
  for (const auto& entry : log) {
    LOG(INFO) << "Entry: " << PrettyWriteRapidJsonToString(entry);
    TableType type;
    bool parse_result = TableType_Parse(
        std::string(ASSERT_RESULT(GetMemberAsStr(entry, "table_type"))), &type);
    ASSERT_TRUE(parse_result);
    ASSERT_EQ(type, TableType::YQL_TABLE_TYPE);
    auto namespace_name = ASSERT_RESULT(GetMemberAsStr(entry, "namespace"));
    ASSERT_EQ(namespace_name, kNamespaceName);
    auto table_name = ASSERT_RESULT(GetMemberAsStr(entry, "table"));
    ASSERT_EQ(table_name, kTableName);
    actions.emplace_back(ASSERT_RESULT(GetMemberAsStr(entry, "action")));
  }
  ASSERT_EQ(actions[0], "Drop column text_column");
  ASSERT_EQ(actions[1], "Drop index test_idx");
  ASSERT_EQ(actions[2], "Add column int_column[int32 NULLABLE VALUE]");
}

TEST_F(AdminCliTest, FlushSysCatalog) {
  BuildAndStart();
  const auto master_addrs = cluster_->GetMasterAddresses();
  std::vector<string> master_addr_strs;
  for (const auto& addr : master_addrs) {
    master_addr_strs.push_back(ToString(addr));
  }

  auto client = ASSERT_RESULT(YBClientBuilder().master_server_addrs(master_addr_strs).Build());
  ASSERT_OK(CallAdmin("flush_sys_catalog"));
}

TEST_F(AdminCliTest, CompactSysCatalog) {
  BuildAndStart();
  const auto master_addrs = cluster_->GetMasterAddresses();
  std::vector<string> master_addr_strs;
  for (const auto& addr : master_addrs) {
    master_addr_strs.push_back(ToString(addr));
  }
  
  auto client = ASSERT_RESULT(YBClientBuilder().master_server_addrs(master_addr_strs).Build());
  ASSERT_OK(CallAdmin("compact_sys_catalog"));
}

// A simple smoke test to ensure it working and
// nothing is broken by future changes.
TEST_F(AdminCliTest, SetWalRetentionSecsTest) {
  constexpr auto WAL_RET_TIME_SEC = 100ul;
  constexpr auto UNEXPECTED_ARG = 911ul;

  BuildAndStart();
  const auto master_address = ToString(cluster_->master()->bound_rpc_addr());

  // Default table that gets created;
  const auto& table_name = kTableName.table_name();
  const auto& keyspace = kTableName.namespace_name();

  // No WAL ret time found
  {
    const auto output = ASSERT_RESULT(CallAdmin("get_wal_retention_secs", keyspace, table_name));
    ASSERT_NE(output.find("not set"), std::string::npos);
  }

  // Successfuly set WAL time and verified by the getter
  {
    ASSERT_OK(CallAdmin("set_wal_retention_secs", keyspace, table_name, WAL_RET_TIME_SEC));
    const auto output = ASSERT_RESULT(CallAdmin("get_wal_retention_secs", keyspace, table_name));
    ASSERT_TRUE(
        output.find(std::to_string(WAL_RET_TIME_SEC)) != std::string::npos &&
        output.find(table_name) != std::string::npos);
  }

  // Too many args in input
  {
    const auto output_setter =
        CallAdmin("set_wal_retention_secs", keyspace, table_name, WAL_RET_TIME_SEC, UNEXPECTED_ARG);
    ASSERT_FALSE(output_setter.ok());
    ASSERT_TRUE(output_setter.status().IsRuntimeError());
    ASSERT_TRUE(
        output_setter.status().ToUserMessage().find("Invalid argument") != std::string::npos);

    const auto output_getter =
        CallAdmin("get_wal_retention_secs", keyspace, table_name, UNEXPECTED_ARG);
    ASSERT_FALSE(output_getter.ok());
    ASSERT_TRUE(output_getter.status().IsRuntimeError());
    ASSERT_TRUE(
        output_getter.status().ToUserMessage().find("Invalid argument") != std::string::npos);
  }

  // Outbounded arg in input
  {
    const auto output_setter =
        CallAdmin("set_wal_retention_secs", keyspace, table_name, -WAL_RET_TIME_SEC);
    ASSERT_FALSE(output_setter.ok());
    ASSERT_TRUE(output_setter.status().IsRuntimeError());

    const auto output_getter =
        CallAdmin("get_wal_retention_secs", keyspace, table_name + "NoTable");
    ASSERT_FALSE(output_getter.ok());
    ASSERT_TRUE(output_getter.status().IsRuntimeError());
  }
}

TEST_F(AdminCliTest, AddTransactionStatusTablet) {
  const std::string kNamespaceName = "test_namespace";
  const std::string kTableName = "test_table";
  const auto kWaitNewTabletReadyTimeout = MonoDelta::FromMilliseconds(10000);

  BuildAndStart({}, {});

  string master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder().add_master_server_addr(master_address).Build());

  // Force creation of system.transactions.
  auto session = ASSERT_RESULT(CqlConnect());
  ASSERT_OK(session.ExecuteQueryFormat(
      "CREATE KEYSPACE IF NOT EXISTS $0", kNamespaceName));
  ASSERT_OK(session.ExecuteQueryFormat("USE $0", kNamespaceName));
  ASSERT_OK(session.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY) "
      "WITH transactions = { 'enabled' : true }", kTableName));

  auto global_txn_table = YBTableName(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  auto global_txn_table_id = ASSERT_RESULT(client::GetTableId(client_.get(), global_txn_table));

  auto tablets_before = ASSERT_RESULT(CallAdmin(
      "list_tablets", master::kSystemNamespaceName, kGlobalTransactionsTableName));
  auto num_tablets_before = std::count(tablets_before.begin(), tablets_before.end(), '\n');
  LOG(INFO) << "Tablets before adding transaction tablet: " << AsString(tablets_before);

  ASSERT_OK(CallAdmin("add_transaction_tablet", global_txn_table_id));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto tablets = VERIFY_RESULT(CallAdmin(
        "list_tablets", master::kSystemNamespaceName, kGlobalTransactionsTableName));
    auto num_tablets = std::count(tablets.begin(), tablets.end(), '\n');
    LOG(INFO) << "Tablets: " << AsString(tablets);
    return num_tablets == num_tablets_before + 1;
  }, kWaitNewTabletReadyTimeout, "Timeout waiting for new status tablet to be ready"));
}

class AdminCliListTabletsTest : public AdminCliTest {
 public:
  template <class... Args>
  Status ListTabletsWithFailure(const std::string& expected_error_substr, Args&&... args) {
    auto result = ListTablets(std::forward<Args>(args)...);
    SCHECK(!result.ok(), IllegalState, "list_tablets command expected to fail");
    auto error = result.status().ToUserMessage();
    SCHECK(
        error.find(expected_error_substr) != std::string::npos,
        IllegalState,
        Format("$0 substring was not found in $1", expected_error_substr, error));
    return Status::OK();
  }

  template <class... Args>
  Result<std::size_t> GetListTabletsCount(Args&&... args) {
    JsonDocument doc;
    auto root = VERIFY_RESULT(doc.Parse(VERIFY_RESULT(ListTablets(std::forward<Args>(args)...,
                                                                  "json"))));
    return root["tablets"].size();
  }

 private:
  template <class... Args>
  Result<std::string> ListTablets(Args&&... args) {
    return CallAdminVec(ToStringVector(
        GetAdminToolPath(), "--master_addresses", GetMasterAddresses(), "list_tablets",
        std::forward<Args>(args)...));
  }
};

TEST_F_EX(AdminCliTest, CheckTableNameAndNamespaceUsage, AdminCliListTabletsTest) {
  ASSERT_NO_FATALS(BuildAndStart());

  // Check table name is missing.
  ASSERT_OK(ListTabletsWithFailure("Empty list of tables", kTableName.namespace_name()));

  // Check namespace does not exist.
  const auto kBadNamespaceName = kTableName.namespace_name() + "bad";
  ASSERT_OK(ListTabletsWithFailure(
      Format("Namespace '$0' of type 'ycql' not found", kBadNamespaceName), kBadNamespaceName,
      kTableName.table_name()));

  // Check wrong keyspace of a table when called with table name.
  const auto client =
      ASSERT_RESULT(YBClientBuilder().add_master_server_addr(GetMasterAddresses()).Build());
  ASSERT_OK(client->CreateNamespace(kBadNamespaceName, YQL_DATABASE_CQL));
  ASSERT_OK(ListTabletsWithFailure(
      Format(
          "Table with name '$0' not found in namespace 'ycql.$1'",
          kTableName.table_name(),
          kBadNamespaceName),
      kBadNamespaceName, kTableName.table_name()));

  // Check wrong keyspace of a table when called with table id.
  const auto tables =
      ASSERT_RESULT(client->ListTables(kTableName.table_name(), /* exclude_ysql */ true));
  ASSERT_EQ(1, tables.size());
  const auto& table_id = tables.front().table_id();
  ASSERT_OK(ListTabletsWithFailure(
      Format(
          "Table with id '$0' belongs to different namespace 'ycql.$1'",
          table_id,
          kBadNamespaceName),
      kBadNamespaceName,
      Format("tableid.$0", table_id)));
}

TEST_F_EX(AdminCliTest, ListTabletDefaultTenTablets, AdminCliListTabletsTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 1;

  ASSERT_NO_FATALS(BuildAndStart({}, {}));

  YBSchema schema;
  YBSchemaBuilder schema_builder;
  schema_builder.AddColumn("k")->HashPrimaryKey()->Type(DataType::BINARY)->NotNull();
  schema_builder.AddColumn("v")->Type(DataType::BINARY)->NotNull();
  ASSERT_OK(schema_builder.Build(&schema));

  const auto client =
      ASSERT_RESULT(YBClientBuilder().add_master_server_addr(GetMasterAddresses()).Build());
  // Default table that gets created.
  const auto& keyspace = kTableName.namespace_name();
  // Create table with 20 tablets.
  constexpr auto* kTestTableName = "t20";
  ASSERT_OK(client->NewTableCreator()
      ->table_name(YBTableName(YQL_DATABASE_CQL, keyspace, kTestTableName))
      .table_type(YBTableType::YQL_TABLE_TYPE)
      .schema(&schema)
      .num_tablets(20)
      .Create());

  // Test 10 tablets should be listed by default
  auto count = ASSERT_RESULT(GetListTabletsCount(keyspace, kTestTableName));
  ASSERT_EQ(count, 10);

  // Test all tablets should be listed when value is 0
  count = ASSERT_RESULT(GetListTabletsCount(keyspace, kTestTableName, 0));
  ASSERT_EQ(count, 20);
}

TEST_F_EX(AdminCliTest, TestSplitTabletDefault, AdminCliListTabletsTest) {
  BuildAndStart();
  const auto& keyspace = kTableName.namespace_name();
  const auto& table_name = kTableName.table_name();

  // Insert some rows.
  TestYcqlWorkload workload(cluster_.get());
  workload.set_table_name(kTableName);
  workload.set_timeout_allowed(true);
  workload.Setup();
  workload.Start();
  workload.WaitInserted(100);
  workload.StopAndJoin();
  LOG(INFO) << "Number of rows inserted: " << workload.rows_inserted();

  // Flush to SST. The middle key determination Tablet::GetEncodedMiddleSplitKey() works off SSTs.
  ASSERT_OK(CallAdmin("flush_table", keyspace, table_name));

  // Split tablet.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_enable_multi_way_tablet_split", "false"));
  ASSERT_OK(CallAdmin("split_tablet", tablet_id_));

  // Verify the default split creates 2 child tablets.
  constexpr int kSplitFactor = 2;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return kSplitFactor == VERIFY_RESULT(GetListTabletsCount(keyspace, table_name));
      }, 30s, "Wait for tablet split to complete"));
}

TEST_F_EX(AdminCliTest, TestSplitTabletMultiWay, AdminCliListTabletsTest) {
  BuildAndStart();
  const auto& keyspace = kTableName.namespace_name();
  const auto& table_name = kTableName.table_name();

  // Insert some rows.
  TestYcqlWorkload workload(cluster_.get());
  workload.set_table_name(kTableName);
  workload.set_timeout_allowed(true);
  workload.Setup();
  workload.Start();
  workload.WaitInserted(100);
  workload.StopAndJoin();
  LOG(INFO) << "Number of rows inserted: " << workload.rows_inserted();

  // Verify multi-way split is disallowed when gFlag is disabled.
  constexpr int kSplitFactor = 5;
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_enable_multi_way_tablet_split", "false"));
  ASSERT_NOK_STR_CONTAINS(
      CallAdmin("split_tablet", tablet_id_, kSplitFactor),
      "Split factor must be 2");

  // Verify multi-way split is allowed when gFlag is enabled.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_enable_multi_way_tablet_split", "true"));
  ASSERT_OK(CallAdmin("split_tablet", tablet_id_, kSplitFactor));

  // Verify the split creates 5 child tablets.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return kSplitFactor == VERIFY_RESULT(GetListTabletsCount(keyspace, table_name));
      }, 30s, "Wait for tablet split to complete"));
}

TEST_F(AdminCliTest, GetAutoFlagsConfig) {
  BuildAndStart();
  auto message = ASSERT_RESULT(CallAdmin("get_auto_flags_config"));
  ASSERT_STR_CONTAINS(message, R"#(AutoFlags config:
config_version: 1
promoted_flags {)#");
}

TEST_F(AdminCliTest, PromoteAutoFlags) {
  BuildAndStart();
  const auto master_address = ToString(cluster_->master()->bound_rpc_addr());

  ASSERT_NOK_STR_CONTAINS(
      CallAdmin(kPromoteAutoFlagsCmd, "invalid"), "Invalid value provided for max_flags_class");
  ASSERT_NOK_STR_CONTAINS(
      CallAdmin(kPromoteAutoFlagsCmd, "kExternal", "invalid"), "Invalid arguments for operation");
  ASSERT_NOK_STR_CONTAINS(
      CallAdmin(kPromoteAutoFlagsCmd, "kExternal", "force", "invalid"),
      "Invalid arguments for operation");

  ASSERT_OK(CallAdmin(kPromoteAutoFlagsCmd, "kLocalVolatile"));
  ASSERT_OK(CallAdmin(kPromoteAutoFlagsCmd, "kLocalPersisted"));
  ASSERT_OK(CallAdmin(kPromoteAutoFlagsCmd, "kExternal"));

  auto result = ASSERT_RESULT(CallAdmin(kPromoteAutoFlagsCmd, "kLocalVolatile"));
  ASSERT_STR_CONTAINS(
      result,
      "PromoteAutoFlags completed successfully\n"
      "No new AutoFlags eligible to promote\n"
      "Current config version: 1");

  result = ASSERT_RESULT(CallAdmin(kPromoteAutoFlagsCmd, "kLocalVolatile", "force"));
  ASSERT_STR_CONTAINS(
      result,
      "PromoteAutoFlags completed successfully\n"
      "New AutoFlags were promoted\n"
      "New config version: 2");
}

TEST_F(AdminCliTest, RollbackAutoFlags) {
  BuildAndStart(
      /* ts_flags = */ {}, /* master_flags = */ {"--limit_auto_flag_promote_for_new_universe=0"});
  const auto master_address = ToString(cluster_->master()->bound_rpc_addr());

  auto status = CallAdmin(kRollbackAutoFlagsCmd);
  ASSERT_NOK(status);
  ASSERT_NE(status.ToString().find("Invalid arguments for operation"), std::string::npos);

  status = CallAdmin(kRollbackAutoFlagsCmd, "invalid");
  ASSERT_NOK(status);
  ASSERT_NE(status.ToString().find("invalid is not a valid number"), std::string::npos);

  status = CallAdmin(kRollbackAutoFlagsCmd, std::numeric_limits<int64_t>::max());
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "rollback_version exceeds bounds");

  auto result = ASSERT_RESULT(CallAdmin(kRollbackAutoFlagsCmd, 0));
  ASSERT_STR_CONTAINS(
      result,
      "RollbackAutoFlags completed successfully\n"
      "No AutoFlags have been promoted since version 0\n"
      "Current config version: 0");

  result = ASSERT_RESULT(CallAdmin(kPromoteAutoFlagsCmd, "kLocalVolatile"));
  ASSERT_STR_CONTAINS(
      result,
      "New AutoFlags were promoted\n"
      "New config version:");

  result = ASSERT_RESULT(CallAdmin(kRollbackAutoFlagsCmd, 0));
  ASSERT_STR_CONTAINS(
      result,
      "RollbackAutoFlags completed successfully\n"
      "AutoFlags that were promoted after config version 0 were successfully rolled back\n"
      "New config version: 2");

  result = ASSERT_RESULT(CallAdmin(kRollbackAutoFlagsCmd, 0));
  ASSERT_STR_CONTAINS(
      result,
      "RollbackAutoFlags completed successfully\n"
      "No AutoFlags have been promoted since version 0\n"
      "Current config version: 2");

  result = ASSERT_RESULT(CallAdmin(kPromoteAutoFlagsCmd));
  ASSERT_STR_CONTAINS(
      result,
      "PromoteAutoFlags completed successfully\n"
      "New AutoFlags were promoted\n"
      "New config version: 3");

  // Rollback external AutoFlags should fail.
  status = CallAdmin(kRollbackAutoFlagsCmd, 0);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "is not eligible for rollback");
}

TEST_F(AdminCliTest, PromoteDemoteSingleAutoFlag) {
  BuildAndStart();
  const auto kPromoteSingleFlaCmd = "promote_single_auto_flag";
  const auto kDemoteSingleFlagCmd = "demote_single_auto_flag";

  // Promote a single AutoFlag
  auto result = CallAdmin(kPromoteSingleFlaCmd, "NAProcess", "NAFlag");
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.ToString(), "Process NAProcess not found");

  result = CallAdmin(kPromoteSingleFlaCmd, "yb-master", "NAFlag");
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.ToString(), "AutoFlag NAFlag not found in process yb-master");

  result = CallAdmin(kPromoteSingleFlaCmd, "yb-master", "TEST_auto_flags_initialized");
  ASSERT_OK(result);
  ASSERT_STR_CONTAINS(
      result.ToString(),
      "Failed to promote AutoFlag TEST_auto_flags_initialized from process yb-master. Check the "
      "logs for more information\n"
      "Current config version: 1");

  // Demote a single AutoFlag
  result = CallAdmin(kDemoteSingleFlagCmd, "NAProcess", "NAFlag", "force");
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.ToString(), "Process NAProcess not found");

  result = CallAdmin(kDemoteSingleFlagCmd, "yb-master", "NAFlag", "force");
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.ToString(), "AutoFlag NAFlag not found in process yb-master");

  result = CallAdmin(kDemoteSingleFlagCmd, "yb-master", "TEST_auto_flags_initialized", "force");
  ASSERT_OK(result);
  ASSERT_STR_CONTAINS(
      result.ToString(),
      "AutoFlag TEST_auto_flags_initialized from process yb-master was successfully demoted\n"
      "New config version: 2");

  result = CallAdmin(kDemoteSingleFlagCmd, "yb-master", "TEST_auto_flags_initialized", "force");
  ASSERT_OK(result);
  ASSERT_STR_CONTAINS(
      result.ToString(),
      "Unable to demote AutoFlag TEST_auto_flags_initialized from process yb-master because the "
      "flag is not in promoted state\n"
      "Current config version: 2");

  result = CallAdmin(kPromoteSingleFlaCmd, "yb-master", "TEST_auto_flags_initialized");
  ASSERT_OK(result);
  ASSERT_STR_CONTAINS(
      result.ToString(),
      "AutoFlag TEST_auto_flags_initialized from process yb-master was successfully promoted\n"
      "New config version: 3");
}

TEST_F(AdminCliTest, TestListNamespaces) {
  BuildAndStart();
  ASSERT_OK(client_->CreateNamespaceIfNotExists("a_user_namespace"));
  auto status = CallAdmin("list_namespaces");
  ASSERT_OK(status);

  std::string output = status.ToString();

  ASSERT_STR_CONTAINS(output, "User Namespaces");
  auto user_namespaces_pos = output.find("User Namespaces");
  ASSERT_STR_CONTAINS(output, "System Namespaces");
  auto system_namespaces_pos = output.find("System Namespaces");

  std::regex system_namespace_regex("system .* ycql [a-zA-Z_]+ [a-zA-Z_]+");
  std::smatch system_namespace_match;
  std::regex_search(output, system_namespace_match, system_namespace_regex);
  ASSERT_FALSE(system_namespace_match.empty());
  // We expect the "system" keyspace to be under "System Keyspaces".
  ASSERT_GT(system_namespace_match.position(0), system_namespaces_pos);

  std::smatch user_namespace_match;
  std::regex user_namespace_regex("a_user_namespace");
  std::regex_search(output, user_namespace_match, user_namespace_regex);
  ASSERT_FALSE(user_namespace_match.empty());
  /* Because we compare their character positions, we expect "User Namespaces:" to be outputted
  before "System Namespaces:" to simplify testing of whether "a_user_namespace" is under
  "User Namespaces:" and not "System Namespaces:". */
  ASSERT_LT(user_namespaces_pos, system_namespaces_pos);
  ASSERT_GT(user_namespace_match.position(0), user_namespaces_pos);
  ASSERT_LT(user_namespace_match.position(0), system_namespaces_pos);

  // We test just YSQL namespaces here because YCQL namespaces get directly deleted without being
  // in the DELETED state and thus there is nothing for include_nonrunning to see in that case.
  ASSERT_OK(client_->CreateNamespace("new_user_namespace", YQL_DATABASE_PGSQL));

  ASSERT_OK(CallAdmin("delete_namespace", "ysql.new_user_namespace"));

  status = CallAdmin("list_namespaces");
  ASSERT_OK(status);
  ASSERT_STR_NOT_CONTAINS(status.ToString(), "new_user_namespace");

  status = CallAdmin("list_namespaces", "include_nonrunning");
  ASSERT_OK(status);
  ASSERT_TRUE(std::regex_search(status.ToString(), std::regex("new_user_namespace.*DELETED")));
}

TEST_F(AdminCliTest, PrintArgumentExpressions) {
  const auto namespace_expression = "<namespace>:\n [(ycql|ysql).]<namespace_name> (default ycql.)";
  const auto table_expression = "<table>:\n <namespace> <table_name> | tableid.<table_id>";
  const auto index_expression = "<index>:\n  <namespace> <index_name> | tableid.<index_id>";

  BuildAndStart();
  auto status = CallAdmin("delete_table");
  ASSERT_NOK(status);
  ASSERT_NE(status.ToString().find(table_expression), std::string::npos);

  status = CallAdmin("delete_namespace");
  ASSERT_NOK(status);
  ASSERT_NE(status.ToString().find(namespace_expression), std::string::npos);

  status = CallAdmin("delete_index");
  ASSERT_NOK(status);
  ASSERT_NE(status.ToString().find(index_expression), std::string::npos);

  status = CallAdmin("add_universe_key_to_all_masters");
  ASSERT_NOK(status);
  ASSERT_EQ(status.ToString().find(namespace_expression), std::string::npos);
  ASSERT_EQ(status.ToString().find(table_expression), std::string::npos);
  ASSERT_EQ(status.ToString().find(index_expression), std::string::npos);
}

TEST_F(AdminCliTest, TestCompactionStatusBeforeCompaction) {
  BuildAndStart();
  const string master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder().add_master_server_addr(master_address).Build());

  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> {
        const string output = VERIFY_RESULT(
            CallAdmin("compaction_status", kTableName.namespace_name(), kTableName.table_name()));

        std::smatch match;
        const std::regex regex(
            "No full compaction taking place\n"
            "A full compaction has never been completed\n"
            "An admin compaction has never been requested");
        std::regex_search(output, match, regex);
        return !match.empty();
      },
      30s, "Wait for initial metrics heartbeats to report full compaction statuses"));
}

TEST_F(AdminCliTest, TestCompactionStatusAfterCompactionFinishes) {
  BuildAndStart();
  const string master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder().add_master_server_addr(master_address).Build());

  const auto time_before_compaction = DateTime::TimestampNow();
  ASSERT_OK(CallAdmin("compact_table", kTableName.namespace_name(), kTableName.table_name()));

  string output;
  std::smatch match;
  ASSERT_OK(WaitFor(
      [&]() {
        const auto result =
            CallAdmin("compaction_status", kTableName.namespace_name(), kTableName.table_name());
        if (!result.ok()) {
          return false;
        }
        output = *result;
        const std::regex regex("No full compaction taking place");
        std::regex_search(output, match, regex);
        return !match.empty();
      },
      30s /* timeout */, "Wait for compaction status to report no compaction"));

  const std::regex regex(
      "Last full compaction completion time: (.+)\nLast admin compaction request time: (.+)");
  std::regex_search(output, match, regex);
  ASSERT_FALSE(match.empty());
  const auto last_full_compaction_time =
      ASSERT_RESULT(DateTime::TimestampFromString(match[1].str()));
  ASSERT_GT(last_full_compaction_time, time_before_compaction);
  const auto last_request_time = ASSERT_RESULT(DateTime::TimestampFromString(match[2].str()));
  ASSERT_GT(last_request_time, time_before_compaction);
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/19957
TEST_F(AdminCliTest, TestAdminCommandTimeout) {
  // The test checks that an admin command timeout for compactions and flushes is honored by
  // FlushManager on yb-master side (no dependency on `master_ts_rpc_timeout_ms`).

  BuildAndStart();
  const string master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder().add_master_server_addr(master_address).Build());

  // No admin timeout higher than `master_ts_rpc_timeout_ms` was honored for flush and compaction
  // operation (passed via FlushManager) before the fix, which means `master_ts_rpc_timeout_ms`
  // was the max timeout available for the mentioned operations. It is definitely not enough at
  // least for large compactions.
  // In case of timeout by admin command timeout value, the admin tool gets the error:
  // (1) `Timed out waiting for FlushTables`.
  // In case of timeout by `master_ts_rpc_timeout_ms` value, the admin tool gets the error like:
  // (2) `Flush operation for id: 9be5b8cbd0d14cfea3371da1a6cbccab failed`.
  // With the fix, only the error (1) is thrown for timied out commands.
  constexpr size_t kMasterRpcTimeout = 4000;
  constexpr size_t kCompactionPause  = 2 * kMasterRpcTimeout;
  constexpr size_t kAdminCmdTimeout  =
      kMasterRpcTimeout + ((kCompactionPause - kMasterRpcTimeout) / 2);

  // Update flags.
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "master_ts_rpc_timeout_ms", std::to_string(kMasterRpcTimeout)));
  ASSERT_OK(cluster_->SetFlagOnTServers(
      "TEST_pause_tablet_compact_flush_ms", std::to_string(kCompactionPause)));
  SleepFor(MonoDelta::FromSeconds(1));

  const auto before_ts = DateTime::TimestampNow();
  auto result = CallAdmin(
      "compact_table", kTableName.namespace_name(), kTableName.table_name(),
      std::to_string(kAdminCmdTimeout / MonoTime::kMillisecondsPerSecond));
  auto duration = MonoDelta::FromMicroseconds(DateTime::TimestampNow().value() - before_ts.value());
  LOG(INFO) << "Result: " << result << ", dutaion: " << duration;

  // It is expected to always get the timeout error since kAdminCmdTimeout < kCompactionPause,
  // additionaly it covers the cases kAdminCmdTimeout > kMasterRpcTimeout for the expected error.
  bool timed_out = !result.ok() &&
                    result.status().message().Contains("Timed out waiting for FlushTables");
  ASSERT_TRUE(timed_out);

  // The expectation is to have the call's duration approximately kAdminCmdTimeout amount.
  ASSERT_GT(duration, MonoDelta::FromMilliseconds(kAdminCmdTimeout));
  ASSERT_LT(duration, MonoDelta::FromMilliseconds(kAdminCmdTimeout * 1.25));

  // Wait for some time to let the compaction be unpaused and completed.
  SleepFor(MonoDelta::FromMilliseconds(1.1 * (kCompactionPause - kAdminCmdTimeout)));
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/26722
TEST_F(AdminCliTest, TestAdminRpcTimeout) {
  // The test checks that an admin command timeout is honored when it is greater than admin's
  // default RPC timeout, which is set by FLAGS_yb_client_admin_rpc_timeout_sec.

  BuildAndStart();
  const string master_address = ToString(cluster_->master()->bound_rpc_addr());
  auto client = ASSERT_RESULT(YBClientBuilder().add_master_server_addr(master_address).Build());

  // With the fix for https://github.com/yugabyte/yugabyte-db/issues/19957, no admin command
  // timeout higher than 60 seconds was honored for any admin RPC operation (refer to
  // YBClientBuilder::Data::default_rpc_timeout_). Which means any admin command was limited
  // by the mentioned 60 seconds even if a user was specified the higher timeout.
  // In case of timeout by admin command timeout value, the admin tool gets the error:
  // (1) `Timed out waiting for FlushTables`.
  // In case of timeout by default_rpc_timeout_ value, the admin tool gets the error like:
  // (2) `Flush operation for id: 9be5b8cbd0d14cfea3371da1a6cbccab failed`.
  // After the fix, only the error (1) is thrown for timed out commands.
  constexpr size_t kAdminRpcTimeout  = 4000;
  constexpr size_t kAdminCmdTimeout  = 2 * kAdminRpcTimeout;
  constexpr size_t kCompactionPause  = 1.5 * kAdminCmdTimeout;
  constexpr size_t kMasterRpcTimeout = kAdminRpcTimeout / 2;

  // Update flags.
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "master_ts_rpc_timeout_ms", std::to_string(kMasterRpcTimeout)));
  ASSERT_OK(cluster_->SetFlagOnTServers(
      "TEST_pause_tablet_compact_flush_ms", std::to_string(kCompactionPause)));
  SleepFor(MonoDelta::FromSeconds(1));

  const auto before_ts = DateTime::TimestampNow();
  auto result = CallAdmin(
      "--yb_client_admin_rpc_timeout_sec",
      std::to_string(kAdminRpcTimeout / MonoTime::kMillisecondsPerSecond),
      "compact_table", kTableName.namespace_name(), kTableName.table_name(),
      std::to_string(kAdminCmdTimeout / MonoTime::kMillisecondsPerSecond));
  auto duration = MonoDelta::FromMicroseconds(DateTime::TimestampNow().value() - before_ts.value());
  LOG(INFO) << "Result: " << result;

  // It is expected to always get the timeout error since kAdminCmdTimeout < kCompactionPause.
  bool timed_out = !result.ok() &&
                    result.status().message().Contains("Timed out waiting for FlushTables");
  ASSERT_TRUE(timed_out);

  // The expectation is to have the call's duration approximately kAdminCmdTimeout amount.
  ASSERT_GT(duration, MonoDelta::FromMilliseconds(kAdminCmdTimeout));
  ASSERT_LT(duration, MonoDelta::FromMilliseconds(kAdminCmdTimeout * 1.25));

  // Wait for some time to let the compaction be unpaused and completed.
  SleepFor(MonoDelta::FromMilliseconds(1.1 * (kCompactionPause - kAdminCmdTimeout)));
}

Status AdminCliTest::TestDumpSysCatalogEntry(const std::string& cluster_uuid) {
  const auto dump_file_path = tmp_dir_ / Format("$0-", kClusterConfigEntryTypeName);

  // We should be able to dump the data while not in emergency_repair_mode.
  auto output =
      VERIFY_RESULT(CallAdmin("dump_sys_catalog_entries", kClusterConfigEntryTypeName, *tmp_dir_));
  SCHECK_STR_CONTAINS(output, dump_file_path);

  auto file_contents = VERIFY_RESULT(ReadFileToString(dump_file_path));
  SCHECK_STR_CONTAINS(file_contents, cluster_uuid);
  return Status::OK();
}

TEST_F(AdminCliTest, TestDumpSysCatalogEntryInNonEmergencyMode) {
  BuildAndStart();
  const auto cluster_uuid = ASSERT_RESULT(GetClusterUuid());
  ASSERT_OK(TestDumpSysCatalogEntry(cluster_uuid));
}

TEST_F(AdminCliTest, TestDumpSysCatalogEntryInEmergencyMode) {
  BuildAndStart();
  const auto cluster_uuid = ASSERT_RESULT(GetClusterUuid());
  ASSERT_OK(RestartMaster(EmergencyRepairMode::kTrue));
  ASSERT_OK(TestDumpSysCatalogEntry(cluster_uuid));
  ASSERT_OK(RestartMaster(EmergencyRepairMode::kFalse));
}

// Make sure we cannot write to sys catalog when not in emergency repair mode.
TEST_F(AdminCliTest, BlockWriteToSysCatalogEntryInNonEmergencyMode) {
  BuildAndStart();

  auto env = Env::Default();
  const auto dump_file_path = tmp_dir_ / Format("$0-", kClusterConfigEntryTypeName);

  master::SysClusterConfigEntryPB dummy_cluster_config;
  dummy_cluster_config.set_cluster_uuid(Uuid::Generate().ToString());

  ASSERT_OK(WriteStringToFileSync(env, dummy_cluster_config.DebugString(), dump_file_path));

  const auto kExpectedError = "Updating sys_catalog is only allowed in emergency repair mode";

  ASSERT_NOK_STR_CONTAINS(
      CallAdmin("write_sys_catalog_entry", "delete", kClusterConfigEntryTypeName, "", "", "force"),
      kExpectedError);

  ASSERT_NOK_STR_CONTAINS(
      CallAdmin(
          "write_sys_catalog_entry", "insert", kClusterConfigEntryTypeName, "dummy", dump_file_path,
          "force"),
      kExpectedError);

  ASSERT_NOK_STR_CONTAINS(
      CallAdmin(
          "write_sys_catalog_entry", "update", kClusterConfigEntryTypeName, "", dump_file_path,
          "force"),
      kExpectedError);
}

// Test insert and delete of CatalogEntity.
TEST_F(AdminCliTest, TestInsertDeleteSysCatalogEntry) {
  BuildAndStart();
  ASSERT_OK(RestartMaster(EmergencyRepairMode::kTrue));

  auto env = Env::Default();
  const auto second_cluster_config_id = "second_cc";

  master::SysClusterConfigEntryPB second_cluster_config;
  second_cluster_config.set_cluster_uuid(Uuid::Generate().ToString());
  const auto file_path =
      tmp_dir_ / Format("$0-$1", kClusterConfigEntryTypeName, second_cluster_config_id);

  ASSERT_OK(WriteStringToFileSync(env, second_cluster_config.DebugString(), file_path));

  ASSERT_OK(CallAdmin(
      "write_sys_catalog_entry", "insert", kClusterConfigEntryTypeName, second_cluster_config_id,
      file_path, "force"));

  auto validate_dump_cluster_config = [&]() -> Status {
    RETURN_NOT_OK(env->DeleteFile(file_path));
    auto output = VERIFY_RESULT(CallAdmin(
        "dump_sys_catalog_entries", kClusterConfigEntryTypeName, *tmp_dir_,
        second_cluster_config_id));
    SCHECK_STR_CONTAINS(output, file_path);
    auto file_contents = VERIFY_RESULT(ReadFileToString(file_path));
    SCHECK_STR_CONTAINS(file_contents, second_cluster_config.cluster_uuid());
    return Status::OK();
  };

  // Dump the new entry to make sure it was updated.
  ASSERT_OK(validate_dump_cluster_config());

  // Restart the master and dump the new entry to make sure it was persisted.
  auto* leader_master = cluster_->GetLeaderMaster();
  leader_master->Shutdown();
  ASSERT_OK(leader_master->Restart());

  ASSERT_OK(validate_dump_cluster_config());

  // Delete the second entry.
  ASSERT_OK(CallAdmin(
      "write_sys_catalog_entry", "delete", kClusterConfigEntryTypeName, second_cluster_config_id,
      "", "force"));

  auto output = ASSERT_RESULT(CallAdmin(
      "dump_sys_catalog_entries", kClusterConfigEntryTypeName, *tmp_dir_,
      second_cluster_config_id));
  ASSERT_STR_CONTAINS(output, "Found 0 entries of type CLUSTER_CONFIG");

  // Restart the master to make sure it starts correctly. If the delete did not succeed the master
  // would crash and fail to start.
  ASSERT_OK(RestartMaster(EmergencyRepairMode::kFalse));
  ASSERT_OK(cluster_->WaitForTabletServerCount(cluster_->num_tablet_servers(), 30s));
}

// Test insert and delete of TableEntry.
TEST_F(AdminCliTest, TestInsertDeleteTableEntry) {
  const auto new_table_name = "new_table";
  const auto new_table_id = Uuid::Generate().ToString();
  auto env = Env::Default();

  BuildAndStart();
  const auto tables =
      ASSERT_RESULT(client_->ListTables(kTableName.table_name(), /* exclude_ysql */ true));
  ASSERT_EQ(1, tables.size());
  const auto& table_id = tables.front().table_id();

  ASSERT_OK(RestartMaster(EmergencyRepairMode::kTrue));

  const auto folder_path = *tmp_dir_;
  const auto existing_table_file_path = tmp_dir_ / Format("$0-$1", kTableEntryTypeName, table_id);
  const auto new_table_file_path = tmp_dir_ / Format("$0-$1", kTableEntryTypeName, new_table_id);
  auto output = ASSERT_RESULT(
      CallAdmin("dump_sys_catalog_entries", kTableEntryTypeName, folder_path, table_id));
  ASSERT_STR_CONTAINS(output, existing_table_file_path);
  auto file_contents = ASSERT_RESULT(ReadFileToString(existing_table_file_path));
  boost::replace_all(file_contents, table_id, new_table_id);
  boost::replace_all(file_contents, kTableName.table_name(), new_table_name);
  ASSERT_OK(WriteStringToFileSync(env, file_contents, new_table_file_path));

  ASSERT_OK(CallAdmin(
      "write_sys_catalog_entry", "insert", kTableEntryTypeName, new_table_id, new_table_file_path,
      "force"));

  auto validate_dump_cluster_config = [&]() -> Status {
    RETURN_NOT_OK(env->DeleteFile(new_table_file_path));
    auto output = VERIFY_RESULT(
        CallAdmin("dump_sys_catalog_entries", kTableEntryTypeName, folder_path, new_table_id));
    SCHECK_STR_CONTAINS(output, new_table_file_path);
    auto file_contents = VERIFY_RESULT(ReadFileToString(new_table_file_path));
    SCHECK_STR_CONTAINS(file_contents, new_table_name);
    return Status::OK();
  };

  // Dump the new entry to make sure it was inserted.
  ASSERT_OK(validate_dump_cluster_config());

  // Restart the master and dump the new entry to make sure it was persisted.
  auto* leader_master = cluster_->GetLeaderMaster();
  leader_master->Shutdown();
  ASSERT_OK(leader_master->Restart());

  ASSERT_OK(validate_dump_cluster_config());

  // Delete the new table.
  ASSERT_OK(CallAdmin(
      "write_sys_catalog_entry", "delete", kTableEntryTypeName, new_table_id, "", "force"));

  output = ASSERT_RESULT(
      CallAdmin("dump_sys_catalog_entries", kTableEntryTypeName, *tmp_dir_, new_table_id));
  ASSERT_STR_CONTAINS(output, "Found 0 entries of type TABLE");
}

// Update the ClusterConfig entry in the sys catalog and verify that the change is persisted across
// master restarts.
TEST_F(AdminCliTest, TestUpdateSysCatalogEntry) {
  BuildAndStart();
  const auto old_cluster_uuid = ASSERT_RESULT(GetClusterUuid());
  ASSERT_OK(RestartMaster(EmergencyRepairMode::kTrue));

  auto env = Env::Default();
  const auto file_path = tmp_dir_ / Format("$0-", kClusterConfigEntryTypeName);

  auto new_cluster_uuid = Uuid::Generate().ToString();
  LOG(INFO) << "Replacing cluster_uuid " << old_cluster_uuid << " with " << new_cluster_uuid;

  master::SysClusterConfigEntryPB new_cluster_config;
  new_cluster_config.set_cluster_uuid(new_cluster_uuid);
  ASSERT_OK(WriteStringToFileSync(env, new_cluster_config.DebugString(), file_path));

  auto output = ASSERT_RESULT(CallAdmin(
      "write_sys_catalog_entry", "update", kClusterConfigEntryTypeName, "", file_path, "force"));
  ASSERT_STR_CONTAINS(output, new_cluster_uuid);

  ASSERT_OK(RestartMaster(EmergencyRepairMode::kFalse));

  const auto cluster_uuid = ASSERT_RESULT(GetClusterUuid());
  ASSERT_EQ(cluster_uuid, new_cluster_uuid);

  ASSERT_OK(env->DeleteFile(file_path));
  output =
      ASSERT_RESULT(CallAdmin("dump_sys_catalog_entries", kClusterConfigEntryTypeName, *tmp_dir_));
  ASSERT_STR_CONTAINS(output, file_path);
  auto file_contents = ASSERT_RESULT(ReadFileToString(file_path));
  ASSERT_STR_NOT_CONTAINS(file_contents, old_cluster_uuid);
  ASSERT_STR_CONTAINS(file_contents, new_cluster_uuid);
}

TEST_F(AdminCliTest, TestRemoveTabletServer) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 1;
  BuildAndStart({}, {"--enable_load_balancing=false", "--tserver_unresponsive_timeout_ms=5000"});
  ASSERT_OK(cluster_->AddTabletServer(true));
  auto added_tserver = cluster_->tablet_server(cluster_->num_tablet_servers() - 1);
  ASSERT_OK(cluster_->AddTServerToBlacklist(cluster_->master(), added_tserver));
  added_tserver->Shutdown();
  auto cluster_client =
      master::MasterClusterClient(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  ASSERT_OK(WaitFor(
      [&cluster_client, added_tserver]() -> Result<bool> {
        auto tserver = VERIFY_RESULT(cluster_client.GetTabletServer(added_tserver->uuid()));
        return tserver && !tserver->alive();
      },
      30s, "tserver not present or still alive"));
  ASSERT_RESULT(CallAdmin("remove_tablet_server", added_tserver->uuid()));
  auto find_tserver_result = ASSERT_RESULT(cluster_client.GetTabletServer(added_tserver->uuid()));
  EXPECT_EQ(find_tserver_result, std::nullopt);
}

TEST_F(AdminCliTest, TestDisallowImplicitStreamCreation) {
  std::string test_namespace = "pg_namespace_cdc";
  BuildAndStart();
  ASSERT_OK(client_->CreateNamespace(test_namespace, YQL_DATABASE_PGSQL));

  ASSERT_NOK(CallAdmin("create_change_data_stream", "ysql." + test_namespace, "IMPLICIT"));
}

TEST_F(AdminCliTest, TestAllowImplicitStreamCreationWhenFlagEnabled) {
  std::string test_namespace = "pg_namespace_cdc";
  BuildAndStart(
      {"--cdc_enable_implicit_checkpointing=true"}, {});
  ASSERT_OK(client_->CreateNamespace(test_namespace, YQL_DATABASE_PGSQL));

  auto output =
      ASSERT_RESULT(CallAdmin("create_change_data_stream", "ysql." + test_namespace, "IMPLICIT"));

  ASSERT_STR_CONTAINS(
      output, "Creation of streams with IMPLICIT checkpointing is deprecated");
}

// Test yb-admin get_table_hash command for YCQL table.
TEST_F(AdminCliTest, TestGetTableXorHash) {
  BuildAndStart();
  const auto table_name =
      YBTableName(YQLDatabase::YQL_DATABASE_CQL, kTableName.namespace_name(), "my_table");

  client::TableHandle table;
  const auto num_tablets = 4;
  ASSERT_OK(table.Create(table_name, num_tablets, client::YBSchema(schema_), client_.get()));

  TestYcqlWorkload workload(cluster_.get());
  workload.set_table_name(table_name);
  workload.Setup();
  workload.Start();
  workload.WaitInserted(100);
  auto ht1 = ASSERT_RESULT(cluster_->master()->GetServerTime());
  // Insert 100 more rows.
  workload.WaitInserted(workload.rows_inserted() + 100);
  workload.StopAndJoin();

  auto rows_inserted = boost::size(client::TableRange(table));
  ASSERT_GE(rows_inserted, 200);

  auto tables = ASSERT_RESULT(client_->ListTables(table_name.table_name()));
  ASSERT_EQ(1, tables.size());
  const auto table_id = tables.front().table_id();

  auto extract_from_output = [&](const std::string& output) -> std::pair<uint64_t, uint64_t> {
    LOG(INFO) << "Command output: " << output;
    // Count the number of line with `Tablet ID:`
    // Read HT: { days: 20466 time: 15:34:35.245376 }
    // Tablet ID: 9bd7b975e43e4f41aa41d8bd9c2f8fb0
    // Row count: 16100
    // XOR hash: 5512406178816

    // Total row count: 16100
    // Total XOR hash: 5512406178816
    size_t tablet_id_lines = 0;
    const std::string table_id_str = "Tablet ID:";
    size_t pos = 0;
    while ((pos = output.find(table_id_str, pos)) != std::string::npos) {
      ++tablet_id_lines;
      pos += table_id_str.length();
    }
    CHECK_EQ(tablet_id_lines, num_tablets);

    // Extract the row count from the output.
    // Total row count: 100
    // Total XOR hash: 1234567890
    const auto total_row_count_prefix = "Total row count: ";
    const auto total_xor_hash_prefix = "Total XOR hash: ";
    auto row_count_pos = output.find(total_row_count_prefix);
    auto xor_hash_pos = output.find(total_xor_hash_prefix);
    auto row_count = std::stoull(output.substr(row_count_pos + strlen(total_row_count_prefix)));
    auto xor_hash = std::stoull(output.substr(xor_hash_pos + strlen(total_xor_hash_prefix)));
    LOG(INFO) << "Row count: " << row_count << ", XOR hash: " << xor_hash;
    return std::make_pair(row_count, xor_hash);
  };
  auto output = ASSERT_RESULT(CallAdmin("get_table_hash", table_id));
  auto [row_count, xor_hash] = extract_from_output(output);
  ASSERT_EQ(row_count, rows_inserted);
  ASSERT_NE(xor_hash, 0);

  output = ASSERT_RESULT(CallAdmin("get_table_hash", table_id, ht1.ToUint64()));
  auto [row_count2, xor_hash2] = extract_from_output(output);
  ASSERT_LT(row_count2, rows_inserted);
  ASSERT_GT(row_count2, 100);
  ASSERT_NE(xor_hash2, 0);
  ASSERT_NE(xor_hash, xor_hash2);
}

}  // namespace tools
}  // namespace yb
