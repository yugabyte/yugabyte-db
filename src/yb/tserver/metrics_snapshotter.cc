// Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/metrics_snapshotter.h"

#include <sys/statvfs.h>

#include <memory>
#include <vector>
#include <mutex>
#include <set>

#include <chrono>
#include <thread>

#ifdef __APPLE__
#include <mach/mach_init.h>
#include <mach/mach_error.h>
#include <mach/mach_host.h>
#include <mach/vm_map.h>
#else
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#endif

#include <boost/algorithm/string.hpp>

#include <rapidjson/document.h>

#include "yb/common/jsonb.h"
#include "yb/common/wire_protocol.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_defaults.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server_options.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/client/client_fwd.h"
#include "yb/gutil/macros.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/capabilities.h"
#include "yb/util/date_time.h"
#include "yb/util/decimal.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"
#include "yb/util/varint.h"

#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_stats.h"

using namespace std::literals;

DEFINE_UNKNOWN_int32(metrics_snapshotter_interval_ms, 30 * 1000,
             "Interval at which the metrics are snapshotted.");
TAG_FLAG(metrics_snapshotter_interval_ms, advanced);

DEFINE_UNKNOWN_string(metrics_snapshotter_tserver_metrics_whitelist,
    "handler_latency_yb_client_read_local_sum,handler_latency_yb_client_read_local_count",
    "Tserver metrics to record in native metrics storage.");
TAG_FLAG(metrics_snapshotter_tserver_metrics_whitelist, advanced);

DEFINE_UNKNOWN_string(metrics_snapshotter_table_metrics_whitelist,
    "rocksdb_sst_read_micros_sum,rocksdb_sst_read_micros_count",
    "Table metrics to record in native metrics storage.");
TAG_FLAG(metrics_snapshotter_table_metrics_whitelist, advanced);

constexpr int kTServerMetricsSnapshotterYbClientDefaultTimeoutMs =
  yb::RegularBuildVsSanitizers(5, 60) * 1000;

DEFINE_UNKNOWN_int32(tserver_metrics_snapshotter_yb_client_default_timeout_ms,
    kTServerMetricsSnapshotterYbClientDefaultTimeoutMs,
    "Default timeout for the YBClient embedded into the tablet server that is used "
    "by metrics snapshotter.");
TAG_FLAG(tserver_metrics_snapshotter_yb_client_default_timeout_ms, advanced);

DEFINE_UNKNOWN_uint64(metrics_snapshotter_ttl_ms, 7 * 24 * 60 * 60 * 1000 /* 1 week */,
             "Ttl for snapshotted metrics.");
TAG_FLAG(metrics_snapshotter_ttl_ms, advanced);

DECLARE_bool(enable_ysql_conn_mgr_stats);

using std::shared_ptr;
using std::vector;
using std::set;

namespace yb {

using client::YBSession;
using client::YBTableName;
using client::YBqlOp;

namespace tserver {

// Most of the actual logic of the metrics snapshotter is inside this inner class,
// to avoid having too many dependencies from the header itself.
//
// This is basically the "PIMPL" pattern.
class MetricsSnapshotter::Thread {
 public:
  Thread(const TabletServerOptions& opts, TabletServer* server);

  Status Start();
  Status Stop();

 private:
  void RunThread();
  int GetMillisUntilNextMetricsSnapshot() const;

  Status DoPrometheusMetricsSnapshot(const client::TableHandle& table,
    shared_ptr<YBSession> session, const std::string& entity_type, const std::string& entity_id,
    const std::string& metric_name, int64_t metric_val, const rapidjson::Document* details);
  Status DoYsqlConnMgrMetricsSnapshot(const client::TableHandle& table,
    shared_ptr<YBSession> session);
  Status DoMetricsSnapshot();

  void FlushSession(const std::shared_ptr<YBSession>& session,
      const std::vector<std::shared_ptr<YBqlOp>>& ops = {});
  void LogSessionErrors(const client::FlushStatus& flush_status);
  bool IsCurrentThread() const;

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  // Retrieves current cpu usage information.
  Result<vector<uint64_t>> GetCpuUsage();

  // The server for which we are collecting metrics.
  TabletServer* const server_;

  // The actual running thread (NULL before it is started)
  scoped_refptr<yb::Thread> thread_;

  boost::optional<yb::client::AsyncClientInitializer> async_client_init_;

  // True once at least one attempt to record a snapshot has been made.
  bool has_metricssnapshotted_ = false;

  // Mutex/condition pair to trigger the metrics snapshotter thread
  // to either snapshot early or exit.
  Mutex mutex_;
  ConditionVariable cond_;

