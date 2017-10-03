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
#ifndef KUDU_UTIL_METRICS_H
#define KUDU_UTIL_METRICS_H

/////////////////////////////////////////////////////
// Kudu Metrics
/////////////////////////////////////////////////////
//
// Summary
// ------------------------------------------------------------
//
// This API provides a basic set of metrics primitives along the lines of the Code Hale's
// metrics library along with JSON formatted output of running metrics.
//
// The metrics system has a few main concepts in its data model:
//
// Metric Prototypes
// -----------------
// Every metric that may be emitted is constructed from a prototype. The prototype defines
// the name of the metric, the entity it is attached to, its type, its units, and a description.
//
// Metric prototypes are defined statically using the METRIC_DEFINE_*(...) macros. This
// allows us to easily enumerate a full list of every metric that might be emitted from a
// server, thus allowing auto-generation of metric metadata for integration with
// monitoring systems such as Cloudera Manager.
//
// Metric Entity Prototypes
// ------------------------
// The other main type in the data model is the Metric Entity. The most basic entity is the
// "server" entity -- metrics such as memory usage, RPC rates, etc, are typically associated
// with the server as a whole.
//
// Users of the metrics framework can define more entity types using the
// METRIC_DEFINE_entity(...) macro.
//
// MetricEntity instances
// -----------------------
// Each defined Metric Entity Type serves as a prototype allowing instantiation of a
// MetricEntity object. Each instance then has its own unique set of metrics. For
// example, in the case of Kudu, we define a Metric Entity Type called 'tablet', and the
// Tablet Server instantiates one MetricEntity instance per tablet that it hosts.
//
// MetricEntity instances are instantiated within a MetricRegistry, and each instance is
// expected to have a unique string identifier within that registry. To continue the
// example above, a tablet entity uses its tablet ID as its unique identifier. These
// identifiers are exposed to the operator and surfaced in monitoring tools.
//
// MetricEntity instances may also carry a key-value map of string attributes. These
// attributes are directly exposed to monitoring systems via the JSON output. Monitoring
// systems may use this information to allow hierarchical aggregation beteween entities,
// display them to the user, etc.
//
// Metric instances
// ----------------
// Given a MetricEntity instance and a Metric Prototype, one can instantiate a Metric
// instance. For example, the Kudu Tablet Server instantiates one MetricEntity instance
// for each tablet, and then instantiates the 'tablet_rows_inserted' prototype within that
// entity. Thus, each tablet then has a separate instance of the metric, allowing the end
// operator to track the metric on a per-tablet basis.
//
//
// Types of metrics
// ------------------------------------------------------------
// Gauge: Set or get a point-in-time value.
//  - string: Gauge for a string value.
//  - Primitive types (bool, int64_t/uint64_t, double): Lock-free gauges.
// Counter: Get, reset, increment or decrement an int64_t value.
// Histogram: Increment buckets of values segmented by configurable max and precision.
//
// Gauge vs. Counter
// ------------------------------------------------------------
//
// A Counter is a metric we expect to only monotonically increase. A
// Gauge is a metric that can decrease and increase. Use a Gauge to
// reflect a sample, e.g., the number of transaction in-flight at a
// given time; use a Counter when considering a metric over time,
// e.g., exposing the number of transactions processed since start to
// produce a metric for the number of transactions processed over some
// time period.
//
// The one exception to this rule is that occasionally it may be more convenient to
// implement a metric as a Gauge, even when it is logically a counter, due to Gauge's
// support for fetching metric values via a bound function. In that case, you can
// use the 'EXPOSE_AS_COUNTER' flag when defining the gauge prototype. For example:
//
// METRIC_DEFINE_gauge_uint64(server, threads_started,
//                            "Threads Started",
//                            kudu::MetricUnit::kThreads,
//                            "Total number of threads started on this server",
//                            kudu::EXPOSE_AS_COUNTER);
//
//
// Metrics ownership
// ------------------------------------------------------------
//
// Metrics are reference-counted, and one of the references is always held by a metrics
// entity itself. Users of metrics should typically hold a scoped_refptr to their metrics
// within class instances, so that they also hold a reference. The one exception to this
// is FunctionGauges: see the class documentation below for a typical Gauge ownership pattern.
//
// Because the metrics entity holds a reference to the metric, this means that metrics will
// not be immediately destructed when your class instance publishing them is destructed.
// This is on purpose: metrics are retained for a configurable time interval even after they
// are no longer being published. The purpose of this is to allow monitoring systems, which
// only poll metrics infrequently (eg once a minute) to see the last value of a metric whose
// owner was destructed in between two polls.
//
//
// Example usage for server-level metrics
// ------------------------------------------------------------
//
// 1) In your server class, define the top-level registry and the server entity:
//
//   MetricRegistry metric_registry_;
//   scoped_refptr<MetricEntity> metric_entity_;
//
// 2) In your server constructor/initialization, construct metric_entity_. This instance
//    will be plumbed through into other subsystems that want to register server-level
//    metrics.
//
//   metric_entity_ = METRIC_ENTITY_server.Instantiate(&registry_, "some server identifier)");
//
// 3) At the top of your .cc file where you want to emit a metric, define the metric prototype:
//
//   METRIC_DEFINE_counter(server, ping_requests, "Ping Requests", kudu::MetricUnit::kRequests,
//       "Number of Ping() RPC requests this server has handled since start");
//
// 4) In your class where you want to emit metrics, define the metric instance itself:
//   scoped_refptr<Counter> ping_counter_;
//
// 5) In your class constructor, instantiate the metric based on the MetricEntity plumbed in:
//
//   MyClass(..., const scoped_refptr<MetricEntity>& metric_entity) :
//     ping_counter_(METRIC_ping_requests.Instantiate(metric_entity)) {
//   }
//
// 6) Where you want to change the metric value, just use the instance variable:
//
//   ping_counter_->IncrementBy(100);
//
//
// Example usage for custom entity metrics
// ------------------------------------------------------------
// Follow the same pattern as above, but also define a metric entity somewhere. For example:
//
// At the top of your CC file:
//
//   METRIC_DEFINE_entity(my_entity);
//   METRIC_DEFINE_counter(my_entity, ping_requests, "Ping Requests", kudu::MetricUnit::kRequests,
//       "Number of Ping() RPC requests this particular entity has handled since start");
//
// In whatever class represents the entity:
//
//   entity_ = METRIC_ENTITY_my_entity.Instantiate(&registry_, my_entity_id);
//
// In whatever classes emit metrics:
//
//   scoped_refptr<Counter> ping_requests_ = METRIC_ping_requests.Instantiate(entity);
//   ping_requests_->Increment();
//
// NOTE: at runtime, the metrics system prevents you from instantiating a metric in the
// wrong entity type. This ensures that the metadata can fully describe the set of metric-entity
// relationships.
//
// Plumbing of MetricEntity and MetricRegistry objects
// ------------------------------------------------------------
// Generally, the rule of thumb to follow when plumbing through entities and registries is
// this: if you're creating new entities or you need to dump the registry contents
// (e.g. path handlers), pass in the registry. Otherwise, pass in the entity.
//
// ===========
// JSON output
// ===========
//
// The first-class output format for metrics is pretty-printed JSON.
// Such a format is relatively easy for humans and machines to read.
//
// The top level JSON object is an array, which contains one element per
// entity. Each entity is an object which has its type, id, and an array
// of metrics. Each metric contains its type, name, unit, description, value,
// etc.
// TODO: Output to HTML.
//
// Example JSON output:
//
// [
//     {
//         "type": "tablet",
//         "id": "e95e57ba8d4d48458e7c7d35020d4a46",
//         "attributes": {
//           "table_id": "12345",
//           "table_name": "my_table"
//         },
//         "metrics": [
//             {
//                 "type": "counter",
//                 "name": "log_reader_bytes_read",
//                 "label": "Log Reader Bytes Read",
//                 "unit": "bytes",
//                 "description": "Number of bytes read since tablet start",
//                 "value": 0
//             },
//             ...
//           ]
//      },
//      ...
// ]
//
/////////////////////////////////////////////////////

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest_prod.h>

