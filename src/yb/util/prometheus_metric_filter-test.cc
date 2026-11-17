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

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <gtest/gtest.h>

#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/prometheus_metric_filter.h"
#include "yb/util/status_format.h"
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

    // Initialize metric entities.
    entity_attr["table_type"] = "PGSQL_TABLE_TYPE";
    entity_attr["table_id"] = "table_id_1";
    table_entity_1_ =
        METRIC_ENTITY_table.Instantiate(&registry_, "table_entity_id_1", entity_attr);

    entity_attr["table_id"] = "table_id_2";
    table_entity_2_ =
        METRIC_ENTITY_table.Instantiate(&registry_, "table_entity_id_2", entity_attr);

    entity_attr["tablet_id"] = "tablet_id_2";
    tablet_entity_ =
        METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet_entity_id", entity_attr);

    server_entity_ =
        METRIC_ENTITY_server.Instantiate(&registry_, "server_entity_id");

    // Make sure counter metrics will not be retired by adding references.
    metrics_.emplace_back(METRIC_server_metric.Instantiate(server_entity_));
    metrics_.emplace_back(METRIC_table_metric.Instantiate(table_entity_1_));
    metrics_.emplace_back(METRIC_table_metric.Instantiate(table_entity_2_));
    metrics_.emplace_back(METRIC_tablet_metric_1.Instantiate(tablet_entity_));
    metrics_.emplace_back(METRIC_tablet_metric_2.Instantiate(tablet_entity_));
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

  scoped_refptr<MetricEntity> table_entity_1_;
  scoped_refptr<MetricEntity> table_entity_2_;
  scoped_refptr<MetricEntity> tablet_entity_;
  scoped_refptr<MetricEntity> server_entity_;

  std::vector<scoped_refptr<Counter>> metrics_;
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

TEST_F(PrometheusMetricFilterTest, TestV1GeneralMetricsAllowlist) {
  MetricPrometheusOptions opts;
  ASSERT_EQ(kFilterVersionOne, opts.version);
  std::vector<std::string> allowlist_ = {"tablet_", "server_"};
  opts.general_metrics_allowlist = allowlist_;

  auto names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kNoLevel, names_to_levels[kTableMetricName]);
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
  ASSERT_EQ(kNoLevel, names_to_levels[kServerMetricName]);

  // Now, table_metric is in both server allowlist and blocklist.
  opts.server_blocklist_string = "table_.*c";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kNoLevel, names_to_levels[kServerMetricName]);

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
  ASSERT_EQ(kNoLevel, names_to_levels[kServerMetricName]);

  opts.server_allowlist_string = "";
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));

  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kTableLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kNoLevel, names_to_levels[kServerMetricName]);
}

TEST_F(PrometheusMetricFilterTest, TestActiveTableIds) {
  auto GetOutput = [this](const MetricPrometheusOptions& opts) -> Result<std::string> {
    std::stringstream output;
    PrometheusWriter writer(&output, opts);
    RETURN_NOT_OK(registry_.WriteForPrometheus(&writer, opts));
    return output.str();
  };

  static const auto kTableIdLabel1 = R"#(table_id="table_id_1")#";
  static const auto kTableIdLabel2 = R"#(table_id="table_id_2")#";

  MetricPrometheusOptions opts;
  opts.active_table_ids = std::make_shared<std::unordered_set<std::string>>(
      std::initializer_list<std::string>{"table_id_1"});

  // v1 ignores the list: a table level metric has no server level copy there, so filtering it
  // would make node totals undercount.
  opts.version = kFilterVersionOne;
  auto output = ASSERT_RESULT(GetOutput(opts));
  ASSERT_STR_CONTAINS(output, kTableIdLabel1);
  ASSERT_STR_CONTAINS(output, kTableIdLabel2);

  opts.version = kFilterVersionTwo;

  // Without an active-table list, every table is exported.
  opts.active_table_ids.reset();
  output = ASSERT_RESULT(GetOutput(opts));
  ASSERT_STR_CONTAINS(output, kTableIdLabel1);
  ASSERT_STR_CONTAINS(output, kTableIdLabel2);
  ASSERT_STR_CONTAINS(output, kServerMetricName);

  // A list drops the table level series of every table outside of it.
  opts.active_table_ids = std::make_shared<std::unordered_set<std::string>>(
      std::initializer_list<std::string>{"table_id_1"});
  output = ASSERT_RESULT(GetOutput(opts));
  ASSERT_STR_CONTAINS(output, kTableIdLabel1);
  ASSERT_STR_NOT_CONTAINS(output, kTableIdLabel2);
  ASSERT_STR_CONTAINS(output, kServerMetricName);

  // An empty list drops all of them, while metrics without a table id are untouched.
  opts.active_table_ids = std::make_shared<std::unordered_set<std::string>>();
  output = ASSERT_RESULT(GetOutput(opts));
  ASSERT_STR_NOT_CONTAINS(output, "table_id=");
  ASSERT_STR_CONTAINS(output, kServerMetricName);
  ASSERT_STR_CONTAINS(output, kTableMetricName);
}

