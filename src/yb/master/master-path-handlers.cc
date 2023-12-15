// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/master/master-path-handlers.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_set>

#include <boost/date_time/posix_time/time_formatters.hpp>

#include "yb/common/common_types_util.h"
#include "yb/common/hybrid_time.h"
#include "yb/dockv/partition.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_fwd.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/encryption_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_encryption.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/server/webserver.h"
#include "yb/server/webui_util.h"

#include "yb/tablet/tablet_types.pb.h"
#include "yb/util/curl_util.h"
#include "yb/util/flags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/logging.h"
#include "yb/util/status_log.h"
#include "yb/util/string_case.h"
#include "yb/util/timestamp.h"
#include "yb/util/url-coding.h"
#include "yb/util/version_info.h"
#include "yb/util/version_info.pb.h"

DEFINE_UNKNOWN_int32(
    hide_dead_node_threshold_mins, 60 * 24,
    "After this many minutes of no heartbeat from a node, hide it from the UI "
    "(we presume it has been removed from the cluster). If -1, this flag is ignored and node is "
    "never hidden from the UI");

DEFINE_RUNTIME_bool(master_webserver_require_https, false,
    "Require HTTPS when redirecting master UI requests to the leader.");

DEFINE_test_flag(bool, master_ui_redirect_to_leader, true,
                 "Redirect master UI requests to the master leader");

DEFINE_RUNTIME_uint32(leaderless_tablet_alert_delay_secs, 2 * 60,
    "From master's view, if the tablet doesn't have a valid leader for this amount of seconds, "
    "alert it as a leaderless tablet.");

DECLARE_int32(ysql_tablespace_info_refresh_secs);

DECLARE_string(webserver_ca_certificate_file);

DECLARE_string(webserver_certificate_file);

DEPRECATE_FLAG(uint64, master_maximum_heartbeats_without_lease, "12_2023");

namespace yb {

namespace {

const int64_t kCurlTimeoutSec = 180;

const char* GetProtocol() {
  return FLAGS_webserver_certificate_file.empty() || !FLAGS_master_webserver_require_https
      ? "http" : "https";
}

std::optional<HostPortPB> GetPublicHttpHostPort(const ServerRegistrationPB& registration) {
  if (registration.http_addresses().empty()) {
    return {};
  }
  if (registration.broadcast_addresses().empty()) {
    return registration.http_addresses(0);
  }
  HostPortPB public_http_hp;
  public_http_hp.set_host(registration.broadcast_addresses(0).host());
  public_http_hp.set_port(registration.http_addresses(0).port());
  return public_http_hp;
}

}  // namespace

using consensus::RaftPeerPB;
using std::vector;
using std::map;
using std::pair;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::min;
using strings::Substitute;
using server::MonitoredTask;

using namespace std::placeholders;

namespace master {

MasterPathHandlers::~MasterPathHandlers() {
}

void MasterPathHandlers::TabletCounts::operator+=(const TabletCounts& other) {
  user_tablet_leaders += other.user_tablet_leaders;
  user_tablet_followers += other.user_tablet_followers;
  system_tablet_leaders += other.system_tablet_leaders;
  system_tablet_followers += other.system_tablet_followers;
}

MasterPathHandlers::ZoneTabletCounts::ZoneTabletCounts(
  const TabletCounts& tablet_counts,
  uint32_t active_tablets_count) : tablet_counts(tablet_counts),
                                   active_tablets_count(active_tablets_count) {
}

void MasterPathHandlers::ZoneTabletCounts::operator+=(const ZoneTabletCounts& other) {
  tablet_counts += other.tablet_counts;
  node_count += other.node_count;
  active_tablets_count += other.active_tablets_count;
}

// Retrieve the specified URL response from the leader master
void MasterPathHandlers::RedirectToLeader(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  auto redirect_result = GetLeaderAddress(req);
  if (!redirect_result) {
    auto s = redirect_result.status();
    LOG(WARNING) << s.ToString();
    *output << "<h2>" << EscapeForHtmlToString(s.ToString()) << "</h2>\n";
    return;
  }
  std::string redirect = *redirect_result;
  EasyCurl curl;
  faststring buf;

  curl.set_follow_redirects(true);
  curl.set_ca_cert(FLAGS_webserver_ca_certificate_file);
  auto s = curl.FetchURL(redirect, &buf, kCurlTimeoutSec, {} /* headers */);
  if (!s.ok()) {
    LOG(WARNING) << "Error retrieving leader master URL: " << redirect
                 << ", error :" << s.ToString();
    *output << "Error retrieving leader master URL: <a href=\"" << redirect
            << "\">" + redirect + "</a><br> Error: "
            << EscapeForHtmlToString(s.ToString()) << ".<br>";
    return;
  }
  *output << buf.ToString();
}

Result<std::string> MasterPathHandlers::GetLeaderAddress(const Webserver::WebRequest& req) {
  vector<ServerEntryPB> masters;
  Status s = master_->ListMasters(&masters);
  if (!s.ok()) {
    s = s.CloneAndPrepend("Unable to list masters during web request handling");
    return s;
  }
  ServerRegistrationPB local_reg;
  s = master_->GetMasterRegistration(&local_reg);
  if (!s.ok()) {
    s = s.CloneAndPrepend("Unable to get local registration during web request handling");
    return s;
  }
  const auto leader = std::find_if(masters.begin(), masters.end(), [](const auto& master) {
    return !master.has_error() && master.role() == PeerRole::LEADER;
  });
  if (leader == masters.end() || leader->registration().http_addresses().empty()) {
    return STATUS(
        NotFound, "Unable to locate leader master to redirect this request: " + req.redirect_uri);
  }
  auto& reg = leader->registration();
  auto http_broadcast_addresses = reg.broadcast_addresses();
  for (HostPortPB& host_port : http_broadcast_addresses) {
    host_port.set_port(reg.http_addresses(0).port());
  }
  return Format(
      "$0://$1$2$3",
      GetProtocol(),
      HostPortPBToString(DesiredHostPort(
          http_broadcast_addresses,
          reg.http_addresses(),
          reg.cloud_info(),
          local_reg.cloud_info())),
      req.redirect_uri,
      req.query_string.empty() ? "?raw" : "?" + req.query_string + "&raw");
}

void MasterPathHandlers::CallIfLeaderOrPrintRedirect(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp,
    const Webserver::PathHandlerCallback& callback) {
  string redirect;
  // Lock the CatalogManager in a self-contained block, to prevent double-locking on callbacks.
  {
    SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());

    // If we are not the master leader, redirect the URL.
    if (!l.IsInitializedAndIsLeader() && PREDICT_TRUE(FLAGS_TEST_master_ui_redirect_to_leader)) {
      RedirectToLeader(req, resp);
      return;
    }

    // Handle the request as a leader master.
    callback(req, resp);
    return;
  }
}

inline void MasterPathHandlers::TServerTable(std::stringstream* output,
                                             TServersViewType viewType) {
  *output << "<table class='table table-striped'>\n";
  *output << "    <tr>\n"
          << "      <th>Server</th>\n"
          << "      <th>Time since </br>heartbeat</th>\n"
          << "      <th>Status & Uptime</th>\n";

  if (viewType == TServersViewType::kTServersClocksView) {
    *output << "      <th>Physical Time (UTC)</th>\n"
            << "      <th>Hybrid Time (UTC)</th>\n"
            << "      <th>Heartbeat RTT</th>\n";
  } else {
    DCHECK_EQ(viewType, TServersViewType::kTServersDefaultView);
    *output << "      <th>User Tablet-Peers / Leaders</th>\n"
            << "      <th>RAM Used</th>\n"
            << "      <th>Num SST Files</th>\n"
            << "      <th>Total SST Files Size</th>\n"
            << "      <th>Uncompressed SST </br>Files Size</th>\n"
            << "      <th>Read ops/sec</th>\n"
            << "      <th>Write ops/sec</th>\n";
  }

  *output << "      <th>Cloud</th>\n"
          << "      <th>Region</th>\n"
          << "      <th>Zone</th>\n";

  if (viewType == TServersViewType::kTServersDefaultView) {
    *output << "      <th>System Tablet-Peers / Leaders</th>\n"
            << "      <th>Active Tablet-Peers</th>\n";
  }

  *output << "    </tr>\n";
}

namespace {

constexpr int kHoursPerDay = 24;
constexpr int kSecondsPerMinute = 60;
constexpr int kMinutesPerHour = 60;
constexpr int kSecondsPerHour = kSecondsPerMinute * kMinutesPerHour;
constexpr int kMinutesPerDay = kMinutesPerHour * kHoursPerDay;
constexpr int kSecondsPerDay = kSecondsPerHour * kHoursPerDay;

string UptimeString(uint64_t seconds) {
  auto days = seconds / kSecondsPerDay;
  auto hours = (seconds / kSecondsPerHour) - (days * kHoursPerDay);
  auto mins = (seconds / kSecondsPerMinute) - (days * kMinutesPerDay) - (hours * kMinutesPerHour);

  std::ostringstream uptime_string_stream;
  uptime_string_stream << " ";
  if (days > 0) {
    uptime_string_stream << days << "days, ";
  }
  uptime_string_stream << hours << ":" << std::setw(2) << std::setfill('0') << mins <<
      ":" << std::setw(2) << std::setfill('0') << (seconds % 60);

  return uptime_string_stream.str();
}

bool ShouldHideTserverNodeFromDisplay(
    const TSDescriptor* ts, int hide_dead_node_threshold_mins) {
  return hide_dead_node_threshold_mins > 0
      && !ts->IsLive()
      && ts->TimeSinceHeartbeat().ToMinutes() > hide_dead_node_threshold_mins;
}

int GetTserverCountForDisplay(const TSManager* ts_manager) {
  int count = 0;
  for (const auto& tserver : ts_manager->GetAllDescriptors()) {
    if (!ShouldHideTserverNodeFromDisplay(tserver.get(), FLAGS_hide_dead_node_threshold_mins)) {
      count++;
    }
  }
  return count;
}

}  // anonymous namespace

string MasterPathHandlers::GetHttpHostPortFromServerRegistration(
    const ServerRegistrationPB& reg) const {
  auto hp = GetPublicHttpHostPort(reg);
  if (hp) {
    return HostPortPBToString(*hp);
  }
  return "";
}

namespace {

bool TabletServerComparator(
    const std::shared_ptr<TSDescriptor>& a, const std::shared_ptr<TSDescriptor>& b) {
  auto a_cloud_info = a->GetRegistration().common().cloud_info();
  auto b_cloud_info = b->GetRegistration().common().cloud_info();

  if (a_cloud_info.placement_cloud() == b_cloud_info.placement_cloud()) {
    if (a_cloud_info.placement_region() == b_cloud_info.placement_region()) {
      if (a_cloud_info.placement_zone() == b_cloud_info.placement_zone()) {
        return a->permanent_uuid() < b->permanent_uuid();
      }
      return a_cloud_info.placement_zone() < b_cloud_info.placement_zone();
    }
    return a_cloud_info.placement_region() < b_cloud_info.placement_region();
  }
  return a_cloud_info.placement_cloud() < b_cloud_info.placement_cloud();
}

}  // anonymous namespace

void MasterPathHandlers::TServerDisplay(const std::string& current_uuid,
                                        std::vector<std::shared_ptr<TSDescriptor>>* descs,
                                        TabletCountMap* tablet_map,
                                        std::stringstream* output,
                                        const int hide_dead_node_threshold_mins,
                                        TServersViewType viewType) {
  // Copy vector to avoid changes to the reference descs passed
  std::vector<std::shared_ptr<TSDescriptor>> local_descs(*descs);

  auto blacklist_result = master_->catalog_manager()->BlacklistSetFromPB();
  BlacklistSet blacklist = blacklist_result.ok() ? *blacklist_result : BlacklistSet();
  auto leader_blacklist_result =
      master_->catalog_manager()->BlacklistSetFromPB(true);  // leader_blacklist
  BlacklistSet leader_blacklist =
      leader_blacklist_result.ok() ? *leader_blacklist_result : BlacklistSet();
  vector<AffinitizedZonesSet> affinitized_zones;
  auto status = master_->catalog_manager()->GetAllAffinitizedZones(&affinitized_zones);
  if (!status.ok()) {
    status = status.CloneAndPrepend("Unable to get preferred zone list");
    LOG(WARNING) << status.ToString();
  }

  // Comparator orders by cloud, region, zone and uuid fields.
  std::sort(local_descs.begin(), local_descs.end(), &TabletServerComparator);

  for (auto desc : local_descs) {
    if (desc->placement_uuid() == current_uuid) {
      if (ShouldHideTserverNodeFromDisplay(desc.get(), hide_dead_node_threshold_mins)) {
        continue;
      }
      const string time_since_hb = StringPrintf("%.1fs", desc->TimeSinceHeartbeat().ToSeconds());
      TSRegistrationPB reg = desc->GetRegistration();
      string host_port = GetHttpHostPortFromServerRegistration(reg.common());
      *output << "  <tr>\n";
      *output << "  <td>" << RegistrationToHtml(reg.common(), host_port) << "</br>";
      *output << "  " << desc->permanent_uuid();

      if (viewType == TServersViewType::kTServersDefaultView) {
        auto ci = reg.common().cloud_info();
        for (size_t i = 0; i < affinitized_zones.size(); i++) {
          if (affinitized_zones[i].find(ci) != affinitized_zones[i].end()) {
            *output << "</br>  Leader preference priority: " << i + 1;
            break;
          }
        }
      }

      *output << "</td><td>" << time_since_hb << "</td>";

      string status;
      string color = "Green";
      if (desc->IsLive()) {
        status = Format("$0:$1", kTserverAlive, UptimeString(desc->uptime_seconds()));
      } else {
        color = "Red";
        status = kTserverDead;
      }
      if (viewType == TServersViewType::kTServersDefaultView) {
        if (desc->IsBlacklisted(blacklist)) {
          color = color == "Green" ? kYBOrange : color;
          status += "</br>Blacklisted";
        }
        if (desc->IsBlacklisted(leader_blacklist)) {
          color = color == "Green" ? kYBOrange : color;
          status += "</br>Leader Blacklisted";
        }
      }

      *output << Format("    <td style=\"color:$0\">$1</td>", color, status);

      auto tserver = tablet_map->find(desc->permanent_uuid());
      bool no_tablets = tserver == tablet_map->end();

      if (viewType == TServersViewType::kTServersClocksView) {
        // Render physical time.
        const Timestamp p_ts(desc->physical_time());
        *output << "    <td>" << p_ts.ToHumanReadableTime() << "</td>";

        // Render the physical and logical components of the hybrid time.
        const HybridTime ht = desc->hybrid_time();
        const Timestamp h_ts(ht.GetPhysicalValueMicros());
        *output << "    <td>" << h_ts.ToHumanReadableTime();
        if (ht.GetLogicalValue()) {
          *output << " / Logical: " << ht.GetLogicalValue();
        }
        *output << "</td>";
        // Render the roundtrip time of previous heartbeat.
        double rtt_ms = desc->heartbeat_rtt().ToMicroseconds()/1000.0;
        *output << "    <td>" <<  StringPrintf("%.2fms", rtt_ms) << "</td>";
      } else {
        DCHECK_EQ(viewType, TServersViewType::kTServersDefaultView);
        *output << "    <td>" << (no_tablets ? 0
                : tserver->second.user_tablet_leaders + tserver->second.user_tablet_followers)
                << " / " << (no_tablets ? 0 : tserver->second.user_tablet_leaders) << "</td>";
        *output << "    <td>" << HumanizeBytes(desc->total_memory_usage()) << "</td>";
        *output << "    <td>" << desc->num_sst_files() << "</td>";
        *output << "    <td>" << HumanizeBytes(desc->total_sst_file_size()) << "</td>";
        *output << "    <td>" << HumanizeBytes(desc->uncompressed_sst_file_size()) << "</td>";
        *output << "    <td>" << desc->read_ops_per_sec() << "</td>";
        *output << "    <td>" << desc->write_ops_per_sec() << "</td>";
      }

      *output << "    <td>" << EscapeForHtmlToString(reg.common().cloud_info().placement_cloud())
              << "</td>";
      *output << "    <td>" << EscapeForHtmlToString(reg.common().cloud_info().placement_region())
              << "</td>";
      *output << "    <td>" << EscapeForHtmlToString(reg.common().cloud_info().placement_zone())
              << "</td>";

      if (viewType == TServersViewType::kTServersDefaultView) {
        *output << "    <td>" << (no_tablets ? 0
                : tserver->second.system_tablet_leaders + tserver->second.system_tablet_followers)
                << " / " << (no_tablets ? 0 : tserver->second.system_tablet_leaders) << "</td>";
        *output << "    <td>" << (no_tablets ? 0 : desc->num_live_replicas()) << "</td>";
      }

      *output << "  </tr>\n";
    }
  }
  *output << "</table>\n";
}

void MasterPathHandlers::DisplayTabletZonesTable(
  const ZoneTabletCounts::CloudTree& cloud_tree,
  std::stringstream* output
) {
  *output << "<h3>Tablet-Peers by Availability Zone</h3>\n"
          << "<table class='table table-striped'>\n"
          << "  <tr>\n"
          << "    <th>Cloud</th>\n"
          << "    <th>Region</th>\n"
          << "    <th>Zone</th>\n"
          << "    <th>Total Nodes</th>\n"
          << "    <th>User Tablet-Peers / Leaders</th>\n"
          << "    <th>System Tablet-Peers / Leaders</th>\n"
          << "    <th>Active Tablet-Peers</th>\n"
          << "  </tr>\n";

  for (const auto& cloud_iter : cloud_tree) {
    const auto& region_tree = cloud_iter.second;
    bool needs_new_row = false;

    int total_size_rows = 0;
    for (const auto& region_iter : region_tree) {
      total_size_rows += region_iter.second.size();
    }

    *output << "<tr>\n"
            << "  <td rowspan=\"" << total_size_rows <<"\">"
            << EscapeForHtmlToString(cloud_iter.first)
            << "</td>\n";

    for (const auto& region_iter : region_tree) {
      const auto& zone_tree = region_iter.second;

      if (needs_new_row) {
        *output << "<tr>\n";
        needs_new_row = false;
      }

      *output << "  <td rowspan=\"" << zone_tree.size() <<"\">"
              << EscapeForHtmlToString(region_iter.first)
              << "</td>\n";

      for (const auto& zone_iter : zone_tree) {
        const auto& counts = zone_iter.second;

        if (needs_new_row) {
          *output << "<tr>\n";
        }

        *output << "  <td>" << EscapeForHtmlToString(zone_iter.first) << "</td>\n";

        uint32_t user_leaders = counts.tablet_counts.user_tablet_leaders;
        uint32_t user_total = user_leaders + counts.tablet_counts.user_tablet_followers;
        uint32_t system_leaders = counts.tablet_counts.system_tablet_leaders;
        uint32_t system_total = system_leaders + counts.tablet_counts.system_tablet_followers;

        *output << "  <td>" << counts.node_count << "</td>\n"
                << "  <td>" << user_total << " / " << user_leaders << "</td>\n"
                << "  <td>" << system_total << " / " << system_leaders << "</td>\n"
                << "  <td>" << counts.active_tablets_count << "</td>\n"
                << "</tr>\n";

        needs_new_row = true;
      }
    }
  }

  *output << "</table>\n";
}

MasterPathHandlers::ZoneTabletCounts::CloudTree MasterPathHandlers::CalculateTabletCountsTree(
  const std::vector<std::shared_ptr<TSDescriptor>>& descriptors,
  const TabletCountMap& tablet_count_map
) {
  ZoneTabletCounts::CloudTree cloud_tree;

  for (const auto& descriptor : descriptors) {
    CloudInfoPB cloud_info = descriptor->GetRegistration().common().cloud_info();
    std::string cloud = cloud_info.placement_cloud();
    std::string region = cloud_info.placement_region();
    std::string zone = cloud_info.placement_zone();

    auto tablet_count_search = tablet_count_map.find(descriptor->permanent_uuid());
    ZoneTabletCounts counts = tablet_count_search == tablet_count_map.end()
        ? ZoneTabletCounts()
        : ZoneTabletCounts(tablet_count_search->second, descriptor->num_live_replicas());

    auto cloud_iter = cloud_tree.find(cloud);
    if (cloud_iter == cloud_tree.end()) {
      ZoneTabletCounts::RegionTree region_tree;
      ZoneTabletCounts::ZoneTree zone_tree;

      zone_tree.emplace(zone, std::move(counts));
      region_tree.emplace(region, std::move(zone_tree));
      cloud_tree.emplace(cloud, std::move(region_tree));
    } else {
      ZoneTabletCounts::RegionTree& region_tree = cloud_iter->second;

      auto region_iter = region_tree.find(region);
      if (region_iter == region_tree.end()) {
        ZoneTabletCounts::ZoneTree zone_tree;

        zone_tree.emplace(zone, std::move(counts));
        region_tree.emplace(region, std::move(zone_tree));
      } else {
        ZoneTabletCounts::ZoneTree& zone_tree = region_iter->second;

        auto zone_iter = zone_tree.find(zone);
        if (zone_iter == zone_tree.end()) {
          zone_tree.emplace(zone, std::move(counts));
        } else {
          zone_iter->second += counts;
        }
      }
    }
  }

  return cloud_tree;
}

void MasterPathHandlers::HandleTabletServers(const Webserver::WebRequest& req,
                                             Webserver::WebResponse* resp,
                                             TServersViewType viewType) {
  std::stringstream *output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  auto threshold_arg = req.parsed_args.find("live_threshold_mins");
  int hide_dead_node_threshold_override = FLAGS_hide_dead_node_threshold_mins;
  if (threshold_arg != req.parsed_args.end()) {
    hide_dead_node_threshold_override = atoi(threshold_arg->second.c_str());
  }

  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    *output << "<div class=\"alert alert-warning\">"
            << EscapeForHtmlToString(s.ToString()) << "</div>";
    return;
  }

