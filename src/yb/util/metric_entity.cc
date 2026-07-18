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
#include "yb/util/metric_entity.h"

#include <boost/regex.hpp>

#include "yb/gutil/map-util.h"

#include "yb/util/debug.h"
#include "yb/util/flags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"

using std::string;
using std::vector;

DEFINE_RUNTIME_int32(metrics_retirement_age_ms, 120 * 1000,
    "The minimum number of milliseconds a metric will be kept for after it is "
    "no longer active. (Advanced option)");
TAG_FLAG(metrics_retirement_age_ms, advanced);

// TODO: changed to empty string and add logic to get this from cluster_uuid in case empty.
DEFINE_UNKNOWN_string(metric_node_name, "DEFAULT_NODE_NAME",
              "Value to use as node name for metrics reporting");

namespace yb {

namespace {

const boost::regex prometheus_name_regex("[a-zA-Z_:][a-zA-Z0-9_:]*");
// Registry of all of the metric and entity prototypes that have been
// defined.
//
// Prototypes are typically defined as static variables in different compilation
// units, and their constructors register themselves here. The registry is then
// used in order to dump metrics metadata to generate a Cloudera Manager MDL
// file.
//
// This class is thread-safe.
class MetricPrototypeRegistry {
 public:
  // Get the singleton instance.
  static MetricPrototypeRegistry* get();

  // Dump a JSON document including all of the registered entity and metric
  // prototypes.
  void WriteAsJson(JsonWriter* writer) const;

  // Register a metric prototype in the registry.
  void AddMetric(const MetricPrototype* prototype);

  // Register a metric entity prototype in the registry.
  void AddEntity(const MetricEntityPrototype* prototype);

 private:
  MetricPrototypeRegistry() {}
  ~MetricPrototypeRegistry() {}

  mutable simple_spinlock lock_;
  std::vector<const MetricPrototype*> metrics_;
  std::vector<const MetricEntityPrototype*> entities_;

