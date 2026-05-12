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
#pragma once

#ifdef __linux__

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <tuple>

#include "yb/gutil/stl_util.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/result.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(ClearChildCgroups);

struct CgroupCpuStats {
  int64_t nr_periods = 0;
  int64_t nr_throttled = 0;
  int64_t throttled_time_ns = 0;
  int64_t usage_ns = 0;
  int64_t usage_user_ns = 0;
  int64_t usage_sys_ns = 0;
};

class Cgroup {
 public:
  // Default of cfs_period_us.
  static constexpr int kDefaultCpuPeriod = 100'000;

  // Weight is relative: two groups with weights 10 and 20 will split CPU 1:2 if both are not idle,
  // but either group can take up 100% if the other is idle.
  // Indirectly maps to cpu.shares for cgroup v1 and cpu.weight for cgroup v2.
  //
  // cpu.shares expects values in [2, 262144] whereas cpu.weight expects weights in [1, 10000].
  // Since:
  // - we expect to control everything under the root cgroup and set all weights
  // - we do not leave any thread in internal cgroups (which would otherwise have a weight
  //   calculated automatically based on niceness)
  // weights for cgroups are relative only to other weights that we pick, and the absolute value is
  // irrelevant. So our code uses weights in [1, 10000], and this maps to the underlying weight
  // for cpu.weight, and half the underlying weight for cpu.shares.
  static constexpr int kDefaultCpuWeight = 100;

  Cgroup(Cgroup* parent, std::string_view name);
  Cgroup(Cgroup&&) = delete;
  Cgroup(const Cgroup&) = delete;

  ~Cgroup();

  Status Init(bool is_root = false);

  static Status CheckMaxCpuValidForPeriod(double max_cpu_fraction, int period_us);

  // max_cpu_fraction is a fraction of total CPUs, i.e., 1.0 is all CPUs (uncapped).
  // std::nullopt for either parameter to leave it unchanged.
  Status UpdateCpuLimits(
      std::optional<double> max_cpu_fraction, std::optional<int> period_us = std::nullopt)
      EXCLUDES(mutex_);

  Status UpdateCpuWeight(int weight) EXCLUDES(mutex_);

  Result<Cgroup&> CreateOrLoadChild(std::string_view name) EXCLUDES(mutex_);

  Status MoveThreadToGroup(int64_t thread_id) EXCLUDES(mutex_);
  Status MoveCurrentThreadToGroup() EXCLUDES(mutex_);

  // Move a child process (by PID) into this cgroup. Writes to cgroup.procs.
  Status MoveProcessToGroup(int64_t pid) EXCLUDES(mutex_);

  // Read CPU throttling stats (cpu.stat) and usage counters (cpuacct.usage*).
  // All values are cumulative since cgroup creation.
  Result<CgroupCpuStats> ReadCpuStats() const;

  void VisitChildren(const std::function<void(Cgroup&)>& visitor);

  void VisitTree(
      const std::function<void(Cgroup&, size_t)>& visitor,
      size_t current_depth = 0,
      size_t max_depth = std::numeric_limits<size_t>::max());

  // These functions have an inherent race condition: the threads they read may change
  // cgroups or exit immediately after reading, and the thread id may even be reused by
  // another thread before returning. They should only be used for testing and for
  // informational purposes like logs and debugging UI.
  Result<std::vector<int64_t>> ReadThreadIds();
  Result<std::vector<std::string>> ReadThreadNames();

  // The part of the cgroup name under the root cgroup, e.g. name() of /sys/fs/cgroup/a/b/c with
  // root cgroup of /sys/fs/cgroup/a is "b/c".
  std::string_view name() const { return name_; }

  // The full cgroup name, matching entries in /proc/$PID/cgroup, e.g. name() of
  // /sys/fs/cgroup/a/b/c is "/a/b/c".
  std::string full_name() const;

  // The full cgroup path, e.g. /sys/fs/cgroup/a/b/c.
  std::string path() const;

  Cgroup* parent() const { return parent_; }

  Cgroup* child(std::string_view name) EXCLUDES(mutex_);