  // Protected by mutex_.
  bool should_run_ = false;

  const std::string log_prefix_;

  // Tokens from FLAGS_metrics_snapshotter_tserver_metrics_whitelist.
  std::unordered_set<std::string> tserver_metrics_whitelist_;

  // Tokens from FLAGS_metrics_snapshotter_table_metrics_whitelist.
  std::unordered_set<std::string> table_metrics_whitelist_;

  // Used to calculate CPU usage if enabled. Stores {total_ticks, user_ticks, system_ticks}.
  vector<uint64_t> prev_ticks_ = {0, 0, 0};
  bool first_run_cpu_ticks_ = true;

  TabletServerOptions opts_;

  DISALLOW_COPY_AND_ASSIGN(Thread);
};

////////////////////////////////////////////////////////////
// MetricsSnapshotter
////////////////////////////////////////////////////////////

MetricsSnapshotter::MetricsSnapshotter(const TabletServerOptions& opts, TabletServer* server)
  : thread_(new Thread(opts, server)) {
}
MetricsSnapshotter::~MetricsSnapshotter() {
  WARN_NOT_OK(Stop(), "Unable to stop metrics snapshotter thread");
}

Status MetricsSnapshotter::Start() {
  return thread_->Start();
}
Status MetricsSnapshotter::Stop() {
  return thread_->Stop();
}

////////////////////////////////////////////////////////////
// MetricsSnapshotter::Thread
////////////////////////////////////////////////////////////

static std::unordered_set<std::string> CSVToSet(const std::string& s) {
  std::unordered_set<std::string> t;
  boost::split(t, s, boost::is_any_of(","));
  return t;
}

MetricsSnapshotter::Thread::Thread(const TabletServerOptions& opts, TabletServer* server)
  : server_(server),
    cond_(&mutex_),
    log_prefix_(Format("P $0: ", server_->permanent_uuid())),
    opts_(opts) {
  VLOG_WITH_PREFIX(1) << "Initializing metrics snapshotter thread";

  // Parse whitelist elements out of flag.
  tserver_metrics_whitelist_ = CSVToSet(FLAGS_metrics_snapshotter_tserver_metrics_whitelist);
  table_metrics_whitelist_ = CSVToSet(FLAGS_metrics_snapshotter_table_metrics_whitelist);

  async_client_init_.emplace(
      "tserver_metrics_snapshotter_client",
      std::chrono::milliseconds(FLAGS_tserver_metrics_snapshotter_yb_client_default_timeout_ms),
      "" /* tserver_uuid */, &server->options(), server->metric_entity(), server->mem_tracker(),
      server->messenger());
}

int MetricsSnapshotter::Thread::GetMillisUntilNextMetricsSnapshot() const {
  // When we first start up, snapshot immediately.
  if (!has_metricssnapshotted_) {
    return 0;
  }

  return FLAGS_metrics_snapshotter_interval_ms;
}

void MetricsSnapshotter::Thread::LogSessionErrors(const client::FlushStatus& flush_status) {
  const auto& errors = flush_status.errors;

  size_t num_errors_to_log = 10;

  // Log only the first 10 errors.
  LOG_WITH_PREFIX(INFO) << errors.size() << " failed ops. First few errors follow";
  size_t i = 0;
  for (const auto& e : errors) {
    if (i == num_errors_to_log) {
      break;
    }
    LOG_WITH_PREFIX(INFO) << "Op " << e->failed_op().ToString()
              << " had status " << e->status().ToString();
    i++;
  }

  if (errors.size() > num_errors_to_log) {
    LOG_WITH_PREFIX(INFO) << (errors.size() - num_errors_to_log) << " failed ops skipped.";
  }
}

void MetricsSnapshotter::Thread::FlushSession(
    const std::shared_ptr<YBSession>& session,
    const std::vector<std::shared_ptr<YBqlOp>>& ops) {
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  auto flush_status = session->TEST_FlushAndGetOpsErrors();
  if (PREDICT_FALSE(!flush_status.status.ok())) {
    LogSessionErrors(flush_status);
    return;
  }

  for (auto& op : ops) {
    if (QLResponsePB::YQL_STATUS_OK != op->response().status()) {
      LOG_WITH_PREFIX(WARNING) << "Status: " <<
        QLResponsePB::QLStatus_Name(op->response().status());
    }
  }
}

Status MetricsSnapshotter::Thread::DoPrometheusMetricsSnapshot(const client::TableHandle& table,
    shared_ptr<YBSession> session, const std::string& entity_type, const std::string& entity_id,
    const std::string& metric_name, int64_t metric_val,
    const rapidjson::Document* details = nullptr) {
  auto op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto req = op->mutable_request();

  QLAddStringHashValue(req, server_->permanent_uuid());
  QLAddStringRangeValue(req, entity_type);
  QLAddStringRangeValue(req, entity_id);
  QLAddStringRangeValue(req, metric_name);
  QLAddTimestampRangeValue(req, DateTime::TimestampNow().ToInt64());
  table.AddInt64ColumnValue(req, "value", metric_val);
  if (details != nullptr) {
    common::Jsonb jsonb;
    RETURN_NOT_OK(jsonb.FromRapidJson(*details));
    table.AddJsonbColumnValue(req, "details", jsonb.MoveSerializedJsonb());
  }

  req->set_ttl(FLAGS_metrics_snapshotter_ttl_ms);
  session->Apply(op);
  return Status::OK();
}

namespace {

constexpr uint32_t kYsqlConnMgrMaxPools = YSQL_CONN_MGR_MAX_POOLS;

constexpr auto kMetricWhitelistItemNodeUp = "node_up";
constexpr auto kMetricWhitelistItemCpuUsage = "cpu_usage";
constexpr auto kMetricWhitelistItemDiskUsage = "disk_usage";
constexpr auto kMetricWhitelistItemYsqlConnMgr = "ysql_conn_mgr";

} // namespace

Status MetricsSnapshotter::Thread::DoYsqlConnMgrMetricsSnapshot(const client::TableHandle& table,
    shared_ptr<YBSession> session) {
  if (!FLAGS_enable_ysql_conn_mgr_stats) {
    YB_LOG_EVERY_N_SECS(WARNING, 120) << "Metrics whitelist contains ysql_conn_mgr, but "
                                      << "enable_ysql_conn_mgr_stats flag is false.";
    return Status::OK();
  }
  // Below is a modified copy of the GetYsqlConnMgrStats function in
  // pgsql_webserver_wrapper.cc.
  std::vector<ConnectionStats> stats_list;
  auto shm_key = server_->GetYsqlConnMgrStatsShmemKey();
  if (shm_key == 0) {
    YB_LOG_EVERY_N_SECS(WARNING, 120) << "Ysql connection manager shmem key is zero.";
    return Status::OK();
  }

  int shmid = shmget(shm_key, 0, 0666);
  if (shmid == -1) {
    YB_LOG_EVERY_N_SECS(WARNING, 120) << "Unable to find ysql conn mgr stats from the shared "
                                      << "memory segment, with errno: "
                                      << strerror(errno);
    return Status::OK();
  }
  // Attach to the segment to get a pointer to it.
  auto *shmp = (struct ConnectionStats *)shmat(shmid, NULL, 0);
  if (shmp == NULL) {
    YB_LOG_EVERY_N_SECS(WARNING, 120) << "Unable to find ysql conn mgr stats from the shared "
                                      << "memory segment, with errno: "
                                      << strerror(errno);
    return Status::OK();
  }
  for (uint32_t itr = 0; itr < kYsqlConnMgrMaxPools; itr++) {
    if (strcmp(shmp[itr].pool_name, "") == 0) {
      break;
    }
    stats_list.push_back(shmp[itr]);
  }
  // Detach from shared memory.
  shmdt(shmp);
  // End of modified copy of the GetYsqlConnMgrStats function.

  uint64_t total_logical_connections = 0;
  uint64_t total_physical_connections = 0;
  for (const auto &stat : stats_list) {
    if (strcmp(stat.pool_name, "control_connection") != 0) {
      total_logical_connections += stat.active_clients +
                                   stat.queued_clients +
                                   stat.idle_or_pending_clients;
      total_physical_connections += stat.active_servers + stat.idle_servers;
    }
  }
  RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "tserver",
                                            server_->permanent_uuid(),
                                            "total_logical_connections",
                                            total_logical_connections));
  RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "tserver",
                                            server_->permanent_uuid(),
                                            "total_physical_connections",
                                            total_physical_connections));
  return Status::OK();
}

