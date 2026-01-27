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

#include "yb/common/common_net.h"

#include <algorithm>
#include <compare>
#include <map>
#include <string>
#include <utility>

#include "yb/util/logging.h"

namespace yb {

HostPortPB MakeHostPortPB(std::string&& host, uint32_t port) {
  HostPortPB result;
  result.set_host(std::move(host));
  result.set_port(port);
  return result;
}

CloudInfoPB MakeCloudInfoPB(std::string&& cloud, std::string&& region, std::string&& zone) {
  CloudInfoPB result;
  *result.mutable_placement_cloud() = std::move(cloud);
  *result.mutable_placement_region() = std::move(region);
  *result.mutable_placement_zone() = std::move(zone);
  return result;
}

bool IsCloudInfoEqual(const CloudInfoPB& lhs, const CloudInfoPB& rhs) {
  return (lhs.placement_cloud() == rhs.placement_cloud() &&
          lhs.placement_region() == rhs.placement_region() &&
          lhs.placement_zone() == rhs.placement_zone());
}

bool CloudInfoContainsCloudInfo(const CloudInfoPB& lhs, const CloudInfoPB& rhs) {
  if (!lhs.has_placement_cloud()) return true;
  if (lhs.placement_cloud() != rhs.placement_cloud()) return false;
  if (!lhs.has_placement_region()) return true;
  if (lhs.placement_region() != rhs.placement_region()) return false;
  if (!lhs.has_placement_zone()) return true;
  return lhs.placement_zone() == rhs.placement_zone();
}

bool PlacementInfoContainsCloudInfo(
    const PlacementInfoPB& placement_info, const CloudInfoPB& cloud_info) {
  for (const auto& placement_block : placement_info.placement_blocks()) {
    if (CloudInfoContainsCloudInfo(placement_block.cloud_info(), cloud_info)) {
      return true;
    }
  }
  return false;
}

bool CloudInfoMatchesPlacementInfo(
    const CloudInfoPB& cloud_info, const PlacementInfoPB& placement_info) {
  for (const auto& placement_block : placement_info.placement_blocks()) {
    if (CloudInfoContainsCloudInfo(cloud_info, placement_block.cloud_info())) {
      return true;
    }
  }
  return false;
}

bool PlacementInfoSpansMultipleRegions(const PlacementInfoPB& placement_info) {
  int num_blocks = placement_info.placement_blocks_size();
  if (num_blocks < 2) {
    return false;
  }
  const auto& first_block = placement_info.placement_blocks(0).cloud_info();
  for (int i = 1; i < num_blocks; ++i) {
    const auto& cur_block = placement_info.placement_blocks(i).cloud_info();
    if (first_block.placement_cloud() != cur_block.placement_cloud() ||
        first_block.placement_region() != cur_block.placement_region()) {
      return true;
    }
  }
  return false;
}

namespace {

struct CloudInfoLess {
  bool operator()(const CloudInfoPB& lhs, const CloudInfoPB& rhs) const {
    if (!lhs.has_placement_cloud()) {
      return rhs.has_placement_cloud();
    }
    auto cmp = lhs.placement_cloud() <=> rhs.placement_cloud();
    if (cmp != std::strong_ordering::equivalent) {
      return cmp == std::strong_ordering::less;
    }

    if (!lhs.has_placement_region()) {
      return rhs.has_placement_region();
    }
    cmp = lhs.placement_region() <=> rhs.placement_region();
    if (cmp != std::strong_ordering::equivalent) {
      return cmp == std::strong_ordering::less;
    }

    if (!lhs.has_placement_zone()) {
      return rhs.has_placement_zone();
    }
    return lhs.placement_zone() < rhs.placement_zone();
  }
};

void SetToCommonAncestor(CloudInfoPB& lhs, const CloudInfoPB& rhs) {
  if (!rhs.has_placement_cloud() || lhs.placement_cloud() != rhs.placement_cloud()) {
    lhs.clear_placement_cloud();
    lhs.clear_placement_region();
    lhs.clear_placement_zone();
  } else if (!rhs.has_placement_region() || lhs.placement_region() != rhs.placement_region()) {
    lhs.clear_placement_region();
    lhs.clear_placement_zone();
  } else if (!rhs.has_placement_zone() || lhs.placement_zone() != rhs.placement_zone()) {
    lhs.clear_placement_zone();
  }
}

CloudInfoPB CommonAncestorCloudInfo(const PlacementInfoPB& placement) {
  const auto& blocks = placement.placement_blocks();
  auto itr = blocks.begin();
  // Sanity check - this should never be the case for valid placement info.
  DCHECK(itr != blocks.end());

  CloudInfoPB common_ancestor = itr->cloud_info();
  ++itr;
  for (; itr != blocks.end(); ++itr) {
    SetToCommonAncestor(common_ancestor, itr->cloud_info());
  }
  return common_ancestor;
}

} // namespace

bool PlacementInfoContainsPlacementInfo(const PlacementInfoPB& lhs, const PlacementInfoPB& rhs) {
  if (lhs.has_placement_uuid() || rhs.has_placement_uuid()) {
    if (!lhs.has_placement_uuid() || !rhs.has_placement_uuid() ||
        lhs.placement_uuid() != rhs.placement_uuid()) {
      return false;
    }
  }

  if (lhs.num_replicas() < rhs.num_replicas()) {
    return false;
  }

  // Basic idea is to try to match replicas of RHS to as specific as possible replicas of LHS.
  // For each placement block, we have exactly min_num_replicas replicas of that placement, and then
  // we have "slack" replicas that can be in of the placement blocks for the remaining replicas.
  // We then try to match replicas from RHS to LHS, e.g. given a.b.c on RHS, try to match to
  // a.b.c on LHS, then to a.b.*, then a.*, then to LHS slack.
  // For slack replicas on RHS, we start matching from the common prefix, then to LHS slack, e.g.
  // RHS slack that can go into a.b.* or a.c.* is first matched to LHS a.*, then to LHS slack.

  std::map<CloudInfoPB, size_t, CloudInfoLess> available_replicas;
  size_t available_slack = lhs.num_replicas();
  for (const auto& block : lhs.placement_blocks()) {
    size_t num_replicas = block.min_num_replicas();
    DCHECK_GE(available_slack, num_replicas);
    available_slack -= num_replicas;
    available_replicas[block.cloud_info()] += num_replicas;
  }

  auto match_cloud_info = [&](const CloudInfoPB& info, size_t num_replicas) {
    CloudInfoPB cloud_info = info;
    auto match_replicas = [&] {
      auto itr = available_replicas.find(cloud_info);
      if (itr != available_replicas.end()) {
        if (itr->second >= num_replicas) {
          itr->second -= num_replicas;
          return true;
        }
        num_replicas -= itr->second;
        itr->second = 0;
      }
      return false;
    };

    if (cloud_info.has_placement_zone() && match_replicas()) {
      return 0uz;
    }
    cloud_info.clear_placement_zone();

    if (cloud_info.has_placement_region() && match_replicas()) {
      return 0uz;
    }
    cloud_info.clear_placement_region();

    if (cloud_info.has_placement_cloud() && match_replicas()) {
      return 0uz;
    }

    if (PlacementInfoContainsCloudInfo(lhs, info) && available_slack >= num_replicas) {
      available_slack -= num_replicas;
      return 0uz;
    }

    return num_replicas;
  };

  size_t rhs_slack = rhs.num_replicas();
  for (const auto& block : rhs.placement_blocks()) {
    size_t num_replicas = block.min_num_replicas();
    DCHECK_GE(rhs_slack, num_replicas);
    rhs_slack -= num_replicas;
    if (match_cloud_info(block.cloud_info(), num_replicas) != 0uz) {
      return false;
    }
  }
  if (rhs_slack == 0) {
    return true;
  }

  // If RHS slack is matchable to lhs non-slack replicas, common ancestor is also matchable.
  auto common_ancestor = CommonAncestorCloudInfo(rhs);
  size_t unmatched = match_cloud_info(common_ancestor, rhs_slack);
  if (unmatched == 0uz) {
    return true;
  }

  // Unmatched replicas here must be mapped to lhs slack replicas. This could be because we have,
  // e.g. LHS { a.b.*, a.c.* } and RHS { a.b.*, a.c.* }, so RHS common ancestor is a.*, which
  // matches no single placement block in LHS, but RHS slack replicas do match lhs slack replicas.
  if (unmatched > available_slack) {
    return false;
  }
  // RHS slack matches to LHS slack if and only if every RHS block is contained by a LHS block.
  for (const auto& rhs_block : rhs.placement_blocks()) {
    if (!PlacementInfoContainsCloudInfo(lhs, rhs_block.cloud_info())) {
      return false;
    }
  }
  return true;
}

}  // namespace yb