  auto live_id = config.replication_info().live_replicas().placement_uuid();

  vector<std::shared_ptr<TSDescriptor> > descs;
  const auto& ts_manager = master_->ts_manager();
  ts_manager->GetAllDescriptors(&descs);

  // Get user and system tablet leader and follower counts for each TabletServer
  TabletCountMap tablet_map;
  CalculateTabletMap(&tablet_map);

  std::unordered_set<string> read_replica_uuids;
  for (auto desc : descs) {
    if (!read_replica_uuids.count(desc->placement_uuid()) && desc->placement_uuid() != live_id) {
      read_replica_uuids.insert(desc->placement_uuid());
    }
  }

  *output << std::setprecision(output_precision_);
  *output << "<h2>Tablet Servers</h2>\n";

  if (!live_id.empty()) {
    *output << "<h3 style=\"color:" << kYBDarkBlue << "\">Primary Cluster UUID: "
            << live_id << "</h3>\n";
  }

  TServerTable(output, viewType);
  TServerDisplay(live_id, &descs, &tablet_map, output, hide_dead_node_threshold_override,
                 viewType);

  for (const auto& read_replica_uuid : read_replica_uuids) {
    *output << "<h3 style=\"color:" << kYBDarkBlue << "\">Read Replica UUID: "
            << (read_replica_uuid.empty() ? kNoPlacementUUID : read_replica_uuid) << "</h3>\n";
    TServerTable(output, viewType);
    TServerDisplay(read_replica_uuid, &descs, &tablet_map, output,
                   hide_dead_node_threshold_override, viewType);
  }

  if (viewType == TServersViewType::kTServersDefaultView) {
    *output << "<p>  *Placement policy, Preferred zones, and Node Blacklist will affect the Peer "
               "and Leader distribution.</p>";

    if (master_->catalog_manager()->IsLoadBalancerEnabled()) {
      IsLoadBalancedRequestPB req;
      IsLoadBalancedResponsePB resp;
      Status load_balanced = master_->catalog_manager()->IsLoadBalanced(&req, &resp);
      if (load_balanced.ok()) {
        *output << "<h4 style=\"color:Green\"><i class='fa fa-tasks yb-dashboard-icon' "
                   "aria-hidden='true'></i>Cluster Load is Balanced</h4>\n";
      } else {
        *output
            << "<h4 style=\"color:" << kYBOrange
            << "\"><i class='fa fa-tasks yb-dashboard-icon' aria-hidden='true'></i>Cluster Load "
               "is not Balanced</h4>\n";
      }
    }
  }

  ZoneTabletCounts::CloudTree counts_tree = CalculateTabletCountsTree(descs, tablet_map);
  DisplayTabletZonesTable(counts_tree, output);
}

void MasterPathHandlers::HandleGetTserverStatus(const Webserver::WebRequest& req,
                                                Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  JsonWriter jw(output, JsonWriter::COMPACT);

  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(s.ToString());
    return;
  }

  vector<std::shared_ptr<TSDescriptor> > descs;
  const auto& ts_manager = master_->ts_manager();
  ts_manager->GetAllDescriptors(&descs);

  // Get user and system tablet leader and follower counts for each TabletServer.
  TabletCountMap tablet_map;
  CalculateTabletMap(&tablet_map);

  std::unordered_set<string> cluster_uuids;
  auto primary_uuid = config.replication_info().live_replicas().placement_uuid();
  cluster_uuids.insert(primary_uuid);
  for (auto desc : descs) {
    cluster_uuids.insert(desc->placement_uuid());
  }

  jw.StartObject();
  for (const auto& cur_uuid : cluster_uuids) {
    jw.String(cur_uuid);
    jw.StartObject();
    for (auto desc : descs) {
      if (desc->placement_uuid() == cur_uuid) {
        TSRegistrationPB reg = desc->GetRegistration();
        string host_port = GetHttpHostPortFromServerRegistration(reg.common());
        jw.String(host_port);

        jw.StartObject();

        // Some stats may be repeated as strings due to backwards compatability.
        jw.String("time_since_hb");
        jw.String(StringPrintf("%.1fs", desc->TimeSinceHeartbeat().ToSeconds()));
        jw.String("time_since_hb_sec");
        jw.Double(desc->TimeSinceHeartbeat().ToSeconds());

        if (desc->IsLive()) {
          jw.String("status");
          jw.String(kTserverAlive);

          jw.String("uptime_seconds");
          jw.Uint64(desc->uptime_seconds());
        } else {
          jw.String("status");
          jw.String(kTserverDead);

          jw.String("uptime_seconds");
          jw.Uint(0);
        }

        jw.String("ram_used");
        jw.String(HumanizeBytes(desc->total_memory_usage()));
        jw.String("ram_used_bytes");
        jw.Uint64(desc->total_memory_usage());

        jw.String("num_sst_files");
        jw.Uint64(desc->num_sst_files());

        jw.String("total_sst_file_size");
        jw.String(HumanizeBytes(desc->total_sst_file_size()));
        jw.String("total_sst_file_size_bytes");
        jw.Uint64(desc->total_sst_file_size());

        jw.String("uncompressed_sst_file_size");
        jw.String(HumanizeBytes(desc->uncompressed_sst_file_size()));
        jw.String("uncompressed_sst_file_size_bytes");
        jw.Uint64(desc->uncompressed_sst_file_size());

        jw.String("path_metrics");
        jw.StartArray();
        for(const auto& path_metric : desc->path_metrics()) {
          jw.StartObject();
          jw.String("path");
          jw.String(path_metric.first);
          jw.String("space_used");
          jw.Uint64(path_metric.second.used_space);
          jw.String("total_space_size");
          jw.Uint64(path_metric.second.total_space);
          jw.EndObject();
        }
        jw.EndArray();

        jw.String("read_ops_per_sec");
        jw.Double(desc->read_ops_per_sec());

        jw.String("write_ops_per_sec");
        jw.Double(desc->write_ops_per_sec());

        auto tserver = tablet_map.find(desc->permanent_uuid());
        uint user_tablets_total = 0;
        uint user_tablets_leaders = 0;
        uint system_tablets_total = 0;
        uint system_tablets_leaders = 0;
        int active_tablets = 0;
        if (!(tserver == tablet_map.end())) {
          user_tablets_total = tserver->second.user_tablet_leaders +
            tserver->second.user_tablet_followers;
          user_tablets_leaders = tserver->second.user_tablet_leaders;
          system_tablets_total = tserver->second.system_tablet_leaders +
            tserver->second.system_tablet_followers;
          system_tablets_leaders = tserver->second.system_tablet_leaders;
          active_tablets = desc->num_live_replicas();
        }
        jw.String("user_tablets_total");
        jw.Uint(user_tablets_total);

        jw.String("user_tablets_leaders");
        jw.Uint(user_tablets_leaders);

        jw.String("system_tablets_total");
        jw.Uint(system_tablets_total);

        jw.String("system_tablets_leaders");
        jw.Uint(system_tablets_leaders);

        jw.String("active_tablets");
        jw.Int(active_tablets);

        CloudInfoPB cloud_info = reg.common().cloud_info();

        jw.String("cloud");
        jw.String(cloud_info.placement_cloud());

        jw.String("region");
        jw.String(cloud_info.placement_region());

        jw.String("zone");
        jw.String(cloud_info.placement_zone());

        jw.EndObject();
      }
    }
    jw.EndObject();
  }
  jw.EndObject();
}

