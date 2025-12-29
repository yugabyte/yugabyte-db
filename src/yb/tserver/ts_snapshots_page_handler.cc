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

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/client/client.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/strings/human_readable.h"

#include "yb/master/master_backup.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/html_print_helper.h"
#include "yb/server/webui_util.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver-path-handlers.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/url-coding.h"

DECLARE_int32(yb_client_admin_operation_timeout_sec);

using std::string;
using std::vector;

namespace yb::tserver {

namespace {

const std::string kActiveRocksDBSnapshotId = "Active RocksDB";

using InodeMap = std::unordered_map<uint64_t, tablet::FilePB>;
using HybridTimeMicros = uint64_t;
constexpr HybridTimeMicros kMaxHybridTimeMicros = std::numeric_limits<HybridTimeMicros>::max();
using SnapshotMetadataMap = std::unordered_map<SnapshotId, master::SysSnapshotEntryPB>;
// Logically, this is a map from (namespace name, snapshot creation time, snapshot id) to the set of
// inodes in that snapshots. Having the snapshot creation time makes it easy to display in reverse
// chronological order.
using NamespaceToSnapshotToInodesMap =
    std::unordered_map<NamespaceName, std::map<std::pair<HybridTimeMicros, SnapshotId>, InodeMap>>;

Result<SnapshotMetadataMap> GetSnapshotMetadata(client::YBClient* client) {
  SnapshotMetadataMap result;
  for (const auto& snapshot : VERIFY_RESULT(client->ListSnapshots())) {
    result[TryFullyDecodeTxnSnapshotId(snapshot.id()).ToString()] = std::move(snapshot.entry());
  }
  return result;
}

Result<NamespaceToSnapshotToInodesMap> GetNamespaceToSnapshotToInodesMap(
    const TSTabletManager& ts_manager, const SnapshotMetadataMap& snapshot_metadata) {
  NamespaceToSnapshotToInodesMap namespace_to_snapshot_to_inodes_map;
  auto* env = ts_manager.server()->fs_manager()->env();
  for (const auto& peer : ts_manager.GetTabletPeers()) {
    if (!peer->IsRunning()) {
      VLOG(1) << "Tablet peer " << peer->tablet_id() << " is not running";
      continue;
    }
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet) {
      VLOG(1) << "No tablet found for peer " << peer->tablet_id();
      continue;
    }
    auto namespace_name = tablet->metadata()->namespace_name();
    auto& snapshot_to_inodes_map = namespace_to_snapshot_to_inodes_map[namespace_name];

    // Add files from active rocksdb directory using a dummy id for the snapshot id.
    const auto active_rocksdb_id = kActiveRocksDBSnapshotId + namespace_name;
    if (!env->DirExists(tablet->metadata()->rocksdb_dir())) {
      VLOG(1) << "No rocksdb directory found for tablet " << tablet->tablet_id();
      continue;
    }
    auto& inode_map =
        snapshot_to_inodes_map[std::make_pair(kMaxHybridTimeMicros, active_rocksdb_id)];
    for (auto& file : VERIFY_RESULT(tablet::ListFiles(tablet->metadata()->rocksdb_dir()))) {
      inode_map.insert({file.inode(), std::move(file)});
    }

    // Add files from snapshot directories.
    auto snapshots_dir =
        tablet::TabletSnapshots::SnapshotsDirName(tablet->metadata()->rocksdb_dir());
    if (!env->DirExists(snapshots_dir)) {
      VLOG(1) << "No snapshots directory found for tablet " << tablet->tablet_id();
      continue;
    }
    auto snapshot_dirs = VERIFY_RESULT(env->GetChildren(snapshots_dir, ExcludeDots::kTrue));
    for (const auto& snapshot_id : snapshot_dirs) {
      auto snapshot_dir = JoinPathSegments(snapshots_dir, snapshot_id);
      if (!env->DirExists(snapshot_dir)) continue;

      uint64_t snapshot_creation_time = 0;
      if (auto it = snapshot_metadata.find(snapshot_id); it != snapshot_metadata.end()) {
        snapshot_creation_time = HybridTime::FromPB(
            it->second.snapshot_hybrid_time()).GetPhysicalValueMicros();
      } else {
        LOG(WARNING) << "Snapshot " << snapshot_id << " not found in snapshot metadata";
      }
      auto& inode_map = snapshot_to_inodes_map[std::make_pair(snapshot_creation_time, snapshot_id)];
      for (auto& file : VERIFY_RESULT(tablet::ListFiles(snapshot_dir))) {
        inode_map.insert({file.inode(), std::move(file)});
      }
    }
  }
  return namespace_to_snapshot_to_inodes_map;
}

// These structs are eventually printed in the UI.
struct SnapshotDisplayInfo {
  bool is_active_rocksdb = false;
  std::string snapshot_id;
  std::string schedule_id;
  // The sum of the sizes of files in this snapshot, all newer snapshots, and active RocksDB files.
  uint64_t cumulative_bytes = 0;
  // The sum of the sizes of files in this snapshot that are exclusive to this snapshot (i.e., the
  // space would go down by this much if the snapshot was deleted).
  uint64_t exclusive_bytes = 0;
  HybridTimeMicros hybrid_time_micros;
};
using NamespaceToSnapshotDisplayInfo =
    std::unordered_map<NamespaceName, std::vector<SnapshotDisplayInfo>>;

Result<NamespaceToSnapshotDisplayInfo> FillSnapshotDisplayInfo(
    const NamespaceToSnapshotToInodesMap& namespace_to_snapshot_to_inodes_map,
    const SnapshotMetadataMap& snapshot_metadata_map) {
  // Figure out the set of inodes that are only in one snapshot to calculate exclusive bytes later.
  std::unordered_map<uint64_t, int> inode_counts;
  for (const auto& [_, snapshot_map] : namespace_to_snapshot_to_inodes_map) {
    for (const auto& [_, inode_map] : snapshot_map) {
      for (const auto& [inode, _] : inode_map) {
        ++inode_counts[inode];
      }
    }
  }

  // Fill the snapshot display info for each namespace.
  NamespaceToSnapshotDisplayInfo result;
  for (const auto& [namespace_name, snapshot_map] : namespace_to_snapshot_to_inodes_map) {
    uint64_t cumulative_bytes = 0;
    std::unordered_set<uint64_t> inodes_in_namespace;
    // Iterate over the snapshot map in reverse order to get the snapshots in reverse chronological
    // order.
    for (auto it = snapshot_map.rbegin(); it != snapshot_map.rend(); ++it) {
      SnapshotDisplayInfo info;
      auto& [snapshot_creation_time_and_id, inode_map] = *it;
      auto& [snapshot_creation_time, snapshot_id] = snapshot_creation_time_and_id;
      info.is_active_rocksdb = snapshot_id.starts_with(kActiveRocksDBSnapshotId);
      info.snapshot_id = snapshot_id;
      auto snapshot_metadata_it = snapshot_metadata_map.find(info.snapshot_id);
      if (!info.is_active_rocksdb && snapshot_metadata_it != snapshot_metadata_map.end()) {
        info.hybrid_time_micros = snapshot_creation_time;
        // Manual snapshots do not have a schedule id.
        if (auto schedule_id = FullyDecodeSnapshotScheduleId(
            snapshot_metadata_it->second.schedule_id()); schedule_id.ok()) {
          info.schedule_id = schedule_id->ToString();
        }
      } else {
        LOG(WARNING) << "Snapshot " << snapshot_id << " not found in snapshot metadata";
      }

      for (const auto& [inode, file] : inode_map) {
        if (inodes_in_namespace.insert(inode).second) {
          VLOG(1) << Format("File $0 ($1) is new to snapshot $2", file.name(),
              HumanReadableNumBytes::ToString(file.size_bytes()), snapshot_id);
          cumulative_bytes += file.size_bytes();
        }
        if (inode_counts[inode] == 1) {
          info.exclusive_bytes += file.size_bytes();
          VLOG(1) << Format(
              "File $0 ($1) is exclusive to snapshot $2", file.name(),
              HumanReadableNumBytes::ToString(file.size_bytes()), snapshot_id);
        }
      }
      info.cumulative_bytes = cumulative_bytes;
      result[namespace_name].push_back(std::move(info));
    }
  }
  return result;
}

Status RenderSnapshotDisplayInfo(
    std::stringstream& output,
    const NamespaceToSnapshotDisplayInfo& namespace_to_snapshot_display_info) {
  output << "<h1>Scheduled Snapshots Disk Usage</h1>\n";
  output << "<p>This page shows the total disk usage of active RocksDB and snapshots. "
         << "It does not include the disk usage of WALs.</p>\n";
  output << "<p>The 'Cumulative Size' column shows the total size of files in this snapshot, newer "
         << "snapshots, and active RocksDB files. The size delta from the previous snapshot is "
         << "shown in parentheses.</p>\n";
  output << "<p>The 'Exclusive Size' column shows the total size of files that are only "
         << "retained by this snapshot. (Disk size would decrease by this amount if the snapshot "
         << "was deleted.)</p>\n";

  HtmlPrintHelper html_print_helper(output);
  for (const auto& [namespace_name, snapshot_display_infos] : namespace_to_snapshot_display_info) {
    output << Format("<h2>Namespace: $0</h2>\n", namespace_name);

    auto html_table = html_print_helper.CreateTablePrinter(
        Format("snapshots_$0", namespace_name),
        {"Snapshot ID", "Snapshot Time (UTC)", "Cumulative Size (+ Incremental Size)",
         "Exclusive Size", "Schedule ID"});
    uint64_t previous_cumulative_bytes = 0;
    for (const auto& snapshot_display_info : snapshot_display_infos) {
      std::string snapshot_id = "Active RocksDB";
      std::string timestamp = "N/A";
      std::string schedule_id =
          snapshot_display_info.schedule_id.empty() ? "N/A" : snapshot_display_info.schedule_id;
      std::string size_delta_str = "";
      if (!snapshot_display_info.snapshot_id.starts_with(kActiveRocksDBSnapshotId)) {
        snapshot_id = snapshot_display_info.snapshot_id;
        timestamp = Timestamp(
            snapshot_display_info.hybrid_time_micros).ToHumanReadableTime(UseUTC::kTrue);
        size_delta_str = Format(" (+$0)", HumanReadableNumBytes::ToString(
                snapshot_display_info.cumulative_bytes - previous_cumulative_bytes));
      }
      auto cumulative_bytes_str =
          HumanReadableNumBytes::ToString(snapshot_display_info.cumulative_bytes) + size_delta_str;
      html_table.AddRow(
          snapshot_id, timestamp, cumulative_bytes_str,
          HumanReadableNumBytes::ToString(snapshot_display_info.exclusive_bytes), schedule_id);
      previous_cumulative_bytes = snapshot_display_info.cumulative_bytes;
    }
    html_table.Print();
  }
  return Status::OK();
}

Status HandleSnapshotsPageUI(const TabletServer& tserver, Webserver::WebResponse* resp) {
  auto snapshot_metadata = VERIFY_RESULT(GetSnapshotMetadata(tserver.client_future().get()));
  auto namespace_to_snapshot_to_inodes_map = VERIFY_RESULT(
      GetNamespaceToSnapshotToInodesMap(*tserver.tablet_manager(), snapshot_metadata));
  auto namespace_to_snapshot_display_info = VERIFY_RESULT(
      FillSnapshotDisplayInfo(namespace_to_snapshot_to_inodes_map, snapshot_metadata));
  return RenderSnapshotDisplayInfo(resp->output, namespace_to_snapshot_display_info);
}

} // namespace

void TabletServerPathHandlers::HandleSnapshotsPage(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  auto status = HandleSnapshotsPageUI(*tserver_, resp);
  if (!status.ok()) {
    resp->output << EscapeForHtmlToString(StatusToString(status));
  }
}

// TODO(#29623): Add a JSON endpoint here that reuses everything except the HTML rendering.

} // namespace yb::tserver