#include "kudu/gutil/bind.h"
#include "kudu/gutil/callback.h"
#include "kudu/gutil/casts.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/singleton.h"
#include "kudu/util/atomic.h"
#include "kudu/util/jsonwriter.h"
#include "kudu/util/locks.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"
#include "kudu/util/striped64.h"

// Define a new entity type.
//
// The metrics subsystem itself defines the entity type 'server', but other
// entity types can be registered using this macro.
#define METRIC_DEFINE_entity(name)                               \
  ::kudu::MetricEntityPrototype METRIC_ENTITY_##name(#name)

// Convenience macros to define metric prototypes.
// See the documentation at the top of this file for example usage.
#define METRIC_DEFINE_counter(entity, name, label, unit, desc)   \
  ::kudu::CounterPrototype METRIC_##name(                        \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc))

#define METRIC_DEFINE_gauge_string(entity, name, label, unit, desc, ...) \
  ::kudu::GaugePrototype<std::string> METRIC_##name(                 \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc, ## __VA_ARGS__))
#define METRIC_DEFINE_gauge_bool(entity, name, label, unit, desc, ...) \
  ::kudu::GaugePrototype<bool> METRIC_##  name(                    \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc, ## __VA_ARGS__))
#define METRIC_DEFINE_gauge_int32(entity, name, label, unit, desc, ...) \
  ::kudu::GaugePrototype<int32_t> METRIC_##name(                   \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc, ## __VA_ARGS__))
