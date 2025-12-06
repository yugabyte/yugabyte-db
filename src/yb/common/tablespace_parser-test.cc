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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/tablespace_parser.h"

#include "yb/util/test_macros.h"

using std::string;
using std::vector;

DECLARE_bool(enable_tablespace_validation);

namespace yb {

// Test the tablespace info parsing.
TEST(TablespaceParserTest, TestTablespaceJsonProcessing) {
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
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Valid live placement option.
  options.push_back(option);
  ASSERT_OK(TablespaceParser::FromQLValue(options));

  // Adding the same option as a read replica option should fail,
  // since it is missing a placement_uuid.
  options.push_back(invalid_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Invalid option name (only read_replica_placement alone is not sufficient for live placement).
  options.clear();
  options.push_back(invalid_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Empty json.
  options.clear();
  auto opt_empty_value = "replica_placement=[{}]";
  options.emplace_back(opt_empty_value);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Missing num_replicas field.
  options.clear();
  auto invalid_json_option = "replica_placement={\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":3}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Invalid value for num_replicas field.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":\"abc\",\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":3}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Missing placement blocks field is ok.
  options.clear();
  invalid_json_option = "replica_placement={\"num_replicas\":3}";
  options.emplace_back(invalid_json_option);
  ASSERT_OK(TablespaceParser::FromQLValue(options));

  // Missing key (min_num_replicas) in placement blocks.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":\"abc\",\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\"}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Missing key (zone) in placement blocks.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Missing key (region) in placement blocks.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"zone\":\"z1\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Missing key (cloud) in placement blocks.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Invalid format for "min_num_replicas".
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":3,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":\"abc\"}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Invalid json.
  options.clear();
  invalid_json_option = "replica_placement=["
      "{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_number_of_replica";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Test whether total replication factor is populated correctly.
  options.clear();
  options.push_back(option);
  auto replication_info = ASSERT_RESULT(TablespaceParser::FromQLValue(options));
  ASSERT_EQ(replication_info.live_replicas().num_replicas(), 3);

  // Test whether the cloud/region/zone information has been populated correctly.
  auto& placement_infos = replication_info.live_replicas().placement_blocks();
  ASSERT_EQ(placement_infos.size(), 2);
  for (auto& placement_block : placement_infos) {
    if (placement_block.cloud_info().placement_cloud() == "c1") {
      ASSERT_EQ(placement_block.cloud_info().placement_region(), "r1");
      ASSERT_EQ(placement_block.cloud_info().placement_zone(), "z1");
      ASSERT_EQ(placement_block.min_num_replicas(), 2);
      continue;
    }
    ASSERT_EQ(placement_block.cloud_info().placement_cloud(), "c2");
    ASSERT_EQ(placement_block.cloud_info().placement_region(), "r2");
    ASSERT_EQ(placement_block.cloud_info().placement_zone(), "z2");
    ASSERT_EQ(placement_block.min_num_replicas(), 1);
  }

  // Wildcard placement for cloud is not possible.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"*\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Empty placement for cloud is not possible.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Empty placement for region is not possible.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Empty placement for zone is not possible.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Wildcard for region/zone has to be specified at all levels that follow.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"cloud1\",\"region\":\"*\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Wildcard for region/zone has to be specified at all levels that follow.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"*\",\"zone\":\"z1\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_NOK(TablespaceParser::FromQLValue(options));

  // Wildcard for region/zone has to be specified at all levels that follow.
  options.clear();
  invalid_json_option =
      "replica_placement={\"num_replicas\":1,\"placement_blocks\":"
      "[{\"cloud\":\"c1\",\"region\":\"*\",\"zone\":\"*\",\"min_num_replicas\":1}]}";
  options.emplace_back(invalid_json_option);
  ASSERT_OK(TablespaceParser::FromQLValue(options));
}

TEST(TablespaceParserTest, DuplicateReplicaPlacementOptionsFail) {
  const string live_option =
      "replica_placement='{"
      "\"num_replicas\": 1, "
      "\"placement_blocks\": [{"
      "\"cloud\": \"c1\", "
      "\"region\": \"r1\", "
      "\"zone\": \"z1\", "
      "\"min_num_replicas\": 1"
      "}]}'";

  vector<std::string> options = {
      live_option,
      live_option
  };

  auto result = TablespaceParser::FromQLValue(options);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(
      result.status().message().ToBuffer(), "duplicate replica_placement option found");
}

TEST(TablespaceParserTest, DuplicateReadReplicaPlacementOptionsFail) {
  const string read_option =
      "read_replica_placement='{"
      "\"num_replicas\": 1, "
      "\"placement_uuid\": \"read\", "
      "\"placement_blocks\": [{"
      "\"cloud\": \"c2\", "
      "\"region\": \"r2\", "
      "\"zone\": \"z2\", "
      "\"min_num_replicas\": 1"
      "}]}'";
  vector<std::string> options = {read_option, read_option};

  auto result = TablespaceParser::FromQLValue(options);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(
      result.status().message().ToBuffer(),
      "duplicate read_replica_placement option found. Only one "
      "replica_placement option is supported via tablespaces.");
}

TEST(TablespaceParserTest, InvalidTablespaceOptionFails) {
  const string live_option =
      "replica_placement='{"
      "\"num_replicas\": 1, "
      "\"placement_blocks\": [{"
      "\"cloud\": \"c1\", "
      "\"region\": \"r1\", "
      "\"zone\": \"z1\", "
      "\"min_num_replicas\": 1"
      "}]}'";
  vector<std::string> options = {live_option, "invalid_option=value"};

  auto result = TablespaceParser::FromQLValue(options);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(
      result.status().message().ToBuffer(),
      "expected replica_placement or read_replica_placement option. Got invalid_option=value");
}

TEST(TablespaceParserTest, ReadReplicaPlacementParsingErrors) {
  const auto kLivePlacement =
      "{\"num_replicas\":3,\"placement_blocks\":["
      "{\"cloud\":\"cloud0\",\"region\":\"rack1\",\"zone\":\"zone\",\"min_num_replicas\":1},"
      "{\"cloud\":\"cloud1\",\"region\":\"rack2\",\"zone\":\"zone\",\"min_num_replicas\":1},"
      "{\"cloud\":\"cloud2\",\"region\":\"rack3\",\"zone\":\"zone\",\"min_num_replicas\":1}]}";

  struct TablespaceParserTestCase {
    std::string name;
    std::string read_replica_json;
    std::string expected_error;
  };

  const std::vector<TablespaceParserTestCase> invalid_cases = {
    {"invalid_json", R"({"num_replicas":1)",
     "JSON parsing of read replica placement option failed"},
    {"array_with_empty_object", R"([{}])", "Invalid JSON type for read_replica_placement"},
    {"missing_num",
     R"({
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "Invalid or missing \"num_replicas\""},
    {"missing_num_empty_blocks",
     R"({
        "placement_uuid": "read_replica",
        "placement_blocks": []
      })",
      "Invalid or missing \"num_replicas\""},
    {"missing_uuid",
     R"({
        "num_replicas":1,
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "missing \"placement_uuid\""},
    {"invalid_uuid_type",
     R"({
        "num_replicas":1,
        "placement_uuid": 1,
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "Invalid type for \"placement_uuid\""},
    {"multiple_blocks_sum",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":1},
           {"cloud":"cloud0",
            "region":"rack5",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "Sum of min_num_replicas fields (2) exceeds the total replication factor"},
    {"missing_cloud",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"region":"rack4",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "Missing required key"},
    {"empty_cloud",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "\"cloud\" in placement block cannot be empty"},
    {"wildcard_cloud",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"*",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "cannot use wildcard placement at cloud level"},
    {"empty_region",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "\"region\" in placement block cannot be empty"},
    {"empty_zone",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":"",
            "min_num_replicas":1}
        ]
      })",
     "\"zone\" in placement block cannot be empty"},
    {"invalid_cloud_type",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":123,
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "Invalid type for \"cloud\" in placement block. Expected string, got number"},
    {"invalid_region_type",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":["array"],
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "Invalid type for \"region\" in placement block. Expected string, got array"},
    {"invalid_zone_type",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":{"nested": "object"},
            "min_num_replicas":1}
        ]
      })",
     "Invalid type for \"zone\" in placement block. Expected string, got object"},
    {"invalid_min_num_replicas_type",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":"one"}
        ]
      })",
     "Invalid type for \"min_num_replicas\" in placement block. Expected int, got string"},
    {"wildcard_region_without_zone",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"*",
            "zone":"zone",
            "min_num_replicas":1}
        ]
      })",
     "A wildcard '*' at region level should be followed by a wildcard at zone level"},
    {"min_replicas_not_one",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":2}
        ]
      })",
     "Sum of min_num_replicas fields (2) exceeds the total replication factor"},
    {"leader_preference",
     R"({
        "num_replicas":1,
        "placement_uuid": "read_replica",
        "placement_blocks": [
           {"cloud":"cloud0",
            "region":"rack4",
            "zone":"zone",
            "min_num_replicas":1,
            "leader_preference":1}
        ]
      })",
     "leader_preference is not supported for read replicas"},
};

  for (const auto& invalid_case : invalid_cases) {
    SCOPED_TRACE(invalid_case.name);
    auto result = TablespaceParser::FromString(kLivePlacement, invalid_case.read_replica_json);
    ASSERT_NOK_STR_CONTAINS(result, invalid_case.expected_error);
  }

  const std::vector<TablespaceParserTestCase> valid_cases = {
      {"empty_read_replica_placement", R"()", ""},
      {"no_blocks",
       R"({
          "placement_uuid": "read_replica",
          "num_replicas": 2
        })",
       ""},
      {"empty_blocks",
       R"({
          "placement_uuid": "read_replica",
          "num_replicas": 1,
          "placement_blocks": []
        })",
       ""},
      {"wildcard_region_and_zone",
       R"({
          "placement_uuid": "read_replica",
          "num_replicas": 1,
          "placement_blocks": [
            {"cloud":"c1",
             "region":"*",
             "zone":"*",
             "min_num_replicas":1}
          ]
        })",
       ""},
      {"zone_wildcard_only",
       R"({
          "placement_uuid": "read_replica",
          "num_replicas": 2,
          "placement_blocks": [
            {"cloud":"c1",
             "region":"r1",
             "zone":"*",
             "min_num_replicas":1},
            {"cloud":"c1",
             "region":"r2",
             "zone":"z2",
             "min_num_replicas":1}
          ]
        })",
       ""},
      {"multiple_blocks_sum_equals_num",
       R"({
          "placement_uuid": "read_replica",
          "num_replicas": 3,
          "placement_blocks": [
            {"cloud":"c1",
             "region":"r1",
             "zone":"z1",
             "min_num_replicas":1},
            {"cloud":"c1",
             "region":"r2",
             "zone":"z2",
             "min_num_replicas":2}
          ]
        })",
       ""},
  };

  for (const auto& valid_case : valid_cases) {
    SCOPED_TRACE(valid_case.name);
    auto result = TablespaceParser::FromString(kLivePlacement, valid_case.read_replica_json);
    ASSERT_OK(result);
  }
}

