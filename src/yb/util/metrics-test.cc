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
#include <unordered_set>
#include <vector>

#include "yb/util/logging.h"
#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include "yb/gutil/bind.h"
#include "yb/gutil/map-util.h"

#include "yb/util/hdr_histogram.h"
#include "yb/util/histogram.pb.h"
#include "yb/util/json_document.h"
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

METRIC_DEFINE_entity(cdcsdk);
METRIC_DEFINE_entity(table);
METRIC_DEFINE_entity(tablet);
METRIC_DEFINE_entity(test_entity);
METRIC_DEFINE_entity(xcluster);

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

  void DoAttributesCheck(
      const MetricEntity::AttributeMap& expected_attributes,
      const MetricEntity::AttributeMap& actual_attributes) {
    // To simplify unit test, allow actual attributes to have more entries than expected,
    // because actual attributes may have additional Prometheus entries.
    for (const auto& expected_attribute : expected_attributes) {
      auto actual_attribute_it = actual_attributes.find(expected_attribute.first);
      ASSERT_NE(actual_attribute_it, actual_attributes.end());
      ASSERT_EQ(actual_attribute_it->second, expected_attribute.second);
    }
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

METRIC_DEFINE_gauge_int32(tablet, test_sum_gauge, "Test Sum Gauge", MetricUnit::kMilliseconds,
                          "Test Gauge with SUM aggregation.");
METRIC_DEFINE_gauge_int32(tablet, test_max_gauge, "Test Max Gauge", MetricUnit::kMilliseconds,
                          "Test Gauge with MAX aggregation.",
                          {0, yb::AggregationFunction::kMax} /* optional_args */);
METRIC_DEFINE_counter(tablet, test_sum_counter, "Test Sum Counter", MetricUnit::kMilliseconds,
                          "Test Counter with SUM aggregation.");

TEST_F(MetricsTest, AggregationTest) {
  std::map<std::string, scoped_refptr<MetricEntity>> entities;
  auto AddTabletEntityAndSumMaxMetrics =
      [&](const std::string& tablet_id, const std::string& table_id, int metric_value) {
    MetricEntity::AttributeMap entity_attr;
    entity_attr["tablet_id"] = tablet_id + "_id";
    entity_attr["table_name"] = table_id;
    entity_attr["table_id"] = table_id + "_id";
    auto entity = METRIC_ENTITY_tablet.Instantiate(&registry_, tablet_id, entity_attr);

    // Test SUM aggregation
    auto sum_gauge = METRIC_test_sum_gauge.Instantiate(entity, 0 /* initial_value */);
    // Test MAX aggregation
    auto max_gauge = METRIC_test_max_gauge.Instantiate(entity, 0 /* initial_value */);
    // Test SUM Counter
    auto sum_counter = METRIC_test_sum_counter.Instantiate(entity);

    sum_gauge->set_value(metric_value);
    max_gauge->set_value(metric_value);
    sum_counter->IncrementBy(metric_value);

    entities.insert({tablet_id, entity});
  };
  AddTabletEntityAndSumMaxMetrics("tablet_1", "table_1", /* metric_value = */10);
  AddTabletEntityAndSumMaxMetrics("tablet_2", "table_1", /* metric_value = */9);
  AddTabletEntityAndSumMaxMetrics("tablet_3", "table_2", /* metric_value = */8);
  AddTabletEntityAndSumMaxMetrics("tablet_4", "table_2", /* metric_value = */7);

  auto CheckPreAggreatedAndScrapeTimeValues = [&](
      PrometheusWriter& writer,
      const std::string& metric_name,
      const std::string& aggregation_id,
      std::optional<int64_t> expected_pre_aggregated_value,
      std::optional<int64_t> expected_scrape_time_aggregated_value) {
    ASSERT_EQ(registry_.TEST_metrics_aggregator()
        ->TEST_GetMetricPreAggregatedValue(metric_name, aggregation_id),
        expected_pre_aggregated_value);
    ASSERT_EQ(writer.TEST_GetScrapeTimeAggregatedValue(metric_name, aggregation_id),
        expected_scrape_time_aggregated_value);
  };

  auto CheckPreStoredAndScrapeTimeAttributes = [&](
      PrometheusWriter& writer,
      const std::string& aggregation_id,
      std::optional<MetricEntity::AttributeMap> expected_pre_stored_attributes,
      std::optional<MetricEntity::AttributeMap> expected_scrape_time_attributes) {
    auto actual_pre_stored_attributes = registry_.TEST_metrics_aggregator()
        ->TEST_GetAttributesForAggregationId("tablet", aggregation_id);
    auto actual_scrape_time_attributes =
        writer.TEST_GetAttributesForAggregationId("tablet", aggregation_id);

    auto CheckAttributes = [&](const auto& actual, const auto& expected) {
      if (expected.has_value()) {
        ASSERT_TRUE(actual.has_value());
        DoAttributesCheck(expected.value(), actual.value());
      } else {
        ASSERT_FALSE(actual.has_value());
      }
    };

    CheckAttributes(actual_pre_stored_attributes, expected_pre_stored_attributes);
    CheckAttributes(actual_scrape_time_attributes, expected_scrape_time_attributes);
  };

  const auto kSumGaugeName = METRIC_test_sum_gauge.name();
  const auto kMaxGaugeName = METRIC_test_max_gauge.name();
  const auto kSumCounterName = METRIC_test_sum_counter.name();
  MetricPrometheusOptions opts;
  std::stringstream output;
  {
    // Check table level aggregation
    PrometheusWriter writer(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));

    CheckPreAggreatedAndScrapeTimeValues(writer, kSumGaugeName, "table_1_id", 19, std::nullopt);
    CheckPreAggreatedAndScrapeTimeValues(writer, kMaxGaugeName, "table_1_id", std::nullopt, 10);
    CheckPreAggreatedAndScrapeTimeValues(writer, kSumCounterName, "table_1_id", 19, std::nullopt);
    MetricEntity::AttributeMap attributes;
    attributes["metric_type"] = "tablet";
    attributes["table_id"] = "table_1_id";
    attributes["table_name"] = "table_1";
    CheckPreStoredAndScrapeTimeAttributes(writer, "table_1_id", attributes, attributes);

    CheckPreAggreatedAndScrapeTimeValues(writer, kSumGaugeName, "table_2_id", 15, std::nullopt);
    CheckPreAggreatedAndScrapeTimeValues(writer, kMaxGaugeName, "table_2_id", std::nullopt, 8);
    CheckPreAggreatedAndScrapeTimeValues(writer, kSumCounterName, "table_2_id", 15, std::nullopt);
    attributes["table_id"] = "table_2_id";
    attributes["table_name"] = "table_2";
    CheckPreStoredAndScrapeTimeAttributes(writer, "table_2_id", attributes, attributes);
  }
  {
    // Check server level aggregation
    opts.priority_regex_string = "";
    PrometheusWriter writer(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    CheckPreAggreatedAndScrapeTimeValues(
        writer, kSumGaugeName, kServerLevelAggregationId, 34, std::nullopt);
    CheckPreAggreatedAndScrapeTimeValues(
        writer, kMaxGaugeName, kServerLevelAggregationId, std::nullopt, 10);
    CheckPreAggreatedAndScrapeTimeValues(
        writer, kSumCounterName, kServerLevelAggregationId, 34, std::nullopt);
    MetricEntity::AttributeMap attributes;
    attributes["metric_type"] = "tablet";
    CheckPreStoredAndScrapeTimeAttributes(
        writer, kServerLevelAggregationId, attributes, attributes);
  }
  {
    // Check pre-aggregated values of metrics are maintain correctly after
    // dropping and adding tablet metric entity.

    // Simulate dropping a tablet metric entity.
    entities["tablet_1"]->RemoveFromMetricMap(&METRIC_test_sum_gauge);
    entities["tablet_1"]->RemoveFromMetricMap(&METRIC_test_max_gauge);
    entities["tablet_1"]->RemoveFromMetricMap(&METRIC_test_sum_counter);

    opts.priority_regex_string = ".*";
    PrometheusWriter writer(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    CheckPreAggreatedAndScrapeTimeValues(writer, kSumGaugeName, "table_1_id", 9, std::nullopt);
    CheckPreAggreatedAndScrapeTimeValues(writer, kMaxGaugeName, "table_1_id", std::nullopt, 9);
    CheckPreAggreatedAndScrapeTimeValues(writer, kSumCounterName, "table_1_id", 9, std::nullopt);
    MetricEntity::AttributeMap attributes;
    attributes["metric_type"] = "tablet";
    attributes["table_id"] = "table_1_id";
    attributes["table_name"] = "table_1";
    CheckPreStoredAndScrapeTimeAttributes(writer, "table_1_id", attributes, attributes);

    // Simulate adding a tablet metric entity.
    AddTabletEntityAndSumMaxMetrics("tablet_5", "table_1", /* metric_value = */11);

    PrometheusWriter writer2(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer2, opts));
    CheckPreAggreatedAndScrapeTimeValues(writer2, kSumGaugeName, "table_1_id", 20, std::nullopt);
    CheckPreAggreatedAndScrapeTimeValues(writer2, kMaxGaugeName, "table_1_id", std::nullopt, 11);
    CheckPreAggreatedAndScrapeTimeValues(writer2, kSumCounterName, "table_1_id", 20, std::nullopt);
    CheckPreStoredAndScrapeTimeAttributes(writer2, "table_1_id", attributes, attributes);

    // Finally, bump the values and verify.
    auto sum_gauge_ptr =
        entities["tablet_5"]->FindOrNull<AtomicGauge<int32_t>>(METRIC_test_sum_gauge);
    ASSERT_NE(sum_gauge_ptr, nullptr);
    sum_gauge_ptr->IncrementBy(1);
    auto max_gauge_ptr =
        entities["tablet_5"]->FindOrNull<AtomicGauge<int32_t>>(METRIC_test_max_gauge);
    ASSERT_NE(max_gauge_ptr, nullptr);
    max_gauge_ptr->IncrementBy(1);
    auto sum_counter_ptr =
        entities["tablet_5"]->FindOrNull<Counter>(METRIC_test_sum_counter);
    ASSERT_NE(sum_counter_ptr, nullptr);
    sum_counter_ptr->IncrementBy(1);

    PrometheusWriter writer3(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer3, opts));
    CheckPreAggreatedAndScrapeTimeValues(writer3, kSumGaugeName, "table_1_id", 21, std::nullopt);
    CheckPreAggreatedAndScrapeTimeValues(writer3, kMaxGaugeName, "table_1_id", std::nullopt, 12);
    CheckPreAggreatedAndScrapeTimeValues(writer3, kSumCounterName, "table_1_id", 21, std::nullopt);
    CheckPreStoredAndScrapeTimeAttributes(writer3, "table_1_id", attributes, attributes);
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
  JsonDocument doc;
  auto metrics = ASSERT_RESULT(doc.Parse(out.str()))["metrics"];

  auto size = ASSERT_RESULT(metrics.size());
  ASSERT_EQ(1, size);
  auto metric_name = ASSERT_RESULT(metrics[0]["name"].GetString());
  ASSERT_EQ("reqs_pending", metric_name);
  auto metric_value = ASSERT_RESULT(metrics[0]["value"].GetInt64());
  ASSERT_EQ(1L, metric_value);

  auto attributes = doc.Root()["attributes"];
  auto attr_value = ASSERT_RESULT(attributes["test_attr"].GetString());
  ASSERT_EQ("attr_val", attr_value);
}

// Test that metrics are retired when they are no longer referenced.
TEST_F(MetricsTest, RetirementTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_metrics_retirement_age_ms) = 100;

  const string kMetricName = "foo";
  scoped_refptr<Counter> counter = METRIC_reqs_pending.Instantiate(entity_);
  ASSERT_EQ(1, entity_->TEST_UsageMetricsMap().size());

  // Since we hold a reference to the counter, it should not get retired.
  entity_->RetireOldMetrics();
  ASSERT_EQ(1, entity_->TEST_UsageMetricsMap().size());

  // When we de-ref it, it should not get immediately retired, either, because
  // we keep retirable metrics around for some amount of time. We try retiring
  // a number of times to hit all the cases.
  counter = nullptr;
  for (int i = 0; i < 3; i++) {
    entity_->RetireOldMetrics();
    ASSERT_EQ(1, entity_->TEST_UsageMetricsMap().size());
  }

  // If we wait for longer than the retirement time, and call retire again, we'll
  // actually retire it.
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_metrics_retirement_age_ms * 1.5));
  entity_->RetireOldMetrics();
  ASSERT_EQ(0, entity_->TEST_UsageMetricsMap().size());
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
    ASSERT_EQ(1, entity_->TEST_UsageMetricsMap().size());
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
  ASSERT_EQ(num_entities, 6);

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

  static const auto kTestMetricName = "test_metric_name";

  MetricEntity::AttributeMap attributes;
  attributes[kLabel] = kLabelVal;
  attributes["stream_id"] = "stream_id_1";
  attributes["table_id"] = "table_id_1";
  attributes["metric_type"] = kXClusterMetricEntityName;

  MetricEntity::AttributeMap expected_attributes = attributes;
  expected_attributes.erase("table_id");

  std::stringstream output;
  MetricPrometheusOptions opts;
  PrometheusWriter writer(&output, opts);
  ASSERT_OK(writer.WriteSingleEntry(attributes, kTestMetricName, 1u,
      AggregationFunction::kMax, kStreamLevel, kXClusterMetricEntityName));
  ASSERT_OK(writer.WriteSingleEntry(attributes, kTestMetricName, 2u,
      AggregationFunction::kMax, kStreamLevel, kXClusterMetricEntityName));
  auto actual_value = writer.TEST_GetScrapeTimeAggregatedValue(kTestMetricName, "stream_id_1");
  ASSERT_EQ(actual_value.value(), 2);

  auto actual_attributes =
      writer.TEST_GetAttributesForAggregationId(kXClusterMetricEntityName, "stream_id_1");
  ASSERT_TRUE(actual_attributes.has_value());
  DoAttributesCheck(expected_attributes, actual_attributes.value());

  std::stringstream output_2;
  PrometheusWriter writer_2(&output_2, opts);
  ASSERT_OK(writer_2.WriteSingleEntry(attributes, kTestMetricName, 1u,
      AggregationFunction::kSum, kStreamLevel, kXClusterMetricEntityName));
  ASSERT_OK(writer_2.WriteSingleEntry(attributes, kTestMetricName, 1u,
      AggregationFunction::kSum, kStreamLevel, kXClusterMetricEntityName));

  actual_value = writer_2.TEST_GetScrapeTimeAggregatedValue(kTestMetricName, "stream_id_1");
  ASSERT_EQ(actual_value.value(), 2);

  actual_attributes =
      writer_2.TEST_GetAttributesForAggregationId(kXClusterMetricEntityName, "stream_id_1");
  ASSERT_TRUE(actual_attributes.has_value());
  DoAttributesCheck(expected_attributes, actual_attributes.value());
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
METRIC_DEFINE_event_stats(xcluster, t_event_stats, "Test EventStats Label",
    MetricUnit::kMilliseconds, "Test event stats description");