#define METRIC_DEFINE_gauge_uint32(entity, name, label, unit, desc, ...) \
  ::kudu::GaugePrototype<uint32_t> METRIC_##name(                    \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc, ## __VA_ARGS__))
#define METRIC_DEFINE_gauge_int64(entity, name, label, unit, desc, ...) \
  ::kudu::GaugePrototype<int64_t> METRIC_##name(                   \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc, ## __VA_ARGS__))
#define METRIC_DEFINE_gauge_uint64(entity, name, label, unit, desc, ...) \
  ::kudu::GaugePrototype<uint64_t> METRIC_##name(                    \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc, ## __VA_ARGS__))
#define METRIC_DEFINE_gauge_double(entity, name, label, unit, desc, ...) \
  ::kudu::GaugePrototype<double> METRIC_##name(                      \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc, ## __VA_ARGS__))

#define METRIC_DEFINE_histogram(entity, name, label, unit, desc, max_val, num_sig_digits) \
  ::kudu::HistogramPrototype METRIC_##name(                                       \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc), \
    max_val, num_sig_digits)

// The following macros act as forward declarations for entity types and metric prototypes.
#define METRIC_DECLARE_entity(name) \
  extern ::kudu::MetricEntityPrototype METRIC_ENTITY_##name
#define METRIC_DECLARE_counter(name)                             \
  extern ::kudu::CounterPrototype METRIC_##name
#define METRIC_DECLARE_gauge_string(name) \
  extern ::kudu::GaugePrototype<std::string> METRIC_##name
#define METRIC_DECLARE_gauge_bool(name) \
  extern ::kudu::GaugePrototype<bool> METRIC_##name
#define METRIC_DECLARE_gauge_int32(name) \
  extern ::kudu::GaugePrototype<int32_t> METRIC_##name
#define METRIC_DECLARE_gauge_uint32(name) \
  extern ::kudu::GaugePrototype<uint32_t> METRIC_##name
#define METRIC_DECLARE_gauge_int64(name) \
  extern ::kudu::GaugePrototype<int64_t> METRIC_##name
#define METRIC_DECLARE_gauge_uint64(name) \
  extern ::kudu::GaugePrototype<uint64_t> METRIC_##name
#define METRIC_DECLARE_gauge_double(name) \
  extern ::kudu::GaugePrototype<double> METRIC_##name
#define METRIC_DECLARE_histogram(name) \
  extern ::kudu::HistogramPrototype METRIC_##name

