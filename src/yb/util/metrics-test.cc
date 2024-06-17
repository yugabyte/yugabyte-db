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
#include <unordered_set>
#include <vector>

#include "yb/util/logging.h"
#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include "yb/gutil/bind.h"
#include "yb/gutil/map-util.h"

#include "yb/util/hdr_histogram.h"
#include "yb/util/histogram.pb.h"
#include "yb/util/jsonreader.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/metrics.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using std::unordered_set;
using std::vector;
using std::pair;
using namespace std::literals;

DECLARE_int32(metrics_retirement_age_ms);
DECLARE_bool(TEST_pause_flush_aggregated_metrics);

namespace yb {

METRIC_DEFINE_entity(test_entity);

static const string kTableId = "table_id";

class MetricsTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();

    entity_ = METRIC_ENTITY_test_entity.Instantiate(&registry_, "my-test");
  }

 protected:
  template <class LagType>
  void DoLagTest(const MillisLagPrototype& metric) {
    scoped_refptr<LagType> lag = new LagType(&metric);
    ASSERT_EQ(metric.description(), lag->prototype()->description());
    SleepFor(MonoDelta::FromMilliseconds(500));
    // Internal timestamp is set to the time when the metric was created.
    // So this lag is measure of the time elapsed since the metric was
    // created and the check time.
    ASSERT_GE(lag->lag_ms(), 500);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    lag->UpdateTimestampInMilliseconds(now_ms);
    // Verify that the update happened correctly. The lag time should
    // be close to 0, but giving it extra time to account for slow
    // tests.
    ASSERT_LT(lag->lag_ms(), 200);
    // Set the timestamp to some time in the future to verify that the
    // metric can correctly deal with this case.
    lag->UpdateTimestampInMilliseconds(now_ms * 2);
    ASSERT_EQ(0, lag->lag_ms());
  }

  void DoAggregationCheck(const PrometheusWriter& writer,
                          const string& entity_id,
                          const string& metric_name,
                          int expected_aggregation_value,
                          const MetricEntity::AttributeMap& expected_attrs) {
    auto metric_entity_type_it = writer.metric_name_to_entity_type_.find(metric_name);
    ASSERT_NE(metric_entity_type_it, writer.metric_name_to_entity_type_.end());
    auto expected_metric_entity_type = expected_attrs.at("metric_type");
    ASSERT_EQ(metric_entity_type_it->second, expected_metric_entity_type);

    auto attrs_it =
        writer.aggregated_attributes_by_metric_type_.find(metric_entity_type_it->second);
    ASSERT_NE(attrs_it, writer.aggregated_attributes_by_metric_type_.end());
    auto attr_it = attrs_it->second.find(entity_id);
    ASSERT_NE(attr_it, attrs_it->second.end());

    auto actual_attrs = attr_it->second;
    for (const auto& expected_attr : expected_attrs) {
      auto actual_attr_it = actual_attrs.find(expected_attr.first);
      ASSERT_NE(actual_attr_it, actual_attrs.end());
      ASSERT_EQ(actual_attr_it->second, expected_attr.second);
    }
    auto metric_it = writer.aggregated_values_.find(metric_name);
    ASSERT_NE(metric_it, writer.aggregated_values_.end());
    auto value_it = metric_it->second.find(entity_id);
    ASSERT_NE(value_it, metric_it->second.end());
    ASSERT_EQ(value_it->second, expected_aggregation_value);
  }

  std::string dumpPrometheusWriterOutput(const PrometheusWriter& w) { return w.output_->str(); }

  MetricRegistry registry_;
  scoped_refptr<MetricEntity> entity_;
};

METRIC_DEFINE_counter(test_entity, reqs_pending, "Requests Pending", MetricUnit::kRequests,
                      "Number of requests pending");

TEST_F(MetricsTest, SimpleCounterTest) {
  scoped_refptr<Counter> requests =
    new Counter(&METRIC_reqs_pending);
  ASSERT_EQ("Number of requests pending", requests->prototype()->description());
  ASSERT_EQ(0, requests->value());
  requests->Increment();
  ASSERT_EQ(1, requests->value());
  requests->IncrementBy(2);
  ASSERT_EQ(3, requests->value());
}