TEST_F(PrometheusMetricFilterTest, TestActiveTableIdsScope) {
  MetricPrometheusOptions opts;
  opts.version = kFilterVersionTwo;
  opts.active_table_ids = std::make_shared<std::unordered_set<std::string>>();
  auto filter = CreatePrometheusMetricFilter(opts);

  // The filter caches its decision per table ID, so every table gets its own ID.
  auto Attributes = [](
      const std::string& table_type, const std::string& namespace_name,
      const std::string& table_name) {
    return MetricEntity::AttributeMap{
        {"table_id", Format("$0.$1.$2", table_type, namespace_name, table_name)},
        {"table_type", table_type}, {"namespace_name", namespace_name},
        {"table_name", table_name}};
  };
  auto ShouldExport = [&filter](
      const MetricEntity::AttributeMap& attributes,
      const std::string& metric_entity_type = kTabletMetricEntityName) {
    return filter->ShouldExportTableMetrics(kTableMetricName, attributes, metric_entity_type);
  };

  const auto ysql_table = Attributes("PGSQL_TABLE_TYPE", "yugabyte", "user_table");
  ASSERT_FALSE(ShouldExport(ysql_table));
  ASSERT_FALSE(ShouldExport(ysql_table, kTableMetricEntityName));

  // Only table and tablet entities are filtered, even if another entity type carries a table id.
  ASSERT_TRUE(ShouldExport(ysql_table, kXClusterMetricEntityName));
  ASSERT_TRUE(ShouldExport(ysql_table, "new_entity_type"));

  // Server level aggregates carry no table id.
  ASSERT_TRUE(ShouldExport({{"metric_type", kTabletMetricEntityName}}));

  // A table without a table_type label cannot be shown to be a YSQL table, so it is not filtered.
  ASSERT_TRUE(ShouldExport({
      {"table_id", "no_table_type"}, {"namespace_name", "yugabyte"},
      {"table_name", "user_table"}}));

  // Only YSQL tables are filtered.
  ASSERT_TRUE(ShouldExport(Attributes("YQL_TABLE_TYPE", "keyspace", "user_table")));

  // Every table YugabyteDB creates itself, as listed in the IDEA-2151 design doc, is never
  // filtered.
  struct SystemTable {
    const char* table_type;
    const char* namespace_name;
    const char* table_name;
  };
  for (const auto& table : std::initializer_list<SystemTable>{
           // Transaction status tables, global and per placement, by table type.
           {"TRANSACTION_STATUS_TABLE_TYPE", "system", "transactions"},
           {"TRANSACTION_STATUS_TABLE_TYPE", "system", "transactions_a1b2c3"},
           // YCQL system tables, by table type.
           {"YQL_TABLE_TYPE", "system", "cdc_state"},
           {"YQL_TABLE_TYPE", "system", "pg_advisory_locks"},
           // YSQL tables in the namespaces YugabyteDB creates.
           {"PGSQL_TABLE_TYPE", "system", "internal_table"},
           {"PGSQL_TABLE_TYPE", "system_postgres", "sequences_data"},
           {"PGSQL_TABLE_TYPE", "yb_system", "pg_yb_notifications"},
           {"PGSQL_TABLE_TYPE", "yb_system", "another_yb_system_table"},
           // xCluster automatic mode DDL replication tables, which live in user databases.
           {"PGSQL_TABLE_TYPE", "yugabyte", "ddl_queue"},
           {"PGSQL_TABLE_TYPE", "yugabyte", "replicated_ddls"},
       }) {
    SCOPED_TRACE(Format("$0.$1", table.namespace_name, table.table_name));
    const auto attributes = Attributes(table.table_type, table.namespace_name, table.table_name);
    ASSERT_TRUE(ShouldExport(attributes));
    ASSERT_TRUE(ShouldExport(attributes, kTableMetricEntityName));
  }

  // Each table gets its own cached decision: a filtered table and an exempt table, interleaved over
  // several tablets, keep their own answers.
  const auto internal_table = Attributes("PGSQL_TABLE_TYPE", "yb_system", "pg_yb_notifications");
  for (int i = 0; i < 3; ++i) {
    ASSERT_FALSE(ShouldExport(ysql_table));
    ASSERT_TRUE(ShouldExport(internal_table));
  }

  // sequences_data is recognised by its fixed table ID, even when its tablets carry no
  // namespace_name, as tablets created before v2.3.0.0 do.
  ASSERT_TRUE(ShouldExport({
      {"table_id", "0000ffff00003000800000000000ffff"}, {"table_type", "PGSQL_TABLE_TYPE"},
      {"namespace_name", ""}, {"table_name", "sequences_data"}}));
  // A user table with the same name but its own ID is filtered.
  ASSERT_FALSE(ShouldExport(Attributes("PGSQL_TABLE_TYPE", "yugabyte", "sequences_data")));

  // Names are matched exactly, so similar user names are still filtered.
  ASSERT_FALSE(ShouldExport(Attributes("PGSQL_TABLE_TYPE", "yb_system_copy", "user_table")));
  ASSERT_FALSE(ShouldExport(Attributes("PGSQL_TABLE_TYPE", "yugabyte", "ddl_queue_copy")));

  // v1 never applies the list.
  opts.version = kFilterVersionOne;
  filter = CreatePrometheusMetricFilter(opts);
  ASSERT_TRUE(ShouldExport(ysql_table));
}