void MasterPathHandlers::HandleHealthCheck(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  // TODO: Lock not needed since other APIs handle it.  Refactor other functions accordingly
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);

  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(s.ToString());
    return;
  }
  auto replication_factor = master_->catalog_manager()->GetReplicationFactor();
  if (!replication_factor.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(replication_factor.status().ToString());
    return;
  }

  vector<std::shared_ptr<TSDescriptor> > descs;
  const auto* ts_manager = master_->ts_manager();
  ts_manager->GetAllDescriptors(&descs);

  const auto& live_placement_uuid = config.replication_info().live_replicas().placement_uuid();
  // Ignore read replica health for V1.

  vector<std::shared_ptr<TSDescriptor> > dead_nodes;
  uint64_t most_recent_uptime = std::numeric_limits<uint64_t>::max();

  jw.StartObject();
  {
    // Iterate TabletServers, looking for health anomalies.
    for (const auto & desc : descs) {
      if (desc->placement_uuid() == live_placement_uuid) {
        if (!desc->IsLive()) {
          // 1. Are any of the TS marked dead in the master?
          dead_nodes.push_back(desc);
        } else {
          // 2. Have any of the servers restarted lately?
          most_recent_uptime = min(most_recent_uptime, desc->uptime_seconds());
        }
      }
    }

    jw.String("dead_nodes");
    jw.StartArray();
    for (auto const & ts_desc : dead_nodes) {
      jw.String(ts_desc->permanent_uuid());
    }
    jw.EndArray();

    jw.String("most_recent_uptime");
    jw.Uint64(most_recent_uptime);

    auto time_arg = req.parsed_args.find("tserver_death_interval_msecs");
    int64 death_interval_msecs = 0;
    if (time_arg != req.parsed_args.end()) {
      death_interval_msecs = atoi(time_arg->second.c_str());
    }

    // Get all the tablets and add the tablet id for each tablet that has
    // replication locations lesser than 'replication_factor'.
    jw.String("under_replicated_tablets");
    jw.StartArray();

    auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);
    for (const auto& table : tables) {
      // Ignore tables that are neither user tables nor user indexes.
      // However there are a bunch of system tables that still need to be investigated:
      // 1. Redis system table.
      // 2. Transaction status table.
      // 3. Metrics table.
      if (!master_->catalog_manager()->IsUserTable(*table) &&
          table->GetTableType() != REDIS_TABLE_TYPE &&
          table->GetTableType() != TRANSACTION_STATUS_TABLE_TYPE &&
          !(table->namespace_id() == kSystemNamespaceId &&
            table->name() == kMetricsSnapshotsTableName)) {
        continue;
      }

      TabletInfos tablets = table->GetTablets();

      for (const auto& tablet : tablets) {
        auto replication_locations = tablet->GetReplicaLocations();

        if (replication_locations->size() < *replication_factor) {
          // These tablets don't have the required replication locations needed.
          jw.String(tablet->tablet_id());
          continue;
        }

        // Check if we have tablets that have replicas on the dead node.
        if (dead_nodes.size() == 0) {
          continue;
        }
        size_t recent_replica_count = 0;
        for (const auto& iter : *replication_locations) {
          if (std::find_if(dead_nodes.begin(),
                           dead_nodes.end(),
                           [iter, death_interval_msecs] (const auto& ts) {
                               return (ts->permanent_uuid() == iter.first &&
                                       ts->TimeSinceHeartbeat().ToMilliseconds() >
                                           death_interval_msecs); }) ==
                  dead_nodes.end()) {
            ++recent_replica_count;
          }
        }
        if (recent_replica_count < *replication_factor) {
          jw.String(tablet->tablet_id());
        }
      }
    }
    jw.EndArray();

    // TODO: Add these health checks in a subsequent diff
    //
    // 4. is the load balancer busy moving tablets/leaders around
    /* Use: Status IsLoadBalancerIdle(const IsLoadBalancerIdleRequestPB* req,
                                              IsLoadBalancerIdleResponsePB* resp);
     */
    // 5. do any of the TS have tablets they were not able to start up
  }
  jw.EndObject();
}

string MasterPathHandlers::GetParentTableOid(scoped_refptr<TableInfo> parent_table) {
  TableId t_id = parent_table->id();;
  if (parent_table->IsColocatedDbParentTable()) {
    // No YSQL parent id for colocated database parent table
    return "";
  }
  const auto parent_result = GetPgsqlTablegroupOidByTableId(t_id);
  RETURN_NOT_OK_RET(ResultToStatus(parent_result), "");
  return std::to_string(*parent_result);
}

string GetOnDiskSizeInHtml(const TabletReplicaDriveInfo &info) {
  std::ostringstream disk_size_html;
  disk_size_html << "<ul>"
                 << "<li>" << "Total: "
                 << HumanReadableNumBytes::ToString(info.sst_files_size + info.wal_files_size)
                 << "<li>" << "WAL Files: "
                 << HumanReadableNumBytes::ToString(info.wal_files_size)
                 << "<li>" << "SST Files: "
                 << HumanReadableNumBytes::ToString(info.sst_files_size)
                 << "<li>" << "SST Files Uncompressed: "
                 << HumanReadableNumBytes::ToString(info.uncompressed_sst_file_size)
                 << "</ul>";

  return disk_size_html.str();
}

void MasterPathHandlers::HandleCatalogManager(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp, bool only_user_tables) {
  std::stringstream* output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kAll);

  typedef map<string, string[kNumColumns]> StringMap;

  // The first stores user tables, the second index tables, the third parent tables,
  // and the fourth system tables.
  StringMap ordered_tables[kNumTypes];
  bool has_tablegroups[kNumTypes];
  bool has_colocated_tables[kNumTypes];
  bool show_missing_size_footer[kNumTypes];
  for (int i = 0; i < kNumTypes; ++i) {
    ordered_tables[i] = StringMap();
    show_missing_size_footer[i] = false;
    has_tablegroups[i] = false;
    has_colocated_tables[i] = false;
  }

  for (const auto& table : tables) {
    auto table_locked = table->LockForRead();
    if (!table_locked->is_running()) {
      continue;
    }

    string table_uuid = table->id();
    string keyspace = master_->catalog_manager()->GetNamespaceName(table->namespace_id());

    TableType table_cat = GetTableType(*table);
    // Skip non-user tables if we should.
    if (only_user_tables && table_cat != kUserIndex && table_cat != kUserTable) {
      continue;
    }

    auto& table_row = ordered_tables[table_cat][table_uuid];
    table_row[kKeyspace] = EscapeForHtmlToString(keyspace);
    string href_table_id = table_uuid;
    string table_name = table_locked->name();
    table_row[kState] = SysTablesEntryPB_State_Name(table_locked->pb.state());
    table_row[kHidden] = table_locked->is_hidden() ? "true" : "false";
    Capitalize(&table_row[kState]);
    table_row[kMessage] = EscapeForHtmlToString(table_locked->pb.state_msg());

    if (table->GetTableType() == PGSQL_TABLE_TYPE && table_cat != kParentTable) {
      const auto result = GetPgsqlTableOid(table_uuid);
      if (result.ok()) {
        table_row[kYsqlOid] = std::to_string(*result);
      } else {
        LOG(ERROR) << "Failed to get OID of '" << table_uuid << "' ysql table";
      }

      const auto& schema = table_locked->schema();
      if (schema.has_colocated_table_id() && schema.colocated_table_id().has_colocation_id()) {
        table_row[kColocationId] = Format("$0", schema.colocated_table_id().colocation_id());
        has_colocated_tables[table_cat] = true;
      }

      auto colocated_tablet = table->GetColocatedUserTablet();
      if (colocated_tablet) {
        const auto parent_table = colocated_tablet->table();
        table_row[kParentOid] = GetParentTableOid(parent_table);
        has_tablegroups[table_cat] = true;
      }
    } else if (table_cat == kParentTable) {
      // Colocated parent table.
      table_row[kYsqlOid] = GetParentTableOid(table);
      std::string parent_name = table_locked->name();

      // Insert a newline in id and name to wrap long tablegroup text.
      table_name = parent_name.insert(32, "\n");
      table_uuid = table_uuid.insert(32, "\n");
    }

    // System tables and colocated user tables do not have size info
    if (table_cat != kSystemTable && !table->IsColocatedUserTable()) {
      TabletReplicaDriveInfo aggregated_drive_info;
      auto tablets = table->GetTablets();
      bool table_has_missing_size = false;
      for (const auto& tablet : tablets) {
        auto drive_info = tablet->GetLeaderReplicaDriveInfo();
        if (drive_info.ok()) {
          aggregated_drive_info.wal_files_size += drive_info.get().wal_files_size;
          aggregated_drive_info.sst_files_size += drive_info.get().sst_files_size;
          aggregated_drive_info.uncompressed_sst_file_size +=
              drive_info.get().uncompressed_sst_file_size;
        } else {
          show_missing_size_footer[table_cat] = true;
          table_has_missing_size = true;
        }
      }

      table_row[kOnDiskSize] = GetOnDiskSizeInHtml(aggregated_drive_info);
      if (table_has_missing_size) {
        table_row[kOnDiskSize] += "*";
      }
    }

    table_row[kTableName] = Format(
        "<a href=\"/table?id=$0\">$1</a>",
        UrlEncodeToString(href_table_id),
        EscapeForHtmlToString(table_name));

    table_row[kUuid] = EscapeForHtmlToString(table_uuid);
  }

  for (int type_index = 0; type_index < kNumTypes; ++type_index) {
    if (only_user_tables && (type_index != kUserIndex && type_index != kUserTable)) {
      continue;
    }
    if (ordered_tables[type_index].empty() && type_index == kParentTable) {
      continue;
    }

    (*output) << "<div class='panel panel-default'>\n"
              << "<div class='panel-heading'><h2 class='panel-title'>" << table_type_[type_index]
              << " tables</h2></div>\n";
    (*output) << "<div class='panel-body table-responsive'>";

    if (ordered_tables[type_index].empty()) {
      (*output) << "There are no " << static_cast<char>(tolower(table_type_[type_index][0]))
                << table_type_[type_index].substr(1) << " tables.\n";
    } else {
      (*output) << "<table class='table table-responsive'>\n";
      (*output) << "  <tr><th>Keyspace</th>\n"
                << "  <th>Table Name</th>\n"
                << "  <th>State</th>\n"
                << "  <th>Message</th>\n"
                << "  <th>UUID</th>\n"
                << "  <th>YSQL OID</th>\n"
                << "  <th>Hidden</th>\n";

      if (type_index == kUserTable || type_index == kUserIndex) {
        if (has_tablegroups[type_index]) {
          (*output) << "  <th>Parent OID</th>\n";
        }

        if (has_colocated_tables[type_index]) {
          (*output) << "  <th>Colocation ID</th>\n";
        }
      }

      if (type_index != kSystemTable) {
        (*output) << "  <th>On-disk size</th></tr>\n";
      }

      for (const StringMap::value_type& table : ordered_tables[type_index]) {
        (*output) << Format(
            "<tr>"
            "<td>$0</td>"
            "<td>$1</td>"
            "<td>$2</td>"
            "<td>$3</td>"
            "<td>$4</td>"
            "<td>$5</td>"
            "<td>$6</td>",
            table.second[kKeyspace],
            table.second[kTableName],
            table.second[kState],
            table.second[kMessage],
            table.second[kUuid],
            table.second[kYsqlOid],
            table.second[kHidden]);

        if (type_index == kUserTable || type_index == kUserIndex) {
          if (has_tablegroups[type_index]) {
            (*output) << Format("<td>$0</td>", table.second[kParentOid]);
          }

          if (has_colocated_tables[type_index]) {
            (*output) << Format("<td>$0</td>", table.second[kColocationId]);
          }
        }

        if (type_index != kSystemTable) {
          (*output) << Format("<td>$0</td>", table.second[kOnDiskSize]);
        }

        (*output) << "</tr>\n";
      }

      (*output) << "</table>\n";

      if (show_missing_size_footer[type_index]) {
        (*output) << "<p>* Some tablets did not provide disk size estimates,"
                  << " and were not added to the displayed totals.</p>";
      }
    }
    (*output) << "</div> <!-- panel-body -->\n";
    (*output) << "</div> <!-- panel -->\n";
  }
}

void MasterPathHandlers::HandleCatalogManagerJSON(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  bool only_user_tables = ParseLeadingBoolValue(
      FindWithDefault(req.parsed_args, "only_user_tables", ""), false);

  JsonWriter jw(output, JsonWriter::COMPACT);
  jw.StartObject();

  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kAll);

  // A struct that holds data for each JSON object to be returned.
  // Each JSON object represents one table.
  struct CatalogManagerJSONData {
    string keyspace;
    string table_name;
    string state;
    string message;
    string uuid;
    string ysql_oid;
    string parent_oid;
    string colocation_id;
    TabletReplicaDriveInfo on_disk_size;
    bool has_missing_size;
    bool hidden;
  };

  typedef map<string, CatalogManagerJSONData> DataMap;

  // The first stores user tables, the second index tables, the third parent tables,
  // and the fourth system tables.
  DataMap ordered_tables[kNumTypes];
  for (int i = 0; i < kNumTypes; ++i) {
    ordered_tables[i] = DataMap();
  }

  for (const auto& table : tables) {
    auto table_locked = table->LockForRead();
    if (!table_locked->is_running()) {
      continue;
    }

    string table_uuid = table->id();
    string keyspace = master_->catalog_manager()->GetNamespaceName(table->namespace_id());

    TableType table_cat = GetTableType(*table);
    if (only_user_tables && table_cat != kUserIndex && table_cat != kUserTable) {
      continue;
    }

    auto& table_row = ordered_tables[table_cat][table_uuid];
    table_row.keyspace = keyspace;
    string href_table_id = table_uuid;
    string table_name = table_locked->name();
    table_row.state = SysTablesEntryPB_State_Name(table_locked->pb.state());
    table_row.hidden = table_locked->is_hidden();
    table_row.message = table_locked->pb.state_msg();

    if (table->GetTableType() == PGSQL_TABLE_TYPE && table_cat != kParentTable) {
      const auto result = GetPgsqlTableOid(table_uuid);
      if (result.ok()) {
        table_row.ysql_oid = std::to_string(*result);
      } else {
        LOG(ERROR) << "Failed to get OID of '" << table_uuid << "' ysql table";
      }

      const auto& schema = table_locked->schema();
      if (schema.has_colocated_table_id() && schema.colocated_table_id().has_colocation_id()) {
        table_row.colocation_id = Format("$0", schema.colocated_table_id().colocation_id());
      }

      auto colocated_tablet = table->GetColocatedUserTablet();
      if (colocated_tablet) {
        const auto parent_table = colocated_tablet->table();
        table_row.parent_oid = GetParentTableOid(parent_table);
      }
    } else if (table_cat == kParentTable) {
      // Colocated parent table.
      table_row.ysql_oid = GetParentTableOid(table);
    }

    // System tables and colocated user tables do not have size info.
    if (table_cat != kSystemTable && !table->IsColocatedUserTable()) {
      TabletReplicaDriveInfo aggregated_drive_info;
      auto tablets = table->GetTablets();
      bool table_has_missing_size = false;
      for (const auto& tablet : tablets) {
        auto drive_info = tablet->GetLeaderReplicaDriveInfo();
        if (drive_info.ok()) {
          aggregated_drive_info.wal_files_size += drive_info.get().wal_files_size;
          aggregated_drive_info.sst_files_size += drive_info.get().sst_files_size;
          aggregated_drive_info.uncompressed_sst_file_size +=
              drive_info.get().uncompressed_sst_file_size;
        } else {
          table_has_missing_size = true;
        }
      }

      table_row.on_disk_size = aggregated_drive_info;
      table_row.has_missing_size = table_has_missing_size;
    }

    table_row.table_name = table_name;
    table_row.uuid = table_uuid;
  }

  for (int type_index = 0; type_index < kNumTypes; ++type_index) {
    if (only_user_tables && (type_index != kUserIndex && type_index != kUserTable)) {
      continue;
    }
    if (ordered_tables[type_index].empty() && type_index == kParentTable) {
      continue;
    }

    jw.String(ToLowerCase(table_type_[type_index]));
    jw.StartArray();

    for (const DataMap::value_type& table : ordered_tables[type_index]) {
      jw.StartObject();
      jw.String("keyspace");
      jw.String(table.second.keyspace);
      jw.String("table_name");
      jw.String(table.second.table_name);
      jw.String("state");
      jw.String(table.second.state);
      jw.String("message");
      jw.String(table.second.message);
      jw.String("uuid");
      jw.String(table.second.uuid);
      jw.String("ysql_oid");
      jw.String(table.second.ysql_oid);
      jw.String("hidden");
      jw.Bool(table.second.hidden);

      if (type_index == kUserTable || type_index == kUserIndex) {
        if (!table.second.parent_oid.empty()) {
          jw.String("parent_oid");
          jw.String(table.second.parent_oid);
        }

        if (!table.second.colocation_id.empty()) {
          jw.String("colocation_id");
          jw.String(table.second.colocation_id);
        }
      }

      if (type_index != kSystemTable) {
        jw.String("on_disk_size");
        jw.StartObject();
        jw.String("wal_files_size");
        jw.String(HumanReadableNumBytes::ToString(table.second.on_disk_size.wal_files_size));
        jw.String("wal_files_size_bytes");
        jw.Uint64(table.second.on_disk_size.wal_files_size);
        jw.String("sst_files_size");
        jw.String(HumanReadableNumBytes::ToString(table.second.on_disk_size.sst_files_size));
        jw.String("sst_files_size_bytes");
        jw.Uint64(table.second.on_disk_size.sst_files_size);
        jw.String("uncompressed_sst_file_size");
        jw.String(
            HumanReadableNumBytes::ToString(table.second.on_disk_size.uncompressed_sst_file_size));
        jw.String("uncompressed_sst_file_size_bytes");
        jw.Uint64(table.second.on_disk_size.uncompressed_sst_file_size);
        jw.String("has_missing_size");
        jw.Bool(table.second.has_missing_size);
        jw.EndObject();
      }
      jw.EndObject();
    }
    jw.EndArray();
  }
  jw.EndObject();
}