METRIC_DEFINE_counter(tablet, t_counter, "Test Counter Label", MetricUnit::kMilliseconds,
    "Test counter description");
METRIC_DEFINE_gauge_int32(tablet, t_gauge, "Test Gauge Label", MetricUnit::kMilliseconds,
    "Test gauge description");
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
  const std::string kTableGaugeName = "table_gauge_name";
  const std::string kTabletGaugeName = "tablet_gauge_name";

  MetricEntity::AttributeMap entity_attr;
  // Create table entity with a table metric.
  entity_attr["table_name"] = "test_table";
  entity_attr["table_id"] = "table_id_50";
  auto table_entity =
      METRIC_ENTITY_table.Instantiate(&registry_, "table_entity_id_51", entity_attr);
  scoped_refptr<AtomicGauge<int64_t>> table_gauge =
      table_entity->FindOrCreateMetric<AtomicGauge<int64_t>>(
        std::shared_ptr<GaugePrototype<int64_t>>(new OwningGaugePrototype<int64_t>(
            table_entity->prototype().name(), kTableGaugeName,
            "label", MetricUnit::kBytes, "description", yb::MetricLevel::kInfo)),
        static_cast<int64_t>(0));
  // Create tablet entity with a tablet metric.
  entity_attr["tablet_id"] = "tablet_id_49";
  auto tablet_entity =
      METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet_entity_id_51", entity_attr);
  scoped_refptr<AtomicGauge<int64_t>> tablet_gauge =
      tablet_entity->FindOrCreateMetric<AtomicGauge<int64_t>>(
        std::unique_ptr<GaugePrototype<int64_t>>(new OwningGaugePrototype<int64_t>(
            tablet_entity->prototype().name(), kTabletGaugeName,
            "label", MetricUnit::kBytes, "description", yb::MetricLevel::kInfo)),
        static_cast<int64_t>(0));

  MetricPrometheusOptions opts;
  opts.export_help_and_type = ExportHelpAndType::kTrue;
  {
    // Verify that metrics are presented in the scrape output.
    std::stringstream output;
    PrometheusWriter writer(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    ASSERT_NE(output.str().find(kTableGaugeName), string::npos);
    ASSERT_NE(output.str().find(kTabletGaugeName), string::npos);
    ASSERT_TRUE(registry_.TEST_metrics_aggregator()->IsPreAggregatedMetric(kTabletGaugeName));
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_flush_aggregated_metrics) = true;
  std::thread metric_deletion_thread([&]{
    // Simulate metric deletion in the middle of WriteForPrometheus.
    std::this_thread::sleep_for(2s);
    tablet_entity->RemoveFromMetricMap(tablet_gauge->prototype());
    table_entity->RemoveFromMetricMap(table_gauge->prototype());
    table_entity->RetireOldMetrics();
    tablet_gauge.reset(nullptr);
    table_gauge.reset(nullptr);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_flush_aggregated_metrics) = false;
  });
  {
    // Verify that metrics are absent in the scrape output after deletion.
    std::stringstream output;
    PrometheusWriter writer(&output, opts);
    ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));
    metric_deletion_thread.join();
    ASSERT_EQ(output.str().find(kTableGaugeName), string::npos);
    ASSERT_EQ(output.str().find(kTabletGaugeName), string::npos);
    // Ensure the background metric cleanup thread removes the tablet metric from the aggregator.
    std::this_thread::sleep_for(2s);
    ASSERT_FALSE(registry_.TEST_metrics_aggregator()->IsPreAggregatedMetric(kTabletGaugeName));
  }
}

