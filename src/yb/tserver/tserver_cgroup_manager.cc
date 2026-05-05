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

#include <algorithm>
#include <chrono>
#include <thread>

#include "yb/gutil/sysinfo.h"

#include "yb/util/cgroups.h"
#include "yb/util/flags.h"
#include "yb/util/flag_validators.h"
#include "yb/util/html_print_helper.h"
#include "yb/util/metrics.h"
#include "yb/util/thread.h"
#include "yb/util/url-coding.h"

DECLARE_bool(enable_qos);

DEFINE_RUNTIME_double(qos_max_db_cpu_percent, 100.0,
    "Maximum per-database CPU as a percentage of the @capped-pool budget "
    "(100% - qos_system_high_cpu_reserved_percent). @normal (the tenant pool) is uncapped within "
    "@capped-pool, so databases can use whatever @system-med doesn't consume. 100.0 means a single "
    "database may burst to the full @capped-pool budget when no other tenant or @system-med work "
    "is contending. Values below 100 cap each database at that fraction even when peers are idle.");

DEFINE_RUNTIME_int32(qos_evaluation_window_us, yb::Cgroup::kDefaultCpuPeriod,
    "Period (in microseconds) for the CPU scheduler to use to determine whether a cgroup "
    "has exceeded its limit.");
TAG_FLAG(qos_evaluation_window_us, advanced);

DEFINE_RUNTIME_double(qos_system_high_cpu_reserved_percent, 0,
    "CPU reserved for @system-high, not available for other work even if unused. "
    "Subtracted from @capped-pool's cap: @capped-pool = 100% - this value. "
    "Does NOT cap @system-high itself (see qos_system_high_cpu_max_percent). "
    "0 means no reservation.");
TAG_FLAG(qos_system_high_cpu_reserved_percent, advanced);

DEFINE_RUNTIME_double(qos_system_high_cpu_max_percent, 100.0,
    "Hard cap on CPU for the @system-high cgroup (reactors, consensus, WAL). "
    "100.0 = uncapped (all cores). Lower values restrict @system-high's maximum CPU usage. "
    "Independent of qos_system_high_cpu_reserved_percent, which controls @capped-pool's cap.");
TAG_FLAG(qos_system_high_cpu_max_percent, advanced);

DEFINE_RUNTIME_int32(qos_capped_pool_cpu_weight, yb::Cgroup::kDefaultCpuWeight,
    "CPU scheduling weight for @capped-pool. @system-high uses the default weight ("
    "100). Under contention, CPU splits proportionally: e.g. weight=400 gives "
    "@capped-pool 4x the CPU share of @system-high. "
    "Default = equal weighting. Range: [1, 10000].");
TAG_FLAG(qos_capped_pool_cpu_weight, advanced);

DEFINE_RUNTIME_double(qos_system_med_cpu_max_percent, 0,
    "Hard cap on CPU for @system-med as a percentage of the @capped-pool budget "
    "(100% - qos_system_high_cpu_reserved_percent). "
    "0 means uncapped within @capped-pool. 100.0 = the full @capped-pool budget.");
TAG_FLAG(qos_system_med_cpu_max_percent, advanced);

DEFINE_RUNTIME_bool(qos_compaction_per_db_cgroups, false,
    "When true, compaction/flush tasks run in the tablet's per-database cgroup (db_$DBOID) "
    "instead of the shared @system-med cgroup.");
TAG_FLAG(qos_compaction_per_db_cgroups, advanced);

DEFINE_RUNTIME_bool(qos_consensus_per_db_cgroups, false,
    "EXPERIMENTAL. When true, consensus/WAL tasks (raft, append, log-sync, prepare) run in "
    "the tablet's per-database cgroup (db_$DBOID) instead of the shared @system-high cgroup. "
    "The current implementation uses per-task cgroup switching on shared thread pools, which "
    "incurs frequent MoveCurrentThreadToGroup() syscalls and may degrade kernel CFS scheduler "
    "accuracy. A follow-up diff (GH #31010) will replace this with dedicated per-DB thread pools "
    "(TaggedThreadPools).");
TAG_FLAG(qos_consensus_per_db_cgroups, hidden);

