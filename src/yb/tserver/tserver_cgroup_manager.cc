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

#include "yb/tserver/tserver_cgroup_manager.h"

#ifdef __linux__

#include "yb/util/cgroups.h"
#include "yb/util/flags.h"
#include "yb/util/flag_validators.h"

DEFINE_NON_RUNTIME_bool(enable_qos, false, "Enable the QoS feature.");

DEFINE_RUNTIME_double(qos_max_db_cpu_percent, 100.0,
    "Maximum per-database CPU percentage (100.0 = all cores, unbounded).");

DEFINE_RUNTIME_int32(qos_evaluation_window_us, yb::Cgroup::kDefaultCpuPeriod,
    "Period (in microseconds) for the CPU scheduler to use to determine whether a database "
    "has exceeded its limit.");
TAG_FLAG(qos_evaluation_window_us, advanced);

DEFINE_validator(qos_evaluation_window_us,
    // Linux requires cfs_period_us to be between 1ms and 1s.
    FLAG_RANGE_VALIDATOR(1'000, 1'000'000),
    FLAG_DELAYED_OK_VALIDATOR(yb::Cgroup::CheckMaxCpuValidForPeriod(
        FLAGS_qos_max_db_cpu_percent / 100.0, _value)));
DEFINE_validator(qos_max_db_cpu_percent,
    FLAG_RANGE_VALIDATOR(0.0, 100.0),
    FLAG_DELAYED_OK_VALIDATOR(yb::Cgroup::CheckMaxCpuValidForPeriod(
        _value / 100.0, FLAGS_qos_evaluation_window_us)));

namespace yb::tserver {

namespace {

class FlagUpdateCallbacks {
 public:
  void Register(TServerCgroupManager* manager) {
    std::lock_guard lock(mutex_);
    DCHECK(!cgroup_manager_);
    cgroup_manager_ = manager;
  }

  void Unregister([[maybe_unused]] TServerCgroupManager* manager) {
    std::lock_guard lock(mutex_);
    DCHECK(cgroup_manager_ == manager);
    cgroup_manager_ = nullptr;
  }

  void UpdateQosDbCpuLimits() {
    std::lock_guard lock(mutex_);
    // This can happen on startup, as well as if enable_qos = false.
    if (!cgroup_manager_) {
      VLOG(1) << "Cgroup manager not set, ignoring update to QoS gflags";
      return;
    }
    WARN_NOT_OK(
        cgroup_manager_->UpdateDbCpuLimits(
            FLAGS_qos_max_db_cpu_percent / 100.0, FLAGS_qos_evaluation_window_us),
        "Failed to update per-database CPU limits");
  }

 private:
  std::mutex mutex_;
  TServerCgroupManager* cgroup_manager_ GUARDED_BY(mutex_) = nullptr;
};

FlagUpdateCallbacks flag_update_callbacks;

REGISTER_CALLBACK(qos_max_db_cpu_percent, "qos_max_db_cpu_percent update callback",
    [] { flag_update_callbacks.UpdateQosDbCpuLimits(); });
REGISTER_CALLBACK(qos_evaluation_window_us, "qos_evaluation_window update callback",
    [] { flag_update_callbacks.UpdateQosDbCpuLimits(); });

Result<Cgroup&> SetupCgroupForDb(PgOid db_oid) {
  auto& cgroup =
      VERIFY_RESULT_REF(DCHECK_NOTNULL(RootCgroup())->CreateOrLoadChild(Format("db_$0", db_oid)));
  RETURN_NOT_OK(cgroup.UpdateCpuLimits(
      FLAGS_qos_max_db_cpu_percent / 100.0, FLAGS_qos_evaluation_window_us));
  return cgroup;
}

} // namespace

TServerCgroupManager::TServerCgroupManager() {
  flag_update_callbacks.Register(this);
}

TServerCgroupManager::~TServerCgroupManager() {
  flag_update_callbacks.Unregister(this);
}

Result<Cgroup&> TServerCgroupManager::CgroupForDb(PgOid db_oid) {
  // The Cgroup hierarchy we set up is:
  // $ROOT_CGROUP
  //   - @default (all uncategorized threads - DefaultThreadCgroup())
  //   - db_16384 (all threads marked as work for DB 16384)
  //   - db_16385
  //   - ...
  //   - db_$DBOID
  // with all the db groups being lazily created when CgroupForDb() is called.
  std::lock_guard lock(mutex_);
  auto iter = db_cgroups_.find(db_oid);
  if (iter == db_cgroups_.end()) {
    iter = db_cgroups_.try_emplace(db_oid, VERIFY_RESULT_REF(SetupCgroupForDb(db_oid))).first;
  }
  return iter->second;
}

Status TServerCgroupManager::UpdateDbCpuLimits(double max_cpu, int period) {
  std::lock_guard lock(mutex_);
  for (auto& cgroup : db_cgroups_ | std::views::values) {
    RETURN_NOT_OK(cgroup.UpdateCpuLimits(max_cpu, period));
  }
  return Status::OK();
}

} // namespace yb::tserver

#endif // __linux__

namespace yb::tserver {

bool TServerCgroupManagementEnabled() {
#ifdef __linux__
  return FLAGS_enable_qos;
#else
  return false;
#endif
}

} // namespace yb::tserver
