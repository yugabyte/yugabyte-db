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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/integration-tests/external_mini_cluster.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <gtest/gtest.h>

#include "yb/client/client.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/algorithm.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/singleton.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/util.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_cluster_client.h"
#include "yb/master/master_rpc.h"
#include "yb/master/sys_catalog.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"
#include "yb/rpc/secure.h"

#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"
#include "yb/util/flags.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

#define YB_FORWARD_FLAG(flag_name) \
  "--" BOOST_PP_STRINGIZE(flag_name) "="s + FlagToString(BOOST_PP_CAT(FLAGS_, flag_name))


using namespace std::literals;  // NOLINT
using namespace yb::size_literals;  // NOLINT

using std::atomic;
using std::lock_guard;
using std::mutex;
using std::shared_ptr;
using std::string;
using std::thread;
using std::unique_ptr;
using std::vector;
using std::min;
using std::map;
using std::ostream;

using yb::master::GetLeaderMasterRpc;
using yb::master::IsInitDbDoneRequestPB;
using yb::master::IsInitDbDoneResponsePB;
using yb::server::ServerStatusPB;
using yb::tserver::ListTabletsRequestPB;
using yb::tserver::ListTabletsResponsePB;
using yb::tserver::TabletServerErrorPB;
using yb::tserver::TabletServerServiceProxy;
using yb::consensus::ConsensusServiceProxy;
using yb::consensus::RaftPeerPB;
using yb::consensus::ChangeConfigRequestPB;
using yb::consensus::ChangeConfigResponsePB;
using yb::consensus::ChangeConfigType;
using yb::consensus::GetLastOpIdRequestPB;
using yb::consensus::GetLastOpIdResponsePB;
using yb::consensus::LeaderStepDownRequestPB;
using yb::consensus::LeaderStepDownResponsePB;
using yb::consensus::RunLeaderElectionRequestPB;
using yb::consensus::RunLeaderElectionResponsePB;
using yb::master::IsMasterLeaderReadyRequestPB;
using yb::master::IsMasterLeaderReadyResponsePB;
using yb::master::GetMasterClusterConfigRequestPB;
using yb::master::GetMasterClusterConfigResponsePB;
using yb::master::ChangeMasterClusterConfigRequestPB;
using yb::master::ChangeMasterClusterConfigResponsePB;
using yb::master::SysClusterConfigEntryPB;
using yb::tserver::ListTabletsForTabletServerRequestPB;
using yb::tserver::ListTabletsForTabletServerResponsePB;
using yb::master::ListMastersRequestPB;
using yb::master::ListMastersResponsePB;
using yb::tserver::TabletServerErrorPB;
using yb::rpc::RpcController;

typedef ListTabletsResponsePB::StatusAndSchemaPB StatusAndSchemaPB;

DECLARE_string(vmodule);
DECLARE_int32(replication_factor);
DECLARE_bool(mem_tracker_logging);
DECLARE_bool(mem_tracker_log_stack_trace);
DECLARE_string(minicluster_daemon_id);
DECLARE_bool(use_libbacktrace);
DECLARE_bool(never_fsync);
DECLARE_bool(allow_insecure_connections);
DECLARE_bool(node_to_node_encryption_use_client_certificates);
DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_dir);

DECLARE_int64(outbound_rpc_block_size);
DECLARE_int64(outbound_rpc_memory_limit);

DECLARE_string(dynamically_linked_exe_suffix);

namespace yb {

static const char* const kMasterBinaryNamePrefix = "yb-master";
static const char* const kTabletServerBinaryNamePrefix = "yb-tserver";
static MonoDelta kTabletServerRegistrationTimeout = 60s;

constexpr size_t kDefaultMemoryLimitHardBytes = NonTsanVsTsan(1_GB, 512_MB);

void AppendCsvFlagValue(
    std::vector<std::string>& flag_list, const std::string& flag_name,
    const std::string& value_to_add) {
  for (auto& flag : flag_list) {
    if (flag.starts_with(Format("--$0=", flag_name))) {
      flag += Format(",$0", value_to_add);
      return;
    }
  }
  flag_list.push_back(Format("--$0=$1", flag_name, value_to_add));
}

void AppendFlagToAllowedPreviewFlagsCsv(
    std::vector<std::string>& flag_list, const std::string& flag_to_add) {
  AppendCsvFlagValue(flag_list, "allowed_preview_flags_csv", flag_to_add);
}

namespace {

constexpr auto kDefaultTimeout = 10s * kTimeMultiplier;

std::vector<std::string> FsRootDirs(const std::string& data_dir,
                                    uint16_t num_drives) {
  if (num_drives == 1) {
    return vector<string>{data_dir};
  }
  vector<string> data_dirs;
  for (int drive =  1; drive <= num_drives; ++drive) {
    data_dirs.push_back(JoinPathSegments(data_dir, Format("d-$0", drive)));
  }
  return data_dirs;
}

std::vector<std::string> FsDataDirs(const std::string& data_dir,
                                    const std::string& server_type,
                                    uint16_t num_drives) {
  if (num_drives == 1) {
    return vector<string>{GetServerTypeDataPath(data_dir, server_type)};
  }
  vector<string> data_dirs;
  for (int drive =  1; drive <= num_drives; ++drive) {
    data_dirs.push_back(GetServerTypeDataPath(
                          JoinPathSegments(data_dir, Format("d-$0", drive)), server_type));
  }
  return data_dirs;
}

std::string GetMasterBinaryName() {
  return kMasterBinaryNamePrefix + FLAGS_dynamically_linked_exe_suffix;
}

std::string GetTServerBinaryName() {
  return kTabletServerBinaryNamePrefix + FLAGS_dynamically_linked_exe_suffix;
}

}  // anonymous namespace

// ------------------------------------------------------------------------------------------------
// ExternalMiniClusterOptions
// ------------------------------------------------------------------------------------------------

Status ExternalMiniClusterOptions::RemovePort(const uint16_t port) {
  auto iter = std::find(master_rpc_ports.begin(), master_rpc_ports.end(), port);

  if (iter == master_rpc_ports.end()) {
    return STATUS(InvalidArgument, Format(
        "Port to be removed '$0' not found in existing list of $1 masters.",
        port, num_masters));
  }

  master_rpc_ports.erase(iter);
  --num_masters;

  return Status::OK();
}

Status ExternalMiniClusterOptions::AddPort(const uint16_t port) {
  auto iter = std::find(master_rpc_ports.begin(), master_rpc_ports.end(), port);

  if (iter != master_rpc_ports.end()) {
    return STATUS(InvalidArgument, Format(
        "Port to be added '$0' already found in the existing list of $1 masters.",
        port, num_masters));
  }

  master_rpc_ports.push_back(port);
  ++num_masters;

  return Status::OK();
}

void ExternalMiniClusterOptions::AdjustMasterRpcPorts() {
  if (master_rpc_ports.size() == 1 && master_rpc_ports[0] == 0) {
    // Add missing master ports to avoid errors when we try to start the cluster.
    while (master_rpc_ports.size() < num_masters) {
      master_rpc_ports.push_back(0);
    }
  }
}

// ------------------------------------------------------------------------------------------------
// ExternalMiniCluster
// ------------------------------------------------------------------------------------------------

ExternalMiniCluster::ExternalMiniCluster(const ExternalMiniClusterOptions& opts) : opts_(opts) {
  opts_.AdjustMasterRpcPorts();
  // These "extra mini cluster options" are added in the end of the command line.
  const auto common_extra_flags = {
      "--TEST_running_test=true"s,
      "--enable_tracing"s,
      Format("--memory_limit_hard_bytes=$0", kDefaultMemoryLimitHardBytes),
      Format("--never_fsync=$0", FLAGS_never_fsync),
      (opts.log_to_file ? "--alsologtostderr"s : "--logtostderr"s),
      Format("--rpc_slow_query_threshold_ms=$0", NonTsanVsTsan("10000", "20000"))};
  for (auto* extra_flags : {&opts_.extra_master_flags, &opts_.extra_tserver_flags}) {
    // Common default extra flags are inserted in the beginning so that they can be overridden by
    // caller-specified flags.
    extra_flags->insert(extra_flags->begin(),
                        common_extra_flags.begin(),
                        common_extra_flags.end());
  }
  // Given how little memory kDefaultMemoryLimitHardBytes provides, we will need to use more of
  // our memory for per-tablet overhead.
  opts_.extra_tserver_flags.insert(
      opts_.extra_tserver_flags.begin(), "--tablet_overhead_size_percentage=30"s);
  AddExtraFlagsFromEnvVar("YB_EXTRA_MASTER_FLAGS", &opts_.extra_master_flags);
  AddExtraFlagsFromEnvVar("YB_EXTRA_TSERVER_FLAGS", &opts_.extra_tserver_flags);
}

ExternalMiniCluster::~ExternalMiniCluster() {
  Shutdown();
  if (messenger_holder_) {
    messenger_holder_->Shutdown();
  }
}

Status ExternalMiniCluster::DeduceBinRoot(std::string* ret) {
  string exe;
  RETURN_NOT_OK(Env::Default()->GetExecutablePath(&exe));
  *ret = DirName(exe) + "/../bin";
  return Status::OK();
}

void ExternalMiniCluster::SetDaemonBinPath(const std::string& bin_path) {
  daemon_bin_path_ = bin_path;
}

std::string ExternalMiniCluster::GetClusterDataDirName() const {
  if (opts_.cluster_id == "") {
    return "minicluster-data";
  }
  return Format("minicluster-data-$0", opts_.cluster_id);
}

Status ExternalMiniCluster::HandleOptions() {
  daemon_bin_path_ = opts_.daemon_bin_path;
  if (daemon_bin_path_.empty()) {
    RETURN_NOT_OK(DeduceBinRoot(&daemon_bin_path_));
  }

  data_root_ = opts_.data_root;
  if (data_root_.empty()) {
    // If they don't specify a data root, use the current gtest directory.
    data_root_ = JoinPathSegments(GetTestDataDirectory(), GetClusterDataDirName());

    // If "data_root_counter" is non-negative, and the auto-generated "data_root_" directory already
    // exists, create a subdirectory using the counter value as its name. The caller should maintain
    // this counter and increment it for each test run.
    if (opts_.data_root_counter >= 0) {
      struct stat sb;
      if (stat(data_root_.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
        data_root_ = Format("$0/$1", data_root_, opts_.data_root_counter);
        CHECK_EQ(mkdir(data_root_.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH), 0);
      }
    }
  }

  return Status::OK();
}

Status ExternalMiniCluster::Start(rpc::Messenger* messenger) {
  CHECK(masters_.empty()) << "Masters are not empty (size: " << masters_.size()
      << "). Maybe you meant Restart()?";
  CHECK(tablet_servers_.empty()) << "Tablet servers are not empty (size: "
      << tablet_servers_.size() << "). Maybe you meant Restart()?";
  RETURN_NOT_OK(HandleOptions());
  FLAGS_replication_factor =
    opts_.replication_factor > 0 ? opts_.replication_factor : narrow_cast<int>(opts_.num_masters);

  if (messenger == nullptr) {
    rpc::MessengerBuilder builder("minicluster-messenger");
    builder.set_num_reactors(1);
    messenger_holder_ = VERIFY_RESULT_PREPEND(
        builder.Build(), "Failed to start Messenger for minicluster");
    messenger_ = messenger_holder_.get();
  } else {
    messenger_holder_ = nullptr;
    messenger_ = messenger;
  }
  proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_);

  RETURN_NOT_OK(Env::Default()->CreateDirs(data_root_));

  LOG(INFO) << "Starting cluster with option bind_to_unique_loopback_addresses="
      << (opts_.bind_to_unique_loopback_addresses ? "true" : "false");

  LOG(INFO) << "Starting " << opts_.num_masters << " masters";
  RETURN_NOT_OK_PREPEND(StartMasters(), "Failed to start masters.");

  if (opts_.num_tablet_servers > 0) {
    LOG(INFO) << "Starting " << opts_.num_tablet_servers << " tablet servers";

    for (size_t i = 1; i <= opts_.num_tablet_servers; i++) {
      RETURN_NOT_OK_PREPEND(
          AddTabletServer(ExternalMiniClusterOptions::kDefaultStartCqlProxy),
          Format("Failed starting tablet server $0", i));
    }
    RETURN_NOT_OK(WaitForTabletServerCount(
        opts_.num_tablet_servers, kTabletServerRegistrationTimeout));
  } else {
    LOG(INFO) << "No need to start tablet servers";
  }

  running_ = true;
  return Status::OK();
}

void ExternalMiniCluster::Shutdown(NodeSelectionMode mode, RequireExitCode0 require_exit_code_0) {
  // TODO: in the regular MiniCluster Shutdown is a no-op if running_ is false.
  // Therefore, in case of an error during cluster startup behavior might be different.
  if (mode == ALL) {
    for (const scoped_refptr<ExternalMaster>& master : masters_) {
      if (master) {
        master->Shutdown(SafeShutdown::kTrue, require_exit_code_0);
      }
    }
  }

  for (const scoped_refptr<ExternalTabletServer>& ts : tablet_servers_) {
    ts->Shutdown(SafeShutdown::kTrue, require_exit_code_0);
  }

  for (const auto& yb_controller : yb_controller_servers_) {
    yb_controller->Shutdown();
  }
  running_ = false;
}

Status ExternalMiniCluster::Restart() {
  LOG(INFO) << "Restarting cluster with " << masters_.size() << " masters.";
  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    if (master && master->IsShutdown()) {
      RETURN_NOT_OK_PREPEND(master->Restart(), "Cannot restart master bound at: " +
                                               master->bound_rpc_hostport().ToString());
    }
  }