Status MetricsSnapshotter::Thread::DoMetricsSnapshot() {
  CHECK(IsCurrentThread());

  auto client = async_client_init_->client();
  auto session = client->NewSession(15s);

  const YBTableName kTableName(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, kMetricsSnapshotsTableName);

  client::TableHandle table;
  RETURN_NOT_OK(table.Open(kTableName, client));

  NMSWriter::EntityMetricsMap table_metrics;
  NMSWriter::MetricsMap server_metrics;
  MetricPrometheusOptions opts;
  NMSWriter nmswriter(&table_metrics, &server_metrics, opts);
  WARN_NOT_OK(
      server_->metric_registry()->WriteForPrometheus(&nmswriter, opts),
      "Couldn't write metrics for native metrics storage");
  for (const auto& [metric_name, metric_value] : server_metrics) {
    if (tserver_metrics_whitelist_.contains(metric_name)) {
      RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "tserver",
            server_->permanent_uuid(), metric_name, metric_value));
    }
  }

  if (tserver_metrics_whitelist_.contains(kMetricWhitelistItemNodeUp)) {
    RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "tserver",
                                              server_->permanent_uuid(), "node_up",
                                              1));
  }

  if (tserver_metrics_whitelist_.contains(kMetricWhitelistItemDiskUsage)) {
    struct statvfs stat;
    set<uint64_t> fs_ids;
    std::vector<std::string> all_data_paths = opts_.fs_opts.data_paths;
    all_data_paths.insert(
      all_data_paths.end(), opts_.fs_opts.wal_paths.begin(), opts_.fs_opts.wal_paths.end());
    for (const auto& path : all_data_paths) {
      if (statvfs(path.c_str(), &stat) == 0 && fs_ids.insert(stat.f_fsid).second) {
        uint64_t num_frags = static_cast<uint64_t>(stat.f_blocks);
        uint64_t frag_size = static_cast<uint64_t>(stat.f_frsize);
        uint64_t free_blocks = static_cast<uint64_t>(stat.f_bfree);
        uint64_t total_disk = num_frags * frag_size;
        uint64_t free_disk = free_blocks * frag_size;
        RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "table",
                                                  server_->permanent_uuid(), "total_disk",
                                                  total_disk));
        RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "table",
                                                  server_->permanent_uuid(), "free_disk",
                                                  free_disk));
      }
    }
  }

  if (tserver_metrics_whitelist_.contains(kMetricWhitelistItemCpuUsage)) {
    // Store the {total_ticks, user_ticks, and system_ticks}
    auto cur_ticks = CHECK_RESULT(GetCpuUsage());
    bool get_cpu_success = std::all_of(
        cur_ticks.begin(), cur_ticks.end(), [](bool v) { return v > 0; });
    if (get_cpu_success && first_run_cpu_ticks_) {
      prev_ticks_ = cur_ticks;
      first_run_cpu_ticks_ = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      cur_ticks = CHECK_RESULT(GetCpuUsage());
      get_cpu_success = std::all_of(
          cur_ticks.begin(), cur_ticks.end(), [](bool v) { return v > 0; });
    }

    if (get_cpu_success) {
      uint64_t total_ticks = cur_ticks[0] - prev_ticks_[0];
      uint64_t user_ticks = cur_ticks[1] - prev_ticks_[1];
      uint64_t system_ticks = cur_ticks[2] - prev_ticks_[2];
      if (total_ticks <= 0) {
        YB_LOG_EVERY_N_SECS(ERROR, 120) << Format("Failed to calculate CPU usage - "
                                                 "invalid total CPU ticks: $0.", total_ticks);
      } else {
        double cpu_usage_user = static_cast<double>(user_ticks) / total_ticks;
        double cpu_usage_system = static_cast<double>(system_ticks) / total_ticks;

        // The value column is type bigint, so store real value in details.
        rapidjson::Document details;
        details.SetObject();
        details.AddMember("value", cpu_usage_user, details.GetAllocator());
        RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "table",
                        server_->permanent_uuid(), "cpu_usage_user", 1000000 * cpu_usage_user,
                        &details));

        details.RemoveAllMembers();
        details.AddMember("value", cpu_usage_system, details.GetAllocator());
        RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "table",
                        server_->permanent_uuid(), "cpu_usage_system", 1000000 * cpu_usage_system,
                        &details));
      }
    } else {
      YB_LOG_EVERY_N_SECS(WARNING, 120) << Format("Failed to retrieve cpu ticks. Got "
                                                  "[total_ticks, user-ticks, system_ticks]=$0.",
                                                  cur_ticks);
    }
  }

  if (tserver_metrics_whitelist_.contains(kMetricWhitelistItemYsqlConnMgr)) {
    RETURN_NOT_OK(DoYsqlConnMgrMetricsSnapshot(table, session));
  }

  for (const auto& [table_id, table_metrics] : table_metrics) {
    for (const auto& [metric_name, metric_value] : table_metrics) {
      if (table_metrics_whitelist_.contains(metric_name)) {
        RETURN_NOT_OK(DoPrometheusMetricsSnapshot(table, session, "table", table_id, metric_name,
              metric_value));
      }
    }
  }

  FlushSession(session);
  return Status::OK();
}

