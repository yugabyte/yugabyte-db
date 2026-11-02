//
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
//

#include "yb/util/prometheus_metric_filter.h"

#include <boost/regex.hpp>

namespace yb {

namespace {

// The active-table list applies only to YSQL tables. It is built from YSQL ASH activity, so
// tables of other APIs cannot be ranked by it. TableType_Name(PGSQL_TABLE_TYPE).
const std::string kPgsqlTableType = "PGSQL_TABLE_TYPE";

// YSQL tables created by YugabyteDB itself rather than by the user. They keep all of their
// table-level metrics whatever the active-table list says, since the collector supplying the list
// cannot rank them, and new internal tables in these namespaces are covered without the collector
// having to know about them. Transaction status tables and YCQL system tables are not YSQL tables,
// so they are never filtered either.
bool IsInternalTable(const MetricEntity::AttributeMap& attributes) {
  // master::kSystemNamespaceName, the namespace of sequences_data (kPgSequencesNamespaceName in
  // pg_create_table.cc) and master::kYbSystemDbName.
  static const std::unordered_set<std::string> kInternalNamespaces = {
      "system", "system_postgres", "yb_system"};
  // The xCluster automatic mode DDL replication tables, xcluster::kDDLQueueTableName and
  // xcluster::kDDLReplicatedTableName, one pair per replicated database. The schema is not a metric
  // label, so a user table with the same name is exported too, which is harmless.
  static const std::unordered_set<std::string> kInternalTableNames = {
      "ddl_queue", "replicated_ddls"};
  // sequences_data has a fixed table ID, kPgSequencesDataTableId. Its tablets can carry an empty
  // namespace_name: tablets created before namespace names flowed from the master (v2.3.0.0) were
  // never backfilled, so the namespace check alone would miss it on old universes.
  static const std::string kPgSequencesDataTableId = "0000ffff00003000800000000000ffff";
  // The names above duplicate constants in yb/master, yb/cdc and yb/common, which this library
  // cannot depend on. TabletServerTest.ActiveTableMetricsInternalTableConstants keeps them in sync.

  auto it = attributes.find("table_id");
  if (it != attributes.end() && it->second == kPgSequencesDataTableId) {
    return true;
  }
  it = attributes.find("namespace_name");
  if (it != attributes.end() && kInternalNamespaces.contains(it->second)) {
    return true;
  }
  it = attributes.find("table_name");
  return it != attributes.end() && kInternalTableNames.contains(it->second);
}

} // namespace

ActiveTableListExemption GetActiveTableListExemption(const MetricEntity::AttributeMap& attributes) {
  const auto table_type = attributes.find("table_type");
  if (table_type == attributes.end() || table_type->second != kPgsqlTableType) {
    return ActiveTableListExemption::kNotYsqlTable;
  }
  return IsInternalTable(attributes) ? ActiveTableListExemption::kInternalTable
                                     : ActiveTableListExemption::kNone;
}

namespace {

class PrometheusMetricFilterV1 : public PrometheusMetricFilter {
 public:
  explicit PrometheusMetricFilterV1(const MetricPrometheusOptions& opts);

  AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_aggregation_levels) override;

  std::string Version() const override;

 private:
  bool ShouldCollectMetric(const std::string& metric_name) const;

  const boost::regex priority_regex_;

  const std::optional<std::vector<std::string>> general_metrics_allowlist_;
};

PrometheusMetricFilterV1::PrometheusMetricFilterV1(const MetricPrometheusOptions& opts)
    : priority_regex_(opts.priority_regex_string),
      general_metrics_allowlist_(opts.general_metrics_allowlist) {}

bool PrometheusMetricFilterV1::ShouldCollectMetric(const std::string& metric_name) const {
  if (!general_metrics_allowlist_) {
    // Metric is scraped if the general_metrics_allowlist_ is not provided.
    return true;
  }

  for (const auto& required_metric_substring : *general_metrics_allowlist_) {
    if (metric_name.find(required_metric_substring) != std::string::npos) {
      // Required metric substring is found in the metric name.
      return true;
    }
  }

  return false;
}

AggregationLevels PrometheusMetricFilterV1::GetAggregationLevels(
    const std::string& metric_name, AggregationLevels default_aggregation_levels) {
  auto [levels_from_regex_it, inserted] = metric_filter_.try_emplace(metric_name, kNoLevel);
  auto& levels_from_regex = levels_from_regex_it->second;

  if (inserted && ShouldCollectMetric(metric_name)) {
    // No filter for stream and server level.
    levels_from_regex = kStreamLevel | kServerLevel;
    if (boost::regex_match(metric_name, priority_regex_)) {
      levels_from_regex |= kTableLevel;
    }
  }

  auto aggregation_level = levels_from_regex & default_aggregation_levels;
  // If a metric will be exposed at the table level, it cannot be exposed at the server level.
  if (aggregation_level & kTableLevel) {
    aggregation_level &= ~kServerLevel;
  }

  return aggregation_level;
}

std::string PrometheusMetricFilterV1::Version() const {
  return kFilterVersionOne;
}

class PrometheusMetricFilterV2 : public PrometheusMetricFilter {
 public:
  explicit PrometheusMetricFilterV2(const MetricPrometheusOptions& opts);

