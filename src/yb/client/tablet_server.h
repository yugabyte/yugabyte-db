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

#include <string>
#pragma once

namespace yb {
namespace client {

// In-memory representation of a remote tablet server.
struct YBTabletServer {
  std::string uuid;
  std::string hostname;
  std::string placement_uuid;

  template <class PB, class CloudInfo>
  static YBTabletServer FromPB(const PB& pb, const CloudInfo& cloud_info) {
    return YBTabletServer {
      .uuid = pb.instance_id().permanent_uuid(),
      .hostname = DesiredHostPort(pb.registration().common(), cloud_info).host(),
      .placement_uuid = pb.registration().common().placement_uuid(),
    };
  }

  template <class PB>
  static YBTabletServer FromPB(const PB& pb) {
    return YBTabletServer {
      .uuid = pb.uuid(),
      .hostname = pb.hostname(),
      .placement_uuid = pb.placement_uuid(),
    };
  }

  template <class PB>
  void ToPB(PB* pb) const {
    pb->set_uuid(uuid);
    pb->set_hostname(hostname);
    pb->set_placement_uuid(placement_uuid);
  }
};

struct YBTabletServerPlacementInfo {
  YBTabletServer server;
  std::string cloud;
  std::string region;
  std::string zone;
  bool is_primary;
  std::string public_ip;
  uint16_t pg_port;

  template <class PB>
  static YBTabletServerPlacementInfo FromPB(const PB& pb) {
    return YBTabletServerPlacementInfo {
      .server = YBTabletServer::FromPB(pb),
      .cloud = pb.cloud(),
      .region = pb.region(),
      .zone = pb.zone(),
      .is_primary = pb.is_primary(),
      .public_ip = pb.public_ip(),
      .pg_port = static_cast<uint16_t>(pb.pg_port()),
    };
  }

  template <class PB>
  void ToPB(PB* pb) const {
    server.ToPB(pb);
    pb->set_cloud(cloud);
    pb->set_region(region);
    pb->set_zone(zone);
    pb->set_is_primary(is_primary);
    pb->set_public_ip(public_ip);
    pb->set_pg_port(pg_port);
  }
};

struct TabletServersInfo {
  std::vector<YBTabletServerPlacementInfo> tablet_servers;
  std::string universe_uuid;

  template <class PB>
  static TabletServersInfo FromPB(const PB& pb) {
    TabletServersInfo info;
    info.universe_uuid = pb.universe_uuid();
    info.tablet_servers.reserve(pb.servers().size());
    for (const auto& server : pb.servers()) {
      info.tablet_servers.push_back(YBTabletServerPlacementInfo::FromPB(server));
    }
    return info;
  }
};

} // namespace client
} // namespace yb