  for (const scoped_refptr<ExternalTabletServer>& ts : tablet_servers_) {
    if (ts->IsShutdown()) {
      RETURN_NOT_OK_PREPEND(ts->Restart(), "Cannot restart tablet server bound at: " +
                                           ts->bound_rpc_hostport().ToString());
    }
  }

  RETURN_NOT_OK(WaitForTabletServerCount(tablet_servers_.size(), kTabletServerRegistrationTimeout));

  if (UseYbController()) {
    for (const auto& yb_controller : yb_controller_servers_) {
      if (yb_controller->IsShutdown()) {
        RETURN_NOT_OK_PREPEND(
            yb_controller->Restart(),
            "Cannot restart YB Controller server bound at: " + yb_controller->GetServerAddress());
      }
    }
  }

  // Give some more time for the cluster to be ready. If we proceed to run the
  // unit test prematurely before the master/tserver are fully ready, deadlock
  // can happen which leads to test flakiness.
  SleepFor(2s * kTimeMultiplier);
  running_ = true;
  return Status::OK();
}

string ExternalMiniCluster::GetBinaryPath(const string& binary) const {
  CHECK(!daemon_bin_path_.empty());
  string default_path = JoinPathSegments(daemon_bin_path_, binary);
  if (Env::Default()->FileExists(default_path)) {
    return default_path;
  }

  // In CLion-based builds we sometimes have to look for the binary in other directories.
  string alternative_dir;
  if (binary == "yb-master") {
    alternative_dir = "master";
  } else if (binary == "yb-tserver") {
    alternative_dir = "tserver";
  } else {
    LOG(WARNING) << "Default path " << default_path << " for binary " << binary <<
      " does not exist, and no alternative directory is available for this binary";
    return default_path;
  }

  string alternative_path = JoinPathSegments(daemon_bin_path_,
    "../" + alternative_dir + "/" + binary);
  if (Env::Default()->FileExists(alternative_path)) {
    LOG(INFO) << "Default path " << default_path << " for binary " << binary <<
      " does not exist, using alternative location: " << alternative_path;
    return alternative_path;
  } else {
    LOG(WARNING) << "Neither " << default_path << " nor " << alternative_path << " exist";
    return default_path;
  }
}

string ExternalMiniCluster::GetDataPath(const string& daemon_id) const {
  CHECK(!data_root_.empty());
  return JoinPathSegments(data_root_, daemon_id);
}

std::string ExternalMiniCluster::GetMasterBinaryPath() const {
  return GetBinaryPath(GetMasterBinaryName());
}

std::string ExternalMiniCluster::GetTServerBinaryPath() const {
  return GetBinaryPath(GetTServerBinaryName());
}

bool ExternalMiniCluster::IsYsqlConnMgrEnabledInTests() const {
  if (!opts_.enable_ysql) {
    return false;
  }
  static const char *enableYsqlConnMgr = getenv("YB_ENABLE_YSQL_CONN_MGR_IN_TESTS");
  if (enableYsqlConnMgr != NULL
      && strcasecmp(enableYsqlConnMgr, "true") == 0) {
      return true;
  }
  return false;
}

namespace {
vector<string> SubstituteInFlags(const vector<string>& orig_flags, size_t index) {
  string str_index = std::to_string(index);
  vector<string> ret;
  for (const string& orig : orig_flags) {
    ret.push_back(StringReplace(orig, "${index}", str_index, true));
  }
  return ret;
}

}  // anonymous namespace

Result<ExternalMasterPtr> ExternalMiniCluster::StartMaster(
    const std::string& peer_addrs, uint16_t rpc_port, bool shell_mode) {
  if (rpc_port == 0) {
    rpc_port = AllocateFreePort();
  }

  uint16_t http_port = AllocateFreePort();
  LOG(INFO) << "Using " << YB_STRUCT_TO_STRING(peer_addrs, rpc_port, shell_mode)
            << " to start a new external mini-cluster master with index " << add_new_master_at_;

  string addr = MasterAddressForPort(rpc_port);
  string exe = GetBinaryPath(GetMasterBinaryName());

  vector<string> flags = opts_.extra_master_flags;
  // Disable WAL fsync for tests
  flags.push_back("--durable_wal_write=false");
  flags.push_back("--enable_leader_failure_detection=true");
  if (opts_.replication_factor > 0) {
    flags.push_back(Format("--replication_factor=$0", opts_.replication_factor));
  }
  // Limit number of transaction table tablets to help avoid timeouts.
  flags.push_back(
      Format("--transaction_table_num_tablets=$0", NumTabletsPerTransactionTable(opts_)));
  // For sanitizer builds, it is easy to overload the master, leading to quorum changes.
  // This could end up breaking ever trivial DDLs like creating an initial table in the cluster.
  if (IsSanitizer()) {
    flags.push_back("--leader_failure_max_missed_heartbeat_periods=10");
  }
  if (opts_.enable_ysql) {
    flags.push_back("--enable_ysql=true");
    flags.push_back("--master_auto_run_initdb");
    if (opts_.enable_ysql_auth) {
      flags.push_back("--ysql_enable_auth=true");
    }
  } else {
    flags.push_back("--enable_ysql=false");
  }

  ExternalMasterPtr master = new ExternalMaster(
      add_new_master_at_, messenger_, proxy_cache_.get(), exe,
      GetDataPath(Format("master-$0", add_new_master_at_)),
      SubstituteInFlags(flags, add_new_master_at_), addr, http_port, peer_addrs);

  RETURN_NOT_OK(master->Start(shell_mode));
  add_new_master_at_++;

  return master;
}

Result<ExternalMasterPtr> ExternalMiniCluster::StartShellMaster() {
  return StartMaster(/*peer_addrs=*/"", /*rpc_port=*/0, /*shell_mode=*/true);
}

std::string ExternalMiniCluster::MasterAddressForPort(uint16_t port) const {
  return Format(opts_.use_even_ips ? "127.0.0.2:$0" : "127.0.0.1:$0", port);
}

Status ExternalMiniCluster::CheckPortAndMasterSizes() const {
  if (opts_.num_masters != masters_.size() ||
      opts_.num_masters != opts_.master_rpc_ports.size()) {
    string fatal_err_msg = Format(
        "Mismatch number of masters in options $0, compared to masters vector $1 or rpc ports $2",
        opts_.num_masters, masters_.size(), opts_.master_rpc_ports.size());
    LOG(FATAL) << fatal_err_msg;
  }

  return Status::OK();
}

Status ExternalMiniCluster::AddMaster(const ExternalMasterPtr& master) {
  auto iter = std::find_if(masters_.begin(), masters_.end(), MasterComparator(master));

  if (iter != masters_.end()) {
    return STATUS(InvalidArgument, Format(
        "Master to be added '$0' already found in existing list of $1 masters.",
        master->bound_rpc_hostport().ToString(), opts_.num_masters));
  }

  RETURN_NOT_OK(opts_.AddPort(master->bound_rpc_hostport().port()));
  masters_.push_back(master);

  RETURN_NOT_OK(CheckPortAndMasterSizes());

  return Status::OK();
}

Status ExternalMiniCluster::RemoveMaster(const ExternalMasterPtr& master) {
  auto iter = std::find_if(masters_.begin(), masters_.end(), MasterComparator(master));

  if (iter == masters_.end()) {
    return STATUS(InvalidArgument, Format(
        "Master to be removed '$0' not found in existing list of $1 masters.",
        master->bound_rpc_hostport().ToString(), opts_.num_masters));
  }

  RETURN_NOT_OK(opts_.RemovePort(master->bound_rpc_hostport().port()));
  masters_.erase(iter);

  RETURN_NOT_OK(CheckPortAndMasterSizes());

  return Status::OK();
}

ConsensusServiceProxy ExternalMiniCluster::GetLeaderConsensusProxy() {
  return GetConsensusProxy(GetLeaderMaster());
}

ConsensusServiceProxy ExternalMiniCluster::GetConsensusProxy(ExternalDaemon* external_deamon) {
  return GetProxy<ConsensusServiceProxy>(external_deamon);
}

Status ExternalMiniCluster::StepDownMasterLeader(
    TabletServerErrorPB::Code* error_code, const std::string& new_leader_uuid) {
  ExternalMaster* leader = GetLeaderMaster();
  string leader_uuid = leader->uuid();
  auto host_port = leader->bound_rpc_addr();
  LeaderStepDownRequestPB lsd_req;
  lsd_req.set_tablet_id(yb::master::kSysCatalogTabletId);
  lsd_req.set_dest_uuid(leader_uuid);
  if (!new_leader_uuid.empty()) {
    lsd_req.set_new_leader_uuid(new_leader_uuid);
  }
  LeaderStepDownResponsePB lsd_resp;
  RpcController lsd_rpc;
  lsd_rpc.set_timeout(opts_.timeout);
  ConsensusServiceProxy proxy(proxy_cache_.get(), host_port);
  RETURN_NOT_OK(proxy.LeaderStepDown(lsd_req, &lsd_resp, &lsd_rpc));
  if (lsd_resp.has_error()) {
    LOG(ERROR) << "LeaderStepDown for " << leader_uuid << " for destination " << new_leader_uuid
               << " received error " << lsd_resp.error().ShortDebugString();
    *error_code = lsd_resp.error().code();
    return StatusFromPB(lsd_resp.error().status());
  }

  LOG(INFO) << "Leader at host/port '" << host_port
            << " stepped down in favor of node with uuid "
            << new_leader_uuid;

  return Status::OK();
}

Status ExternalMiniCluster::StepDownMasterLeaderAndWaitForNewLeader(
    const std::string& new_leader_uuid) {
  ExternalMaster* leader = GetLeaderMaster();
  string old_leader_uuid = leader->uuid();
  string leader_uuid = old_leader_uuid;
  TabletServerErrorPB::Code error_code = TabletServerErrorPB::UNKNOWN_ERROR;
  LOG(INFO) << "Starting step down of leader " << leader->bound_rpc_addr();

  // while loop will not be needed once JIRA ENG-49 is fixed.
  int iter = 1;
  while (leader_uuid == old_leader_uuid) {
    Status s = StepDownMasterLeader(&error_code, new_leader_uuid);
    // If step down hits any error except not-ready, exit.
    if (!s.ok() && error_code != TabletServerErrorPB::LEADER_NOT_READY_TO_STEP_DOWN) {
      return s;
    }
    sleep(3);  // TODO: add wait for election api.
    leader = GetLeaderMaster();
    leader_uuid = leader->uuid();
    LOG(INFO) << "Got new leader " << leader->bound_rpc_addr() << ", iter=" << iter;
    iter++;
  }

  return Status::OK();
}

