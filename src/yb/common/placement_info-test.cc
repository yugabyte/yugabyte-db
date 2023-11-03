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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include <gtest/gtest.h>

#include "yb/common/placement_info.h"
#include "yb/util/test_macros.h"

using std::string;
using std::vector;

namespace yb {

// Test the tablespace info parsing.
TEST(PlacementInfoTest, TestTablespaceJsonProcessing) {
  // Variables to be used throughout the test.
  const string& valid_json =
      "{\"num_replicas\":3,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":2},"
      "{\"cloud\":\"c2\",\"region\":\"r2\",\"zone\":\"z2\",\"min_num_replicas\":1}]}";

  auto option = "replica_placement=" + valid_json;
  auto invalid_option = "read_replica_placement=" + valid_json;

  vector<std::string> options;

  // Negative tests.
  // 1. Empty input.
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 2. Invalid number of options.
  options.push_back(option);
  options.push_back(invalid_option);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 3. Invalid option name.
  options.clear();
  options.push_back(invalid_option);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 4. Empty json.
  options.clear();
  auto opt_empty_value = "replica_placement=[{}]";
  options.emplace_back(opt_empty_value);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 5. Missing num_replicas field.
  options.clear();
  auto invalid_json_option = "replica_placement={\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":3}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 6. Invalid value for num_replicas field.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":\"abc\",\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":3}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 7. Missing placement blocks field.
  options.clear();
  invalid_json_option = "replica_placement={\"num_replicas\":3}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 8. Missing keys in placement blocks.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":\"abc\",\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\"}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 9. Invalid format for "min_num_replicas".
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":3,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":\"abc\"}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 10. Invalid json.
  options.clear();
  invalid_json_option = "replica_placement=["
      "{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_number_of_replica";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(PlacementInfoConverter::FromQLValue(options));

  // 11. Test whether total replication factor is populated correctly.
  options.clear();
  options.push_back(option);
  PlacementInfoConverter::Placement result = EXPECT_RESULT(
      PlacementInfoConverter::FromQLValue(options));
  const auto placement_infos = result.placement_infos;
  ASSERT_EQ(result.num_replicas, 3);

  // 12. Test whether the cloud/region/zone information has been populated correctly.
  ASSERT_EQ(placement_infos.size(), 2);
  for (auto& placement_block : placement_infos) {
    if (placement_block.cloud == "c1") {
      ASSERT_EQ(placement_block.region, "r1");
      ASSERT_EQ(placement_block.zone, "z1");
      ASSERT_EQ(placement_block.min_num_replicas, 2);
      continue;
    }
    ASSERT_EQ(placement_block.cloud, "c2");
    ASSERT_EQ(placement_block.region, "r2");
    ASSERT_EQ(placement_block.zone, "z2");
    ASSERT_EQ(placement_block.min_num_replicas, 1);
  }
}

// Test the tablespace preferred zone info parsing.
TEST(PlacementInfoTest, TestPreferredZoneJsonProcessing) {
  // Variables to be used throughout the test.
  const string& zone1 = R"#("cloud":"c1","region":"r1","zone":"z1","min_num_replicas":1)#";
  const string& zone2 = R"#("cloud":"c1","region":"r1","zone":"z2","min_num_replicas":1)#";
  const string& zone3 = R"#("cloud":"c1","region":"r1","zone":"z3","min_num_replicas":1)#";
  const string format =
      R"#(replica_placement={"num_replicas":3,"placement_blocks": [{$0},{$1},{$2}]})#";

  // Valid option with no preferred zones.
  {
    auto no_preferred_zone = strings::Substitute(format, zone1, zone2, zone3);
    PlacementInfoConverter::Placement result =
        ASSERT_RESULT(PlacementInfoConverter::FromQLValue(vector<std::string>{no_preferred_zone}));
    for (auto& info : result.placement_infos) {
      ASSERT_EQ(info.leader_preference, 0);
    }
  }

  // Negative priority.
  {
    auto negative_priority =
        strings::Substitute(format, zone1 + R"#(,"leader_preference":-1)#", zone2, zone3);
    ASSERT_NOK(PlacementInfoConverter::FromQLValue(vector<std::string>{negative_priority}));
  }

  // Zero priority.
  {
    auto zero_priority =
        strings::Substitute(format, zone1 + R"#(,"leader_preference":0)#", zone2, zone3);
    ASSERT_NOK(PlacementInfoConverter::FromQLValue(vector<std::string>{zero_priority}));
  }

  // No priority 1.
  {
    auto no_priority_1 =
        strings::Substitute(format, zone1 + R"#(,"leader_preference":2)#", zone2, zone3);
    ASSERT_NOK(PlacementInfoConverter::FromQLValue(vector<std::string>{no_priority_1}));
  }

  // Non contiguous priority.
  {
    auto non_cont_priority_1 = strings::Substitute(
        format,
        zone1 + R"#(,"leader_preference":1)#",
        zone2 + R"#(,"leader_preference":1)#",
        zone3 + R"#(,"leader_preference":3)#");
    ASSERT_NOK(PlacementInfoConverter::FromQLValue(vector<std::string>{non_cont_priority_1}));
  }

  // Non contiguous priority3.
  {
    auto non_cont_priority_3 = strings::Substitute(
        format,
        zone1 + R"#(,"leader_preference":1)#",
        zone2 + R"#(,"leader_preference":3)#",
        zone3 + R"#(,"leader_preference":3)#");

    ASSERT_NOK(PlacementInfoConverter::FromQLValue(vector<std::string>{non_cont_priority_3}));
  }

  // Only 1 zone has priority
  {
    auto one_zone_priority =
        strings::Substitute(format, zone1 + R"#(,"leader_preference":1)#", zone2, zone3);
    auto result =
        ASSERT_RESULT(PlacementInfoConverter::FromQLValue(vector<std::string>{one_zone_priority}));
    for (auto& info : result.placement_infos) {
      ASSERT_EQ(info.leader_preference, info.zone == "z1" ? 1 : 0);
    }
  }

  // Two zones have priority 1
  {
    auto two_zone_priority = strings::Substitute(
        format,
        zone1 + R"#(,"leader_preference":1)#",
        zone2 + R"#(,"leader_preference":1)#",
        zone3);
    auto result =
        ASSERT_RESULT(PlacementInfoConverter::FromQLValue(vector<std::string>{two_zone_priority}));
    for (auto& info : result.placement_infos) {
      ASSERT_EQ(info.leader_preference, info.zone == "z3" ? 0 : 1);
    }
  }

  // All unique priority
  {
    auto teo_zone_priority = strings::Substitute(
        format,
        zone1 + R"#(,"leader_preference":1)#",
        zone2 + R"#(,"leader_preference":2)#",
        zone3 + R"#(,"leader_preference":3)#");
    auto result =
        ASSERT_RESULT(PlacementInfoConverter::FromQLValue(vector<std::string>{teo_zone_priority}));
    for (auto& info : result.placement_infos) {
      ASSERT_EQ(info.leader_preference, info.zone[1] - '0');
    }
  }
}

} // namespace yb