Result<vector<uint64_t>> MetricsSnapshotter::Thread::GetCpuUsage() {
  uint64_t total_ticks = 0, total_user_ticks = 0, total_system_ticks = 0;
#ifdef __APPLE__
  host_cpu_load_info_data_t cpuinfo;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(
        mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t) &cpuinfo, &count) == KERN_SUCCESS) {
    for (int i = 0; i < CPU_STATE_MAX; i++) {
      total_ticks += cpuinfo.cpu_ticks[i];
    }
    total_user_ticks = cpuinfo.cpu_ticks[CPU_STATE_USER];
    total_system_ticks = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
  } else {
    YB_LOG_EVERY_N_SECS(WARNING, 120) << "Couldn't get CPU ticks, failed opening host_statistics "
                                      << "with errno: " << strerror(errno);
  }
#else
  FILE* file = fopen("/proc/stat", "r");
  if (!file) {
    YB_LOG_EVERY_N_SECS(WARNING, 120) << "Could not get CPU ticks: failed to open /proc/stat "
                                      << "with errno: " << strerror(errno);
  }
  uint64_t user_ticks, user_nice_ticks, system_ticks, idle_ticks;
  int scanned = fscanf(file, "cpu %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
      &user_ticks, &user_nice_ticks, &system_ticks, &idle_ticks);
  if (scanned <= 0) {
    YB_LOG_EVERY_N_SECS(WARNING, 120) << Format("Failed to scan /proc/stat for cpu ticks "
                                               "with error code=$0 and errno=$1.", scanned, errno);
  } else if (scanned != 4) {
    YB_LOG_EVERY_N_SECS(WARNING, 120) << Format("Failed to scan /proc/stat for cpu ticks. ",
                                               "Expected 4 inputs but got $0.", scanned);
  } else {
    if (fclose(file)) {
      YB_LOG_EVERY_N_SECS(WARNING, 120) << "Failed to close /proc/stat with errno: "
                                        << strerror(errno);
    }
    total_ticks = user_ticks + user_nice_ticks + system_ticks + idle_ticks;
    total_user_ticks = user_ticks + user_nice_ticks;
    total_system_ticks = system_ticks;
  }
