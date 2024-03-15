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

#include <memory>
#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

METRIC_DEFINE_entity(tablet);

METRIC_DEFINE_entity(table);

METRIC_DEFINE_counter(server, server_metric, "server_metric_label",
    yb::MetricUnit::kMilliseconds, "server_metric description");

METRIC_DEFINE_counter(table, table_metric, "table_metric_label",
    yb::MetricUnit::kMilliseconds, "table_metric description");

METRIC_DEFINE_counter(tablet, tablet_metric_1, "tablet_metric_1 Label",
    yb::MetricUnit::kMilliseconds, "tablet_metric_1 description");

METRIC_DEFINE_counter(tablet, tablet_metric_2, "tablet_metric_2 label",
    yb::MetricUnit::kMilliseconds, "tablet_metric_2 description");

namespace yb {

static const std::string kServerMetricName = "server_metric";
static const std::string kTableMetricName = "table_metric";
static const std::string kTabletMetricName1 = "tablet_metric_1";
static const std::string kTabletMetricName2 = "tablet_metric_2";

class PrometheusMetricFilterTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();

    MetricEntity::AttributeMap entity_attr;
    entity_attr["table_id"] = "table_id_1";

    table_entity_ =
        METRIC_ENTITY_table.Instantiate(&registry_, "table_entity_id", entity_attr);

    entity_attr["tablet_id"] = "tablet_id_2";

    tablet_entity_ =
        METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet_entity_id", entity_attr);
    server_entity_ =
        METRIC_ENTITY_server.Instantiate(&registry_, "server_entity_id");

    server_metric_ = METRIC_server_metric.Instantiate(server_entity_);
    table_metric_ = METRIC_table_metric.Instantiate(table_entity_);
    tablet_metric_1_ = METRIC_tablet_metric_1.Instantiate(tablet_entity_);
    tablet_metric_2_ = METRIC_tablet_metric_2.Instantiate(tablet_entity_);
  }

 protected:
  Result<std::unordered_map<std::string, AggregationLevels>> GetAndParseMetricOutput(
      const MetricPrometheusOptions& opts) {
    if (opts.export_help_and_type == true) {
      return STATUS(IllegalState, "export_help_and_type should be disabled.");
    }
    std::stringstream output;
    PrometheusWriter writer(&output, opts);
    RETURN_NOT_OK(registry_.WriteForPrometheus(&writer, opts));

    std::unordered_map<std::string, AggregationLevels> names_to_levels;
    std::string line;
    while (std::getline(output, line)) {
      auto open_brace_idx = line.find("{");
      if (open_brace_idx == std::string::npos) {
        return STATUS_FORMAT(IllegalState,
            "Failed to obtain metric name from $0, because open brace cannot be found.", line);
      }
      auto metric_name = line.substr(0, open_brace_idx);

      if (line.find("table_id=") != std::string::npos) {
        names_to_levels[metric_name] |= kTableLevel;
      } else {
        names_to_levels[metric_name] |= kServerLevel;
      }
    }

    return names_to_levels;
  }

  MetricRegistry registry_;

  scoped_refptr<MetricEntity> table_entity_;
  scoped_refptr<MetricEntity> tablet_entity_;
  scoped_refptr<MetricEntity> server_entity_;

  scoped_refptr<Counter> server_metric_;
  scoped_refptr<Counter> table_metric_;
  scoped_refptr<Counter> tablet_metric_1_;
  scoped_refptr<Counter> tablet_metric_2_;
};

TEST_F(PrometheusMetricFilterTest, TestV1Default) {
  MetricPrometheusOptions opts;
  ASSERT_EQ(kFilterVersionOne, opts.version);
  auto names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);
}

TEST_F(PrometheusMetricFilterTest, TestV1PriorityRegex) {
  MetricPrometheusOptions opts;
  ASSERT_EQ(kFilterVersionOne, opts.version);

  // Only tablet_metric_1 is allowed at the table level.
  opts.priority_regex_string = "tablet_m.*1";
  auto names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);

  // No metrics are allowed at the table level.
  opts.priority_regex_string = "";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);
}

TEST_F(PrometheusMetricFilterTest, TestV2Default) {
  MetricPrometheusOptions opts;
  opts.version = kFilterVersionTwo;
  auto names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);
}

TEST_F(PrometheusMetricFilterTest, TestV2TableLevel) {
  MetricPrometheusOptions opts;
  opts.version = kFilterVersionTwo;

  // Only tablet_metric_1 and table_metric are allowed at the table level.
  opts.table_allowlist_string = "tablet_m.*1|table_.*c";
  auto names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);

  // Now, table_metric is in both table allowlist and blocklist.
  opts.table_blocklist_string = "table_.*c";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);

  // Reset the table allowlist.
  opts.table_allowlist_string = ".*";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);

  // Test block all metrics at the table level.
  opts.table_blocklist_string = ".*";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);

  opts.table_allowlist_string = "";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);
}

TEST_F(PrometheusMetricFilterTest, TestV2ServerLevel) {
  MetricPrometheusOptions opts;
  opts.version = kFilterVersionTwo;

  // Only tablet_metric_1 and table_metric are allowed at the server level.
  opts.server_allowlist_string = "tablet_m.*1|table_.*c";
  auto names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(0, names_to_levels[kServerMetricName]);

  // Now, table_metric is in both server allowlist and blocklist.
  opts.server_blocklist_string = "table_.*c";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(0, names_to_levels[kServerMetricName]);

  // Reset the server allowlist to only test server blocklist.
  opts.server_allowlist_string = ".*";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);

  // Test block all metrics at the server level.
  opts.server_blocklist_string = ".*";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(0, names_to_levels[kServerMetricName]);

  opts.server_allowlist_string = "";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(0, names_to_levels[kServerMetricName]);
}

} // namespace yb