METRIC_DEFINE_lag(test_entity, lag_simple, "Test MillisLag", "Test MillisLag Description");
TEST_F(MetricsTest, SimpleLagTest) {
  ASSERT_NO_FATALS(DoLagTest<MillisLag>(METRIC_lag_simple));
}

METRIC_DEFINE_lag(test_entity, atomic_lag_simple, "Test Atomic MillisLag",
                  "Test Atomic MillisLag Description");
TEST_F(MetricsTest, SimpleAtomicLagTest) {
  ASSERT_NO_FATALS(DoLagTest<AtomicMillisLag>(METRIC_atomic_lag_simple));
}

METRIC_DEFINE_gauge_uint64(test_entity, fake_memory_usage, "Memory Usage",
                           MetricUnit::kBytes, "Test Gauge 1");

TEST_F(MetricsTest, SimpleAtomicGaugeTest) {
  scoped_refptr<AtomicGauge<uint64_t> > mem_usage =
    METRIC_fake_memory_usage.Instantiate(entity_, 0);
  ASSERT_EQ(METRIC_fake_memory_usage.description(), mem_usage->prototype()->description());
  ASSERT_EQ(0, mem_usage->value());
  mem_usage->IncrementBy(7);
  ASSERT_EQ(7, mem_usage->value());
  mem_usage->set_value(5);
  ASSERT_EQ(5, mem_usage->value());
}

METRIC_DEFINE_gauge_int64(test_entity, test_func_gauge, "Test Gauge", MetricUnit::kBytes,
                          "Test Gauge 2");

static int64_t MyFunction(int* metric_val) {
  return (*metric_val)++;
}

TEST_F(MetricsTest, SimpleFunctionGaugeTest) {
  int metric_val = 1000;
  scoped_refptr<FunctionGauge<int64_t> > gauge =
    METRIC_test_func_gauge.InstantiateFunctionGauge(
      entity_, Bind(&MyFunction, Unretained(&metric_val)));

  ASSERT_EQ(1000, gauge->value());
  ASSERT_EQ(1001, gauge->value());

  gauge->DetachToCurrentValue();
  // After detaching, it should continue to return the same constant value.
  ASSERT_EQ(1002, gauge->value());
  ASSERT_EQ(1002, gauge->value());

  // Test resetting to a constant.
  gauge->DetachToConstant(2);
  ASSERT_EQ(2, gauge->value());
}

TEST_F(MetricsTest, AutoDetachToLastValue) {
  int metric_val = 1000;
  scoped_refptr<FunctionGauge<int64_t> > gauge =
    METRIC_test_func_gauge.InstantiateFunctionGauge(
        entity_, Bind(&MyFunction, Unretained(&metric_val)));

  ASSERT_EQ(1000, gauge->value());
  ASSERT_EQ(1001, gauge->value());
  {
    std::shared_ptr<void> detacher;
    gauge->AutoDetachToLastValue(&detacher);
    ASSERT_EQ(1002, gauge->value());
    ASSERT_EQ(1003, gauge->value());
  }

  ASSERT_EQ(1004, gauge->value());
  ASSERT_EQ(1004, gauge->value());
}

TEST_F(MetricsTest, AutoDetachToConstant) {
  int metric_val = 1000;
  scoped_refptr<FunctionGauge<int64_t> > gauge =
    METRIC_test_func_gauge.InstantiateFunctionGauge(
        entity_, Bind(&MyFunction, Unretained(&metric_val)));

  ASSERT_EQ(1000, gauge->value());
  ASSERT_EQ(1001, gauge->value());
  {
    std::shared_ptr<void> detacher;
    gauge->AutoDetach(&detacher, 12345);
    ASSERT_EQ(1002, gauge->value());
    ASSERT_EQ(1003, gauge->value());
  }

  ASSERT_EQ(12345, gauge->value());
}

METRIC_DEFINE_gauge_uint64(test_entity, counter_as_gauge, "Gauge exposed as Counter",
                           MetricUnit::kBytes, "Gauge exposed as Counter",
                           EXPOSE_AS_COUNTER);
