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

#include "yb/consensus/log.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/docdb/docdb_rocksdb_util.h"

#include "yb/master/master_heartbeat.pb.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_service.service.h"

#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_int32(tserver_heartbeat_metrics_interval_ms, 5000,
             "Interval (in milliseconds) at which tserver sends its metrics in a heartbeat to "
             "master.");

DEFINE_UNKNOWN_bool(tserver_heartbeat_metrics_add_drive_data, true,
            "Add drive data to metrics which tserver sends to master");

DEFINE_UNKNOWN_bool(tserver_heartbeat_metrics_add_replication_status, true,
            "Add replication status to metrics tserver sends to master");

DEFINE_UNKNOWN_bool(tserver_heartbeat_metrics_add_leader_info, true,
            "Add leader info to metrics tserver sends to master");

DECLARE_uint64(rocksdb_max_file_size_for_compaction);

using namespace std::literals;

namespace yb {
namespace tserver {

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

  bool no_full_tablet_report = !req->has_tablet_report() || req->tablet_report().is_incremental();
  bool should_add_tablet_data =
      FLAGS_tserver_heartbeat_metrics_add_drive_data && no_full_tablet_report;

  for (const auto& tablet_peer : server().tablet_manager()->GetTabletPeers()) {
    if (tablet_peer) {
      auto tablet = tablet_peer->shared_tablet();
      if (tablet) {
        auto sizes = tablet->GetCurrentVersionSstFilesAllSizes();
        total_file_sizes += sizes.first;
        uncompressed_file_sizes += sizes.second;
        num_files += tablet->GetCurrentVersionNumSSTFiles();
        if (should_add_tablet_data && tablet_peer->log_available() &&
            tablet_peer->tablet_metadata()->tablet_data_state() ==
              tablet::TabletDataState::TABLET_DATA_READY) {
          auto storage_metadata = req->add_storage_metadata();
          storage_metadata->set_tablet_id(tablet_peer->tablet_id());
          storage_metadata->set_sst_file_size(sizes.first);
          storage_metadata->set_wal_file_size(tablet_peer->log()->OnDiskSize());
          storage_metadata->set_uncompressed_sst_file_size(sizes.second);
          storage_metadata->set_may_have_orphaned_post_split_data(
                tablet->MayHaveOrphanedPostSplitData());
          if (FLAGS_tserver_heartbeat_metrics_add_leader_info) {
            auto consensus_result = tablet_peer->GetRaftConsensus();
            if (consensus_result) {
              MicrosTime ht_lease_exp;
              consensus::LeaderLeaseStatus leader_lease_status =
                  consensus_result.get()->GetLeaderLeaseStatusIfLeader(&ht_lease_exp);
              auto leader_info = req->add_leader_info();
              leader_info->set_tablet_id(tablet_peer->tablet_id());
              leader_info->set_leader_lease_status(leader_lease_status);
              if (leader_lease_status == consensus::LeaderLeaseStatus::HAS_LEASE) {
                leader_info->set_ht_lease_expiration(ht_lease_exp);
              }
            }
          }
        }

        if (no_full_tablet_report) {
          auto full_compaction_status = req->add_full_compaction_statuses();
          full_compaction_status->set_tablet_id(tablet->tablet_id());
          if (tablet->HasActiveFullCompaction()) {
            full_compaction_status->set_full_compaction_state(tablet::COMPACTING);
          } else {
            full_compaction_status->set_full_compaction_state(tablet::IDLE);
          }
          full_compaction_status->set_last_full_compaction_time(
              tablet->metadata()->last_full_compaction_time());
        }
      }
    }
  }

  // Report xCluster consumer heartbeat info.
  if (FLAGS_tserver_heartbeat_metrics_add_replication_status) {
    auto xcluster_consumer = server().GetXClusterConsumer();
    if (xcluster_consumer != nullptr) {
      xcluster_consumer->PopulateMasterHeartbeatRequest(req, last_resp.needs_full_tablet_report());
    }
  }

  metrics->set_total_sst_file_size(total_file_sizes);
  metrics->set_uncompressed_sst_file_size(uncompressed_file_sizes);
  metrics->set_num_sst_files(num_files);

  // Get the total number of read and write operations.
  auto reads_hist = server().GetMetricsHistogram(
      TabletServerServiceRpcMethodIndexes::kRead);
  uint64_t num_reads = (reads_hist != nullptr) ? reads_hist->TotalCount() : 0;

  auto writes_hist = server().GetMetricsHistogram(
      TabletServerServiceRpcMethodIndexes::kWrite);
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
  // If the "max file size for compaction" flag is greater than 0, then tablet splitting should
  // be disabled for tablets with a default TTL.
  metrics->set_disable_tablet_split_if_default_ttl(FLAGS_rocksdb_max_file_size_for_compaction > 0);

  VLOG_WITH_PREFIX(4) << "Read Ops per second: " << rops_per_sec;
  VLOG_WITH_PREFIX(4) << "Write Ops per second: " << wops_per_sec;
  VLOG_WITH_PREFIX(4) << "Total SST File Sizes: "<< total_file_sizes;
  VLOG_WITH_PREFIX(4) << "Uptime seconds: "<< uptime_seconds;

  if (FLAGS_tserver_heartbeat_metrics_add_drive_data) {
    for (const std::string& path : server().fs_manager()->GetFsRootDirs()) {
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
}

uint64_t TServerMetricsHeartbeatDataProvider::CalculateUptime() {
  MonoDelta delta = MonoTime::Now().GetDeltaSince(start_time_);
  uint64_t uptime_seconds = static_cast<uint64_t>(delta.ToSeconds());
  return uptime_seconds;
}


} // namespace tserver
} // namespace yb