TEST_F(PrometheusMetricFilterTest, TestTableAlwaysAllowlist) {
  MetricPrometheusOptions opts;
  opts.version = kFilterVersionTwo;
  opts.active_table_ids = std::make_shared<std::unordered_set<std::string>>();
  opts.table_always_allowlist_string = kTableMetricName;

  std::stringstream output;
  PrometheusWriter writer(&output, opts);
  ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
  const auto output_str = output.str();

  // The always-allowed metric is exported for every table, although none is in the list.
  ASSERT_STR_CONTAINS(output_str, R"#(table_id="table_id_1")#");
  ASSERT_STR_CONTAINS(output_str, R"#(table_id="table_id_2")#");

  // Other table level metrics are still filtered, and server level metrics are untouched.
  auto names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));
  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName1]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName2]);
  ASSERT_EQ(kServerLevel, names_to_levels[kServerMetricName]);

  // The always-export list only exempts metrics that table_allowlist already exports per table. It
  // does not add table level series for a metric that table_allowlist leaves out.
  opts.table_allowlist_string = kTabletMetricName1;
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));
  ASSERT_EQ(kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kServerLevel, names_to_levels[kTabletMetricName1]);

  // Listing a table brings back its table level series of table_allowlist metrics only.
  opts.active_table_ids = std::make_shared<std::unordered_set<std::string>>(
      std::initializer_list<std::string>{"table_id_2"});
  names_to_levels = ASSERT_RESULT(GetAndParseMetricOutput(opts));
  ASSERT_EQ(kServerLevel, names_to_levels[kTableMetricName]);
  ASSERT_EQ(kTableLevel | kServerLevel, names_to_levels[kTabletMetricName1]);

  // v1 does not filter, so the always-export list has nothing to exempt.
  opts.version = kFilterVersionOne;
  opts.active_table_ids = std::make_shared<std::unordered_set<std::string>>();
  auto filter = CreatePrometheusMetricFilter(opts);
  ASSERT_TRUE(filter->ShouldExportTableMetrics(
      kTabletMetricName2,
      {{"table_id", "table_id_2"}, {"table_type", "PGSQL_TABLE_TYPE"},
       {"namespace_name", "yugabyte"}, {"table_name", "user_table"}},
      kTabletMetricEntityName));
}