Status ExternalMiniCluster::ChangeConfig(
    const ExternalMasterPtr& master, ChangeConfigType type, consensus::PeerMemberType member_type,
    bool use_hostport) {
  if (type != consensus::ADD_SERVER && type != consensus::REMOVE_SERVER) {
    return STATUS(InvalidArgument, Format("Invalid Change Config type $0", type));
  }

  ChangeConfigRequestPB req;
  ChangeConfigResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout);

  RaftPeerPB peer_pb;
  peer_pb.set_permanent_uuid(use_hostport ? "" : master->uuid());
  if (type == consensus::ADD_SERVER) {
    peer_pb.set_member_type(member_type);
  }
  HostPortToPB(master->bound_rpc_hostport(), peer_pb.mutable_last_known_private_addr()->Add());
  req.set_tablet_id(yb::master::kSysCatalogTabletId);
  req.set_type(type);
  req.set_use_host(use_hostport);
  *req.mutable_server() = peer_pb;

  // There could be timing window where we found the leader host/port, but an election in the
  // meantime could have made it step down. So we retry till we hit the leader correctly.
  int num_attempts = 1;
  while (true) {
    ExternalMaster* leader = GetLeaderMaster();
    auto leader_proxy = std::make_unique<ConsensusServiceProxy>(
        proxy_cache_.get(), leader->bound_rpc_addr());
    string leader_uuid = leader->uuid();

    if (type == consensus::REMOVE_SERVER && leader_uuid == req.server().permanent_uuid()) {
      RETURN_NOT_OK(StepDownMasterLeaderAndWaitForNewLeader());
      leader = GetLeaderMaster();
      leader_uuid = leader->uuid();
      leader_proxy.reset(new ConsensusServiceProxy(proxy_cache_.get(), leader->bound_rpc_addr()));
    }

    req.set_dest_uuid(leader_uuid);
    RETURN_NOT_OK(leader_proxy->ChangeConfig(req, &resp, &rpc));
    if (resp.has_error()) {
      if (resp.error().code() != TabletServerErrorPB::NOT_THE_LEADER &&
          resp.error().code() != TabletServerErrorPB::LEADER_NOT_READY_CHANGE_CONFIG) {
        return STATUS(RuntimeError, Format("Change Config RPC to leader hit error: $0",
                                               resp.error().ShortDebugString()));
      }
    } else {
      break;
    }

    // Need to retry as we come here with NOT_THE_LEADER.
    if (num_attempts >= kMaxRetryIterations) {
      return STATUS(IllegalState,
                    Format("Failed to complete ChangeConfig request '$0' even after maximum "
                               "number of attempts. Last error '$1'",
                               req.ShortDebugString(), resp.error().ShortDebugString()));
    }

    SleepFor(MonoDelta::FromSeconds(1));

    LOG(INFO) << "Resp error '" << resp.error().ShortDebugString() << "', num=" << num_attempts
              << ", retrying...";

    rpc.Reset();
    num_attempts++;
  }

  LOG(INFO) << "Master " << master->bound_rpc_hostport().ToString() << ", change type "
            << type << " to " << masters_.size() << " masters.";

  if (type == consensus::ADD_SERVER || type == consensus::REMOVE_SERVER) {
    if (type == consensus::ADD_SERVER) {
      RETURN_NOT_OK(AddMaster(master.get()));
    } else if (type == consensus::REMOVE_SERVER) {
      RETURN_NOT_OK(RemoveMaster(master.get()));
    }

    UpdateMasterAddressesOnTserver();
    return Status::OK();
  }

  string err_msg = Format("Should not reach here - change type $0", type);

  LOG(FATAL) << err_msg;

  // Satisfy the compiler with a return from here
  return STATUS(RuntimeError, err_msg);
}

// We look for the exact master match. Since it is possible to stop/restart master on
// a given host/port, we do not want a stale master pointer input to match a newer master.
int ExternalMiniCluster::GetIndexOfMaster(const ExternalMaster* master) const {
  for (size_t i = 0; i < masters_.size(); i++) {
    if (masters_[i].get() == master) {
      return narrow_cast<int>(i);
    }
  }
  return -1;
}

Status ExternalMiniCluster::PingMaster(const ExternalMaster* master) const {
  int index = GetIndexOfMaster(master);
  server::PingRequestPB req;
  server::PingResponsePB resp;
  std::shared_ptr<server::GenericServiceProxy> proxy =
      index == -1 ? master_generic_proxy(master->bound_rpc_addr()) : master_generic_proxy(index);
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout);
  return proxy->Ping(req, &resp, &rpc);
}

Status ExternalMiniCluster::AddTServerToBlacklist(
    ExternalMaster* master,
    ExternalTabletServer* ts) {
  GetMasterClusterConfigRequestPB config_req;
  GetMasterClusterConfigResponsePB config_resp;
  int index = GetIndexOfMaster(master);

  if (index == -1) {
    return STATUS(InvalidArgument, Format(
        "Given master '$0' not in the current list of $1 masters.",
        master->bound_rpc_hostport().ToString(), masters_.size()));
  }

  auto proxy = GetMasterProxy<master::MasterClusterProxy>(index);
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout);
  RETURN_NOT_OK(proxy.GetMasterClusterConfig(config_req, &config_resp, &rpc));
  if (config_resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "GetMasterClusterConfig RPC response hit error: $0",
        config_resp.error().ShortDebugString()));
  }
  // Get current config
  ChangeMasterClusterConfigRequestPB change_req;
  SysClusterConfigEntryPB config = *config_resp.mutable_cluster_config();
  // add tserver to blacklist
  HostPortToPB(ts->bound_rpc_hostport(), config.mutable_server_blacklist()->mutable_hosts()->Add());
  *change_req.mutable_cluster_config() = config;
  ChangeMasterClusterConfigResponsePB change_resp;
  rpc.Reset();
  RETURN_NOT_OK(proxy.ChangeMasterClusterConfig(change_req, &change_resp, &rpc));
  if (change_resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "ChangeMasterClusterConfig RPC response hit error: $0",
        change_resp.error().ShortDebugString()));
  }

  LOG(INFO) << "TServer at " << ts->bound_rpc_hostport().ToString()
  << " was added to the blacklist";

  return Status::OK();
}

Status ExternalMiniCluster::GetMinReplicaCountForPlacementBlock(
    ExternalMaster* master,
    const string& cloud, const string& region, const string& zone,
    int* min_num_replicas) {
  GetMasterClusterConfigRequestPB config_req;
  GetMasterClusterConfigResponsePB config_resp;
  int index = GetIndexOfMaster(master);

  if (index == -1) {
    return STATUS(InvalidArgument, Format(
        "Given master '$0' not in the current list of $1 masters.",
        master->bound_rpc_hostport().ToString(), masters_.size()));
  }

  auto proxy = GetMasterProxy<master::MasterClusterProxy>(index);
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout);
  RETURN_NOT_OK(proxy.GetMasterClusterConfig(config_req, &config_resp, &rpc));
  if (config_resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "GetMasterClusterConfig RPC response hit error: $0",
        config_resp.error().ShortDebugString()));
  }
  const SysClusterConfigEntryPB& config = config_resp.cluster_config();

  if (!config.has_replication_info() || !config.replication_info().has_live_replicas()) {
    return STATUS(InvalidArgument, Format(
        "Given placement block '$0.$1.$2' not in the current list of placement blocks.",
        cloud, region, zone));
  }

  const PlacementInfoPB& pi = config.replication_info().live_replicas();

  int found_index = -1;
  bool found = false;
  for (int i = 0; i < pi.placement_blocks_size(); i++) {
    if (!pi.placement_blocks(i).has_cloud_info()) {
      continue;
    }

    bool is_cloud_same = false, is_region_same = false, is_zone_same = false;

    if (pi.placement_blocks(i).cloud_info().has_placement_cloud() && cloud != "") {
      is_cloud_same = pi.placement_blocks(i).cloud_info().placement_cloud() == cloud;
    } else if (!pi.placement_blocks(i).cloud_info().has_placement_cloud() && cloud == "") {
      is_cloud_same = true;
    }

    if (pi.placement_blocks(i).cloud_info().has_placement_region() && region != "") {
      is_region_same = pi.placement_blocks(i).cloud_info().placement_region() == region;
    } else if (!pi.placement_blocks(i).cloud_info().has_placement_region() && region == "") {
      is_region_same = true;
    }

    if (pi.placement_blocks(i).cloud_info().has_placement_zone() && zone != "") {
      is_zone_same = pi.placement_blocks(i).cloud_info().placement_zone() == zone;
    } else if (!pi.placement_blocks(i).cloud_info().has_placement_zone() && zone == "") {
      is_zone_same = true;
    }

    if (is_cloud_same && is_region_same && is_zone_same) {
      found = true;
      found_index = i;
      break;
    }
  }

  if (!found || !pi.placement_blocks(found_index).has_min_num_replicas()) {
    return STATUS(InvalidArgument, Format(
        "Given placement block '$0.$1.$2' not in the current list of placement blocks.",
        cloud, region, zone));
  }

  *min_num_replicas = pi.placement_blocks(found_index).min_num_replicas();
  return Status::OK();
}

Status ExternalMiniCluster::AddTServerToLeaderBlacklist(
    ExternalMaster* master,
    ExternalTabletServer* ts) {
  GetMasterClusterConfigRequestPB config_req;
  GetMasterClusterConfigResponsePB config_resp;
  int index = GetIndexOfMaster(master);

  if (index == -1) {
    return STATUS(InvalidArgument, Format(
        "Given master '$0' not in the current list of $1 masters.",
        master->bound_rpc_hostport().ToString(), masters_.size()));
  }

  auto proxy = GetMasterProxy<master::MasterClusterProxy>(index);
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout);
  RETURN_NOT_OK(proxy.GetMasterClusterConfig(config_req, &config_resp, &rpc));
  if (config_resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "GetMasterClusterConfig RPC response hit error: $0",
        config_resp.error().ShortDebugString()));
  }
  // Get current config
  ChangeMasterClusterConfigRequestPB change_req;
  SysClusterConfigEntryPB config = *config_resp.mutable_cluster_config();
  // add tserver to blacklist
  HostPortToPB(ts->bound_rpc_hostport(), config.mutable_leader_blacklist()->mutable_hosts()->Add());
  *change_req.mutable_cluster_config() = config;
  ChangeMasterClusterConfigResponsePB change_resp;
  rpc.Reset();
  RETURN_NOT_OK(proxy.ChangeMasterClusterConfig(change_req, &change_resp, &rpc));
  if (change_resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "ChangeMasterClusterConfig RPC response hit error: $0",
        change_resp.error().ShortDebugString()));
  }

  LOG(INFO) << "TServer at " << ts->bound_rpc_hostport().ToString()
  << " was added to the leader blacklist";

  return Status::OK();
}

Status ExternalMiniCluster::ClearBlacklist(
    ExternalMaster* master) {
  GetMasterClusterConfigRequestPB config_req;
  GetMasterClusterConfigResponsePB config_resp;
  int index = GetIndexOfMaster(master);

  if (index == -1) {
    return STATUS(InvalidArgument, Format(
        "Given master '$0' not in the current list of $1 masters.",
        master->bound_rpc_hostport().ToString(), masters_.size()));
  }

  auto proxy = GetMasterProxy<master::MasterClusterProxy>(index);
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout);
  RETURN_NOT_OK(proxy.GetMasterClusterConfig(config_req, &config_resp, &rpc));
  if (config_resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "GetMasterClusterConfig RPC response hit error: $0",
        config_resp.error().ShortDebugString()));
  }
  // Get current config.
  ChangeMasterClusterConfigRequestPB change_req;
  SysClusterConfigEntryPB config = *config_resp.mutable_cluster_config();
  // Clear blacklist.
  config.mutable_server_blacklist()->mutable_hosts()->Clear();
  config.mutable_leader_blacklist()->mutable_hosts()->Clear();
  *change_req.mutable_cluster_config() = config;
  ChangeMasterClusterConfigResponsePB change_resp;
  rpc.Reset();
  RETURN_NOT_OK(proxy.ChangeMasterClusterConfig(change_req, &change_resp, &rpc));
  if (change_resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "ChangeMasterClusterConfig RPC response hit error: $0",
        change_resp.error().ShortDebugString()));
  }

  LOG(INFO) << "Blacklist cleared successfully";

  return Status::OK();
}

Status ExternalMiniCluster::GetNumMastersAsSeenBy(ExternalMaster* master, int* num_peers) {
  ListMastersRequestPB list_req;
  ListMastersResponsePB list_resp;
  int index = GetIndexOfMaster(master);

  if (index == -1) {
    return STATUS(InvalidArgument, Format(
        "Given master '$0' not in the current list of $1 masters.",
        master->bound_rpc_hostport().ToString(), masters_.size()));
  }

  auto proxy = GetMasterProxy<master::MasterClusterProxy>(index);
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout);
  RETURN_NOT_OK(proxy.ListMasters(list_req, &list_resp, &rpc));
  if (list_resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "List Masters RPC response hit error: $0", list_resp.error().ShortDebugString()));
  }

  LOG(INFO) << "List Masters for master at index " << index
            << " got " << list_resp.masters_size() << " peers";

  *num_peers = list_resp.masters_size();

  return Status::OK();
}