#if defined(__APPLE__)
#define METRIC_DEFINE_gauge_size(entity, name, label, unit, desc, ...) \
  ::kudu::GaugePrototype<size_t> METRIC_##name(                    \
      ::kudu::MetricPrototype::CtorArgs(#entity, #name, label, unit, desc, ## __VA_ARGS__))
#define METRIC_DECLARE_gauge_size(name) \
  extern ::kudu::GaugePrototype<size_t> METRIC_##name
#else
#define METRIC_DEFINE_gauge_size METRIC_DEFINE_gauge_uint64
#define METRIC_DECLARE_gauge_size METRIC_DECLARE_gauge_uint64
#endif

namespace kudu {

class Counter;
class CounterPrototype;

template<typename T>
class AtomicGauge;
template<typename T>
class FunctionGauge;
class Gauge;
template<typename T>
class GaugePrototype;

class Metric;
class MetricEntityPrototype;
class MetricPrototype;
class MetricRegistry;

class HdrHistogram;
class Histogram;
class HistogramPrototype;
class HistogramSnapshotPB;

class MetricEntity;

} // namespace kudu

// Forward-declare the generic 'server' entity type.
// We have to do this here below the forward declarations, but not
// in the kudu namespace.
METRIC_DECLARE_entity(server);

namespace kudu {

// Unit types to be used with metrics.
// As additional units are required, add them to this enum and also to Name().
struct MetricUnit {
  enum Type {
    kCacheHits,
    kCacheQueries,
    kBytes,
    kRequests,
    kEntries,
    kRows,
    kCells,
    kConnections,
    kOperations,
    kProbes,
    kNanoseconds,
    kMicroseconds,
    kMilliseconds,
    kSeconds,
    kThreads,
    kTransactions,
    kUnits,
    kScanners,
    kMaintenanceOperations,
    kBlocks,
    kLogBlockContainers,
    kTasks,
    kMessages,
    kContextSwitches,
  };
  static const char* Name(Type unit);
};

class MetricType {
 public:
  enum Type { kGauge, kCounter, kHistogram };
  static const char* Name(Type t);
 private:
  static const char* const kGaugeType;
  static const char* const kCounterType;
  static const char* const kHistogramType;
};

struct MetricJsonOptions {
  MetricJsonOptions() :
    include_raw_histograms(false),
    include_schema_info(false) {
  }

  // Include the raw histogram values and counts in the JSON output.
  // This allows consumers to do cross-server aggregation or window
  // data over time.
  // Default: false
  bool include_raw_histograms;

  // Include the metrics "schema" information (i.e description, label,
  // unit, etc).
  // Default: false
  bool include_schema_info;
};

class MetricEntityPrototype {
 public:
  explicit MetricEntityPrototype(const char* name);
  ~MetricEntityPrototype();

  const char* name() const { return name_; }

  // Find or create an entity with the given ID within the provided 'registry'.
  scoped_refptr<MetricEntity> Instantiate(
      MetricRegistry* registry,
      const std::string& id) const {
    return Instantiate(registry, id, std::unordered_map<std::string, std::string>());
  }

  // If the entity already exists, then 'initial_attrs' will replace all existing
  // attributes.
  scoped_refptr<MetricEntity> Instantiate(
      MetricRegistry* registry,
      const std::string& id,
      const std::unordered_map<std::string, std::string>& initial_attrs) const;

 private:
  const char* const name_;

  DISALLOW_COPY_AND_ASSIGN(MetricEntityPrototype);
};

class MetricEntity : public RefCountedThreadSafe<MetricEntity> {
 public:
  typedef std::unordered_map<const MetricPrototype*, scoped_refptr<Metric> > MetricMap;
  typedef std::unordered_map<std::string, std::string> AttributeMap;

  scoped_refptr<Counter> FindOrCreateCounter(const CounterPrototype* proto);
  scoped_refptr<Histogram> FindOrCreateHistogram(const HistogramPrototype* proto);

  template<typename T>
  scoped_refptr<AtomicGauge<T> > FindOrCreateGauge(const GaugePrototype<T>* proto,
                                                   const T& initial_value);

  template<typename T>
  scoped_refptr<FunctionGauge<T> > FindOrCreateFunctionGauge(const GaugePrototype<T>* proto,
                                                             const Callback<T()>& function);

  // Return the metric instantiated from the given prototype, or NULL if none has been
  // instantiated. Primarily used by tests trying to read metric values.
  scoped_refptr<Metric> FindOrNull(const MetricPrototype& prototype) const;

  const std::string& id() const { return id_; }

  // See MetricRegistry::WriteAsJson()
  Status WriteAsJson(JsonWriter* writer,
                     const std::vector<std::string>& requested_metrics,
                     const MetricJsonOptions& opts) const;

  const MetricMap& UnsafeMetricsMapForTests() const { return metric_map_; }

  // Mark that the given metric should never be retired until the metric
  // registry itself destructs. This is useful for system metrics such as
  // tcmalloc, etc, which should live as long as the process itself.
  void NeverRetire(const scoped_refptr<Metric>& metric);

  // Scan the metrics map for metrics needing retirement, removing them as necessary.
  //
  // Metrics are retired when they are no longer referenced outside of the metrics system
  // itself. Additionally, we only retire a metric that has been in this state for
  // at least FLAGS_metrics_retirement_age_ms milliseconds.
  void RetireOldMetrics();

  // Replaces all attributes for this entity.
  // Any attributes currently set, but not in 'attrs', are removed.
  void SetAttributes(const AttributeMap& attrs);

  // Set a particular attribute. Replaces any current value.
  void SetAttribute(const std::string& key, const std::string& val);

  int num_metrics() const {
    lock_guard<simple_spinlock> l(&lock_);
    return metric_map_.size();
  }

 private:
  friend class MetricRegistry;
  friend class RefCountedThreadSafe<MetricEntity>;

  MetricEntity(const MetricEntityPrototype* prototype, std::string id,
               AttributeMap attributes);
  ~MetricEntity();

  // Ensure that the given metric prototype is allowed to be instantiated
  // within this entity. This entity's type must match the expected entity
  // type defined within the metric prototype.
  void CheckInstantiation(const MetricPrototype* proto) const;

  const MetricEntityPrototype* const prototype_;
  const std::string id_;

  mutable simple_spinlock lock_;

  // Map from metric name to Metric object. Protected by lock_.
  MetricMap metric_map_;

  // The key/value attributes. Protected by lock_
  AttributeMap attributes_;

  // The set of metrics which should never be retired. Protected by lock_.
  std::vector<scoped_refptr<Metric> > never_retire_metrics_;
};

// Base class to allow for putting all metrics into a single container.
// See documentation at the top of this file for information on metrics ownership.
class Metric : public RefCountedThreadSafe<Metric> {
 public:
  // All metrics must be able to render themselves as JSON.
  virtual Status WriteAsJson(JsonWriter* writer,
                             const MetricJsonOptions& opts) const = 0;

  const MetricPrototype* prototype() const { return prototype_; }

 protected:
  explicit Metric(const MetricPrototype* prototype);
  virtual ~Metric();

  const MetricPrototype* const prototype_;

 private:
  friend class MetricEntity;
  friend class RefCountedThreadSafe<Metric>;

  // The time at which we should retire this metric if it is still un-referenced outside
  // of the metrics subsystem. If this metric is not due for retirement, this member is
  // uninitialized.
  MonoTime retire_time_;

  DISALLOW_COPY_AND_ASSIGN(Metric);
};

// Registry of all the metrics for a server.
//
// This aggregates the MetricEntity objects associated with the server.
class MetricRegistry {
 public:
  MetricRegistry();
  ~MetricRegistry();

  scoped_refptr<MetricEntity> FindOrCreateEntity(const MetricEntityPrototype* prototype,
                                                 const std::string& id,
                                                 const MetricEntity::AttributeMap& initial_attrs);

  // Writes metrics in this registry to 'writer'.
  //
  // 'requested_metrics' is a set of substrings to match metric names against,
  // where '*' matches all metrics.
  //
  // The string matching can either match an entity ID or a metric name.
  // If it matches an entity ID, then all metrics for that entity will be printed.
  //
  // See the MetricJsonOptions struct definition above for options changing the
  // output of this function.
  Status WriteAsJson(JsonWriter* writer,
                     const std::vector<std::string>& requested_metrics,
                     const MetricJsonOptions& opts) const;

  // For each registered entity, retires orphaned metrics. If an entity has no more
  // metrics and there are no external references, entities are removed as well.
  //
  // See MetricEntity::RetireOldMetrics().
  void RetireOldMetrics();

  // Return the number of entities in this registry.
  int num_entities() const {
    lock_guard<simple_spinlock> l(&lock_);
    return entities_.size();
  }

 private:
  typedef std::unordered_map<std::string, scoped_refptr<MetricEntity> > EntityMap;
  EntityMap entities_;

  mutable simple_spinlock lock_;
  DISALLOW_COPY_AND_ASSIGN(MetricRegistry);
};

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

  // Convenience wrapper around WriteAsJson(...). This dumps the JSON information
  // to stdout and then exits.
  void WriteAsJsonAndExit() const;
 private:
  friend class Singleton<MetricPrototypeRegistry>;
  friend class MetricPrototype;
  friend class MetricEntityPrototype;
  MetricPrototypeRegistry() {}
  ~MetricPrototypeRegistry() {}

  // Register a metric prototype in the registry.
  void AddMetric(const MetricPrototype* prototype);

  // Register a metric entity prototype in the registry.
  void AddEntity(const MetricEntityPrototype* prototype);

  mutable simple_spinlock lock_;
  std::vector<const MetricPrototype*> metrics_;
  std::vector<const MetricEntityPrototype*> entities_;

  DISALLOW_COPY_AND_ASSIGN(MetricPrototypeRegistry);
};

enum PrototypeFlags {
  // Flag which causes a Gauge prototype to expose itself as if it
  // were a counter.
  EXPOSE_AS_COUNTER = 1 << 0
};

class MetricPrototype {
 public:
  // Simple struct to aggregate the arguments common to all prototypes.
  // This makes constructor chaining a little less tedious.
  struct CtorArgs {
    CtorArgs(const char* entity_type,
             const char* name,
             const char* label,
             MetricUnit::Type unit,
             const char* description,
             uint32_t flags = 0)
      : entity_type_(entity_type),
        name_(name),
        label_(label),
        unit_(unit),
        description_(description),
        flags_(flags) {
    }

    const char* const entity_type_;
    const char* const name_;
    const char* const label_;
    const MetricUnit::Type unit_;
    const char* const description_;
    const uint32_t flags_;
  };

  const char* entity_type() const { return args_.entity_type_; }
  const char* name() const { return args_.name_; }
  const char* label() const { return args_.label_; }
  MetricUnit::Type unit() const { return args_.unit_; }
  const char* description() const { return args_.description_; }
  virtual MetricType::Type type() const = 0;

  // Writes the fields of this prototype to the given JSON writer.
  void WriteFields(JsonWriter* writer,
                   const MetricJsonOptions& opts) const;

 protected:
  explicit MetricPrototype(CtorArgs args);
  virtual ~MetricPrototype() {
  }

  const CtorArgs args_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MetricPrototype);
};