TEST_F(MetricsTest, TEstExposeGaugeAsCounter) {
  ASSERT_EQ(MetricType::kCounter, METRIC_counter_as_gauge.type());
}

METRIC_DEFINE_histogram(test_entity, test_hist, "Test Histogram",
                        MetricUnit::kMilliseconds, "A default histogram.", 100000000L, 2);

METRIC_DEFINE_event_stats(test_entity, test_event_stats, "Test Event Stats",
                          MetricUnit::kMilliseconds, "A default event stats.");

METRIC_DEFINE_entity(tablet);

METRIC_DEFINE_gauge_int32(tablet, test_sum_gauge, "Test Sum Gauge", MetricUnit::kMilliseconds,
                          "Test Gauge with SUM aggregation.");
METRIC_DEFINE_gauge_int32(tablet, test_max_gauge, "Test Max", MetricUnit::kMilliseconds,
                          "Test Gauge with MAX aggregation.",
                          {0, yb::AggregationFunction::kMax} /* optional_args */);

TEST_F(MetricsTest, AggregationTest) {
  const pair<string, string> tablets[] = {{"tablet_1", "table_1"},
                                          {"tablet_2", "table_1"},
                                          {"tablet_3", "table_2"},
                                          {"tablet_4", "table_2"}};
  std::map<std::string, scoped_refptr<MetricEntity>> entities;
  vector<scoped_refptr<Gauge>> gauges;
  int counter = 10;
  for (const auto& tablet : tablets) {
    MetricEntity::AttributeMap entity_attr;
    entity_attr["tablet_id"] = tablet.first + "_id";
    entity_attr["table_name"] = tablet.second;
    entity_attr["table_id"] = tablet.second + "_id";
    auto entity = METRIC_ENTITY_tablet.Instantiate(&registry_, tablet.first, entity_attr);

    // Test SUM aggregation
    auto sum_gauge = METRIC_test_sum_gauge.Instantiate(entity,
                                                       0 /* initial_value */);
    // Test MAX aggregation
    auto max_gauge = METRIC_test_max_gauge.Instantiate(entity,
                                                       0 /* initial_value */);
    sum_gauge->set_value(counter);
    max_gauge->set_value(counter);
    --counter;
    gauges.emplace_back(sum_gauge);
    gauges.emplace_back(max_gauge);
    entities.insert({tablet.first, entity});
  }

  MetricPrometheusOptions opts;
  {
    std::stringstream output;
    PrometheusWriter writer(&output, opts);
    for (const auto& tablet : tablets) {
      ASSERT_OK(entities[tablet.first]->WriteForPrometheus(&writer, opts));
    }
    // Check table aggregation.
    MetricEntity::AttributeMap expected_attrs;
    expected_attrs["metric_type"] = "tablet";
    expected_attrs["table_id"] = "table_1_id";
    expected_attrs["table_name"] = "table_1";
    DoAggregationCheck(writer, "table_1_id", METRIC_test_sum_gauge.name(), 19, expected_attrs);
    DoAggregationCheck(writer, "table_1_id", METRIC_test_max_gauge.name(), 10, expected_attrs);
    expected_attrs["table_id"] = "table_2_id";
    expected_attrs["table_name"] = "table_2";
    DoAggregationCheck(writer, "table_2_id", METRIC_test_sum_gauge.name(), 15, expected_attrs);
    DoAggregationCheck(writer, "table_2_id", METRIC_test_max_gauge.name(), 8, expected_attrs);
  }
  {
    std::stringstream output;
    // Block table level aggregation to make all metrics to be aggregated to server level.
    opts.priority_regex_string = "";
    PrometheusWriter writer(&output, opts);
    for (const auto& tablet : tablets) {
      ASSERT_OK(entities[tablet.first]->WriteForPrometheus(&writer, opts));
    }
    MetricEntity::AttributeMap expected_attrs;
    expected_attrs["metric_type"] = "tablet";
    // Check server aggregation. Using metric_entity_type as entity_id.
    DoAggregationCheck(writer, "tablet", METRIC_test_sum_gauge.name(), 34, expected_attrs);
    DoAggregationCheck(writer, "tablet", METRIC_test_max_gauge.name(), 10, expected_attrs);
  }
}