DEFINE_validator(qos_evaluation_window_us,
    // Linux requires cfs_period_us to be between 1ms and 1s.
    FLAG_RANGE_VALIDATOR(1'000, 1'000'000),
    FLAG_DELAYED_OK_VALIDATOR(yb::Cgroup::CheckMaxCpuValidForPeriod(
        FLAGS_qos_max_db_cpu_percent / 100.0, _value)));
DEFINE_validator(qos_max_db_cpu_percent,
    FLAG_RANGE_VALIDATOR(0.0, 100.0),
    FLAG_DELAYED_OK_VALIDATOR(yb::Cgroup::CheckMaxCpuValidForPeriod(
        _value / 100.0, FLAGS_qos_evaluation_window_us)));
DEFINE_validator(qos_system_high_cpu_reserved_percent,
    FLAG_RANGE_VALIDATOR(0.0, 100.0));
DEFINE_validator(qos_system_high_cpu_max_percent,
    FLAG_RANGE_VALIDATOR(0.0, 100.0));
DEFINE_validator(qos_capped_pool_cpu_weight,
    FLAG_RANGE_VALIDATOR(1, 10000));
DEFINE_validator(qos_system_med_cpu_max_percent,
    FLAG_RANGE_VALIDATOR(0.0, 100.0));

DEFINE_RUNTIME_int32(qos_metrics_interval_sec, 2,
    "How often (seconds) to sample cgroup CPU stats for metrics. "
    "0 disables cgroup metrics collection.");

METRIC_DEFINE_entity(cgroup);

METRIC_DEFINE_gauge_int64(cgroup, cgroup_cpu_usage_ns,
    "Cgroup CPU Usage (ns)", yb::MetricUnit::kNanoseconds,
    "Cumulative CPU time (ns) consumed by this cgroup.",
    yb::EXPOSE_AS_COUNTER);
METRIC_DEFINE_gauge_int64(cgroup, cgroup_cpu_user_ns,
    "Cgroup User CPU (ns)", yb::MetricUnit::kNanoseconds,
    "Cumulative user-mode CPU time (ns) for this cgroup.",
    yb::EXPOSE_AS_COUNTER);
METRIC_DEFINE_gauge_int64(cgroup, cgroup_cpu_sys_ns,
    "Cgroup Kernel CPU (ns)", yb::MetricUnit::kNanoseconds,
    "Cumulative kernel-mode CPU time (ns) for this cgroup.",
    yb::EXPOSE_AS_COUNTER);
METRIC_DEFINE_gauge_int64(cgroup, cgroup_nr_periods,
    "Cgroup CFS Periods", yb::MetricUnit::kUnits,
    "Cumulative CFS scheduling periods for this cgroup.",
    yb::EXPOSE_AS_COUNTER);
METRIC_DEFINE_gauge_int64(cgroup, cgroup_nr_throttled,
    "Cgroup Throttled Periods", yb::MetricUnit::kUnits,
    "Cumulative CFS periods where this cgroup was throttled.",
    yb::EXPOSE_AS_COUNTER);
METRIC_DEFINE_gauge_int64(cgroup, cgroup_throttled_time_ns,
    "Cgroup Throttled Time (ns)", yb::MetricUnit::kNanoseconds,
    "Cumulative time (ns) threads in this cgroup were throttled by CFS.",
    yb::EXPOSE_AS_COUNTER);

using namespace std::chrono_literals;

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

  void ApplyLimits() {
    std::lock_guard lock(mutex_);
    if (!cgroup_manager_) {
      VLOG(1) << "Cgroup manager not set, ignoring update to QoS gflags";
      return;
    }
    WARN_NOT_OK(cgroup_manager_->ApplyCpuLimits(), "Failed to apply cgroup CPU limits");
  }

 private:
  std::mutex mutex_;
  TServerCgroupManager* cgroup_manager_ GUARDED_BY(mutex_) = nullptr;
};

FlagUpdateCallbacks flag_update_callbacks;

void ApplyQosCpuLimits() { flag_update_callbacks.ApplyLimits(); }

