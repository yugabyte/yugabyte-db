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

#include <mutex>
#include <optional>
#include <unordered_map>

#include "yb/common/pg_types.h"

#include "yb/gutil/thread_annotations.h"

namespace yb {

class Cgroup;

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

 private:
  double ComputePerDbCpuFraction() const;
  // Returns the fraction of total machine CPU available for @capped-pool
  // (everything except @system-high). = (100% - qos_system_high_cpu_reserved_percent) / 100.
  double CappedPoolCpuFraction() const;

  std::mutex mutex_;
  std::unordered_map<PgOid, Cgroup&> db_cgroups_ GUARDED_BY(mutex_);

  Cgroup* system_high_cgroup_ = nullptr;
  Cgroup* capped_pool_cgroup_ = nullptr;   // parent of @system-med and @normal
  Cgroup* system_med_cgroup_ = nullptr;
  Cgroup* normal_pool_cgroup_ = nullptr;
};

} // namespace tserver
} // namespace yb

#endif // __linux__

namespace yb::tserver {

bool TServerCgroupManagementEnabled();

} // namespace yb::tserver
