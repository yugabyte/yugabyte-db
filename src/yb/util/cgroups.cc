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
#include "yb/util/cgroups.h"

#include <cmath>
#include <ranges>

#include "yb/gutil/sysinfo.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/os-util.h"

DEFINE_NON_RUNTIME_bool(use_cgroups_cpu, false,
    "Use the cgroup CPU quota to determine the effective number of CPUs instead of the host CPU "
    "count. Useful for containerized deployments (e.g. Kubernetes) where the process is limited "
    "to a fraction of the host's CPUs via cgroup CPU quotas.");

DEFINE_test_flag(uint64, cgroups_delay_init_ms, 0, "Milliseconds to delay cgroup initialization.");

DECLARE_int32(num_cpus);

#ifdef __linux__

#include <dirent.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <syscall.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>

#include "yb/util/enums.h"
#include "yb/util/errno.h"
#include "yb/util/flag_validators.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/stol_utils.h"
#include "yb/util/thread.h"
#include "yb/util/tostring.h"

using namespace std::literals;

namespace yb {

namespace {

constexpr auto kRootPath = "/sys/fs/cgroup";
constexpr auto kCpuRootPathV1 = "/sys/fs/cgroup/cpu";
constexpr auto kDefaultThreadCgroupName = "@default";

// Maximum time to wait for the process/thread that created a cgroup to set it up. This should
// normally be on the order of microseconds (just a couple of syscalls).
constexpr auto kCgroupSetupWaitTime = 5s;

YB_DEFINE_ENUM(CgroupVersion, (kVersion1)(kVersion2));

Status WriteConfigToDescriptor(int fd, std::string_view value) {
  ssize_t written = VERIFY_ERRNO_FN_CALL(write, fd, value.data(), value.size());
  if (written != static_cast<ssize_t>(value.size())) {
    return STATUS_FROM_ERRNO("write cgroup config", errno);
  }
  return Status::OK();
}

Status WriteConfigToPath(const std::string& path, std::string_view value) {
  int fd = VERIFY_ERRNO_FN_CALL(open, path.c_str(), O_WRONLY);
  ScopeExit s([fd] { close(fd); });
  return WriteConfigToDescriptor(fd, value);
}

Result<CgroupVersion> GetCgroupVersion() {
  static const auto version = ([] -> Result<CgroupVersion> {
    struct statfs s;
    RETURN_ON_ERRNO_FN_CALL(statfs, kRootPath, &s);
    switch (s.f_type) {
      // Sometimes it gets mounted as tmpfs instead of cgroup.
      case TMPFS_MAGIC:
        [[fallthrough]];
      case CGROUP_SUPER_MAGIC:
        return CgroupVersion::kVersion1;
      case CGROUP2_SUPER_MAGIC:
        return CgroupVersion::kVersion2;
      default:
        return STATUS_FORMAT(
            IllegalState, "filesystem type of $0 is not cgroup or cgroup2", kRootPath);
    }
  })();
  return version;
}

Result<std::string> CpuRootPath() {
  if (VERIFY_RESULT(GetCgroupVersion()) == CgroupVersion::kVersion1) {
    return kCpuRootPathV1;
  } else {
    return kRootPath;
  }
}

Result<std::string> ReadCpuGroup(
    int64_t process_or_thread_id, std::optional<CgroupVersion> version,
    bool check_controllers = true) {
  // Arbitrary length that is definitely long enough.
  constexpr auto kMaxConfigLength = 65535uz;
  std::string cgroups = VERIFY_RESULT(ReadUnixConfigFromPath(
      Format("/proc/$0/cgroup", process_or_thread_id), kMaxConfigLength));
  if (!version) {
    version.emplace(VERIFY_RESULT(GetCgroupVersion()));
  }
  if (*version == CgroupVersion::kVersion1) {
    // Cgroups v1 format is multiple lines like:
    // 5:cpuacct,cpu,cpuset:/cgroup/path1
    // 4:memory:/cgroup/path2
    // The line we care about is the one with cpu.
    for (const auto& cgroup : StringSplit(cgroups, '\n')) {
      auto parts = StringSplit(cgroup, ':');
      auto controllers = StringSplit(parts[1], ',');
      if (std::ranges::find(controllers, "cpu") != controllers.end()) {
        return parts[2];
      }
    }
    return STATUS(IllegalState, "CPU controller not mounted");
  } else {
    // Cgroups v2 format is a single line:
    // 0::/cgroup/path
    auto cgroup = cgroups.substr(3);
    if (check_controllers) {
      // Since Cgroups v2 has a unified hierarchy for all controllers, we also need to check
      // if CPU controller is available.
      auto controllers = StringSplit(VERIFY_RESULT(ReadUnixConfigFromPath(
          Format("$0$1/cgroup.controllers", VERIFY_RESULT(CpuRootPath()), cgroup),
          kMaxConfigLength)), ' ');
      if (std::ranges::find(controllers, "cpu") == controllers.end()) {
        return STATUS(IllegalState, "CPU controller not delegated");
      }
    }
    return cgroup;
  }
}

Status CleanupAllChildCgroups(const std::string& cgroup) {
  VLOG(1) << "Cleaning up cgroup " << cgroup;
  DIR* dir = opendir(cgroup.c_str());
  if (!dir) {
    return STATUS_FROM_ERRNO("open cgroup "s + cgroup, errno);
  }
  ScopeExit s([dir] { closedir(dir); });

  errno = 0;
  while (auto* entry = readdir(dir)) {
    if (entry->d_type == DT_DIR && entry->d_name[0] != '.') {
      VLOG(2) << "Found child cgroup " << entry->d_name << " under cgroup " << cgroup;
      auto full_path = Format("$0/$1", cgroup, entry->d_name);
      RETURN_NOT_OK(CleanupAllChildCgroups(full_path.c_str()));
      // cgroup filesystem is special: there are special files but the directory itself is
      // considered empty if there are no child cgroups, so rmdir works.
      RETURN_ON_ERRNO_FN_CALL(rmdir, full_path.c_str());
    } else {
      VLOG(2) << "Found non-cgroup file " << entry->d_name << " under cgroup " << cgroup
              << ", ignoring";
    }
  }
  return STATUS_FROM_ERRNO("reading cgroup "s + cgroup, errno);
}

// Use of this class is not thread safe until Init() has been called (and it is read-only
// afterwards).
class CgroupManager {
 public:
  Status Init(ClearChildCgroups clear) {
    RSTATUS_DCHECK(!initialized_, IllegalState, "CgroupManager already initialized");
    version_ = VERIFY_RESULT(GetCgroupVersion());
    auto cpu_cgroup = VERIFY_RESULT(ReadCpuGroup(getpid(), version_));
    RSTATUS_DCHECK(!cpu_cgroup.empty(), IllegalState, "CPU controller not available");
    RSTATUS_DCHECK(cpu_cgroup != "/", IllegalState, "Cannot manage root cgroup");
    process_cpu_cgroup_ = cpu_cgroup;
    cpu_root_path_ = VERIFY_RESULT(CpuRootPath());
    process_cpu_cgroup_path_ = cpu_root_path_ + cpu_cgroup;

    if (clear) {
      RETURN_NOT_OK(CleanupAllChildCgroups(process_cpu_cgroup_path_));
    }
    root_ = std::make_unique<Cgroup>(nullptr /* parent */, "" /* name */);
    RETURN_NOT_OK(root_->Init(true /* is_root */));
    default_group_.store(
        &VERIFY_RESULT_REF(root_->CreateOrLoadChild(kDefaultThreadCgroupName)),
        std::memory_order_release);
    RETURN_NOT_OK(default_group_.load(std::memory_order_relaxed)->MoveCurrentThreadToGroup());

    initialized_ = true;
    return Status::OK();
  }