REGISTER_CALLBACK(qos_max_db_cpu_percent, "qos cpu limit update", ApplyQosCpuLimits);
REGISTER_CALLBACK(qos_evaluation_window_us, "qos cpu limit update", ApplyQosCpuLimits);
REGISTER_CALLBACK(qos_system_high_cpu_reserved_percent, "qos cpu limit update", ApplyQosCpuLimits);
REGISTER_CALLBACK(qos_system_high_cpu_max_percent, "qos cpu limit update", ApplyQosCpuLimits);
REGISTER_CALLBACK(qos_capped_pool_cpu_weight, "qos cpu limit update", ApplyQosCpuLimits);
REGISTER_CALLBACK(qos_system_med_cpu_max_percent, "qos cpu limit update", ApplyQosCpuLimits);

Result<Cgroup&> GetOrCreateDbCgroup(PgOid db_oid) {
  auto* root = DCHECK_NOTNULL(RootCgroup());
  auto& capped_pool = VERIFY_RESULT_REF(root->CreateOrLoadChild("@capped-pool"));
  auto& normal = VERIFY_RESULT_REF(capped_pool.CreateOrLoadChild("@normal"));
  return VERIFY_RESULT_REF(normal.CreateOrLoadChild(Format("db_$0", db_oid)));
}

Result<Cgroup&> SetupCgroupForDb(PgOid db_oid, double per_db_cpu_fraction) {
  auto& cgroup = VERIFY_RESULT_REF(GetOrCreateDbCgroup(db_oid));
  RETURN_NOT_OK(cgroup.UpdateCpuLimits(per_db_cpu_fraction, FLAGS_qos_evaluation_window_us));
  return cgroup;
}

std::string FormatNanoseconds(int64_t ns) {
  return StringPrintf("%.3f", static_cast<double>(ns) / 1e9);
}

} // namespace

TServerCgroupManager::TServerCgroupManager() {
  flag_update_callbacks.Register(this);
}

TServerCgroupManager::~TServerCgroupManager() {
  Shutdown();
  flag_update_callbacks.Unregister(this);
}

void TServerCgroupManager::Shutdown() {
  if (metrics_thread_) {
    {
      std::lock_guard lock(shutdown_mutex_);
      shutdown_requested_ = true;
    }
    shutdown_cv_.notify_one();
    metrics_thread_->Join();
    metrics_thread_ = nullptr;
  }
}

Status TServerCgroupManager::Init() {
  // The Cgroup hierarchy:
  // $ROOT_CGROUP
  //   - @system-high  (latency-sensitive: reactors, consensus, WAL, network)
  //   - @capped-pool  (cap = 100% - reserved%; contains tenant and background work)
  //     - @system-med  (hard cap -- background: compaction, flushes, maintenance)
  //     - @normal      (uncapped within @capped-pool; groups tenant work so that
  //                     @system-med's weight share stays constant as DBs come and go)
  //       - @default   (uncategorized threads; uncapped within @normal)
  //       - db_$DBOID  (per-database query processing threads; individually capped)
  //
  // Protection model (configured via gflags, both mechanisms compose):
  //   Cap:    qos_system_high_cpu_max_percent < 100 => hard ceiling on @system-high.
  //   Weight: qos_capped_pool_cpu_weight vs @system-high (default weight)
  //           => proportional CPU split under contention.
  // @system-med and @normal share @capped-pool's budget: if @system-med is idle,
  // @normal (and its per-DB children) can use the full @capped-pool budget.
  auto* root = DCHECK_NOTNULL(RootCgroup());
  system_high_cgroup_ = &VERIFY_RESULT_REF(root->CreateOrLoadChild("@system-high"));
  capped_pool_cgroup_ = &VERIFY_RESULT_REF(root->CreateOrLoadChild("@capped-pool"));
  system_med_cgroup_ = &VERIFY_RESULT_REF(capped_pool_cgroup_->CreateOrLoadChild("@system-med"));
  normal_pool_cgroup_ = &VERIFY_RESULT_REF(capped_pool_cgroup_->CreateOrLoadChild("@normal"));

  // Move the infrastructure-created @default under @normal.
  // SetupCgroupManagement() creates @default under $ROOT as a landing zone; we create the
  // "real" @default under @normal and move threads there.
  default_cgroup_ = &VERIFY_RESULT_REF(normal_pool_cgroup_->CreateOrLoadChild("@default"));
  RETURN_NOT_OK(default_cgroup_->MoveCurrentThreadToGroup());

  return ApplyCpuLimits();
}