void MasterPathHandlers::HandleNamespacesHTML(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp, bool only_user_namespaces) {
  std::stringstream* output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  std::vector<scoped_refptr<NamespaceInfo>> namespaces;
  master_->catalog_manager()->GetAllNamespaces(&namespaces);

  typedef map<string, string[kNumNamespaceColumns]> StringMap;

  auto user_namespaces = std::make_unique<StringMap>();
  auto system_namespaces = std::make_unique<StringMap>();

  for (const auto& namespace_info : namespaces) {
    const auto lock = namespace_info->LockForRead();
    auto& namespaces = IsSystemNamespace(lock->name()) ? system_namespaces : user_namespaces;
    auto& namespace_row = (*namespaces)[namespace_info->id()];
    namespace_row[kNamespaceName] = EscapeForHtmlToString(lock->name());
    namespace_row[kNamespaceId] = EscapeForHtmlToString(namespace_info->id());
    namespace_row[kNamespaceLanguage] = DatabasePrefix(lock->database_type());
    namespace_row[kNamespaceState] = SysNamespaceEntryPB_State_Name(lock->pb.state());
    namespace_row[kNamespaceColocated] = lock->colocated() ? "true" : "false";
  }

  const auto GenerateHTML = [&] (const std::unique_ptr<StringMap>& namespaces,
      const std::string& name) {
    (*output) << "<div class='panel panel-default'>\n"
              << "<div class='panel-heading'><h2 class='panel-title'>" << name
              << " Namespaces</h2></div>\n";
    (*output) << "<div class='panel-body table-responsive'>";

    if (namespaces->empty()) {
      (*output) << "There are no " << tolower(name[0])
                << name.substr(1) << " namespaces.\n";
    } else {
      (*output) << "<table class='table table-responsive'>\n";
      (*output) << "  <tr><th>Namespace Name</th>\n"
                << "  <th>Namespace Id</th>\n"
                << "  <th>Language</th>\n"
                << "  <th>State</th>\n"
                << "  <th>Colocated</th>\n";

      for (const auto& namespace_row : *namespaces) {
        (*output) << Substitute(
            "<tr>"
            "<td>$0</td>"
            "<td>$1</td>"
            "<td>$2</td>"
            "<td>$3</td>"
            "<td>$4</td>",
            namespace_row.second[kNamespaceName],
            namespace_row.second[kNamespaceId],
            namespace_row.second[kNamespaceLanguage],
            namespace_row.second[kNamespaceState],
            namespace_row.second[kNamespaceColocated]);
      }

      (*output) << "</table>\n";

    }
    (*output) << "</div> <!-- panel-body -->\n";
    (*output) << "</div> <!-- panel -->\n";
  };

  GenerateHTML(user_namespaces, "User");
  if (!only_user_namespaces) {
    GenerateHTML(system_namespaces, "System");
  }
}

void MasterPathHandlers::HandleNamespacesJSON(const Webserver::WebRequest& req,
    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  JsonWriter jw(output, JsonWriter::COMPACT);

  std::vector<scoped_refptr<NamespaceInfo>> namespaces;
  master_->catalog_manager()->GetAllNamespaces(&namespaces);

  const auto generateJSON = [&] (bool for_system_namespaces) {
    jw.String(for_system_namespaces ? "System Namespaces" : "User Namespaces");
    jw.StartArray();
    for (const auto& ns : namespaces) {
      const auto lock = ns->LockForRead();
      if (for_system_namespaces == IsSystemNamespace(ns->name())) {
        jw.StartObject();
        jw.String("name");
        jw.String(lock->name());
        jw.String("id");
        jw.String(ns->id());
        jw.String("language");
        jw.String(DatabasePrefix(lock->database_type()));
        jw.String("state");
        jw.String(SysNamespaceEntryPB_State_Name(lock->pb.state()));
        jw.String("colocated");
        jw.String(lock->colocated() ? "true" : "false");
        jw.EndObject();
      }
    }
    jw.EndArray();
  };

  jw.StartObject();
  generateJSON(false /* for_system_namespace */);
  generateJSON(true);
  jw.EndObject();
}

namespace {

bool CompareByHost(const TabletReplica& a, const TabletReplica& b) {
    return a.ts_desc->permanent_uuid() < b.ts_desc->permanent_uuid();
}

}  // anonymous namespace


void MasterPathHandlers::HandleTablePage(const Webserver::WebRequest& req,
                                         Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  // True if table_id, false if (keyspace, table).
  const auto arg_end = req.parsed_args.end();
  auto id_arg = req.parsed_args.find("id");
  auto keyspace_arg = arg_end;
  auto table_arg = arg_end;
  if (id_arg == arg_end) {
    keyspace_arg = req.parsed_args.find("keyspace_name");
    table_arg = req.parsed_args.find("table_name");
    if (keyspace_arg == arg_end || table_arg == arg_end) {
      *output << " Missing 'id' argument or 'keyspace_name, table_name' argument pair.";
      *output << " Arguments must either contain the table id or the "
                 " (keyspace_name, table_name) pair.";
      return;
    }
  }

  scoped_refptr<TableInfo> table;

  if (id_arg != arg_end) {
    table = master_->catalog_manager()->GetTableInfo(id_arg->second);
  } else {
    const auto keyspace_type_arg = req.parsed_args.find("keyspace_type");
    const auto keyspace_type = (keyspace_type_arg == arg_end
        ? GetDefaultDatabaseType(keyspace_arg->second)
        : DatabaseTypeByName(keyspace_type_arg->second));
    if (keyspace_type == YQLDatabase::YQL_DATABASE_UNKNOWN) {
      *output << "Wrong keyspace_type found '" << EscapeForHtmlToString(keyspace_type_arg->second)
              << "'. Possible values are: " << kDBTypeNameCql << ", "
              << kDBTypeNamePgsql << ", " << kDBTypeNameRedis << ".";
      return;
    }
    table = master_->catalog_manager()->GetTableInfoFromNamespaceNameAndTableName(
        keyspace_type, keyspace_arg->second, table_arg->second);
  }

  if (table == nullptr) {
    *output << "Table not found!";
    return;
  }

  Schema schema;
  dockv::PartitionSchema partition_schema;
  NamespaceName keyspace_name;
  TableName table_name;
  TabletInfos tablets;
  {
    auto l = table->LockForRead();
    keyspace_name = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    table_name = l->name();
    *output << "<h1>Table: "
            << EscapeForHtmlToString(server::TableLongName(keyspace_name, table_name))
            << " ("<< table->id() <<") </h1>\n";

    *output << "<table class='table table-striped'>\n";
    *output << "  <tr><td>Version:</td><td>" << l->pb.version() << "</td></tr>\n";

    *output << "  <tr><td>Type:</td><td>" << TableType_Name(l->pb.table_type())
            << "</td></tr>\n";

    string state = SysTablesEntryPB_State_Name(l->pb.state());
    Capitalize(&state);
    *output << "  <tr><td>State:</td><td>"
            << state
            << EscapeForHtmlToString(l->pb.state_msg())
            << "</td></tr>\n";

    TablespaceId tablespace_id;
    auto result = master_->catalog_manager()->GetTablespaceForTable(table);
    if (result.ok()) {
      // If the table is associated with a tablespace, display tablespace, otherwise
      // just display replication info.
      if (result.get()) {
        tablespace_id = result.get().value();
        *output << "  <tr><td>Tablespace OID:</td><td>"
                << GetPgsqlTablespaceOid(tablespace_id)
                << "  </td></tr>\n";
      }
      *output << "  <tr><td>Replication Info:</td><td>";
      auto replication_info = master_->catalog_manager()->GetTableReplicationInfo(
          l->pb.replication_info(), tablespace_id);
      if (replication_info.ok()) {
        *output << "    <pre class=\"prettyprint\">"
                << EscapeForHtmlToString(replication_info->DebugString()) << "</pre>";
      } else {
        LOG(WARNING) << replication_info.status().CloneAndPrepend(
            "Unable to determine Tablespace information.");
        *output << "  Unable to determine Tablespace information.";
      }
      *output << "  </td></tr>\n";
    } else {
      // The table was associated with a tablespace, but that tablespace was not found.
      *output << "  <tr><td>Replication Info:</td><td>";
      if (FLAGS_ysql_tablespace_info_refresh_secs > 0) {
        *output << "  Tablespace information not available now, please try again after "
                << FLAGS_ysql_tablespace_info_refresh_secs << " seconds. ";
      } else {
        *output << "  Tablespace information is not available as the periodic task "
                << "  used to refresh it is disabled.";

      }
      *output << "  </td></tr>\n";
    }

    if (l->has_ysql_ddl_txn_verifier_state()) {
      auto result = FullyDecodeTransactionId(l->pb.transaction().transaction_id());
      *output << "  <tr><td>Verifying Ysql DDL Transaction: </td><td>";
      if (result)
        *output << result.get();
      else
        *output << "Failed to decode transaction with error:" << result;
      *output << "  </td></tr>\n";

      const bool contains_alter = l->pb.ysql_ddl_txn_verifier_state(0).contains_alter_table_op();
      *output << "  <tr><td>Ysql DDL transaction Operations: </td><td>"
              << (l->is_being_created_by_ysql_ddl_txn() ? "Create " : "")
              << (contains_alter ? " Alter " : "")
              << (l->is_being_deleted_by_ysql_ddl_txn() ? "Delete" : "")
              << "  </td></tr>\n";
      if (contains_alter && !l->is_being_created_by_ysql_ddl_txn()) {
        *output << "  <tr><td>Previous table name: </td><td>"
                << EscapeForHtmlToString(l->pb.ysql_ddl_txn_verifier_state(0).previous_table_name())
                << "  </td></tr>\n </table>\n";
        Schema previous_schema;
        Status s =
            SchemaFromPB(l->pb.ysql_ddl_txn_verifier_state(0).previous_schema(), &previous_schema);
        if (s.ok()) {
          *output << "  Previous Schema\n";
          server::HtmlOutputSchemaTable(previous_schema, output);
          *output << "  Current Schema\n";
        }
      } else {
        *output << "</table>\n";
      }
    } else {
      *output << "</table>\n";
    }

    Status s = SchemaFromPB(l->pb.schema(), &schema);
    if (s.ok()) {
      s = dockv::PartitionSchema::FromPB(l->pb.partition_schema(), schema, &partition_schema);
    }
    if (!s.ok()) {
      *output << "Unable to decode partition schema: " << EscapeForHtmlToString(s.ToString());
      return;
    }
    tablets = table->GetTablets(IncludeInactive::kTrue);
  }

  server::HtmlOutputSchemaTable(schema, output);

  bool has_deleted_tablets = false;
  for (const auto& tablet : tablets) {
    if (tablet->LockForRead()->is_deleted()) {
      has_deleted_tablets = true;
      break;
    }
  }

  const bool show_deleted_tablets =
      has_deleted_tablets ? req.parsed_args.find("show_deleted") != req.parsed_args.end() : false;

  if (has_deleted_tablets) {
    *output << Format(
        "<a href=\"$0?id=$1$2\">$3 deleted tablets</a>",
        EscapeForHtmlToString(req.redirect_uri),
        EscapeForHtmlToString(table->id()),
        EscapeForHtmlToString(show_deleted_tablets ? "" : "&show_deleted"),
        EscapeForHtmlToString(show_deleted_tablets ? "Hide" : "Show"));
  }

  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Tablet ID</th><th>Partition</th><th>SplitDepth</th><th>State</th>"
             "<th>Hidden</th><th>Message</th><th>RaftConfig</th></tr>\n";
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    if (!show_deleted_tablets && tablet->LockForRead()->is_deleted()) {
      continue;
    }
    auto locations = tablet->GetReplicaLocations();
    vector<TabletReplica> sorted_locations;
    AppendValuesFromMap(*locations, &sorted_locations);
    std::sort(sorted_locations.begin(), sorted_locations.end(), &CompareByHost);

    auto l = tablet->LockForRead();

    dockv::Partition partition;
    dockv::Partition::FromPB(l->pb.partition(), &partition);

    string state = SysTabletsEntryPB_State_Name(l->pb.state());
    Capitalize(&state);
    *output << Format(
        "<tr><th>$0</th><td>$1</td><td>$2</td><td>$3</td><td>$4</td><td>$5</td><td>$6</td></tr>\n",
        tablet->tablet_id(),
        EscapeForHtmlToString(partition_schema.PartitionDebugString(partition, schema)),
        l->pb.split_depth(),
        state,
        l->is_hidden(),
        EscapeForHtmlToString(l->pb.state_msg()),
        RaftConfigToHtml(sorted_locations, tablet->tablet_id()));
  }
  *output << "</table>\n";

  HtmlOutputTasks(table->GetTasks(), output);
}

void JsonOutputTask(const std::shared_ptr<MonitoredTask>& task, JsonWriter *jw) {
  double time_since_started = 0;
  if (task->start_timestamp().Initialized()) {
    time_since_started =
        MonoTime::Now().GetDeltaSince(task->start_timestamp()).ToSeconds();
  }
  double running_secs = 0;
  if (task->completion_timestamp().Initialized()) {
    running_secs = task->completion_timestamp().GetDeltaSince(
        task->start_timestamp()).ToSeconds();
  } else if (task->start_timestamp().Initialized()) {
    running_secs = MonoTime::Now().GetDeltaSince(
        task->start_timestamp()).ToSeconds();
  }

  jw->String("task_type");
  jw->String(task->type_name());
  jw->String("task_state");
  jw->String(ToString(task->state()));
  jw->String("task_time_since_started");
  jw->String(HumanReadableElapsedTime::ToShortString(time_since_started));
  jw->String("task_running_secs");
  jw->String(HumanReadableElapsedTime::ToShortString(running_secs));
  jw->String("task_description");
  jw->String(task->description());
}

void JsonOutputTasks(const std::unordered_set<std::shared_ptr<MonitoredTask>>& tasks,
                     JsonWriter *jw) {
  jw->String("Tasks");
  jw->StartArray();
  for (const auto& task : tasks) {
    JsonOutputTask(task, jw);
  }
  jw->EndArray();
}

void JsonOutputSchemaTable(const Schema& schema, JsonWriter* jw) {
  jw->String("columns");
  jw->StartArray();
  for (size_t i = 0; i < schema.num_columns(); i++) {
    jw->StartObject();
    const ColumnSchema& col = schema.column(i);
    jw->String("column");
    jw->String(EscapeForHtmlToString(col.name()));
    jw->String("id");
    jw->String(schema.column_id(i).ToString());
    jw->String("type");
    jw->String(col.TypeToString());
    jw->EndObject();
  }
  jw->EndArray();
}

string TSDescriptorToJson(const TSDescriptor& desc,
                          const std::string& tablet_id) {
  TSRegistrationPB reg = desc.GetRegistration();

  auto public_http_hp = GetPublicHttpHostPort(reg.common());
  if (public_http_hp) {
    return Format(
        "$0://$1/tablet?id=$2",
        GetProtocol(),
        HostPortPBToString(*public_http_hp),
        UrlEncodeToString(tablet_id));
  } else {
    return EscapeForHtmlToString(desc.permanent_uuid());
  }
}