TEST_F(MetricsTest, SimpleHistogramTest) {
  scoped_refptr<Histogram> hist = METRIC_test_hist.Instantiate(entity_);
  hist->Increment(2);
  hist->IncrementBy(4, 1);
  ASSERT_EQ(2, hist->histogram_->MinValue());
  ASSERT_EQ(3, hist->histogram_->MeanValue());
  ASSERT_EQ(4, hist->histogram_->MaxValue());
  ASSERT_EQ(2, hist->histogram_->TotalCount());
  ASSERT_EQ(6, hist->histogram_->TotalSum());
  // TODO: Test coverage needs to be improved a lot.
}

TEST_F(MetricsTest, ResetHistogramTest) {
  scoped_refptr<Histogram> hist = METRIC_test_hist.Instantiate(entity_);
  for (int i = 1; i <= 100; i++) {
    hist->Increment(i);
  }
  EXPECT_EQ(5050, hist->histogram_->TotalSum());
  EXPECT_EQ(100, hist->histogram_->TotalCount());
  EXPECT_EQ(5050, hist->histogram_->CurrentSum());
  EXPECT_EQ(100, hist->histogram_->CurrentCount());

  EXPECT_EQ(1, hist->histogram_->MinValue());
  EXPECT_EQ(50.5, hist->histogram_->MeanValue());
  EXPECT_EQ(100, hist->histogram_->MaxValue());
  EXPECT_EQ(10, hist->histogram_->ValueAtPercentile(10));
  EXPECT_EQ(25, hist->histogram_->ValueAtPercentile(25));
  EXPECT_EQ(50, hist->histogram_->ValueAtPercentile(50));
  EXPECT_EQ(75, hist->histogram_->ValueAtPercentile(75));
  EXPECT_EQ(99, hist->histogram_->ValueAtPercentile(99));
  EXPECT_EQ(100, hist->histogram_->ValueAtPercentile(99.9));
  EXPECT_EQ(100, hist->histogram_->ValueAtPercentile(100));

  hist->histogram_->DumpHumanReadable(&LOG(INFO));
  // Test that the Histogram's percentiles are reset.
  HistogramSnapshotPB snapshot_pb;
  MetricJsonOptions options;
  options.include_raw_histograms = true;
  ASSERT_OK(hist->GetAndResetHistogramSnapshotPB(&snapshot_pb, options));
  hist->histogram_->DumpHumanReadable(&LOG(INFO));

  EXPECT_EQ(5050, hist->histogram_->TotalSum());
  EXPECT_EQ(100, hist->histogram_->TotalCount());
  EXPECT_EQ(0, hist->histogram_->CurrentSum());
  EXPECT_EQ(0, hist->histogram_->CurrentCount());

  EXPECT_EQ(0, hist->histogram_->MinValue());
  EXPECT_EQ(0, hist->histogram_->MeanValue());
  EXPECT_EQ(0, hist->histogram_->MaxValue());
  EXPECT_EQ(0, hist->histogram_->ValueAtPercentile(10));
  EXPECT_EQ(0, hist->histogram_->ValueAtPercentile(25));
  EXPECT_EQ(0, hist->histogram_->ValueAtPercentile(50));
  EXPECT_EQ(0, hist->histogram_->ValueAtPercentile(75));
  EXPECT_EQ(0, hist->histogram_->ValueAtPercentile(99));
  EXPECT_EQ(0, hist->histogram_->ValueAtPercentile(99.9));
  EXPECT_EQ(0, hist->histogram_->ValueAtPercentile(100));
}

TEST_F(MetricsTest, SimpleEventStatsTest) {
  scoped_refptr<EventStats> stats = METRIC_test_event_stats.Instantiate(entity_);
  stats->Increment(2);
  stats->IncrementBy(4, 1);
  ASSERT_EQ(2, stats->stats_->MinValue());
  ASSERT_EQ(3, stats->stats_->MeanValue());
  ASSERT_EQ(4, stats->stats_->MaxValue());
  ASSERT_EQ(2, stats->stats_->TotalCount());
  ASSERT_EQ(6, stats->stats_->TotalSum());
}