// A description of a Gauge.
template<typename T>
class GaugePrototype : public MetricPrototype {
 public:
  explicit GaugePrototype(const MetricPrototype::CtorArgs& args)
    : MetricPrototype(args) {
  }

  // Instantiate a "manual" gauge.
  scoped_refptr<AtomicGauge<T> > Instantiate(
      const scoped_refptr<MetricEntity>& entity,
      const T& initial_value) const {
    return entity->FindOrCreateGauge(this, initial_value);
  }

  // Instantiate a gauge that is backed by the given callback.
  scoped_refptr<FunctionGauge<T> > InstantiateFunctionGauge(
      const scoped_refptr<MetricEntity>& entity,
      const Callback<T()>& function) const {
    return entity->FindOrCreateFunctionGauge(this, function);
  }

  virtual MetricType::Type type() const OVERRIDE {
    if (args_.flags_ & EXPOSE_AS_COUNTER) {
      return MetricType::kCounter;
    } else {
      return MetricType::kGauge;
    }
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(GaugePrototype);
};

// Abstract base class to provide point-in-time metric values.
class Gauge : public Metric {
 public:
  explicit Gauge(const MetricPrototype* prototype)
    : Metric(prototype) {
  }
  virtual ~Gauge() {}
  virtual Status WriteAsJson(JsonWriter* w,
                             const MetricJsonOptions& opts) const OVERRIDE;
 protected:
  virtual void WriteValue(JsonWriter* writer) const = 0;
 private:
  DISALLOW_COPY_AND_ASSIGN(Gauge);
};

// Gauge implementation for string that uses locks to ensure thread safety.
class StringGauge : public Gauge {
 public:
  StringGauge(const GaugePrototype<std::string>* proto,
              std::string initial_value);
  std::string value() const;
  void set_value(const std::string& value);
 protected:
  virtual void WriteValue(JsonWriter* writer) const OVERRIDE;
 private:
  std::string value_;
  mutable simple_spinlock lock_;  // Guards value_
  DISALLOW_COPY_AND_ASSIGN(StringGauge);
};

// Lock-free implementation for types that are convertible to/from int64_t.
template <typename T>
class AtomicGauge : public Gauge {
 public:
  AtomicGauge(const GaugePrototype<T>* proto, T initial_value)
    : Gauge(proto),
      value_(initial_value) {
  }
  T value() const {
    return static_cast<T>(value_.Load(kMemOrderRelease));
  }
  virtual void set_value(const T& value) {
    value_.Store(static_cast<int64_t>(value), kMemOrderNoBarrier);
  }
  void Increment() {
    value_.IncrementBy(1, kMemOrderNoBarrier);
  }
  virtual void IncrementBy(int64_t amount) {
    value_.IncrementBy(amount, kMemOrderNoBarrier);
  }
  void Decrement() {
    IncrementBy(-1);
  }
  void DecrementBy(int64_t amount) {
    IncrementBy(-amount);
  }