void RaftConfigToJson(const std::vector<TabletReplica>& locations,
                      const std::string& tablet_id,
                      JsonWriter *jw) {
  jw->String("locations");
  jw->StartArray();
  for (const TabletReplica& location : locations) {
    jw->StartObject();
    jw->String("uuid");
    jw->String(location.ts_desc->permanent_uuid());
    jw->String("role");
    jw->String(PeerRole_Name(location.role));
    jw->String("location");
    jw->String(TSDescriptorToJson(*location.ts_desc, tablet_id));
    jw->EndObject();
  }
  jw->EndArray();
}

void MasterPathHandlers::HandleTablePageJSON(const Webserver::WebRequest& req,
                                             Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  JsonWriter jw(output, JsonWriter::COMPACT);
  jw.StartObject();

  // True if table_id, false if (keyspace, table).
  const auto arg_end = req.parsed_args.end();
  auto id_arg = req.parsed_args.find("id");
  auto keyspace_arg = arg_end;
  auto table_arg = arg_end;
  if (id_arg == arg_end) {
    keyspace_arg = req.parsed_args.find("keyspace_name");
    table_arg = req.parsed_args.find("table_name");
    if (keyspace_arg == arg_end || table_arg == arg_end) {
      jw.String("error");
      jw.String("Missing 'id' argument or 'keyspace_name, table_name' argument pair. "
                "Arguments must either contain the table id or the "
                "(keyspace_name, table_name) pair.");
      jw.EndObject();
      return;
    }
  }

  scoped_refptr<TableInfo> table;

  if (id_arg != arg_end) {
    table = master_->catalog_manager()->GetTableInfo(id_arg->second);
  } else {
    const auto keyspace_type_arg = req.parsed_args.find("keyspace_type");
    const auto keyspace_type = (keyspace_type_arg == arg_end
        ? GetDefaultDatabaseType(keyspace_arg->second)
        : DatabaseTypeByName(keyspace_type_arg->second));
    if (keyspace_type == YQLDatabase::YQL_DATABASE_UNKNOWN) {
      jw.String("error");
      jw.String(
          Format("Wrong keyspace_type found '$0'. Possible values are: $1, $2, $3",
                 keyspace_type_arg->second, kDBTypeNameCql, kDBTypeNamePgsql, kDBTypeNameRedis));
      jw.EndObject();
      return;
    }
    table = master_->catalog_manager()->GetTableInfoFromNamespaceNameAndTableName(
        keyspace_type, keyspace_arg->second, table_arg->second);
  }

  if (table == nullptr) {
    jw.String("error");
    jw.String("Table not found!");
    jw.EndObject();
    return;
  }

  Schema schema;
  dockv::PartitionSchema partition_schema;
  TabletInfos tablets;
  {
    NamespaceName keyspace_name;
    TableName table_name;
    auto l = table->LockForRead();
    keyspace_name = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    table_name = l->name();
    jw.String("table_name");
    jw.String(server::TableLongName(keyspace_name, table_name));
    jw.String("table_id");
    jw.String(table->id());
    jw.String("table_version");
    jw.Uint64(l->pb.version());
    jw.String("table_type");
    jw.String(TableType_Name(l->pb.table_type()));

    string state = SysTablesEntryPB_State_Name(l->pb.state());
    Capitalize(&state);
    jw.String("table_state");
    jw.String(state);
    jw.String("table_state_message");
    jw.String(l->pb.state_msg());

    TablespaceId tablespace_id;
    auto result = master_->catalog_manager()->GetTablespaceForTable(table);
    if (result.ok()) {
      // If the table is associated with a tablespace, display tablespace, otherwise
      // just display replication info.
      if (result.get()) {
        tablespace_id = result.get().value();
        jw.String("table_tablespace_oid");
        jw.Uint64(GetPgsqlTablespaceOid(tablespace_id).get());
      }
      auto replication_info = master_->catalog_manager()->GetTableReplicationInfo(
          l->pb.replication_info(), tablespace_id);
      jw.String("table_replication_info");
      if (replication_info.ok()) {
        jw.Protobuf(replication_info.get());
      } else {
        LOG(WARNING) << replication_info.status().CloneAndPrepend(
            "Unable to determine Tablespace information.");
        jw.String("Unable to determine Tablespace information");
      }
    } else {
      // The table was associated with a tablespace, but that tablespace was not found.
      jw.String("table_replication_info");
      if (FLAGS_ysql_tablespace_info_refresh_secs > 0) {
        jw.String("Tablespace information not available now, please try again after " +
                  std::to_string(FLAGS_ysql_tablespace_info_refresh_secs) + " seconds");
      } else {
        jw.String("Tablespace information is not available as the periodic task "
                  "used to refresh it is disabled.");

      }
    }

    if (l->has_ysql_ddl_txn_verifier_state()) {
      auto result = FullyDecodeTransactionId(l->pb.transaction().transaction_id());
      jw.String("table_transaction_verifier_state");
      if (result)
        jw.String(result.get().ToString());
      else
        jw.String("Failed to decode transaction with error:" + result.ToString());

      jw.String("table_transaction_ddl");
      const bool contains_alter = l->pb.ysql_ddl_txn_verifier_state(0).contains_alter_table_op();
      std::stringstream ddls;
      ddls << (l->is_being_created_by_ysql_ddl_txn() ? "Create " : "")
           << (contains_alter ? " Alter " : "")
           << (l->is_being_deleted_by_ysql_ddl_txn() ? "Delete" : "");
      jw.String(ddls.str());
      if (contains_alter && !l->is_being_created_by_ysql_ddl_txn()) {
        jw.String("table_transaction_ddl_previous_table_name");
        jw.String(l->pb.ysql_ddl_txn_verifier_state(0).previous_table_name());
        Schema previous_schema;
        Status s =
            SchemaFromPB(l->pb.ysql_ddl_txn_verifier_state(0).previous_schema(), &previous_schema);
        if (s.ok()) {
          jw.String("table_transaction_ddl_previous_table_previous_schema");
          jw.Protobuf(l->pb.ysql_ddl_txn_verifier_state(0).previous_schema());
          jw.String("table_transaction_ddl_previous_table_current_schema");
        }
      }
    }

    Status s = SchemaFromPB(l->pb.schema(), &schema);
    if (s.ok()) {
      s = dockv::PartitionSchema::FromPB(l->pb.partition_schema(), schema, &partition_schema);
    }
    if (!s.ok()) {
      jw.String("Unable to decode partition schema: " + s.ToString());
      jw.EndObject();
      return;
    }
    tablets = table->GetTablets(IncludeInactive::kTrue);
  }

  JsonOutputSchemaTable(schema, &jw);

  jw.String("tablets");
  jw.StartArray();
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto locations = tablet->GetReplicaLocations();
    vector<TabletReplica> sorted_locations;
    AppendValuesFromMap(*locations, &sorted_locations);
    std::sort(sorted_locations.begin(), sorted_locations.end(), &CompareByHost);

    auto l = tablet->LockForRead();

    dockv::Partition partition;
    dockv::Partition::FromPB(l->pb.partition(), &partition);

    string state = SysTabletsEntryPB_State_Name(l->pb.state());
    Capitalize(&state);
    jw.StartObject();
    jw.String("tablet_id");
    jw.String(tablet->tablet_id());
    jw.String("partition");
    jw.String(partition_schema.PartitionDebugString(partition, schema));
    jw.String("split_depth");
    jw.Uint64(l->pb.split_depth());
    jw.String("state");
    jw.String(state);
    jw.String("hidden");
    jw.String(l->is_hidden()?"true":"false");
    jw.String("message");
    jw.String(l->pb.state_msg());
    RaftConfigToJson(sorted_locations, tablet->tablet_id(), &jw);
    jw.EndObject();
  }
  jw.EndArray();

  JsonOutputTasks(table->GetTasks(), &jw);
  jw.EndObject();
}

void MasterPathHandlers::HandleTasksPage(const Webserver::WebRequest& req,
                                         Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kAll);
  *output << "<h3>Active Tasks</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Task Name</th><th>State</th><th>Start "
             "Time</th><th>Time</th><th>Description</th></tr>\n";
  for (const auto& table : tables) {
    for (const auto& task : table->GetTasks()) {
      HtmlOutputTask(task, output);
    }
  }
  *output << "</table>\n";

  std::vector<std::shared_ptr<MonitoredTask>> jobs =
      master_->catalog_manager()->GetRecentJobs();
  *output << Format(
      "<h3>Last $0 user-initiated jobs started in the past $1 "
      "hours</h3>\n",
      FLAGS_tasks_tracker_num_long_term_tasks,
      FLAGS_long_term_tasks_tracker_keep_time_multiplier *
          MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms)
              .ToSeconds() /
          3600);
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Job Name</th><th>State</th><th>Start "
             "Time</th><th>Duration</th><th>Description</th></tr>\n";
  for (std::vector<std::shared_ptr<MonitoredTask>>::reverse_iterator iter =
           jobs.rbegin();
       iter != jobs.rend(); ++iter) {
    HtmlOutputTask(*iter, output);
  }
  *output << "</table>\n";

  std::vector<std::shared_ptr<MonitoredTask> > tasks =
    master_->catalog_manager()->GetRecentTasks();
  *output << Format(
      "<h3>Last $0 tasks started in the past $1 seconds</h3>\n",
      FLAGS_tasks_tracker_num_tasks,
      FLAGS_tasks_tracker_keep_time_multiplier *
          MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms)
              .ToSeconds());
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Task Name</th><th>State</th><th>Start "
             "Time</th><th>Duration</th><th>Description</th></tr>\n";
  for (std::vector<std::shared_ptr<MonitoredTask>>::reverse_iterator iter =
           tasks.rbegin();
       iter != tasks.rend(); ++iter) {
    HtmlOutputTask(*iter, output);
  }
  *output << "</table>\n";
}

std::vector<TabletInfoPtr> MasterPathHandlers::GetNonSystemTablets() {
  std::vector<TabletInfoPtr> nonsystem_tablets;

  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);

  for (const auto& table : tables) {
    if (master_->catalog_manager()->IsSystemTable(*table.get())) {
      continue;
    }
    TabletInfos ts = table->GetTablets(IncludeInactive::kTrue);

    for (TabletInfoPtr t : ts) {
      nonsystem_tablets.push_back(t);
    }
  }
  return nonsystem_tablets;
}

std::vector<std::pair<TabletInfoPtr, std::string>> MasterPathHandlers::GetLeaderlessTablets() {
  std::vector<std::pair<TabletInfoPtr, std::string>> leaderless_tablets;

  auto nonsystem_tablets = GetNonSystemTablets();
  for (TabletInfoPtr t : nonsystem_tablets) {
    if (t.get()->LockForRead()->is_deleted()) {
      continue;
    }
    const auto time_since_valid_leader_secs =
        MonoTime::Now().GetDeltaSince(t->last_time_with_valid_leader()).ToSeconds();
    if (time_since_valid_leader_secs >
            GetAtomicFlag(&FLAGS_leaderless_tablet_alert_delay_secs)) {
      leaderless_tablets.push_back(std::make_pair(
          t,
          Format("No valid leader reported for $0 seconds",
                 time_since_valid_leader_secs)));
    }
  }
  return leaderless_tablets;
}

// Returns the placement_uuids of any placement in which the given tablet is underreplicated.
vector<string> GetTabletUnderReplicatedPlacements(
    const TabletInfoPtr& tablet, const ReplicationInfoPB& replication_info) {
  VLOG_WITH_FUNC(1) << "Processing tablet " << tablet->id();
  // We will decrement the num_replicas and replication_factor counters in each placement as we find
  // replicas in its placement blocks.
  // If the tablet is under-replicated, it will have some counter > 0.
  ReplicationInfoPB replication_info_copy = replication_info;
  vector<PlacementInfoPB*> placements;
  placements.push_back(replication_info_copy.mutable_live_replicas());
  for (int i = 0; i < replication_info_copy.read_replicas_size(); ++i) {
    placements.push_back(replication_info_copy.mutable_read_replicas(i));
  }

  auto replica_locations = tablet->GetReplicaLocations();
  for (auto& [ts_uuid, replica] : *replica_locations) {
    // Replicas are put into the UNKNOWN state by config change until they heartbeat so we count
    // UNKNOWN replicas as running to avoid false positives.
    if (replica.state != tablet::RaftGroupStatePB::RUNNING &&
        replica.state != tablet::RaftGroupStatePB::UNKNOWN) {
      VLOG_WITH_FUNC(1) << "Skipping replica in state " << RaftGroupStatePB_Name(replica.state);
      continue;
    }

    // Decrement counters in the relevant placement.
    auto& ts_desc = replica.ts_desc;
    VLOG_WITH_FUNC(1) << "Processing tablet replica on TS " << ts_desc->permanent_uuid();
    for (auto* placement : placements) {
      if (placement->placement_uuid() == ts_desc->placement_uuid()) {
        VLOG_WITH_FUNC(1) << "TS matches placement " << placement->placement_uuid();
        placement->set_num_replicas(placement->num_replicas() - 1);

        // Decrement the unique placement block within this placement.
        for (int i = 0; i < placement->placement_blocks_size(); ++i) {
          if (ts_desc->MatchesCloudInfo(placement->placement_blocks(i).cloud_info())) {
            VLOG_WITH_FUNC(1) << "TS matches placement "
                              << placement->placement_blocks(i).ShortDebugString();
            placement->mutable_placement_blocks(i)->set_min_num_replicas(
                placement->placement_blocks(i).min_num_replicas() - 1);
            break;
          }
        }
      }
    }
  }

  // If the tablet is under-replicated, it will have some counter > 0.
  vector<string> underreplicated_placements;
  for (const auto* placement : placements) {
    if (placement->num_replicas() > 0) {
      VLOG_WITH_FUNC(1) << Format("Tablet $0 underreplicated in placement $1. Need $2 more "
          "replicas.", tablet->id(), placement->placement_uuid(), placement->num_replicas());
      underreplicated_placements.push_back(placement->placement_uuid());
      continue;
    }

    // Check placement blocks within this placement.
    for (auto& placement_block : placement->placement_blocks()) {
      if (placement_block.min_num_replicas() > 0) {
        VLOG_WITH_FUNC(1) << Format("Tablet $0 underreplicated in placement block $1 for placement "
            "$2. Need $3 more replicas.", tablet->id(),
            placement_block.cloud_info().ShortDebugString(), placement->placement_uuid(),
            placement_block.min_num_replicas());
        underreplicated_placements.push_back(placement->placement_uuid());
        break;
      }
    }
  }
  return underreplicated_placements;
}

Result<vector<pair<TabletInfoPtr, vector<string>>>>
    MasterPathHandlers::GetUnderReplicatedTablets() {
  auto* catalog_mgr = master_->catalog_manager();

  catalog_mgr->AssertLeaderLockAcquiredForReading();
  auto tables = catalog_mgr->GetTables(GetTablesMode::kRunning);

  vector<pair<TabletInfoPtr, vector<string>>> underreplicated_tablets;
  for (const auto& table : tables) {
    if (catalog_mgr->IsSystemTable(*table.get())) {
      continue;
    }
    auto replication_info = VERIFY_RESULT(catalog_mgr->GetTableReplicationInfo(table));
    for (TabletInfoPtr tablet : table->GetTablets()) {
      auto underreplicated_placements =
          GetTabletUnderReplicatedPlacements(tablet, replication_info);
      if (!underreplicated_placements.empty()) {
        underreplicated_tablets.emplace_back(
            std::move(tablet), std::move(underreplicated_placements));
      }
    }
  }
  return underreplicated_tablets;
}