// Test the tablespace preferred zone info parsing.
TEST(TablespaceParserTest, TestPreferredZoneJsonProcessing) {
  // Variables to be used throughout the test.
  const string& zone1 = R"#("cloud":"c1","region":"r1","zone":"z1","min_num_replicas":1)#";
  const string& zone2 = R"#("cloud":"c1","region":"r1","zone":"z2","min_num_replicas":1)#";
  const string& zone3 = R"#("cloud":"c1","region":"r1","zone":"z3","min_num_replicas":1)#";
  const string format =
      R"#(replica_placement={"num_replicas":3,"placement_blocks": [{$0},{$1},{$2}]})#";

  // Valid option with no preferred zones.
  {
    auto no_preferred_zone = strings::Substitute(format, zone1, zone2, zone3);
    auto replication_info =
        ASSERT_RESULT(TablespaceParser::FromQLValue(vector<std::string>{no_preferred_zone}));
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 0);
  }

  // Negative priority.
  {
    auto negative_priority =
        strings::Substitute(format, zone1 + R"#(,"leader_preference":-1)#", zone2, zone3);
    ASSERT_NOK(TablespaceParser::FromQLValue(vector<std::string>{negative_priority}));
  }

  // Zero priority.
  {
    auto zero_priority =
        strings::Substitute(format, zone1 + R"#(,"leader_preference":0)#", zone2, zone3);
    ASSERT_NOK(TablespaceParser::FromQLValue(vector<std::string>{zero_priority}));
  }

  // No priority 1.
  {
    auto no_priority_1 =
        strings::Substitute(format, zone1 + R"#(,"leader_preference":2)#", zone2, zone3);
    ASSERT_NOK(TablespaceParser::FromQLValue(vector<std::string>{no_priority_1}));
  }

  // Non contiguous priority.
  {
    auto non_cont_priority_1 = strings::Substitute(
        format,
        zone1 + R"#(,"leader_preference":1)#",
        zone2 + R"#(,"leader_preference":1)#",
        zone3 + R"#(,"leader_preference":3)#");
    ASSERT_NOK(TablespaceParser::FromQLValue(vector<std::string>{non_cont_priority_1}));
  }

  // Non contiguous priority3.
  {
    auto non_cont_priority_3 = strings::Substitute(
        format,
        zone1 + R"#(,"leader_preference":1)#",
        zone2 + R"#(,"leader_preference":3)#",
        zone3 + R"#(,"leader_preference":3)#");

    ASSERT_NOK(TablespaceParser::FromQLValue(vector<std::string>{non_cont_priority_3}));
  }

  // Only 1 zone has priority
  {
    auto one_zone_priority =
        strings::Substitute(format, zone1 + R"#(,"leader_preference":1)#", zone2, zone3);
    auto replication_info =
        ASSERT_RESULT(TablespaceParser::FromQLValue(vector<std::string>{one_zone_priority}));
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(0).placement_zone(), "z1");
  }

  // Two zones have priority 1
  {
    auto two_zone_priority = strings::Substitute(
        format,
        zone1 + R"#(,"leader_preference":1)#",
        zone2 + R"#(,"leader_preference":1)#",
        zone3);
    auto replication_info =
        ASSERT_RESULT(TablespaceParser::FromQLValue(vector<std::string>{two_zone_priority}));
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones_size(), 2);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(0).placement_zone(), "z1");
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(1).placement_zone(), "z2");
  }

  // All unique priority
  {
    auto teo_zone_priority = strings::Substitute(
        format,
        zone1 + R"#(,"leader_preference":1)#",
        zone2 + R"#(,"leader_preference":2)#",
        zone3 + R"#(,"leader_preference":3)#");
    auto replication_info =
        ASSERT_RESULT(TablespaceParser::FromQLValue(vector<std::string>{teo_zone_priority}));
    ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 3);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(0).zones(0).placement_zone(), "z1");
    ASSERT_EQ(replication_info.multi_affinitized_leaders(1).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(1).zones(0).placement_zone(), "z2");
    ASSERT_EQ(replication_info.multi_affinitized_leaders(2).zones_size(), 1);
    ASSERT_EQ(replication_info.multi_affinitized_leaders(2).zones(0).placement_zone(), "z3");
  }
}

