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
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/common_net.h"

using namespace std::literals;

namespace yb {

namespace {

std::pair<std::string_view, std::string_view> SplitString(std::string_view v, char c) {
  size_t pos = v.find(c);
  if (pos == v.npos) {
    return {v, ""sv};
  }
  return {v.substr(0, pos), v.substr(pos + 1)};
}

struct PlacementInfo {
  int32_t replicas;

  struct PlacementBlock {
    int32_t min_replicas;
    std::string_view placement;
  };
  std::vector<PlacementBlock> blocks;

  std::optional<std::string_view> uuid = std::nullopt;

  PlacementInfoPB ToPB() const {
    PlacementInfoPB out;
    out.set_num_replicas(replicas);
    if (uuid) {
      out.set_placement_uuid(uuid->begin(), uuid->size());
    }
    for (const auto& block : blocks) {
      auto* placement_block = out.add_placement_blocks();
      placement_block->set_min_num_replicas(block.min_replicas);

      auto* cloud_info = placement_block->mutable_cloud_info();

      auto [cloud, region_zone] = SplitString(block.placement, '.');
      cloud_info->set_placement_cloud(cloud.begin(), cloud.size());
      if (!region_zone.empty() && region_zone != "*") {
        auto [region, zone] = SplitString(region_zone, '.');
        cloud_info->set_placement_region(region.begin(), region.size());

        if (!zone.empty() && zone != "*") {
          cloud_info->set_placement_zone(zone.begin(), zone.size());
        }
      }
    }
    return out;
  }
};

bool PlacementInfoContains(PlacementInfo lhs, PlacementInfo rhs) {
  return PlacementInfoContainsPlacementInfo(lhs.ToPB(), rhs.ToPB());
}

} // namespace

TEST(TestPlacementInfoContainsPlacementInfo, TestSimple) {
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 2, .placement = "c1.r1.z1" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} },
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z2" },
            { .min_replicas = 1, .placement = "c1.r1.z1" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));
}

TEST(TestPlacementInfoContainsPlacementInfo, TestWildcard) {
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.*" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.*" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.*" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 2, .placement = "c1.r2.z1" }} }));

  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.*" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.*" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.*" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.*" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.*" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.*" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 2, .placement = "c2.r1.z1" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.*" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 2, .placement = "c2.*" }} }));

  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r1.z1" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r1.z1" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));

  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} },
      { .replicas = 1, .blocks = {{ .min_replicas = 1, .placement = "c1.r2.z1" }} }));

  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} },
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} },
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r2.*" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} }));

  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.*" },
            { .min_replicas = 1, .placement = "c2.*" }} },
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c2.r1.*" }} }));

  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 2, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 2, .placement = "c1.r2.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} }));

  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 3, .blocks = {{ .min_replicas = 3, .placement = "c1.*" }} },
      { .replicas = 3, .blocks = {{ .min_replicas = 3, .placement = "c1.r1.*" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 3, .blocks = {{ .min_replicas = 3, .placement = "c1.r1.*" }} },
      { .replicas = 3, .blocks = {{ .min_replicas = 3, .placement = "c1.*" }} }));
}

TEST(TestPlacementInfoContainsPlacementInfo, TestUUID) {
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 1,
        .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }},
        .uuid = "01234567-89ab-cdef-0123-456789abcdef" },
      { .replicas = 1,
        .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }},
        .uuid = "01234567-89ab-cdef-0123-456789abcdef" }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 1,
        .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }},
        .uuid = "01234567-89ab-cdef-0123-456789abcdef" },
      { .replicas = 1,
        .blocks = {{ .min_replicas = 1, .placement = "c2.r2.z2" }},
        .uuid = "01234567-89ab-cdef-0123-456789abcdef" }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 1,
        .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }},
        .uuid = "01234567-89ab-cdef-0123-456789abcdef" },
      { .replicas = 1,
        .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }},
        .uuid = "00000000-0000-0000-0000-000000000000" }));
}

TEST(TestPlacementInfoContainsPlacementInfo, TestSlack) {
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} },
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" }} }));

  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r1.z2" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} }));

  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.*" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.z1" },
            { .min_replicas = 1, .placement = "c1.r3.z1" }} }));

  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));

  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 2,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 2, .blocks = {{ .min_replicas = 1, .placement = "c1.r1.z1" }} }));

  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" },
            { .min_replicas = 1, .placement = "c1.r3.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.z1" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r4.z1" }} }));

  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.*" }} }));
  ASSERT_FALSE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 3, .placement = "c1.*" }} }));
  ASSERT_TRUE(PlacementInfoContains(
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.*" }} },
      { .replicas = 3,
        .blocks = {
            { .min_replicas = 1, .placement = "c1.r1.z1" },
            { .min_replicas = 1, .placement = "c1.r2.*" }} }));
}

} // namespace yb
