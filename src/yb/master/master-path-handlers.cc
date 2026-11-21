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

#include <iomanip>
#include <optional>
#include <sstream>

#include "yb/common/common_net.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_cluster.pb.h"

#include "yb/util/curl_util.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/url-coding.h"

DEFINE_RUNTIME_bool(master_webserver_require_https, false,
    "Require HTTPS when redirecting master UI requests to the leader.");

DECLARE_string(webserver_ca_certificate_file);
DECLARE_string(webserver_certificate_file);

namespace yb::master {

using std::string;

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

Result<std::string> GetLeaderAddress(
    const Result<std::vector<ServerEntryPB>>& masters,
    const Result<ServerRegistrationPB>& local_reg, const Webserver::WebRequest& req) {
  if (!masters.ok()) {
    return masters.status().CloneAndPrepend("Unable to list masters during web request handling");
  }
  if (!local_reg.ok()) {
    return local_reg.status().CloneAndPrepend(
        "Unable to get local registration during web request handling");
  }
  const auto leader = std::find_if(masters->begin(), masters->end(), [](const auto& master) {
    return !master.has_error() && master.role() == PeerRole::LEADER;
  });
  if (leader == masters->end() || leader->registration().http_addresses().empty()) {
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
          local_reg->cloud_info())),
      req.redirect_uri,
      req.query_string.empty() ? "?raw" : "?" + req.query_string + "&raw");
}

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

}  // namespace

void RedirectToLeader(
    const Result<std::vector<ServerEntryPB>>& masters,
    const Result<ServerRegistrationPB>& local_reg, const Webserver::WebRequest& req,
    Webserver::WebResponse* resp) {
  std::stringstream* output = &resp->output;
  auto redirect_result = GetLeaderAddress(masters, local_reg, req);
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

void RenderMastersPage(
    const Result<std::vector<ServerEntryPB>>& masters_result, const std::string& local_uuid,
    std::stringstream* output) {
  if (!masters_result.ok()) {
    auto s = masters_result.status().CloneAndPrepend("Unable to list Masters");
    LOG(WARNING) << s.ToString();
    *output << "<h1>" << EscapeForHtmlToString(s.ToString()) << "</h1>\n";
    return;
  }
  const auto& masters = *masters_result;
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
    if (master.instance_id().permanent_uuid() == local_uuid) {
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

void RenderMastersJson(
    const Result<std::vector<ServerEntryPB>>& masters, std::stringstream* output) {
  ListMastersResponsePB pb_resp;
  JsonWriter jw(output, JsonWriter::COMPACT);
  if (!masters.ok()) {
    jw.Protobuf(pb_resp);
    return;
  }
  for (const ServerEntryPB& master : *masters) {
    pb_resp.add_masters()->CopyFrom(master);
  }
  jw.Protobuf(pb_resp);
}

}  // namespace yb::master
