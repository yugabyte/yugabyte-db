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
#include "yb/util/wait_state.h"
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

struct YBActiveUniverseHistoryInfo {
  util::AUHMetadata metadata;
  uint64_t wait_status_code;
  util::AUHAuxInfo aux_info;
  std::string wait_status_code_as_string;

  template <class PB>
  static YBActiveUniverseHistoryInfo FromPB(const PB& pb) {
    return YBActiveUniverseHistoryInfo {
      .metadata = util::AUHMetadata::FromPB(pb.metadata()),
      .wait_status_code = pb.wait_status_code(),
      .aux_info = util::AUHAuxInfo::FromPB(pb.aux_info()),
      .wait_status_code_as_string = pb.wait_status_code_as_string(),
    };
  }
};

struct YCQLStatStatementInfo {
  uint64_t queryid;
  std::string query;
  bool is_prepared;
  int64_t calls;
  double total_time;
  double min_time;
  double max_time;
  double mean_time;
  double stddev_time;
  template <class PB>
  static YCQLStatStatementInfo FromPB(const PB& pb) {
    return YCQLStatStatementInfo {
        .queryid = pb.queryid(),
        .query = pb.query(),
        .is_prepared = pb.is_prepared(),
        .calls = pb.calls(),
        .total_time = pb.total_time(),
        .min_time = pb.min_time(),
        .max_time = pb.max_time(),
        .mean_time = pb.mean_time(),
        .stddev_time = pb.stddev_time(),
    };
  }

  template <class PB>
  void ToPB(PB* pb) const {
    pb->set_queryid(queryid);
    pb->set_query(query);
    pb->set_is_prepared(is_prepared);
    pb->set_calls(calls);
    pb->set_total_time(total_time);
    pb->set_min_time(min_time);
    pb->set_max_time(max_time);
    pb->set_mean_time(mean_time);
    pb->set_stddev_time(stddev_time);
  }
};

} // namespace client
} // namespace yb
