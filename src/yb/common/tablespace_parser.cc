//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/common/tablespace_parser.h"

#include <boost/algorithm/string/predicate.hpp>

#include <rapidjson/document.h>

#include "yb/gutil/strings/split.h"
#include "yb/util/flags.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"

using std::string;
using std::vector;

DEFINE_RUNTIME_bool(enable_tablespace_validation, true, "Whether to enable extended validation for "
    "tablespaces. This can be set to false to allow ysql_dump to succeed when creating tablespaces "
    "that were created before the checks.");

namespace yb {

const std::string TablespaceParser::kWildcardPlacement = "*";

// TODO(#26671): When we start using this for the yb-admin APIs as well, we should take into
// account prefix placements (wildcards).
// TODO(#12180): Support read replica validation.
Status ValidateReplicationInfo(const ReplicationInfoPB& replication_info) {
  // Only live replicas are validated right now.
  const auto& live_replicas = replication_info.live_replicas();
  if (live_replicas.num_replicas() <= 0) {
    return STATUS_FORMAT(Corruption, "Invalid num_replicas: $0", live_replicas.num_replicas());
  }

  const auto& placements = live_replicas.placement_blocks();

  auto total_min_replicas = 0;
  for (auto i = placements.begin(); i != placements.end(); ++i) {
    if (i->min_num_replicas() <= 0) {
      return STATUS_FORMAT(Corruption, "min_num_replicas ($0) must be > 0", i->min_num_replicas());
    }
    total_min_replicas += i->min_num_replicas();
    // Check for duplicate placement blocks.
    for (auto j = placements.begin(); j != i; ++j) {
      if (pb_util::ArePBsEqual(i->cloud_info(), j->cloud_info(), nullptr /* diff_str */)) {
        return STATUS_FORMAT(
            Corruption, "Duplicate placement block found: $0", i->cloud_info().ShortDebugString());
      }
    }
  }

  if (total_min_replicas > live_replicas.num_replicas()) {
    return STATUS_FORMAT(
        Corruption,
        "Sum of min_num_replicas fields ($0) exceeds the total replication factor "
        "in the placement policy ($1)", total_min_replicas, live_replicas.num_replicas());
  }

  // Affinitized leader zones should be contiguous.
  for (const auto& affinitized_leader : replication_info.multi_affinitized_leaders()) {
    if (affinitized_leader.zones_size() == 0) {
      return STATUS_FORMAT(
          Corruption, "Empty affinitized leader zone set found in replication info "
          "(leader preferences must be contiguous from 1..N): $0",
          replication_info.ShortDebugString());
    }
  }

  return Status::OK();
}

Result<ReplicationInfoPB> TablespaceParser::FromJson(
    const string& placement_str, const rapidjson::Document& placement,
    bool fail_on_validation_error) {

  ReplicationInfoPB replication_info;
  PlacementInfoPB* live_placement_info = replication_info.mutable_live_replicas();

  if (!placement.IsObject()) {
    return STATUS_FORMAT(Corruption, "Invalid JSON object: $0. Expected object.", placement_str);
  }

  // Parse the number of replicas.
  if (!placement.HasMember("num_replicas") || !placement["num_replicas"].IsInt()) {
    return STATUS_FORMAT(Corruption,
                         "Invalid or missing \"num_replicas\" field in the placement "
                         "policy: $0", placement_str);
  }
  live_placement_info->set_num_replicas(placement["num_replicas"].GetInt());

  // Parse the placement blocks.
  // It is possible to have no placement blocks -
  // it just means we have no placement constraints on the total num replicas.
  if (placement.HasMember("placement_blocks") && !placement["placement_blocks"].IsArray()) {
    return STATUS_FORMAT(Corruption,
                         "\"placement_blocks\" in the placement policy should be an array: $0",
                         placement_str);
  }

  if (placement.HasMember("placement_blocks")) {
    const rapidjson::Value& pb_arr = placement["placement_blocks"];
    for (rapidjson::SizeType i = 0; i < pb_arr.Size(); ++i) {
      const rapidjson::Value& placement = pb_arr[i];
      if (!placement.HasMember("cloud") || !placement.HasMember("region") ||
          !placement.HasMember("zone") || !placement.HasMember("min_num_replicas")) {
        return STATUS_FORMAT(
            Corruption,
            "Missing required key (cloud/region/zone/min_num_replicas) in placement "
            "block. Placement policy: $0. \n"
            "To indicate that any region/zone is acceptable, use "
            "the wildcard character: '$1'", placement_str, kWildcardPlacement);
      }
      if (!placement["cloud"].IsString() || strlen(placement["cloud"].GetString()) == 0 ||
            !placement["region"].IsString() || strlen(placement["region"].GetString()) == 0 ||
          !placement["zone"].IsString() || strlen(placement["zone"].GetString()) == 0 ||
          !placement["min_num_replicas"].IsInt()) {
        return STATUS_FORMAT(
            Corruption, "Invalid type/value for some key in placement block. Placement policy: $0",
            placement_str);
      }

      auto* placement_block = live_placement_info->add_placement_blocks();
      placement_block->set_min_num_replicas(placement["min_num_replicas"].GetInt());

      auto* cloud_info = placement_block->mutable_cloud_info();
      // The special value '*' is allowed for a placement to match any region/zone.
      // It is not allowed to use it for cloud, though because allowing any cloud, region
      // and zone is not really a placement constraint.
      if (std::string(placement["cloud"].GetString()) != kWildcardPlacement) {
        cloud_info->set_placement_cloud(placement["cloud"].GetString());
      } else {
        return STATUS_FORMAT(
          Corruption, "Cannot use wildcard placement at cloud level. Placement policy: $0",
          placement_str);
      }

      bool in_wildcard = false;
      if (std::string(placement["region"].GetString()) != kWildcardPlacement) {
        cloud_info->set_placement_region(placement["region"].GetString());
      } else {
        in_wildcard = true;
      }

      if (std::string(placement["zone"].GetString()) != kWildcardPlacement) {
        if (in_wildcard) {
          return STATUS_FORMAT(Corruption,
          "A wildcard '*' at region level should be followed by a wildcard at zone level");
        }
        cloud_info->set_placement_zone(placement["zone"].GetString());
      }
      // Add zones until we have at least leader_preference zones.
      if (placement.HasMember("leader_preference")) {
        if (!placement["leader_preference"].IsInt() ||
            placement["leader_preference"].GetInt() <= 0) {
          return STATUS_FORMAT(
              Corruption, "Invalid type/value for leader_preference option (must be >0): $0",
              placement_str);
        }

        const int priority = placement["leader_preference"].GetInt();
        while (replication_info.multi_affinitized_leaders_size() < priority) {
          replication_info.add_multi_affinitized_leaders();
        }
        auto* zone_set = replication_info.mutable_multi_affinitized_leaders(priority - 1);
        zone_set->add_zones()->CopyFrom(*cloud_info);
      }
    }
  }

  auto status = ValidateReplicationInfo(replication_info);
  if (fail_on_validation_error && FLAGS_enable_tablespace_validation) {
    RETURN_NOT_OK(status);
  } else {
    if (!status.ok()) {
      LOG(ERROR) << "Replication info validation failed: " << status;
    }
  }
  return replication_info;
}

Result<ReplicationInfoPB> TablespaceParser::FromString(
    const std::string& placement, bool fail_on_validation_error) {
  // Example value of placement:
  //   '{"num_replicas":3,
  //     "placement_blocks": [
  //         {"cloud":"c1", "region":"r1", "zone":"z1", "min_num_replicas":1,
  //            "leader_preference":1},
  //         {"cloud":"c2", "region":"r2", "zone":"z2", "min_num_replicas":1},
  //         {"cloud":"c3", "region":"r3", "zone":"z3", "min_num_replicas":1}]}'

  rapidjson::Document document;
  if (document.Parse(placement.c_str()).HasParseError()) {
    return STATUS_FORMAT(
        Corruption, "Json parsing of replica_placement option failed: $0", placement);
  }
  return FromJson(placement, document, fail_on_validation_error);
}

Result<ReplicationInfoPB> TablespaceParser::FromQLValue(
    const std::vector<std::string>& placements, bool fail_on_validation_error) {
  // Today, only one option is supported (replica_placement), so this array should have only one
  // option.
  if (placements.size() != 1) {
    return STATUS_FORMAT(Corruption,
                         "Unexpected number of options: $0", placements.size());
  }

  auto& placement = placements[0];
  if (!placement.starts_with("replica_placement")) {
    return STATUS(InvalidArgument, "Invalid option found in spcoptions: $0", placement);
  }

  vector<string> split;
  split = strings::Split(placement, "replica_placement=", strings::SkipEmpty());
  if (split.size() != 1) {
    return STATUS_FORMAT(Corruption, "replica_placement option ill-formed: $0", placement);
  }
  return FromString(split[0]);
}

LocalityLevel TablespaceParser::GetLocalityLevel(
    const CloudInfoPB& src_cloud_info, const CloudInfoPB& dest_cloud_info) {
  if (!src_cloud_info.has_placement_region() || !dest_cloud_info.has_placement_region() ||
      src_cloud_info.placement_region() != dest_cloud_info.placement_region()) {
    return LocalityLevel::kNone;
  }
  if (!src_cloud_info.has_placement_zone() || !dest_cloud_info.has_placement_zone() ||
      src_cloud_info.placement_zone() != dest_cloud_info.placement_zone()) {
    return LocalityLevel::kRegion;
  }
  return LocalityLevel::kZone;
}

} // namespace yb