 protected:
  virtual void WriteValue(JsonWriter* writer) const OVERRIDE {
    writer->Value(value());
  }
  AtomicInt<int64_t> value_;
 private:
  DISALLOW_COPY_AND_ASSIGN(AtomicGauge);
};

// Utility class to automatically detach FunctionGauges when a class destructs.
//
// Because FunctionGauges typically access class instance state, it's important to ensure
// that they are detached before the class destructs. One approach is to make all
// FunctionGauge instances be members of the class, and then call gauge_->Detach() in your
// class's destructor. However, it's easy to forget to do this, which would lead to
// heap-use-after-free bugs. This type of bug is easy to miss in unit tests because the
// tests don't always poll metrics. Using a FunctionGaugeDetacher member instead makes
// the detaching automatic and thus less error-prone.
//
// Example usage:
//
// METRIC_define_gauge_int64(my_metric, MetricUnit::kOperations, "My metric docs");
// class MyClassWithMetrics {
//  public:
//   MyClassWithMetrics(const scoped_refptr<MetricEntity>& entity) {
//     METRIC_my_metric.InstantiateFunctionGauge(entity,
//       Bind(&MyClassWithMetrics::ComputeMyMetric, Unretained(this)))
//       ->AutoDetach(&metric_detacher_);
//   }
//   ~MyClassWithMetrics() {
//   }
//
//   private:
//    int64_t ComputeMyMetric() {
//      // Compute some metric based on instance state.
//    }
//    FunctionGaugeDetacher metric_detacher_;
// };
class FunctionGaugeDetacher {
 public:
  FunctionGaugeDetacher();
  ~FunctionGaugeDetacher();