TEST_F(MetricsTest, ResetEventStatsTest) {
  scoped_refptr<EventStats> stats = METRIC_test_event_stats.Instantiate(entity_);
  for (int i = 1; i <= 100; i++) {
    stats->Increment(i);
  }
  EXPECT_EQ(5050, stats->stats_->TotalSum());
  EXPECT_EQ(100, stats->stats_->TotalCount());
  EXPECT_EQ(5050, stats->stats_->CurrentSum());
  EXPECT_EQ(100, stats->stats_->CurrentCount());

  EXPECT_EQ(1, stats->stats_->MinValue());
  EXPECT_EQ(50.5, stats->stats_->MeanValue());
  EXPECT_EQ(100, stats->stats_->MaxValue());

  // Test that the EventStat's min/mean/max are reset.
  HistogramSnapshotPB snapshot_pb;
  MetricJsonOptions options;
  options.include_raw_histograms = true;
  ASSERT_OK(stats->GetAndResetHistogramSnapshotPB(&snapshot_pb, options));

  EXPECT_EQ(5050, stats->stats_->TotalSum());
  EXPECT_EQ(100, stats->stats_->TotalCount());
  EXPECT_EQ(0, stats->stats_->CurrentSum());
  EXPECT_EQ(0, stats->stats_->CurrentCount());

  EXPECT_EQ(0, stats->stats_->MinValue());
  EXPECT_EQ(0, stats->stats_->MeanValue());
  EXPECT_EQ(0, stats->stats_->MaxValue());
}

TEST_F(MetricsTest, JsonPrintTest) {
  scoped_refptr<Counter> bytes_seen = METRIC_reqs_pending.Instantiate(entity_);
  bytes_seen->Increment();
  entity_->SetAttribute("test_attr", "attr_val");

  // Generate the JSON.
  std::stringstream out;
  JsonWriter writer(&out, JsonWriter::PRETTY);
  ASSERT_OK(entity_->WriteAsJson(&writer, MetricJsonOptions()));

  // Now parse it back out.
  JsonReader reader(out.str());
  ASSERT_OK(reader.Init());

  vector<const rapidjson::Value*> metrics;
  ASSERT_OK(reader.ExtractObjectArray(reader.root(), "metrics", &metrics));
  ASSERT_EQ(1, metrics.size());
  string metric_name;
  ASSERT_OK(reader.ExtractString(metrics[0], "name", &metric_name));
  ASSERT_EQ("reqs_pending", metric_name);
  int64_t metric_value;
  ASSERT_OK(reader.ExtractInt64(metrics[0], "value", &metric_value));
  ASSERT_EQ(1L, metric_value);

  const rapidjson::Value* attributes;
  ASSERT_OK(reader.ExtractObject(reader.root(), "attributes", &attributes));
  string attr_value;
  ASSERT_OK(reader.ExtractString(attributes, "test_attr", &attr_value));
  ASSERT_EQ("attr_val", attr_value);
}

// Test that metrics are retired when they are no longer referenced.
TEST_F(MetricsTest, RetirementTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_metrics_retirement_age_ms) = 100;

  const string kMetricName = "foo";
  scoped_refptr<Counter> counter = METRIC_reqs_pending.Instantiate(entity_);
  ASSERT_EQ(1, entity_->UnsafeMetricsMapForTests().size());

  // Since we hold a reference to the counter, it should not get retired.
  entity_->RetireOldMetrics();
  ASSERT_EQ(1, entity_->UnsafeMetricsMapForTests().size());

  // When we de-ref it, it should not get immediately retired, either, because
  // we keep retirable metrics around for some amount of time. We try retiring
  // a number of times to hit all the cases.
  counter = nullptr;
  for (int i = 0; i < 3; i++) {
    entity_->RetireOldMetrics();
    ASSERT_EQ(1, entity_->UnsafeMetricsMapForTests().size());
  }

  // If we wait for longer than the retirement time, and call retire again, we'll
  // actually retire it.
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_metrics_retirement_age_ms * 1.5));
  entity_->RetireOldMetrics();
  ASSERT_EQ(0, entity_->UnsafeMetricsMapForTests().size());
}

