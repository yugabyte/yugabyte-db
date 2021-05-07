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

#include "yb/tserver/tserver_metrics_heartbeat_data_provider.h"

#include "yb/master/master.pb.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"

DEFINE_int32(tserver_heartbeat_metrics_interval_ms, 5000,
             "Interval (in milliseconds) at which tserver sends its metrics in a heartbeat to "
             "master.");

using namespace std::literals;

namespace yb {
namespace tserver {

void addTabletData(master::TabletPathInfoPB* path_info,
                   const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                   uint64_t sst_file_size,
                   uint64_t uncompressed_sst_file_size,
                   std::unordered_map<std::string, master::ListTabletsOnPathPB*>* paths) {
  std::string data_dir = tablet_peer->tablet_metadata()->data_root_dir();
  const auto& tablet = tablet_peer->shared_tablet();
  if (!tablet_peer->log_available() || !tablet || data_dir.empty()) {
    return;
  }
  // Ignore WAL files when using another path
  uint64 wal_file_size = data_dir == tablet_peer->log()->wal_dir() ?
                          tablet_peer->log()->OnDiskSize() : 0;

  auto list_tablets_on_path_it = paths->find(data_dir);
  if (list_tablets_on_path_it == paths->end()) {
    auto* list_tablets_on_path = path_info->add_list_path();
    list_tablets_on_path->set_path_id(data_dir);
    list_tablets_on_path_it = paths->emplace(data_dir, list_tablets_on_path).first;
  }

  auto* const tablet_on_path = list_tablets_on_path_it->second->add_tablet();
  tablet_on_path->set_tablet_id(tablet_peer->tablet_id());
  tablet_on_path->set_sst_file_size(sst_file_size);
  tablet_on_path->set_wal_file_size(wal_file_size);
  tablet_on_path->set_uncompressed_sst_file_size(uncompressed_sst_file_size);
}


TServerMetricsHeartbeatDataProvider::TServerMetricsHeartbeatDataProvider(TabletServer* server) :
  PeriodicalHeartbeatDataProvider(server,
      MonoDelta::FromMilliseconds(FLAGS_tserver_heartbeat_metrics_interval_ms)),
  start_time_(MonoTime::Now()) {}

void TServerMetricsHeartbeatDataProvider::DoAddData(
    const master::TSHeartbeatResponsePB& last_resp, master::TSHeartbeatRequestPB* req) {
  // Get the total memory used.
  size_t mem_usage = MemTracker::GetRootTracker()->GetUpdatedConsumption(true /* force */);
  auto* metrics = req->mutable_metrics();
  metrics->set_total_ram_usage(static_cast<int64_t>(mem_usage));
  VLOG_WITH_PREFIX(4) << "Total Memory Usage: " << mem_usage;

  uint64_t total_file_sizes = 0;
  uint64_t uncompressed_file_sizes = 0;
  uint64_t num_files = 0;

  std::unordered_map<std::string, master::ListTabletsOnPathPB*> paths;
  master::TabletPathInfoPB* path_info =
      !req->has_tablet_report() || req->tablet_report().is_incremental() ?
        req->mutable_tablet_path_info() : nullptr;

  for (const auto& tablet_peer : server().tablet_manager()->GetTabletPeers()) {
    if (tablet_peer) {
      auto tablet = tablet_peer->shared_tablet();
      if (tablet) {
        auto sizes = tablet->GetCurrentVersionSstFilesAllSizes();
        total_file_sizes += sizes.first;
        uncompressed_file_sizes += sizes.second;
        num_files += tablet->GetCurrentVersionNumSSTFiles();

        if (path_info) {
          addTabletData(path_info, tablet_peer, sizes.first, sizes.second, &paths);
        }
      }
    }
  }
  metrics->set_total_sst_file_size(total_file_sizes);
  metrics->set_uncompressed_sst_file_size(uncompressed_file_sizes);
  metrics->set_num_sst_files(num_files);

  // Get the total number of read and write operations.
  auto reads_hist = server().GetMetricsHistogram(
      TabletServerServiceIf::RpcMetricIndexes::kMetricIndexRead);
  uint64_t num_reads = (reads_hist != nullptr) ? reads_hist->TotalCount() : 0;

  auto writes_hist = server().GetMetricsHistogram(
      TabletServerServiceIf::RpcMetricIndexes::kMetricIndexWrite);
  uint64_t num_writes = (writes_hist != nullptr) ? writes_hist->TotalCount() : 0;

  // Calculate the read and write ops per second.
  MonoDelta diff = CoarseMonoClock::Now() - prev_run_time();
  double_t div = diff.ToSeconds();

  double rops_per_sec = (div > 0 && num_reads > 0) ?
      (static_cast<double>(num_reads - prev_reads_) / div) : 0;

  double wops_per_sec = (div > 0 && num_writes > 0) ?
      (static_cast<double>(num_writes - prev_writes_) / div) : 0;

  prev_reads_ = num_reads;
  prev_writes_ = num_writes;
  metrics->set_read_ops_per_sec(rops_per_sec);
  metrics->set_write_ops_per_sec(wops_per_sec);
  uint64_t uptime_seconds = CalculateUptime();

  metrics->set_uptime_seconds(uptime_seconds);

  VLOG_WITH_PREFIX(4) << "Read Ops per second: " << rops_per_sec;
  VLOG_WITH_PREFIX(4) << "Write Ops per second: " << wops_per_sec;
  VLOG_WITH_PREFIX(4) << "Total SST File Sizes: "<< total_file_sizes;
  VLOG_WITH_PREFIX(4) << "Uptime seconds: "<< uptime_seconds;

  for (const std::string& path : server().fs_manager()->GetDataRootDirs()) {
    auto stat = server().GetEnv()->GetFilesystemStatsBytes(path.c_str());
    if (!stat.ok()) {
      continue;
    }
    auto* path_metric = metrics->add_path_metrics();
    path_metric->set_path_id(path);
    path_metric->set_used_space(stat->used_space);
    path_metric->set_total_space(stat->total_space);
  }
}

uint64_t TServerMetricsHeartbeatDataProvider::CalculateUptime() {
  MonoDelta delta = MonoTime::Now().GetDeltaSince(start_time_);
  uint64_t uptime_seconds = static_cast<uint64_t>(delta.ToSeconds());
  return uptime_seconds;
}


} // namespace tserver
} // namespace yb
