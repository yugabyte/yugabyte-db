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

#include "yb/util/metrics.h"

#include <map>
#include <set>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"

#include "yb/util/hdr_histogram.h"
#include "yb/util/histogram.pb.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/locks.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_bool(expose_metric_histogram_percentiles, true,
            "Should we expose the percentiles information for metrics histograms.");

DEPRECATE_FLAG(int32, max_tables_metrics_breakdowns, "08_2023"
             "The maxmimum number of tables to retrieve metrics for");

// Process/server-wide metrics should go into the 'server' entity.
// More complex applications will define other entities.
METRIC_DEFINE_entity(server);

namespace yb {

void RegisterMetricPrototype(const MetricPrototype* prototype);

using std::string;
using std::vector;
using strings::Substitute;

//
// MetricUnit
//

const char* MetricUnit::Name(Type unit) {
  switch (unit) {
    case kCacheHits:
      return "hits";
    case kCacheQueries:
      return "queries";
    case kBytes:
      return "bytes";
    case kRequests:
      return "requests";
    case kEntries:
      return "entries";
    case kRows:
      return "rows";
    case kCells:
      return "cells";
    case kConnections:
      return "connections";
    case kOperations:
      return "operations";
    case kProbes:
      return "probes";
    case kNanoseconds:
      return "nanoseconds";
    case kMicroseconds:
      return "microseconds";
    case kMilliseconds:
      return "milliseconds";
    case kSeconds:
      return "seconds";
    case kThreads:
      return "threads";
    case kTransactions:
      return "transactions";
    case kUnits:
      return "units";
    case kMaintenanceOperations:
      return "operations";
    case kBlocks:
      return "blocks";
    case kLogBlockContainers:
      return "log block containers";
    case kTasks:
      return "tasks";
    case kMessages:
      return "messages";
    case kContextSwitches:
      return "context switches";
    case kKeys:
      return "keys";
    default:
      return "UNKNOWN UNIT";
  }
}

//
// MetricType
//

const char* const MetricType::kGaugeType = "gauge";
const char* const MetricType::kCounterType = "counter";
const char* const MetricType::kHistogramType = "histogram";
const char* MetricType::Name(MetricType::Type type) {
  switch (type) {
    case kGauge:
      return kGaugeType;
    case kCounter:
      return kCounterType;
    case kHistogram:
      return kHistogramType;
    default:
      return "UNKNOWN TYPE";
  }
}

// For prometheus # TYPE.
const char* MetricType::PrometheusType(MetricType::Type type) {
  switch (type) {
    case kGauge: case kLag:
      return kGaugeType;
    case kCounter:
      return kCounterType;
    default:
      LOG(DFATAL) << Format("$0 type can't be exported to prometheus # TYPE",
          Name(type));
      return "UNKNOWN TYPE";
  }
}

namespace {

const char* MetricLevelName(MetricLevel level) {
  switch (level) {
    case MetricLevel::kDebug:
      return "debug";
    case MetricLevel::kInfo:
      return "info";
    case MetricLevel::kWarn:
      return "warn";
    default:
      return "UNKNOWN LEVEL";
  }
}

} // anonymous namespace

//
// MetricRegistry
//

MetricRegistry::MetricRegistry() {
}

MetricRegistry::~MetricRegistry() {
}

bool MetricRegistry::TabletHasBeenShutdown(const scoped_refptr<MetricEntity> entity) const {
    if (strcmp(entity->prototype_->name(), "tablet") == 0 && tablets_shutdown_find(entity->id())) {
      DVLOG(5) << "Do not report metrics for shutdown tablet " << entity->id();
      return true;
    }

    return false;
}

Status MetricRegistry::WriteAsJson(JsonWriter* writer,
                                   const MetricJsonOptions& opts) const {
  EntityMap entities;
  {
    std::lock_guard<simple_spinlock> l(lock_);
    entities = entities_;
  }

  writer->StartArray();
  for (const EntityMap::value_type& e : entities) {
    if (TabletHasBeenShutdown(e.second)) {
      continue;
    }

    WARN_NOT_OK(e.second->WriteAsJson(writer, opts),
        Substitute("Failed to write entity $0 as JSON", e.second->id()));
  }
  writer->EndArray();

  // Rather than having a thread poll metrics periodically to retire old ones,
  // we'll just retire them here. The only downside is that, if no one is polling
  // metrics, we may end up leaving them around indefinitely; however, metrics are
  // small, and one might consider it a feature: if monitoring stops polling for
  // metrics, we should keep them around until the next poll.
  entities.clear(); // necessary to deref metrics we just dumped before doing retirement scan.
  const_cast<MetricRegistry*>(this)->RetireOldMetrics();
  return Status::OK();
}

Status MetricRegistry::WriteForPrometheus(PrometheusWriter* writer,
                                          const MetricPrometheusOptions& opts) const {
  EntityMap entities;
  {
    std::lock_guard<simple_spinlock> l(lock_);
    entities = entities_;
  }

  for (const EntityMap::value_type& e : entities) {
    if (TabletHasBeenShutdown(e.second)) {
      continue;
    }

    WARN_NOT_OK(e.second->WriteForPrometheus(writer, opts),
                Substitute("Failed to write entity $0 as Prometheus", e.second->id()));
  }

  RETURN_NOT_OK(writer->FlushAggregatedValues());

  // Rather than having a thread poll metrics periodically to retire old ones,
  // we'll just retire them here. The only downside is that, if no one is polling
  // metrics, we may end up leaving them around indefinitely; however, metrics are
  // small, and one might consider it a feature: if monitoring stops polling for
  // metrics, we should keep them around until the next poll.
  entities.clear(); // necessary to deref metrics we just dumped before doing retirement scan.
  const_cast<MetricRegistry*>(this)->RetireOldMetrics();
  return Status::OK();
}

void MetricRegistry::get_all_prototypes(std::set<std::string>& prototypes) const {
  EntityMap entities;
  {
    std::lock_guard<simple_spinlock> l(lock_);
    entities = entities_;
  }
  for (const EntityMap::value_type& e : entities) {
    prototypes.insert(e.second->prototype().name());
  }
}

void MetricRegistry::RetireOldMetrics() {
  std::lock_guard<simple_spinlock> l(lock_);
  for (auto it = entities_.begin(); it != entities_.end();) {
    it->second->RetireOldMetrics();

    if (it->second->num_metrics() == 0 && it->second->HasOneRef()) {
      // No metrics and no external references to this entity, so we can retire it.
      // Unlike retiring the metrics themselves, we don't wait for any timeout
      // to retire them -- we assume that that timed retention has been satisfied
      // by holding onto the metrics inside the entity.

      // For a tablet that has been shutdown, metrics are being deleted. So do not track
      // the tablet anymore.
      if (strcmp(it->second->prototype_->name(), "tablet") == 0) {
        DVLOG(3) << "T " << it->first << ": "
          << "Remove from set of tablets that have been shutdown so as to be freed";
        tablets_shutdown_erase(it->first);
      }

      entities_.erase(it++);
    } else {
      ++it;
    }
  }
}

//
// MetricPrototype
//
MetricPrototype::MetricPrototype(CtorArgs args) : args_(std::move(args)) {
  RegisterMetricPrototype(this);
}

void MetricPrototype::WriteFields(JsonWriter* writer,
                                  const MetricJsonOptions& opts) const {
  writer->String("name");
  writer->String(name());

  if (opts.include_schema_info) {
    writer->String("label");
    writer->String(label());

    writer->String("type");
    writer->String(MetricType::Name(type()));

    writer->String("unit");
    writer->String(MetricUnit::Name(unit()));

    writer->String("description");
    writer->String(description());

    writer->String("level");
    writer->String(MetricLevelName(level()));
  }
}

//
// FunctionGaugeDetacher
//

scoped_refptr<MetricEntity> MetricRegistry::FindOrCreateEntity(
    const MetricEntityPrototype* prototype,
    const std::string& id,
    const MetricEntity::AttributeMap& initial_attributes) {
  std::lock_guard<simple_spinlock> l(lock_);
  scoped_refptr<MetricEntity> e = FindPtrOrNull(entities_, id);
  if (!e) {
    e = new MetricEntity(prototype, id, initial_attributes);
    InsertOrDie(&entities_, id, e);
  } else {
    e->SetAttributes(initial_attributes);
  }
  return e;
}

//
// Metric
//
Metric::Metric(const MetricPrototype* prototype)
  : prototype_(prototype) {
}

Metric::Metric(std::unique_ptr<MetricPrototype> prototype)
  : prototype_holder_(std::move(prototype)), prototype_(prototype_holder_.get()) {
}

Metric::~Metric() {
}

//
// Gauge
//

Status Gauge::WriteAsJson(JsonWriter* writer,
                          const MetricJsonOptions& opts) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  writer->StartObject();