Status ExternalMiniCluster::WaitForLeaderCommitTermAdvance() {
  OpIdPB start_opid;
  RETURN_NOT_OK(GetLastOpIdForLeader(&start_opid));
  LOG(INFO) << "Start OPID : " << start_opid.ShortDebugString();

  // Need not do any wait if it is a restart case - so the commit term will be > 0.
  if (start_opid.term() != 0)
    return Status::OK();

  MonoTime now = MonoTime::Now();
  MonoTime deadline = now;
  deadline.AddDelta(opts_.timeout);
  auto opid = start_opid;

  for (int i = 1; now.ComesBefore(deadline); ++i) {
    if (opid.term() > start_opid.term()) {
      LOG(INFO) << "Final OPID: " << opid.ShortDebugString() << " after "
                << i << " iterations.";

      return Status::OK();
    }
    SleepFor(MonoDelta::FromMilliseconds(min(i, 10)));
    RETURN_NOT_OK(GetLastOpIdForLeader(&opid));
    now = MonoTime::Now();
  }

  return STATUS(TimedOut, Format("Term did not advance from $0.", start_opid.term()));
}

Status ExternalMiniCluster::GetLastOpIdForMasterPeers(
    const MonoDelta& timeout,
    consensus::OpIdType opid_type,
    vector<OpIdPB>* op_ids,
    const std::vector<ExternalMaster*>& masters) {
  GetLastOpIdRequestPB opid_req;
  GetLastOpIdResponsePB opid_resp;
  opid_req.set_tablet_id(yb::master::kSysCatalogTabletId);
  RpcController controller;
  controller.set_timeout(timeout);

  op_ids->clear();
  for (auto master : masters) {
    opid_req.set_dest_uuid(master->uuid());
    opid_req.set_opid_type(opid_type);
    RETURN_NOT_OK_PREPEND(
        GetConsensusProxy(master).GetLastOpId(opid_req, &opid_resp, &controller),
        Format("Failed to fetch last op id from $0", master->bound_rpc_hostport().port()));
    op_ids->push_back(opid_resp.opid());
    controller.Reset();
  }

  return Status::OK();
}

Status ExternalMiniCluster::WaitForMastersToCommitUpTo(int64_t target_index) {
  std::vector<ExternalMaster*> masters;
  for (auto master : masters_) {
    masters.push_back(master.get());
  }
  return WaitForMastersToCommitUpTo(target_index, masters);
}

Status ExternalMiniCluster::WaitForMastersToCommitUpTo(
    int64_t target_index, const std::vector<ExternalMaster*>& masters, MonoDelta timeout) {
  if (!timeout.Initialized()) {
    timeout = opts_.timeout;
  }
  auto deadline = CoarseMonoClock::Now() + timeout.ToSteadyDuration();

  for (int i = 1;; i++) {
    vector<OpIdPB> ids;
    Status s = GetLastOpIdForMasterPeers(opts_.timeout, consensus::COMMITTED_OPID, &ids, masters);

    if (s.ok()) {
      bool any_behind = false;
      for (const auto& id : ids) {
        if (id.index() < target_index) {
          any_behind = true;
          break;
        }
      }
      if (!any_behind) {
        LOG(INFO) << "Committed up to " << target_index;
        return Status::OK();
      }
    } else {
      LOG(WARNING) << "Got error getting last opid for each replica: " << s.ToString();
    }

    if (CoarseMonoClock::Now() >= deadline) {
      if (!s.ok()) {
        return s;
      }

      return STATUS_FORMAT(TimedOut,
                           "Index $0 not available on all replicas after $1. ",
                           target_index,
                           opts_.timeout);
    }

    SleepFor(MonoDelta::FromMilliseconds(min(i * 100, 1000)));
  }
}

Status ExternalMiniCluster::WaitForAllIntentsApplied(const MonoDelta& timeout) {
  auto deadline = MonoTime::Now() + timeout;
  for (const auto& ts : tablet_servers_) {
    RETURN_NOT_OK(WaitForAllIntentsApplied(ts.get(), deadline));
  }
  return Status::OK();
}

Status ExternalMiniCluster::WaitForAllIntentsApplied(
    ExternalTabletServer* ts, const MonoDelta& timeout) {
  return WaitForAllIntentsApplied(ts, MonoTime::Now() + timeout);
}


Status ExternalMiniCluster::WaitForAllIntentsApplied(
    ExternalTabletServer* ts, const MonoTime& deadline) {
  auto proxy = GetProxy<tserver::TabletServerAdminServiceProxy>(ts);
  return Wait(
      [proxy, &deadline]() -> Result<bool> {
        tserver::CountIntentsRequestPB req;
        tserver::CountIntentsResponsePB resp;
        rpc::RpcController rpc;
        rpc.set_deadline(deadline);
        RETURN_NOT_OK(proxy.CountIntents(req, &resp, &rpc));
        return resp.num_intents() == 0;
      },
      deadline, Format("Waiting for all intents to be applied at tserver $0", ts->uuid()));
}

Status ExternalMiniCluster::GetIsMasterLeaderServiceReady(ExternalMaster* master) {
  IsMasterLeaderReadyRequestPB req;
  IsMasterLeaderReadyResponsePB resp;
  int index = GetIndexOfMaster(master);

  if (index == -1) {
    return STATUS(InvalidArgument, Format(
        "Given master '$0' not in the current list of $1 masters.",
        master->bound_rpc_hostport().ToString(), masters_.size()));
  }

  auto proxy = GetMasterProxy<master::MasterClusterProxy>(index);
  rpc::RpcController rpc;
  rpc.set_timeout(opts_.timeout);
  RETURN_NOT_OK(proxy.IsMasterLeaderServiceReady(req, &resp, &rpc));
  if (resp.has_error()) {
    return STATUS(RuntimeError, Format(
        "Is master ready RPC response hit error: $0", resp.error().ShortDebugString()));
  }

  return Status::OK();
}

Status ExternalMiniCluster::GetLastOpIdForLeader(OpIdPB* opid) {
  ExternalMaster* leader = GetLeaderMaster();
  auto leader_master_sock = leader->bound_rpc_addr();
  std::shared_ptr<ConsensusServiceProxy> leader_proxy =
    std::make_shared<ConsensusServiceProxy>(proxy_cache_.get(), leader_master_sock);

  RETURN_NOT_OK(itest::GetLastOpIdForMasterReplica(
      leader_proxy,
      yb::master::kSysCatalogTabletId,
      leader->uuid(),
      consensus::COMMITTED_OPID,
      opts_.timeout,
      opid));

  return Status::OK();
}

string ExternalMiniCluster::GetMasterAddresses() const {
  string peer_addrs = "";
  for (size_t i = 0; i < opts_.num_masters; i++) {
    if (!peer_addrs.empty()) {
      peer_addrs += ",";
    }
    peer_addrs += MasterAddressForPort(opts_.master_rpc_ports[i]);
  }
  return peer_addrs;
}

string ExternalMiniCluster::GetTabletServerAddresses() const {
  string peer_addrs = "";
  for (const auto& ts : tablet_servers_) {
    if (!peer_addrs.empty()) {
      peer_addrs += ",";
    }
    peer_addrs += HostPortToString(ts->bind_host(), ts->rpc_port());
  }
  return peer_addrs;
}

string ExternalMiniCluster::GetTabletServerHTTPAddresses() const {
  string peer_addrs = "";
  for (const auto& ts : tablet_servers_) {
    if (!peer_addrs.empty()) {
      peer_addrs += ",";
    }
    peer_addrs += HostPortToString(ts->bind_host(), ts->http_port());
  }
  return peer_addrs;
}

Status ExternalMiniCluster::StartMasters() {
  auto num_masters = opts_.num_masters;

  if (opts_.master_rpc_ports.size() != num_masters) {
    LOG(FATAL) << num_masters << " masters requested, but " <<
        opts_.master_rpc_ports.size() << " ports specified in 'master_rpc_ports'";
  }

  for (auto& port : opts_.master_rpc_ports) {
    if (port == 0) {
      port = AllocateFreePort();
      LOG(INFO) << "Using an auto-assigned port " << port
                << " to start an external mini-cluster master";
    }
  }

  vector<string> peer_addrs;
  for (size_t i = 0; i < num_masters; i++) {
    peer_addrs.push_back(MasterAddressForPort(opts_.master_rpc_ports[i]));
  }
  const auto peer_addrs_str = JoinStrings(peer_addrs, ",");

  // Start the masters.
  for (size_t i = 0; i < num_masters; i++) {
    auto master = VERIFY_RESULT_PREPEND(
        StartMaster(peer_addrs_str, opts_.master_rpc_ports[i]),
        Format("Unable to start Master at index $0", i));

    masters_.push_back(master);
  }

  if (opts_.enable_ysql) {
    RETURN_NOT_OK(WaitForInitDb());
  }
  return Status::OK();
}

Status ExternalMiniCluster::WaitForInitDb() {
  const auto start_time = std::chrono::steady_clock::now();
  const auto kTimeout = NonTsanVsTsan(1200s, 1800s);
  int num_timeouts = 0;
  const int kMaxTimeouts = 10;
  while (true) {
    for (size_t i = 0; i < opts_.num_masters; i++) {
      auto elapsed_time = std::chrono::steady_clock::now() - start_time;
      if (elapsed_time > kTimeout) {
        return STATUS_FORMAT(
            TimedOut,
            "Timed out while waiting for initdb to complete: elapsed time is $0, timeout is $1",
            elapsed_time, kTimeout);
      }
      auto proxy = GetMasterProxy<master::MasterAdminProxy>(i);
      rpc::RpcController rpc;
      rpc.set_timeout(opts_.timeout);
      IsInitDbDoneRequestPB req;
      IsInitDbDoneResponsePB resp;
      Status status = proxy.IsInitDbDone(req, &resp, &rpc);
      if (status.IsTimedOut()) {
        num_timeouts++;
        LOG(WARNING) << status << " (seen " << num_timeouts << " timeouts so far)";
        if (num_timeouts == kMaxTimeouts) {
          LOG(ERROR) << "Reached " << kMaxTimeouts << " timeouts: " << status;
          return status;
        }
        continue;
      }
      LOG_IF(INFO, !status.ok()) << "IsInitDbDone failed: " << status;
      if (!opts_.allow_crashes_during_init_db && !status.ok() && !masters_[i]->IsProcessAlive()) {
        return STATUS_FORMAT(RuntimeError, "Master $0 crashed during initdb", i);
      }
      if (resp.has_error() &&
          resp.error().code() != master::MasterErrorPB::NOT_THE_LEADER) {

        return STATUS(RuntimeError, Format(
            "IsInitDbDone RPC response hit error: $0",
            resp.error().ShortDebugString()));
      }
      if (resp.done()) {
        if (resp.has_initdb_error() && !resp.initdb_error().empty()) {
          LOG(ERROR) << "master reported an initdb error: " << resp.initdb_error();
          return STATUS(RuntimeError, "initdb failed: " + resp.initdb_error());
        }
        LOG(INFO) << "master indicated that initdb is done";
        return Status::OK();
      }
    }
    std::this_thread::sleep_for(500ms);
  }
}

Result<bool> ExternalMiniCluster::is_ts_stale(int ts_idx, MonoDelta deadline) {
  auto proxy = GetLeaderMasterProxy<master::MasterClusterProxy>();
  std::shared_ptr<rpc::RpcController> controller = std::make_shared<rpc::RpcController>();
  master::ListTabletServersRequestPB req;
  master::ListTabletServersResponsePB resp;
  controller->Reset();
  controller->set_timeout(deadline);

  RETURN_NOT_OK(proxy.ListTabletServers(req, &resp, controller.get()));

  bool is_stale = false, is_ts_found = false;
  for (int i = 0; i < resp.servers_size(); i++) {
    if (!resp.servers(i).has_instance_id()) {
      return STATUS_SUBSTITUTE(
        Uninitialized,
        "ListTabletServers RPC returned a TS with uninitialized instance id."
      );
    }

    if (!resp.servers(i).instance_id().has_permanent_uuid()) {
      return STATUS_SUBSTITUTE(
        Uninitialized,
        "ListTabletServers RPC returned a TS with uninitialized UUIDs."
      );
    }

    if (resp.servers(i).instance_id().permanent_uuid() == tablet_server(ts_idx)->uuid()) {
      is_ts_found = true;
      is_stale = !resp.servers(i).alive();
    }
  }

  if (!is_ts_found) {
    return STATUS_SUBSTITUTE(
        NotFound,
        "Given TS not found in ListTabletServers RPC."
    );
  }
  return is_stale;
}