  Cgroup* root_group() { return root_.get(); }
  Cgroup* default_thread_group() { return default_group_.load(std::memory_order_acquire); }
  void set_default_thread_group(Cgroup* cg) {
    default_group_.store(cg, std::memory_order_release);
  }
  CgroupVersion version() { return version_; }
  std::string_view cpu_group() { return process_cpu_cgroup_; }
  std::string_view cpu_root_path() { return cpu_root_path_; }
  std::string_view cpu_group_path() { return process_cpu_cgroup_path_; }

  bool initialized() { return initialized_; }

 private:
  CgroupVersion version_;
  std::string process_cpu_cgroup_;
  std::string cpu_root_path_;
  std::string process_cpu_cgroup_path_;
  std::unique_ptr<Cgroup> root_;
  std::atomic<Cgroup*> default_group_{nullptr};
  bool initialized_ = false;
};

CgroupManager cgroup_manager;

std::string CgroupConfigPath(std::string_view cgroup_name, std::string_view config) {
  return Format("$0$1/$2", cgroup_manager.cpu_group_path(), cgroup_name, config);
}

Result<int> CpuFractionToCfsQuotaMicroseconds(double cpu_fraction, int period_us) {
  int max_quota_us = base::NumCPUs() * period_us;
  int cfs_quota_us = static_cast<int>(std::round(cpu_fraction * max_quota_us));
  // Linux requires cfs_quota_us to be at least 1 ms.
  if (cfs_quota_us < 1'000) {
    double min_cpu_fraction = 1'000.0 / max_quota_us;
    return STATUS_FORMAT(
        InvalidArgument,
        "Period ($0 us) is too low for max cpu of $1% (minimum possible setting with this period "
        "is $2%)",
        period_us, (100.0 * cpu_fraction), (100.0 * min_cpu_fraction));
  }
  // Return -1 for max, since that's what cgroups v1 uses for cfs_quota_us.
  return cfs_quota_us < max_quota_us ? cfs_quota_us : -1;
}

} // namespace

Cgroup::Cgroup(Cgroup* parent, std::string_view name)
  : parent_(parent), name_(name),
    log_prefix_(Format("cgroup[$0]: ", name.empty() ? "<root>"sv : name)) {}

Cgroup::~Cgroup() {
  if (threads_fd_ != -1) {
    close(threads_fd_);
  }
}

Status Cgroup::WriteConfig(std::string_view config, std::string_view value) const {
  VLOG_WITH_PREFIX(1) << "Write config: " << config << " = " << value;
  return WriteConfigToPath(CgroupConfigPath(name_, config), value);
}

Result<std::string> Cgroup::ReadConfig(std::string_view config, size_t max_length) const {
  VLOG_WITH_PREFIX(1) << "Read config: " << config;
  auto out = ReadUnixConfigFromPath(CgroupConfigPath(name_, config), max_length);
  VLOG_WITH_PREFIX(1) << "Config: " << config << " = " << out;
  return out;
}

Status Cgroup::Init(bool is_root) {
  auto init_delay = FLAGS_TEST_cgroups_delay_init_ms;
  if (init_delay > 0) {
    SleepFor(init_delay * 1ms);
  }

  if (!is_root) {
    RETURN_NOT_OK(UpdateCpuLimits(/*max_cpu_fraction=*/1.0));
    RETURN_NOT_OK(UpdateCpuWeight(kDefaultCpuWeight));
    if (cgroup_manager.version() == CgroupVersion::kVersion2) {
      RETURN_NOT_OK(WriteConfig("cgroup.type", "threaded"));
    }
  }
  if (cgroup_manager.version() == CgroupVersion::kVersion2) {
    RETURN_NOT_OK(WriteConfig("cgroup.subtree_control", "+cpu"));
  } else {
    // This setting only affects the cpuset controller, which we do not use. So this effectively
    // does nothing, and we just use it as a way to signal that initialization is complete.
    RETURN_NOT_OK(WriteConfig("cgroup.clone_children", "1"));
  }
  return Status::OK();
}

Result<bool> Cgroup::CheckReady() const {
  if (cgroup_manager.version() == CgroupVersion::kVersion2) {
    // Need to check both since reading from cgroup.subtree_control will fail if cgroup.type hasn't
    // been set.
    return VERIFY_RESULT(ReadConfig("cgroup.type")) == "threaded" &&
        VERIFY_RESULT(ReadConfig("cgroup.subtree_control")) == "cpu";
  } else {
    return VERIFY_RESULT(ReadConfig("cgroup.clone_children")) == "1";
  }
}

Status Cgroup::CheckMaxCpuValidForPeriod(double max_cpu_fraction, int period_us) {
  return ResultToStatus(CpuFractionToCfsQuotaMicroseconds(max_cpu_fraction, period_us));
}

Status Cgroup::UpdateCpuLimits(
    std::optional<double> max_cpu_fraction, std::optional<int> period_us) {
  std::lock_guard lock(mutex_);

  int cfs_period_us = period_us.value_or(cpu_period_us_);
  double cpu_fraction = max_cpu_fraction.value_or(cpu_max_fraction_);
  int cfs_quota_us = VERIFY_RESULT(CpuFractionToCfsQuotaMicroseconds(cpu_fraction, cfs_period_us));

  if (cgroup_manager.version() == CgroupVersion::kVersion1) {
    if (period_us) {
      RETURN_NOT_OK(WriteConfig("cpu.cfs_period_us", AsString(cfs_period_us)));
    }
    if (max_cpu_fraction) {
      RETURN_NOT_OK(WriteConfig("cpu.cfs_quota_us", AsString(cfs_quota_us)));
    }
  } else {
    RETURN_NOT_OK(WriteConfig(
        "cpu.max",
        Format("$0 $1", cfs_quota_us == -1 ? "max"s : AsString(cfs_quota_us), cfs_period_us)));
  }
  cpu_max_fraction_ = cpu_fraction;
  cpu_period_us_ = cfs_period_us;
  return Status::OK();
}

Status Cgroup::UpdateCpuWeight(int weight) {
  RSTATUS_DCHECK_GE(weight, 1, InvalidArgument, "weight must be in range [1, 10000]");
  RSTATUS_DCHECK_LE(weight, 10'000, InvalidArgument, "weight must be in range [1, 10000]");

  std::lock_guard lock(mutex_);
  if (cgroup_manager.version() == CgroupVersion::kVersion1) {
    // See comment in header about cpu weight.
    RETURN_NOT_OK(WriteConfig("cpu.shares", AsString(weight * 2)));
  } else {
    RETURN_NOT_OK(WriteConfig("cpu.weight", AsString(weight)));
  }
  cpu_weight_ = weight;
  return Status::OK();
}

Result<Cgroup&> Cgroup::CreateOrLoadChild(std::string_view child_name) {
  std::lock_guard lock(mutex_);

  if (auto itr = children_.find(child_name); itr != children_.end()) {
    return itr->second;
  }

  std::string name = Format("$0/$1", name_, child_name);
  std::string full_path = std::string(cgroup_manager.cpu_group_path()) + name;
  if (mkdir(full_path.c_str(), 0755) != -1) {
    VLOG_WITH_PREFIX(1) << "Create cgroup " << full_path;
    auto& cg = children_.try_emplace(std::string(name), this, name).first->second;
    RETURN_NOT_OK(cg.Init());
    return cg;
  }

  if (errno != EEXIST) {
    return STATUS_FROM_ERRNO("creating cgroup " + full_path, errno);
  }
  VLOG_WITH_PREFIX(1) << "Found existing cgroup " << full_path;

  // Cgroup already exists: it may have been created by another process (child/parent).
  auto& cg = children_.try_emplace(std::string(name), this, name).first->second;
  RETURN_NOT_OK(WaitFor(
      [&cg] { return cg.CheckReady(); },
      kCgroupSetupWaitTime,
      Format("Wait for creator of cgroup $0 to set it up", name)));

  int cfs_period_us;
  int cfs_quota_us;
  int weight;
  if (cgroup_manager.version() == CgroupVersion::kVersion1) {
    cfs_period_us = VERIFY_RESULT(CheckedStoi(VERIFY_RESULT(cg.ReadConfig("cpu.cfs_period_us"))));
    cfs_quota_us = VERIFY_RESULT(CheckedStoi(VERIFY_RESULT(cg.ReadConfig("cpu.cfs_quota_us"))));
    weight = VERIFY_RESULT(CheckedStoi(VERIFY_RESULT(cg.ReadConfig("cpu.shares"))));
  } else {
    // This is in format "quota period" where quota/period are positive integers, except quota
    // may be the string "max".
    auto max_config = VERIFY_RESULT(cg.ReadConfig("cpu.max"));
    auto separator = max_config.find(' ');
    if (separator == max_config.npos) {
      LOG_WITH_PREFIX(DFATAL) << "could not parse cpu.max: " << max_config;
      return STATUS_FORMAT(IllegalState, "could not parse cpu.max: $0", max_config);
    }

    std::string_view max_config_view = max_config;
    auto period_str = max_config_view.substr(0, separator);
    if (period_str == "max") {
      cfs_quota_us = -1;
    } else {
      cfs_quota_us = VERIFY_RESULT(CheckedStoi(period_str));
    }
    cfs_period_us = VERIFY_RESULT(CheckedStoi(max_config_view.substr(separator + 1)));
    weight = VERIFY_RESULT(CheckedStoi(VERIFY_RESULT(cg.ReadConfig("cpu.weight"))));
  }

  std::lock_guard child_lock(cg.mutex_);
  cg.cpu_period_us_ = cfs_period_us;
  cg.cpu_max_fraction_ = cfs_quota_us < 0 ? 1.0 : static_cast<double>(cfs_quota_us) / cfs_period_us;
  cg.cpu_weight_ = weight;
  return cg;
}

Status Cgroup::MoveThreadToGroup(int64_t thread_id) {
  DCHECK(parent_) << "Cannot move thread into root group";

  VLOG_WITH_PREFIX(1) << "Add thread " << thread_id;

  int fd = threads_fd_.load();
  if (fd == -1) {
    std::lock_guard lock(mutex_);
    fd = threads_fd_.load();
    if (fd == -1) {
      fd = VERIFY_ERRNO_FN_CALL(
          open,
          CgroupConfigPath(
              name_,
              cgroup_manager.version() == CgroupVersion::kVersion1 ? "tasks" : "cgroup.threads")
              .c_str(),
          O_WRONLY);
      threads_fd_.store(fd);
    }
  }
  return WriteConfigToDescriptor(fd, AsString(thread_id));
}

Status Cgroup::MoveCurrentThreadToGroup() {
  return MoveThreadToGroup(Thread::CurrentThreadId());
}

Status Cgroup::MoveProcessToGroup(int64_t pid) {
  DCHECK(parent_) << "Cannot move process into root group";
  VLOG_WITH_PREFIX(1) << "Add process " << pid;
  return WriteConfig("cgroup.procs", AsString(pid));
}

void Cgroup::VisitChildren(const std::function<void(Cgroup&)>& visitor) {
  std::vector<Cgroup*> children;
  {
    std::lock_guard lock(mutex_);
    children.reserve(children_.size());
    for (auto& child : children_ | std::views::values) {
      children.push_back(&child);
    }
  }
  for (auto& child : children) {
    visitor(*child);
  }
}

void Cgroup::VisitTree(
    const std::function<void(Cgroup&, size_t)>& visitor, size_t current_depth, size_t max_depth) {
  visitor(*this, current_depth);
  if (current_depth == max_depth) {
    return;
  }
  VisitChildren([&](Cgroup& child) {
    child.VisitTree(visitor, current_depth + 1, max_depth);
  });
}

Result<std::vector<int64_t>> Cgroup::ReadThreadIds() {
  auto path = CgroupConfigPath(
      name_, cgroup_manager.version() == CgroupVersion::kVersion1 ? "tasks" : "cgroup.threads");
  VLOG(3) << "Read " << path;
  int fd = VERIFY_ERRNO_FN_CALL(open, path.c_str(), O_RDONLY);
  ScopeExit s([fd] { close(fd); });

  constexpr ssize_t kReadSize = 4096;
  ssize_t bytes_read;
  std::string contents;
  do {
    std::array<char, kReadSize> buffer;
    bytes_read = VERIFY_ERRNO_FN_CALL(read, fd, buffer.data(), kReadSize);
    contents += std::string(buffer.data(), bytes_read);
  } while (bytes_read == kReadSize);

  if (contents.empty()) {
    return std::vector<int64_t>{};
  }

  // Last byte is a newline, drop it.
  contents.resize(contents.size() - 1);
  std::vector<int64_t> ids;
  for (const auto& id_str : StringSplit(contents, '\n')) {
    ids.push_back(VERIFY_RESULT(CheckedStol<int64_t>(Slice(id_str))));
  }
  return ids;
}

Result<std::vector<std::string>> Cgroup::ReadThreadNames() {
  std::vector<std::string> names;
  for (int64_t thread_id : VERIFY_RESULT(ReadThreadIds())) {
    auto result = Thread::ThreadName(thread_id);
    if (!result.ok()) {
      // This is possible if thread has exited since ReadThreadIds().
      LOG(WARNING) << "Failed to read thread name: " << result.status();
      continue;
    }
    names.emplace_back(*result);
  }
  return names;
}

Result<CgroupCpuStats> Cgroup::ReadCpuStats() const {
  CgroupCpuStats stats;

  if (cgroup_manager.version() == CgroupVersion::kVersion1) {
    // cpu.stat: "nr_periods N\nnr_throttled N\nthrottled_time N\n"
    auto cpu_stat = VERIFY_RESULT(ReadConfig("cpu.stat", /*max_length=*/256));
    for (const auto& line : StringSplit(cpu_stat, '\n')) {
      auto parts = StringSplit(line, ' ');
      if (parts.size() < 2) continue;
      if (parts[0] == "nr_periods") {
        stats.nr_periods = VERIFY_RESULT(CheckedStoll(parts[1]));
      } else if (parts[0] == "nr_throttled") {
        stats.nr_throttled = VERIFY_RESULT(CheckedStoll(parts[1]));
      } else if (parts[0] == "throttled_time") {
        stats.throttled_time_ns = VERIFY_RESULT(CheckedStoll(parts[1]));
      }
    }
    stats.usage_ns = VERIFY_RESULT(CheckedStoll(VERIFY_RESULT(ReadConfig("cpuacct.usage"))));
    stats.usage_user_ns =
        VERIFY_RESULT(CheckedStoll(VERIFY_RESULT(ReadConfig("cpuacct.usage_user"))));
    stats.usage_sys_ns =
        VERIFY_RESULT(CheckedStoll(VERIFY_RESULT(ReadConfig("cpuacct.usage_sys"))));
  } else {
    // cgv2: cpu.stat has all fields in one file.
    auto cpu_stat = VERIFY_RESULT(ReadConfig("cpu.stat", /*max_length=*/512));
    for (const auto& line : StringSplit(cpu_stat, '\n')) {
      auto parts = StringSplit(line, ' ');
      if (parts.size() < 2) continue;
      if (parts[0] == "usage_usec") {
        stats.usage_ns = VERIFY_RESULT(CheckedStoll(parts[1])) * 1000;
      } else if (parts[0] == "user_usec") {
        stats.usage_user_ns = VERIFY_RESULT(CheckedStoll(parts[1])) * 1000;
      } else if (parts[0] == "system_usec") {
        stats.usage_sys_ns = VERIFY_RESULT(CheckedStoll(parts[1])) * 1000;
      } else if (parts[0] == "nr_periods") {
        stats.nr_periods = VERIFY_RESULT(CheckedStoll(parts[1]));
      } else if (parts[0] == "nr_throttled") {
        stats.nr_throttled = VERIFY_RESULT(CheckedStoll(parts[1]));
      } else if (parts[0] == "throttled_usec") {
        stats.throttled_time_ns = VERIFY_RESULT(CheckedStoll(parts[1])) * 1000;
      }
    }
  }

  return stats;
}

std::string Cgroup::full_name() const {
  return std::string(cgroup_manager.cpu_group()) + name_;
}

std::string Cgroup::path() const {
  return std::string(cgroup_manager.cpu_root_path()) + full_name();
}

Cgroup* Cgroup::child(std::string_view name) {
  std::lock_guard lock(mutex_);
  auto itr = children_.find(name);
  if (itr == children_.end()) {
    return nullptr;
  }
  return &itr->second;
}

bool Cgroup::is_leaf() const {
  std::lock_guard lock(mutex_);
  return children_.empty();
}

std::string Cgroup::ToString() const {
  std::lock_guard lock(mutex_);
  return YB_STRUCT_TO_STRING(name_, cpu_period_us_, cpu_max_fraction_, cpu_weight_);
}

Status SetupCgroupManagement(ClearChildCgroups clear) {
  return cgroup_manager.Init(clear);
}

bool CgroupManagementEnabled() {
  return cgroup_manager.initialized();
}

Cgroup* RootCgroup() {
  return cgroup_manager.root_group();
}

Cgroup* DefaultThreadCgroup() {
  return cgroup_manager.default_thread_group();
}

void SetDefaultThreadCgroup(Cgroup* cgroup) {
  cgroup_manager.set_default_thread_group(cgroup);
}

Result<std::string> GetProcessCpuCgroup(int64_t process_id, bool check_controllers) {
  return ReadCpuGroup(
      process_id == -1 ? getpid() : process_id,
      cgroup_manager.initialized() ? std::make_optional(cgroup_manager.version()) : std::nullopt,
      check_controllers);
}

Result<std::string> GetProcessCpuCgroupPath(int64_t process_id, bool check_controllers) {
  return VERIFY_RESULT(CpuRootPath()) +
         VERIFY_RESULT(GetProcessCpuCgroup(process_id, check_controllers));
}

Result<std::string> GetThreadCpuCgroup(int64_t thread_id) {
  return ReadCpuGroup(
      thread_id == -1 ? Thread::CurrentThreadId() : thread_id,
      cgroup_manager.initialized() ? std::make_optional(cgroup_manager.version()) : std::nullopt);
}

Status MoveProcessToCgroupPath(std::string_view cgroup_path) {
  LOG(INFO) << "Move process to cgroup: " << cgroup_path;
  return WriteConfigToPath(Format("$0/cgroup.procs", cgroup_path), AsString(getpid()));
}

Result<int> GetCgroupCpuQuota() {
  // Arbitrary length that is definitely long enough.
  constexpr auto kMaxConfigLength = 256uz;
  auto cgroup_path = VERIFY_RESULT(GetProcessCpuCgroupPath(getpid(), /*check_controllers=*/false));
  auto version = VERIFY_RESULT(GetCgroupVersion());

  int64_t quota_us = 0;
  int64_t period_us = 0;
  if (version == CgroupVersion::kVersion1) {
    auto quota_str = VERIFY_RESULT(ReadUnixConfigFromPath(
        cgroup_path + "/cpu.cfs_quota_us", kMaxConfigLength));
    auto period_str = VERIFY_RESULT(ReadUnixConfigFromPath(
        cgroup_path + "/cpu.cfs_period_us", kMaxConfigLength));
    quota_us = VERIFY_RESULT(CheckedStol<int64_t>(Slice(quota_str)));
    period_us = VERIFY_RESULT(CheckedStol<int64_t>(Slice(period_str)));
    // cgroup v1 uses -1 to signal "no limit".
    if (quota_us < 0) {
      return -1;
    }
  } else {
    auto max_config = VERIFY_RESULT(ReadUnixConfigFromPath(
        cgroup_path + "/cpu.max", kMaxConfigLength));
    auto separator = max_config.find(' ');
    if (separator == max_config.npos) {
      return STATUS_FORMAT(IllegalState, "could not parse cpu.max: $0", max_config);
    }
    std::string_view max_view = max_config;
    auto quota_part = max_view.substr(0, separator);
    if (quota_part == "max") {
      return -1;
    }
    quota_us = VERIFY_RESULT(CheckedStol<int64_t>(Slice(quota_part)));
    period_us = VERIFY_RESULT(CheckedStol<int64_t>(Slice(max_view.substr(separator + 1))));
  }

  if (quota_us <= 0 || period_us <= 0) {
    return -1;
  }
  return static_cast<int>(std::ceil(static_cast<double>(quota_us) / period_us));
}

} // namespace yb
#endif // __linux__

namespace yb {

int NumEffectiveCPUs() {
  // --num_cpus is an explicit operator override and takes precedence over the cgroup quota.
  if (FLAGS_num_cpus != 0 || !FLAGS_use_cgroups_cpu) {
    return base::NumCPUs();
  }
#ifdef __linux__
  // Read once and cache for the lifetime of the process. Runtime changes to the cgroup quota
  // will not be reflected.
  static const int cached = [] {
    auto quota = GetCgroupCpuQuota();
    if (!quota.ok()) {
      LOG(WARNING) << "Failed to read cgroup CPU quota, falling back to host CPU count: "
                   << quota.status();
      return base::NumCPUs();
    }
    if (*quota <= 0) {
      // No cgroup CPU limit set.
      return base::NumCPUs();
    }
    LOG(INFO) << "Using cgroup CPU quota: " << *quota << " (host CPUs: " << base::NumCPUs() << ")";
    return *quota;
  }();
  return cached;
#else
  return base::NumCPUs();
#endif
}

} // namespace yb
