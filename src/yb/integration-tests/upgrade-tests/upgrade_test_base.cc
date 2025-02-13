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

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include <boost/algorithm/string/trim.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <gtest/gtest.h>

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug.h"
#include "yb/util/env_util.h"
#include "yb/util/scope_exit.h"
#include "yb/common/version_info.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"

#include "yb/master/master_admin.pb.h"
#include "yb/master/master_admin.proxy.h"

DECLARE_uint32(auto_flags_apply_delay_ms);

using namespace std::literals;

namespace yb {

namespace {

const MonoDelta kRpcTimeout = 20s * kTimeMultiplier;

// Returns the URL for the current build type and os platform. Returns empty string if a valid URL
// does not exist.
std::string GetRelevantUrl(const BuildInfo& info) {
#if defined(__APPLE__) && defined(__aarch64__)
  return kIsDebug ? info.darwin_debug_arm64_url : info.darwin_release_arm64_url;
#elif defined(__linux__) && defined(__x86_64__)
  return kIsDebug ? info.linux_debug_x86_url : info.linux_release_x86_url;
#elif defined(__linux__) && defined(__aarch64__)
  return kIsDebug ? "" : info.linux_release_aarch64_url;
#endif

  return "";
}

Status RunCommand(const std::vector<std::string>& args) {
  LOG(INFO) << "Execute: " << AsString(args);
  return Subprocess::Call(args);
}

// Get the value of the key from the xml node as a string, and trims the value.
template <typename T>
std::string GetXmlPathAsString(const T& node, const std::string& key) {
  auto value = node.template get<std::string>(key);
  boost::trim(value);
  return value;
}

// Gets the build info for the given version from the builds.xml file.
Result<BuildInfo> GetBuildInfoForVersion(const std::string& version) {
  const auto sub_dir = "upgrade_test_builds";
  const auto build_file_xml =
      JoinPathSegments(env_util::GetRootDir(sub_dir), sub_dir, "builds.xml");

  LOG(INFO) << "Reading build info from " << build_file_xml;

  try {
    boost::property_tree::ptree pt;
    boost::property_tree::xml_parser::read_xml(build_file_xml, pt);
    for (const auto& [_, node] : pt.get_child("builds")) {
      if (GetXmlPathAsString(node, "<xmlattr>.version") == version) {
        BuildInfo build_info;
        build_info.version = version;
        build_info.build_number = GetXmlPathAsString(node, "build_number");
        build_info.linux_debug_x86_url = GetXmlPathAsString(node, "linux_debug_x86");
        build_info.linux_release_x86_url = GetXmlPathAsString(node, "linux_release_x86");
        build_info.linux_release_aarch64_url = GetXmlPathAsString(node, "linux_release_aarch64");
        build_info.darwin_debug_arm64_url = GetXmlPathAsString(node, "darwin_debug_arm64");
        build_info.darwin_release_arm64_url = GetXmlPathAsString(node, "darwin_release_arm64");
        return build_info;
      }
    }
  } catch (const std::exception& e) {
    return STATUS_FORMAT(NotFound, "Failed to parse build file $0: $1", build_file_xml, e.what());
  }

  return STATUS_FORMAT(
      NotFound, "Build info for version $0 not found in $1", version, build_file_xml);
}

// Download and extract the old version if it does not already exist, and return the old version bin
// path. A ready.txt file is placed in the bin directory to indicate that the old version is ready
// for use.
Result<std::string> DownloadAndGetBinPath(const BuildInfo& build_info) {
  std::string arch = "linux";
  std::string tar_bin = "tar";
#ifdef __APPLE__
  arch = "darwin";
  tar_bin = "gtar";
#endif
  arch += kIsDebug ? "_debug" : "_release";

  auto env = Env::Default();
  const std::string build_root =
      JoinPathSegments(DirName(env_util::GetRootDir("bin")), "db-upgrade");
  RETURN_NOT_OK(env_util::CreateDirIfMissing(env, build_root));
  const auto version_root_path = JoinPathSegments(
      build_root, Format("yugabyte_$0-$1_$2", build_info.version, build_info.build_number, arch));
  RETURN_NOT_OK(env_util::CreateDirIfMissing(env, version_root_path));

  // Get a lock on a file since multiple tests can be running in parallel and downloading the same
  // build to the same location.
  const auto lock_file = JoinPathSegments(version_root_path, "lock.lck");
  FileLock* f_lock = nullptr;
  MonoTime start = MonoTime::Now();
  do {
    auto s = env->LockFile(lock_file, &f_lock, /*recursive_lock_ok=*/false);
    if (s.ok()) {
      break;
    }

    SCHECK_LT(
        MonoTime::Now() - start, 5min, IllegalState,
        Format("Failed to acquire lock on ready file $0", lock_file));
    SleepFor(100ms);
  } while (true);
  auto se = ScopeExit([f_lock, &env] { CHECK_OK(env->UnlockFile(f_lock)); });

  const auto ready_file = JoinPathSegments(version_root_path, "ready.txt");
  const auto extract_path =
      JoinPathSegments(version_root_path, Format("yugabyte-$0", build_info.version));
  const auto bin_path = JoinPathSegments(extract_path, "bin");
  if (env->FileExists(ready_file)) {
    LOG(INFO) << bin_path << " already downloaded and ready for use";
    return bin_path;
  }

  const auto download_url = GetRelevantUrl(build_info);
  const auto tar_file_name = BaseName(download_url);

  const std::string kDownloadDir = "/opt/yb-build/db-upgrade";
  const auto tar_file_path = JoinPathSegments(kDownloadDir, tar_file_name);

  if (!env->FileExists(tar_file_path)) {
    RETURN_NOT_OK(env_util::CreateDirIfMissing(env, kDownloadDir));
    LOG(INFO) << "Downloading " << download_url << " to " << tar_file_path;
    RETURN_NOT_OK(RunCommand(
        {"curl", "--retry", "3", "--retry-delay", "3", download_url, "-o", tar_file_path}));
  }

  LOG(INFO) << "Extracting " << tar_file_path << " to " << version_root_path;
  if (env->DirExists(extract_path)) {
    RETURN_NOT_OK(env->DeleteRecursively(extract_path));
  }
  RETURN_NOT_OK(env->CreateDir(extract_path));
  RETURN_NOT_OK(
      RunCommand({tar_bin, "xzf", tar_file_path, "--skip-old-files", "-C", version_root_path}));

#if defined(__linux__)
  RETURN_NOT_OK(RunCommand({"bash", JoinPathSegments(bin_path, "post_install.sh")}));
#endif

  RETURN_NOT_OK(WriteStringToFileSync(env, MonoTime::Now().ToFormattedString(), ready_file));

  return bin_path;
}

template <typename T>
Status RestartDaemonInVersion(T& daemon, const std::string& bin_path) {
  daemon.Shutdown();
  daemon.SetExe(bin_path);
  return daemon.Restart();
}

// Add the flag_name to undefok list, so that it can be set on all versions even if the version does
// not contain the flag. If the flag_list already contains an undefok flag, append to it, else
// insert a new entry.
void AddUnDefOkAndSetFlag(
    std::vector<std::string>& flag_list, const std::string& flag_name,
    const std::string& flag_value) {
  AppendCsvFlagValue(flag_list, "undefok", flag_name);
  flag_list.emplace_back(Format("--$0=$1", flag_name, flag_value));
}

void WaitForAutoFlagApply() { SleepFor(FLAGS_auto_flags_apply_delay_ms * 1ms + 3s); }

// This is a pg15 version which supports upgrade only from certain versions.
// Check if the given version is supported for upgrade.
bool IsUpgradeSupported(const std::string& from_version) {
  auto parts = StringSplit(from_version, '.');
  CHECK_GE(parts.size(), 2);
  int major = std::stoi(parts[0]);
  auto minor = std::stoi(parts[1]);
  CHECK_GT(major, 0);
  CHECK_GT(minor, 0);

  // Stable releases in the older 2 dot numbering scheme are not supported.
  // Only preview release after 2.25 are supported.
  if (major == 2) {
    return minor >= 25;
  }

  // Only 2024.2.0.0 and later are supported.
  return major > 2024 || (major == 2024 && minor >= 2);
}

}  // namespace

const MonoDelta UpgradeTestBase::kNoDelayBetweenNodes = 0s;

UpgradeTestBase::UpgradeTestBase(const std::string& from_version)
    : old_version_info_(CHECK_RESULT(GetBuildInfoForVersion(from_version))) {
  LOG(INFO) << "Old version: " << old_version_info_.version << ": "
            << GetRelevantUrl(old_version_info_);
}

void UpgradeTestBase::SetUp() {
  if (IsSanitizer()) {
    GTEST_SKIP() << "Upgrade testing not supported with sanitizers";
  }

// Disable mac tests in the lab since the lab runs multiple tests in parallel on the mac causing
// these to timeout.
#ifdef __APPLE__
  if (getenv("YB_SPARK_COPY_MODE")) {
    GTEST_SKIP() << "Upgrade testing not supported on mac spark machines";
  }
#endif

  if (GetRelevantUrl(old_version_info_).empty()) {
    GTEST_SKIP() << "Upgrade testing not supported from version " << old_version_info_.version
                 << " for this OS architecture and build type";
  }

  if (GetRelevantUrl(old_version_info_).empty()) {
    GTEST_SKIP() << "Upgrade testing not supported from version " << old_version_info_.version
                 << " for this OS architecture and build type";
  }

  if (!IsUpgradeSupported(old_version_info_.version)) {
    GTEST_SKIP() << "PG15 upgrade not supported from version " << old_version_info_.version;
  }

  ExternalMiniClusterITestBase::SetUp();

  VersionInfo::GetVersionInfoPB(&current_version_info_);
  LOG(INFO) << "Current version: " << current_version_info_.DebugString();
}

Status UpgradeTestBase::StartClusterInOldVersion() {
  ExternalMiniClusterOptions default_opts;
  default_opts.num_masters = 3;
  default_opts.num_tablet_servers = 3;

  return StartClusterInOldVersion(default_opts);
}

void UpgradeTestBase::SetUpOptions(ExternalMiniClusterOptions& opts) {
  opts.enable_ysql = true;
  opts.daemon_bin_path = ASSERT_RESULT(DownloadAndGetBinPath(old_version_info_));

  // There should be at least one tserver running on the same address as master.
  // This will force all masters to run on 127.0.0.2 and tservers to run on 127.0.0.2, 127.0.0.4
  // and 127.0.0.6.
  opts.use_even_ips = true;

  // Allow local socket connections for ysqlsh. This was added in newer versions as part of
  // D39566.
  std::string hba_conf_value = "local all yugabyte trust";
  if (!opts.enable_ysql_auth) {
    // Include the default allow all setting.
    hba_conf_value += ",host all all all trust";
  }
  AppendCsvFlagValue(opts.extra_master_flags, "ysql_hba_conf_csv", hba_conf_value);
  AppendCsvFlagValue(opts.extra_tserver_flags, "ysql_hba_conf_csv", hba_conf_value);

  // Disable TEST_always_return_consensus_info_for_succeeded_rpc since it is not upgrade safe.
  AddUnDefOkAndSetFlag(
      opts.extra_master_flags, "TEST_always_return_consensus_info_for_succeeded_rpc", "false");
  AddUnDefOkAndSetFlag(
      opts.extra_tserver_flags, "TEST_always_return_consensus_info_for_succeeded_rpc", "false");

  ExternalMiniClusterITestBase::SetUpOptions(opts);
}

Status UpgradeTestBase::StartClusterInOldVersion(const ExternalMiniClusterOptions& options) {
  LOG(INFO) << "Starting cluster in version: " << old_version_info_.version;

  RETURN_NOT_OK(ExternalMiniClusterITestBase::StartCluster(options));

  old_version_bin_path_ = cluster_->GetDaemonBinPath();
  old_version_master_bin_path_ = cluster_->GetMasterBinaryPath();
  old_version_tserver_bin_path_ = cluster_->GetTServerBinaryPath();

  RETURN_NOT_OK(cluster_->DeduceBinRoot(&current_version_bin_path_));
  cluster_->SetDaemonBinPath(current_version_bin_path_);
  current_version_master_bin_path_ = cluster_->GetMasterBinaryPath();
  current_version_tserver_bin_path_ = cluster_->GetTServerBinaryPath();
  cluster_->SetDaemonBinPath(old_version_bin_path_);

  if (cluster_->opts_.enable_ysql) {
    server::GetStatusRequestPB req;
    server::GetStatusResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    RETURN_NOT_OK(
        cluster_->GetLeaderMasterProxy<server::GenericServiceProxy>().GetStatus(req, &resp, &rpc));
    LOG(INFO) << "From version: " << resp.status().version_info().DebugString();

    is_ysql_major_version_upgrade_ = resp.status().version_info().ysql_major_version() !=
                                     current_version_info_.ysql_major_version();
  }

  if (IsYsqlMajorVersionUpgrade()) {
    RETURN_NOT_OK(
        cluster_->AddAndSetExtraFlag("ysql_yb_major_version_upgrade_compatibility", "11"));
  }

  return Status::OK();
}

Status UpgradeTestBase::UpgradeClusterToCurrentVersion(
    MonoDelta delay_between_nodes, bool auto_finalize) {
  LOG(INFO) << "Upgrading cluster to current version";

  RETURN_NOT_OK_PREPEND(
      RestartAllMastersInCurrentVersion(delay_between_nodes), "Failed to restart masters");

  RETURN_NOT_OK_PREPEND(
      PerformYsqlMajorCatalogUpgrade(), "Failed to run ysql major catalog upgrade");

  RETURN_NOT_OK_PREPEND(
      RestartAllTServersInCurrentVersion(delay_between_nodes), "Failed to restart tservers");

  RETURN_NOT_OK_PREPEND(
      PromoteAutoFlags(AutoFlagClass::kLocalVolatile), "Failed to promote volatile AutoFlags");

  if (!auto_finalize) {
    return Status::OK();
  }

  RETURN_NOT_OK_PREPEND(FinalizeUpgrade(), "Failed to finalize upgrade");

  LOG(INFO) << "Cluster upgraded to current version";
  return Status::OK();
}

Status UpgradeTestBase::RestartAllMastersInCurrentVersion(MonoDelta delay_between_nodes) {
  LOG(INFO) << "Restarting all yb-masters in current version";

  for (auto* master : cluster_->master_daemons()) {
    RETURN_NOT_OK(RestartMasterInCurrentVersion(*master, /*wait_for_cluster_to_stabilize=*/false));
    SleepFor(delay_between_nodes);
  }

  RETURN_NOT_OK(WaitForClusterToStabilize());

  return Status::OK();
}

Status UpgradeTestBase::RestartMasterInCurrentVersion(
    ExternalMaster& master, bool wait_for_cluster_to_stabilize) {
  LOG(INFO) << "Restarting yb-master " << master.id() << " in current version";

  if (is_ysql_major_version_upgrade_) {
    // Multiple tests can run on the same box, so use a unique ports.
    master.AddExtraFlag("ysql_upgrade_postgres_port", yb::ToString(cluster_->AllocateFreePort()));
  }

  RETURN_NOT_OK(RestartDaemonInVersion(master, current_version_master_bin_path_));

  if (wait_for_cluster_to_stabilize) {
    RETURN_NOT_OK(WaitForClusterToStabilize());
  }

  return Status::OK();
}

Status UpgradeTestBase::RestartAllTServersInCurrentVersion(MonoDelta delay_between_nodes) {
  LOG(INFO) << "Restarting all yb-tservers in current version";

  for (auto* tserver : cluster_->tserver_daemons()) {
    RETURN_NOT_OK(
        RestartTServerInCurrentVersion(*tserver, /*wait_for_cluster_to_stabilize=*/false));
    SleepFor(delay_between_nodes);
  }

  RETURN_NOT_OK(WaitForClusterToStabilize());

  return Status::OK();
}

Status UpgradeTestBase::RestartTServerInCurrentVersion(
    ExternalTabletServer& ts, bool wait_for_cluster_to_stabilize) {
  LOG(INFO) << "Restarting yb-tserver " << ts.id() << " in current version";
  RETURN_NOT_OK(RestartDaemonInVersion(ts, current_version_tserver_bin_path_));

  if (wait_for_cluster_to_stabilize) {
    RETURN_NOT_OK(WaitForClusterToStabilize());
  }

  return Status::OK();
}

Status UpgradeTestBase::PerformYsqlMajorCatalogUpgrade() {
  if (!is_ysql_major_version_upgrade_) {
    return Status::OK();
  }

  RETURN_NOT_OK(StartYsqlMajorCatalogUpgrade());

  return WaitForYsqlMajorCatalogUpgradeToFinish();
}

Status UpgradeTestBase::StartYsqlMajorCatalogUpgrade() {
  LOG_WITH_FUNC(INFO) << "Starting ysql major upgrade";

  LOG(INFO) << "Running ysql major catalog version upgrade";

  master::StartYsqlMajorCatalogUpgradeRequestPB req;
  master::StartYsqlMajorCatalogUpgradeResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(kRpcTimeout);
  auto master_admin_proxy = cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>();
  RETURN_NOT_OK(master_admin_proxy.StartYsqlMajorCatalogUpgrade(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status UpgradeTestBase::WaitForYsqlMajorCatalogUpgradeToFinish() {
  auto master_admin_proxy = cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>();

  auto is_upgrade_done = [&master_admin_proxy]() -> Result<bool> {
    master::IsYsqlMajorCatalogUpgradeDoneRequestPB req;
    master::IsYsqlMajorCatalogUpgradeDoneResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    RETURN_NOT_OK(master_admin_proxy.IsYsqlMajorCatalogUpgradeDone(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp.done();
  };

  return LoggedWaitFor(
      is_upgrade_done, 10min, "Waiting for ysql major catalog upgrade to complete",
      /*initial_delay*/ 1s);
}

Status UpgradeTestBase::PromoteAutoFlags(AutoFlagClass flag_class) {
  LOG(INFO) << "Promoting AutoFlags " << flag_class;

  master::PromoteAutoFlagsRequestPB req;
  master::PromoteAutoFlagsResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(kRpcTimeout);
  req.set_max_flag_class(ToString(flag_class));
  req.set_promote_non_runtime_flags(false);
  req.set_force(false);
  RETURN_NOT_OK(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>().PromoteAutoFlags(
      req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  WaitForAutoFlagApply();

  LOG(INFO) << "Promoted AutoFlags: " << resp.DebugString();

  if (flag_class == AutoFlagClass::kLocalVolatile) {
    // Store the version info in case we want to rollback.
    SCHECK(!auto_flags_rollback_version_, IllegalState, "Already promoted local volatile");
    if (resp.flags_promoted()) {
      auto_flags_rollback_version_ = resp.new_config_version() - 1;
    }
  } else {
    // Can no longer rollback volatile flags.
    auto_flags_rollback_version_.reset();
  }

  return Status::OK();
}

Status UpgradeTestBase::FinalizeYsqlMajorCatalogUpgrade() {
  if (!is_ysql_major_version_upgrade_) {
    return Status::OK();
  }

  LOG(INFO) << "Finalizing ysql major catalog upgrade";

  master::FinalizeYsqlMajorCatalogUpgradeRequestPB req;
  master::FinalizeYsqlMajorCatalogUpgradeResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(kRpcTimeout);
  RETURN_NOT_OK(
      cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>().FinalizeYsqlMajorCatalogUpgrade(
          req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (IsYsqlMajorVersionUpgrade()) {
    RETURN_NOT_OK(cluster_->AddAndSetExtraFlag("ysql_yb_major_version_upgrade_compatibility", "0"));
  }

  return Status::OK();
}

Status UpgradeTestBase::PerformYsqlUpgrade() {
  if (!cluster_->opts_.enable_ysql) {
    return Status::OK();
  }

  LOG(INFO) << "Running ysql upgrade";

  tserver::UpgradeYsqlRequestPB req;
  tserver::UpgradeYsqlResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(2min * kTimeMultiplier);

  RETURN_NOT_OK(cluster_->GetTServerProxy<tserver::TabletServerAdminServiceProxy>(0).UpgradeYsql(
      req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status UpgradeTestBase::FinalizeUpgrade() {
  LOG(INFO) << "Finalizing upgrade";

  RETURN_NOT_OK_PREPEND(
      FinalizeYsqlMajorCatalogUpgrade(), "Failed to run ysql major catalog upgrade");

  RETURN_NOT_OK_PREPEND(PromoteAutoFlags(), "Failed to promote AutoFlags");

  RETURN_NOT_OK_PREPEND(PerformYsqlUpgrade(), "Failed to perform ysql upgrade");

  // Set the current version bin path for the cluster, so that any newly added nodes get started on
  // the new version.
  cluster_->SetDaemonBinPath(current_version_bin_path_);

  return Status::OK();
}

Status UpgradeTestBase::RollbackYsqlMajorCatalogVersion() {
  if (!is_ysql_major_version_upgrade_) {
    return Status::OK();
  }

  LOG(INFO) << "Running ysql major catalog rollback";

  master::RollbackYsqlMajorCatalogVersionRequestPB req;
  master::RollbackYsqlMajorCatalogVersionResponsePB resp;
  rpc::RpcController rpc;
  // Rollback RPC is synchronous and can take a while.
  rpc.set_timeout(3min);
  RETURN_NOT_OK(
      cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>().RollbackYsqlMajorCatalogVersion(
          req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status UpgradeTestBase::RollbackVolatileAutoFlags() {
  if (!auto_flags_rollback_version_) {
    return Status::OK();
  }

  LOG(INFO) << "Rolling back AutoFlags to version " << *auto_flags_rollback_version_;

  master::RollbackAutoFlagsRequestPB req;
  master::RollbackAutoFlagsResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(kRpcTimeout);
  req.set_rollback_version(*auto_flags_rollback_version_);
  RETURN_NOT_OK(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>().RollbackAutoFlags(
      req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  auto_flags_rollback_version_.reset();

  WaitForAutoFlagApply();

  LOG(INFO) << "Rolled back AutoFlags: " << resp.DebugString();

  return Status::OK();
}

Status UpgradeTestBase::RollbackClusterToOldVersion(MonoDelta delay_between_nodes) {
  LOG(INFO) << "Rolling back upgrade";

  RETURN_NOT_OK_PREPEND(RollbackVolatileAutoFlags(), "Failed to rollback Volatile AutoFlags");

  RETURN_NOT_OK_PREPEND(
      RestartAllTServersInOldVersion(delay_between_nodes), "Failed to restart tservers");

  RETURN_NOT_OK_PREPEND(
      RollbackYsqlMajorCatalogVersion(), "Failed to run ysql major catalog rollback");

  RETURN_NOT_OK_PREPEND(
      RestartAllMastersInOldVersion(delay_between_nodes), "Failed to restart masters");

  LOG(INFO) << "Cluster rolled back to old version";
  return Status::OK();
}

Status UpgradeTestBase::RestartAllMastersInOldVersion(MonoDelta delay_between_nodes) {
  LOG(INFO) << "Restarting all yb-masters in old version";

  for (auto* master : cluster_->master_daemons()) {
    RETURN_NOT_OK(RestartMasterInOldVersion(*master, /*wait_for_cluster_to_stabilize=*/false));
    SleepFor(delay_between_nodes);
  }

  RETURN_NOT_OK(WaitForClusterToStabilize());

  return Status::OK();
}

Status UpgradeTestBase::RestartMasterInOldVersion(
    ExternalMaster& master, bool wait_for_cluster_to_stabilize) {
  LOG(INFO) << "Restarting yb-master " << master.id() << " in old version";

  if (is_ysql_major_version_upgrade_) {
    // Multiple tests can run on the same box, so use a unique ports.
    master.RemoveExtraFlag("ysql_upgrade_postgres_port");
  }

  RETURN_NOT_OK(RestartDaemonInVersion(master, old_version_master_bin_path_));

  if (wait_for_cluster_to_stabilize) {
    RETURN_NOT_OK(WaitForClusterToStabilize());
  }

  return Status::OK();
}

Status UpgradeTestBase::RestartAllTServersInOldVersion(MonoDelta delay_between_nodes) {
  LOG(INFO) << "Restarting all yb-tservers in old version";

  for (auto* tserver : cluster_->tserver_daemons()) {
    RETURN_NOT_OK(RestartTServerInOldVersion(*tserver, /*wait_for_cluster_to_stabilize=*/false));
    SleepFor(delay_between_nodes);
  }

  RETURN_NOT_OK(WaitForClusterToStabilize());

  return Status::OK();
}

Status UpgradeTestBase::RestartTServerInOldVersion(
    ExternalTabletServer& ts, bool wait_for_cluster_to_stabilize) {
  LOG(INFO) << "Restarting yb-tserver " << ts.id() << " in old version";

  RETURN_NOT_OK(RestartDaemonInVersion(ts, old_version_tserver_bin_path_));

  if (wait_for_cluster_to_stabilize) {
    RETURN_NOT_OK(WaitForClusterToStabilize());
  }

  return Status::OK();
}

Status UpgradeTestBase::WaitForClusterToStabilize() {
  RETURN_NOT_OK(cluster_->WaitForTabletServerCount(cluster_->num_tablet_servers(), 5min));

  return Status::OK();
}

}  // namespace yb
