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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/common_consensus_util.h"
#include "yb/common/xcluster_util.h"

#include "yb/common/common_types_util.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/tablet_limits.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/dockv/partition.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/async_rbs_info_task.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/encryption_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_encryption.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_util.h"
#include "yb/master/object_lock_info_manager.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/tablet_creation_limits.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/master/xcluster/xcluster_status.h"

#include "yb/server/html_print_helper.h"
#include "yb/server/webserver.h"
#include "yb/server/webui_util.h"

#include "yb/tablet/tablet_types.pb.h"

#include "yb/tserver/remote_bootstrap_info.h"

#include "yb/util/curl_util.h"
#include "yb/util/flags.h"
#include "yb/util/hash_util.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/logging.h"
#include "yb/util/string_case.h"
#include "yb/util/timestamp.h"
#include "yb/util/url-coding.h"
#include "yb/common/version_info.h"

DEFINE_RUNTIME_int32(
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

DEFINE_test_flag(int32, sleep_before_reporting_lb_ui_ms, 0,
                 "Sleep before reporting tasks in the cluster balancer UI, to give tasks a chance "
                 "to complete.");

DECLARE_bool(enforce_tablet_replica_limits);
DECLARE_int32(ysql_tablespace_info_refresh_secs);
DECLARE_string(webserver_ca_certificate_file);
DECLARE_string(webserver_certificate_file);
DEPRECATE_FLAG(uint64, master_maximum_heartbeats_without_lease, "12_2023");

using namespace std::chrono_literals;

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

std::optional<HostPortPB> GetRpcBindAddress(const ServerRegistrationPB& reg) {
  if (reg.private_rpc_addresses().empty()) {
    return {};
  }
  return reg.private_rpc_addresses(0);
}

std::string BoolToString(bool val) { return val ? "true" : "false"; }

std::optional<uint64_t> ToUnsignedOrNullopt(int64_t val) {
  if (val == std::numeric_limits<int64_t>::max()) {
    return std::nullopt;
  } else {
    return val;
  }
}

std::string TSDescriptorToHtml(const std::string& ts_uuid,
                               std::optional<ConstRefWrap<master::TSDescriptor>> desc,
                               const std::string& tablet_id) {
  if (!desc) {
    return EscapeForHtmlToString(ts_uuid);
  }
  auto public_http_hp = GetPublicHttpHostPort(desc->get().GetRegistration());
  if (!public_http_hp) {
    return EscapeForHtmlToString(ts_uuid);
  }
  return Format("<a href=\"$0://$1/tablet?id=$2\">$3</a>",
                GetProtocol(),
                EscapeForHtmlToString(HostPortPBToString(*public_http_hp)),
                UrlEncodeToString(tablet_id),
                EscapeForHtmlToString(public_http_hp->host()));
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
  hidden_tablet_peers += other.hidden_tablet_peers;
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

std::string GetHttpHostPortFromServerRegistration(const ServerRegistrationPB& reg) {
  auto hp = GetPublicHttpHostPort(reg);
  if (hp) {
    return HostPortPBToString(*hp);
  }
  return "";
}

// Convert the specified server registration to HTML, adding a link
// to the server's own web server (if specified in 'reg') with
// anchor text 'link_text'.
std::string RegistrationToHtml(const ServerRegistrationPB& reg) {
  std::stringstream link_html;
  auto public_http_hp = GetPublicHttpHostPort(reg);
  if (public_http_hp) {
    link_html << Format(
        "<a href=\"$0://$1/\">$2</a>", GetProtocol(),
        EscapeForHtmlToString(HostPortPBToString(*public_http_hp)),
        EscapeForHtmlToString(GetHttpHostPortFromServerRegistration(reg)));
  }
  auto rpc_bind_addr = GetRpcBindAddress(reg);
  if (rpc_bind_addr && public_http_hp && rpc_bind_addr->host() != public_http_hp->host()) {
    link_html << Format("<br/>$0", EscapeForHtmlToString(rpc_bind_addr->host()));
  }

  return link_html.str();
}
}  // anonymous namespace

namespace {

bool TabletServerComparator(
    const std::shared_ptr<TSDescriptor>& a, const std::shared_ptr<TSDescriptor>& b) {
  auto a_cloud_info = a->GetRegistration().cloud_info();
  auto b_cloud_info = b->GetRegistration().cloud_info();

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

struct LocalTserverInfo {
  std::string server_id;
  MonoDelta time_since_heartbeat;
  std::string status;
  std::string color;
  std::string placement;

  explicit LocalTserverInfo(TSDescriptor& desc) {
    auto reg = desc.GetRegistration();

    server_id = Format("$0 </br> $1", RegistrationToHtml(reg), desc.permanent_uuid());
    time_since_heartbeat = desc.TimeSinceHeartbeat();

    color = "Green";
    if (desc.IsLive()) {
      status = Format("$0:$1", kTserverAlive, UptimeString(desc.uptime_seconds()));
    } else {
      color = "Red";
      status = kTserverDead;
    }

    placement = Format(
        "$0.$1.$2", EscapeForHtmlToString(reg.cloud_info().placement_cloud()),
        EscapeForHtmlToString(reg.cloud_info().placement_region()),
        EscapeForHtmlToString(reg.cloud_info().placement_zone()));
  }

  std::string FormattedStatus() const {
    return Format("<font color=\"$0\">$1</font>", color, status);
  }
};

}  // anonymous namespace

MasterPathHandlers::UniverseTabletCounts MasterPathHandlers::CalculateUniverseTabletCounts(
    const TabletCountMap& tablet_count_map, const std::vector<std::shared_ptr<TSDescriptor>>& descs,
    const BlacklistSet& blacklist_set,
    int hide_dead_node_threshold_mins) {
  UniverseTabletCounts counts;
  for (const auto& desc : descs) {
    if (ShouldHideTserverNodeFromDisplay(desc.get(), hide_dead_node_threshold_mins)) {
      continue;
    }
    const auto& placement_uuid = desc->placement_uuid();
    PlacementClusterTabletCounts& placement_cluster_counts =
        counts.per_placement_cluster_counts[placement_uuid];
    if (auto* tablet_count = FindOrNull(tablet_count_map, desc->permanent_uuid())) {
      placement_cluster_counts.counts += *tablet_count;
    }
    if (desc->IsBlacklisted(blacklist_set)) {
      placement_cluster_counts.blacklisted_node_count++;
    } else if (desc->IsLive()) {
      placement_cluster_counts.live_node_count++;
    } else {
      placement_cluster_counts.dead_node_count++;
    }
    placement_cluster_counts.active_tablet_peer_count += desc->num_live_replicas();
  }

  auto limits = GetTabletReplicaPerResourceLimits();
  for (auto& [placement_uuid, cluster_counts] : counts.per_placement_cluster_counts) {
    auto cluster_info = ComputeAggregatedClusterInfo(descs, blacklist_set, placement_uuid);
    cluster_counts.tablet_replica_limit =
        ToUnsignedOrNullopt(ComputeTabletReplicaLimit(cluster_info, limits));
  }
  return counts;
}

void MasterPathHandlers::TServerDisplay(
    const std::string& current_uuid, const std::vector<std::shared_ptr<TSDescriptor>>& descs,
    const TabletCountMap& tablet_map, HtmlPrintHelper& html_print_helper) {
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

  auto html_table = html_print_helper.CreateTablePrinter(
      Format("$0_tserver", current_uuid),
      {"Server", "Time since heartbeat", "Status & Uptime", "User Tablet-Peers / Leaders",
       "System Tablet-Peers / Leaders", "RAM Used", "Num SST Files", "Total SST Files Size",
       "Uncompressed SST </br>Files Size", "Read ops/sec", "Write ops/sec", "Placement",
       "Active Tablet-Peers"});

  int max_peers = 0;
  for (const auto& desc : descs) {
    if (desc->placement_uuid() == current_uuid && desc->num_live_replicas() > max_peers) {
      max_peers = desc->num_live_replicas();
    }
  }

  for (const auto& desc : descs) {
    if (desc->placement_uuid() != current_uuid) {
      continue;
    }
    auto reg = desc->GetRegistration();
    auto& html_row = html_table.AddRow();

    LocalTserverInfo tserver_info(*desc);
    auto ci = reg.cloud_info();
    for (size_t i = 0; i < affinitized_zones.size(); i++) {
      if (affinitized_zones[i].find(ci) != affinitized_zones[i].end()) {
        tserver_info.server_id += Format("</br>  Leader preference priority: $0", i + 1);
        break;
      }
    }
    html_row.AddColumn(std::move(tserver_info.server_id));
    html_row.AddColumn(StringPrintf("%.1fs", tserver_info.time_since_heartbeat.ToSeconds()));

    if (desc->IsBlacklisted(blacklist)) {
      tserver_info.color = tserver_info.color == "Green" ? kYBOrange : tserver_info.color;
      tserver_info.status += "</br>Blacklisted";
    }
    if (desc->IsBlacklisted(leader_blacklist)) {
      tserver_info.color = tserver_info.color == "Green" ? kYBOrange : tserver_info.color;
      tserver_info.status += "</br>Leader Blacklisted";
    }

    html_row.AddColumn(
        Format("<font color=\"$0\">$1</font>", tserver_info.color, tserver_info.status));

    auto counts = FindOrNull(tablet_map, desc->permanent_uuid());
    if (counts) {
      html_row.AddColumn(Format(
          "$0 / $1", (counts->user_tablet_leaders + counts->user_tablet_followers),
          counts->user_tablet_leaders));
      html_row.AddColumn(Format(
          "$0 / $1", (counts->system_tablet_leaders + counts->system_tablet_followers),
          counts->system_tablet_leaders));
    } else {
      html_row.AddColumn("0 / 0");
      html_row.AddColumn("0 / 0");
    }

    html_row.AddColumn(HumanizeBytes(desc->total_memory_usage()));
    html_row.AddColumn(desc->num_sst_files());
    html_row.AddColumn(HumanizeBytes(desc->total_sst_file_size()));
    html_row.AddColumn(HumanizeBytes(desc->uncompressed_sst_file_size()));
    html_row.AddColumn(desc->read_ops_per_sec());
    html_row.AddColumn(desc->write_ops_per_sec());

    html_row.AddColumn(tserver_info.placement);

    html_row.AddColumn(counts ? desc->num_live_replicas() : 0);
  }
  html_table.Print();
}

void TServerClockDisplay(
    const std::string& current_uuid, std::vector<std::shared_ptr<TSDescriptor>>& descs,
    HtmlPrintHelper& html_print_helper) {
  auto html_table = html_print_helper.CreateTablePrinter(
      Format("$0_tserver", current_uuid),
      {"Server", "Time since heartbeat", "Status & Uptime", "Physical Time (UTC)",
       "Hybrid Time (UTC)", "Heartbeat RTT", "Placement"});

  for (const auto& desc : descs) {
    if (desc->placement_uuid() != current_uuid) {
      continue;
    }
    auto& html_row = html_table.AddRow();

    LocalTserverInfo tserver_info(*desc);

    html_row.AddColumn(std::move(tserver_info.server_id));
    html_row.AddColumn(StringPrintf("%.1fs", tserver_info.time_since_heartbeat.ToSeconds()));
    html_row.AddColumn(tserver_info.FormattedStatus());

    // Render physical time.
    const Timestamp p_ts(desc->physical_time());
    html_row.AddColumn(p_ts.ToHumanReadableTime(UseUTC::kTrue));

    // Render the physical and logical components of the hybrid time.
    const HybridTime ht = desc->hybrid_time();
    const Timestamp h_ts(ht.GetPhysicalValueMicros());
    {
      auto uptime = h_ts.ToHumanReadableTime(UseUTC::kTrue);
      const auto logical_value = ht.GetLogicalValue();
      if (logical_value) {
        uptime += Format(" / Logical: $0", logical_value);
      }
      html_row.AddColumn(std::move(uptime));
    }

    html_row.AddColumn(StringPrintf("%.2fms", desc->heartbeat_rtt().ToMicroseconds() / 1000.0));

    html_row.AddColumn(tserver_info.placement);
  }
  html_table.Print();
}

void MasterPathHandlers::DisplayUniverseSummary(
    const TabletCountMap& tablet_map, const std::vector<std::shared_ptr<TSDescriptor>>& all_descs,
    const std::string& live_id, int hide_dead_node_threshold_mins,
    HtmlPrintHelper& html_print_helper) {
  auto blacklist_result = master_->catalog_manager()->BlacklistSetFromPB();
  BlacklistSet blacklist = blacklist_result.ok() ? *blacklist_result : BlacklistSet();
  auto universe_counts = CalculateUniverseTabletCounts(
      tablet_map, all_descs, blacklist, hide_dead_node_threshold_mins);

  auto tablet_peer_limit_col_name =
      Format("Tablet Peer Limit $0", FLAGS_enforce_tablet_replica_limits ? "" : "(Unenforced)");
  auto html_table = html_print_helper.CreateTablePrinter(
      "universe_summary",
      {"Cluster UUID", "Total Live TServers", "Total Blacklisted TServers", "Total Dead TServers",
       "User Tablet-Peers", "System Tablet-Peers", "Hidden Tablet-Peers", "Active Tablet-Peers",
       tablet_peer_limit_col_name});
  for (const auto& [placement_uuid, cluster_counts] :
       universe_counts.per_placement_cluster_counts) {
    auto placement_uuid_entry = Format(
        "$0 $1", placement_uuid == live_id ? "Primary Cluster" : "Read Replica", placement_uuid);
    std::string limit_entry = "N/A";
    if (cluster_counts.tablet_replica_limit.has_value()) {
      limit_entry = Format(
          cluster_counts.active_tablet_peer_count > *cluster_counts.tablet_replica_limit
              ? "<b><font color=\"red\">$0</font></b>"
              : "$0",
          *cluster_counts.tablet_replica_limit);
    }

    auto user_total =
        cluster_counts.counts.user_tablet_followers + cluster_counts.counts.user_tablet_leaders;
    auto system_total =
        cluster_counts.counts.system_tablet_followers + cluster_counts.counts.system_tablet_leaders;
    html_table.AddRow(
        std::move(placement_uuid_entry), cluster_counts.live_node_count,
        cluster_counts.blacklisted_node_count, cluster_counts.dead_node_count, user_total,
        system_total, cluster_counts.counts.hidden_tablet_peers,
        cluster_counts.active_tablet_peer_count, std::move(limit_entry));
  }
  html_table.Print();
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
    CloudInfoPB cloud_info = descriptor->GetRegistration().cloud_info();
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

  auto cluster_config_result = master_->catalog_manager_impl()->GetClusterConfig();
  if (!cluster_config_result.ok()) {
    *output << "<div class=\"alert alert-warning\">"
            << EscapeForHtmlToString(cluster_config_result.status().ToString()) << "</div>";
    return;
  }

  auto live_id = cluster_config_result->replication_info().live_replicas().placement_uuid();

  auto descs = master_->ts_manager()->GetAllDescriptors();

  // Get user and system tablet leader and follower counts for each TabletServer
  TabletCountMap tablet_map;
  auto s = CalculateTabletMap(&tablet_map);
  if (!s.ok()) {
    *output << "<div class=\"alert alert-warning\">"
            << EscapeForHtmlToString(s.ToString()) << "</div>";
    return;
  }

  std::unordered_set<string> read_replica_uuids;
  for (auto desc : descs) {
    if (!read_replica_uuids.count(desc->placement_uuid()) && desc->placement_uuid() != live_id) {
      read_replica_uuids.insert(desc->placement_uuid());
    }
  }

  *output << std::setprecision(output_precision_);

  HtmlPrintHelper html_print_helper(*output);
  if (viewType == TServersViewType::kTServersDefaultView) {
    *output << "<h2>Universe Summary</h2>\n";
    DisplayUniverseSummary(
        tablet_map, descs, live_id, hide_dead_node_threshold_override, html_print_helper);
  }
  *output << "<h2>Tablet Servers</h2>\n";

  if (!live_id.empty()) {
    *output << "<h3 style=\"color:" << kYBDarkBlue << "\">Primary Cluster UUID: "
            << live_id << "</h3>\n";
  }

  std::erase_if(
      descs, [hide_dead_node_threshold_override](const std::shared_ptr<TSDescriptor>& desc) {
        return ShouldHideTserverNodeFromDisplay(desc.get(), hide_dead_node_threshold_override);
      });

  // Comparator orders by cloud, region, zone and uuid fields.
  std::sort(descs.begin(), descs.end(), &TabletServerComparator);

  if (viewType == TServersViewType::kTServersDefaultView) {
    TServerDisplay(live_id, descs, tablet_map, html_print_helper);
  } else {
    TServerClockDisplay(live_id, descs, html_print_helper);
  }

  for (const auto& read_replica_uuid : read_replica_uuids) {
    *output << "<h3 style=\"color:" << kYBDarkBlue << "\">Read Replica UUID: "
            << (read_replica_uuid.empty() ? kNoPlacementUUID : read_replica_uuid) << "</h3>\n";
    if (viewType == TServersViewType::kTServersDefaultView) {
      TServerDisplay(read_replica_uuid, descs, tablet_map, html_print_helper);
    } else {
      TServerClockDisplay(read_replica_uuid, descs, html_print_helper);
    }
  }

  if (viewType == TServersViewType::kTServersDefaultView) {
    *output << "<p>  *Placement policy, Preferred zones, and Node Blacklist will affect the Peer "
               "and Leader distribution.</p>";

    if (master_->catalog_manager()->IsLoadBalancerEnabled()) {
      IsLoadBalancedRequestPB req;
      IsLoadBalancedResponsePB resp;
      Status cluster_balancer_idle = master_->catalog_manager()->IsLoadBalanced(&req, &resp);
      if (cluster_balancer_idle.ok()) {
        *output << "<h4 style=\"color:Green\"><i class='fa fa-tasks yb-dashboard-icon' "
                   "aria-hidden='true'></i>Cluster Balancer is Idle</h4>\n";
      } else {
        *output
            << "<h4 style=\"color:" << kYBOrange
            << "\"><i class='fa fa-tasks yb-dashboard-icon' aria-hidden='true'></i>Cluster Load "
               "is not Idle</h4>\n";
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

  auto cluster_config_result = master_->catalog_manager()->GetClusterConfig();
  if (!cluster_config_result.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(cluster_config_result.status().ToString());
    return;
  }
  auto descs = master_->ts_manager()->GetAllDescriptors();
  // Get user and system tablet leader and follower counts for each TabletServer.
  TabletCountMap tablet_map;
  auto s = CalculateTabletMap(&tablet_map);
  if (!s.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(Format("Couldn't get tablets: $0", s));
  }

  std::unordered_set<string> cluster_uuids;
  auto primary_uuid = cluster_config_result->replication_info().live_replicas().placement_uuid();
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
        auto reg = desc->GetRegistration();
        string host_port = GetHttpHostPortFromServerRegistration(reg);
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

        jw.String("start_time_us");
        jw.Uint64(desc->start_time_us());

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

        const auto& cloud_info = reg.cloud_info();

        jw.String("cloud");
        jw.String(cloud_info.placement_cloud());

        jw.String("region");
        jw.String(cloud_info.placement_region());

        jw.String("zone");
        jw.String(cloud_info.placement_zone());

        jw.String("permanent_uuid");
        jw.String(desc->permanent_uuid());

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

  auto cluster_config_result = master_->catalog_manager_impl()->GetClusterConfig();
  if (!cluster_config_result.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(cluster_config_result.status().ToString());
    return;
  }
  auto replication_factor = master_->catalog_manager()->GetReplicationFactor();
  if (!replication_factor.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(replication_factor.status().ToString());
    return;
  }

  auto descs = master_->ts_manager()->GetAllDescriptors();

  const auto& live_placement_uuid =
      cluster_config_result->replication_info().live_replicas().placement_uuid();
  // Ignore read replica health for V1.

  vector<std::shared_ptr<TSDescriptor> > dead_nodes;
  uint64_t most_recent_uptime = std::numeric_limits<uint64_t>::max();

  jw.StartObject();
  std::vector<TableId> skipped_tables;
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
      if (!table->IsUserTable() &&
          table->GetTableType() != REDIS_TABLE_TYPE &&
          table->GetTableType() != TRANSACTION_STATUS_TABLE_TYPE &&
          !(table->namespace_id() == kSystemNamespaceId &&
            table->name() == kMetricsSnapshotsTableName)) {
        continue;
      }

      auto tablets_result = table->GetTablets();
      if (!tablets_result) {
        skipped_tables.push_back(table->id());
        continue;
      }
      auto tablets = *tablets_result;

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
    // 4. is the cluster balancer busy moving tablets/leaders around
    /* Use: Status IsLoadBalancerIdle(const IsLoadBalancerIdleRequestPB* req,
                                              IsLoadBalancerIdleResponsePB* resp);
     */
    // 5. do any of the TS have tablets they were not able to start up
  }
  if (!skipped_tables.empty()) {
    jw.String("error");
    jw.StartObject();
    jw.String("skipped tables");
    jw.StartArray();
    for (const auto& table : skipped_tables) {
      jw.String(table);
    }
    jw.EndArray();
    jw.EndObject();
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

void MasterPathHandlers::HandleAllTables(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp, bool only_user_tables) {
  std::stringstream* output = &resp->output;
  HtmlPrintHelper html_print_helper(*output);

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
    string keyspace = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    TableType table_cat = GetTableType(*table);

    auto table_locked = table->LockForRead();
    if (!table_locked->is_running()) {
      continue;
    }

    string table_uuid = table->id();

    // Skip non-user tables if we should.
    if (only_user_tables && table_cat != kUserIndex && table_cat != kUserTable) {
      continue;
    }

    auto& table_row = ordered_tables[table_cat][table_uuid];
    table_row[kKeyspace] = EscapeForHtmlToString(keyspace);
    string href_table_id = table_uuid;
    string table_name = table_locked->name();
    table_row[kState] = SysTablesEntryPB_State_Name(table_locked->pb.state());
    Capitalize(&table_row[kState]);
    if (table_locked->is_hidden()) {
      table_row[kState] += " (HIDDEN)";
    }
    table_row[kMessage] = EscapeForHtmlToString(table_locked->pb.state_msg());

    if (table->GetTableType() == PGSQL_TABLE_TYPE && table_cat != kParentTable) {
      const auto result = table->GetPgTableOid();
      if (result.ok()) {
        table_row[kYsqlOid] = std::to_string(*result);
      } else {
        LOG(WARNING) << "Failed to get OID of '" << table_uuid << "' ysql table";
      }

      const auto& schema = table_locked->schema();
      if (schema.has_colocated_table_id() && schema.colocated_table_id().has_colocation_id()) {
        table_row[kColocationId] = Format("$0", schema.colocated_table_id().colocation_id());
        has_colocated_tables[table_cat] = true;
      }

      if (table->IsSecondaryTable() && !table_locked->is_vector_index()) {
        auto colocated_tablets = table->GetTablets();
        if (colocated_tablets.ok()) {
          const auto parent_table = colocated_tablets->front()->table();
          table_row[kParentOid] = GetParentTableOid(parent_table);
          has_tablegroups[table_cat] = true;
        }
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
    if (table_cat != kSystemTable && !table->IsSecondaryTable()) {
      TabletReplicaDriveInfo aggregated_drive_info;
      auto tablets_result = table->GetTablets();
      if (!tablets_result) continue;
      bool table_has_missing_size = false;
      for (const auto& tablet : *tablets_result) {
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

    auto fs = html_print_helper.CreateFieldset(table_type_[type_index] + " tables");

    if (ordered_tables[type_index].empty()) {
      (*output) << "There are no " << static_cast<char>(tolower(table_type_[type_index][0]))
                << table_type_[type_index].substr(1) << " tables.\n";
    } else {
      std::vector<std::string> columns = {"Keyspace", "Table Name", "State",
                                          "Message",  "UUID",       "YSQL OID"};

      if (type_index == kUserTable || type_index == kUserIndex) {
        if (has_tablegroups[type_index]) {
          columns.push_back("Parent OID");
        }

        if (has_colocated_tables[type_index]) {
          columns.push_back("Colocation ID");
        }
      }

      if (type_index != kSystemTable) {
        columns.push_back("On-disk size");
      }

      auto html_table = html_print_helper.CreateTablePrinter("table", type_index, columns);

      for (const StringMap::value_type& table : ordered_tables[type_index]) {
        auto& row = html_table.AddRow();

        row.AddColumn(table.second[kKeyspace]);
        row.AddColumn(table.second[kTableName]);
        row.AddColumn(table.second[kState]);
        row.AddColumn(table.second[kMessage]);
        row.AddColumn(table.second[kUuid]);
        row.AddColumn(table.second[kYsqlOid]);

        if (type_index == kUserTable || type_index == kUserIndex) {
          if (has_tablegroups[type_index]) {
            row.AddColumn(table.second[kParentOid]);
          }

          if (has_colocated_tables[type_index]) {
            row.AddColumn(table.second[kColocationId]);
          }
        }

        if (type_index != kSystemTable) {
          row.AddColumn(table.second[kOnDiskSize]);
        }
      }
      html_table.Print();

      if (show_missing_size_footer[type_index]) {
        (*output) << "<p>* Some tablets did not provide disk size estimates,"
                  << " and were not added to the displayed totals.</p>";
      }
    }
  }
}

void MasterPathHandlers::HandleAllTablesJSON(
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
    string keyspace = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    auto table_locked = table->LockForRead();
    if (!table_locked->is_running()) {
      continue;
    }

    string table_uuid = table->id();

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
      const auto result = table->GetPgTableOid();
      if (result.ok()) {
        table_row.ysql_oid = std::to_string(*result);
      } else {
        LOG(WARNING) << "Failed to get OID of '" << table_uuid << "' ysql table";
      }

      const auto& schema = table_locked->schema();
      if (schema.has_colocated_table_id() && schema.colocated_table_id().has_colocation_id()) {
        table_row.colocation_id = Format("$0", schema.colocated_table_id().colocation_id());
      }

      if (table->IsSecondaryTable() && !table_locked->is_vector_index()) {
        auto colocated_tablets = table->GetTablets();
        if (colocated_tablets.ok()) {
          const auto parent_table = colocated_tablets->front()->table();
          table_row.parent_oid = GetParentTableOid(parent_table);
        }
      }
    } else if (table_cat == kParentTable) {
      // Colocated parent table.
      table_row.ysql_oid = GetParentTableOid(table);
    }

    // System tables and colocated user tables do not have size info.
    if (table_cat != kSystemTable && !table->IsSecondaryTable()) {
      TabletReplicaDriveInfo aggregated_drive_info;
      auto tablets_result = table->GetTablets();
      if (!tablets_result) continue;
      bool table_has_missing_size = false;
      for (const auto& tablet : *tablets_result) {
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

void MasterPathHandlers::HandleGetMetaCacheJson(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  JsonWriter writer(output, JsonWriter::COMPACT);
  master_->WriteServerMetaCacheAsJson(&writer);
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

bool CompareByHost(
    const TsUuidAndTabletReplica& a,
    const TsUuidAndTabletReplica& b) {
  return a.first.get() < b.first.get();
}

std::vector<TsUuidAndTabletReplica>
TabletReplicaMapToSortedVector(const TabletReplicaMap& replicas) {
  std::vector<TsUuidAndTabletReplica> sorted_replicas;
  sorted_replicas.reserve(replicas.size());
  for (const auto& [ts_uuid, replica] : replicas) {
    sorted_replicas.push_back(std::pair(std::cref(ts_uuid), replica));
  }
  std::sort(sorted_replicas.begin(), sorted_replicas.end(), &CompareByHost);
  return sorted_replicas;
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
    keyspace_name = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    auto l = table->LockForRead();
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
    Result<TabletInfos> tablets_result = table->GetTabletsIncludeInactive();
    if (!tablets_result) {
      *output << "Unable to fetch tablets for table: " << EscapeForHtmlToString(s.ToString());
      return;
    }
    tablets = *tablets_result;
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
  *output << "  <tr><th>Tablet ID</th><th>Partition</th><th>SplitDepth</th><th>RaftConfig / "
             "Replica info</th><th>State</th><th>Hidden</th><th>Message</th></tr>\n";

  std::sort(tablets.begin(), tablets.end(), [](const TabletInfoPtr& t1, const TabletInfoPtr& t2) {
    auto l1 = t1->LockForRead();
    auto l2 = t2->LockForRead();
    auto res = Slice(l1->pb.partition().partition_key_start())
                   .compare(l2->pb.partition().partition_key_start());
    // Parent tablet goes first in case of partition_key_start equality.
    return res == 0 ? (l1->pb.split_depth() < l2->pb.split_depth()) : res < 0;
  });

  for (const auto& tablet : tablets) {
    if (!show_deleted_tablets && tablet->LockForRead()->is_deleted()) {
      continue;
    }
    std::shared_ptr<const TabletReplicaMap> tablet_replica_map = tablet->GetReplicaLocations();
    auto sorted_locations = TabletReplicaMapToSortedVector(*tablet_replica_map);
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
        ReplicaInfoToHtml(sorted_locations, tablet->tablet_id()),
        state,
        l->is_hidden(),
        EscapeForHtmlToString(l->pb.state_msg()));
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

string TSDescriptorToJson(const std::string& ts_uuid,
                          const std::optional<ConstRefWrap<TSDescriptor>> desc,
                          const std::string& tablet_id) {
  if (!desc) {
    return EscapeForHtmlToString(ts_uuid);
  }
  auto public_http_hp = GetPublicHttpHostPort(desc->get().GetRegistration());
  if (!public_http_hp) {
    return EscapeForHtmlToString(ts_uuid);
  }
  return Format("$0://$1/tablet?id=$2",
                GetProtocol(),
                HostPortPBToString(*public_http_hp),
                UrlEncodeToString(tablet_id));
}

void RaftConfigToJson(
    const std::vector<TsUuidAndTabletReplica>&
        locations,
    const std::string& tablet_id, JsonWriter* jw) {
  jw->String("locations");
  jw->StartArray();
  for (const auto& [ts_uuid, replica] : locations) {
    jw->StartObject();
    jw->String("uuid");
    jw->String(ts_uuid);
    jw->String("role");
    jw->String(PeerRole_Name(replica.role));
    jw->String("location");
    auto ts_desc_ptr = replica.ts_desc.lock();
    jw->String(
        TSDescriptorToJson(ts_uuid, opt_cref(ts_desc_ptr), tablet_id));
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
    auto keyspace_name = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    TableName table_name;
    auto l = table->LockForRead();
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
      jw.String("error");
      jw.String("Unable to decode partition schema: " + s.ToString());
      jw.EndObject();
      return;
    }
    auto tablets_result = table->GetTabletsIncludeInactive();
    if (!tablets_result) {
      jw.String("error");
      jw.String("Unable to fetch tablets: " + s.ToString());
      jw.EndObject();
      return;
    }
    tablets = *tablets_result;
  }

  JsonOutputSchemaTable(schema, &jw);

  jw.String("tablets");
  jw.StartArray();
  for (const TabletInfoPtr& tablet : tablets) {
    std::shared_ptr<const TabletReplicaMap> tablet_replica_map = tablet->GetReplicaLocations();
    auto sorted_locations = TabletReplicaMapToSortedVector(*tablet_replica_map);
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

Result<std::vector<TabletInfoPtr>> MasterPathHandlers::GetNonSystemTablets() {
  std::vector<TabletInfoPtr> nonsystem_tablets;

  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);

  for (const auto& table : tables) {
    if (table->is_system()) {
      continue;
    }
    TabletInfos ts = VERIFY_RESULT(table->GetTabletsIncludeInactive());
    nonsystem_tablets.insert(
        nonsystem_tablets.end(), std::make_move_iterator(ts.begin()),
        std::make_move_iterator(ts.end()));
  }
  return nonsystem_tablets;
}

Result<std::vector<std::pair<TabletInfoPtr, std::string>>>
MasterPathHandlers::GetLeaderlessTablets() {
  std::vector<std::pair<TabletInfoPtr, std::string>> leaderless_tablets;

  auto nonsystem_tablets = VERIFY_RESULT(GetNonSystemTablets());

  for (TabletInfoPtr t : nonsystem_tablets) {
    if (t.get()->LockForRead()->is_deleted()) {
      continue;
    }
    const auto time_since_valid_leader_secs =
        MonoTime::Now().GetDeltaSince(t->last_time_with_valid_leader()).ToSeconds();
    if (time_since_valid_leader_secs >
            FLAGS_leaderless_tablet_alert_delay_secs) {
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
    auto ts_desc = replica.ts_desc.lock();
    if (!ts_desc) {
      LOG(WARNING) << Format("Descriptor for ts $0 is no longer valid", ts_uuid);
      continue;
    }
    VLOG_WITH_FUNC(1) << "Processing tablet replica on TS " << ts_uuid;
    for (auto* placement : placements) {
      if (placement->placement_uuid() == ts_desc->placement_uuid()) {
        VLOG_WITH_FUNC(1) << "TS matches placement id " << placement->placement_uuid();
        placement->set_num_replicas(placement->num_replicas() - 1);

        // Decrement a matching placement block which has a positive min_num_replicas.
        // There will only be one matching placement string (we cannot have c.r.z1 and c.r.*), but
        // replication_info may contain multiple copies of that string, in which case the
        // min_num_replicas are effectively added across the copies.
        // I.e., {c.r.z1: min_num_replicas = 1, c.r.z1: min_num_replicas = 1} is equivalent to
        //       {c.r.z1: min_num_replicas = 2}
        for (int i = 0; i < placement->placement_blocks_size(); ++i) {
          const auto& placement_block = placement->placement_blocks(i);
          if (placement_block.min_num_replicas() <= 0) {
            continue;
          }
          if (ts_desc->MatchesCloudInfo(placement_block.cloud_info())) {
            VLOG_WITH_FUNC(1) << "TS matches placement block "
                              << placement_block.ShortDebugString();
            placement->mutable_placement_blocks(i)->set_min_num_replicas(
                placement_block.min_num_replicas() - 1);
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
    if (table->is_system()) {
      continue;
    }
    auto replication_info = VERIFY_RESULT(catalog_mgr->GetTableReplicationInfo(table));
    for (TabletInfoPtr tablet : VERIFY_RESULT(table->GetTablets())) {
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

  if (!leaderless_tablets || !underreplicated_tablets) {
    *output << "<h2>Cannot calculate tablet replicas. Try again.</h2>\n";
    return;
  }

  *output << "<h3>Leaderless Tablets</h3>\n";
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Table Name</th><th>Table UUID</th><th>Tablet ID</th>"
             "<th>Leaderless Reason</th></tr>\n";

  for (const std::pair<TabletInfoPtr, string>& t : *leaderless_tablets) {
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
  if (!leaderless_ts) {
    jw.StartObject();
    jw.String("error");
    jw.String(leaderless_ts.status().ToString());
    jw.EndObject();
    return;
  }

  jw.StartObject();
  jw.String("leaderless_tablets");
  jw.StartArray();

  for (const std::pair<TabletInfoPtr, std::string>& t : *leaderless_ts) {
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
    jw.String("error");
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

  auto cluster_config_result = master_->catalog_manager_impl()->GetClusterConfig();
  if (!cluster_config_result.ok()) {
    *output << "<div class=\"alert alert-warning\">"
            << EscapeForHtmlToString(cluster_config_result.status().ToString()) << "</div>";
    return;
  }
  const auto& config = *cluster_config_result;

  // Get all the tables.
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);

  // Get the list of user tables.
  std::vector<TableInfoPtr> user_tables;
  for (auto& table : tables) {
    if (table->IsUserTable()) {
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

  // Cluster balancer status.
  bool load_balancer_enabled = master_->catalog_manager()->IsLoadBalancerEnabled();
  (*output) << Format(" <tr><td>$0<span class='yb-overview'>$1</span></td>"
                          "<td><i class='fa $2' aria-hidden='true'> </i></td></tr>\n",
                          "<i class='fa fa-tasks yb-dashboard-icon' aria-hidden='true'></i>",
                          "Cluster Balancer Enabled",
                          load_balancer_enabled ? "fa-check"
                                                : "fa-times label label-danger");
  if (load_balancer_enabled) {
    IsLoadBalancedRequestPB req;
    IsLoadBalancedResponsePB resp;
    Status load_balanced = master_->catalog_manager()->IsLoadBalanced(&req, &resp);

    (*output) << Format(" <tr><td>$0<span class='yb-overview'>$1</span></td>"
                            "<td><i class='fa $2' aria-hidden='true'> </i></td></tr>\n",
                            "<i class='fa fa-tasks yb-dashboard-icon' aria-hidden='true'></i>",
                            "Is Cluster Load Balanced?",
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
    string reg_text = RegistrationToHtml(reg);
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
  auto cluster_config_result = master_->catalog_manager_impl()->GetClusterConfig();
  if (!cluster_config_result.ok()) {
    *output << "<div class=\"alert alert-warning\">"
            << EscapeForHtmlToString(cluster_config_result.status().ToString()) << "</div>";
    return;
  }

  *output << "<div class=\"alert alert-success\">Successfully got cluster config!</div>"
          << "<pre class=\"prettyprint\">"
          << EscapeForHtmlToString(cluster_config_result->DebugString()) << "</pre>";
}

void MasterPathHandlers::HandleGetClusterConfigJSON(
  const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  JsonWriter jw(output, JsonWriter::COMPACT);

  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  auto cluster_config_result = master_->catalog_manager_impl()->GetClusterConfig();
  if (!cluster_config_result.ok()) {
    jw.StartObject();
    jw.String("error");
    jw.String(cluster_config_result.status().ToString());
    jw.EndObject();
    return;
  }

  // return cluster config in JSON format
  jw.Protobuf(*cluster_config_result);
}

void MasterPathHandlers::GetXClusterJSON(std::stringstream& output, bool pretty) {
  JsonWriter jw(&output, pretty ? JsonWriter::PRETTY : JsonWriter::COMPACT);
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();

  jw.StartObject();
  auto s = master_->xcluster_manager()->PopulateXClusterStatusJson(jw);
  if (!s.ok()) {
    jw.String("error");
    jw.String(s.ToString());
  }
  jw.EndObject();
  return;
}

void MasterPathHandlers::HandleGetXClusterJSON(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  GetXClusterJSON(resp->output, /*pretty=*/false);
}

void MasterPathHandlers::HandleXCluster(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream& output = resp->output;
  HtmlPrintHelper html_print_helper(output);

  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();
  auto xcluster_status_result = master_->xcluster_manager()->GetXClusterStatus();

  if (!xcluster_status_result.ok()) {
    output << "<div class=\"alert alert-warning\">"
           << EscapeForHtmlToString(xcluster_status_result.status().ToString()) << "</div>";
    return;
  }
  const auto& xcluster_status = *xcluster_status_result;

  if (!xcluster_status.IsEnabled()) {
    output << "<h3>xCluster replication is not enabled</h3 >\n";
    return;
  }
  output << "<h1>xCluster state</h1>\n";

  uint32 outbound_group_table_id = 0;
  if (!xcluster_status.outbound_replication_group_statuses.empty()) {
    output << "<br><h3>Outbound Replication Groups</h3>\n";
    for (const auto& outbound_replication_group :
         xcluster_status.outbound_replication_group_statuses) {
      auto group_fs = html_print_helper.CreateFieldset(
          Format("Group: $0", outbound_replication_group.replication_group_id));

      output << "<pre class=\"prettyprint\">" << "state: " << outbound_replication_group.state;
      output << "\nddl_mode: "
             << (outbound_replication_group.automatic_ddl_mode ? "automatic" : "semi-automatic");
      if (!outbound_replication_group.target_universe_info.empty()) {
        output << "\ntarget_universe_info: " << outbound_replication_group.target_universe_info;
      }
      output << "</pre>";

      for (const auto& namespace_status : outbound_replication_group.namespace_statuses) {
        auto namespace_fs = html_print_helper.CreateFieldset(
            Format("Namespace: $0", namespace_status.namespace_name));

        output << "<pre class=\"prettyprint\">";
        output << "NamespaceId: " << namespace_status.namespace_id
               << "\nstate: " << namespace_status.state << "\ninitial_bootstrap_required: "
               << BoolToString(namespace_status.initial_bootstrap_required)
               << "\nstatus: " << namespace_status.status;
        output << "</pre>";

        auto outbound_replication_group = html_print_helper.CreateTablePrinter(
            "outbound_replication_group", outbound_group_table_id,
            {"Table name", "Table Id", "Stream Id", "State", "Checkpointing",
             "Part of initial bootstrap"});
        outbound_group_table_id++;

        for (const auto& table_status : namespace_status.table_statuses) {
          outbound_replication_group.AddRow(
              table_status.full_table_name, table_status.table_id, table_status.stream_id,
              table_status.state, BoolToString(table_status.is_checkpointing),
              BoolToString(table_status.is_part_of_initial_bootstrap));
        }
        outbound_replication_group.Print();
      }
    }
  }

  if (!xcluster_status.outbound_table_stream_statuses.empty()) {
    output << "<br><h3>Outbound table streams</h3>\n";
    auto outbound_streams = html_print_helper.CreateTablePrinter(
        "outbound_table_streams", {"Table name", "Table Id", "Stream Id", "State"});
    for (const auto& table_status : xcluster_status.outbound_table_stream_statuses) {
      outbound_streams.AddRow(
          table_status.full_table_name, table_status.table_id, table_status.stream_id,
          table_status.state);
    }
    outbound_streams.Print();
  }

  output << "<br><h3>Inbound Replication Groups</h3>\n";

  uint32 inbound_group_table_id = 0;
  for (const auto& inbound_replication_group : xcluster_status.inbound_replication_group_statuses) {
    auto group_fs = html_print_helper.CreateFieldset(
        Format("Group: $0", inbound_replication_group.replication_group_id));

    output << "<pre class=\"prettyprint\">"
           << "state: " << inbound_replication_group.state
           << (inbound_replication_group.disable_stream ? " (DISABLED)" : "") << "\ntype: "
           << xcluster::ShortReplicationType(inbound_replication_group.replication_type)
           << "\nmaster_addrs: " << inbound_replication_group.master_addrs;
    if (!inbound_replication_group.db_scoped_info.empty()) {
      output << "\ndb_scoped_info: " << inbound_replication_group.db_scoped_info;
    }
    output << "\nvalidated_local_auto_flags_config_version: "
           << inbound_replication_group.validated_local_auto_flags_config_version
           << "\nvalidated_remote_auto_flags_config_version: "
           << inbound_replication_group.validated_remote_auto_flags_config_version
           << "\ncompatible_auto_flag_config_version: "
           << inbound_replication_group.compatible_auto_flag_config_version;
    output << "</pre>";

    for (const auto& [namespace_name, table_statuses] :
         inbound_replication_group.table_statuses_by_namespace) {
      auto namespace_fs = html_print_helper.CreateFieldset(Format("Namespace: $0", namespace_name));

      auto inbound_replication_group = html_print_helper.CreateTablePrinter(
          "inbound_replication_group", inbound_group_table_id,
          {"Table name", "Producer Table Id", "Stream Id", "Consumer Table Id",
           "Producer Tablet Count", "Consumer Tablet Count", "Local tserver optimized",
           "Producer schema version", "Consumer schema version", "Status"});
      inbound_group_table_id++;
      for (const auto& table_status : table_statuses) {
        inbound_replication_group.AddRow(
            table_status.full_table_name, table_status.source_table_id, table_status.stream_id,
            table_status.target_table_id, table_status.source_tablet_count,
            table_status.target_tablet_count, BoolToString(table_status.local_tserver_optimized),
            table_status.source_schema_version, table_status.target_schema_version,
            table_status.status);
      }
      inbound_replication_group.Print();
    }
  }
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

namespace {
void RenderRbsInfo(
    CatalogManagerIf* cm,
    const std::unordered_map<TabletServerId, tserver::GetActiveRbsInfoResponsePB>& responses,
    HtmlPrintHelper& html_print_helper, std::stringstream* output) {

  // Render the RBS info.
  auto rbs_info_table = html_print_helper.CreateTablePrinter(
      "Ongoing Remote Bootstraps",
      {"Tablet ID", "Namespace.Table", "Source TServer UUID", "Destination TServer UUID",
       "Progress"});
  for (auto& [dest_uuid, response] : responses) {
    for (auto& rbs_info : response.rbs_infos()) {
      auto tablet_info_result = cm->GetTabletInfo(rbs_info.tablet_id());
      std::string table_desc = "Not found";
      if (tablet_info_result.ok()) {
        table_desc = Format(
            "$0.$1", (*tablet_info_result)->table()->namespace_name(),
            (*tablet_info_result)->table()->name());
      }
      rbs_info_table.AddRow(
          rbs_info.tablet_id(), table_desc, rbs_info.source_ts_uuid(),
          dest_uuid, GetRemoteBootstrapProgressMessage(rbs_info));
    }
  }
  rbs_info_table.Print();
}
} // namespace

void MasterPathHandlers::HandleLoadBalancer(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  HtmlPrintHelper html_print_helper(*output);

  *output << "<h1>Cluster Balancer</h1>\n";

  *output << "<h2>Ongoing Remote Bootstraps</h2>\n";
  auto rbs_info = FetchRbsInfo(master_, 5s /* timeout */);
  RenderRbsInfo(master_->catalog_manager(), rbs_info, html_print_helper, output);

  auto activity_info = master_->catalog_manager()->cluster_balancer()->GetLatestActivityInfo();
  if (FLAGS_TEST_sleep_before_reporting_lb_ui_ms > 0) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_sleep_before_reporting_lb_ui_ms));
  }

  *output << "<h2>Last Run Summary</h2>\n";

  // TODO(asrivastava): Display LB bottlenecks here.
  *output << "<h3>Warnings Summary</h3>\n";
  *output << "Warnings are grouped by type (warnings for different tables / tablets are summed):\n";
  auto warnings_summary_table = html_print_helper.CreateTablePrinter(
      "Warnings Summary", {"Example Warning", "Count of similar messages"});
  for (const auto& warning : activity_info.GetWarningsSummary()) {
    warnings_summary_table.AddRow(warning.example_message, warning.count);
  }
  warnings_summary_table.Print();

  *output << "<h3>Tasks Summary</h3>\n";
  *output << "Tasks are grouped by type and state (tasks for different tablets are summed):\n";
  auto tasks_summary_table = html_print_helper.CreateTablePrinter(
      "Tasks Summary",
      {"Example description", "Task state", "Count", "Example status (if complete)"});
  for (const auto& [type_and_state, aggregated] : activity_info.GetTasksSummary()) {
    auto& [_, state] = type_and_state;
    tasks_summary_table.AddRow(
        aggregated.example_description, state, aggregated.count,
        MonitoredTask::IsStateTerminal(state) ? aggregated.example_status : "");
  }
  tasks_summary_table.Print();

  *output << "<h2>Tablet Distribution</h2>\n";
  auto descs = master_->ts_manager()->GetAllDescriptors();
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kAll);
  auto tserver_tree_result = CalculateTServerTree(-1 /* max_table_count */);
  if (!tserver_tree_result.ok()) {
    *output << "<div class='alert alert-warning'>"
            << "Cannot Calculate TServer Tree."
            << "</div>";
    return;
  }

  RenderLoadBalancerViewPanel(*tserver_tree_result, descs, tables, output);
}

void MasterPathHandlers::HandleStatefulServices(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream& output = resp->output;
  HtmlPrintHelper html_print_helper(output);

  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();
  auto stateful_service_result = master_->catalog_manager_impl()->GetStatefulServicesStatus();

  if (!stateful_service_result.ok()) {
    output << "<div class=\"alert alert-warning\">"
           << EscapeForHtmlToString(stateful_service_result.status().ToString()) << "</div>";
    return;
  }

  output << "<h1>Stateful Services</h1>\n";
  auto stateful_services = html_print_helper.CreateTablePrinter(
      "stateful_services", {"Service Name", "Hosting server", "Table Id", "Tablet ID"});

  for (const auto& service : *stateful_service_result) {
    auto reg = service.hosting_node->GetRegistration();
    const auto& cloud_info = reg.cloud_info();
    const auto& host_server = Format(
        "$0<br/>$1<br/>$2", RegistrationToHtml(reg), service.hosting_node->permanent_uuid(),
        EscapeForHtmlToString(Format(
            "$0.$1.$2", cloud_info.placement_cloud(), cloud_info.placement_region(),
            cloud_info.placement_zone())));

    stateful_services.AddRow(
        service.service_name, host_server,
        Format("<a href=\"/table?id=$0\">$0</a>", service.service_table_id),
        service.service_tablet_id);
  }
  stateful_services.Print();
}

void MasterPathHandlers::HandleStatefulServicesJson(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  JsonWriter jw(&resp->output, JsonWriter::COMPACT);

  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();
  jw.StartObject();
  auto stateful_service_result = master_->catalog_manager_impl()->GetStatefulServicesStatus();
  if (!stateful_service_result.ok()) {
    jw.String("error");
    jw.String(stateful_service_result.status().ToString());
  } else {
    jw.String("stateful_services");
    jw.StartArray();
    for (const auto& service : *stateful_service_result) {
      jw.StartObject();
      jw.String("service_name");
      jw.String(service.service_name);
      jw.String("hosting_server");
      jw.String(service.hosting_node->permanent_uuid());
      jw.String("table_id");
      jw.String(service.service_table_id);
      jw.String("tablet_id");
      jw.String(service.service_tablet_id);
      jw.EndObject();
    }
    jw.EndArray();
  }
  jw.EndObject();
}

void MasterPathHandlers::HandleObjectLocksPage(
    const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
  std::stringstream& output = resp->output;
  master_->catalog_manager()->AssertLeaderLockAcquiredForReading();
  auto ts_local_lock_manager = master_->catalog_manager_impl()
                                      ->object_lock_info_manager()
                                      ->ts_local_lock_manager();
  if (!ts_local_lock_manager) {
    output << "<h2>Could not locate the object lock manager...</h2>\n";
    return;
  }
  ts_local_lock_manager->DumpLocksToHtml(output);
}

Status MasterPathHandlers::Register(Webserver* server) {
  const bool is_styled = true;
  const bool is_on_nav_bar = true;

  // The set of handlers visible on the nav bar.
  RegisterPathHandler(
      server, "/", "Home", &MasterPathHandlers::RootHandler, is_styled, is_on_nav_bar,
      "fa fa-home");

  RegisterLeaderOrRedirectWithArgs(
      server, "/tablet-servers", "Tablet Servers", is_styled, is_on_nav_bar, "fa fa-server",
      &MasterPathHandlers::HandleTabletServers, TServersViewType::kTServersDefaultView);

  RegisterLeaderOrRedirectWithArgs(
      server, "/tablet-server-clocks", "Tablet Server Clocks", is_styled, /*is_on_nav_bar=*/false,
      /*icon=*/"", &MasterPathHandlers::HandleTabletServers, TServersViewType::kTServersClocksView);

  RegisterLeaderOrRedirectWithArgs(
      server, "/tables", "Tables", is_styled, is_on_nav_bar, "fa fa-table",
      &MasterPathHandlers::HandleAllTables, /*only_user_tables=*/false);

  RegisterLeaderOrRedirectWithArgs(
      server, "/namespaces", "Namespaces", is_styled, is_on_nav_bar, "fa fa-table",
      &MasterPathHandlers::HandleNamespacesHTML, /*only_user_namespaces=*/false);

  // The set of handlers not currently visible on the nav bar.
  RegisterLeaderOrRedirect(
      server, "/table", /*alias=*/"", &MasterPathHandlers::HandleTablePage, is_styled);

  RegisterPathHandler(server, "/masters", "Masters", &MasterPathHandlers::HandleMasters, is_styled);

  RegisterLeaderOrRedirect(
      server, "/cluster-config", "Cluster Config", &MasterPathHandlers::HandleGetClusterConfig,
      is_styled);

  RegisterLeaderOrRedirect(
      server, "/xcluster", "xCluster", &MasterPathHandlers::HandleXCluster, is_styled);

  RegisterLeaderOrRedirect(
      server, "/tasks", "Tasks", &MasterPathHandlers::HandleTasksPage, is_styled);

  RegisterLeaderOrRedirect(
      server, "/tablet-replication", "Tablet Replication Health",
      &MasterPathHandlers::HandleTabletReplicasPage, is_styled);

  RegisterLeaderOrRedirect(
      server, "/load-distribution", "Cluster Balancer View",
      &MasterPathHandlers::HandleLoadBalancer, is_styled);

  RegisterLeaderOrRedirect(
      server, "/stateful-services", "Stateful Services",
      &MasterPathHandlers::HandleStatefulServices, is_styled);

  RegisterLeaderOrRedirect(
      server, "/ObjectLockManager", "Object Lock Manager",
      &MasterPathHandlers::HandleObjectLocksPage, is_styled);

  // JSON Endpoints
  RegisterLeaderOrRedirect(
      server, "/api/v1/tablet-servers", "Tserver Statuses",
      &MasterPathHandlers::HandleGetTserverStatus);

  RegisterLeaderOrRedirect(
      server, "/api/v1/health-check", "Cluster Health Check",
      &MasterPathHandlers::HandleHealthCheck);

  RegisterLeaderOrRedirect(
      server, "/api/v1/tablet-replication", "Tablet Replication Health",
      &MasterPathHandlers::HandleGetReplicationStatus);

  RegisterLeaderOrRedirect(
      server, "/api/v1/tablet-under-replication", "Tablet UnderReplication Status",
      &MasterPathHandlers::HandleGetUnderReplicationStatus);

  RegisterLeaderOrRedirect(
      server, "/dump-entities", "Dump Entities", &MasterPathHandlers::HandleDumpEntities);

  RegisterLeaderOrRedirect(
      server, "/api/v1/namespaces", "Namespaces", &MasterPathHandlers::HandleNamespacesJSON);

  RegisterPathHandler(
      server, "/api/v1/is-leader", "Leader Check", &MasterPathHandlers::HandleCheckIfLeader);

  RegisterPathHandler(
      server, "/api/v1/masters", "Master Statuses", &MasterPathHandlers::HandleGetMastersStatus);

  RegisterPathHandler(
      server, "/api/v1/version", "YB Version Information",
      &MasterPathHandlers::HandleVersionInfoDump);

  RegisterLeaderOrRedirect(
      server, "/api/v1/table", "Table Info", &MasterPathHandlers::HandleTablePageJSON);

  RegisterLeaderOrRedirect(
      server, "/api/v1/tables", "Tables", &MasterPathHandlers::HandleAllTablesJSON);

  RegisterLeaderOrRedirect(
      server, "/api/v1/cluster-config", "Cluster Config JSON",
      &MasterPathHandlers::HandleGetClusterConfigJSON);

  RegisterLeaderOrRedirect(
      server, "/api/v1/xcluster", "xCluster", &MasterPathHandlers::HandleGetXClusterJSON);

  server->RegisterPathHandler(
      "/api/v1/meta-cache", "MetaCache",
      std::bind(&MasterPathHandlers::HandleGetMetaCacheJson, this, _1, _2), false, false);

  RegisterLeaderOrRedirect(
      server, "/api/v1/stateful-services", "Stateful Services",
      &MasterPathHandlers::HandleStatefulServicesJson);

  return Status::OK();
}

string MasterPathHandlers::ReplicaInfoToHtml(
    const std::vector<TsUuidAndTabletReplica>& locations, const std::string& tablet_id) const {
  stringstream html;

  html << "<ul>\n";
  for (const auto& [ts_uuid, replica] : locations) {
    auto ts_desc_ptr = replica.ts_desc.lock();
    string location_html = TSDescriptorToHtml(ts_uuid.get(), opt_cref(ts_desc_ptr), tablet_id);
    if (replica.role == PeerRole::LEADER) {
      auto leader_lease_info = replica.leader_lease_info;
      // The master might haven't received any heartbeats containing this leader yet.
      // Set the status to UNKNOWN.
      auto leader_lease_status = leader_lease_info.initialized
          ? LeaderLeaseStatus_Name(leader_lease_info.leader_lease_status)
          : "UNKNOWN";
      html << Format("  <li><b>LEADER: $0 ($1)</b></li>\n", location_html, leader_lease_status);
      if (leader_lease_info.leader_lease_status == consensus::LeaderLeaseStatus::HAS_LEASE) {
        auto ht_lease_exp = leader_lease_info.ht_lease_expiration;
        if (ht_lease_exp == consensus::kInfiniteHybridTimeLeaseExpiration) {
          html << "Remaining ht_lease: +INF(RF1)<br>\n";
        } else {
          // Get the remaining milliseconds of the current valid lease.
          const auto now_usec = boost::posix_time::microseconds(
              master_->clock()->Now().GetPhysicalValueMicros());
          auto ht_lease_usec = boost::posix_time::microseconds(ht_lease_exp);
          auto diff = ht_lease_usec - now_usec;
          html << Format("Remaining ht_lease (may be stale): $0 ms<br>\n",
                         diff.total_milliseconds());
        }
      } else if (leader_lease_info.heartbeats_without_leader_lease > 0) {
        html << Format(
            "Cannot replicate lease for past <b><font color='red'>$0</font></b> heartbeats<br>",
            leader_lease_info.heartbeats_without_leader_lease);
      }
    } else {
      html << Format("  <li>$0: $1</li>\n",
                         PeerRole_Name(replica.role), location_html);
    }
    html << Format("UUID: $0<br/>", ts_uuid);
    html << Format(
        "Active SSTs size: $0<br/>",
        HumanReadableNumBytes::ToString(replica.drive_info.sst_files_size));
    html << Format(
        "WALs size: $0\n", HumanReadableNumBytes::ToString(replica.drive_info.wal_files_size));
  }
  html << "</ul>\n";
  return html.str();
}

Status MasterPathHandlers::CalculateTabletMap(TabletCountMap* tablet_map) {
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);
  for (const auto& table : tables) {
    if (table->IsSecondaryTable()) {
      // will be taken care of by colocated parent table
      continue;
    }

    TabletInfos tablets = VERIFY_RESULT(table->GetTabletsIncludeInactive());
    bool is_user_table = table->IsUserCreated();
    for (const auto& tablet : tablets) {
      if (tablet->LockForRead()->is_deleted()) {
        continue;
      }
      auto replication_locations = tablet->GetReplicaLocations();
      for (const auto& replica : *replication_locations) {
        auto& counts = (*tablet_map)[replica.first];
        if (tablet->LockForRead()->is_hidden()) {
          counts.hidden_tablet_peers++;
        }
        if (is_user_table || table->IsColocationParentTable()) {
          if (replica.second.role == PeerRole::LEADER) {
            counts.user_tablet_leaders++;
          } else {
            counts.user_tablet_followers++;
          }
        } else {
          if (replica.second.role == PeerRole::LEADER) {
            counts.system_tablet_leaders++;
          } else {
            counts.system_tablet_followers++;
          }
        }
      }
    }
  }
  return Status::OK();
}

Result<MasterPathHandlers::TServerTree> MasterPathHandlers::CalculateTServerTree(
    int max_table_count) {
  TServerTree tserver_tree;
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kRunning);

  if (max_table_count != -1) {
    int count = 0;
    for (const auto& table : tables) {
      if (!table->IsUserCreated() || table->IsSecondaryTable()) {
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
    if (!table->IsUserCreated() || table->IsSecondaryTable()) {
      // only display user created tables that are not colocated.
      continue;
    }

    TabletInfos tablets = VERIFY_RESULT(table->GetTabletsIncludeInactive());

    for (const auto& tablet : tablets) {
      if (tablet->LockForRead()->is_deleted()) {
        continue;
      }
      auto replica_locations = tablet->GetReplicaLocations();
      for (const auto& replica : *replica_locations) {
        tserver_tree[replica.first][tablet->table()->id()].emplace_back(
          replica.second.role,
          tablet->tablet_id()
        );
      }
    }
  }

  return tserver_tree;
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
    auto reg = desc->GetRegistration();
    *output << Format("<th>$0<br>$1</th>", RegistrationToHtml(reg), desc->permanent_uuid());
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
    auto keyspace = master_->catalog_manager()->GetNamespaceName(table->namespace_id());
    auto table_cat = GetTableType(*table);
    auto table_locked = table->LockForRead();
    if (!table_locked->is_running()) {
      continue;
    }

    // Skip non-user tables if we should.
    if (table_cat != kUserIndex && table_cat != kUserTable) {
      continue;
    }
    const string& table_name = table_locked->name();
    const string& table_id = table->id();

    std::unordered_set<TabletId> tablet_ids;
    for (const auto& [_, table_tree] : tserver_tree) {
      for (const auto& [_, replicas] : table_tree) {
        for (const auto& replica : replicas) {
          tablet_ids.insert(replica.tablet_id);
        }
      }
    }
    auto tablet_count = tablet_ids.size();

    *output << Format(
        "<tr>"
        "<td>$0</td>"
        "<td><a href=\"/table?id=$1\">$2</a></td>"
        "<td>$3</td>",
        EscapeForHtmlToString(keyspace), UrlEncodeToString(table_id),
        EscapeForHtmlToString(table_name), tablet_count);
    for (auto& desc : descs) {
      uint64 num_replicas = 0;
      uint64 num_leaders = 0;
      const auto* table_tree = FindOrNull(tserver_tree, desc->permanent_uuid());
      const auto* replicas = table_tree != nullptr ? FindOrNull(*table_tree, table_id) : nullptr;
      if (replicas != nullptr) {
        num_replicas = replicas->size();
        num_leaders = std::count_if(
            replicas->begin(), replicas->end(),
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
  } else if (table.IsUserIndex()) {
    table_cat = kUserIndex;
  } else if (table.IsUserTable()) {
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