  prototype_->WriteFields(writer, opts);

  writer->String("value");
  WriteValue(writer);

  writer->EndObject();
  return Status::OK();
}

//
// StringGauge
//

StringGauge::StringGauge(const GaugePrototype<string>* proto,
                         string initial_value)
    : Gauge(proto), value_(std::move(initial_value)) {}

std::string StringGauge::value() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return value_;
}

void StringGauge::set_value(const std::string& value) {
  std::lock_guard<simple_spinlock> l(lock_);
  value_ = value;
}

void StringGauge::WriteValue(JsonWriter* writer) const {
  writer->String(value());
}

Status StringGauge::WriteForPrometheus(
    PrometheusWriter* writer,
    const MetricEntity::AttributeMap& attr,
    const MetricPrometheusOptions& opts,
    const AggregationLevels aggregation_levels) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  // TODO(bogdan): don't think we need this?
  // return writer->WriteSingleEntry(attr, prototype_->name(), value());
  return Status::OK();
}

//
// Counter
//
// This implementation is optimized by using a striped counter. See LongAdder for details.

scoped_refptr<Counter> CounterPrototype::Instantiate(
    const scoped_refptr<MetricEntity>& entity) const {
  return entity->FindOrCreateCounter(this);
}

Counter::Counter(const CounterPrototype* proto) : Metric(proto) {
}