TEST_F(MetricsTest, VerifyLabelValueEscaping) {
  // Test escaping of quotes in table names
  MetricEntity::AttributeMap entity_attr1;
  entity_attr1["tablet_id"] = "tablet_id_52";
  entity_attr1["table_name"] = "\"yo\".\"name_split\"";
  entity_attr1["table_id"] = "table_id_53";
  auto tablet_entity1 =
      METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet_entity_id_54", entity_attr1);
  scoped_refptr<Counter> counter1 = METRIC_t_counter.Instantiate(tablet_entity1);

  // Test escaping of backslashes
  MetricEntity::AttributeMap entity_attr2;
  entity_attr2["tablet_id"] = "tablet_id_55";
  entity_attr2["table_name"] = "path\\to\\table";
  entity_attr2["table_id"] = "table_id_56";
  auto tablet_entity2 =
      METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet_entity_id_57", entity_attr2);
  scoped_refptr<Counter> counter2 = METRIC_t_counter.Instantiate(tablet_entity2);

  // Test escaping of newlines
  MetricEntity::AttributeMap entity_attr3;
  entity_attr3["tablet_id"] = "tablet_id_58";
  entity_attr3["table_name"] = "line1\nline2";
  entity_attr3["table_id"] = "table_id_59";
  auto tablet_entity3 =
      METRIC_ENTITY_tablet.Instantiate(&registry_, "tablet_entity_id_60", entity_attr3);
  scoped_refptr<Counter> counter3 = METRIC_t_counter.Instantiate(tablet_entity3);

  MetricPrometheusOptions opts;
  std::stringstream output;
  PrometheusWriter writer(&output, opts);
  ASSERT_OK(registry_.WriteForPrometheus(&writer, opts));

  string output_str = output.str();

  // Quotes should be escaped
  ASSERT_STR_CONTAINS(output_str, "table_name=\"\\\"yo\\\".\\\"name_split\\\"\"");
  ASSERT_STR_NOT_CONTAINS(output_str, "table_name=\"\"yo\".\"name_split\"\"");

  // Backslashes should be escaped
  ASSERT_STR_CONTAINS(output_str, "table_name=\"path\\\\to\\\\table\"");

  // Newlines should be escaped
  ASSERT_STR_CONTAINS(output_str, "table_name=\"line1\\nline2\"");
}

} // namespace yb