TEST_F(MetricsTest, TestRetiringEntities) {
  ASSERT_EQ(1, registry_.num_entities());

  // Drop the reference to our entity.
  entity_.reset();

  // Retire metrics. Since there is nothing inside our entity, it should
  // retire immediately (no need to loop).
  registry_.RetireOldMetrics();

  ASSERT_EQ(0, registry_.num_entities());
}

// Test that we can mark a metric to never be retired.
TEST_F(MetricsTest, NeverRetireTest) {
  entity_->NeverRetire(METRIC_test_hist.Instantiate(entity_));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_metrics_retirement_age_ms) = 0;

  for (int i = 0; i < 3; i++) {
    entity_->RetireOldMetrics();
    ASSERT_EQ(1, entity_->UnsafeMetricsMapForTests().size());
  }
}

TEST_F(MetricsTest, TestInstantiatingTwice) {
  // Test that re-instantiating the same entity ID returns the same object.
  scoped_refptr<MetricEntity> new_entity = METRIC_ENTITY_test_entity.Instantiate(
      &registry_, entity_->id());
  ASSERT_EQ(new_entity.get(), entity_.get());
}

TEST_F(MetricsTest, TestInstantiatingDifferentEntities) {
  scoped_refptr<MetricEntity> new_entity = METRIC_ENTITY_test_entity.Instantiate(
      &registry_, "some other ID");
  ASSERT_NE(new_entity.get(), entity_.get());
}

TEST_F(MetricsTest, TestDumpJsonPrototypes) {
  // Dump the prototype info.
  std::stringstream out;
  JsonWriter w(&out, JsonWriter::PRETTY);
  WriteRegistryAsJson(&w);
  string json = out.str();

  // Quick sanity check for one of our metrics defined in this file.
  const char* expected =
    "        {\n"
    "            \"name\": \"test_func_gauge\",\n"
    "            \"label\": \"Test Gauge\",\n"
    "            \"type\": \"gauge\",\n"
    "            \"unit\": \"bytes\",\n"
    "            \"description\": \"Test Gauge 2\",\n"
    "            \"level\": \"info\",\n"
    "            \"entity_type\": \"test_entity\"\n"
    "        }";
  ASSERT_STR_CONTAINS(json, expected);

  // Parse it.
  rapidjson::Document d;
  d.Parse<0>(json.c_str());

  // Ensure that we got a reasonable number of metrics.
  int num_metrics = d["metrics"].Size();
  int num_entities = d["entities"].Size();
  LOG(INFO) << "Parsed " << num_metrics << " metrics and " << num_entities << " entities";
  ASSERT_GT(num_metrics, 5);
  ASSERT_EQ(num_entities, 5);

  // Spot-check that some metrics were properly registered and that the JSON was properly
  // formed.
  unordered_set<string> seen_metrics;
  for (rapidjson::SizeType i = 0; i < d["metrics"].Size(); i++) {
    InsertOrDie(&seen_metrics, d["metrics"][i]["name"].GetString());
  }
  ASSERT_TRUE(ContainsKey(seen_metrics, "threads_started"));
  ASSERT_TRUE(ContainsKey(seen_metrics, "test_hist"));
}

// A basic test to verify PrometheusWriter member functions
TEST_F(MetricsTest, PrometheusWriter) {
  static const auto kLabel = "lable1";
  static const auto kLabelVal = "lable1_value";
  static const auto kTestMetricName = "test_metric_name";

  std::stringstream output;
  MetricPrometheusOptions opts;
  PrometheusWriter writer(&output, opts);

  MetricEntity::AttributeMap attr;
  attr[kLabel] = kLabelVal;

  ASSERT_OK(writer.WriteSingleEntryNonTable(attr, kTestMetricName, 1u));
  std::ostringstream expected;
  expected << kTestMetricName  << "{" << kLabel << "=\"" << kLabelVal << "\"} " << 1;
  auto pw_output = dumpPrometheusWriterOutput(writer);

  ASSERT_STR_CONTAINS(pw_output, expected.str());

  attr["table_id"] = "table_1";
  ASSERT_NOK(writer.WriteSingleEntryNonTable(attr, kTestMetricName, 1u));
}

