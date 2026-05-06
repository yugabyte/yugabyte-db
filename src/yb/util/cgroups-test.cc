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
#ifdef __linux__
#include <sys/stat.h>

#include <gtest/gtest.h>

#include "yb/gutil/sysinfo.h"

#include "yb/util/cgroups.h"
#include "yb/util/errno.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

DECLARE_bool(use_cgroups_cpu);
DECLARE_int32(num_cpus);

using namespace std::literals;

namespace yb {

class CgroupsTest : public YBTest {
 public:
  void SetUp() override {
    google::SetVLOGLevel("cgroups", 5);
    if (auto status = CheckTestInCgroup(); !status.ok()) {
      LOG(FATAL) << "Test is not running inside dedicated cgroup: " << status;
    }
  }

  Status CheckTestInCgroup() {
    auto process_cgroup = VERIFY_RESULT(GetProcessCpuCgroup());
    if (process_cgroup == "/") {
      return STATUS(IllegalState, "Process CPU cgroup is root cgroup");
    }
    if (!process_cgroup.contains("/run-") || !process_cgroup.ends_with(".scope")) {
      return STATUS(IllegalState, "Not running in systemd scope");
    }
    return Status::OK();
  }

  void CheckThreadCgroup(Cgroup& cgroup, int64_t thread_id = -1) {
    auto actual_group = ASSERT_RESULT(GetThreadCpuCgroup(thread_id));
    LOG(INFO) << "Thread " << (thread_id == -1 ? Thread::CurrentThreadId() : thread_id)
              << " in cgroup: " << actual_group << " for CPU, checking against "
              << cgroup.full_name();
    ASSERT_EQ(actual_group, cgroup.full_name());
  }

  // Run and time a simple workload involving a fixed number of integer increments.
  int64_t TimeIncrementWorkload() {
    // volatile to avoid compiler optimizing out the increment.
    volatile size_t counter = 0;
    constexpr size_t iters = 5'000'000'000uz;

    MonoTime start = MonoTime::Now();
    for (auto i = 0uz; i < iters; ++i) {
      counter = counter + 1;
    }
    MonoTime end = MonoTime::Now();
    LOG(INFO) << AsString(start) << " to " << AsString(end) << ": " << (end - start);
    return (end - start).ToNanoseconds();
  }
};

TEST_F(CgroupsTest, TestSimple) {
  ASSERT_OK(SetupCgroupManagement(ClearChildCgroups::kTrue));
  ASSERT_NO_FATALS(CheckThreadCgroup(*DefaultThreadCgroup()));

  auto& child_group = ASSERT_RESULT_REF(RootCgroup()->CreateOrLoadChild("test"));

  std::mutex m;
  std::unique_lock lock(m);
  std::thread thread1([&] {
    ASSERT_NO_FATALS(CheckThreadCgroup(*DefaultThreadCgroup()));

    std::lock_guard lock(m);
    ASSERT_NO_FATALS(CheckThreadCgroup(*DefaultThreadCgroup()));
  });

  ASSERT_OK(child_group.MoveCurrentThreadToGroup());
  ASSERT_NO_FATALS(CheckThreadCgroup(child_group));

  auto& grandchild_group = ASSERT_RESULT_REF(child_group.CreateOrLoadChild("test"));
  ASSERT_OK(grandchild_group.MoveCurrentThreadToGroup());
  ASSERT_NO_FATALS(CheckThreadCgroup(grandchild_group));
  lock.unlock();
  thread1.join();
}

TEST_F(CgroupsTest, TestCleanup) {
  auto root_cgroup_path = ASSERT_RESULT(GetProcessCpuCgroupPath());
  const std::vector<std::string_view> garbage_cgroups = {
      "foo"sv, "foo/bar"sv, "baz"sv, "baz/foo"sv, "baz/foo/bar"sv, "baz/qux"sv};
  for (const auto& cgroup : garbage_cgroups) {
    auto path = Format("$0/$1", root_cgroup_path, cgroup);
    ASSERT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(
        Format("mkdir($0)", path), mkdir(path.c_str(), 0755)));
  }

  ASSERT_OK(SetupCgroupManagement(ClearChildCgroups::kTrue));
  for (const auto& cgroup : garbage_cgroups) {
    struct stat s;
    ASSERT_EQ(stat(Format("$0/$1", root_cgroup_path, cgroup).c_str(), &s), -1);
    ASSERT_EQ(errno, ENOENT);
  }
}