  AggregationLevels GetAggregationLevels(
      const std::string& metric_name, AggregationLevels default_aggregation_levels) override;

  bool ShouldExportTableMetrics(
      const std::string& metric_name, const MetricEntity::AttributeMap& attributes,
      const std::string& metric_entity_type) override;

  std::string Version() const override;

 private:
  bool IsAlwaysExported(const std::string& metric_name);

  const boost::regex table_allowlist_;
  const boost::regex table_blocklist_;
  const boost::regex server_allowlist_;
  const boost::regex server_blocklist_;

  const std::shared_ptr<const std::unordered_set<std::string>> active_table_ids_;
  const bool has_table_always_allowlist_;
  const boost::regex table_always_allowlist_;
  // Result of matching table_always_allowlist_, by metric name, so that the regex runs once per
  // metric rather than once per table.
  std::unordered_map<std::string, bool> always_exported_by_metric_name_;
  // Whether a table is exempt from active_table_ids_ or listed in it, by table ID.
  std::unordered_map<std::string, bool> table_passes_list_by_table_id_;
};

PrometheusMetricFilterV2::PrometheusMetricFilterV2(const MetricPrometheusOptions& opts)
    : table_allowlist_(opts.table_allowlist_string),
      table_blocklist_(opts.table_blocklist_string),
      server_allowlist_(opts.server_allowlist_string),
      server_blocklist_(opts.server_blocklist_string),
      active_table_ids_(opts.active_table_ids),
      has_table_always_allowlist_(!opts.table_always_allowlist_string.empty()),
      table_always_allowlist_(opts.table_always_allowlist_string) {}

bool PrometheusMetricFilterV2::ShouldExportTableMetrics(
    const std::string& metric_name, const MetricEntity::AttributeMap& attributes,
    const std::string& metric_entity_type) {
  if (!active_table_ids_) {
    return true;
  }
  // Only table and tablet entities carry table-level metrics. Other entity types may carry a
  // table id too, for example xCluster series that are aggregated per stream, and must not be
  // filtered by accident.
  if (metric_entity_type != kTabletMetricEntityName &&
      metric_entity_type != kTableMetricEntityName) {
    return true;
  }
  // Server-level aggregates carry no table id and always cover every table.
  const auto table_id = attributes.find("table_id");
  if (table_id == attributes.end()) {
    return true;
  }
  // Every tablet of a table carries the same table attributes, so the per-table part of the
  // decision is computed once per table and scrape.
  auto [it, inserted] = table_passes_list_by_table_id_.try_emplace(table_id->second, false);
  if (inserted) {
    it->second = GetActiveTableListExemption(attributes) != ActiveTableListExemption::kNone ||
                 active_table_ids_->contains(table_id->second);
  }
  return it->second || IsAlwaysExported(metric_name);
}

bool PrometheusMetricFilterV2::IsAlwaysExported(const std::string& metric_name) {
  if (!has_table_always_allowlist_) {
    return false;
  }
  auto [it, inserted] = always_exported_by_metric_name_.try_emplace(metric_name, false);
  if (inserted) {
    it->second = boost::regex_match(metric_name, table_always_allowlist_);
  }
  return it->second;
}

AggregationLevels PrometheusMetricFilterV2::GetAggregationLevels(
    const std::string& metric_name, AggregationLevels default_aggregation_levels) {
  auto [levels_from_regex_it, inserted] = metric_filter_.try_emplace(metric_name, kNoLevel);
  auto& levels_from_regex = levels_from_regex_it->second;

  if (inserted) {
    // No filter for stream level.
    levels_from_regex = kStreamLevel;
    if (!boost::regex_match(metric_name, table_blocklist_) &&
        boost::regex_match(metric_name, table_allowlist_)) {
      levels_from_regex |= kTableLevel;
    }
    if (!boost::regex_match(metric_name, server_blocklist_) &&
        boost::regex_match(metric_name, server_allowlist_)) {
      levels_from_regex |= kServerLevel;
    }
  }

  return levels_from_regex & default_aggregation_levels;
}

std::string PrometheusMetricFilterV2::Version() const {
  return kFilterVersionTwo;
}

} // namespace

std::unique_ptr<PrometheusMetricFilter> CreatePrometheusMetricFilter(
    const MetricPrometheusOptions& opts) {
  DCHECK(opts.version == kFilterVersionOne || opts.version == kFilterVersionTwo);
  if (opts.version == kFilterVersionTwo) {
    return std::make_unique<PrometheusMetricFilterV2>(opts);
  }
  return std::make_unique<PrometheusMetricFilterV1>(opts);
}

}  // namespace yb
