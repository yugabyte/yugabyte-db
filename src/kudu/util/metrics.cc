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
#include "kudu/util/metrics.h"

#include <iostream>
#include <map>
#include <set>

#include <gflags/gflags.h>

#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/casts.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/singleton.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/hdr_histogram.h"
#include "kudu/util/histogram.pb.h"
#include "kudu/util/jsonwriter.h"
#include "kudu/util/locks.h"
#include "kudu/util/status.h"

DEFINE_int32(metrics_retirement_age_ms, 120 * 1000,
             "The minimum number of milliseconds a metric will be kept for after it is "
             "no longer active. (Advanced option)");
TAG_FLAG(metrics_retirement_age_ms, runtime);
TAG_FLAG(metrics_retirement_age_ms, advanced);

// Process/server-wide metrics should go into the 'server' entity.
// More complex applications will define other entities.
METRIC_DEFINE_entity(server);

namespace kudu {

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
    case kScanners:
      return "scanners";
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
    const MetricEntity::AttributeMap& initial_attrs) const {
  return registry->FindOrCreateEntity(this, id, initial_attrs);
}


//
// MetricEntity
//

MetricEntity::MetricEntity(const MetricEntityPrototype* prototype,
                           std::string id, AttributeMap attributes)
    : prototype_(prototype),
      id_(std::move(id)),
      attributes_(std::move(attributes)) {}

MetricEntity::~MetricEntity() {
}

void MetricEntity::CheckInstantiation(const MetricPrototype* proto) const {
  CHECK_STREQ(prototype_->name(), proto->entity_type())
    << "Metric " << proto->name() << " may not be instantiated entity of type "
    << prototype_->name() << " (expected: " << proto->entity_type() << ")";
}

scoped_refptr<Metric> MetricEntity::FindOrNull(const MetricPrototype& prototype) const {
  lock_guard<simple_spinlock> l(&lock_);
  return FindPtrOrNull(metric_map_, &prototype);
}

