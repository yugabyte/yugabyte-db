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
#ifdef __linux__
#include "yb/util/cgroups.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <syscall.h>
#include <unistd.h>

#include <cstdlib>

#include "yb/gutil/sysinfo.h"

#include "yb/util/enums.h"
#include "yb/util/errno.h"
#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
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

Result<std::string> ReadConfigFromPath(const std::string& path, size_t max_length) {
  VLOG(3) << "Read config path: " << path;
  int fd = VERIFY_ERRNO_FN_CALL(open, path.c_str(), O_RDONLY);
  ScopeExit s([fd] { close(fd); });

  std::string out(max_length + 1, '\0');
  ssize_t bytes_read = VERIFY_ERRNO_FN_CALL(read, fd, out.data(), max_length + 1);
  if (static_cast<size_t>(bytes_read) > max_length) {
    return STATUS_FROM_ERRNO(Format(
        "cgroup config $0 too long, first $1 bytes: $2", path, max_length + 1, out), errno);
  }

  if (bytes_read == 0) {
    return STATUS_FORMAT(IllegalState, "config file $0 is empty", path);
  }

  // Last byte is a newline, drop it.
  out.resize(static_cast<size_t>(bytes_read - 1));
  return out;
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
    int64_t process_or_thread_id, std::optional<CgroupVersion> version = std::nullopt) {
  // Arbitrary length that is definitely long enough.
  constexpr auto kMaxConfigLength = 65535uz;
  std::string cgroups = VERIFY_RESULT(ReadConfigFromPath(
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
    // Since Cgroups v2 has a unified hierarchy for all controllers, we also need to check
    // if CPU controller is available.
    auto controllers = StringSplit(VERIFY_RESULT(ReadConfigFromPath(
        Format("$0$1/cgroup.controllers", VERIFY_RESULT(CpuRootPath()), cgroup),
        kMaxConfigLength)), ' ');
    if (std::ranges::find(controllers, "cpu") != controllers.end()) {
      return cgroup;
    }
    return STATUS(IllegalState, "CPU controller not delegated");
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
    process_cpu_cgroup_path_ = VERIFY_RESULT(CpuRootPath()) + cpu_cgroup;

    if (clear) {
      RETURN_NOT_OK(CleanupAllChildCgroups(process_cpu_cgroup_path_));
    }
    root_ = std::make_unique<Cgroup>(nullptr /* parent */, "" /* name */);
    RETURN_NOT_OK(root_->Init(true /* is_root */));
    default_group_ = &VERIFY_RESULT_REF(root_->CreateOrLoadChild(kDefaultThreadCgroupName));
    RETURN_NOT_OK(default_group_->MoveCurrentThreadToGroup());

    initialized_ = true;
    return Status::OK();
  }

  Cgroup* root_group() { return root_.get(); }
  Cgroup* default_thread_group() { return default_group_; }
  CgroupVersion version() { return version_; }
  std::string_view cpu_group() { return process_cpu_cgroup_; }
  std::string_view cpu_group_path() { return process_cpu_cgroup_path_; }

  bool initialized() { return initialized_; }

 private:
  CgroupVersion version_;
  std::string process_cpu_cgroup_;
  std::string process_cpu_cgroup_path_;
  std::unique_ptr<Cgroup> root_;
  Cgroup* default_group_;
  bool initialized_ = false;
};

CgroupManager cgroup_manager;

std::string CgroupConfigPath(std::string_view cgroup_name, std::string_view config) {
  return Format("$0$1/$2", cgroup_manager.cpu_group_path(), cgroup_name, config);
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
  auto out = ReadConfigFromPath(CgroupConfigPath(name_, config), max_length);
  VLOG_WITH_PREFIX(1) << "Config: " << config << " = " << out;
  return out;
}

Status Cgroup::Init(bool is_root) {
  if (!is_root) {
    RETURN_NOT_OK(UpdateMaxCpu(1.0 /* quota */));
    RETURN_NOT_OK(UpdateCpuWeight(kDefaultCpuWeight));
    if (cgroup_manager.version() == CgroupVersion::kVersion2) {
      RETURN_NOT_OK(WriteConfig("cgroup.type", "threaded"));
    }
  }
  if (cgroup_manager.version() == CgroupVersion::kVersion2) {
    RETURN_NOT_OK(WriteConfig("cgroup.subtree_control", "+cpu"));
  }
  return Status::OK();
}

Status Cgroup::UpdateMaxCpu(std::optional<double> quota, std::optional<int> period_us) {
  std::lock_guard lock(mutex_);

  int cfs_quota_us;
  int cfs_period_us = period_us.value_or(cpu_period_us_);
  double new_quota = quota.value_or(cpu_quota_);
  if (new_quota >= 1.0) {
    // Cgroups v1 uses -1 as the unbounded value, while Cgroups v2 uses "max" as unbounded.
    cfs_quota_us = -1;
  } else {
    cfs_quota_us = static_cast<int>(std::round(new_quota * base::NumCPUs() * cfs_period_us));
    // Linux requires cfs_quota_us to be at least 1ms.
    if (cfs_quota_us < 1'000) {
      double min_quota = 1'000.0 / cfs_period_us;
      return STATUS_FORMAT(
          InvalidArgument,
          "Period ($0us) is too low, cannot satisfy requested max cpu of $1% (minimum possible "
          "setting with current period is $2%)",
          cfs_period_us, (100.0 * new_quota), (100.0 * min_quota));
    }
  }

  if (cgroup_manager.version() == CgroupVersion::kVersion1) {
    RETURN_NOT_OK(WriteConfig("cpu.cfs_period_us", AsString(cfs_period_us)));
    if (quota) {
      RETURN_NOT_OK(WriteConfig("cpu.cfs_quota_us", AsString(cfs_quota_us)));
    }
  } else {
    RETURN_NOT_OK(WriteConfig(
        "cpu.max",
        Format("$0 $1", cfs_quota_us == -1 ? "max"s : AsString(cfs_quota_us), cfs_period_us)));
  }
  cpu_quota_ = new_quota;
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

  double quota = cfs_quota_us < 0 ? 1.0 : static_cast<double>(cfs_quota_us) / cfs_period_us;
  std::lock_guard child_lock(cg.mutex_);
  cg.cpu_period_us_ = cfs_period_us;
  cg.cpu_quota_ = quota;
  cg.cpu_weight_ = weight;
  return cg;
}

Status Cgroup::MoveCurrentThreadToGroup() {
  DCHECK(parent_) << "Cannot move thread into root group";

  auto thread_id = Thread::CurrentThreadId();
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

std::string Cgroup::full_name() const {
  return std::string(cgroup_manager.cpu_group()) + name_;
}

Cgroup* Cgroup::child(std::string_view name) {
  std::lock_guard lock(mutex_);
  auto itr = children_.find(name);
  if (itr == children_.end()) {
    return nullptr;
  }
  return &itr->second;
}

std::string Cgroup::ToString() const {
  std::lock_guard lock(mutex_);
  return YB_STRUCT_TO_STRING(name_, cpu_period_us_, cpu_quota_, cpu_weight_);
}

Status SetupCgroupManagement(ClearChildCgroups clear) {
  return cgroup_manager.Init(clear);
}

Cgroup* RootCgroup() {
  return cgroup_manager.root_group();
}

Cgroup* DefaultThreadCgroup() {
  return cgroup_manager.default_thread_group();
}

Result<std::string> GetProcessCpuCgroup() {
  return ReadCpuGroup(
      getpid(),
      cgroup_manager.initialized() ? std::make_optional(cgroup_manager.version()) : std::nullopt);
}

Result<std::string> GetProcessCpuCgroupPath() {
  return VERIFY_RESULT(CpuRootPath()) + VERIFY_RESULT(GetProcessCpuCgroup());
}

Result<std::string> GetThreadCpuCgroup(int64_t thread_id) {
  return ReadCpuGroup(
      thread_id == -1 ? Thread::CurrentThreadId() : thread_id,
      cgroup_manager.initialized() ? std::make_optional(cgroup_manager.version()) : std::nullopt);
}

} // namespace yb
#endif // __linux__