TEST(TablespaceParserTest, TestDisabledTablespaceValidation) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablespace_validation) = false;

  // Check that we still have some basic validation checks.
  const string invalid_placement = R"#(replica_placement={"num_replicas":"incorrect"})#";
  auto result = TablespaceParser::FromQLValue({invalid_placement});
  ASSERT_NOK_STR_CONTAINS(result, "Invalid or missing \"num_replicas\" field");

  // Should be able to create replication info, despite the duplicate placement blocks, when
  // FLAGS_enable_tablespace_validation is false.
  const string& zone1 = R"#("cloud":"c1","region":"r1","zone":"z1","min_num_replicas":1)#";
  const string& zone1_copy = R"#("cloud":"c1","region":"r1","zone":"z1","min_num_replicas":1)#";
  const string& zone3 = R"#("cloud":"c1","region":"r1","zone":"z3","min_num_replicas":1)#";
  const string format =
      R"#(replica_placement={"num_replicas":3,"placement_blocks": [{$0},{$1},{$2}]})#";
  const auto duplicate_placement = strings::Substitute(format, zone1, zone1_copy, zone3);
  ASSERT_RESULT(TablespaceParser::FromQLValue({duplicate_placement}));
}

TEST(TablespaceParserTest, TestDuplicatePlacementBlocks) {
  const string& zone1 = R"#("cloud":"c1","region":"r1","zone":"z1","min_num_replicas":1)#";
  const string& zone1_copy = R"#("cloud":"c1","region":"r1","zone":"z1","min_num_replicas":1)#";
  const string& zone3 = R"#("cloud":"c1","region":"r1","zone":"z3","min_num_replicas":1)#";
  const string format =
      R"#(replica_placement={"num_replicas":3,"placement_blocks": [{$0},{$1},{$2}]})#";
  const auto duplicate_placement = strings::Substitute(format, zone1, zone1_copy, zone3);

  // Should fail to create replication info because of duplicate placement blocks.
  auto result = TablespaceParser::FromQLValue({duplicate_placement});
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().message().ToBuffer(), "Duplicate placement block");
}