 private:
  template<typename T>
  friend class FunctionGauge;

  void OnDestructor(const Closure& c) {
    callbacks_.push_back(c);
  }

  std::vector<Closure> callbacks_;

  DISALLOW_COPY_AND_ASSIGN(FunctionGaugeDetacher);
};


// A Gauge that calls back to a function to get its value.
//
// This metric type should be used in cases where it is difficult to keep a running
// measure of a metric, but instead would like to compute the metric value whenever it is
// requested by a user.
//
// The lifecycle should be carefully considered when using a FunctionGauge. In particular,
// the bound function needs to always be safe to run -- so if it references a particular
// non-singleton class instance, the instance must out-live the function. Typically,
// the easiest way to ensure this is to use a FunctionGaugeDetacher (see above).
template <typename T>
class FunctionGauge : public Gauge {
 public:
  T value() const {
    lock_guard<simple_spinlock> l(&lock_);
    return function_.Run();
  }

  virtual void WriteValue(JsonWriter* writer) const OVERRIDE {
    writer->Value(value());
  }

  // Reset this FunctionGauge to return a specific value.
  // This should be used during destruction. If you want a settable
  // Gauge, use a normal Gauge instead of a FunctionGauge.
  void DetachToConstant(T v) {
    lock_guard<simple_spinlock> l(&lock_);
    function_ = Bind(&FunctionGauge::Return, v);
  }

  // Get the current value of the gauge, and detach so that it continues to return this
  // value in perpetuity.
  void DetachToCurrentValue() {
    T last_value = value();
    DetachToConstant(last_value);
  }

  // Automatically detach this gauge when the given 'detacher' destructs.
  // After detaching, the metric will return 'value' in perpetuity.
  void AutoDetach(FunctionGaugeDetacher* detacher, T value = T()) {
    detacher->OnDestructor(Bind(&FunctionGauge<T>::DetachToConstant,
                                this, value));
  }

  // Automatically detach this gauge when the given 'detacher' destructs.
  // After detaching, the metric will return whatever its value was at the
  // time of detaching.
  //
  // Note that, when using this method, you should be sure that the FunctionGaugeDetacher
  // is destructed before any objects which are required by the gauge implementation.
  // In typical usage (see the FunctionGaugeDetacher class documentation) this means you
  // should declare the detacher member after all other class members that might be
  // accessed by the gauge function implementation.
  void AutoDetachToLastValue(FunctionGaugeDetacher* detacher) {
    detacher->OnDestructor(Bind(&FunctionGauge<T>::DetachToCurrentValue,
                                this));
  }

 private:
  friend class MetricEntity;

  FunctionGauge(const GaugePrototype<T>* proto, Callback<T()> function)
      : Gauge(proto), function_(std::move(function)) {}

  static T Return(T v) {
    return v;
  }

  mutable simple_spinlock lock_;
  Callback<T()> function_;
  DISALLOW_COPY_AND_ASSIGN(FunctionGauge);
};

// Prototype for a counter.
class CounterPrototype : public MetricPrototype {
 public:
  explicit CounterPrototype(const MetricPrototype::CtorArgs& args)
    : MetricPrototype(args) {
  }
  scoped_refptr<Counter> Instantiate(const scoped_refptr<MetricEntity>& entity);

  virtual MetricType::Type type() const OVERRIDE { return MetricType::kCounter; }

 private:
  DISALLOW_COPY_AND_ASSIGN(CounterPrototype);
};

// Simple incrementing 64-bit integer.
// Only use Counters in cases that we expect the count to only increase. For example,
// a counter is appropriate for "number of transactions processed by the server",
// but not for "number of transactions currently in flight". Monitoring software
// knows that counters only increase and thus can compute rates over time, rates
// across multiple servers, etc, which aren't appropriate in the case of gauges.
class Counter : public Metric {
 public:
  int64_t value() const;
  void Increment();
  void IncrementBy(int64_t amount);
  virtual Status WriteAsJson(JsonWriter* w,
                             const MetricJsonOptions& opts) const OVERRIDE;

 private:
  FRIEND_TEST(MetricsTest, SimpleCounterTest);
  FRIEND_TEST(MultiThreadedMetricsTest, CounterIncrementTest);
  friend class MetricEntity;

  explicit Counter(const CounterPrototype* proto);

