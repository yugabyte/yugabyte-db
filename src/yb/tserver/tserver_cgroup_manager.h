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

#include <condition_variable>
#include <mutex>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "yb/common/pg_types.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/metrics_fwd.h"

namespace yb {

class Cgroup;
class MetricRegistry;
class Thread;
struct CgroupCpuStats;

template <typename T>
class AtomicGauge;

namespace tserver {

// This class is responsible for the cgroup hierarchy used by TServer threads and the corresponding
// Postgres processes. It is basically the translation layer from higher level "cgroup for X" to
// the actual cgroup hierarchy. For example users of this class may deal with things like
// "cgroup for database 1" or "cgroup for high priority threads", but how exactly that maps to a
// cgroup hierarchy is implementation details of this class.
class TServerCgroupManager {
 public:
  TServerCgroupManager();
  ~TServerCgroupManager();

  Result<Cgroup&> CgroupForDb(PgOid db_oid);

  // Store the database name for metric labels. Called from ts_tablet_manager
  // when a tablet provides the mapping. Name updates (e.g. after ALTER DATABASE
  // RENAME) are picked up on the next metrics collection cycle.
  void RegisterDbName(PgOid db_oid, std::string name);

  // System cgroups for shared/communal threads.
  // These are created once at startup and never destroyed.
  Cgroup* SystemHighCgroup() const { return system_high_cgroup_; }
  Cgroup* SystemMedCgroup() const { return system_med_cgroup_; }
  // Shared tenant CPU pool. @normal is a child of @capped-pool (along with @system-med).
  // @normal is uncapped within @capped-pool so it can use whatever @system-med doesn't.
  // Per-database cgroups and @default are children of @normal.
  Cgroup* NormalPoolCgroup() const { return normal_pool_cgroup_; }

  Status UpdateDbCpuLimits(double max_cpu_fraction, int period);

  // Recompute and apply CPU limits to all cgroups based on current gflag values.
  // Called on init and whenever any qos_* flag changes at runtime.
  Status ApplyCpuLimits();

  static Status MovePgBackendToCgroup(PgOid db_oid);

  Status Init();

  // Register cgroup metrics and start a background thread that periodically
  // reads cgroup CPU stats and updates the gauges exposed on /metrics and
  // /prometheus-metrics.
  Status RegisterMetrics(MetricRegistry* registry);

  void Shutdown();

  // Dump cgroups to HTML for the /cgroups endpoint. sample_interval_ms is the interval at which we
  // sample statistics (we sample twice in order to determine if a cgroup is currently throttled).
  void DumpCgroupsToHtml(std::ostream& out, uint64_t sample_interval_ms) const;

 private:
  struct CgroupMetrics {
    Cgroup* cgroup = nullptr;
    scoped_refptr<MetricEntity> entity;
    scoped_refptr<AtomicGauge<int64_t>> cpu_usage_ns;
    scoped_refptr<AtomicGauge<int64_t>> cpu_user_ns;
    scoped_refptr<AtomicGauge<int64_t>> cpu_sys_ns;
    scoped_refptr<AtomicGauge<int64_t>> nr_periods;
    scoped_refptr<AtomicGauge<int64_t>> nr_throttled;
    scoped_refptr<AtomicGauge<int64_t>> throttled_time_ns;
  };

  double ComputePerDbCpuFraction() const;
  // Returns the fraction of total machine CPU available for @capped-pool
  // (everything except @system-high). = (100% - qos_system_high_cpu_reserved_percent) / 100.
  double CappedPoolCpuFraction() const;

  void MetricsCollectorThread();
  CgroupMetrics CreateCgroupMetrics(
      Cgroup* cgroup, const std::string& name, MetricRegistry* registry,
      const MetricAttributeMap& extra_attrs = {});
  void UpdateCgroupMetrics(CgroupMetrics& m);

  std::mutex mutex_;
  std::unordered_map<PgOid, Cgroup&> db_cgroups_ GUARDED_BY(mutex_);
  std::unordered_map<PgOid, std::string> db_names_ GUARDED_BY(mutex_);

  Cgroup* system_high_cgroup_ = nullptr;
  Cgroup* capped_pool_cgroup_ = nullptr;   // parent of @system-med and @normal
  Cgroup* system_med_cgroup_ = nullptr;
  Cgroup* normal_pool_cgroup_ = nullptr;

  // Background metrics collector. shutdown_mutex_ + shutdown_cv_ enable responsive
  // shutdown: MetricsCollectorThread sleeps via cv.wait_for instead of SleepFor,
  // and Shutdown() notifies the cv so the thread wakes immediately.
  std::mutex shutdown_mutex_;
  std::condition_variable shutdown_cv_;
  bool shutdown_requested_ = false;  // only written under shutdown_mutex_, read in cv predicate
  scoped_refptr<Thread> metrics_thread_;
  MetricRegistry* metric_registry_ = nullptr;

  // Fixed cgroup metrics: system tiers plus @normal (shared tenant CPU pool). Per-DB cgroups are
  // children of @normal but use metric entity ids `db_$OID` (leaf name only).
  std::vector<CgroupMetrics> system_cgroup_metrics_;
  // Per-DB cgroup metrics (created dynamically). Only accessed from MetricsCollectorThread.
  std::unordered_map<PgOid, CgroupMetrics> db_cgroup_metrics_;
};

} // namespace tserver
} // namespace yb

#endif // __linux__

namespace yb::tserver {

bool TServerCgroupManagementEnabled();

} // namespace yb::tserver