Counter::Counter(std::unique_ptr<CounterPrototype> proto) : Metric(std::move(proto)) {
}

int64_t Counter::value() const {
  return value_.Value();
}

void Counter::Increment() {
  IncrementBy(1);
}

void Counter::IncrementBy(int64_t amount) {
  value_.IncrementBy(amount);
}

Status Counter::WriteAsJson(JsonWriter* writer,
                            const MetricJsonOptions& opts) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  writer->StartObject();

  prototype_->WriteFields(writer, opts);

  writer->String("value");
  writer->Int64(value());

  writer->EndObject();
  return Status::OK();
}

Status Counter::WriteForPrometheus(
    PrometheusWriter* writer,
    const MetricEntity::AttributeMap& attr,
    const MetricPrometheusOptions& opts,
    const AggregationLevels aggregation_levels) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  return writer->WriteSingleEntry(attr, prototype_->name(), value(),
                                  prototype()->aggregation_function(),
                                  aggregation_levels,
                                  MetricType::PrometheusType(prototype_->type()),
                                  prototype_->description());
}

//
// MillisLag
//

scoped_refptr<MillisLag> MillisLagPrototype::Instantiate(
    const scoped_refptr<MetricEntity>& entity) const {
  return entity->FindOrCreateMillisLag(this);
}

MillisLag::MillisLag(const MillisLagPrototype* proto)
  : Metric(proto),
    timestamp_ms_(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count())) {
}

Status MillisLag::WriteAsJson(JsonWriter* writer, const MetricJsonOptions& opts) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  writer->StartObject();

  prototype_->WriteFields(writer, opts);

  writer->String("value");
  writer->Int64(lag_ms());

  writer->EndObject();
  return Status::OK();
}

Status MillisLag::WriteForPrometheus(
    PrometheusWriter* writer,
    const MetricEntity::AttributeMap& attr,
    const MetricPrometheusOptions& opts,
    const AggregationLevels aggregation_levels) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  return writer->WriteSingleEntry(attr, prototype_->name(), lag_ms(),
                                  prototype()->aggregation_function(),
                                  aggregation_levels,
                                  MetricType::PrometheusType(prototype_->type()),
                                  prototype_->description());
}

AtomicMillisLag::AtomicMillisLag(const MillisLagPrototype* proto)
  : MillisLag(proto),
    atomic_timestamp_ms_(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count())) {
}

Status AtomicMillisLag::WriteAsJson(JsonWriter* writer, const MetricJsonOptions& opts) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  writer->StartObject();

  prototype_->WriteFields(writer, opts);

  writer->String("value");
  writer->Int64(this->lag_ms());

  writer->EndObject();
  return Status::OK();
}

/////////////////////////////////////////////////
// HistogramPrototype
/////////////////////////////////////////////////

HistogramPrototype::HistogramPrototype(const MetricPrototype::CtorArgs& args,
                                       uint64_t max_trackable_value, int num_sig_digits,
                                       ExportPercentiles export_percentiles)
  : MetricPrototype(args),
    max_trackable_value_(max_trackable_value),
    num_sig_digits_(num_sig_digits),
    export_percentiles_(export_percentiles) {
  // Better to crash at definition time that at instantiation time.
  CHECK(HdrHistogram::IsValidHighestTrackableValue(max_trackable_value))
      << Substitute("Invalid max trackable value on histogram $0: $1",
                    args.name_, max_trackable_value);
  CHECK(HdrHistogram::IsValidNumSignificantDigits(num_sig_digits))
      << Substitute("Invalid number of significant digits on histogram $0: $1",
                    args.name_, num_sig_digits);
}