Status ExternalMiniCluster::WaitForMasterToMarkTSAlive(int ts_idx, MonoDelta deadline) {
  RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
    return !VERIFY_RESULT(is_ts_stale(ts_idx));
  }, deadline * kTimeMultiplier, "Is TS Alive", 1s));

  return Status::OK();
}

Status ExternalMiniCluster::WaitForMasterToMarkTSDead(int ts_idx, MonoDelta deadline) {
  RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
    return is_ts_stale(ts_idx);
  }, deadline * kTimeMultiplier, "Is TS dead", 1s));

  return Status::OK();
}

string ExternalMiniCluster::GetBindIpForTabletServer(size_t index) const {
  if (opts_.use_even_ips) {
    return Format("127.0.0.$0", (index + 1) * 2);
  } else if (opts_.bind_to_unique_loopback_addresses) {
#if defined(__APPLE__)
    return Format("127.0.0.$0", index + 1); // Use default 127.0.0.x IPs.
#else
    const pid_t p = getpid();
    return Format("127.$0.$1.$2", (p >> 8) & 0xff, p & 0xff, index);
#endif
  } else {
    return "127.0.0.1";
  }
}

Status ExternalMiniCluster::AddTabletServer(
    bool start_cql_proxy, const std::vector<std::string>& extra_flags, int num_drives) {
  CHECK(GetLeaderMaster() != nullptr)
      << "Must have started at least 1 master before adding tablet servers";

  size_t idx = tablet_servers_.size();

  string exe = GetBinaryPath(GetTServerBinaryName());
  vector<HostPort> master_hostports;
  for (size_t i = 0; i < num_masters(); i++) {
    master_hostports.push_back(DCHECK_NOTNULL(master(i))->bound_rpc_hostport());
  }

  uint16_t ts_rpc_port = 0;
  uint16_t ts_http_port = 0;
  uint16_t redis_rpc_port = 0;
  uint16_t redis_http_port = 0;
  uint16_t cql_rpc_port = 0;
  uint16_t cql_http_port = 0;
  uint16_t pgsql_rpc_port = 0;
  uint16_t ysql_conn_mgr_rpc_port = 0;
  uint16_t pgsql_http_port = 0;

  const bool use_same_port = idx > 0 && opts_.use_same_ts_ports &&
          opts_.bind_to_unique_loopback_addresses;
  if (use_same_port) {
    const scoped_refptr<ExternalTabletServer>& first_ts = tablet_servers_[0];
    ts_rpc_port = first_ts->rpc_port();
    ts_http_port = first_ts->http_port();
    redis_rpc_port = first_ts->redis_rpc_port();
    redis_http_port = first_ts->redis_http_port();
    cql_rpc_port = first_ts->cql_rpc_port();
    cql_http_port = first_ts->cql_http_port();
    pgsql_rpc_port = first_ts->pgsql_rpc_port();
    ysql_conn_mgr_rpc_port = first_ts->ysql_conn_mgr_rpc_port();
    pgsql_http_port = first_ts->pgsql_http_port();
  } else {
    ts_rpc_port = AllocateFreePort();
    ts_http_port = AllocateFreePort();
    redis_rpc_port = AllocateFreePort();
    redis_http_port = AllocateFreePort();
    cql_rpc_port = AllocateFreePort();
    cql_http_port = AllocateFreePort();
    pgsql_rpc_port = AllocateFreePort();
    pgsql_http_port = AllocateFreePort();
  }

  vector<string> flags = opts_.extra_tserver_flags;
  if (opts_.enable_ysql) {
    flags.push_back("--enable_ysql=true");
    if (opts_.enable_ysql_auth) {
      flags.push_back("--ysql_enable_auth=true");
    }
  } else {
    flags.push_back("--enable_ysql=false");
  }
  flags.insert(flags.end(), extra_flags.begin(), extra_flags.end());
  if (IsYsqlConnMgrEnabledInTests()) {
    flags.push_back("--enable_ysql_conn_mgr=true");
    AppendFlagToAllowedPreviewFlagsCsv(flags, "enable_ysql_conn_mgr");
    if (!use_same_port) {
      ysql_conn_mgr_rpc_port = AllocateFreePort();
    }
  }

  if (num_drives < 0) {
    num_drives = opts_.num_drives;
  }

  scoped_refptr<ExternalTabletServer> ts = new ExternalTabletServer(
      idx, messenger_, proxy_cache_.get(), exe, GetDataPath(Format("ts-$0", idx + 1)),
      num_drives, GetBindIpForTabletServer(idx), ts_rpc_port, ts_http_port, redis_rpc_port,
      redis_http_port, cql_rpc_port, cql_http_port, pgsql_rpc_port,
      ysql_conn_mgr_rpc_port, pgsql_http_port,
      master_hostports, SubstituteInFlags(flags, idx));
  RETURN_NOT_OK(ts->Start(start_cql_proxy));
  tablet_servers_.push_back(ts);

  // Add yb controller for the new ts if we already have controllers for existing TSs.
  // This is for the cases where we add new ts in middle of tests.
  if (!yb_controller_servers_.empty()) {
    RETURN_NOT_OK(AddYbControllerServer(ts));
  }

  return Status::OK();
}

Status ExternalMiniCluster::RemoveTabletServer(const std::string& ts_uuid, MonoTime deadline) {
  return RemoveTabletServers({ts_uuid}, deadline);
}

Status ExternalMiniCluster::RemoveTabletServers(
    const std::vector<std::reference_wrapper<const std::string>>& ts_uuids, MonoTime deadline) {
  std::vector<HostPortPB> hps;
  for (const auto& ts_uuid : ts_uuids) {
    auto ts = tablet_server_by_uuid(ts_uuid);
    if (ts == nullptr) {
      return STATUS_FORMAT(InvalidArgument, "Cannot find tserver with uuid $0", ts_uuid);
    }
    hps.push_back(HostPortToPB(ts->bound_rpc_addr()));
    ts->Shutdown();
  }
  auto leader_master = GetLeaderMaster();
  auto cluster_client = master::MasterClusterClient(
      master::MasterClusterProxy(proxy_cache_.get(), leader_master->bound_rpc_addr()));
  for (const auto& hp : hps) {
    RETURN_NOT_OK(cluster_client.BlacklistHost(HostPortPB(hp)));
  }
  auto original_flag_value =
      VERIFY_RESULT(leader_master->GetFlag("tserver_unresponsive_timeout_ms"));
  RETURN_NOT_OK(SetFlag(leader_master, "tserver_unresponsive_timeout_ms", "3000"));
  std::vector<std::reference_wrapper<const std::string>> ts_to_remove;
  for (const auto& ts_uuid : ts_uuids) {
    ts_to_remove.push_back(ts_uuid);
  }
  RETURN_NOT_OK(Wait([&cluster_client, &ts_to_remove]() -> Result<bool> {
        for (auto it = ts_to_remove.begin(); it != ts_to_remove.end();) {
          auto s = cluster_client.RemoveTabletServer(std::string(*it));
          if (!s.ok()) {
            if (s.IsInvalidArgument()) {
              return false;
            }
            return s;
          }
          it = ts_to_remove.erase(it);
        }
        return true;
      }, deadline, Format("Timed out trying to remove tablet servers from the cluster")));

  RETURN_NOT_OK(SetFlag(leader_master, "tserver_unresponsive_timeout_ms", original_flag_value));
  for (const auto& hp : hps) {
    RETURN_NOT_OK(cluster_client.UnBlacklistHost(hp));
  }
  // ExternalTabletServer* tablet_server_by_uuid(const std::string& uuid) const;
  return Status::OK();
}

Status ExternalMiniCluster::StartYbControllerServers() {
  for (auto ts : tablet_servers_) {
    RETURN_NOT_OK(AddYbControllerServer(ts));
  }
  return Status::OK();
}

Status ExternalMiniCluster::AddYbControllerServer(const scoped_refptr<ExternalTabletServer> ts) {
  // Return if we already have a Yb Controller for the given ts.
  for (auto ybController : yb_controller_servers_) {
    if (ybController->GetServerAddress() == ts->bind_host()) {
      return Status::OK();
    }
  }

  size_t idx = yb_controller_servers_.size() + 1;
  vector<string> extra_flags;
  for (const auto& flag : opts_.extra_tserver_flags) {
    if (flag.find("certs_dir") != string::npos) {
      extra_flags.push_back("--certs_dir_name" + flag.substr(flag.find("=")));
    }
  }

  // All yb controller servers need to be on the same port.
  uint16_t server_port;
  if (idx == 1) {
    server_port = AllocateFreePort();
  } else {
    server_port = yb_controller_servers_[0]->GetServerPort();
  }
  const auto yb_controller_log_dir = GetDataPath(Format("ybc-$0/logs", idx));
  const auto yb_controller_tmp_dir = GetDataPath(Format("ybc-$0/tmp", idx));
  RETURN_NOT_OK(Env::Default()->CreateDirs(yb_controller_log_dir));
  RETURN_NOT_OK(Env::Default()->CreateDirs(yb_controller_tmp_dir));
  scoped_refptr<ExternalYbController> yb_controller = new ExternalYbController(
      idx, yb_controller_log_dir, yb_controller_tmp_dir, ts->bind_host(), GetToolPath("yb-admin"),
      GetToolPath("../../../bin", "yb-ctl"), GetToolPath("../../../bin", "ycqlsh"),
      GetPgToolPath("ysql_dump"), GetPgToolPath("ysql_dumpall"), GetPgToolPath("ysqlsh"),
      server_port, masters_[0]->http_port(), ts->http_port(), ts->bind_host(),
      GetYbcToolPath("yb-controller-server"), extra_flags);

  RETURN_NOT_OK_PREPEND(
      yb_controller->Start(), "Failed to start YB Controller at index " + std::to_string(idx));
  yb_controller_servers_.push_back(yb_controller);

  return Status::OK();
}

void ExternalMiniCluster::UpdateMasterAddressesOnTserver() {
  vector<HostPort> master_hostports;
  for (size_t i = 0; i < num_masters(); i++) {
    master_hostports.push_back(DCHECK_NOTNULL(master(i))->bound_rpc_hostport());
  }

  for (size_t i = 0; i < num_tablet_servers(); i++) {
    DCHECK_NOTNULL(tablet_server(i))->UpdateMasterAddress(master_hostports);
  }
}

Status ExternalMiniCluster::WaitForTabletServerCount(size_t count, const MonoDelta& timeout) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);

  std::vector<scoped_refptr<ExternalTabletServer>> last_unmatched = tablet_servers_;
  bool had_leader = false;

  while (true) {
    MonoDelta remaining = deadline - MonoTime::Now();
    if (remaining.ToSeconds() < 0) {
      std::vector<std::string> unmatched_uuids;
      unmatched_uuids.reserve(last_unmatched.size());
      for (const auto& server : last_unmatched) {
        unmatched_uuids.push_back(server->instance_id().permanent_uuid());
      }
      if (!had_leader) {
        return STATUS(TimedOut, "Does not have active master leader to check tserver registration");
      }
      return STATUS_FORMAT(TimedOut, "$0 TS(s) never registered with master (not registered $1)",
                           count, unmatched_uuids);
    }

    // We should give some time for RPC to proceed, otherwise all requests would fail.
    remaining = std::max<MonoDelta>(remaining, 250ms);

    last_unmatched = tablet_servers_;
    had_leader = false;
    for (size_t i = 0; i < masters_.size(); i++) {
      master::ListTabletServersRequestPB req;
      master::ListTabletServersResponsePB resp;
      rpc::RpcController rpc;
      rpc.set_timeout(remaining);
      auto status = GetMasterProxy<master::MasterClusterProxy>(i).ListTabletServers(
          req, &resp, &rpc);
      LOG_IF(WARNING, !status.ok()) << "ListTabletServers failed: " << status;
      if (!status.ok() || resp.has_error()) {
        continue;
      }
      had_leader = true;
      // ListTabletServers() may return servers that are no longer online.
      // Do a second step of verification to verify that the descs that we got
      // are aligned (same uuid/seqno) with the TSs that we have in the cluster.
      size_t match_count = 0;
      for (const master::ListTabletServersResponsePB_Entry& e : resp.servers()) {
        for (auto it = last_unmatched.begin(); it != last_unmatched.end(); ++it) {
          if ((**it).instance_id().permanent_uuid() == e.instance_id().permanent_uuid() &&
              (**it).instance_id().instance_seqno() == e.instance_id().instance_seqno()) {
            match_count++;
            last_unmatched.erase(it);
            break;
          }
        }
      }
      if (match_count >= count) {
        LOG(INFO) << count << " TS(s) registered with Master";
        return Status::OK();
      }
    }
    SleepFor(MonoDelta::FromMilliseconds(100));
  }
}