TEST_F(PrometheusMetricFilterTest, TestActiveTableIdsWithMaxMetricEntries) {
  MetricPrometheusOptions opts;
  opts.version = kFilterVersionTwo;
  opts.table_allowlist_string = kTableMetricName;
  opts.server_allowlist_string = kServerMetricName;
  // Only table_id_1 and the server metric are left to export, so the two entries fit into the
  // budget and nothing is reported as cut off.
  opts.active_table_ids = std::make_shared<std::unordered_set<std::string>>(
      std::initializer_list<std::string>{"table_id_1"});
  opts.max_metric_entries = 2;

  NMSWriter::EntityMetricsMap table_metrics;
  NMSWriter::MetricsMap server_metrics;
  NMSWriter writer(&table_metrics, &server_metrics, opts);
  ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));

  ASSERT_EQ(1, table_metrics.size());
  ASSERT_TRUE(table_metrics.contains("table_id_1"));
  ASSERT_EQ(1, server_metrics.size());
  ASSERT_EQ(0, writer.TEST_GetNumberOfEntriesCutOff());
}

TEST_F(PrometheusMetricFilterTest, TestLimitMaxMetricEntries) {
  auto TabletMetricEntryCount = [](NMSWriter::EntityMetricsMap table_metrics){
    size_t num = 0;
    for (auto& [_, metric_map] : table_metrics) {
      num += metric_map.size();
    }
    return num;
  };

  MetricPrometheusOptions opts;
  opts.version = kFilterVersionTwo;
  opts.server_allowlist_string = kServerMetricName;
  opts.table_allowlist_string = kTableMetricName;

  {
    NMSWriter::EntityMetricsMap table_metrics;
    NMSWriter::MetricsMap server_metrics;
    NMSWriter writer(&table_metrics, &server_metrics, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    ASSERT_EQ(2, TabletMetricEntryCount(table_metrics));
    ASSERT_EQ(1, server_metrics.size());
    ASSERT_EQ(0, writer.TEST_GetNumberOfEntriesCutOff());
  }
  {
    opts.max_metric_entries = 2;
    NMSWriter::EntityMetricsMap table_metrics;
    NMSWriter::MetricsMap server_metrics;
    NMSWriter writer(&table_metrics, &server_metrics, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    ASSERT_EQ(0, TabletMetricEntryCount(table_metrics));
    ASSERT_EQ(1, server_metrics.size());
    ASSERT_EQ(2, writer.TEST_GetNumberOfEntriesCutOff());
  }
  {
    opts.max_metric_entries = 0;
    NMSWriter::EntityMetricsMap table_metrics;
    NMSWriter::MetricsMap server_metrics;
    NMSWriter writer(&table_metrics, &server_metrics, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    ASSERT_EQ(0, TabletMetricEntryCount(table_metrics));
    ASSERT_EQ(0, server_metrics.size());
    ASSERT_EQ(3, writer.TEST_GetNumberOfEntriesCutOff());
  }
}

} // namespace yb
