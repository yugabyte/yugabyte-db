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

#include <dirent.h>
#include <fstream>
#include <set>

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server-test-base.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_cgroup_manager.h"

#include "yb/gutil/sysinfo.h"

#include "yb/util/cgroups.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

DECLARE_bool(enable_qos);
DECLARE_double(qos_max_db_cpu_percent);
DECLARE_int32(qos_evaluation_window_us);
DECLARE_int32(qos_metrics_interval_sec);

METRIC_DECLARE_entity(cgroup);
METRIC_DECLARE_gauge_int64(cgroup_cpu_usage_ns);
METRIC_DECLARE_gauge_int64(cgroup_cpu_user_ns);
METRIC_DECLARE_gauge_int64(cgroup_cpu_sys_ns);
METRIC_DECLARE_gauge_int64(cgroup_nr_periods);
METRIC_DECLARE_gauge_int64(cgroup_nr_throttled);
METRIC_DECLARE_gauge_int64(cgroup_throttled_time_ns);

namespace yb::tserver {

// Must run before TServerCgroupManagerTest which initializes the global cgroup manager singleton.
class MovePgBackendWithoutCgroupInitTest : public YBTest {};

TEST_F(MovePgBackendWithoutCgroupInitTest, ReturnsErrorWhenCgroupsNotInitialized) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_qos) = true;
  ASSERT_FALSE(CgroupManagementEnabled());
  auto status = TServerCgroupManager::MovePgBackendToCgroup(12345);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Cgroup management not initialized");
}

class TServerCgroupManagerTest : public TabletServerTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_qos) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_qos_metrics_interval_sec) = 1;
    ASSERT_OK(SetupCgroupManagement(ClearChildCgroups::kTrue));

    TabletServerTestBase::SetUp();
    StartTabletServer();
    auto& server = *mini_server_->server();
    manager_ = CHECK_NOTNULL(server.cgroup_manager());
    registry_ = server.metric_registry();
  }

  scoped_refptr<MetricEntity> FindCgroupEntity(const std::string& name) {
    return registry_->FindOrCreateEntity(&METRIC_ENTITY_cgroup, name, {});
  }

  struct ThreadCgroupInfo {
    int64_t tid;
    std::string comm;
    std::string cgroup;
  };

  // Reads /proc/self/task/<tid>/comm for each thread in the process and
  // returns threads whose comm starts with one of the given prefixes, along
  // with their actual kernel cgroup (via GetThreadCpuCgroup).
  Result<std::vector<ThreadCgroupInfo>> GetPoolThreadCgroups(
      const std::vector<std::string>& pool_prefixes) {
    std::string root_name = RootCgroup()->full_name();
    std::vector<ThreadCgroupInfo> results;

    auto dir_closer = [](DIR* d) { if (d) closedir(d); };
    std::unique_ptr<DIR, decltype(dir_closer)> task_dir(
        opendir("/proc/self/task"), dir_closer);
    SCHECK(task_dir, IOError, "Failed to open /proc/self/task");

    struct dirent* entry;
    while ((entry = readdir(task_dir.get())) != nullptr) {
      if (entry->d_name[0] == '.') continue;
      int64_t tid = std::strtoll(entry->d_name, nullptr, 10);
      if (tid <= 0) continue;

      std::string comm_path = Format("/proc/self/task/$0/comm", tid);
      std::ifstream comm_file(comm_path);
      if (!comm_file.is_open()) continue;

      std::string comm;
      std::getline(comm_file, comm);

      for (const auto& prefix : pool_prefixes) {
        if (comm.substr(0, prefix.size()) == prefix) {
          auto cgroup_result = GetThreadCpuCgroup(tid);
          if (!cgroup_result.ok()) break;
          std::string relative = cgroup_result->substr(root_name.size());
          results.push_back({tid, comm, relative});
          break;
        }
      }
    }
    return results;
  }

  TServerCgroupManager* manager_;
  MetricRegistry* registry_;
};