void MasterPathHandlers::HandleTabletReplicasPage(const Webserver::WebRequest& req,
                                                  Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;

  auto leaderless_tablets = GetLeaderlessTablets();
  auto underreplicated_tablets = GetUnderReplicatedTablets();

  *output << "<h3>Leaderless Tablets</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Table Name</th><th>Table UUID</th><th>Tablet ID</th>"
             "<th>Leaderless Reason</th></tr>\n";

  for (const std::pair<TabletInfoPtr, string>& t : leaderless_tablets) {
    *output << Format(
        "<tr><td><a href=\"/table?id=$0\">$1</a></td><td>$2</td><td>$3</td><td>$4</td></tr>\n",
        UrlEncodeToString(t.first->table()->id()),
        EscapeForHtmlToString(t.first->table()->name()),
        EscapeForHtmlToString(t.first->table()->id()),
        EscapeForHtmlToString(t.first.get()->tablet_id()),
        EscapeForHtmlToString(t.second));
  }

  *output << "</table>\n";

  if (!underreplicated_tablets.ok()) {
    LOG(WARNING) << underreplicated_tablets.ToString();
    *output << "<h2>Call to GetUnderReplicatedTablets failed</h2>\n";
    return;
  }

  *output << "<h3>Underreplicated Tablets</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "<tr><th>Table Name</th><th>Table UUID</th><th>Tablet ID</th>"
          << "<th>Tablet Replication Count</th><th>Underreplicated Placements</th></tr>\n";

  for (auto& [tablet, placement_uuids] : *underreplicated_tablets) {
    auto rm = tablet.get()->GetReplicaLocations();

    stringstream underreplicated_placements;
    for (auto& uuid : placement_uuids) {
      underreplicated_placements << (uuid == "" ? "Live (primary) cluster" : uuid) << "\n";
    }
    *output << Format(
        "<tr><td><a href=\"/table?id=$0\">$1</a></td><td>$2</td>"
        "<td>$3</td><td>$4</td><td>$5</td></tr>\n",
        UrlEncodeToString(tablet->table()->id()),
        EscapeForHtmlToString(tablet->table()->name()),
        EscapeForHtmlToString(tablet->table()->id()),
        EscapeForHtmlToString(tablet->tablet_id()),
        EscapeForHtmlToString(std::to_string(rm->size())),
        EscapeForHtmlToString(underreplicated_placements.str()));
  }

  *output << "</table>\n";
}

void MasterPathHandlers::HandleGetReplicationStatus(const Webserver::WebRequest& req,
                                                    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);

  auto leaderless_ts = GetLeaderlessTablets();

  jw.StartObject();
  jw.String("leaderless_tablets");
  jw.StartArray();

  for (const std::pair<TabletInfoPtr, std::string>& t : leaderless_ts) {
    jw.StartObject();
    jw.String("table_uuid");
    jw.String(t.first->table()->id());
    jw.String("tablet_uuid");
    jw.String(t.first.get()->tablet_id());
    jw.String("reason");
    jw.String(t.second);
    jw.EndObject();
  }

  jw.EndArray();
  jw.EndObject();
}

void MasterPathHandlers::HandleGetUnderReplicationStatus(const Webserver::WebRequest& req,
                                                    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);

  auto underreplicated_tablets = GetUnderReplicatedTablets();

  if (!underreplicated_tablets.ok()) {
    jw.StartObject();
    jw.String("Error");
    jw.String(underreplicated_tablets.status().ToString());
    jw.EndObject();
    return;
  }

  jw.StartObject();
  jw.String("underreplicated_tablets");
  jw.StartArray();

  for (auto& [tablet, placement_uuids] : *underreplicated_tablets) {
    jw.StartObject();
    jw.String("table_uuid");
    jw.String(tablet->table()->id());
    jw.String("tablet_uuid");
    jw.String(tablet.get()->tablet_id());
    jw.String("underreplicated_placements");
    jw.StartArray();
    for (auto& uuid : placement_uuids) {
      jw.String(uuid);
    }
    jw.EndArray();
    jw.EndObject();
  }

  jw.EndArray();
  jw.EndObject();
}

void MasterPathHandlers::RootHandler(const Webserver::WebRequest& req,
                                     Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  // First check if we are the master leader. If not, make a curl call to the master leader and
  // return that as the UI payload.
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  if (!l.IsInitializedAndIsLeader()) {
    // We are not the leader master, retrieve the response from the leader master.
    RedirectToLeader(req, resp);
    return;
  }

  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    *output << "<div class=\"alert alert-warning\">"
            << EscapeForHtmlToString(s.ToString()) << "</div>";
    return;
  }

  // Get all the tables.
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);

  // Get the list of user tables.
  vector<scoped_refptr<TableInfo> > user_tables;
  for (scoped_refptr<TableInfo> table : tables) {
    if (master_->catalog_manager()->IsUserTable(*table)) {
      user_tables.push_back(table);
    }
  }
  // Get the version info.
  VersionInfoPB version_info;
  VersionInfo::GetVersionInfoPB(&version_info);

  // Display the overview information.
  (*output) << "<h1>YugabyteDB</h1>\n";

  (*output) << "<div class='row dashboard-content'>\n";

  (*output) << "<div class='col-xs-12 col-md-8 col-lg-6'>\n";
  (*output) << "<div class='panel panel-default'>\n"
            << "<div class='panel-heading'><h2 class='panel-title'> Overview</h2></div>\n";
  (*output) << "<div class='panel-body table-responsive'>";
  (*output) << "<table class='table'>\n";

  // Universe UUID.
  (*output) << "  <tr>";
  (*output) << Format(" <td>$0<span class='yb-overview'>$1</span></td>",
                          "<i class='fa fa-database yb-dashboard-icon' aria-hidden='true'></i>",
                          "Universe UUID ");
  (*output) << Format(" <td>$0</td>",
                          config.cluster_uuid());
  (*output) << "  </tr>\n";

  // Replication factor.
  (*output) << "  <tr>";
  (*output) << Format(" <td>$0<span class='yb-overview'>$1</span></td>",
                          "<i class='fa fa-files-o yb-dashboard-icon' aria-hidden='true'></i>",
                          "Replication Factor ");
  auto num_replicas = master_->catalog_manager()->GetReplicationFactor();
  if (!num_replicas.ok()) {
    num_replicas = num_replicas.status().CloneAndPrepend("Unable to determine Replication factor.");
    LOG(WARNING) << num_replicas.status();
  }
  (*output) << Format(" <td>$0 <a href='$1' class='btn btn-default pull-right'>$2</a></td>",
                      num_replicas, "/cluster-config", "See full config &raquo;");
  (*output) << "  </tr>\n";

  // Tserver count.
  (*output) << "  <tr>";
  (*output) << Format(" <td>$0<span class='yb-overview'>$1</span></td>",
                          "<i class='fa fa-server yb-dashboard-icon' aria-hidden='true'></i>",
                          "Num Nodes (TServers) ");
  (*output) << Format(" <td>$0 <a href='$1' class='btn btn-default pull-right'>$2</a></td>",
                          GetTserverCountForDisplay(master_->ts_manager()),
                          "/tablet-servers",
                          "See all nodes &raquo;");
  (*output) << "  </tr>\n";

  // Num user tables.
  (*output) << "  <tr>";
  (*output) << Format(" <tr><td>$0<span class='yb-overview'>$1</span></td>",
                          "<i class='fa fa-table yb-dashboard-icon' aria-hidden='true'></i>",
                          "Num User Tables ");
  (*output) << Format(" <td>$0 <a href='$1' class='btn btn-default pull-right'>$2</a></td>",
                          user_tables.size(),
                          "/tables",
                          "See all tables &raquo;");
  (*output) << "  </tr>\n";

  // Load balancer status.
  bool load_balancer_enabled = master_->catalog_manager()->IsLoadBalancerEnabled();
  (*output) << Format(" <tr><td>$0<span class='yb-overview'>$1</span></td>"
                          "<td><i class='fa $2' aria-hidden='true'> </i></td></tr>\n",
                          "<i class='fa fa-tasks yb-dashboard-icon' aria-hidden='true'></i>",
                          "Load Balancer Enabled",
                          load_balancer_enabled ? "fa-check"
                                                : "fa-times label label-danger");
  if (load_balancer_enabled) {
    IsLoadBalancedRequestPB req;
    IsLoadBalancedResponsePB resp;
    Status load_balanced = master_->catalog_manager()->IsLoadBalanced(&req, &resp);

    (*output) << Format(" <tr><td>$0<span class='yb-overview'>$1</span></td>"
                            "<td><i class='fa $2' aria-hidden='true'> </i></td></tr>\n",
                            "<i class='fa fa-tasks yb-dashboard-icon' aria-hidden='true'></i>",
                            "Is Load Balanced?",
                            load_balanced.ok() ? "fa-check"
                                               : "fa-times label label-danger");
  }

  // Build version and type.
  (*output) << Format("  <tr><td>$0<span class='yb-overview'>$1</span></td><td>$2</td></tr>\n",
                          "<i class='fa fa-code-fork yb-dashboard-icon' aria-hidden='true'></i>",
                          "YugabyteDB Version ", version_info.version_number());
  (*output) << Format("  <tr><td>$0<span class='yb-overview'>$1</span></td><td>$2</td></tr>\n",
                          "<i class='fa fa-terminal yb-dashboard-icon' aria-hidden='true'></i>",
                          "Build Type ", version_info.build_type());

  // Encryption Status
  string encryption_status_icon;
  string encryption_status_str;

  IsEncryptionEnabledResponsePB encryption_resp;
  auto encryption_state =
      master_->encryption_manager().GetEncryptionState(config.encryption_info(), &encryption_resp);

  switch (encryption_state) {
    case EncryptionManager::EncryptionState::kUnknown:
      encryption_status_icon = "fa-question label label-danger";
      encryption_status_str = "Unknown";
      break;
    case EncryptionManager::EncryptionState::kNeverEnabled:
      encryption_status_icon = "fa-unlock";
      encryption_status_str = "Never enabled";
      break;
    case EncryptionManager::EncryptionState::kEnabled:
      encryption_status_icon = "fa-lock";
      encryption_status_str = Format("Enabled with key: $0", encryption_resp.key_id());
      break;
    case EncryptionManager::EncryptionState::kEnabledUnkownIfKeyIsInMem:
      encryption_status_icon = "fa-question label label-danger";
      encryption_status_str = Format(
          "Enabled with key: $0. Unable to determine if encryption keys are in memory",
          encryption_resp.key_id());
      break;
    case EncryptionManager::EncryptionState::kEnabledKeyNotInMem:
      encryption_status_icon = "fa-times label label-danger";
      encryption_status_str = Format(
          "Enabled with key: $0. Node Does not have universe key in memory",
          encryption_resp.key_id());
      break;
    case EncryptionManager::EncryptionState::kDisabled:
      encryption_status_str = "Disabled";
      encryption_status_icon = "fa-unlock-alt";
      break;
  }

  (*output) << Format(
      " <tr><td>$0<span class='yb-overview'>$1</span></td>"
      "<td>"
      "<div style='overflow-x:auto; max-width:300px; display:inline-block;'>"
      "<i class='fa $2' aria-hidden='true'> </i>  $3</div>"
      "</td></tr>\n",
      "<i class='fa fa-key yb-dashboard-icon' aria-hidden='true'></i>",
      "Encryption Status ",
      encryption_status_icon,
      EscapeForHtmlToString(encryption_status_str));

  (*output) << "</table>";
  (*output) << "</div> <!-- panel-body -->\n";
  (*output) << "</div> <!-- panel -->\n";
  (*output) << "</div> <!-- col-xs-12 col-md-8 col-lg-6 -->\n";

  // Display the master info.
  (*output) << "<div class='col-xs-12 col-md-8 col-lg-6'>\n";
  HandleMasters(req, resp);
  (*output) << "</div> <!-- col-xs-12 col-md-8 col-lg-6 -->\n";
}

void MasterPathHandlers::HandleMasters(const Webserver::WebRequest& req,
                                       Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  vector<ServerEntryPB> masters;
  Status s = master_->ListMasters(&masters);
  if (!s.ok()) {
    s = s.CloneAndPrepend("Unable to list Masters");
    LOG(WARNING) << s.ToString();
    *output << "<h1>" << EscapeForHtmlToString(s.ToString()) << "</h1>\n";
    return;
  }
  (*output) << "<div class='panel panel-default'>\n"
            << "<div class='panel-heading'><h2 class='panel-title'>Masters</h2></div>\n";
  (*output) << "<div class='panel-body table-responsive'>";
  (*output) << "<table class='table'>\n";
  (*output) << "  <tr>\n"
            << "    <th>Server</th>\n"
            << "    <th>RAFT Role</th>\n"
            << "    <th>Uptime</th>\n"
            << "    <th>Details</th>\n"
            << "  </tr>\n";

  for (const ServerEntryPB& master : masters) {
    if (master.has_error()) {
      string error = StatusFromPB(master.error()).ToString();
      *output << "  <tr>\n";
      const string kErrStart = "peer ([";
      const string kErrEnd = "])";
      size_t start_pos = error.find(kErrStart);
      size_t end_pos = error.find(kErrEnd);
      if (start_pos != string::npos && end_pos != string::npos && (start_pos < end_pos)) {
        start_pos = start_pos + kErrStart.length();
        string host_port = error.substr(start_pos, end_pos - start_pos);
        *output << "<td><font color='red'>" << EscapeForHtmlToString(host_port)
                << "</font></td>\n";
        *output << "<td><font color='red'>" << PeerRole_Name(PeerRole::UNKNOWN_ROLE)
                << "</font></td>\n";
      }
      *output << Format("    <td colspan=2><font color='red'><b>ERROR: $0</b></font></td>\n",
                              EscapeForHtmlToString(error));
      *output << "  </tr>\n";
      continue;
    }
    auto reg = master.registration();
    string reg_text = RegistrationToHtml(reg, GetHttpHostPortFromServerRegistration(reg));
    if (master.instance_id().permanent_uuid() == master_->instance_pb().permanent_uuid()) {
      reg_text = Format("<b>$0</b>", reg_text);
    }
    string raft_role = master.has_role() ? PeerRole_Name(master.role()) : "N/A";
    auto delta = Env::Default()->NowMicros() - master.instance_id().start_time_us();
    string uptime = UptimeString(MonoDelta::FromMicroseconds(delta).ToSeconds());
    string cloud = EscapeForHtmlToString(reg.cloud_info().placement_cloud());
    string region = EscapeForHtmlToString(reg.cloud_info().placement_region());
    string zone = EscapeForHtmlToString(reg.cloud_info().placement_zone());

    *output << "  <tr>\n"
            << "    <td>" << reg_text << "</td>\n"
            << "    <td>" << raft_role << "</td>\n"
            << "    <td>" << uptime << "</td>\n"
            << "    <td><div><span class='yb-overview'>CLOUD: </span>" << cloud << "</div>\n"
            << "        <div><span class='yb-overview'>REGION: </span>" << region << "</div>\n"
            << "        <div><span class='yb-overview'>ZONE: </span>" << zone << "</div>\n"
            << "        <div><span class='yb-overview'>UUID: </span>"
            << master.instance_id().permanent_uuid()
            << "</div></td>\n"
            << "  </tr>\n";
  }

  (*output) << "</table>";
  (*output) << "</div> <!-- panel-body -->\n";
  (*output) << "</div> <!-- panel -->\n";
}