  DISALLOW_COPY_AND_ASSIGN(MetricPrototypeRegistry);
};

//
// MetricPrototypeRegistry
//
MetricPrototypeRegistry* MetricPrototypeRegistry::get() {
  static MetricPrototypeRegistry instance;
  return &instance;
}

void MetricPrototypeRegistry::AddMetric(const MetricPrototype* prototype) {
  std::lock_guard l(lock_);
  metrics_.push_back(prototype);
}

void MetricPrototypeRegistry::AddEntity(const MetricEntityPrototype* prototype) {
  std::lock_guard l(lock_);
  entities_.push_back(prototype);
}

void MetricPrototypeRegistry::WriteAsJson(JsonWriter* writer) const {
  std::lock_guard l(lock_);
  MetricJsonOptions opts;
  opts.include_schema_info = true;
  writer->StartObject();

  // Dump metric prototypes.
  writer->String("metrics");
  writer->StartArray();
  for (const MetricPrototype* p : metrics_) {
    writer->StartObject();
    p->WriteFields(writer, opts);
    writer->String("entity_type");
    writer->String(p->entity_type());
    writer->EndObject();
  }
  writer->EndArray();

  // Dump entity prototypes.
  writer->String("entities");
  writer->StartArray();
  for (const MetricEntityPrototype* p : entities_) {
    writer->StartObject();
    writer->String("name");
    writer->String(p->name());
    writer->EndObject();
  }
  writer->EndArray();

  writer->EndObject();
}

} // namespace

//
// MetricEntityPrototype
//

MetricEntityPrototype::MetricEntityPrototype(const char* name)
  : name_(name) {
  MetricPrototypeRegistry::get()->AddEntity(this);
}

MetricEntityPrototype::~MetricEntityPrototype() {
}

scoped_refptr<MetricEntity> MetricEntityPrototype::Instantiate(
    MetricRegistry* registry,
    const std::string& id,
    const MetricEntity::AttributeMap& initial_attrs,
    std::shared_ptr<MemTracker> mem_tracker) const {
  return registry->FindOrCreateEntity(this, id, initial_attrs, std::move(mem_tracker));
}

scoped_refptr<MetricEntity> MetricEntityPrototype::Instantiate(
    MetricRegistry* registry, const std::string& id) const {
  return Instantiate(registry, id, std::unordered_map<std::string, std::string>());
}

//
// MetricEntity
//

MetricEntity::MetricEntity(const MetricEntityPrototype* prototype,
                           std::string id, AttributeMap attributes,
                           MetricsAggregator* metrics_aggregator,
                           std::shared_ptr<MemTracker> mem_tracker)
    : prototype_(prototype),
      id_(std::move(id)),
      attributes_(std::move(attributes)),
      metrics_aggregator_(metrics_aggregator),
      mem_tracker_(std::move(mem_tracker)),
      default_aggregation_levels_(ReconstructPrometheusAttributes()) {}

AggregationLevels MetricEntity::ReconstructPrometheusAttributes() {
  std::lock_guard l(lock_);
  return ReconstructPrometheusAttributesUnlocked();
}

std::string EscapePrometheusLabelValue(const std::string& s) {
  if (s.find_first_of("\\\"\n") == std::string::npos) {
    return s;
  }

  std::string result;
  result.reserve(s.size() * 2);
  for (char c : s) {
    if (c == '\n') {
      result += "\\n";
      continue;
    }
    if (c == '\\' || c == '"') {
      result += '\\';
    }
    result += c;
  }
  return result;
}

AggregationLevels MetricEntity::ReconstructPrometheusAttributesUnlocked() {
  auto default_aggregation_level = kNoLevel;

  const std::string& prototype_type = prototype_->name();
  prometheus_attributes_.clear();
  if (prototype_type == "tablet" || prototype_type == "table") {
    aggregation_id_for_pre_aggregation_ = attributes_["table_id"];
    prometheus_attributes_["table_id"] = aggregation_id_for_pre_aggregation_;
    prometheus_attributes_["table_name"] = EscapePrometheusLabelValue(attributes_["table_name"]);
    prometheus_attributes_["table_type"] = attributes_["table_type"];
    prometheus_attributes_["namespace_name"] =
        EscapePrometheusLabelValue(attributes_["namespace_name"]);
    default_aggregation_level = kTableLevel | kServerLevel;
  } else if (prototype_type == "server" || prototype_type == "cluster") {
    prometheus_attributes_ = attributes_;
    prometheus_attributes_["metric_id"] = id_;
    default_aggregation_level = kServerLevel;
  } else if (prototype_type == kXClusterMetricEntityName) {
    aggregation_id_for_pre_aggregation_ = attributes_["stream_id"];
    prometheus_attributes_["table_id"] = attributes_["table_id"];
    prometheus_attributes_["table_name"] = EscapePrometheusLabelValue(attributes_["table_name"]);
    prometheus_attributes_["table_type"] = attributes_["table_type"];
    prometheus_attributes_["namespace_name"] =
        EscapePrometheusLabelValue(attributes_["namespace_name"]);
    prometheus_attributes_["stream_id"] = aggregation_id_for_pre_aggregation_;
    default_aggregation_level = kStreamLevel;
  } else if (prototype_type == kCdcsdkMetricEntityName) {
    aggregation_id_for_pre_aggregation_ = attributes_["stream_id"];
    prometheus_attributes_["namespace_name"] =
        EscapePrometheusLabelValue(attributes_["namespace_name"]);
    prometheus_attributes_["stream_id"] = aggregation_id_for_pre_aggregation_;
    auto it = attributes_.find("slot_name");
    if (it != attributes_.end() && !it->second.empty()) {
      prometheus_attributes_["slot_name"] = it->second;
    }
    default_aggregation_level = kStreamLevel;
  } else if (prototype_type == "drive") {
    prometheus_attributes_["drive_path"] = attributes_["drive_path"];
    default_aggregation_level = kServerLevel;
  }

  prometheus_attributes_["metric_type"] = prototype_type;
  prometheus_attributes_["exported_instance"] = FLAGS_metric_node_name;

  return default_aggregation_level;
}

MetricEntity::~MetricEntity() = default;

const boost::regex& PrometheusNameRegex() {
  return prometheus_name_regex;
}

void MetricEntity::CheckInstantiation(const MetricPrototype* proto) const {
  CHECK_STREQ(prototype_->name(), proto->entity_type())
    << "Metric " << proto->name() << " may not be instantiated entity of type "
    << prototype_->name() << " (expected: " << proto->entity_type() << ")";
  DCHECK(regex_match(proto->name(), PrometheusNameRegex()))
      << "Metric name is not compatible with Prometheus: " << proto->name();
}

bool MetricEntity::TEST_ContainsMetricName(const std::string& metric_name) const {
  std::lock_guard l(lock_);
  for (const MetricMap::value_type& val : metric_map_) {
    if (val.first->name() == metric_name) {
      return true;
    }
  }
  return false;
}

Result<std::string> MetricEntity::TEST_GetAttributeFromMap(const std::string& key) const {
  std::lock_guard l(lock_);
  auto it = attributes_.find(key);
  if (it == attributes_.end()) {
    return STATUS_FORMAT(NotFound, "Key $0 not found in attributes_ map", key);
  }
  return it->second;
}

MetricEntity::MetricMap MetricEntity::GetFilteredMetricMap(
    const std::optional<std::vector<std::string>>& required_metric_substrings) const {
  if (!required_metric_substrings) {
    // Select all if filter is not provided:
    return metric_map_;
  }

  MetricMap output_metric_map;
  if (required_metric_substrings->empty()) {
    return output_metric_map;
  }

  for (const auto& [prototype, metric] : metric_map_) {
    for (const auto& required_metric_substring : *required_metric_substrings) {
      const std::string& metric_name = prototype->name();
      // Collect the metric if metric name substring is found.
      if (metric_name.find(required_metric_substring) != std::string::npos) {
        output_metric_map[prototype] = metric;
        break;
      }
    }
  }

  return output_metric_map;
}

Status MetricEntity::WriteAsJson(JsonWriter* writer,
                                 const MetricJsonOptions& opts) const {
  MetricMap json_metrics;
  AttributeMap attrs;
  {
    // Snapshot the metrics, attributes & external metrics callbacks in this metrics entity. (Note:
    // this is not guaranteed to be a consistent snapshot).
    std::lock_guard l(lock_);
    json_metrics = GetFilteredMetricMap(opts.general_metrics_allowlist);
    if (json_metrics.empty()) {
      // None of the metrics are matched, or this entity has no metrics.
      return Status::OK();
    }
    attrs = attributes_;
  }

  writer->StartObject();

  writer->String("type");
  writer->String(prototype_->name());

  writer->String("id");
  writer->String(id_);

  writer->String("attributes");
  writer->StartObject();
  for (const AttributeMap::value_type& val : attrs) {
    writer->String(val.first);
    writer->String(val.second);
  }
  writer->EndObject();

  writer->String("metrics");
  writer->StartArray();
  for (const auto& [prototype, metric] : json_metrics) {
      WARN_NOT_OK(metric->WriteAsJson(writer, opts),
          Format("Failed to write $0 as JSON", prototype->name()));
  }

  writer->EndArray();

  writer->EndObject();

  return Status::OK();
}

Status MetricEntity::WriteForPrometheus(PrometheusWriter* writer,
                                        const MetricPrometheusOptions& opts) {
  AttributeMap prometheus_attributes;
  std::shared_ptr<const NonPreAggregatedMetrics> non_pre_aggregated_metrics;
  {
    std::lock_guard lock(lock_);
    RebuildNonPreAggregatedMetricsIfNeeded();
    non_pre_aggregated_metrics = non_pre_aggregated_metrics_;
    prometheus_attributes = prometheus_attributes_;
  }

  for (const auto& metric : *non_pre_aggregated_metrics) {
    WARN_NOT_OK(metric->WriteForPrometheus(
        writer, prometheus_attributes, opts, default_aggregation_levels_),
        Format("Failed to write $0 as Prometheus", metric->prototype()->name()));
  }

  return Status::OK();
}

void MetricEntity::RebuildNonPreAggregatedMetricsIfNeeded() {
  if (!need_rebuild_non_pre_aggregated_metrics_) {
    return;
  }
  auto new_non_pre_aggregated_metrics = std::make_shared<NonPreAggregatedMetrics>();
  for (const auto& [prototype, metric] : metric_map_) {
    if (!metric->IsPreAggregated()) {
      new_non_pre_aggregated_metrics->push_back(metric);
    }
  }
  // Cast to a const vector to enforce immutability
  non_pre_aggregated_metrics_ = std::const_pointer_cast<const NonPreAggregatedMetrics>(
      new_non_pre_aggregated_metrics);
  need_rebuild_non_pre_aggregated_metrics_ = false;
}

void MetricEntity::RemoveFromMetricMap(const MetricPrototype* proto) {
  std::lock_guard l(lock_);
  auto it = metric_map_.find(proto);
  if (it != metric_map_.end()) {
    if (!it->second->IsPreAggregated()) {
      need_rebuild_non_pre_aggregated_metrics_ = true;
    }
    metric_map_.erase(it);
  }
}

void MetricEntity::RetireOldMetrics() {
  MonoTime now = MonoTime::Now();

  std::lock_guard l(lock_);
  RebuildNonPreAggregatedMetricsIfNeeded();
  for (auto it = metric_map_.begin(); it != metric_map_.end();) {
    const scoped_refptr<Metric>& metric = it->second;

    // A metric is considered unused if it has only one reference held by 'metric_map_',
    // or two references and is non-pre-aggregated, meaning it is held by both 'metric_map_' and
    // 'non_pre_aggregated_metrics_'.  Note that, in the case of "NeverRetire()", the metric
    // will have one extra ref-count because it is reffed by the 'never_retire_metrics_' collection.
    bool metric_not_in_used =
        metric->HasOneRef() || (metric->HasTwoRef() && !metric->IsPreAggregated());
    if (PREDICT_TRUE(!metric_not_in_used)) {
      // Ensure that it is not marked for later retirement (this could happen in the case
      // that a metric is un-reffed and then re-reffed later by looking it up from the
      // registry).
      metric->retire_time_ = MonoTime();
      ++it;
      continue;
    }

    if (!metric->retire_time_.Initialized()) {
      VLOG(3) << "Metric " << it->first << " has become un-referenced. Will retire after "
              << "the retention interval";
      // This is the first time we've seen this metric as retirable.
      metric->retire_time_ = now;
      metric->retire_time_.AddDelta(MonoDelta::FromMilliseconds(FLAGS_metrics_retirement_age_ms));
      ++it;
      continue;
    }

    // If we've already seen this metric in a previous scan, check if it's
    // time to retire it yet.
    if (now.ComesBefore(metric->retire_time_)) {
      VLOG(3) << "Metric " << it->first << " is un-referenced, but still within "
              << "the retention interval";
      ++it;
      continue;
    }

    VLOG(2) << "Retiring metric " << it->first;
    if (!metric->IsPreAggregated()) {
      need_rebuild_non_pre_aggregated_metrics_ = true;
    }
    metric_map_.erase(it++);
  }
}

void MetricEntity::NeverRetire(const scoped_refptr<Metric>& metric) {
  std::lock_guard l(lock_);
  never_retire_metrics_.push_back(metric);
}

std::string MetricEntity::LogPrefix() const {
    return strings::Substitute("$0 Metric entity [$1]: ", prototype_->name(), id_);
}

void MetricEntity::SetAttributes(const AttributeMap& attrs) {
  std::lock_guard l(lock_);
  attributes_ = attrs;
  ReconstructPrometheusAttributesUnlocked();
  auto s = metrics_aggregator_->ReplaceAttributes(
      prototype_->name(), aggregation_id_for_pre_aggregation_, prometheus_attributes_);
  LOG_IF_WITH_PREFIX_AND_FUNC(WARNING, !s.ok() && !s.IsNotFound())
      <<"Failed to replace prometheus attributes with error: " << s;
}

void MetricEntity::SetAttribute(const string& key, const string& val) {
  std::lock_guard l(lock_);
  attributes_[key] = val;
  ReconstructPrometheusAttributesUnlocked();
  auto s = metrics_aggregator_->ReplaceAttributes(
      prototype_->name(), aggregation_id_for_pre_aggregation_, prometheus_attributes_);
  LOG_IF_WITH_PREFIX_AND_FUNC(WARNING, !s.ok() && !s.IsNotFound())
      <<"Failed to replace prometheus attributes with error: " << s;
}

void WriteRegistryAsJson(JsonWriter* writer) {
  MetricPrototypeRegistry::get()->WriteAsJson(writer);
}

void RegisterMetricPrototype(const MetricPrototype* prototype) {
  MetricPrototypeRegistry::get()->AddMetric(prototype);
}

void ConvertToServerLevelAttributes(MetricEntity::AttributeMap* non_server_level_attributes) {
  non_server_level_attributes->erase("table_id");
  non_server_level_attributes->erase("table_name");
  non_server_level_attributes->erase("table_type");
  non_server_level_attributes->erase("namespace_name");
}

void AssertAttributesMatchExpectation(
    const std::string& metric_name,
    const std::string& aggregation_id,
    const MetricEntity::AttributeMap& actual_attributes,
    MetricEntity::AttributeMap expected_attributes) {
  if (aggregation_id == kServerLevelAggregationId) {
    ConvertToServerLevelAttributes(&expected_attributes);
  }

  LOG_IF(DFATAL, actual_attributes != expected_attributes)
      << "Attribute match failed for metric " << metric_name << " with aggregation id "
      << aggregation_id << ". Expected: " << AsString(expected_attributes) << ", Actual: "
      << AsString(actual_attributes);
}

} // namespace yb