void ExternalMiniCluster::AssertNoCrashes() {
  vector<ExternalDaemon*> daemons = this->daemons();
  for (ExternalDaemon* d : daemons) {
    if (d->IsShutdown()) continue;
    EXPECT_TRUE(d->IsProcessAlive()) << "At least one process crashed. viz: "
                                     << d->id();
  }
}

Result<std::vector<ListTabletsForTabletServerResponsePB::Entry>> ExternalMiniCluster::GetTablets(
    ExternalTabletServer* ts) {
  TabletServerServiceProxy proxy(proxy_cache_.get(), ts->bound_rpc_addr());
  ListTabletsForTabletServerRequestPB req;
  ListTabletsForTabletServerResponsePB resp;

  rpc::RpcController rpc;
  rpc.set_timeout(10s * kTimeMultiplier);
  RETURN_NOT_OK(proxy.ListTabletsForTabletServer(req, &resp, &rpc));

  std::vector<ListTabletsForTabletServerResponsePB::Entry> result;
  for (const ListTabletsForTabletServerResponsePB::Entry& entry : resp.entries()) {
    result.push_back(entry);
  }

  return result;
}

Result<tserver::GetTabletStatusResponsePB> ExternalMiniCluster::GetTabletStatus(
      const ExternalTabletServer& ts, const yb::TabletId& tablet_id) {
  rpc::RpcController rpc;
  rpc.set_timeout(kDefaultTimeout);

  tserver::GetTabletStatusRequestPB req;
  req.set_tablet_id(tablet_id);

  tserver::GetTabletStatusResponsePB resp;
  RETURN_NOT_OK(GetProxy<TabletServerServiceProxy>(&ts).GetTabletStatus(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status()).CloneAndPrepend(
        Format("Code $0", TabletServerErrorPB::Code_Name(resp.error().code())));
  }
  return resp;
}

Result<tserver::GetSplitKeyResponsePB> ExternalMiniCluster::GetSplitKey(
    const yb::TabletId& tablet_id) {
  size_t attempts = 50;
  while (attempts > 0) {
    --attempts;
    const auto leader_idx = VERIFY_RESULT(GetTabletLeaderIndex(tablet_id));
    auto response = VERIFY_RESULT(GetSplitKey(
        *tablet_server(leader_idx), tablet_id, /* fail_on_response_error = */ false));
    if (!response.has_error()) {
      return response;
    }

    // There's a small chance that a leader is changed after GetTabletLeaderIndex() and before
    // GetSplitKey() is started, in this case we should re-attempt.
    if (response.error().code() != TabletServerErrorPB::NOT_THE_LEADER) {
      return StatusFromPB(response.error().status()).CloneAndPrepend(
          Format("Code $0", TabletServerErrorPB::Code_Name(response.error().code())));
    }

    LOG(WARNING) << Format(
        "Tablet $0: leader was changed, remaining attempts = $1", tablet_id, attempts);
  }

  return STATUS(Expired, Format("Tablet $0: leader was not found for tablet", tablet_id));
}

Result<tserver::GetSplitKeyResponsePB> ExternalMiniCluster::GetSplitKey(
      const ExternalTabletServer& ts, const yb::TabletId& tablet_id, bool fail_on_response_error) {
  rpc::RpcController rpc;
  rpc.set_timeout(kDefaultTimeout);

  tserver::GetSplitKeyRequestPB req;
  req.set_tablet_id(tablet_id);

  tserver::GetSplitKeyResponsePB resp;
  RETURN_NOT_OK(GetProxy<TabletServerServiceProxy>(&ts).GetSplitKey(req, &resp, &rpc));
  if (fail_on_response_error && resp.has_error()) {
    return StatusFromPB(resp.error().status()).CloneAndPrepend(
        Format("Code $0", TabletServerErrorPB::Code_Name(resp.error().code())));
  }
  return resp;
}

Status ExternalMiniCluster::FlushTabletsOnSingleTServer(
    ExternalTabletServer* ts, const std::vector<yb::TabletId> tablet_ids,
    tserver::FlushTabletsRequestPB_Operation operation) {
  tserver::FlushTabletsRequestPB req;
  tserver::FlushTabletsResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(10s * kTimeMultiplier);

  req.set_dest_uuid(ts->uuid());
  req.set_operation(operation);
  for (const auto& tablet_id : tablet_ids) {
    req.add_tablet_ids(tablet_id);
  }
  if (tablet_ids.empty()) {
    req.set_all_tablets(true);
  }

  auto ts_admin_service_proxy = std::make_unique<tserver::TabletServerAdminServiceProxy>(
    proxy_cache_.get(), ts->bound_rpc_addr());
  return ts_admin_service_proxy->FlushTablets(req, &resp, &controller);
}

Result<tserver::ListTabletsResponsePB> ExternalMiniCluster::ListTablets(ExternalTabletServer* ts) {
  rpc::RpcController rpc;
  ListTabletsRequestPB req;
  ListTabletsResponsePB resp;
  rpc.set_timeout(opts_.timeout);
  TabletServerServiceProxy proxy(proxy_cache_.get(), ts->bound_rpc_addr());
  RETURN_NOT_OK(proxy.ListTablets(req, &resp, &rpc));
  return resp;
}

Result<std::vector<std::string>> ExternalMiniCluster::GetTabletIds(ExternalTabletServer* ts) {
  auto tablets = VERIFY_RESULT(GetTablets(ts));
  std::vector<std::string> result;
  for (const auto& tablet : tablets) {
    result.push_back(tablet.tablet_id());
  }
  return result;
}

Result<size_t> ExternalMiniCluster::GetSegmentCounts(ExternalTabletServer* ts) {
  auto tablets = VERIFY_RESULT(GetTablets(ts));
  size_t result = 0;
  for (const auto& tablet : tablets) {
    result += tablet.num_log_segments();
  }
  return result;
}
Status ExternalMiniCluster::WaitForTabletsRunning(ExternalTabletServer* ts,
                                                  const MonoDelta& timeout) {
  TabletServerServiceProxy proxy(proxy_cache_.get(), ts->bound_rpc_addr());
  ListTabletsRequestPB req;
  ListTabletsResponsePB resp;

  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);
  while (MonoTime::Now().ComesBefore(deadline)) {
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(10));
    RETURN_NOT_OK(proxy.ListTablets(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    int num_not_running = 0;
    for (const StatusAndSchemaPB& status : resp.status_and_schema()) {
      if (status.tablet_status().state() != tablet::RUNNING) {
        num_not_running++;
      }
    }

    if (num_not_running == 0) {
      return Status::OK();
    }

    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  return STATUS(TimedOut, resp.DebugString());
}

Status ExternalMiniCluster::WaitForTSToCrash(size_t index, const MonoDelta& timeout) {
  ExternalTabletServer* ts = tablet_server(index);
  return WaitForTSToCrash(ts, timeout);
}

Status ExternalMiniCluster::WaitForTSToCrash(const ExternalTabletServer* ts,
                                             const MonoDelta& timeout) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);
  while (MonoTime::Now().ComesBefore(deadline)) {
    if (!ts->IsProcessAlive()) {
      return Status::OK();
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  return STATUS(TimedOut, Format("TS $0 did not crash!", ts->instance_id().permanent_uuid()));
}

namespace {
void LeaderMasterCallback(HostPort* dst_hostport,
                          Synchronizer* sync,
                          const Status& status,
                          const HostPort& result) {
  if (status.ok()) {
    *dst_hostport = result;
  }
  sync->StatusCB(status);
}
}  // anonymous namespace

Result<size_t> ExternalMiniCluster::GetFirstNonLeaderMasterIndex() {
  return GetPeerMasterIndex(false);
}

Result<size_t> ExternalMiniCluster::GetLeaderMasterIndex() {
  return GetPeerMasterIndex(true);
}

Result<size_t> ExternalMiniCluster::GetPeerMasterIndex(bool is_leader) {
  Synchronizer sync;
  server::MasterAddresses addrs;
  HostPort leader_master_hp;
  auto deadline = CoarseMonoClock::Now() + 5s;

  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    if (master->IsProcessAlive()) {
      addrs.push_back({ master->bound_rpc_addr() });
    }
  }
  if (addrs.empty()) {
    return STATUS(IllegalState, "No running masters");
  }
  rpc::Rpcs rpcs;
  auto rpc = std::make_shared<GetLeaderMasterRpc>(
      Bind(&LeaderMasterCallback, &leader_master_hp, &sync),
      addrs,
      deadline,
      messenger_,
      proxy_cache_.get(),
      &rpcs);
  rpc->SendRpc();
  RETURN_NOT_OK(sync.Wait());
  rpcs.Shutdown();

  const char* peer_type = is_leader ? "leader" : "non-leader";
  for (size_t i = 0; i < masters_.size(); i++) {
    bool matches_leader = masters_[i]->bound_rpc_hostport().port() == leader_master_hp.port();
    if (is_leader == matches_leader) {
      LOG(INFO) << "Found peer " << peer_type << " at index " << i << ".";
      return i;
    }
  }

  // There is never a situation where this should happen, so it's
  // better to exit with a FATAL log message right away vs. return a
  // Status::IllegalState().
  auto status = STATUS_FORMAT(NotFound, "Peer $0 master is not in masters_ list", peer_type);
  LOG(FATAL) << status;
  return status;
}

ExternalMaster* ExternalMiniCluster::GetLeaderMaster() {
  int num_attempts = 0;
  // Retry to get the leader master's index - due to timing issues (like election in progress).
  for (;;) {
    ++num_attempts;
    auto idx = GetLeaderMasterIndex();
    if (idx.ok()) {
      return master(*idx);
    }
    LOG(INFO) << "GetLeaderMasterIndex@" << num_attempts << " hit error: " << idx.status();
    if (num_attempts >= kMaxRetryIterations) {
      LOG(WARNING) << "Failed to get leader master after " << num_attempts << " attempts, "
                   << "returning the first master.";
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(num_attempts * 10));
  }

  return master(0);
}

Result<size_t> ExternalMiniCluster::GetTabletLeaderIndex(
    const yb::TabletId& tablet_id, bool require_lease) {
  for (size_t i = 0; i < num_tablet_servers(); ++i) {
    auto tserver = tablet_server(i);
    if (tserver->IsProcessAlive() && !tserver->IsProcessPaused()) {
      auto tablets = VERIFY_RESULT(GetTablets(tserver));
      for (const auto& tablet : tablets) {
        if (tablet.tablet_id() == tablet_id &&
            tablet.is_leader() &&
            (!require_lease || tablet.has_leader_lease())) {
          return i;
        }
      }
    }
  }
  return STATUS(
      NotFound, Format("Could not find leader of tablet $0 among live tservers.", tablet_id));
}

ExternalTabletServer* ExternalMiniCluster::tablet_server_by_uuid(const std::string& uuid) const {
  for (const scoped_refptr<ExternalTabletServer>& ts : tablet_servers_) {
    if (ts->instance_id().permanent_uuid() == uuid) {
      return ts.get();
    }
  }
  return nullptr;
}

int ExternalMiniCluster::tablet_server_index_by_uuid(const std::string& uuid) const {
  for (size_t i = 0; i < tablet_servers_.size(); i++) {
    if (tablet_servers_[i]->uuid() == uuid) {
      return narrow_cast<int>(i);
    }
  }
  return -1;
}

vector<ExternalMaster*> ExternalMiniCluster::master_daemons() const {
  vector<ExternalMaster*> results;
  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    results.push_back(master.get());
  }
  return results;
}

vector<ExternalDaemon*> ExternalMiniCluster::daemons() const {
  vector<ExternalDaemon*> results;
  for (const scoped_refptr<ExternalTabletServer>& ts : tablet_servers_) {
    results.push_back(ts.get());
  }
  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    results.push_back(master.get());
  }
  return results;
}