scoped_refptr<Histogram> HistogramPrototype::Instantiate(
    const scoped_refptr<MetricEntity>& entity) const {
  return entity->FindOrCreateHistogram(this);
}

/////////////////////////////////////////////////
// Histogram
/////////////////////////////////////////////////

Histogram::Histogram(const HistogramPrototype* proto)
  : Metric(proto),
    histogram_(new HdrHistogram(proto->max_trackable_value(), proto->num_sig_digits())),
    export_percentiles_(proto->export_percentiles()) {
}

Histogram::Histogram(
  std::unique_ptr <HistogramPrototype> proto,  uint64_t highest_trackable_value,
  int num_significant_digits, ExportPercentiles export_percentiles)
  : Metric(std::move(proto)),
    histogram_(new HdrHistogram(highest_trackable_value, num_significant_digits)),
    export_percentiles_(export_percentiles) {
}

void Histogram::Increment(int64_t value) {
  histogram_->Increment(value);
}

void Histogram::IncrementBy(int64_t value, int64_t amount) {
  histogram_->IncrementBy(value, amount);
}

Status Histogram::WriteAsJson(JsonWriter* writer,
                              const MetricJsonOptions& opts) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  HistogramSnapshotPB snapshot;
  RETURN_NOT_OK(GetAndResetHistogramSnapshotPB(&snapshot, opts));
  writer->Protobuf(snapshot);
  return Status::OK();
}

Status Histogram::WriteForPrometheus(
    PrometheusWriter* writer,
    const MetricEntity::AttributeMap& attr,
    const MetricPrometheusOptions& opts,
    const AggregationLevels aggregation_levels) const {
  if (prototype_->level() < opts.level) {
    return Status::OK();
  }

  HdrHistogram snapshot(*histogram_);
  // HdrHistogram reports percentiles based on all the data points from the
  // begining of time. We are interested in the percentiles based on just
  // the "newly-arrived" data. So, in the defualt setting, we will reset
  // the histogram's percentiles between each invocation. User also has the
  // option to set the url parameter reset_histograms=false
  if (opts.reset_histograms) {
    histogram_->ResetPercentiles();
  }

  // Representing the sum and count require suffixed names.
  std::string hist_name = prototype_->name();
  const char* description = prototype_->description();
  const char* counter_type = MetricType::PrometheusType(MetricType::kCounter);
  auto copy_of_attr = attr;
  // For #HELP and #TYPE, we need to print them for each entry, since our
  // histogram doesn't really get exported as histograms.
  RETURN_NOT_OK(writer->WriteSingleEntry(
        copy_of_attr, hist_name + "_sum", snapshot.TotalSum(),
        prototype()->aggregation_function(), aggregation_levels, counter_type, description));
  RETURN_NOT_OK(writer->WriteSingleEntry(
        copy_of_attr, hist_name + "_count", snapshot.TotalCount(),
        prototype()->aggregation_function(), aggregation_levels, counter_type, description));


  // Copy the label map to add the quatiles.
  if (export_percentiles_ && FLAGS_expose_metric_histogram_percentiles) {
    const char* gauge_type = MetricType::PrometheusType(MetricType::kGauge);
    copy_of_attr["quantile"] = "p50";
    RETURN_NOT_OK(writer->WriteSingleEntry(copy_of_attr, hist_name,
                                           snapshot.ValueAtPercentile(50),
                                           prototype()->aggregation_function(),
                                           aggregation_levels,
                                           gauge_type, description));
    copy_of_attr["quantile"] = "p95";
    RETURN_NOT_OK(writer->WriteSingleEntry(copy_of_attr, hist_name,
                                           snapshot.ValueAtPercentile(95),
                                           prototype()->aggregation_function(),
                                           aggregation_levels,
                                           gauge_type, description));
    copy_of_attr["quantile"] = "p99";
    RETURN_NOT_OK(writer->WriteSingleEntry(copy_of_attr, hist_name,
                                           snapshot.ValueAtPercentile(99),
                                           prototype()->aggregation_function(),
                                           aggregation_levels,
                                           gauge_type, description));
    copy_of_attr["quantile"] = "mean";
    RETURN_NOT_OK(writer->WriteSingleEntry(copy_of_attr, hist_name,
                                           snapshot.MeanValue(),
                                           prototype()->aggregation_function(),
                                           aggregation_levels,
                                           gauge_type, description));
    copy_of_attr["quantile"] = "max";
    RETURN_NOT_OK(writer->WriteSingleEntry(copy_of_attr, hist_name,
                                           snapshot.MaxValue(),
                                           prototype()->aggregation_function(),
                                           aggregation_levels,
                                           gauge_type, description));
  }
  return Status::OK();
}