// A test to verify Stream Level Aggregation
TEST_F(MetricsTest, TestStreamLevelAggregation) {
  static const auto kLabel = "label";
  static const auto kLabelVal = "label_value";

  static const auto kTestMetricName1 = "test_metric_name_1";
  static const auto TestMetricName2 = "test_metric_name_2";

  MetricEntity::AttributeMap attrs;
  attrs[kLabel] = kLabelVal;
  attrs["stream_id"] = "stream_id_1";
  attrs["table_id"] = "table_id_1";
  attrs["metric_type"] = kXClusterMetricEntityName;

  MetricEntity::AttributeMap expected_attrs = attrs;
  expected_attrs.erase("table_id");

  std::stringstream output;
  MetricPrometheusOptions opts;
  PrometheusWriter writer(&output, opts);
  ASSERT_OK(writer.WriteSingleEntry(attrs, kTestMetricName1, 1u,
      AggregationFunction::kMax, kStreamLevel));
  ASSERT_OK(writer.WriteSingleEntry(attrs, kTestMetricName1, 2u,
      AggregationFunction::kMax, kStreamLevel));
  DoAggregationCheck(writer, "stream_id_1", kTestMetricName1, 2, expected_attrs);

  std::stringstream output_2;
  PrometheusWriter writer_2(&output_2, opts);
  ASSERT_OK(writer_2.WriteSingleEntry(attrs, TestMetricName2, 1u,
      AggregationFunction::kSum, kStreamLevel));
  ASSERT_OK(writer_2.WriteSingleEntry(attrs, TestMetricName2, 1u,
      AggregationFunction::kSum, kStreamLevel));
  DoAggregationCheck(writer, "stream_id_1", kTestMetricName1, 2, expected_attrs);
}

int StringOccurence(const string& s, const string& target) {
  int occurence = 0;
  size_t pos = -1;
  while(true) {
    pos = s.find(target, pos+1);
    if (pos == string::npos) {
      break;
    }
    ++occurence;
  }
  return occurence;
}

METRIC_DEFINE_histogram(server, t_hist, "Test Histogram Label",
    MetricUnit::kMilliseconds, "Test histogram description", 100000000L, 2);
METRIC_DEFINE_entity(xcluster);
METRIC_DEFINE_event_stats(xcluster, t_event_stats, "Test EventStats Label",
    MetricUnit::kMilliseconds, "Test event stats description");
METRIC_DEFINE_counter(tablet, t_counter, "Test Counter Label", MetricUnit::kMilliseconds,
    "Test counter description");
METRIC_DEFINE_gauge_int32(tablet, t_gauge, "Test Gauge Label", MetricUnit::kMilliseconds,
    "Test gauge description");
METRIC_DEFINE_entity(cdcsdk);
METRIC_DEFINE_lag(cdcsdk, t_lag, "Test lag Label", "Test lag description");