TEST(TablespaceParserTest, FromStringPopulatesReplicas) {
  const string& live_placement_json =
      "{\"num_replicas\":1,\"placement_blocks\":["
      "{\"cloud\":\"live\",\"region\":\"r_live\",\"zone\":\"z_live\",\"min_num_replicas\":1}]}";
  const string& read_replica_placement_json =
      "{\"num_replicas\":1,\"placement_uuid\": \"read_replica\", \"placement_blocks\":["
      "{\"cloud\":\"c1\",\"region\":\"r1\",\"zone\":\"z1\",\"min_num_replicas\":1}]}";

  auto replication_info = ASSERT_RESULT(
      TablespaceParser::FromString(live_placement_json, read_replica_placement_json, true));

  // Validate live replicas.
  const auto& live_replicas = replication_info.live_replicas();
  ASSERT_EQ(live_replicas.num_replicas(), 1);
  ASSERT_EQ(live_replicas.placement_blocks_size(), 1);
  const auto& live_block = live_replicas.placement_blocks(0);
  ASSERT_EQ(live_block.min_num_replicas(), 1);
  ASSERT_EQ(live_block.cloud_info().placement_cloud(), "live");
  ASSERT_EQ(live_block.cloud_info().placement_region(), "r_live");
  ASSERT_EQ(live_block.cloud_info().placement_zone(), "z_live");

  // Validate read replicas.
  ASSERT_EQ(replication_info.read_replicas_size(), 1);
  const auto& read_replica = replication_info.read_replicas(0);
  ASSERT_EQ(read_replica.num_replicas(), 1);
  ASSERT_EQ(read_replica.placement_blocks_size(), 1);
  const auto& block = read_replica.placement_blocks(0);
  ASSERT_EQ(block.min_num_replicas(), 1);
  ASSERT_EQ(block.cloud_info().placement_cloud(), "c1");
  ASSERT_EQ(block.cloud_info().placement_region(), "r1");
  ASSERT_EQ(block.cloud_info().placement_zone(), "z1");
  ASSERT_EQ(replication_info.multi_affinitized_leaders_size(), 0);
}

} // namespace yb
