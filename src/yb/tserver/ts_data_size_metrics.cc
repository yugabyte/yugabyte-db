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

#include "yb/tserver/ts_data_size_metrics.h"

#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

namespace yb::tserver {

METRIC_DEFINE_gauge_uint64(server, ts_data_size, "Data Size", MetricUnit::kBytes,
    "Amount of data in data directories (including snapshots) across all tablets.");

METRIC_DEFINE_gauge_uint64(server, ts_active_data_size, "Active Data Size", MetricUnit::kBytes,
    "Amount of data in active data directories (excluding snapshots) across all non-hidden "
    "tablets. Hidden tablets (retained by a snapshot schedule) are excluded.");

TsDataSizeMetrics::TsDataSizeMetrics(TSTabletManager* tablet_manager):
    tablet_manager_(tablet_manager) {
  ts_data_size_metric_ =
      METRIC_ts_data_size.Instantiate(tablet_manager->server()->metric_entity(), 0);
  ts_active_data_size_metric_ =
      METRIC_ts_active_data_size.Instantiate(tablet_manager->server()->metric_entity(), 0);
}

using Inode_T = uint64_t;
struct FileData {
  uint64_t size = 0;
  // Whether the file would still exist if it was not retained by a snapshot or snapshot schedule.
  bool active = false;
};
using InodeMap = std::unordered_map<Inode_T, FileData>;

void ProcessFilesInDir(const std::string& dir, InodeMap* inode_map, bool active) {
  auto files = tablet::ListFiles(dir);
  if (!files.ok()) {
    VLOG(3) << files.status();
    return;
  }
  for (const auto& file : *files) {
    auto& entry = (*inode_map)[file.inode()];
    entry.size = file.size_bytes();
    entry.active |= active;
  }
}

void TsDataSizeMetrics::Update() {
  auto start_time = CoarseMonoClock::now();
  InodeMap tserver_inode_map;
  auto tablet_peers = tablet_manager_->GetTabletPeers();
  for (const auto& tablet_peer : tablet_peers) {
    auto shared_tablet = tablet_peer->shared_tablet_maybe_null();
    if (shared_tablet == nullptr) continue;

    // Count RegularDB, IntentsDB, and WAL files as active if and only if the tablet is not hidden.
    // Do not count snapshot-only files as active.
    InodeMap tablet_inode_map;
    const auto* meta = shared_tablet->metadata();
    ProcessFilesInDir(meta->rocksdb_dir(), &tablet_inode_map, /*active=*/!meta->hidden());
    ProcessFilesInDir(meta->intents_rocksdb_dir(), &tablet_inode_map, /*active=*/!meta->hidden());
    ProcessFilesInDir(meta->wal_dir(), &tablet_inode_map, /*active=*/!meta->hidden());
    ProcessFilesInDir(meta->snapshots_dir(), &tablet_inode_map, /*active=*/false);

    size_t tablet_total_size = 0;
    for (const auto& [_, file] : tablet_inode_map) {
      tablet_total_size += file.size;
    }
    tablet_peer->SetTabletOnDiskSize(tablet_total_size);

    // Aggregate tablet inode map with tserver inode map.
    for (const auto& [inode, file_data] : tablet_inode_map) {
      auto& tserver_entry = tserver_inode_map[inode];
      tserver_entry.size = file_data.size;
      tserver_entry.active |= file_data.active;
    }
  }

  // Calculate tserver-level metrics from aggregated inode map.
  size_t total_size = 0;
  size_t total_active_size = 0;
  for (const auto& [_, file] : tserver_inode_map) {
    total_size += file.size;
    if (file.active) {
      total_active_size += file.size;
    }
  }
  ts_data_size_metric_->set_value(total_size);
  ts_active_data_size_metric_->set_value(total_active_size);
  VLOG(1) << "Updated tablet server data size metrics in "
          << MonoDelta(CoarseMonoClock::now() - start_time)
          << " total_size=" << total_size
          << " total_active_size=" << total_active_size;
}

} // namespace yb::tserver
