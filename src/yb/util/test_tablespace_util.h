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

#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "yb/common/common_net.pb.h"

namespace yb::test {

// Helper used by tests to construct tablespace placement JSON and CREATE TABLESPACE commands.
class PlacementBlock {
 public:
  PlacementBlock(
      std::string cloud, std::string region, std::string zone, size_t min_num_replicas,
      std::optional<size_t> leader_preference = std::nullopt)
      : cloud_(std::move(cloud)),
        region_(std::move(region)),
        zone_(std::move(zone)),
        min_num_replicas_(min_num_replicas),
        leader_preference_(leader_preference) {}

  // Creates a placement block with the given region ID. For the purposes of these tests, the cloud
  // is always "cloud0" and the zone is always "zone". The region is "region<regionId>".
  PlacementBlock(
      size_t region_id, size_t min_num_replicas,
      std::optional<size_t> leader_preference = std::nullopt)
      : cloud_("cloud0"),
        region_("region" + std::to_string(region_id)),
        zone_("zone"),
        min_num_replicas_(min_num_replicas),
        leader_preference_(leader_preference) {}

  const std::string& cloud() const { return cloud_; }
  const std::string& region() const { return region_; }
  const std::string& zone() const { return zone_; }
  size_t min_num_replicas() const { return min_num_replicas_; }
  const std::optional<size_t>& leader_preference() const { return leader_preference_; }

  std::string ToJson() const {
    std::ostringstream os;
    os << "{\"cloud\":\"" << cloud_ << "\",\"region\":\"" << region_ << "\",\"zone\":\"" << zone_
       << "\",\"min_num_replicas\":" << min_num_replicas_;
    if (leader_preference_) {
      os << ",\"leader_preference\":" << *leader_preference_;
    }
    os << "}";
    return os.str();
  }

  // Checks if the cloud, region, and zone match those of a CloudInfo protobuf.
  // Wildcards ("*") match any value.
  bool MatchesReplica(const CloudInfoPB& replica_cloud_info) const {
    return (replica_cloud_info.placement_cloud() == cloud_ || cloud_ == "*") &&
           (replica_cloud_info.placement_region() == region_ || region_ == "*") &&
           (replica_cloud_info.placement_zone() == zone_ || zone_ == "*");
  }

 private:
  std::string cloud_;
  std::string region_;
  std::string zone_;
  size_t min_num_replicas_;
  std::optional<size_t> leader_preference_;
};

std::string BuildPlacementString(const std::vector<PlacementBlock>& placement_blocks) {
  std::ostringstream ss;
  for (size_t i = 0; i < placement_blocks.size(); ++i) {
    const auto& block = placement_blocks[i];
    ss << block.cloud() << "." << block.region() << "." << block.zone() << ":"
       << block.min_num_replicas();
    if (i + 1 < placement_blocks.size()) {
      ss << ",";
    }
  }
  return ss.str();
}

class Tablespace {
 public:
  std::string name;
  int32_t num_replicas;
  std::vector<PlacementBlock> placement_blocks;
  std::vector<PlacementBlock> read_replica_placement_blocks;

  Tablespace(
      std::string name, int32_t num_replicas, std::vector<PlacementBlock> placement_blocks,
      std::vector<PlacementBlock> read_replica_placement_blocks = {},
      std::optional<int32_t> read_replica_num_replicas = std::nullopt)
      : name(std::move(name)),
        num_replicas(num_replicas),
        placement_blocks(std::move(placement_blocks)),
        read_replica_placement_blocks(std::move(read_replica_placement_blocks)),
        read_replica_num_replicas_(read_replica_num_replicas) {
    live_json_ = GeneratePlacementJson(this->placement_blocks, this->num_replicas);
    if (!this->read_replica_placement_blocks.empty()) {
      const int32_t read_replica_num_replicas_value = read_replica_num_replicas_.value_or(
          TotalMinReplicas(this->read_replica_placement_blocks));
      read_replica_json_ = GeneratePlacementJson(
          this->read_replica_placement_blocks, read_replica_num_replicas_value, "read_replica");
      read_replica_num_replicas_ = read_replica_num_replicas_value;
    }
    create_cmd_ = GenerateCreateCmd();
  }

  int32_t NumReplicas() const { return num_replicas; }
  const std::vector<PlacementBlock>& PlacementBlocks() const { return placement_blocks; }
  const std::vector<PlacementBlock>& ReadReplicaPlacementBlocks() const {
    return read_replica_placement_blocks;
  }

  const std::string& LiveJson() const { return live_json_; }
  std::string ReadReplicaJson() const { return read_replica_json_.value_or(""); }
  std::optional<int32_t> ReadReplicaNumReplicas() const { return read_replica_num_replicas_; }
  const std::string& CreateCmd() const { return create_cmd_; }

 private:
  static int32_t TotalMinReplicas(const std::vector<PlacementBlock>& placement_blocks) {
    int32_t total = 0;
    for (const auto& block : placement_blocks) {
      total += block.min_num_replicas();
    }
    return total;
  }

  static std::string GeneratePlacementJson(
      const std::vector<PlacementBlock>& blocks, int32_t num_replicas,
      const std::string& placement_uuid = "") {
    std::ostringstream os;
    os << "{\"num_replicas\":" << num_replicas;
    if (!placement_uuid.empty()) {
      os << ",\"placement_uuid\":\"" << placement_uuid << "\"";
    }
    if (!blocks.empty()) {
      os << ",\"placement_blocks\":[";
      for (size_t i = 0; i < blocks.size(); ++i) {
        os << blocks[i].ToJson();
        if (i + 1 < blocks.size()) {
          os << ",";
        }
      }
      os << "]";
    }
    os << "}";
    return os.str();
  }

  std::string GenerateCreateCmd() const {
    std::ostringstream os;
    os << "CREATE TABLESPACE " << name << " WITH (replica_placement='" << live_json_ << "'";
    if (read_replica_json_) {
      os << ", read_replica_placement='" << *read_replica_json_ << "'";
    }
    os << ")";
    return os.str();
  }

  std::optional<int32_t> read_replica_num_replicas_;
  std::string live_json_;
  std::optional<std::string> read_replica_json_;
  std::string create_cmd_;
};

}  // namespace yb::test