namespace {

bool MatchMetricInList(const string& metric_name,
                       const vector<string>& match_params) {
  for (const string& param : match_params) {
    // Handle wildcard.
    if (param == "*") return true;
    // The parameter is a substring match of the metric name.
    if (metric_name.find(param) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // anonymous namespace


Status MetricEntity::WriteAsJson(JsonWriter* writer,
                                 const vector<string>& requested_metrics,
                                 const MetricJsonOptions& opts) const {
  bool select_all = MatchMetricInList(id(), requested_metrics);

  // We want the keys to be in alphabetical order when printing, so we use an ordered map here.
  typedef std::map<const char*, scoped_refptr<Metric> > OrderedMetricMap;
  OrderedMetricMap metrics;
  AttributeMap attrs;
  {
    // Snapshot the metrics in this registry (not guaranteed to be a consistent snapshot)
    lock_guard<simple_spinlock> l(&lock_);
    attrs = attributes_;
    for (const MetricMap::value_type& val : metric_map_) {
      const MetricPrototype* prototype = val.first;
      const scoped_refptr<Metric>& metric = val.second;

      if (select_all || MatchMetricInList(prototype->name(), requested_metrics)) {
        InsertOrDie(&metrics, prototype->name(), metric);
      }
    }
  }

  // If we had a filter, and we didn't either match this entity or any metrics inside
  // it, don't print the entity at all.
  if (!requested_metrics.empty() && !select_all && metrics.empty()) {
    return Status::OK();
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
  for (OrderedMetricMap::value_type& val : metrics) {
    WARN_NOT_OK(val.second->WriteAsJson(writer, opts),
                strings::Substitute("Failed to write $0 as JSON", val.first));

  }
  writer->EndArray();

  writer->EndObject();

  return Status::OK();
}

void MetricEntity::RetireOldMetrics() {
  MonoTime now(MonoTime::Now(MonoTime::FINE));

  lock_guard<simple_spinlock> l(&lock_);
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
      metric->retire_time_.AddDelta(MonoDelta::FromMilliseconds(
                                      FLAGS_metrics_retirement_age_ms));
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
  lock_guard<simple_spinlock> l(&lock_);
  never_retire_metrics_.push_back(metric);
}

void MetricEntity::SetAttributes(const AttributeMap& attrs) {
  lock_guard<simple_spinlock> l(&lock_);
  attributes_ = attrs;
}

void MetricEntity::SetAttribute(const string& key, const string& val) {
  lock_guard<simple_spinlock> l(&lock_);
  attributes_[key] = val;
}

//
// MetricRegistry
//

MetricRegistry::MetricRegistry() {
}

MetricRegistry::~MetricRegistry() {
}

Status MetricRegistry::WriteAsJson(JsonWriter* writer,
                                   const vector<string>& requested_metrics,
                                   const MetricJsonOptions& opts) const {
  EntityMap entities;
  {
    lock_guard<simple_spinlock> l(&lock_);
    entities = entities_;
  }

  writer->StartArray();
  for (const EntityMap::value_type e : entities) {
    WARN_NOT_OK(e.second->WriteAsJson(writer, requested_metrics, opts),
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

void MetricRegistry::RetireOldMetrics() {
  lock_guard<simple_spinlock> l(&lock_);
  for (auto it = entities_.begin(); it != entities_.end();) {
    it->second->RetireOldMetrics();

    if (it->second->num_metrics() == 0 && it->second->HasOneRef()) {
      // No metrics and no external references to this entity, so we can retire it.
      // Unlike retiring the metrics themselves, we don't wait for any timeout
      // to retire them -- we assume that that timed retention has been satisfied
      // by holding onto the metrics inside the entity.
      entities_.erase(it++);
    } else {
      ++it;
    }
  }
}

//
// MetricPrototypeRegistry
//
MetricPrototypeRegistry* MetricPrototypeRegistry::get() {
  return Singleton<MetricPrototypeRegistry>::get();
}

void MetricPrototypeRegistry::AddMetric(const MetricPrototype* prototype) {
  lock_guard<simple_spinlock> l(&lock_);
  metrics_.push_back(prototype);
}

void MetricPrototypeRegistry::AddEntity(const MetricEntityPrototype* prototype) {
  lock_guard<simple_spinlock> l(&lock_);
  entities_.push_back(prototype);
}

void MetricPrototypeRegistry::WriteAsJson(JsonWriter* writer) const {
  lock_guard<simple_spinlock> l(&lock_);
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

void MetricPrototypeRegistry::WriteAsJsonAndExit() const {
  std::stringstream s;
  JsonWriter w(&s, JsonWriter::PRETTY);
  WriteAsJson(&w);
  std::cout << s.str() << std::endl;
  exit(0);
}

//
// MetricPrototype
//
MetricPrototype::MetricPrototype(CtorArgs args) : args_(std::move(args)) {
  MetricPrototypeRegistry::get()->AddMetric(this);
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
  }
}

//
// FunctionGaugeDetacher
//

FunctionGaugeDetacher::FunctionGaugeDetacher() {
}

FunctionGaugeDetacher::~FunctionGaugeDetacher() {
  for (const Closure& c : callbacks_) {
    c.Run();
  }
}

scoped_refptr<MetricEntity> MetricRegistry::FindOrCreateEntity(
    const MetricEntityPrototype* prototype,
    const std::string& id,
    const MetricEntity::AttributeMap& initial_attributes) {
  lock_guard<simple_spinlock> l(&lock_);
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

Metric::~Metric() {
}

//
// Gauge
//

Status Gauge::WriteAsJson(JsonWriter* writer,
                          const MetricJsonOptions& opts) const {
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
  lock_guard<simple_spinlock> l(&lock_);
  return value_;
}

void StringGauge::set_value(const std::string& value) {
  lock_guard<simple_spinlock> l(&lock_);
  value_ = value;
}

void StringGauge::WriteValue(JsonWriter* writer) const {
  writer->String(value());
}

//
// Counter
//
// This implementation is optimized by using a striped counter. See LongAdder for details.

scoped_refptr<Counter> CounterPrototype::Instantiate(const scoped_refptr<MetricEntity>& entity) {
  return entity->FindOrCreateCounter(this);
}

Counter::Counter(const CounterPrototype* proto) : Metric(proto) {
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
  writer->StartObject();

  prototype_->WriteFields(writer, opts);

  writer->String("value");
  writer->Int64(value());

  writer->EndObject();
  return Status::OK();
}

/////////////////////////////////////////////////
// HistogramPrototype
/////////////////////////////////////////////////

HistogramPrototype::HistogramPrototype(const MetricPrototype::CtorArgs& args,
                                       uint64_t max_trackable_value, int num_sig_digits)
  : MetricPrototype(args),
    max_trackable_value_(max_trackable_value),
    num_sig_digits_(num_sig_digits) {
  // Better to crash at definition time that at instantiation time.
  CHECK(HdrHistogram::IsValidHighestTrackableValue(max_trackable_value))
      << Substitute("Invalid max trackable value on histogram $0: $1",
                    args.name_, max_trackable_value);
  CHECK(HdrHistogram::IsValidNumSignificantDigits(num_sig_digits))
      << Substitute("Invalid number of significant digits on histogram $0: $1",
                    args.name_, num_sig_digits);
}

scoped_refptr<Histogram> HistogramPrototype::Instantiate(
    const scoped_refptr<MetricEntity>& entity) {
  return entity->FindOrCreateHistogram(this);
}

/////////////////////////////////////////////////
// Histogram
/////////////////////////////////////////////////

Histogram::Histogram(const HistogramPrototype* proto)
  : Metric(proto),
    histogram_(new HdrHistogram(proto->max_trackable_value(), proto->num_sig_digits())) {
}

void Histogram::Increment(int64_t value) {
  histogram_->Increment(value);
}

void Histogram::IncrementBy(int64_t value, int64_t amount) {
  histogram_->IncrementBy(value, amount);
}

Status Histogram::WriteAsJson(JsonWriter* writer,
                              const MetricJsonOptions& opts) const {

  HistogramSnapshotPB snapshot;
  RETURN_NOT_OK(GetHistogramSnapshotPB(&snapshot, opts));
  writer->Protobuf(snapshot);
  return Status::OK();
}

Status Histogram::GetHistogramSnapshotPB(HistogramSnapshotPB* snapshot_pb,
                                         const MetricJsonOptions& opts) const {
  HdrHistogram snapshot(*histogram_);
  snapshot_pb->set_name(prototype_->name());
  if (opts.include_schema_info) {
    snapshot_pb->set_type(MetricType::Name(prototype_->type()));
    snapshot_pb->set_label(prototype_->label());
    snapshot_pb->set_unit(MetricUnit::Name(prototype_->unit()));
    snapshot_pb->set_description(prototype_->description());
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

ScopedLatencyMetric::ScopedLatencyMetric(Histogram* latency_hist)
  : latency_hist_(latency_hist) {
  if (latency_hist_) {
    time_started_ = MonoTime::Now(MonoTime::FINE);
  }
}

ScopedLatencyMetric::~ScopedLatencyMetric() {
  if (latency_hist_ != nullptr) {
    MonoTime time_now = MonoTime::Now(MonoTime::FINE);
    latency_hist_->Increment(time_now.GetDeltaSince(time_started_).ToMicroseconds());
  }
}

} // namespace kudu