// For Prometheus metric output, each metric has a #TYPE and #HELP component.
// This test validates the format and makes sure the type is correct.
TEST_F(MetricsTest, VerifyHelpAndTypeTags) {
  MetricEntity::AttributeMap entity_attr;
  entity_attr["tablet_id"] = "tablet_id_40";
  entity_attr["table_name"] = "test_table";
  entity_attr["table_id"] = "table_id_41";
  auto tablet_entity =
      METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet_entity_id_44", entity_attr);
  auto server_entity = METRIC_ENTITY_server.Instantiate(&registry_, "server_entity_id_45");
  entity_attr["stream_id"] = "stream_id_46";
  auto xcluster_entity =
      METRIC_ENTITY_xcluster.Instantiate(&registry_, "xcluster_entity_id_47", entity_attr);
  auto cdcsdk_entity =
      METRIC_ENTITY_cdcsdk.Instantiate(&registry_, "cdcsdk_entity_id_48", entity_attr);

  scoped_refptr<Gauge> gauge = METRIC_t_gauge.Instantiate(tablet_entity, 0);
  scoped_refptr<Counter> counter = METRIC_t_counter.Instantiate(tablet_entity);
  scoped_refptr<Histogram> hist = METRIC_t_hist.Instantiate(server_entity);
  scoped_refptr<EventStats> event_stats = METRIC_t_event_stats.Instantiate(xcluster_entity);
  scoped_refptr<MillisLag> lag = METRIC_t_lag.Instantiate(cdcsdk_entity);

  MetricPrometheusOptions opts;
  opts.export_help_and_type = ExportHelpAndType::kTrue;
  std::stringstream output;
  PrometheusWriter writer(&output, opts);
  ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));

  string output_str = output.str();
  // Check histogram output.
  EXPECT_EQ(1, StringOccurence(output_str,
      "# HELP t_hist_sum Test histogram description\n# TYPE t_hist_sum counter"));
  EXPECT_EQ(1, StringOccurence(output_str,
      "# HELP t_hist_count Test histogram description\n# TYPE t_hist_count counter"));
  EXPECT_EQ(6, StringOccurence(output_str,
      "# HELP t_hist Test histogram description\n# TYPE t_hist gauge"));
  // Check coarse histogram output.
  EXPECT_EQ(1, StringOccurence(output_str,
      "# HELP t_event_stats_sum Test event stats description\n"
      "# TYPE t_event_stats_sum counter"));
  EXPECT_EQ(1, StringOccurence(output_str,
      "# HELP t_event_stats_count Test event stats description\n"
      "# TYPE t_event_stats_count counter"));
  // Check gauge output.
  EXPECT_EQ(1, StringOccurence(output_str,
      "# HELP t_gauge Test gauge description\n# TYPE t_gauge gauge"));
  // Check counter output.
  EXPECT_EQ(1, StringOccurence(output_str,
      "# HELP t_counter Test counter description\n# TYPE t_counter counter"));
  // Check lag output.
  EXPECT_EQ(1, StringOccurence(output_str,
      "# HELP t_lag Test lag description\n# TYPE t_lag gauge"));
}

TEST_F(MetricsTest, SimulateMetricDeletionBeforeFlush) {
  const std::string kDescription = "gauge description";

  MetricEntity::AttributeMap entity_attr;
  entity_attr["tablet_id"] = "tablet_id_49";
  entity_attr["table_name"] = "test_table";
  entity_attr["table_id"] = "table_id_50";
  auto tablet_entity =
      METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet_entity_id_51", entity_attr);
  scoped_refptr<AtomicGauge<int64_t>> gauge =
      tablet_entity->FindOrCreateMetric<AtomicGauge<int64_t>>(
        std::unique_ptr<GaugePrototype<int64_t>>(new OwningGaugePrototype<int64_t>(
            tablet_entity->prototype().name(), "t_gauge",
            kDescription, MetricUnit::kBytes,
            kDescription, yb::MetricLevel::kInfo)),
        static_cast<int64_t>(0));

  SetAtomicFlag(true, &FLAGS_TEST_pause_flush_aggregated_metrics);
  std::thread metric_deletion_thread([&]{
    // Simulate metric deletion in the middle of WriteForPrometheus.
    std::this_thread::sleep_for(2s);
    tablet_entity->Remove(gauge->prototype());
    gauge.reset(nullptr);
    SetAtomicFlag(false, &FLAGS_TEST_pause_flush_aggregated_metrics);
  });

  MetricPrometheusOptions opts;
  opts.export_help_and_type = ExportHelpAndType::kTrue;
  {
    std::stringstream output;
    PrometheusWriter writer(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    metric_deletion_thread.join();
    // Check that the metric is still present in the output.
    ASSERT_NE(output.str().find(kDescription), string::npos);
  }
  {
    // Pull the metric again and check that it is not present in the output.
    std::stringstream output;
    PrometheusWriter writer(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    ASSERT_EQ(output.str().find(kDescription), string::npos);
  }
}

} // namespace yb
