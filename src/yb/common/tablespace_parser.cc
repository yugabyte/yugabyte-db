//--------------------------------------------------------------------------------------------------
// Copyright (c) Yugabyte, Inc.
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
#include "yb/util/status.h"

using std::string;
using std::vector;

namespace yb {

Result<ReplicationInfoPB> TablespaceParser::FromJson(
    const string& placement_str, const rapidjson::Document& placement) {
  ReplicationInfoPB replication_info;
  PlacementInfoPB* live_placement_info = replication_info.mutable_live_replicas();
  std::set<int> visited_priorities;

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
  if (!placement.HasMember("placement_blocks") || !placement["placement_blocks"].IsArray()) {
    return STATUS_FORMAT(Corruption,
                         "\"placement_blocks\" array not found in the placement policy: $0",
                         placement_str);
  }
  const rapidjson::Value& pb = placement["placement_blocks"];
  if (pb.Size() < 1) {
    return STATUS_FORMAT(Corruption,
                         "\"placement_blocks\" field has empty value in the placement "
                         "policy: $0", placement_str);
  }

  int64 total_min_replicas = 0;
  for (rapidjson::SizeType i = 0; i < pb.Size(); ++i) {
    const rapidjson::Value& placement = pb[i];
    const std::unordered_set<string> allowed_keys =
        {"cloud", "region", "zone", "min_num_replicas", "leader_preference"};
    for (auto& item : placement.GetObject()) {
      if (!allowed_keys.contains(item.name.GetString())) {
        return STATUS_FORMAT(
            Corruption, "Unexpected key ($0) in placement block. Placement policy: $1",
            item.name.GetString(), placement_str);
      }
    }
    if (!placement.HasMember("cloud") || !placement.HasMember("region") ||
        !placement.HasMember("zone") || !placement.HasMember("min_num_replicas")) {
      return STATUS_FORMAT(
          Corruption, "Missing required key (cloud/region/zone/min_num_replicas) in placement "
          "block. Placement policy: $0", placement_str);
    }
    if (!placement["cloud"].IsString() || !placement["region"].IsString() ||
        !placement["zone"].IsString() || !placement["min_num_replicas"].IsInt() ||
        placement["min_num_replicas"].GetInt() <= 0) {
      return STATUS_FORMAT(
          Corruption, "Invalid type/value for some key in placement block. Placement policy: $0",
          placement_str);
    }

    auto* placement_block = live_placement_info->add_placement_blocks();
    placement_block->set_min_num_replicas(placement["min_num_replicas"].GetInt());
    total_min_replicas += placement["min_num_replicas"].GetInt();

    auto* cloud_info = placement_block->mutable_cloud_info();
    cloud_info->set_placement_cloud(placement["cloud"].GetString());
    cloud_info->set_placement_region(placement["region"].GetString());
    cloud_info->set_placement_zone(placement["zone"].GetString());

    // Add zones until we have at least leader_preference zones.
    if (placement.HasMember("leader_preference")) {
      if (!placement["leader_preference"].IsInt() || placement["leader_preference"].GetInt() <= 0) {
        return STATUS_FORMAT(
            Corruption, "Invalid type/value for leader_preference option (must be >0): $0",
            placement_str);
      }

      const int priority = placement["leader_preference"].GetInt();
      visited_priorities.insert(priority);

      while (replication_info.multi_affinitized_leaders_size() < priority) {
        replication_info.add_multi_affinitized_leaders();
      }
      auto* zone_set = replication_info.mutable_multi_affinitized_leaders(priority - 1);
      zone_set->add_zones()->CopyFrom(*cloud_info);
    }
  }

  if (total_min_replicas > live_placement_info->num_replicas()) {
    return STATUS_FORMAT(
        Corruption,
        "Sum of min_num_replicas fields exceeds the total replication factor "
        "in the placement policy: $0",
        placement_str);
  }

  int size = static_cast<int>(visited_priorities.size());
  if (size > 0 && (*(visited_priorities.rbegin()) != size)) {
    return STATUS_FORMAT(
        Corruption, "Values for leader_preference is non-contiguous in option: $0", placement_str);
  }

  return replication_info;
}

Result<ReplicationInfoPB> TablespaceParser::FromString(const std::string& placement) {
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
  return FromJson(placement, document);
}

Result<ReplicationInfoPB> TablespaceParser::FromQLValue(
    const std::vector<std::string>& placements) {
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