namespace {

// Visitor for the catalog table which dumps tables and tablets in a JSON format. This
// dump is interpreted by the CM agent in order to track time series entities in the SMON
// database.
//
// This implementation relies on scanning the catalog table directly instead of using the
// catalog manager APIs. This allows it to work even on a non-leader master, and avoids
// any requirement for locking. For the purposes of metrics entity gathering, it's OK to
// serve a slightly stale snapshot.
//
// It is tempting to directly dump the metadata protobufs using JsonWriter::Protobuf(...),
// but then we would be tying ourselves to textual compatibility of the PB field names in
// our catalog table. Instead, the implementation specifically dumps the fields that we
// care about.
//
// This should be considered a "stable" protocol -- do not rename, remove, or restructure
// without consulting with the CM team.
class JsonDumperBase {
 public:
  explicit JsonDumperBase(JsonWriter* jw) : jw_(jw) {}

  virtual ~JsonDumperBase() {}

  virtual std::string name() const = 0;

 protected:
  JsonWriter* jw_;
};

class JsonKeyspaceDumper : public Visitor<PersistentNamespaceInfo>, public JsonDumperBase {
 public:
  explicit JsonKeyspaceDumper(JsonWriter* jw) : JsonDumperBase(jw) {}

  std::string name() const override { return "keyspaces"; }

  virtual Status Visit(const std::string& keyspace_id,
                       const SysNamespaceEntryPB& metadata) override {
    jw_->StartObject();
    jw_->String("keyspace_id");
    jw_->String(keyspace_id);

    jw_->String("keyspace_name");
    jw_->String(metadata.name());

    jw_->String("keyspace_type");
    jw_->String(DatabaseTypeName((metadata.database_type())));

    jw_->EndObject();
    return Status::OK();
  }
};

class JsonTableDumper : public Visitor<PersistentTableInfo>, public JsonDumperBase {
 public:
  explicit JsonTableDumper(JsonWriter* jw) : JsonDumperBase(jw) {}

  std::string name() const override { return "tables"; }

  Status Visit(const std::string& table_id, const SysTablesEntryPB& metadata) override {
    if (metadata.state() != SysTablesEntryPB::RUNNING) {
      return Status::OK();
    }

    jw_->StartObject();
    jw_->String("table_id");
    jw_->String(table_id);

    jw_->String("keyspace_id");
    jw_->String(metadata.namespace_id());

    jw_->String("table_name");
    jw_->String(metadata.name());

    jw_->String("state");
    jw_->String(SysTablesEntryPB::State_Name(metadata.state()));

    jw_->EndObject();
    return Status::OK();
  }
};

class JsonTabletDumper : public Visitor<PersistentTabletInfo>, public JsonDumperBase {
 public:
  explicit JsonTabletDumper(JsonWriter* jw) : JsonDumperBase(jw) {}

  std::string name() const override { return "tablets"; }

  Status Visit(const std::string& tablet_id, const SysTabletsEntryPB& metadata) override {
    const std::string& table_id = metadata.table_id();
    if (metadata.state() != SysTabletsEntryPB::RUNNING) {
      return Status::OK();
    }

    jw_->StartObject();
    jw_->String("table_id");
    jw_->String(table_id);

    jw_->String("tablet_id");
    jw_->String(tablet_id);

    jw_->String("state");
    jw_->String(SysTabletsEntryPB::State_Name(metadata.state()));

    // Dump replica UUIDs
    if (metadata.has_committed_consensus_state()) {
      const consensus::ConsensusStatePB& cs = metadata.committed_consensus_state();
      jw_->String("replicas");
      jw_->StartArray();
      for (const RaftPeerPB& peer : cs.config().peers()) {
        jw_->StartObject();
        jw_->String("type");
        jw_->String(PeerMemberType_Name(peer.member_type()));

        jw_->String("server_uuid");
        jw_->String(peer.permanent_uuid());

        jw_->String("addr");
        const auto& host_port = peer.last_known_private_addr(0);
        jw_->String(HostPortPBToString(host_port));

        jw_->EndObject();
      }
      jw_->EndArray();

      if (cs.has_leader_uuid()) {
        jw_->String("leader");
        jw_->String(cs.leader_uuid());
      }
    }

    jw_->EndObject();
    return Status::OK();
  }
};

template <class Dumper>
Status JsonDumpCollection(JsonWriter* jw, Master* master, stringstream* output) {
  unique_ptr<Dumper> json_dumper(new Dumper(jw));
  jw->String(json_dumper->name());
  jw->StartArray();
  const Status s = master->catalog_manager()->sys_catalog()->Visit(json_dumper.get());
  if (s.ok()) {
    // End the array only if there is no error.
    jw->EndArray();
  } else {
    // Print just an error message.
    output->str("");
    JsonWriter jw_err(output, JsonWriter::COMPACT);
    jw_err.StartObject();
    jw_err.String("error");
    jw_err.String(s.ToString());
    jw_err.EndObject();
  }
  return s;
}

}  // anonymous namespace

void MasterPathHandlers::HandleDumpEntities(const Webserver::WebRequest& req,
                                            Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  JsonWriter jw(output, JsonWriter::COMPACT);
  jw.StartObject();

  if (JsonDumpCollection<JsonKeyspaceDumper>(&jw, master_, output).ok() &&
      JsonDumpCollection<JsonTableDumper>(&jw, master_, output).ok() &&
      JsonDumpCollection<JsonTabletDumper>(&jw, master_, output).ok()) {
    // End the object only if there is no error.
    jw.EndObject();
  }
}

void MasterPathHandlers::HandleCheckIfLeader(const Webserver::WebRequest& req,
                                              Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);
  jw.StartObject();
  {
    SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());

    // If we are not the master leader.
    if (!l.IsInitializedAndIsLeader()) {
      resp->code = 503;
      return;
    }

    jw.String("STATUS");
    jw.String(Status().CodeAsString());
    jw.EndObject();
    return;
  }
}

void MasterPathHandlers::HandleGetMastersStatus(const Webserver::WebRequest& req,
                                                    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  vector<ServerEntryPB> masters;
  Status s = master_->ListMasters(&masters);
  ListMastersResponsePB pb_resp;
  JsonWriter jw(output, JsonWriter::COMPACT);
  if (!s.ok()) {
    jw.Protobuf(pb_resp);
    return;
  }
  for (const ServerEntryPB& master : masters) {
    pb_resp.add_masters()->CopyFrom(master);
  }
  jw.Protobuf(pb_resp);
}

void MasterPathHandlers::HandleGetClusterConfig(
  const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  *output << "<h1>Current Cluster Config</h1>\n";
  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    *output << "<div class=\"alert alert-warning\">"
            << EscapeForHtmlToString(s.ToString()) << "</div>";
    return;
  }

  *output << "<div class=\"alert alert-success\">Successfully got cluster config!</div>"
  << "<pre class=\"prettyprint\">" << EscapeForHtmlToString(config.DebugString()) << "</pre>";
}

void MasterPathHandlers::HandleGetClusterConfigJSON(
  const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);

  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  SysClusterConfigEntryPB config;
  Status s = master_->catalog_manager()->GetClusterConfig(&config);
  if (!s.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(s.ToString());
    jw.EndObject();
    return;
  }

  // return cluster config in JSON format
  jw.Protobuf(config);
}

Status MasterPathHandlers::GetClusterAndXClusterConfigStatus(
    SysXClusterConfigEntryPB* xcluster_config, SysClusterConfigEntryPB* cluster_config) {
  RETURN_NOT_OK(master_->xcluster_manager()->GetXClusterConfigEntryPB(xcluster_config));
  return master_->catalog_manager()->GetClusterConfig(cluster_config);
}

void MasterPathHandlers::HandleGetXClusterConfig(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  *output << "<h1>Current XCluster Config</h1>\n";
  SysXClusterConfigEntryPB xcluster_config;
  SysClusterConfigEntryPB cluster_config;
  Status s = GetClusterAndXClusterConfigStatus(&xcluster_config, &cluster_config);

  if (!s.ok()) {
    *output << "<div class=\"alert alert-warning\">"
            << EscapeForHtmlToString(s.ToString()) << "</div>";
    return;
  }
  *output << "<div class=\"alert alert-success\">Successfully got xcluster config!</div>"
          << "<pre class=\"prettyprint\">" << EscapeForHtmlToString(xcluster_config.DebugString())
          << "consumer_registry {\n"
          << EscapeForHtmlToString(cluster_config.consumer_registry().DebugString())
          << "}</pre>";
}

void MasterPathHandlers::HandleGetXClusterConfigJSON(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);

  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  SysXClusterConfigEntryPB xcluster_config;
  SysClusterConfigEntryPB cluster_config;
  Status s = GetClusterAndXClusterConfigStatus(&xcluster_config, &cluster_config);
  if (!s.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(s.ToString());
    jw.EndObject();
    return;
  }

  jw.StartObject();
  jw.String("version");
  jw.Int64(xcluster_config.version());
  jw.String("xcluster_producer_registry");
  jw.Protobuf(xcluster_config.xcluster_producer_registry());
  jw.String("consumer_registry");
  jw.Protobuf(cluster_config.consumer_registry());
  jw.EndObject();
}

void MasterPathHandlers::HandleVersionInfoDump(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::PRETTY);

  // Get the version info.
  VersionInfoPB version_info;
  VersionInfo::GetVersionInfoPB(&version_info);

  jw.Protobuf(version_info);
}

void MasterPathHandlers::HandlePrettyLB(
  const Webserver::WebRequest& req, Webserver::WebResponse* resp) {

  std::stringstream *output = &resp->output;

  // Don't render if there are more than 5 tservers.
  vector<std::shared_ptr<TSDescriptor>> descs;
  const auto& ts_manager = master_->ts_manager();
  ts_manager->GetAllDescriptors(&descs);

  if (descs.size() > 5) {
    *output << "<div class='alert alert-warning'>"
            << "Current configuration has more than 5 tservers. Not recommended"
            << " to view this pretty display as it might not be rendered properly."
            << "</div>";
    return;
  }

  // Don't render if there is a lot of placement nesting.
  std::unordered_set<std::string> clouds;
  std::unordered_set<std::string> regions;
  // Map of zone -> {tserver UUIDs}
  // e.g. zone1 -> {ts1uuid, ts2uuid, ts3uuid}.
  std::unordered_map<std::string, vector<std::string>> zones;
  for (const auto& desc : descs) {
    std::string uuid = desc->permanent_uuid();
    std::string cloud = desc->GetCloudInfo().placement_cloud();
    std::string region = desc->GetCloudInfo().placement_region();
    std::string zone = desc->GetCloudInfo().placement_zone();

    zones[zone].push_back(uuid);
    clouds.insert(cloud);
    regions.insert(region);
  }

  // If the we have more than 1 cloud or more than 1 region skip this page
  // as currently it might not diplay prettily.
  if (clouds.size() > 1 || regions.size() > 1 || zones.size() > 3) {
    *output << "<div class='alert alert-warning'>"
            << "Current placement has more than 1 cloud provider or 1 region or 3 zones. "
            << "Not recommended to view this pretty display as it might not be rendered properly."
            << "</div>";
    return;
  }

  // Get the TServerTree.
  // A map of tserver -> all tables with their tablets.
  TServerTree tserver_tree;
  Status s = CalculateTServerTree(&tserver_tree, 4 /* max_table_count */);
  if (!s.ok()) {
    *output << "<div class='alert alert-warning'>"
            << "Current placement has more than 4 tables. Not recommended"
            << " to view this pretty display as it might not be rendered properly."
            << "</div>";
    return;
  }

  auto blacklist_result = master_->catalog_manager()->BlacklistSetFromPB();
  BlacklistSet blacklist = blacklist_result.ok() ? *blacklist_result : BlacklistSet();

  // A single zone.
  int color_index = 0;
  std::unordered_map<std::string, std::string> tablet_colors;

  *output << "<div class='row'>\n";
  for (const auto& zone : zones) {
    // Panel for this Zone.
    // Split the zones in proportion of the number of tservers in each zone.
    *output << Format("<div class='col-lg-$0'>\n", 12*zone.second.size()/descs.size());

    // Change the display of the panel if all tservers in this zone are down.
    bool all_tservers_down = true;
    for (const auto& tserver : zone.second) {
      std::shared_ptr<TSDescriptor> desc;
      if (!master_->ts_manager()->LookupTSByUUID(tserver, &desc)) {
        continue;
      }
      all_tservers_down = all_tservers_down && !desc->IsLive();
    }
    string zone_panel_display = "panel-success";
    if (all_tservers_down) {
      zone_panel_display = "panel-danger";
    }

    *output << Format("<div class='panel $0'>\n", zone_panel_display);
    *output << Format("<div class='panel-heading'>"
                          "<h6 class='panel-title'>Zone: $0</h6></div>\n",
                      EscapeForHtmlToString(zone.first));
    *output << "<div class='row'>\n";

    // Tservers for this panel.
    for (const auto& tserver : zone.second) {
      // Split tservers equally.
      *output << Format("<div class='col-lg-$0'>\n", 12/(zone.second.size()));
      std::shared_ptr<TSDescriptor> desc;
      if (!master_->ts_manager()->LookupTSByUUID(tserver, &desc)) {
        continue;
      }

      // Get the state of tserver.
      bool ts_live = desc->IsLive();
      // Get whether tserver is blacklisted.
      bool blacklisted = desc->IsBlacklisted(blacklist);
      string panel_type = "panel-success";
      string icon_type = "fa-check";
      if (!ts_live || blacklisted) {
        panel_type = "panel-danger";
        icon_type = "fa-times";
      }
      *output << Format("<div class='panel $0' style='margin-bottom: 0px'>\n", panel_type);

      // Point to the tablet servers link.
      TSRegistrationPB reg = desc->GetRegistration();
      *output << Format("<div class='panel-heading'>"
                            "<h6 class='panel-title'><a href='$0://$1'>TServer - $1    "
                            "<i class='fa $2'></i></a></h6></div>\n",
                            GetProtocol(),
                            EscapeForHtmlToString(
                                GetHttpHostPortFromServerRegistration(reg.common())),
                            icon_type);

      *output << "<table class='table table-borderless table-hover'>\n";
      for (const auto& table : tserver_tree[tserver]) {
        *output << Format("<tr height='200px'>\n");
        // Display the table name.
        string tname = master_->catalog_manager()->GetTableInfo(table.first)->name();
        // Link the table name to the corresponding table page on the master.
        ServerRegistrationPB reg;
        if (!master_->GetMasterRegistration(&reg).ok()) {
          continue;
        }
        *output << Format("<td><h4><a href='$0://$1/table?id=$2'>"
                              "<i class='fa fa-table'></i>    $3</a></h4>\n",
                              GetProtocol(),
                              EscapeForHtmlToString(GetHttpHostPortFromServerRegistration(reg)),
                              UrlEncodeToString(table.first),
                              EscapeForHtmlToString(tname));
        // Replicas of this table.
        for (const auto& replica : table.second) {
          // All the replicas of the same tablet will have the same color, so
          // look it up in the map if assigned, otherwise assign one from the pool.
          if (tablet_colors.find(replica.tablet_id) == tablet_colors.end()) {
            tablet_colors[replica.tablet_id] = kYBColorList[color_index];
            color_index = (color_index + 1)%kYBColorList.size();
          }

          // Leaders and followers have different formatting.
          // Leaders need to stand out.
          if (replica.role == PeerRole::LEADER) {
            *output << Format("<button type='button' class='btn btn-default'"
                                "style='background-image:none; border: 6px solid $0; "
                                "font-weight: bolder'>"
                                "L</button>\n",
                                tablet_colors[replica.tablet_id]);
          } else {
            *output << Format("<button type='button' class='btn btn-default'"
                                "style='background-image:none; border: 4px dotted $0'>"
                                "F</button>\n",
                                tablet_colors[replica.tablet_id]);
          }
        }
        *output << "</td>\n";
        *output << "</tr>\n";
      }
      *output << "</table><!-- tserver-level-table -->\n";
      *output << "</div><!-- tserver-level-panel -->\n";
      *output << "</div><!-- tserver-level-spacing -->\n";
    }
    *output << "</div><!-- tserver-level-row -->\n";
    *output << "</div><!-- zone-level-panel -->\n";
    *output << "</div><!-- zone-level-spacing -->\n";
  }
  *output << "</div><!-- zone-level-row -->\n";
}