double TServerCgroupManager::CappedPoolCpuFraction() const {
  return std::max(0.0, (100.0 - FLAGS_qos_system_high_cpu_reserved_percent) / 100.0);
}

double TServerCgroupManager::ComputePerDbCpuFraction() const {
  return CappedPoolCpuFraction() * (FLAGS_qos_max_db_cpu_percent / 100.0);
}

Status TServerCgroupManager::ApplyCpuLimits() {
  auto period = FLAGS_qos_evaluation_window_us;

  // @system-high: default 100% = uncapped (all cores). Lower values impose a hard cap.
  double sys_high_max = FLAGS_qos_system_high_cpu_max_percent / 100.0;
  RETURN_NOT_OK(system_high_cgroup_->UpdateCpuLimits(sys_high_max, period));

  // @system-high keeps the default weight; @capped-pool's weight is configurable.
  RETURN_NOT_OK(capped_pool_cgroup_->UpdateCpuWeight(FLAGS_qos_capped_pool_cpu_weight));

  // @capped-pool cap = 100% - reserved%. This is a ceiling, not a guarantee.
  // The weight above provides the soft guarantee under contention.
  double capped_pool_fraction = CappedPoolCpuFraction();
  if (capped_pool_fraction > 0.0) {
    RETURN_NOT_OK(capped_pool_cgroup_->UpdateCpuLimits(capped_pool_fraction, period));
  } else {
    LOG(WARNING) << "system-high reservation is 100%, no CPU budget left for @capped-pool; "
                 << "applying minimum valid cgroup CPU quota.";
    const double min_cpu_fraction =
        1000.0 / (base::NumCPUs() * static_cast<double>(period));
    RETURN_NOT_OK(capped_pool_cgroup_->UpdateCpuLimits(min_cpu_fraction, period));
  }

  // @system-med is hard-capped relative to @capped-pool's budget.
  if (FLAGS_qos_system_med_cpu_max_percent > 0) {
    double sys_med_fraction =
        FLAGS_qos_system_med_cpu_max_percent / 100.0 * capped_pool_fraction;
    RETURN_NOT_OK(system_med_cgroup_->UpdateCpuLimits(sys_med_fraction, period));
  } else {
    RETURN_NOT_OK(system_med_cgroup_->UpdateCpuLimits(1.0, period));
  }

  double per_db_fraction = ComputePerDbCpuFraction();
  LOG(INFO) << "QoS CPU budget: system-high cap="
            << FLAGS_qos_system_high_cpu_max_percent << "%"
            << ", reserved=" << FLAGS_qos_system_high_cpu_reserved_percent << "%"
            << ", @capped-pool=" << (capped_pool_fraction * 100.0) << "%"
            << " weight=" << FLAGS_qos_capped_pool_cpu_weight
            << " (system-med max=" << FLAGS_qos_system_med_cpu_max_percent
            << "% of pool, @normal uncapped within pool, includes @default)"
            << ", per-db cap=" << (per_db_fraction * 100.0)
            << "% (qos_max_db_cpu_percent=" << FLAGS_qos_max_db_cpu_percent
            << "% of @capped-pool)";

  return UpdateDbCpuLimits(per_db_fraction, period);
}

Result<Cgroup&> TServerCgroupManager::CgroupForDb(PgOid db_oid) {
  std::lock_guard lock(mutex_);
  auto iter = db_cgroups_.find(db_oid);
  if (iter == db_cgroups_.end()) {
    iter = db_cgroups_.try_emplace(
        db_oid,
        VERIFY_RESULT_REF(SetupCgroupForDb(db_oid, ComputePerDbCpuFraction()))).first;
  }
  return iter->second;
}

void TServerCgroupManager::RegisterDbName(PgOid db_oid, std::string name) {
  std::lock_guard lock(mutex_);
  db_names_[db_oid] = std::move(name);
}

Status TServerCgroupManager::UpdateDbCpuLimits(double max_cpu, int period) {
  std::lock_guard lock(mutex_);
  for (auto& cgroup : db_cgroups_ | std::views::values) {
    RETURN_NOT_OK(cgroup.UpdateCpuLimits(max_cpu, period));
  }
  return Status::OK();
}

