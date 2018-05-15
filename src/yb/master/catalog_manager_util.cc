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

#include "yb/master/catalog_manager_util.h"

#include <gflags/gflags.h>

#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/math_util.h"

DEFINE_double(balancer_load_max_standard_deviation, 2.0,
              "The standard deviation among the tserver load, above which that distribution "
              "is considered not balanced.");
TAG_FLAG(balancer_load_max_standard_deviation, advanced);

namespace yb {
namespace master {

using strings::Substitute;

Status CatalogManagerUtil::IsLoadBalanced(const master::TSDescriptorVector& ts_descs) {
  ZoneToDescMap zone_to_ts;
  RETURN_NOT_OK(GetPerZoneTSDesc(ts_descs, &zone_to_ts));

  for (const auto& zone : zone_to_ts) {
    if (zone.second.size() <= 1) {
      continue;
    }
    vector<double> load;
    for (const auto &ts_desc : zone.second) {
      load.push_back(ts_desc->num_live_replicas());
    }

    double std_dev = yb::standard_deviation(load);
    LOG(INFO) << "Load standard deviation is " << std_dev << " for "
              << zone.second.size() << " tservers in placement " << zone.first;

    if (std_dev >= FLAGS_balancer_load_max_standard_deviation) {
      return STATUS(IllegalState, Substitute("Load not balanced: deviation=$0 in $1.",
                                             std_dev, zone.first));
    }
  }
  return Status::OK();
}

Status CatalogManagerUtil::AreLeadersOnPreferredOnly(const TSDescriptorVector& ts_descs,
                                                     const ReplicationInfoPB& replication_info) {
  for (const auto& ts_desc : ts_descs) {
    if (!ts_desc->IsAcceptingLeaderLoad(replication_info) && ts_desc->leader_count() > 0) {
      // This is a ts that shouldn't have leader load but does, return an error.
      return STATUS(
          IllegalState,
          Substitute("Expected no leader load on tserver $0, found $1.",
                     ts_desc->permanent_uuid(), ts_desc->leader_count()));
    }
  }
  return Status::OK();
}

Status CatalogManagerUtil::GetPerZoneTSDesc(const TSDescriptorVector& ts_descs,
                                            ZoneToDescMap* zone_to_ts) {
  if (zone_to_ts == nullptr) {
    return STATUS(InvalidArgument, "Need a non-null zone to tsdesc map that will be filled in.");
  }
  zone_to_ts->clear();
  for (const auto& ts_desc : ts_descs) {
    string placement_id = ts_desc->placement_id();
    auto iter = zone_to_ts->find(placement_id);
    if (iter == zone_to_ts->end()) {
      (*zone_to_ts)[placement_id] = {ts_desc};
    } else {
      iter->second.push_back(ts_desc);
    }
  }
  return Status::OK();
}

} // namespace master
} // namespace yb
