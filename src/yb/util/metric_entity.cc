//
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
//
#include "yb/util/metric_entity.h"

#include "yb/gutil/map-util.h"
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
                           std::shared_ptr<MemTracker> mem_tracker)
    : prototype_(prototype),
      id_(std::move(id)),
      attributes_(std::move(attributes)),
      mem_tracker_(std::move(mem_tracker)) {
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

scoped_refptr<Metric> MetricEntity::FindOrNull(const MetricPrototype& prototype) const {
  std::lock_guard l(lock_);
  return FindPtrOrNull(metric_map_, &prototype);
}

bool MetricEntity::TEST_ContainMetricName(const std::string& metric_name) const {
  std::lock_guard l(lock_);
  for (const MetricMap::value_type& val : metric_map_) {
    if (val.first->name() == metric_name) {
      return true;
    }
  }
  return false;
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
  AttributeMap attrs;
  MetricMap prometheus_metrics;
  {
    // Snapshot the metrics, attributes & external metrics callbacks in this metrics entity. (Note:
    // this is not guaranteed to be a consistent snapshot).
    std::lock_guard l(lock_);
    prometheus_metrics = GetFilteredMetricMap(opts.general_metrics_allowlist);
    if (prometheus_metrics.empty()) {
      // None of the metrics are matched, or this entity has no metrics.
      return Status::OK();
    }
    attrs = attributes_;
  }

  AttributeMap prometheus_attr;
  AggregationLevels aggregation_levels;
  // Per tablet metrics come with tablet_id, as well as table_id and table_name attributes.
  // We ignore the tablet part to squash at the table level.
  if (strcmp(prototype_->name(), "tablet") == 0 || strcmp(prototype_->name(), "table") == 0) {
    prometheus_attr["table_id"] = attrs["table_id"];
    prometheus_attr["table_name"] = attrs["table_name"];
    prometheus_attr["table_type"] = attrs["table_type"];
    prometheus_attr["namespace_name"] = attrs["namespace_name"];
    aggregation_levels = kTableLevel | kServerLevel;
  } else if (
      strcmp(prototype_->name(), "server") == 0 || strcmp(prototype_->name(), "cluster") == 0) {
    prometheus_attr = attrs;
    // This is tablet_id in the case of tablet, but otherwise names the server type, eg: yb.master
    prometheus_attr["metric_id"] = id_;
    aggregation_levels = kServerLevel;
  } else if (strcmp(prototype_->name(), kCdcMetricEntityName) == 0) {
    prometheus_attr["table_id"] = attrs["table_id"];
    prometheus_attr["table_name"] = attrs["table_name"];
    prometheus_attr["table_type"] = attrs["table_type"];
    prometheus_attr["namespace_name"] = attrs["namespace_name"];
    prometheus_attr["stream_id"] = attrs["stream_id"];
    aggregation_levels = kStreamLevel;
  } else if (strcmp(prototype_->name(), kCdcsdkMetricEntityName) == 0) {
    prometheus_attr["table_id"] = attrs["table_id"];
    prometheus_attr["table_name"] = attrs["table_name"];
    prometheus_attr["namespace_name"] = attrs["namespace_name"];
    prometheus_attr["stream_id"] = attrs["stream_id"];
    aggregation_levels = kStreamLevel;
  } else if (strcmp(prototype_->name(), "drive") == 0) {
    prometheus_attr["drive_path"] = attrs["drive_path"];
    aggregation_levels = kServerLevel;
  } else {
    return Status::OK();
  }
  // This is currently tablet / table / server / cluster / cdc / cdcsdk / drive.
  prometheus_attr["metric_type"] = prototype_->name();
  prometheus_attr["exported_instance"] = FLAGS_metric_node_name;

  for (const auto& [prototype, metric]  : prometheus_metrics) {
    WARN_NOT_OK(metric->WriteForPrometheus(
        writer, prometheus_attr, opts, aggregation_levels),
        Format("Failed to write $0 as Prometheus", prototype->name()));
  }

  return Status::OK();
}

void MetricEntity::Remove(const MetricPrototype* proto) {
  std::lock_guard l(lock_);
  metric_map_.erase(proto);
}

void MetricEntity::RetireOldMetrics() {
  MonoTime now = MonoTime::Now();

  std::lock_guard l(lock_);
  for (auto it = metric_map_.begin(); it != metric_map_.end();) {
    const scoped_refptr<Metric>& metric = it->second;

    if (PREDICT_TRUE(!metric->HasOneRef())) {
      // The metric is still in use. Note that, in the case of "NeverRetire()", the metric
      // will have a ref-count of 2 because it is reffed by the 'never_retire_metrics_'
      // collection.

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
    metric_map_.erase(it++);
  }
}

void MetricEntity::NeverRetire(const scoped_refptr<Metric>& metric) {
  std::lock_guard l(lock_);
  never_retire_metrics_.push_back(metric);
}

void MetricEntity::SetAttributes(const AttributeMap& attrs) {
  std::lock_guard l(lock_);
  attributes_ = attrs;
}

void MetricEntity::SetAttribute(const string& key, const string& val) {
  std::lock_guard l(lock_);
  attributes_[key] = val;
}

void WriteRegistryAsJson(JsonWriter* writer) {
  MetricPrototypeRegistry::get()->WriteAsJson(writer);
}

void RegisterMetricPrototype(const MetricPrototype* prototype) {
  MetricPrototypeRegistry::get()->AddMetric(prototype);
}

} // namespace yb