TEST_F(TServerCgroupManagerTest, TestDbCpuLimits) {
  constexpr PgOid db1_oid = 1234;
  constexpr PgOid db2_oid = 5678;
  constexpr PgOid db3_oid = 4321;

  ASSERT_OK(SET_FLAG(qos_max_db_cpu_percent, 100.0));

  auto& cgroup1 = ASSERT_RESULT_REF(manager_->CgroupForDb(db1_oid));
  auto& cgroup2 = ASSERT_RESULT_REF(manager_->CgroupForDb(db2_oid));

  // SET_FLAG calls validators.
  ASSERT_OK(SET_FLAG(qos_max_db_cpu_percent, 50.0));
  ASSERT_DOUBLE_EQ(cgroup1.cpu_max_fraction(), 0.5);
  ASSERT_DOUBLE_EQ(cgroup2.cpu_max_fraction(), 0.5);

  ASSERT_OK(SET_FLAG(qos_evaluation_window_us, 300'000));
  ASSERT_DOUBLE_EQ(cgroup1.cpu_max_fraction(), 0.5);
  ASSERT_DOUBLE_EQ(cgroup2.cpu_max_fraction(), 0.5);
  ASSERT_EQ(cgroup1.cpu_period_us(), 300'000);
  ASSERT_EQ(cgroup2.cpu_period_us(), 300'000);

  auto& cgroup3 = ASSERT_RESULT_REF(manager_->CgroupForDb(db3_oid));
  ASSERT_DOUBLE_EQ(cgroup3.cpu_max_fraction(), 0.5);
  ASSERT_EQ(cgroup3.cpu_period_us(), 300'000);
}

TEST_F(TServerCgroupManagerTest, TestBadDbCpuLimits) {
  const auto num_cpu = base::NumCPUs();

  ASSERT_OK(SET_FLAG(qos_evaluation_window_us, 2'000));

  ASSERT_NOK(SET_FLAG(qos_max_db_cpu_percent, -1.0));
  ASSERT_NOK(SET_FLAG(qos_max_db_cpu_percent, 101.0));
  // Must be at least 50% / num_cpu, since minimum quota is 1ms and period is 2ms.
  ASSERT_NOK(SET_FLAG(qos_max_db_cpu_percent, 49.9 / num_cpu));
  ASSERT_OK(SET_FLAG(qos_max_db_cpu_percent, 50.0 / num_cpu));

  ASSERT_NOK(SET_FLAG(qos_evaluation_window_us, 999));
  ASSERT_NOK(SET_FLAG(qos_evaluation_window_us, 1'000'001));
  // Must be at least 1999, since minimum quota is 1ms and quota is 50% / num_cpu
  // (and because we round 999.5 to 1000).
  ASSERT_NOK(SET_FLAG(qos_evaluation_window_us, 1998));
  ASSERT_OK(SET_FLAG(qos_evaluation_window_us, 1999));
}

// Verifies that thread pool worker threads land in the correct cgroups after
// the tablet server starts. This catches the bug where workers that start before
// SetCgroup() would cache a stale pool cgroup and revert to /@default.
TEST_F(TServerCgroupManagerTest, TestPoolCgroupAssignment) {
  // Wait for the tablet to be fully running (consensus elected) so that pool
  // worker threads have processed at least one task and MoveToPoolCgroup() has run.
  ASSERT_OK(WaitForTabletRunning(kTabletId));

  // Pool name prefix -> expected cgroup path relative to the root cgroup.
  // We test pools that have min_threads=1, guaranteeing at least one worker.
  struct PoolExpectation {
    std::string prefix;
    std::string expected_cgroup;
  };
  std::vector<PoolExpectation> expectations = {
      // system-high pools (consensus/WAL path).
      {"consensus_",  "/@system-high"},
      {"log-sync_",   "/@system-high"},
      {"prepare_",    "/@system-high"},
      {"append_",     "/@system-high"},
      {"log-alloc_",  "/@system-high"},
      // system-med pools (background work, always assigned).
      {"flush-retry",  "/@capped-pool/@system-med"},
      {"wait-queue_",  "/@capped-pool/@system-med"},
  };

  std::vector<std::string> prefixes;
  prefixes.reserve(expectations.size());
  for (const auto& e : expectations) {
    prefixes.push_back(e.prefix);
  }

  auto threads = ASSERT_RESULT(GetPoolThreadCgroups(prefixes));
  ASSERT_GT(threads.size(), 0) << "No pool worker threads found";

  // Build a set of which pool prefixes we found at least one thread for.
  std::set<std::string> found_prefixes;

  for (const auto& t : threads) {
    LOG(INFO) << "Thread tid=" << t.tid << " comm=" << t.comm
              << " cgroup=" << t.cgroup;

    for (const auto& e : expectations) {
      if (t.comm.substr(0, e.prefix.size()) == e.prefix) {
        EXPECT_EQ(t.cgroup, e.expected_cgroup)
            << "Thread " << t.comm << " (tid " << t.tid
            << ") expected in " << e.expected_cgroup
            << " but found in " << t.cgroup;
        found_prefixes.insert(e.prefix);
        break;
      }
    }
  }

  // Verify we found at least one thread for each pool with min_threads=1.
  for (const auto& e : expectations) {
    EXPECT_TRUE(found_prefixes.count(e.prefix))
        << "No worker thread found for pool prefix: " << e.prefix;
  }
}

TEST_F(TServerCgroupManagerTest, TestCgroupMetricsForSystemCgroups) {
  // The metrics collector thread runs every qos_metrics_interval_sec (set to 1s in SetUp).
  // Wait for at least one collection cycle to complete.
  auto system_high_entity = FindCgroupEntity("@system-high");
  auto capped_pool_entity = FindCgroupEntity("@capped-pool");
  auto system_med_entity = FindCgroupEntity("@system-med");
  auto normal_entity = FindCgroupEntity("@normal");
  auto default_entity = FindCgroupEntity("@default");

  auto usage_ns = METRIC_cgroup_cpu_usage_ns.Instantiate(system_high_entity, 0);
  auto user_ns = METRIC_cgroup_cpu_user_ns.Instantiate(system_high_entity, 0);
  auto sys_ns = METRIC_cgroup_cpu_sys_ns.Instantiate(system_high_entity, 0);
  auto nr_periods = METRIC_cgroup_nr_periods.Instantiate(system_high_entity, 0);
  auto nr_throttled = METRIC_cgroup_nr_throttled.Instantiate(system_high_entity, 0);
  auto throttled_ns = METRIC_cgroup_throttled_time_ns.Instantiate(system_high_entity, 0);

  auto pool_usage = METRIC_cgroup_cpu_usage_ns.Instantiate(capped_pool_entity, 0);
  auto med_usage = METRIC_cgroup_cpu_usage_ns.Instantiate(system_med_entity, 0);
  auto normal_usage = METRIC_cgroup_cpu_usage_ns.Instantiate(normal_entity, 0);
  auto def_usage = METRIC_cgroup_cpu_usage_ns.Instantiate(default_entity, 0);

  // Wait until the background thread has populated values.
  ASSERT_EVENTUALLY([&] {
    ASSERT_GE(usage_ns->value(), 0);
    ASSERT_GE(user_ns->value(), 0);
    ASSERT_GE(sys_ns->value(), 0);
    ASSERT_GE(nr_periods->value(), 0);
    ASSERT_GE(nr_throttled->value(), 0);
    ASSERT_GE(throttled_ns->value(), 0);

    ASSERT_GE(pool_usage->value(), 0);
    ASSERT_GE(med_usage->value(), 0);
    ASSERT_GE(normal_usage->value(), 0);
    ASSERT_GE(def_usage->value(), 0);
  });
}

TEST_F(TServerCgroupManagerTest, TestCgroupMetricsForDbCgroup) {
  constexpr PgOid test_db_oid = 99999;
  const std::string test_db_name = "test_database";

  // Register a per-DB cgroup and supply the database name.
  ASSERT_OK(manager_->CgroupForDb(test_db_oid));
  manager_->RegisterDbName(test_db_oid, test_db_name);

  // Retrieve the entity once (FindOrCreateEntity resets attributes on existing entities,
  // so we must not call it repeatedly inside ASSERT_EVENTUALLY).
  auto entity = FindCgroupEntity(Format("db_$0", test_db_oid));

  // Wait for the metrics collector to populate stats and attributes.
  ASSERT_EVENTUALLY([&] {
    auto usage = METRIC_cgroup_cpu_usage_ns.Instantiate(entity, 0);
    ASSERT_GE(usage->value(), 0);
    auto nr_periods = METRIC_cgroup_nr_periods.Instantiate(entity, 0);
    ASSERT_GE(nr_periods->value(), 0);
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_name")), test_db_name);
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_oid")),
              std::to_string(test_db_oid));
  });
}

TEST_F(TServerCgroupManagerTest, TestCgroupMetricsAfterDbRename) {
  constexpr PgOid db_oid = 88888;
  const std::string original_name = "original_db";
  const std::string renamed_name = "renamed_db";

  // Create cgroup and register with original name.
  ASSERT_OK(manager_->CgroupForDb(db_oid));
  manager_->RegisterDbName(db_oid, original_name);

  // Get entity once (FindOrCreateEntity resets attrs on existing entities).
  auto entity = FindCgroupEntity(Format("db_$0", db_oid));

  // Wait for the metric entity to appear with the original name.
  ASSERT_EVENTUALLY([&] {
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_name")), original_name);
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_oid")),
              std::to_string(db_oid));
  });

  // Simulate a database rename. In production this arrives via ts_tablet_manager
  // the next time a TabletPeer for this DB is initialized.
  manager_->RegisterDbName(db_oid, renamed_name);

  // The background thread should pick up the new name on its next cycle.
  ASSERT_EVENTUALLY([&] {
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_name")), renamed_name);
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_oid")),
              std::to_string(db_oid));
  });
}

TEST_F(TServerCgroupManagerTest, TestCgroupMetricsLateMetadata) {
  // Simulates the pg_client_session / PerDbCgroupProvider code path where the cgroup is
  // created without a database name.  The name arrives later via ts_tablet_manager when a
  // tablet for that database is opened.
  constexpr PgOid db_oid = 77777;
  const std::string db_name = "late_arriving_name";

  // Step 1: Cgroup created with no name (like pg_client_session path).
  ASSERT_OK(manager_->CgroupForDb(db_oid));

  // Get entity once (FindOrCreateEntity resets attrs on existing entities).
  auto entity = FindCgroupEntity(Format("db_$0", db_oid));

  // Wait for the metric entity to appear -- it should have database_oid
  // but no database_name yet.
  ASSERT_EVENTUALLY([&] {
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_oid")),
              std::to_string(db_oid));
    auto name_result = entity->TEST_GetAttributeFromMap("database_name");
    ASSERT_TRUE(!name_result.ok() || name_result->empty());
  });

  // Step 2: Later, ts_tablet_manager opens a tablet and supplies the database name.
  manager_->RegisterDbName(db_oid, db_name);

  // The background thread should pick up the late-arriving name.
  ASSERT_EVENTUALLY([&] {
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_name")), db_name);
    ASSERT_EQ(ASSERT_RESULT(entity->TEST_GetAttributeFromMap("database_oid")),
              std::to_string(db_oid));
  });
}

} // namespace yb::tserver

#endif // __linux__