TEST_F(CgroupsTest, TestNoCleanup) {
  auto root_cgroup_path = ASSERT_RESULT(GetProcessCpuCgroupPath());
  const std::vector<std::string_view> garbage_cgroups = {
      "foo"sv, "foo/bar"sv, "baz"sv, "baz/foo"sv, "baz/foo/bar"sv, "baz/qux"sv};
  for (const auto& cgroup : garbage_cgroups) {
    auto path = Format("$0/$1", root_cgroup_path, cgroup);
    ASSERT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(
        Format("mkdir($0)", path), mkdir(path.c_str(), 0755)));
  }

  ASSERT_OK(SetupCgroupManagement(ClearChildCgroups::kFalse));
  for (const auto& cgroup : garbage_cgroups) {
    auto path = Format("$0/$1", root_cgroup_path, cgroup);
    struct stat s;
    ASSERT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(Format("stat($0)", path), stat(path.c_str(), &s)));
  }
}

TEST_F(CgroupsTest, TestCpuLimit) {
  auto base_time = TimeIncrementWorkload();
  LOG(INFO) << "Base time: " << base_time;

  ASSERT_OK(SetupCgroupManagement(ClearChildCgroups::kTrue));
  ASSERT_NO_FATALS(CheckThreadCgroup(*DefaultThreadCgroup()));

  auto& child_group = ASSERT_RESULT_REF(RootCgroup()->CreateOrLoadChild("child"));
  auto& throttled_group = ASSERT_RESULT_REF(child_group.CreateOrLoadChild("throttled"));
  ASSERT_OK(throttled_group.UpdateCpuLimits(
      /*max_cpu=*/0.5 / base::NumCPUs(), /*period_us=*/10'000));

  auto default_group_time = TimeIncrementWorkload();
  LOG(INFO) << "Unthrottled group time: " << default_group_time;
  ASSERT_NEAR(base_time / static_cast<double>(default_group_time), 1.0, 0.25);

  ASSERT_OK(throttled_group.MoveCurrentThreadToGroup());
  auto throttled_group_time = TimeIncrementWorkload();
  LOG(INFO) << "Throttled group time: " << throttled_group_time;
  ASSERT_NEAR(base_time / static_cast<double>(throttled_group_time), 0.5, 0.25);
}

// Standalone tests for NumEffectiveCPUs that don't require running inside a dedicated
// systemd cgroup scope.
class NumEffectiveCPUsTest : public YBTest {
 protected:
  void TearDown() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cgroups_cpu) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 0;
    YBTest::TearDown();
  }
};

TEST_F(NumEffectiveCPUsTest, FlagOffMatchesBase) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cgroups_cpu) = false;
  ASSERT_EQ(NumEffectiveCPUs(), base::NumCPUs());
}

TEST_F(NumEffectiveCPUsTest, PrintWithFlagOn) {
  // Used as an end-to-end validator: print NumEffectiveCPUs and the host count when
  // --use_cgroups_cpu is on. Run from inside a known cgroup (systemd-run scope, K8s pod,
  // etc.) and inspect the log output.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cgroups_cpu) = true;
  auto quota = GetCgroupCpuQuota();
  LOG(INFO) << "VALIDATE NumEffectiveCPUs=" << NumEffectiveCPUs()
            << " base::NumCPUs=" << base::NumCPUs()
            << " GetCgroupCpuQuota=" << (quota.ok() ? AsString(*quota) : quota.status().ToString());
}

TEST_F(NumEffectiveCPUsTest, NumCpusOverridesCgroup) {
  // --num_cpus is an explicit operator override and must win over --use_cgroups_cpu.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_cgroups_cpu) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 7;
  ASSERT_EQ(NumEffectiveCPUs(), 7);
}

TEST_F(NumEffectiveCPUsTest, GetCgroupCpuQuota) {
  // The helper must return cleanly regardless of whether a CPU quota is set on the
  // test process's cgroup. Either a non-negative effective CPU count or -1 for "no limit".
  auto quota = ASSERT_RESULT(GetCgroupCpuQuota());
  ASSERT_TRUE(quota == -1 || quota > 0) << "unexpected quota value: " << quota;
  LOG(INFO) << "Cgroup CPU quota for test process: " << quota
            << " (host CPUs: " << base::NumCPUs() << ")";
}

}  // namespace yb
#endif // __linux__
