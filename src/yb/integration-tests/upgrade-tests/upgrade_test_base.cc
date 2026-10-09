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

#include <dirent.h>
#include <limits.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include <boost/algorithm/string/trim.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/regex.hpp>

#include <gtest/gtest.h>

#include "yb/gutil/walltime.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug.h"
#include "yb/util/env_util.h"
#include "yb/util/scope_exit.h"
#include "yb/common/version_info.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"

#include "yb/master/master_admin.pb.h"
#include "yb/master/master_admin.proxy.h"

using namespace std::literals;

namespace yb {

namespace {

const MonoDelta kRpcTimeout = 20s * kTimeMultiplier;

// Shorter than the 10s default, but still longer than the default heartbeat interval, so every
// daemon usually gets a new config before it is applied.
const uint32_t kAutoFlagsApplyDelayMs = 2000;

// Returns the URL for the current os platform. Returns empty string if a valid URL does not exist.
// The old version is always a release build, whatever the build type of the current version: debug
// builds are on-disk compatible with release builds, and customers only run release builds.
std::string GetRelevantUrl(const BuildInfo& info) {
#if defined(__APPLE__) && defined(__aarch64__)
  return info.darwin_release_arm64_url;
#elif defined(__linux__) && defined(__x86_64__)
  return info.linux_release_x86_url;
#elif defined(__linux__) && defined(__aarch64__)
  return info.linux_release_aarch64_url;
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
        build_info.linux_release_x86_url = GetXmlPathAsString(node, "linux_release_x86");
        build_info.linux_release_aarch64_url = GetXmlPathAsString(node, "linux_release_aarch64");
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
// path. A ready.txt file is placed in the version directory to indicate that the old version is
// ready for use.
// The extracted version lives next to the downloaded tarball rather than in the build tree, so it
// is reused across builds. Jenkins workers unpack a fresh build tree for every build, so a cache in
// the build tree made almost every upgrade test extract the tarball again.
Result<std::string> DownloadAndGetBinPath(const BuildInfo& build_info) {
  std::string arch = "linux";
  std::string tar_bin = "tar";
#ifdef __APPLE__
  arch = "darwin";
  tar_bin = "gtar";
#endif
  arch += "_release";

  auto env = Env::Default();
  const std::string kDownloadDir = "/opt/yb-build/db-upgrade";
  RETURN_NOT_OK(env_util::CreateDirIfMissing(env, kDownloadDir));
  const auto version_root_path = JoinPathSegments(
      kDownloadDir, Format("yugabyte_$0-$1_$2", build_info.version, build_info.build_number, arch));
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

  const auto tar_file_path = JoinPathSegments(kDownloadDir, tar_file_name);

  if (!env->FileExists(tar_file_path)) {
    LOG(INFO) << "Downloading " << download_url << " to " << tar_file_path;
    RETURN_NOT_OK(RunCommand(
        {"curl", "--retry", "3", "--retry-delay", "3", download_url, "-o", tar_file_path}));
  }

  LOG(INFO) << "Extracting " << tar_file_path << " to " << version_root_path;
  if (env->DirExists(extract_path)) {
    RETURN_NOT_OK(env->DeleteRecursively(extract_path));
  }
  RETURN_NOT_OK(env->CreateDir(extract_path));
  RETURN_NOT_OK(RunCommand(
      {tar_bin, "xzf", tar_file_path, "--skip-old-files", "--exclude=._*", "-C",
       version_root_path}));

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

Status ValidateYsqlMigrationCompatibility(const std::string& old_version_base_path) {
  const auto ysq_migration_sub_dir = JoinPathSegments("share", "ysql_migrations");

  const auto get_ysql_migrations =
      [&ysq_migration_sub_dir](const std::string& base_path) -> Result<std::set<int64_t>> {
    std::vector<std::string> migration_files;
    std::set<int64_t> migration_ghs;
    auto migration_dir = JoinPathSegments(base_path, ysq_migration_sub_dir);
    auto env = Env::Default();
    RETURN_NOT_OK(env->GetChildren(migration_dir, &migration_files));

    for (const auto& file : migration_files) {
      if (!file.ends_with(".sql")) {
        continue;
      }
      // Ex from V59.7__26540__yb_int_pg_stats_v11.sql extract 26540.
      static const boost::regex migration_regex(R"(^V[\d.]+__(\d+)__.*\.sql$)");
      boost::smatch match;
      if (!boost::regex_match(file, match, migration_regex) || match.size() != 2) {
        return STATUS_FORMAT(IllegalState, "Invalid migration file name: $0", file);
      }

      const auto gh_number = VERIFY_RESULT(CheckedStoll(match[1].str()));
      SCHECK(
          migration_ghs.emplace(gh_number).second, IllegalState,
          "Duplicate migration script for GH $0 found: $1", gh_number, file);
    }
    return migration_ghs;
  };

  auto old_version_migrations = VERIFY_RESULT(get_ysql_migrations(old_version_base_path));
  auto current_version_migrations = VERIFY_RESULT(
      get_ysql_migrations(VERIFY_RESULT(env_util::GetRootDirResult(ysq_migration_sub_dir))));

  for (const auto migration : old_version_migrations) {
    SCHECK(
        current_version_migrations.contains(migration), NotFound,
        "Old version migration $0 not found in current version migrations", migration);
  }
  return Status::OK();
}

// DIAG (do not merge) helpers.
std::pair<uint64_t, uint64_t> DiagMachineCpu() {
  std::ifstream f("/proc/stat");
  std::string cpu;
  f >> cpu;
  uint64_t busy = 0, total = 0;
  for (int i = 0; i < 10; ++i) {
    uint64_t v = 0;
    if (!(f >> v)) {
      break;
    }
    total += v;
    if (i != 3 && i != 4) {
      busy += v;
    }
  }
  return {busy, total};
}

// utime + stime + cutime + cstime of `root` and all its live descendants, in seconds.
double DiagTreeCpuSeconds(pid_t root) {
  std::map<pid_t, std::vector<pid_t>> children;
  std::map<pid_t, uint64_t> ticks;
  DIR* proc = opendir("/proc");
  if (!proc) {
    return -1;
  }
  while (auto* entry = readdir(proc)) {
    char* end = nullptr;
    const auto pid = static_cast<pid_t>(strtol(entry->d_name, &end, 10));
    if (*end != '\0' || pid <= 0) {
      continue;
    }
    std::ifstream f(Format("/proc/$0/stat", pid));
    std::string stat((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto close_paren = stat.rfind(')');
    if (close_paren == std::string::npos) {
      continue;
    }
    std::istringstream fields(stat.substr(close_paren + 2));
    std::vector<std::string> v;
    std::string field;
    while (fields >> field && v.size() < 16) {
      v.push_back(field);
    }
    if (v.size() < 16) {
      continue;
    }
    // Fields after the command: state(0) ppid(1) ... utime(11) stime(12) cutime(13) cstime(14).
    children[static_cast<pid_t>(std::stol(v[1]))].push_back(pid);
    ticks[pid] = std::stoull(v[11]) + std::stoull(v[12]) + std::stoull(v[13]) + std::stoull(v[14]);
  }
  closedir(proc);
  uint64_t sum = 0;
  std::vector<pid_t> stack = {root};
  while (!stack.empty()) {
    const auto pid = stack.back();
    stack.pop_back();
    sum += ticks[pid];
    for (auto child : children[pid]) {
      stack.push_back(child);
    }
  }
  return static_cast<double>(sum) / static_cast<double>(sysconf(_SC_CLK_TCK));
}

std::string DiagReadFirstLine(const std::string& path) {
  std::ifstream f(path);
  std::string line;
  std::getline(f, line);
  return line;
}

}  // namespace

UpgradeTestBase::DiagScope::DiagScope(UpgradeTestBase*, const char* name)
    : name_(name), start_(MonoTime::Now()),
      tree_cpu_s_(DiagTreeCpuSeconds(getpid())) {
  std::tie(machine_busy_, machine_total_) = DiagMachineCpu();
  LOG(INFO) << "DIAG begin " << name_;
}

UpgradeTestBase::DiagScope::~DiagScope() {
  const auto [busy, total] = DiagMachineCpu();
  const auto busy_pct = total > machine_total_
      ? 100.0 * static_cast<double>(busy - machine_busy_) /
            static_cast<double>(total - machine_total_)
      : 0.0;
  LOG(INFO) << "DIAG end " << name_
            << " wall_s=" << (MonoTime::Now() - start_).ToSeconds()
            << " test_tree_cpu_s=" << DiagTreeCpuSeconds(getpid()) - tree_cpu_s_
            << " machine_busy_pct=" << busy_pct
            << " nproc=" << sysconf(_SC_NPROCESSORS_ONLN)
            << " loadavg=" << DiagReadFirstLine("/proc/loadavg")
            << " cgroup_cpu_max=" << DiagReadFirstLine("/sys/fs/cgroup/cpu.max")
            << " cgroup_v1_quota=" << DiagReadFirstLine("/sys/fs/cgroup/cpu/cpu.cfs_quota_us");
}

// The harness deletes the logs of passing (and skipped) tests, but uploads the JUnit XML. Collect
// the interesting lines of this test's log (stdout is redirected to it) for the skip message.
static std::string DiagCollectLogLines() {
  std::cout.flush();
  fflush(stdout);
  fflush(stderr);
  char path[PATH_MAX];
  const auto len = readlink("/proc/self/fd/1", path, sizeof(path) - 1);
  if (len <= 0) {
    return "DIAG: cannot resolve stdout";
  }
  path[len] = '\0';
  std::ifstream f(path);
  if (!f) {
    return Format("DIAG: cannot read stdout $0", path);
  }
  static const std::vector<std::string> kPatterns = {
    "DIAG end", "initdb took", "Launching pg_upgrade", "pg_upgrade completed",
    "Deleting previous ysql major catalog", "Transitioned major upgrade state",
    "[pg_upgrade] Performing", "[pg_upgrade] Creating dump", "[pg_upgrade] Restoring",
    "[pg_upgrade] Executing", "Clusters are compatible", "already downloaded", "Extracting ",
    "Running ysql major catalog version upgrade", "Promoted AutoFlags",
    "master indicated that initdb is done", "Restarting yb-",
  };
  std::string result;
  std::string line;
  while (std::getline(f, line)) {
    for (const auto& pattern : kPatterns) {
      if (line.find(pattern) != std::string::npos) {
        result += line.substr(0, 400);
        result += "\n";
        break;
      }
    }
  }
  return result;
}

void UpgradeTestBase::TearDown() {
  {
    DiagScope diag(this, "TearDown");
    ExternalMiniClusterITestBase::TearDown();
  }
  if (!HasFailure()) {
    GTEST_SKIP() << "DIAG: reported as skipped so that CI uploads the logs\n"
                 << DiagCollectLogLines();
  }
}

namespace {

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

// Add the flag_name to undefok list, so that it can be set on all versions even if the version does
// not contain the flag. If the flag_list already contains an undefok flag, append to it, else
// insert a new entry.
void UpgradeTestBase::AddUnDefOkAndSetFlag(
    std::vector<std::string>& flag_list, const std::string& flag_name,
    const std::string& flag_value) {
  AppendCsvFlagValue(flag_list, "undefok", flag_name);
  flag_list.emplace_back(Format("--$0=$1", flag_name, flag_value));
}

void UpgradeTestBase::SetUpOptions(ExternalMiniClusterOptions& opts) {
  opts.enable_ysql = true;
  opts.daemon_bin_path = ASSERT_RESULT(DownloadAndGetBinPath(old_version_info_));

  // There should be at least one tserver running on the same address as master.
  // This will force all masters to run on 127.0.0.2 and tservers to run on 127.0.0.2, 127.0.0.4
  // and 127.0.0.6.
  opts.use_even_ips = true;

  // Disable TEST_always_return_consensus_info_for_succeeded_rpc since it is not upgrade safe.
  AddUnDefOkAndSetFlag(
      opts.extra_master_flags, "TEST_always_return_consensus_info_for_succeeded_rpc", "false");
  AddUnDefOkAndSetFlag(
      opts.extra_tserver_flags, "TEST_always_return_consensus_info_for_succeeded_rpc", "false");

  for (auto* flags : {&opts.extra_master_flags, &opts.extra_tserver_flags}) {
    AddUnDefOkAndSetFlag(*flags, "auto_flags_apply_delay_ms", AsString(kAutoFlagsApplyDelayMs));
  }

  ExternalMiniClusterITestBase::SetUpOptions(opts);
}

uint32 UpgradeTestBase::UpgradeCompatibilityGucValue(MajorUpgradeCompatibilityType type) const {
  return type == MajorUpgradeCompatibilityType::kBackwardsCompatible ? old_ysql_major_version_ : 0;
}

Status UpgradeTestBase::SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType type) {
  if (!IsYsqlMajorVersionUpgrade()) {
    return Status::OK();
  }

  auto version = UpgradeCompatibilityGucValue(type);

  LOG(INFO) << "Setting ysql_yb_major_version_upgrade_compatibility to " << version;
  return cluster_->AddAndSetExtraFlag(
      "ysql_yb_major_version_upgrade_compatibility", ToString(version));
}

Status UpgradeTestBase::StartClusterInOldVersion(const ExternalMiniClusterOptions& options) {
  DiagScope diag(this, "StartClusterInOldVersion");
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

  RETURN_NOT_OK(ValidateYsqlMigrationCompatibility(DirName(old_version_bin_path_)));

  if (cluster_->opts_.enable_ysql) {
    server::GetStatusRequestPB req;
    server::GetStatusResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    RETURN_NOT_OK(
        cluster_->GetLeaderMasterProxy<server::GenericServiceProxy>().GetStatus(req, &resp, &rpc));
    LOG(INFO) << "From version: " << resp.status().version_info().DebugString();

    old_ysql_major_version_ = resp.status().version_info().ysql_major_version();
    is_ysql_major_version_upgrade_ =
        old_ysql_major_version_ != current_version_info_.ysql_major_version();
  }

  return Status::OK();
}

Status UpgradeTestBase::UpgradeClusterToCurrentVersion(
    MonoDelta delay_between_nodes, bool auto_finalize) {
  DiagScope diag(this, "UpgradeClusterToCurrentVersion");
  LOG(INFO) << "Upgrading cluster to current version";

  RETURN_NOT_OK_PREPEND(
      RestartAllMastersInCurrentVersion(delay_between_nodes), "Failed to restart masters");

  RETURN_NOT_OK_PREPEND(
      PerformYsqlMajorCatalogUpgrade(), "Failed to run ysql major catalog upgrade");

  RETURN_NOT_OK_PREPEND(
      RestartAllTServersInCurrentVersion(delay_between_nodes), "Failed to restart tservers");

  RETURN_NOT_OK(SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kNone));

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
  DiagScope diag(this, "RestartAllMastersInCurrentVersion");
  LOG(INFO) << "Restarting all yb-masters in current version";

  RETURN_NOT_OK(
      SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kBackwardsCompatible));

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
  DiagScope diag(this, "RestartAllTServersInCurrentVersion");
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
  DiagScope diag(this, "PerformYsqlMajorCatalogUpgrade");
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
  DiagScope diag(this, "PromoteAutoFlags");
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

  if (resp.flags_promoted()) {
    RETURN_NOT_OK(WaitForAutoFlagsConfigApplied(resp.new_config_version()));
  }

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

Status UpgradeTestBase::WaitForAutoFlagsConfigApplied(uint32_t config_version) {
  master::GetAutoFlagsConfigRequestPB req;
  master::GetAutoFlagsConfigResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(kRpcTimeout);
  RETURN_NOT_OK(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>().GetAutoFlagsConfig(
      req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  SCHECK_GE(
      resp.config().config_version(), config_version, IllegalState,
      "Master leader does not have the new AutoFlags config");

  RETURN_NOT_OK(LoggedWaitFor(
      [this, config_version]() -> Result<bool> {
        for (auto* daemon : cluster_->daemons()) {
          if (daemon->IsShutdown()) {
            continue;
          }
          server::GetAutoFlagsConfigVersionRequestPB req;
          server::GetAutoFlagsConfigVersionResponsePB resp;
          rpc::RpcController rpc;
          rpc.set_timeout(kRpcTimeout);
          RETURN_NOT_OK(cluster_->GetProxy<server::GenericServiceProxy>(daemon)
                            .GetAutoFlagsConfigVersion(req, &resp, &rpc));
          if (resp.config_version() < config_version) {
            return false;
          }
        }
        return true;
      },
      60s * kTimeMultiplier, Format("Waiting for all daemons to get AutoFlags config $0",
                                    config_version),
      /*initial_delay=*/100ms));

  // A daemon that gets the config before its apply time applies it at that time, so wait for it.
  if (resp.config().has_config_apply_time()) {
    HybridTime apply_time;
    RETURN_NOT_OK(apply_time.FromUint64(resp.config().config_apply_time()));
    const auto time_left = MonoDelta::FromMicroseconds(
        static_cast<int64_t>(apply_time.GetPhysicalValueMicros()) - GetCurrentTimeMicros());
    if (time_left > -500ms) {
      SleepFor(time_left + 500ms);
    }
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

  return Status::OK();
}

Status UpgradeTestBase::PerformYsqlUpgrade() {
  DiagScope diag(this, "PerformYsqlUpgrade");
  if (!cluster_->opts_.enable_ysql) {
    return Status::OK();
  }

  LOG(INFO) << "Running ysql upgrade";

  tserver::UpgradeYsqlRequestPB req;
  tserver::UpgradeYsqlResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(4min * kTimeMultiplier);

  RETURN_NOT_OK(cluster_->GetTServerProxy<tserver::TabletServerAdminServiceProxy>(0).UpgradeYsql(
      req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status UpgradeTestBase::FinalizeUpgrade() {
  DiagScope diag(this, "FinalizeUpgrade");
  LOG(INFO) << "Finalizing upgrade";

  RETURN_NOT_OK(SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kNone));

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
  DiagScope diag(this, "RollbackVolatileAutoFlags");
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

  if (resp.flags_rolledback()) {
    RETURN_NOT_OK(WaitForAutoFlagsConfigApplied(resp.new_config_version()));
  }

  LOG(INFO) << "Rolled back AutoFlags: " << resp.DebugString();

  return Status::OK();
}

Status UpgradeTestBase::RollbackClusterToOldVersion(MonoDelta delay_between_nodes) {
  DiagScope diag(this, "RollbackClusterToOldVersion");
  LOG(INFO) << "Rolling back upgrade";

  RETURN_NOT_OK(
      SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kBackwardsCompatible));

  RETURN_NOT_OK_PREPEND(RollbackVolatileAutoFlags(), "Failed to rollback Volatile AutoFlags");

  RETURN_NOT_OK_PREPEND(
      RestartAllTServersInOldVersion(delay_between_nodes), "Failed to restart tservers");

  RETURN_NOT_OK_PREPEND(
      RollbackYsqlMajorCatalogVersion(), "Failed to run ysql major catalog rollback");

  RETURN_NOT_OK_PREPEND(
      RestartAllMastersInOldVersion(delay_between_nodes), "Failed to restart masters");

  RETURN_NOT_OK(SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kNone));

  LOG(INFO) << "Cluster rolled back to old version";
  return Status::OK();
}

Status UpgradeTestBase::RestartAllMastersInOldVersion(MonoDelta delay_between_nodes) {
  DiagScope diag(this, "RestartAllMastersInOldVersion");
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
  DiagScope diag(this, "RestartAllTServersInOldVersion");
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