std::vector<ExternalTabletServer*> ExternalMiniCluster::tserver_daemons() const {
  std::vector<ExternalTabletServer*> result;
  result.reserve(tablet_servers_.size());
  for (const auto& ts : tablet_servers_) {
    result.push_back(ts.get());
  }
  return result;
}

// Returns either connection manager or postgres hostport.
HostPort ExternalMiniCluster::ysql_hostport(int node_index) const {
  return HostPort(tablet_servers_[node_index]->bind_host(),
                    IsYsqlConnMgrEnabledInTests() ?
                      tablet_servers_[node_index]->ysql_conn_mgr_rpc_port():
                      tablet_servers_[node_index]->pgsql_rpc_port());
}

rpc::Messenger* ExternalMiniCluster::messenger() {
  return messenger_;
}

std::shared_ptr<server::GenericServiceProxy> ExternalMiniCluster::master_generic_proxy(
    int idx) const {
  CHECK_GE(idx, 0);
  CHECK_LT(idx, masters_.size());
  return std::make_shared<server::GenericServiceProxy>(
    proxy_cache_.get(), CHECK_NOTNULL(master(idx))->bound_rpc_addr());
}

std::shared_ptr<server::GenericServiceProxy> ExternalMiniCluster::master_generic_proxy(
    const HostPort& bound_rpc_addr) const {
  return std::make_shared<server::GenericServiceProxy>(proxy_cache_.get(), bound_rpc_addr);
}

void ExternalMiniCluster::ConfigureClientBuilder(client::YBClientBuilder* builder) {
  CHECK_NOTNULL(builder);
  CHECK(!masters_.empty());
  builder->clear_master_server_addrs();
  for (const scoped_refptr<ExternalMaster>& master : masters_) {
    builder->add_master_server_addr(master->bound_rpc_hostport().ToString());
  }
}

Result<HostPort> ExternalMiniCluster::DoGetLeaderMasterBoundRpcAddr() {
  return GetLeaderMaster()->bound_rpc_addr();
}

Status ExternalMiniCluster::SetFlag(ExternalDaemon* daemon,
                                    const string& flag,
                                    const string& value) {
  server::GenericServiceProxy proxy(proxy_cache_.get(), daemon->bound_rpc_addr());

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(30));
  server::SetFlagRequestPB req;
  server::SetFlagResponsePB resp;
  req.set_flag(flag);
  req.set_value(value);
  req.set_force(true);
  RETURN_NOT_OK_PREPEND(proxy.SetFlag(req, &resp, &controller),
                        "rpc failed");
  if (resp.result() != server::SetFlagResponsePB::SUCCESS) {
    return STATUS(RemoteError, "failed to set flag",
                               resp.ShortDebugString());
  }
  return Status::OK();
}

Status ExternalMiniCluster::SetFlagOnMasters(const string& flag, const string& value) {
  for (const auto& master : masters_) {
    RETURN_NOT_OK(SetFlag(master.get(), flag, value));
  }
  return Status::OK();
}

Status ExternalMiniCluster::SetFlagOnTServers(const string& flag, const string& value) {
  for (const auto& tablet_server : tablet_servers_) {
    RETURN_NOT_OK(SetFlag(tablet_server.get(), flag, value));
  }
  return Status::OK();
}

void ExternalMiniCluster::AddExtraFlagOnTServers(
    const std::string& flag, const std::string& value) {
  for (const auto& tablet_server : tablet_servers_) {
    tablet_server->AddExtraFlag(flag, value);
  }
}
void ExternalMiniCluster::RemoveExtraFlagOnTServers(const std::string& flag) {
  for (const auto& tablet_server : tablet_servers_) {
    tablet_server->RemoveExtraFlag(flag);
  }
}

Status ExternalMiniCluster::AddAndSetExtraFlag(const std::string& flag, const std::string& value) {
  for (const auto& daemon : daemons()) {
    daemon->AddExtraFlag(flag, value);
    RETURN_NOT_OK_PREPEND(
        SetFlag(daemon, flag, value), Format("Failed to set flag on $0", daemon->id()));
  }

  return Status::OK();
}

uint16_t ExternalMiniCluster::AllocateFreePort() {
  // This will take a file lock ensuring the port does not get claimed by another thread/process
  // and add it to our vector of such locks that will be freed on minicluster shutdown.
  free_port_file_locks_.emplace_back();
  return GetFreePort(&free_port_file_locks_.back());
}

Status ExternalMiniCluster::StartElection(ExternalMaster* master) {
  auto master_sock = master->bound_rpc_addr();
  auto master_proxy = std::make_shared<ConsensusServiceProxy>(proxy_cache_.get(), master_sock);

  RunLeaderElectionRequestPB req;
  req.set_dest_uuid(master->uuid());
  req.set_tablet_id(yb::master::kSysCatalogTabletId);
  RunLeaderElectionResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(opts_.timeout);
  RETURN_NOT_OK(master_proxy->RunLeaderElection(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status())
               .CloneAndPrepend(Format("Code $0",
                                           TabletServerErrorPB::Code_Name(resp.error().code())));
  }
  return Status::OK();
}

ExternalMaster* ExternalMiniCluster::master() const {
  if (masters_.empty())
    return nullptr;

  CHECK_EQ(masters_.size(), 1)
      << "master() should not be used with multiple masters, use GetLeaderMaster() instead.";
  return master(0);
}

// Return master at 'idx' or NULL if the master at 'idx' has not been started.
ExternalMaster* ExternalMiniCluster::master(size_t idx) const {
  CHECK_LT(idx, masters_.size());
  return masters_[idx].get();
}

ExternalTabletServer* ExternalMiniCluster::tablet_server(size_t idx) const {
  CHECK_LT(idx, tablet_servers_.size());
  return tablet_servers_[idx].get();
}

Status ExternalMiniCluster::WaitForLoadBalancerToBecomeIdle(
    const std::unique_ptr<yb::client::YBClient>& client, MonoDelta timeout) {
  // Wait for LB to become idle.
  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        return client->IsLoadBalancerIdle();
      },
      timeout,
      "IsLoadBalancerIdle"));
  return Status::OK();
}

Result<pgwrapper::PGConn> ExternalMiniCluster::ConnectToDB(
    const std::string& db_name, std::optional<size_t> node_index, bool simple_query_protocol) {
  if (!node_index) {
    node_index = RandomUniformInt<size_t>(0, num_tablet_servers() - 1);
  }
  LOG(INFO) << "Connecting to PG database " << db_name << " on tserver " << *node_index;

  auto* ts = tablet_server(*node_index);

  auto settings = pgwrapper::PGConnSettings{
      .host = ts->bind_host(), .port = ts->ysql_port(), .dbname = db_name};

  if (opts_.enable_ysql_auth) {
    settings.user = "yugabyte";
    settings.password = "yugabyte";
  }
  return pgwrapper::PGConnBuilder(settings).Connect(simple_query_protocol);
}

namespace {
Result<itest::TabletServerMap> CreateTabletServerMap(ExternalMiniCluster& cluster) {
  auto master = cluster.GetLeaderMaster();
  SCHECK_NOTNULL(master);
  return itest::CreateTabletServerMap(
      cluster.GetProxy<master::MasterClusterProxy>(master), &cluster.proxy_cache());
}
}  // namespace

Status ExternalMiniCluster::MoveTabletLeader(
    const TabletId& tablet_id, std::optional<size_t> new_leader_idx, MonoDelta timeout) {
  if (timeout == MonoDelta::kMin) {
    timeout = MonoDelta::FromSeconds(10 * kTimeMultiplier);
  }

  const auto ts_map = VERIFY_RESULT(CreateTabletServerMap(*this));

  itest::TServerDetails* leader_ts;
  RETURN_NOT_OK(itest::FindTabletLeader(ts_map, tablet_id, timeout, &leader_ts));

  itest::TServerDetails* new_leader_ts = nullptr;
  if (new_leader_idx) {
    new_leader_ts = ts_map.at(tablet_server(*new_leader_idx)->uuid()).get();
  } else {
    for (const auto& [ts_id, ts_details] : ts_map) {
      if (ts_id != leader_ts->uuid()) {
        new_leader_ts = ts_details.get();
      }
    }
  }
  SCHECK_NOTNULL(new_leader_ts);

  // Step down the leader onto the second follower.
  RETURN_NOT_OK(
      (itest::WaitForAllPeersToCatchup(tablet_id, TServerDetailsVector(ts_map), timeout)));
  RETURN_NOT_OK(
      itest::LeaderStepDown(leader_ts, tablet_id, new_leader_ts, timeout, false, nullptr));

  return itest::WaitUntilLeader(new_leader_ts, tablet_id, timeout);
}

void ExternalMiniCluster::SetMaxGracefulShutdownWaitSec(int max_graceful_shutdown_wait_sec) {
  for (auto& master : masters_) {
    master->SetMaxGracefulShutdownWaitSec(max_graceful_shutdown_wait_sec);
  }
  for (auto& tserver : tablet_servers_) {
    tserver->SetMaxGracefulShutdownWaitSec(max_graceful_shutdown_wait_sec);
  }
}

LogWaiter::LogWaiter(ExternalDaemon* daemon, const std::string& string_to_wait) :
    daemon_(daemon), string_to_wait_(string_to_wait) {
  daemon_->SetLogListener(this);
}

Status ExternalMiniCluster::CallYbAdmin(
    const std::vector<std::string>& args, MonoDelta timeout, std::string* output) {
  auto command = ToStringVector(
      GetToolPath("yb-admin"), "-master_addresses", GetMasterAddresses(), "-timeout_ms",
      timeout.ToMilliseconds());
  command.insert(command.end(), args.begin(), args.end());

  LOG(INFO) << "Running " << ToString(command);
  std::string output_internal, error;
  if (!output) {
    output = &output_internal;
  }
  auto status = Subprocess::Call(command, output, &error);
  LOG(INFO) << "yb-admin Output: " << *output;
  if (!error.empty()) {
    LOG(INFO) << "yb-admin Error: " << error;
  }
  return status;
}

void LogWaiter::Handle(const GStringPiece& s) {
  if (s.contains(string_to_wait_)) {
    event_occurred_ = true;
  }
}

Status LogWaiter::WaitFor(const MonoDelta timeout) {
  constexpr auto kInitialWaitPeriod = 100ms;
  return ::yb::WaitFor(
      [this]{ return event_occurred_.load(); }, timeout,
      Format("Waiting for log record '$0' on $1...", string_to_wait_, daemon_->id()),
      kInitialWaitPeriod);
}

LogWaiter::~LogWaiter() {
  daemon_->RemoveLogListener(this);
}

//------------------------------------------------------------
// ScopedResumeExternalDaemon
//------------------------------------------------------------

ScopedResumeExternalDaemon::ScopedResumeExternalDaemon(ExternalDaemon* daemon)
    : daemon_(CHECK_NOTNULL(daemon)) {
}

ScopedResumeExternalDaemon::~ScopedResumeExternalDaemon() {
  CHECK_OK(daemon_->Resume());
}

//------------------------------------------------------------
// ExternalMaster
//------------------------------------------------------------
ExternalMaster::ExternalMaster(
    size_t master_index,
    rpc::Messenger* messenger,
    rpc::ProxyCache* proxy_cache,
    const string& exe,
    const string& data_dir,
    const std::vector<string>& extra_flags,
    const string& rpc_bind_address,
    uint16_t http_port,
    const string& master_addrs)
    : ExternalDaemon(Format("m-$0", master_index + 1), messenger,
                     proxy_cache, exe, data_dir,
                     {GetServerTypeDataPath(data_dir, "master")}, extra_flags),
      rpc_bind_address_(rpc_bind_address),
      master_addrs_(master_addrs),
      http_port_(http_port) {
}

ExternalMaster::~ExternalMaster() {
}

namespace {

class Flags {
 public:
  template <class Value>
  void Add(const std::string& name, const Value& value) {
    value_.push_back(Format("--$0=$1", name, value));
  }

  void AddHostPort(const std::string& name, const std::string& host, uint16_t port) {
    Add(name, HostPort(host, port));
  }

  const std::vector<std::string>& value() const {
    return value_;
  }

 private:
  std::vector<std::string> value_;
};

} // namespace