TServerCgroupManager::CgroupMetrics TServerCgroupManager::CreateCgroupMetrics(
    Cgroup* cgroup, const std::string& name, MetricRegistry* registry,
    const MetricAttributeMap& extra_attrs) {
  CgroupMetrics m;
  m.cgroup = cgroup;
  MetricEntity::AttributeMap attrs;
  attrs["cgroup_name"] = name;
  for (const auto& [k, v] : extra_attrs) {
    attrs[k] = v;
  }
  m.entity = METRIC_ENTITY_cgroup.Instantiate(registry, name, attrs);
  m.cpu_usage_ns = METRIC_cgroup_cpu_usage_ns.Instantiate(m.entity, 0);
  m.cpu_user_ns = METRIC_cgroup_cpu_user_ns.Instantiate(m.entity, 0);
  m.cpu_sys_ns = METRIC_cgroup_cpu_sys_ns.Instantiate(m.entity, 0);
  m.nr_periods = METRIC_cgroup_nr_periods.Instantiate(m.entity, 0);
  m.nr_throttled = METRIC_cgroup_nr_throttled.Instantiate(m.entity, 0);
  m.throttled_time_ns = METRIC_cgroup_throttled_time_ns.Instantiate(m.entity, 0);
  return m;
}

Status TServerCgroupManager::RegisterMetrics(MetricRegistry* registry) {
  metric_registry_ = DCHECK_NOTNULL(registry);

  if (system_high_cgroup_) {
    system_cgroup_metrics_.push_back(
        CreateCgroupMetrics(system_high_cgroup_, "@system-high", registry));
  }
  if (capped_pool_cgroup_) {
    system_cgroup_metrics_.push_back(
        CreateCgroupMetrics(capped_pool_cgroup_, "@capped-pool", registry));
  }
  if (system_med_cgroup_) {
    system_cgroup_metrics_.push_back(
        CreateCgroupMetrics(system_med_cgroup_, "@system-med", registry));
  }
  if (normal_pool_cgroup_) {
    system_cgroup_metrics_.push_back(
        CreateCgroupMetrics(normal_pool_cgroup_, "@normal", registry));
  }
  if (default_cgroup_) {
    system_cgroup_metrics_.push_back(
        CreateCgroupMetrics(default_cgroup_, "@default", registry));
  }

  return Thread::Create(
      "cgroup", "metrics-collector",
      &TServerCgroupManager::MetricsCollectorThread, this, &metrics_thread_);
}

void TServerCgroupManager::UpdateCgroupMetrics(CgroupMetrics& m) {
  auto result = m.cgroup->ReadCpuStats();
  if (!result.ok()) {
    VLOG(2) << "Failed to read cgroup stats: " << result.status();
    return;
  }
  m.cpu_usage_ns->set_value(result->usage_ns);
  m.cpu_user_ns->set_value(result->usage_user_ns);
  m.cpu_sys_ns->set_value(result->usage_sys_ns);
  m.nr_periods->set_value(result->nr_periods);
  m.nr_throttled->set_value(result->nr_throttled);
  m.throttled_time_ns->set_value(result->throttled_time_ns);
}

void TServerCgroupManager::MetricsCollectorThread() {
  auto is_shutdown = [this] {
    std::lock_guard lock(shutdown_mutex_);
    return shutdown_requested_;
  };

  while (!is_shutdown()) {
    int interval = FLAGS_qos_metrics_interval_sec;
    if (interval <= 0) {
      std::unique_lock lock(shutdown_mutex_);
      shutdown_cv_.wait_for(lock, std::chrono::seconds(1),
          [this] { return shutdown_requested_; });
      continue;
    }

    for (auto& m : system_cgroup_metrics_) {
      UpdateCgroupMetrics(m);
    }

    // Create metric entities for newly registered per-DB cgroups, refresh attributes
    // (e.g. after a database rename), and update all gauge values.
    {
      std::lock_guard lock(mutex_);
      for (auto& [db_oid, cgroup] : db_cgroups_) {
        auto name_it = db_names_.find(db_oid);
        auto metrics_it = db_cgroup_metrics_.find(db_oid);
        if (metrics_it == db_cgroup_metrics_.end()) {
          auto entity_id = Format("db_$0", db_oid);
          MetricAttributeMap extra_attrs;
          extra_attrs["database_oid"] = std::to_string(db_oid);
          if (name_it != db_names_.end()) {
            extra_attrs["database_name"] = name_it->second;
          }
          db_cgroup_metrics_.emplace(
              db_oid, CreateCgroupMetrics(&cgroup, entity_id, metric_registry_, extra_attrs));
        } else if (name_it != db_names_.end()) {
          metrics_it->second.entity->SetAttribute("database_name", name_it->second);
        }
      }
    }
    for (auto& [_, m] : db_cgroup_metrics_) {
      UpdateCgroupMetrics(m);
    }

    std::unique_lock lock(shutdown_mutex_);
    shutdown_cv_.wait_for(lock, std::chrono::seconds(interval),
        [this] { return shutdown_requested_; });
  }
}