void MasterPathHandlers::HandleLoadBalancer(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  vector<std::shared_ptr<TSDescriptor>> descs;
  const auto& ts_manager = master_->ts_manager();
  ts_manager->GetAllDescriptors(&descs);

  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kAll);

  TServerTree tserver_tree;
  Status s = CalculateTServerTree(&tserver_tree, -1 /* max_table_count */);
  if (!s.ok()) {
    *output << "<div class='alert alert-warning'>"
            << "Cannot Calculate TServer Tree."
            << "</div>";
    return;
  }

  RenderLoadBalancerViewPanel(tserver_tree, descs, tables, output);
}

Status MasterPathHandlers::Register(Webserver* server) {
  bool is_styled = true;
  bool is_on_nav_bar = true;

  // The set of handlers visible on the nav bar.
  server->RegisterPathHandler(
    "/", "Home", std::bind(&MasterPathHandlers::RootHandler, this, _1, _2), is_styled,
    is_on_nav_bar, "fa fa-home");
  Webserver::PathHandlerCallback cb =
      std::bind(&MasterPathHandlers::HandleTabletServers, this, _1, _2,
                TServersViewType::kTServersDefaultView);
  server->RegisterPathHandler(
      "/tablet-servers", "Tablet Servers",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      is_on_nav_bar, "fa fa-server");
  cb = std::bind(&MasterPathHandlers::HandleTabletServers, this, _1, _2,
                 TServersViewType::kTServersClocksView);
  server->RegisterPathHandler(
      "/tablet-server-clocks", "Tablet Server Clocks",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      false /* is_on_nav_bar */);
  cb = std::bind(&MasterPathHandlers::HandleCatalogManager,
      this, _1, _2, false /* only_user_tables */);
  server->RegisterPathHandler(
      "/tables", "Tables",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      is_on_nav_bar, "fa fa-table");
  cb = std::bind(&MasterPathHandlers::HandleNamespacesHTML,
      this, _1, _2, false /* only_user_namespaces */);
  server->RegisterPathHandler(
      "/namespaces", "Namespaces",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      is_on_nav_bar, "fa fa-table");

  // The set of handlers not currently visible on the nav bar.
  cb = std::bind(&MasterPathHandlers::HandleTablePage, this, _1, _2);
  server->RegisterPathHandler(
      "/table", "", std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb),
      is_styled, false);
  server->RegisterPathHandler(
      "/masters", "Masters", std::bind(&MasterPathHandlers::HandleMasters, this, _1, _2), is_styled,
      false);
  cb = std::bind(&MasterPathHandlers::HandleGetClusterConfig, this, _1, _2);
  server->RegisterPathHandler(
      "/cluster-config", "Cluster Config",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      false);
  cb = std::bind(&MasterPathHandlers::HandleGetClusterConfigJSON, this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/cluster-config", "Cluster Config JSON",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false,
      false);
  cb = std::bind(&MasterPathHandlers::HandleGetXClusterConfig, this, _1, _2);
  server->RegisterPathHandler(
      "/xcluster-config", "XCluster Config",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      false);
  cb = std::bind(&MasterPathHandlers::HandleGetXClusterConfigJSON, this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/xcluster-config", "XCluster Config JSON",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  cb = std::bind(&MasterPathHandlers::HandleTasksPage, this, _1, _2);
  server->RegisterPathHandler(
      "/tasks", "Tasks",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      false);
  cb = std::bind(&MasterPathHandlers::HandleTabletReplicasPage, this, _1, _2);
  server->RegisterPathHandler(
      "/tablet-replication", "Tablet Replication Health",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      false);
  cb = std::bind(&MasterPathHandlers::HandlePrettyLB, this, _1, _2);
  server->RegisterPathHandler(
      "/pretty-lb", "Load balancer Pretty Picture",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      false);
  cb = std::bind(&MasterPathHandlers::HandleLoadBalancer, this, _1, _2);
  server->RegisterPathHandler(
      "/load-distribution", "Load balancer View",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), is_styled,
      false);

  // JSON Endpoints
  cb = std::bind(&MasterPathHandlers::HandleGetTserverStatus, this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/tablet-servers", "Tserver Statuses",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  cb = std::bind(&MasterPathHandlers::HandleHealthCheck, this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/health-check", "Cluster Health Check",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  cb = std::bind(&MasterPathHandlers::HandleGetReplicationStatus, this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/tablet-replication", "Tablet Replication Health",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  cb = std::bind(&MasterPathHandlers::HandleGetUnderReplicationStatus, this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/tablet-under-replication", "Tablet UnderReplication Status",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  cb = std::bind(&MasterPathHandlers::HandleDumpEntities, this, _1, _2);
  server->RegisterPathHandler(
      "/dump-entities", "Dump Entities",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  cb = std::bind(&MasterPathHandlers::HandleNamespacesJSON, this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/namespaces", "Namespaces",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  server->RegisterPathHandler(
      "/api/v1/is-leader", "Leader Check",
      std::bind(&MasterPathHandlers::HandleCheckIfLeader, this, _1, _2), false, false);
  server->RegisterPathHandler(
      "/api/v1/masters", "Master Statuses",
      std::bind(&MasterPathHandlers::HandleGetMastersStatus, this, _1, _2), false, false);
  server->RegisterPathHandler(
      "/api/v1/version", "YB Version Information",
      std::bind(&MasterPathHandlers::HandleVersionInfoDump, this, _1, _2), false, false);
  cb = std::bind(&MasterPathHandlers::HandleTablePageJSON, this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/table", "Table Info",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  cb = std::bind(&MasterPathHandlers::HandleCatalogManagerJSON,
      this, _1, _2);
  server->RegisterPathHandler(
      "/api/v1/tables", "Tables",
      std::bind(&MasterPathHandlers::CallIfLeaderOrPrintRedirect, this, _1, _2, cb), false, false);
  return Status::OK();
}

string MasterPathHandlers::RaftConfigToHtml(const std::vector<TabletReplica>& locations,
                                            const std::string& tablet_id) const {
  stringstream html;

  html << "<ul>\n";
  for (const TabletReplica& location : locations) {
    string location_html = TSDescriptorToHtml(*location.ts_desc, tablet_id);
    if (location.role == PeerRole::LEADER) {
      auto leader_lease_info = location.leader_lease_info;
      // The master might haven't received any heartbeats containing this leader yet.
      // Set the status to UNKNOWN.
      auto leader_lease_status = leader_lease_info.initialized
          ? LeaderLeaseStatus_Name(leader_lease_info.leader_lease_status)
          : "UNKNOWN";
      html << Format("  <li><b>LEADER: $0 ($1)</b></li>\n", location_html, leader_lease_status);
      if (leader_lease_info.leader_lease_status == consensus::LeaderLeaseStatus::HAS_LEASE) {
        // Get the remaining milliseconds of the current valid lease.
        const auto now_usec = boost::posix_time::microseconds(
            master_->clock()->Now().GetPhysicalValueMicros());
        auto ht_lease_usec = boost::posix_time::microseconds(leader_lease_info.ht_lease_expiration);
        auto diff = ht_lease_usec - now_usec;
        html << Format("Remaining ht_lease (may be stale): $0 ms<br>\n", diff.total_milliseconds());
      } else if (leader_lease_info.heartbeats_without_leader_lease > 0) {
        html << Format(
            "Cannot replicate lease for past <b><font color='red'>$0</font></b> heartbeats<br>",
            leader_lease_info.heartbeats_without_leader_lease);
      }
    } else {
      html << Format("  <li>$0: $1</li>\n",
                         PeerRole_Name(location.role), location_html);
    }
    html << Format("UUID: $0\n", location.ts_desc->permanent_uuid());
  }
  html << "</ul>\n";
  return html.str();
}

string MasterPathHandlers::TSDescriptorToHtml(const TSDescriptor& desc,
                                              const std::string& tablet_id) const {
  TSRegistrationPB reg = desc.GetRegistration();

  auto public_http_hp = GetPublicHttpHostPort(reg.common());
  if (public_http_hp) {
    return Format(
        "<a href=\"$0://$1/tablet?id=$2\">$3</a>",
        GetProtocol(),
        EscapeForHtmlToString(HostPortPBToString(*public_http_hp)),
        UrlEncodeToString(tablet_id),
        EscapeForHtmlToString(public_http_hp->host()));
  } else {
    return EscapeForHtmlToString(desc.permanent_uuid());
  }
}

string MasterPathHandlers::RegistrationToHtml(
    const ServerRegistrationPB& reg, const std::string& link_text) const {
  string link_html = EscapeForHtmlToString(link_text);
  auto public_http_hp = GetPublicHttpHostPort(reg);
  if (public_http_hp) {
    link_html = Format("<a href=\"$0://$1/\">$2</a>",
                           GetProtocol(),
                           EscapeForHtmlToString(HostPortPBToString(*public_http_hp)),
                           link_html);
  }
  return link_html;
}

void MasterPathHandlers::CalculateTabletMap(TabletCountMap* tablet_map) {
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);
  for (const auto& table : tables) {
    if (table->IsColocatedUserTable()) {
      // will be taken care of by colocated parent table
      continue;
    }

    TabletInfos tablets = table->GetTablets(IncludeInactive::kTrue);
    bool is_user_table = master_->catalog_manager()->IsUserCreatedTable(*table);

    for (const auto& tablet : tablets) {
      auto replication_locations = tablet->GetReplicaLocations();

      for (const auto& replica : *replication_locations) {
        if (is_user_table || table->IsColocationParentTable()) {
          if (replica.second.role == PeerRole::LEADER) {
            (*tablet_map)[replica.first].user_tablet_leaders++;
          } else {
            (*tablet_map)[replica.first].user_tablet_followers++;
          }
        } else {
          if (replica.second.role == PeerRole::LEADER) {
            (*tablet_map)[replica.first].system_tablet_leaders++;
          } else {
            (*tablet_map)[replica.first].system_tablet_followers++;
          }
        }
      }
    }
  }
}

Status MasterPathHandlers::CalculateTServerTree(TServerTree* tserver_tree, int max_table_count) {
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);

  if (max_table_count != -1) {
    int count = 0;
    for (const auto& table : tables) {
      if (!master_->catalog_manager()->IsUserCreatedTable(*table) ||
          table->IsColocatedUserTable()) {
        continue;
      }

      count++;
      if (count > max_table_count) {
        return STATUS_FORMAT(
            NotSupported, "Not supported for more that $0 tables.", max_table_count);
      }
    }
  }

  for (const auto& table : tables) {
    if (!master_->catalog_manager()->IsUserCreatedTable(*table) ||
        table->IsColocatedUserTable()) {
      // only display user created tables that are not colocated.
      continue;
    }

    TabletInfos tablets = table->GetTablets(IncludeInactive::kTrue);

    for (const auto& tablet : tablets) {
      auto replica_locations = tablet->GetReplicaLocations();
      for (const auto& replica : *replica_locations) {
        (*tserver_tree)[replica.first][tablet->table()->id()].emplace_back(
          replica.second.role,
          tablet->tablet_id()
        );
      }
    }
  }

  return Status::OK();
}

void MasterPathHandlers::RenderLoadBalancerViewPanel(
    const TServerTree& tserver_tree, const vector<std::shared_ptr<TSDescriptor>>& descs,
    const std::vector<TableInfoPtr>& tables, std::stringstream* output) {
  *output << "<div class='panel panel-default'>\n"
          << "<div class='panel-heading'><h2 class='panel-title'>Load Balancing Distribution</h2>\n"
          << "</div>\n";

  *output << "<div class='panel-body table-responsive'>";
  *output << "<table class='table table-responsive'>\n";

  // Table header.
  *output << "<thead>";
  *output << "<tr><th rowspan='2'>Keyspace</th><th rowspan='2'>Table Name</th><th "
             "rowspan='2'>Tablet Count</th>";
  for (const auto& desc : descs) {
    const auto& reg = desc->GetRegistration();
    const string& uuid = desc->permanent_uuid();
    string host_port = GetHttpHostPortFromServerRegistration(reg.common());
    *output << Format("<th>$0<br>$1</th>",
                          RegistrationToHtml(reg.common(), host_port), uuid);
  }
  *output << "</tr>";

  *output << "<tr>";
  for (size_t i = 0; i < descs.size(); i++) {
    *output << "<th>Total/Leaders</th>";
  }
  *output << "</tr>";
  *output << "</thead>";

  // Table rows.
  for (const auto& table : tables) {
    auto table_locked = table->LockForRead();
    if (!table_locked->is_running()) {
      continue;
    }

    const string& keyspace = master_->catalog_manager()->GetNamespaceName(table->namespace_id());

    const auto& table_cat = GetTableType(*table);
    // Skip non-user tables if we should.
    if (table_cat != kUserIndex && table_cat != kUserTable) {
      continue;
    }
    const string& table_name = table_locked->name();
    const string& table_id = table->id();
    auto tablet_count = table->GetTablets(IncludeInactive::kTrue).size();

    *output << Format(
        "<tr>"
        "<td>$0</td>"
        "<td><a href=\"/table?id=$1\">$2</a></td>"
        "<td>$3</td>",
        EscapeForHtmlToString(keyspace),
        UrlEncodeToString(table_id),
        EscapeForHtmlToString(table_name),
        tablet_count);
    for (auto& tserver_desc : descs) {
      const string& tserver_id = tserver_desc->permanent_uuid();
      uint64 num_replicas = 0;
      uint64 num_leaders = 0;

      const auto& tserver_table_to_replicas_mapping = tserver_tree.at(tserver_id);
      auto it = tserver_table_to_replicas_mapping.find(table_id);
      if (it != tserver_table_to_replicas_mapping.end()) {
        auto replicas = it->second;
        num_replicas = replicas.size();
        num_leaders = std::count_if(
            replicas.begin(), replicas.end(),
            [](const ReplicaInfo& replicate) { return replicate.role == LEADER; });
      }
      *output << Format("<td>$0/$1</td>", num_replicas, num_leaders);
    }
    *output << "</tr>";
  }

  *output << "</table><!-- distribution table -->\n";
  *output << "</div> <!-- panel-body -->\n";
  *output << "</div> <!-- panel -->\n";
}

MasterPathHandlers::TableType MasterPathHandlers::GetTableType(const TableInfo& table) {
  string keyspace = master_->catalog_manager()->GetNamespaceName(table.namespace_id());
  bool is_platform = keyspace.compare(kSystemPlatformNamespace) == 0;

  TableType table_cat;
  // Determine the table category. Platform tables should be displayed as system tables.
  if (is_platform) {
    table_cat = kSystemTable;
  } else if (master_->catalog_manager()->IsUserIndex(table)) {
    table_cat = kUserIndex;
  } else if (master_->catalog_manager()->IsUserTable(table)) {
    table_cat = kUserTable;
  } else if (table.IsColocationParentTable()) {
    table_cat = kParentTable;
  } else {
    table_cat = kSystemTable;
  }
  return table_cat;
}
}  // namespace master
}  // namespace yb