Status ExternalMaster::Start(bool shell_mode) {
  Flags flags;
  flags.Add("fs_data_dirs", root_dir_);
  flags.Add("rpc_bind_addresses", rpc_bind_address_);
  flags.Add("webserver_interface", "localhost");
  flags.Add("webserver_port", http_port_);
  // Default master args to make sure we don't wait to trigger new LB tasks upon master leader
  // failover.
  flags.Add("load_balancer_initial_delay_secs", 0);
  // On first start, we need to tell the masters their list of expected peers.
  // For 'shell' master, there is no master addresses.
  if (!shell_mode) {
    flags.Add("master_addresses", master_addrs_);
  }
  RETURN_NOT_OK(StartProcess(flags.value()));
  return Status::OK();
}

Status ExternalMaster::Restart() {
  LOG_WITH_PREFIX(INFO) << "Restart()";
  if (!IsProcessAlive()) {
    // Make sure this function could be safely called if the process has already crashed.
    Shutdown();
  }
  // We store the addresses on shutdown so make sure we did that first.
  if (bound_rpc_.port() == 0) {
    return STATUS(IllegalState, "Master cannot be restarted. Must call Shutdown() first.");
  }
  return Start(true);
}

//------------------------------------------------------------
// ExternalTabletServer
//------------------------------------------------------------

ExternalTabletServer::ExternalTabletServer(
    size_t tablet_server_index, rpc::Messenger* messenger, rpc::ProxyCache* proxy_cache,
    const std::string& exe, const std::string& data_dir, uint16_t num_drives,
    std::string bind_host, uint16_t rpc_port, uint16_t http_port, uint16_t redis_rpc_port,
    uint16_t redis_http_port, uint16_t cql_rpc_port, uint16_t cql_http_port,
    uint16_t pgsql_rpc_port, uint16_t ysql_conn_mgr_rpc_port, uint16_t pgsql_http_port,
    const std::vector<HostPort>& master_addrs, const std::vector<std::string>& extra_flags)
    : ExternalDaemon(Format("ts-$0", tablet_server_index + 1),
                     messenger, proxy_cache, exe, data_dir,
                     FsDataDirs(data_dir, "tserver", num_drives), extra_flags),
      master_addrs_(HostPort::ToCommaSeparatedString(master_addrs)),
      bind_host_(std::move(bind_host)),
      rpc_port_(rpc_port),
      http_port_(http_port),
      redis_rpc_port_(redis_rpc_port),
      redis_http_port_(redis_http_port),
      pgsql_rpc_port_(pgsql_rpc_port),
      ysql_conn_mgr_rpc_port_(ysql_conn_mgr_rpc_port),
      pgsql_http_port_(pgsql_http_port),
      cql_rpc_port_(cql_rpc_port),
      cql_http_port_(cql_http_port),
      num_drives_(num_drives) {}

ExternalTabletServer::~ExternalTabletServer() {
}

Status ExternalTabletServer::Start(
    bool start_cql_proxy, bool set_proxy_addrs,
    std::vector<std::pair<string, string>> extra_flags) {
  auto dirs = FsRootDirs(root_dir_, num_drives_);
  for (const auto& dir : dirs) {
    RETURN_NOT_OK(Env::Default()->CreateDirs(dir));
  }
  start_cql_proxy_ = start_cql_proxy;
  Flags flags;
  flags.Add("fs_data_dirs", JoinStrings(dirs, ","));
  flags.AddHostPort("rpc_bind_addresses", bind_host_, rpc_port_);
  flags.Add("webserver_interface", bind_host_);
  flags.Add("webserver_port", http_port_);
  flags.Add("redis_proxy_webserver_port", redis_http_port_);
  flags.Add("pgsql_proxy_webserver_port", pgsql_http_port_);
  flags.Add("cql_proxy_webserver_port", cql_http_port_);
  flags.Add("ysql_conn_mgr_port",  ysql_conn_mgr_rpc_port_);

  if (set_proxy_addrs) {
    flags.AddHostPort("redis_proxy_bind_address", bind_host_, redis_rpc_port_);
    flags.AddHostPort("pgsql_proxy_bind_address", bind_host_, pgsql_rpc_port_);
    flags.AddHostPort("cql_proxy_bind_address", bind_host_, cql_rpc_port_);
  }

  flags.Add("start_cql_proxy", start_cql_proxy_);
  flags.Add("tserver_master_addrs", master_addrs_);

  for (const auto& flag_value : extra_flags) {
    flags.Add(flag_value.first, flag_value.second);
  }

  RETURN_NOT_OK(StartProcess(flags.value()));

  return Status::OK();
}

Status ExternalTabletServer::BuildServerStateFromInfoPath() {
  RETURN_NOT_OK(ExternalDaemon::BuildServerStateFromInfoPath());
  if (start_cql_proxy_) {
    RETURN_NOT_OK(ExternalDaemon::BuildServerStateFromInfoPath(GetCQLServerInfoPath(),
                                                               &cqlserver_status_));
  }
  return Status::OK();
}

string ExternalTabletServer::GetCQLServerInfoPath() {
  return ExternalDaemon::GetServerInfoPath() + "-cql";
}

bool ExternalTabletServer::ServerInfoPathsExist() {
  if (start_cql_proxy_) {
    return ExternalDaemon::ServerInfoPathsExist() &&
        Env::Default()->FileExists(GetCQLServerInfoPath());
  }
  return ExternalDaemon::ServerInfoPathsExist();
}

Status ExternalTabletServer::DeleteServerInfoPaths() {
  // We want to try a deletion for both files.
  Status s1 = ExternalDaemon::DeleteServerInfoPaths();
  Status s2 = Env::Default()->DeleteFile(GetCQLServerInfoPath());
  RETURN_NOT_OK(s1);
  RETURN_NOT_OK(s2);
  return Status::OK();
}

void ExternalTabletServer::UpdateMasterAddress(const std::vector<HostPort>& master_addrs) {
  master_addrs_ = HostPort::ToCommaSeparatedString(master_addrs);
}

Status ExternalTabletServer::Restart(
    bool start_cql_proxy, std::vector<std::pair<string, string>> flags) {
  LOG_WITH_PREFIX(INFO) << "Restart: start_cql_proxy=" << start_cql_proxy;
  if (!IsProcessAlive()) {
    // Make sure this function could be safely called if the process has already crashed.
    Shutdown();
  }
  // We store the addresses on shutdown so make sure we did that first.
  if (bound_rpc_.port() == 0) {
    return STATUS(IllegalState, "Tablet server cannot be restarted. Must call Shutdown() first.");
  }
  return Start(start_cql_proxy, true /* set_proxy_addrs */, flags);
}

Result<int64_t> ExternalTabletServer::GetInt64CQLMetric(const MetricEntityPrototype* entity_proto,
                                                        const char* entity_id,
                                                        const MetricPrototype* metric_proto,
                                                        const char* value_field) const {
  return GetMetricFromHost<int64>(
      HostPort(bind_host(), cql_http_port()),
      entity_proto, entity_id, metric_proto, value_field);
}

Status ExternalTabletServer::SetNumDrives(uint16_t num_drives) {
  if (IsProcessAlive()) {
    return STATUS(IllegalState, "Cann't set num drives on running Tablet server. "
                                "Must call Shutdown() first.");
  }
  num_drives_ = num_drives;
  data_dirs_ = FsDataDirs(root_dir_, "tserver", num_drives_);
  return Status::OK();
}

Result<pid_t> ExternalTabletServer::PostmasterPid() {
  const std::string pg_pid_file = JoinPathSegments(GetRootDir(), "pg_data", "postmaster.pid");
  if (!Env::Default()->FileExists(pg_pid_file)) {
    return STATUS(NotFound, Format("Postmaster PID file not found in path: $0", pg_pid_file));
  }

  // Error handling inspired by ReadMasterAddressesFromFlagFile() in client/client-internal.cc
  std::ifstream pg_pid_in(pg_pid_file);
  if (!pg_pid_in) {
    return STATUS_FORMAT(IOError, "Unable to open pid file '$0': $1",
        pg_pid_file, strerror(errno));
  }

  string line;
  if (pg_pid_in.good() && std::getline(pg_pid_in, line)) {
     boost::trim(line);
     return static_cast<pid_t>(std::stoi(line));
  }

  // Return blanket error for all other error categories
  return STATUS_FORMAT(IOError, "Failed reading pid file '$0': $1",
      pg_pid_file, strerror(errno));
}

Result<int> ExternalTabletServer::SignalPostmaster(int signal) {
  pid_t postmaster_pid = VERIFY_RESULT(PostmasterPid());
  if (!postmaster_pid) {
    return STATUS(NotFound, "Postmaster not found");
  }

  LOG(INFO) << Format("Sending postmaster process (PID: $0) signal: $1", postmaster_pid, signal);
  return kill(postmaster_pid, signal);
}

Status RestartAllMasters(ExternalMiniCluster* cluster) {
  for (size_t i = 0; i != cluster->num_masters(); ++i) {
    cluster->master(i)->Shutdown();
  }
  for (size_t i = 0; i != cluster->num_masters(); ++i) {
    RETURN_NOT_OK(cluster->master(i)->Restart());
  }

  return Status::OK();
}

Status CompactTablets(ExternalMiniCluster* cluster, const MonoDelta& timeout) {
  RETURN_NOT_OK(CompactSysCatalog(cluster, timeout));
  for (auto* daemon : cluster->tserver_daemons()) {
    tserver::FlushTabletsRequestPB req;
    tserver::FlushTabletsResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s * kTimeMultiplier);

    req.set_dest_uuid(daemon->uuid());
    req.set_operation(tserver::FlushTabletsRequestPB::COMPACT);
    req.set_all_tablets(true);

    auto proxy = cluster->GetProxy<tserver::TabletServerAdminServiceProxy>(daemon);
    RETURN_NOT_OK(proxy.FlushTablets(req, &resp, &controller));
  }

  return Status::OK();
}

Status FlushAndCompactSysCatalog(ExternalMiniCluster* cluster, const MonoDelta& timeout) {
  for (auto* daemon : cluster->master_daemons()) {
    master::FlushSysCatalogRequestPB req;
    master::FlushSysCatalogResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(timeout);

    auto proxy = cluster->GetProxy<master::MasterAdminProxy>(daemon);
    RETURN_NOT_OK(proxy.FlushSysCatalog(req, &resp, &controller));
  }

  return CompactSysCatalog(cluster, timeout);
}

Status CompactSysCatalog(ExternalMiniCluster* cluster, const MonoDelta& timeout) {
  for (auto* daemon : cluster->master_daemons()) {
    master::CompactSysCatalogRequestPB req;
    master::CompactSysCatalogResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(timeout);

    auto proxy = cluster->GetProxy<master::MasterAdminProxy>(daemon);
    RETURN_NOT_OK(proxy.CompactSysCatalog(req, &resp, &controller));
  }
  return Status::OK();
}

std::string FlagToString(bool flag) {
  return flag ? "true" : "false";
}

const std::string& FlagToString(const std::string& flag) {
  return flag;
}

void StartSecure(
    std::unique_ptr<ExternalMiniCluster>* cluster,
    std::unique_ptr<rpc::SecureContext>* secure_context,
    std::unique_ptr<rpc::Messenger>* messenger) {
  rpc::MessengerBuilder messenger_builder("test_client");
  *secure_context = ASSERT_RESULT(rpc::SetupSecureContext(
      /*root_dir=*/"", "127.0.0.100", rpc::SecureContextType::kInternal, &messenger_builder));
  *messenger = ASSERT_RESULT(messenger_builder.Build());
  (**messenger).TEST_SetOutboundIpBase(ASSERT_RESULT(HostToAddress("127.0.0.1")));

  ExternalMiniClusterOptions opts;
  opts.extra_tserver_flags = {
      YB_FORWARD_FLAG(allow_insecure_connections),
      YB_FORWARD_FLAG(certs_dir),
      YB_FORWARD_FLAG(node_to_node_encryption_use_client_certificates),
      YB_FORWARD_FLAG(use_client_to_server_encryption),
      YB_FORWARD_FLAG(use_node_to_node_encryption),
  };
  opts.extra_master_flags = opts.extra_tserver_flags;
  opts.num_tablet_servers = 3;
  opts.use_even_ips = true;
  *cluster = std::make_unique<ExternalMiniCluster>(opts);
  ASSERT_OK((**cluster).Start(messenger->get()));
}

Status WaitForTableIntentsApplied(
    ExternalMiniCluster *cluster, const TableId&, MonoDelta timeout) {
  // TODO(jhe) - Check for just table_id, currently checking for all intents.
  return CHECK_NOTNULL(cluster)->WaitForAllIntentsApplied(timeout);
}

}  // namespace yb