Status TServerCgroupManager::MovePgBackendToCgroup(PgOid db_oid) {
  if (!FLAGS_enable_qos) {
    return Status::OK();
  }

  // For PG backend, we do a simplified setup where we do not need to create a TServerCgroupManager
  // object and where we do not set the CPU limits. This lets us ensure that only TServer needs to
  // know about what limits to set things to.
  // This does mean that for the brief period of time where a PG backend starts up in a new
  // database, we have a database cgroup with the wrong limits. But such a backend will communicate
  // with TServer, so we can load and properly set up the cgroup on the TServer very soon after.
  auto& cgroup = VERIFY_RESULT_REF(GetOrCreateDbCgroup(db_oid));
  return cgroup.MoveCurrentThreadToGroup();
}

void TServerCgroupManager::DumpCgroupsToHtml(std::ostream& out, uint64_t sample_interval_ms) const {
  auto* root_cgroup = RootCgroup();
  if (!root_cgroup) {
    out << "Cgroup management is not enabled.";
    return;
  }

  out << Format(R"#(<p><b>Root cgroup</b>: <code>$0</code></p>)#",
                EscapeForHtmlToString(root_cgroup->full_name()));

  out << Format(R"#(
      <p><b>Configuration</b>:</p>
      <ul>
        <li><b>enable_qos</b>: $0</li>
        <li><b>qos_max_db_cpu_percent</b>: $1%</li>
        <li><b>qos_evaluation_window</b>: $2us</li>
        <li><b>qos_system_high_cpu_reserved_percent</b>: $3%</li>
        <li><b>qos_system_high_cpu_max_percent</b>: $4%</li>
        <li><b>qos_system_med_cpu_max_percent</b>: $5%</li>
        <li><b>qos_capped_pool_cpu_weight</b>: $6</li>
        <li><b>qos_compaction_per_db_cgroups</b>: $7</li>
        <li><b>qos_consensus_per_db_cgroups</b>: $8</li>
        <li><b>qos_metrics_interval_sec</b>: $9s</li>
      </ul>)#",
      FLAGS_enable_qos ? "true" : "false", FLAGS_qos_max_db_cpu_percent,
      FLAGS_qos_evaluation_window_us, FLAGS_qos_system_high_cpu_reserved_percent,
      FLAGS_qos_system_high_cpu_max_percent, FLAGS_qos_system_med_cpu_max_percent,
      FLAGS_qos_capped_pool_cpu_weight, FLAGS_qos_compaction_per_db_cgroups ? "true" : "false",
      FLAGS_qos_consensus_per_db_cgroups ? "true" : "false", FLAGS_qos_metrics_interval_sec);

  out << R"#(
      <table class="table table-striped collapsable-table">
        <thead>
          <tr>
            <th>Cgroup</th>
            <th>Status</th>
            <th>Weight</th>
            <th>CPU Quota</th>
            <th>User CPU (s)</th>
            <th>System CPU (s)</th>
            <th>Total CPU (s)</th>
            <th>Throttled Time (s)</th>
            <th>Throttled Periods</th>
          </tr>
        </thead>
        <tbody>
      )#";

  auto num_cpus = NumEffectiveCPUs();

  struct InitialStats {
    Result<int64_t> throttle_time = 0;
    int child_weight = 0;
  };
  std::unordered_map<Cgroup*, InitialStats> initial_stats;

  root_cgroup->VisitTree([&initial_stats](Cgroup& cgroup, size_t) {
    InitialStats stats;
    auto result = cgroup.ReadCpuStats();
    if (!result.ok()) {
      stats.throttle_time = std::move(result).status();
    } else {
      stats.throttle_time = result->throttled_time_ns;
    }
    cgroup.VisitChildren([&stats](Cgroup& child) {
      stats.child_weight += child.cpu_weight();
    });
    initial_stats.try_emplace(&cgroup, std::move(stats));
  });

  std::this_thread::sleep_for(sample_interval_ms * 1ms);

  root_cgroup->VisitTree([&out, &initial_stats, num_cpus](Cgroup& cgroup, size_t depth) {
    auto current_stats = cgroup.ReadCpuStats();

    std::string throttle_status;
    {
      auto iter = initial_stats.find(&cgroup);
      if (iter == initial_stats.end()) {
        // Newly created cgroup.
        return;
      }
      auto& initial_throttle_time = iter->second.throttle_time;
      if (!initial_throttle_time.ok()) {
        throttle_status = EscapeForHtmlToString(AsString(initial_throttle_time.status()));
      } else if (!current_stats.ok()) {
        throttle_status = EscapeForHtmlToString(AsString(current_stats.status()));
      } else if (*initial_throttle_time < current_stats->throttled_time_ns) {
        throttle_status = "THROTTLED";
      } else {
        throttle_status = "OK";
      }
    }

    int cpu_weight = cgroup.cpu_weight();
    int total_weight = cpu_weight;
    if (depth > 0) {
      auto iter = initial_stats.find(cgroup.parent());
      DCHECK(iter != initial_stats.end());
      total_weight = iter->second.child_weight;
    }

    std::string_view name = cgroup.name();
    if (auto index = name.rfind('/'); index != name.npos) {
      // Take the last part of the name only.
      name = name.substr(index + 1);
    }
    double cpu_fraction = cgroup.cpu_max_fraction();
    int cpu_period_us = cgroup.cpu_period_us();
    int cpu_quota_us = cpu_period_us * cpu_fraction * num_cpus;
    auto quota_str = cpu_period_us * num_cpus <= cpu_quota_us
        ? "uncapped"
        : Format("$0% ($1us / $2us)", 100.0 * cpu_fraction, cpu_quota_us, cpu_period_us);

    out << Format(R"#(
          <tr data-depth="$0" class="level$0 $6" style="display: table-row">
            <td><span class="toggle $6"></span>$1</td>
            <td>$2</td>
            <td>$3% ($4)</td>
            <td>$5</td>
        )#", depth, depth == 0 ? "Root" : name, throttle_status,
        StringPrintf("%.3f", (100.0 * cpu_weight) / total_weight), cpu_weight, quota_str,
        cgroup.is_leaf() ? "expand" : "collapse");

    if (current_stats.ok()) {
      double throttled_percentage = 0.0;
      if (current_stats->nr_periods > 0) {
        throttled_percentage = 100.0 * current_stats->nr_throttled / current_stats->nr_periods;
      }
      out << Format(R"#(
            <td>$0</td>
            <td>$1</td>
            <td>$2</td>
            <td>$3</td>
            <td>$4% ($5 / $6)</td>
          )#",
          FormatNanoseconds(current_stats->usage_user_ns),
          FormatNanoseconds(current_stats->usage_sys_ns),
          FormatNanoseconds(current_stats->usage_ns),
          FormatNanoseconds(current_stats->throttled_time_ns),
          StringPrintf("%.3f", throttled_percentage),
          current_stats->nr_throttled,
          current_stats->nr_periods);
    } else {
      out << R"#(
            <td colspan="5">$0</td>
          )#", EscapeForHtmlToString(AsString(current_stats.status()));
    }

    out << R"#(
          </tr>
        )#";

    if (cgroup.is_leaf()) {
      out << Format(R"#(
          <tr data-depth="$0" class="level$0" style="display: none">
            <td></td>
            <td colspan="8">
          )#", depth + 1);

      auto thread_ids = cgroup.ReadThreadIds();
      if (!thread_ids.ok()) {
        EscapeForHtml(AsString(thread_ids.status()), &out);
      } else {
        out << Format(R"#(
              <p>$0 threads</p>
              <p>$1</p>
            )#", thread_ids->size(), AsString(*thread_ids));
      }

      out << R"#(
          </tr>
          )#";
    }
  });

  out << R"#(
      </tbody>
    </table>
  )#";
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