#endif
  vector<uint64_t> ret = {total_ticks, total_user_ticks, total_system_ticks};
  return ret;
}

void MetricsSnapshotter::Thread::RunThread() {
  CHECK(IsCurrentThread());
  VLOG_WITH_PREFIX(1) << "Metrics snapshot thread starting";

  while (true) {
    MonoTime next_metrics_snapshot = MonoTime::Now();
    next_metrics_snapshot.AddDelta(
        MonoDelta::FromMilliseconds(GetMillisUntilNextMetricsSnapshot()));

    // Wait for either the snapshot interval to elapse, or for the signal to shut down.
    {
      MutexLock l(mutex_);
      while (true) {
        MonoDelta remaining = next_metrics_snapshot.GetDeltaSince(MonoTime::Now());
        if (remaining.ToMilliseconds() <= 0 ||
            !should_run_) {
          break;
        }
        cond_.TimedWait(remaining);
      }

      if (!should_run_) {
        VLOG_WITH_PREFIX(1) << "Metris snapshot thread finished";
        return;
      }
    }

    Status s = DoMetricsSnapshot();
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "Failed to snapshot metrics, code=" << s;
    }
    has_metricssnapshotted_ = true;
  }
}

bool MetricsSnapshotter::Thread::IsCurrentThread() const {
  return thread_.get() == yb::Thread::current_thread();
}

Status MetricsSnapshotter::Thread::Start() {
  CHECK(thread_ == nullptr);

  async_client_init_->Start();

  should_run_ = true;
  return yb::Thread::Create("metrics_snapshotter", "metrics_snapshot",
      &MetricsSnapshotter::Thread::RunThread, this, &thread_);
}

Status MetricsSnapshotter::Thread::Stop() {
  if (!thread_) {
    return Status::OK();
  }

  async_client_init_->Shutdown();

  {
    MutexLock l(mutex_);
    should_run_ = false;
    cond_.Signal();
  }
  RETURN_NOT_OK(ThreadJoiner(thread_.get()).Join());
  thread_ = nullptr;
  return Status::OK();
}

} // namespace tserver
} // namespace yb