  bool is_leaf() const EXCLUDES(mutex_);

  double cpu_max_fraction() const EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return cpu_max_fraction_;
  }

  int cpu_period_us() const EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return cpu_period_us_;
  }

  int cpu_weight() const EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return cpu_weight_;
  }

  std::string ToString() const EXCLUDES(mutex_);

  Cgroup& operator=(Cgroup&&) = delete;
  Cgroup& operator=(const Cgroup&) = delete;

 private:
  const std::string& LogPrefix() const { return log_prefix_; }

  Result<std::string> ReadConfig(std::string_view config, size_t max_length = 20) const;

  Status WriteConfig(std::string_view config, std::string_view value) const;

  mutable std::mutex mutex_;

  Cgroup* const parent_;
  const std::string name_;
  const std::string log_prefix_;

  UnorderedStringMap<std::string, Cgroup> children_ GUARDED_BY(mutex_);

  std::atomic<int> threads_fd_{-1};

  double cpu_max_fraction_ GUARDED_BY(mutex_) = 1.0;
  int cpu_period_us_ GUARDED_BY(mutex_) = kDefaultCpuPeriod;
  int cpu_weight_ GUARDED_BY(mutex_) = kDefaultCpuWeight;
};

// Setup cgroup management. If this is called, the following conditions are expected to be
// satisfied for the root cgroup (the cgroup (cgv1: for the cpu controller) that this process is
// in):
// 1. the current user must be able to create descendant cgroups
// 2. (cgv2) the cpu controller must be enabled
// 3. (cgv2) the current user must be able to write to cgroup.procs and cgroup.subtree_control
//
// Additionally, for cgroups v2, we require support for threaded cgroups (kernel 4.19+).
//
// This function must be called before any threads are created so that we can ensure all threads
// are created in the default thread cgroup.
Status SetupCgroupManagement(ClearChildCgroups clear);

bool CgroupManagementEnabled();

// Root cgroup that the process is under. No threads should be placed into this cgroup.
Cgroup* RootCgroup();

// Threads by default inherit the process cgroup (the root cgroup). But it's not desirable to have
// threads placed in internal cgroups that have child cgroups: the threads get assigned a weight
// based on their niceness and are treated as being in their own cgroup with that weight,
// which means the child cgroups we create get a very small percentage of the total weight when
// there are a lot of threads, i.e. each child cgroup should ideally have only other cgroups or
// threads as children, not both.
// ince our root cgroup contains other cgroups (for each thread pool), we need a sub-cgroup to
// place threads in initially, which is the DefaultThreadCgroup.
// SetupCgroupManagement will initially place threads into this cgroup, until they are moved out
// explicitly.
Cgroup* DefaultThreadCgroup();

// Override the default thread cgroup. Called by TServerCgroupManager after it sets up the
// hierarchy so that thread pools without an explicit cgroup assignment land in @system-med
// rather than the initial landing zone.
void SetDefaultThreadCgroup(Cgroup* cgroup);

Result<std::string> GetProcessCpuCgroup(int64_t process_id = -1, bool check_controllers = true);

Result<std::string> GetProcessCpuCgroupPath(int64_t process_id = -1, bool check_controllers = true);

Result<std::string> GetThreadCpuCgroup(int64_t thread_id = -1);

Status MoveProcessToCgroupPath(std::string_view cgroup_path);

// Returns the effective CPU count derived from this process's cgroup CPU quota:
// ceil(quota / period). Returns -1 if the cgroup has no CPU limit (e.g. cpu.max is "max",
// or cgroups v1 cfs_quota_us is -1).
Result<int> GetCgroupCpuQuota();

} // namespace yb

#endif // __linux__

namespace yb {

// Returns the effective number of CPUs available to the process. When --use_cgroups_cpu is set
// and a cgroup CPU quota is present, returns ceil(quota / period); otherwise returns
// base::NumCPUs(). The result is computed once and cached for the lifetime of the process.
//
// Prefer this over base::NumCPUs() in code outside gutil that sizes work to CPU count
// (thread pools, partition counts, etc.).
int NumEffectiveCPUs();

} // namespace yb