  LongAdder value_;
  DISALLOW_COPY_AND_ASSIGN(Counter);
};

class HistogramPrototype : public MetricPrototype {
 public:
  HistogramPrototype(const MetricPrototype::CtorArgs& args,
                     uint64_t max_trackable_value, int num_sig_digits);
  scoped_refptr<Histogram> Instantiate(const scoped_refptr<MetricEntity>& entity);

  uint64_t max_trackable_value() const { return max_trackable_value_; }
  int num_sig_digits() const { return num_sig_digits_; }
  virtual MetricType::Type type() const OVERRIDE { return MetricType::kHistogram; }

 private:
  const uint64_t max_trackable_value_;
  const int num_sig_digits_;
  DISALLOW_COPY_AND_ASSIGN(HistogramPrototype);
};

class Histogram : public Metric {
 public:
  // Increment the histogram for the given value.
  // 'value' must be non-negative.
  void Increment(int64_t value);

  // Increment the histogram for the given value by the given amount.
  // 'value' and 'amount' must be non-negative.
  void IncrementBy(int64_t value, int64_t amount);

  // Return the total number of values added to the histogram (via Increment()
  // or IncrementBy()).
  uint64_t TotalCount() const;

  virtual Status WriteAsJson(JsonWriter* w,
                             const MetricJsonOptions& opts) const OVERRIDE;

  // Returns a snapshot of this histogram including the bucketed values and counts.
  Status GetHistogramSnapshotPB(HistogramSnapshotPB* snapshot,
                                const MetricJsonOptions& opts) const;

  uint64_t CountInBucketForValueForTests(uint64_t value) const;
  uint64_t MinValueForTests() const;
  uint64_t MaxValueForTests() const;
  double MeanValueForTests() const;

 private:
  FRIEND_TEST(MetricsTest, SimpleHistogramTest);
  friend class MetricEntity;
  explicit Histogram(const HistogramPrototype* proto);

  const gscoped_ptr<HdrHistogram> histogram_;
  DISALLOW_COPY_AND_ASSIGN(Histogram);
};

// Measures a duration while in scope. Adds this duration to specified histogram on destruction.
class ScopedLatencyMetric {
 public:
  // NOTE: the given histogram must live as long as this object.
  // If 'latency_hist' is NULL, this turns into a no-op.
  explicit ScopedLatencyMetric(Histogram* latency_hist);
  ~ScopedLatencyMetric();

 private:
  Histogram* latency_hist_;
  MonoTime time_started_;
};

////////////////////////////////////////////////////////////
// Inline implementations of template methods
////////////////////////////////////////////////////////////

inline scoped_refptr<Counter> MetricEntity::FindOrCreateCounter(
    const CounterPrototype* proto) {
  CheckInstantiation(proto);
  lock_guard<simple_spinlock> l(&lock_);
  scoped_refptr<Counter> m = down_cast<Counter*>(FindPtrOrNull(metric_map_, proto).get());
  if (!m) {
    m = new Counter(proto);
    InsertOrDie(&metric_map_, proto, m);
  }
  return m;
}

inline scoped_refptr<Histogram> MetricEntity::FindOrCreateHistogram(
    const HistogramPrototype* proto) {
  CheckInstantiation(proto);
  lock_guard<simple_spinlock> l(&lock_);
  scoped_refptr<Histogram> m = down_cast<Histogram*>(FindPtrOrNull(metric_map_, proto).get());
  if (!m) {
    m = new Histogram(proto);
    InsertOrDie(&metric_map_, proto, m);
  }
  return m;
}

template<typename T>
inline scoped_refptr<AtomicGauge<T> > MetricEntity::FindOrCreateGauge(
    const GaugePrototype<T>* proto,
    const T& initial_value) {
  CheckInstantiation(proto);
  lock_guard<simple_spinlock> l(&lock_);
  scoped_refptr<AtomicGauge<T> > m = down_cast<AtomicGauge<T>*>(
      FindPtrOrNull(metric_map_, proto).get());
  if (!m) {
    m = new AtomicGauge<T>(proto, initial_value);
    InsertOrDie(&metric_map_, proto, m);
  }
  return m;
}

template<typename T>
inline scoped_refptr<FunctionGauge<T> > MetricEntity::FindOrCreateFunctionGauge(
    const GaugePrototype<T>* proto,
    const Callback<T()>& function) {
  CheckInstantiation(proto);
  lock_guard<simple_spinlock> l(&lock_);
  scoped_refptr<FunctionGauge<T> > m = down_cast<FunctionGauge<T>*>(
      FindPtrOrNull(metric_map_, proto).get());
  if (!m) {
    m = new FunctionGauge<T>(proto, function);
    InsertOrDie(&metric_map_, proto, m);
  }
  return m;
}

} // namespace kudu

#endif // KUDU_UTIL_METRICS_H
