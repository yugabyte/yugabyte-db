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

#include "yb/common/placement_info.h"

#include <boost/algorithm/string/predicate.hpp>

#include <rapidjson/document.h>

#include "yb/gutil/strings/split.h"
#include "yb/util/status.h"

using std::string;
using std::vector;

namespace yb {

namespace {

bool IsReplicaPlacementOption(const string& option) {
  return boost::starts_with(option, "replica_placement");
}

} // namespace

Result<PlacementInfoConverter::Placement> PlacementInfoConverter::FromJson(
    const string& placement_str, const rapidjson::Document& placement) {
  std::vector<PlacementInfo> placement_infos;
  std::set<int> visited_priorities;

  // Parse the number of replicas
  if (!placement.HasMember("num_replicas") || !placement["num_replicas"].IsInt()) {
    return STATUS_FORMAT(Corruption,
                         "Invalid value found for \"num_replicas\" field in the placement "
                         "policy: $0", placement_str);
  }
  const int num_replicas = placement["num_replicas"].GetInt();

  // Parse the placement blocks.
  if (!placement.HasMember("placement_blocks") ||
      !placement["placement_blocks"].IsArray()) {
    return STATUS_FORMAT(Corruption,
                         "\"placement_blocks\" field not found in the placement policy: $0",
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
    if (!placement.HasMember("cloud") || !placement.HasMember("region") ||
        !placement.HasMember("zone") || !placement.HasMember("min_num_replicas")) {
      return STATUS_FORMAT(Corruption,
                           "Missing keys in replica placement option: $0", placement_str);
    }
    if (!placement["cloud"].IsString() || !placement["region"].IsString() ||
        !placement["zone"].IsString() || !placement["min_num_replicas"].IsInt() ||
        placement["min_num_replicas"].GetInt() <= 0) {
      return STATUS_FORMAT(Corruption,
                           "Invalid value for replica_placement option: $0", placement_str);
    }

    const int min_rf = placement["min_num_replicas"].GetInt();
    PlacementInfo info{
        .cloud = placement["cloud"].GetString(),
        .region = placement["region"].GetString(),
        .zone = placement["zone"].GetString(),
        .min_num_replicas = min_rf,
    };

    if (placement.HasMember("leader_preference")) {
      if (!placement["leader_preference"].IsInt() || placement["leader_preference"].GetInt() <= 0) {
        return STATUS_FORMAT(
            Corruption, "Invalid value for leader_preference option: $0", placement_str);
      }

      const int priority = placement["leader_preference"].GetInt();
      visited_priorities.insert(priority);
      info.leader_preference = priority;
    }

    placement_infos.emplace_back(std::move(info));
    total_min_replicas += min_rf;
  }

  if (total_min_replicas > num_replicas) {
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

  return PlacementInfoConverter::Placement{
      .placement_infos = std::move(placement_infos),
      .num_replicas = num_replicas,
  };
}

// TODO(#10869): improve JSON parsing
Result<PlacementInfoConverter::Placement> PlacementInfoConverter::FromString(
    const std::string& placement) {
  rapidjson::Document document;

  // The only tablespace option supported today is "replica_placement" that allows specification
  // of placement policies encoded as a JSON array. Example value:
  // replica_placement=
  //   '{"num_replicas":3,
  //     "placement_blocks": [
  //         {"cloud":"c1", "region":"r1", "zone":"z1", "min_num_replicas":1,
  //            "leader_preference":1},
  //         {"cloud":"c2", "region":"r2", "zone":"z2", "min_num_replicas":1},
  //         {"cloud":"c3", "region":"r3", "zone":"z3", "min_num_replicas":1}]}'
  if (!IsReplicaPlacementOption(placement)) {
    return STATUS(InvalidArgument, "Invalid option found in spcoptions: $0", placement);
  }

  // First split the string and get only the json value in a string.
  vector<string> split;
  split = strings::Split(placement, "replica_placement=", strings::SkipEmpty());
  if (split.size() != 1) {
    return STATUS_FORMAT(Corruption, "replica_placement option illformed: $0", placement);
  }
  auto replica_placement = split[0].c_str();

  if (document.Parse(replica_placement).HasParseError() || !document.IsObject()) {
    return STATUS_FORMAT(Corruption,
                         "Json parsing of replica placement option failed: $0", split[0]);
  }

  return FromJson(replica_placement, document);
}

Result<PlacementInfoConverter::Placement> PlacementInfoConverter::FromQLValue(
    const std::vector<std::string>& placement) {
  // Today only one option is supported, so this array should have only one option.
  if (placement.size() != 1) {
    return STATUS_FORMAT(Corruption,
                         "Unexpected number of options: $0", placement.size());
  }

  return FromString(placement.front());
}

LocalityLevel PlacementInfoConverter::GetLocalityLevel(
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