Status Histogram::GetAndResetHistogramSnapshotPB(HistogramSnapshotPB* snapshot_pb,
                                                 const MetricJsonOptions& opts) const {
  HdrHistogram snapshot(*histogram_);
  // HdrHistogram reports percentiles based on all the data points from the
  // begining of time. We are interested in the percentiles based on just
  // the "newly-arrived" data. So, in the defualt setting, we will reset
  // the histogram's percentiles between each invocation. User also has the
  // option to set the url parameter reset_histograms=false
  if (opts.reset_histograms) {
    histogram_->ResetPercentiles();
  }

  snapshot_pb->set_name(prototype_->name());
  if (opts.include_schema_info) {
    snapshot_pb->set_type(MetricType::Name(prototype_->type()));
    snapshot_pb->set_label(prototype_->label());
    snapshot_pb->set_unit(MetricUnit::Name(prototype_->unit()));
    snapshot_pb->set_description(prototype_->description());
    snapshot_pb->set_level(MetricLevelName(prototype_->level()));
    snapshot_pb->set_max_trackable_value(snapshot.highest_trackable_value());
    snapshot_pb->set_num_significant_digits(snapshot.num_significant_digits());
  }
  snapshot_pb->set_total_count(snapshot.TotalCount());
  snapshot_pb->set_total_sum(snapshot.TotalSum());
  snapshot_pb->set_min(snapshot.MinValue());
  snapshot_pb->set_mean(snapshot.MeanValue());
  snapshot_pb->set_percentile_75(snapshot.ValueAtPercentile(75));
  snapshot_pb->set_percentile_95(snapshot.ValueAtPercentile(95));
  snapshot_pb->set_percentile_99(snapshot.ValueAtPercentile(99));
  snapshot_pb->set_percentile_99_9(snapshot.ValueAtPercentile(99.9));
  snapshot_pb->set_percentile_99_99(snapshot.ValueAtPercentile(99.99));
  snapshot_pb->set_max(snapshot.MaxValue());

  if (opts.include_raw_histograms) {
    RecordedValuesIterator iter(&snapshot);
    while (iter.HasNext()) {
      HistogramIterationValue value;
      RETURN_NOT_OK(iter.Next(&value));
      snapshot_pb->add_values(value.value_iterated_to);
      snapshot_pb->add_counts(value.count_at_value_iterated_to);
    }
  }
  return Status::OK();
}

uint64_t Histogram::CountInBucketForValueForTests(uint64_t value) const {
  return histogram_->CountInBucketForValue(value);
}

uint64_t Histogram::TotalCount() const {
  return histogram_->TotalCount();
}

uint64_t Histogram::MinValueForTests() const {
  return histogram_->MinValue();
}

uint64_t Histogram::MaxValueForTests() const {
  return histogram_->MaxValue();
}
double Histogram::MeanValueForTests() const {
  return histogram_->MeanValue();
}

ScopedLatencyMetric::ScopedLatencyMetric(
    const scoped_refptr<Histogram>& latency_hist, Auto automatic)
    : latency_hist_(latency_hist), auto_(automatic) {
  Restart();
}

ScopedLatencyMetric::ScopedLatencyMetric(ScopedLatencyMetric&& rhs)
    : latency_hist_(std::move(rhs.latency_hist_)), time_started_(rhs.time_started_),
      auto_(rhs.auto_) {
}

void ScopedLatencyMetric::operator=(ScopedLatencyMetric&& rhs) {
  if (auto_) {
    Finish();
  }

  latency_hist_ = std::move(rhs.latency_hist_);
  time_started_ = rhs.time_started_;
  auto_ = rhs.auto_;
}

ScopedLatencyMetric::~ScopedLatencyMetric() {
  if (auto_) {
    Finish();
  }
}

void ScopedLatencyMetric::Restart() {
  if (latency_hist_) {
    time_started_ = MonoTime::Now();
  }
}

void ScopedLatencyMetric::Finish() {
  if (latency_hist_ != nullptr) {
    auto passed = (MonoTime::Now() - time_started_).ToMicroseconds();
    latency_hist_->Increment(passed);
  }
}

// Replace specific chars with underscore to pass PrometheusNameRegex().
void EscapeMetricNameForPrometheus(std::string *id) {
  std::replace(id->begin(), id->end(), ' ', '_');
  std::replace(id->begin(), id->end(), '.', '_');
  std::replace(id->begin(), id->end(), '-', '_');
}

} // namespace yb
