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

#include <glog/logging.h>
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

DECLARE_int32(metrics_retirement_age_ms);

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
                          const string& table,
                         const string& name,
                         int expected_aggregation,
                         const MetricEntity::AttributeMap& expected_attrs) {
    auto attrs_it = writer.aggregated_attributes_.find(table);
    ASSERT_NE(attrs_it, writer.aggregated_attributes_.end());
    for (const auto& attr : expected_attrs) {
      auto attr_it = attrs_it->second.find(attr.first);
      ASSERT_NE(attr_it, attrs_it->second.end());
      ASSERT_EQ(attr_it->second, attr.second);
    }
    auto metric_it = writer.aggregated_values_.find(name);
    ASSERT_NE(metric_it, writer.aggregated_values_.end());
    auto value_it = metric_it->second.find(table);
    ASSERT_NE(value_it, metric_it->second.end());
    ASSERT_EQ(value_it->second, expected_aggregation);
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
  const pair<string, string> tablets[] = {{"tablet1", "table1"},
                                          {"tablet2", "table1"},
                                          {"tablet3", "table2"},
                                          {"tablet4", "table2"}};
  std::map<std::string, scoped_refptr<MetricEntity>> entities;
  vector<scoped_refptr<Gauge>> gauges;
  int counter = 10;
  for (const auto& tablet : tablets) {
    MetricEntity::AttributeMap entity_attr;
    entity_attr["tablet_id"] = tablet.first;
    entity_attr["table_name"] = tablet.second;
    entity_attr["table_id"] = tablet.second;
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
  {
    MetricPrometheusOptions opts;
    std::stringstream output;
    PrometheusWriter writer(&output, ExportHelpAndType::kFalse, AggregationMetricLevel::kTable);
    MetricEntityOptions entity_options;
    entity_options.metrics.push_back("*");
    for (const auto& tablet : tablets) {
      ASSERT_OK(entities[tablet.first]->WriteForPrometheus(&writer, entity_options, opts));
    }
    MetricEntity::AttributeMap attrs;
    attrs["table_id"] = "table1";
    DoAggregationCheck(writer, "table1", METRIC_test_sum_gauge.name(), 19, attrs);
    DoAggregationCheck(writer, "table1", METRIC_test_max_gauge.name(), 10, attrs);
    attrs["table_id"] = "table2";
    DoAggregationCheck(writer, "table2", METRIC_test_sum_gauge.name(), 15, attrs);
    DoAggregationCheck(writer, "table2", METRIC_test_max_gauge.name(), 8, attrs);
  }
  {
    MetricPrometheusOptions opts;
    std::stringstream output;
    PrometheusWriter writer(&output, ExportHelpAndType::kFalse, AggregationMetricLevel::kServer);
    MetricEntityOptions entity_options;
    entity_options.metrics.push_back("*");
    for (const auto& tablet : tablets) {
      ASSERT_OK(entities[tablet.first]->WriteForPrometheus(&writer, entity_options, opts));
    }
    DoAggregationCheck(writer, "", METRIC_test_sum_gauge.name(), 34, {});
    DoAggregationCheck(writer, "", METRIC_test_max_gauge.name(), 10, {});
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
  MetricEntityOptions entity_opts;
  entity_opts.metrics.push_back("*");
  ASSERT_OK(entity_->WriteAsJson(&writer, entity_opts, MetricJsonOptions()));

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

  // Verify that, if we filter for a metric that isn't in this entity, we get no result.
  out.str("");
  entity_opts.metrics = { "not_a_matching_metric" };
  ASSERT_OK(entity_->WriteAsJson(&writer, entity_opts, MetricJsonOptions()));
  ASSERT_EQ("", out.str());
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
  ASSERT_EQ(num_entities, 3);

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
  static const auto LABLE_1 = "lable1";
  static const auto LABLE_1_VAL = "lable1_value";
  static const auto TEST_METRIC_NAME = "test_metric_name";
  static const int ONCE = 1;

  std::stringstream output;
  PrometheusWriter writer(&output, ExportHelpAndType::kFalse);

  MetricEntity::AttributeMap attr;
  attr[LABLE_1] = LABLE_1_VAL;

  ASSERT_OK(writer.WriteSingleEntryNonTable(attr, TEST_METRIC_NAME, 1u));
  std::ostringstream expected;
  expected << TEST_METRIC_NAME << "{" << LABLE_1 << "=\"" << LABLE_1_VAL << "\"} " << ONCE;
  auto pw_output = dumpPrometheusWriterOutput(writer);

  ASSERT_STR_CONTAINS(pw_output, expected.str());

  attr["table_id"] = "table_1";
  ASSERT_NOK(writer.WriteSingleEntryNonTable(attr, TEST_METRIC_NAME, 1u));
}

// A test to verify Stream Level Aggregation
TEST_F(MetricsTest, TestStreamLevelAggregation) {
  static const auto LABEL = "label";
  static const auto LABEL_VAL = "label_value";

  static const auto TEST_METRIC_NAME_1 = "test_metric_name_1";
  static const auto TEST_METRIC_NAME_2 = "test_metric_name_2";
  static const std::string TWO = "2";

  std::stringstream output;
  PrometheusWriter writer(&output, ExportHelpAndType::kFalse, AggregationMetricLevel::kStream);

  MetricEntity::AttributeMap attr;
  attr[LABEL] = LABEL_VAL;
  attr["stream_id"] = "stream_1";
  attr["table_id"] = "table_1";

  ASSERT_OK(writer.WriteSingleEntry(attr, TEST_METRIC_NAME_1, 1u, AggregationFunction::kMax));
  ASSERT_OK(writer.WriteSingleEntry(attr, TEST_METRIC_NAME_1, 2u, AggregationFunction::kMax));
  ASSERT_OK(writer.FlushAggregatedValues(1, TEST_METRIC_NAME_1));
  std::vector<std::string> patterns1 = {
      TEST_METRIC_NAME_1, "stream_id=\"stream_1\"", Format("$0=\"$1\"", LABEL, LABEL_VAL), TWO};
  auto pw_output = dumpPrometheusWriterOutput(writer);
  auto verify_patterns = [&pw_output](std::vector<std::string>& patterns) {
    for (const auto& pattern : patterns) {
      ASSERT_TRUE(pw_output.find(pattern) != string::npos);
    }
  };
  verify_patterns(patterns1);

  std::stringstream output_2;
  PrometheusWriter writer_2(&output_2, ExportHelpAndType::kFalse, AggregationMetricLevel::kStream);

  ASSERT_OK(writer_2.WriteSingleEntry(attr, TEST_METRIC_NAME_2, 1u, AggregationFunction::kSum));
  ASSERT_OK(writer_2.WriteSingleEntry(attr, TEST_METRIC_NAME_2, 1u, AggregationFunction::kSum));
  ASSERT_OK(writer_2.FlushAggregatedValues(1, TEST_METRIC_NAME_2));
  std::vector<std::string> patterns2 = {
      TEST_METRIC_NAME_2, "stream_id=\"stream_1\"", Format("$0=\"$1\"", LABEL, LABEL_VAL), TWO};
  pw_output = dumpPrometheusWriterOutput(writer_2);
  verify_patterns(patterns2);
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
METRIC_DEFINE_event_stats(server, t_event_stats, "Test EventStats Label",
    MetricUnit::kMilliseconds, "Test event stats description");
METRIC_DEFINE_counter(tablet, t_counter, "Test Counter Label", MetricUnit::kMilliseconds,
    "Test counter description");
METRIC_DEFINE_gauge_int32(tablet, t_gauge, "Test Gauge Label", MetricUnit::kMilliseconds,
    "Test gauge description");
METRIC_DEFINE_lag(server, t_lag, "Test lag Label", "Test lag description");

// For Prometheus metric output, each metric has a #TYPE and #HELP component.
// This test validates the format and makes sure the type is correct.
TEST_F(MetricsTest, VerifyHelpAndTypeTags) {
  std::map<std::string, scoped_refptr<MetricEntity>> entities;
  MetricEntity::AttributeMap entity_attr;
  entity_attr["tablet_id"] = "tablet";
  entity_attr["table_name"] = "test_table";
  entity_attr["table_id"] = "table";
  auto tablet_entity = METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet", entity_attr);
  auto server_entity = METRIC_ENTITY_server.Instantiate(&registry_, "server");

  scoped_refptr<Gauge> gauge = METRIC_t_gauge.Instantiate(tablet_entity, 0);
  scoped_refptr<Counter> counter = METRIC_t_counter.Instantiate(tablet_entity);
  scoped_refptr<Histogram> hist = METRIC_t_hist.Instantiate(server_entity);
  scoped_refptr<EventStats> event_stats = METRIC_t_event_stats.Instantiate(server_entity);
  scoped_refptr<MillisLag> lag = METRIC_t_lag.Instantiate(server_entity);

  entities.insert({"tablet", tablet_entity});
  entities.insert({"server", server_entity});
  MetricPrometheusOptions opts;
  opts.max_tables_metrics_breakdowns = INT32_MAX;
  std::stringstream output;
  PrometheusWriter writer(&output, ExportHelpAndType::kTrue, AggregationMetricLevel::kTable);
  MetricEntityOptions entity_options;
  entity_options.metrics.push_back("*");
  ASSERT_OK(registry_.WriteForPrometheus(&writer, entity_options, opts));

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

} // namespace yb
