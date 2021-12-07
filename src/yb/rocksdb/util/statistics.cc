//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
#include "yb/rocksdb/util/statistics.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <algorithm>
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/port/likely.h"
#include "yb/util/format.h"
#include "yb/util/hdr_histogram.h"
#include "yb/util/metrics.h"

namespace rocksdb {

using yb::GaugePrototype;
using yb::HistogramPrototype;
using yb::MetricEntity;
using yb::MetricRegistry;
using yb::MetricPrototype;

std::shared_ptr<Statistics> CreateDBStatistics(
    const scoped_refptr<MetricEntity>& hist_entity,
    const scoped_refptr<MetricEntity>& tick_entity,
    const bool for_intents) {
  return std::make_shared<StatisticsMetricImpl>(hist_entity, tick_entity, for_intents);
}

std::shared_ptr<Statistics> CreateDBStatisticsForTests(bool for_intents) {
  static MetricRegistry metric_registry;
  static yb::MetricEntityPrototype prototype_table("table"), prototype_tablet("tablet");
  static scoped_refptr<MetricEntity> table_metric_entity(
          prototype_table.Instantiate(&metric_registry, "table-entity-for-tests"));
  static scoped_refptr<MetricEntity> tablet_metric_entity(
          prototype_tablet.Instantiate(&metric_registry, "tablet-entity-for-tests"));
  auto stats = CreateDBStatistics(table_metric_entity, tablet_metric_entity, for_intents);
  stats->resetTickersForTest();
  return stats;
}

class StatisticsMetricPrototypes {
 public:
  StatisticsMetricPrototypes() {
    const auto kNumHistograms = HistogramsNameMap.size();
    const auto kNumTickers = TickersNameMap.size();
    // Metrics use a map based on the metric prototype's address.
    // We reserve the capacity apriori so that the elements are not moved around.
    metric_names_.reserve(kNumHistograms + 2 * kNumTickers);
    descriptions_.reserve(kNumHistograms + 2 * kNumTickers);

    for (size_t i = 0; i < kNumHistograms; i++) {
      CHECK_EQ(i, HistogramsNameMap[i].first);
      metric_names_.emplace_back(HistogramsNameMap[i].second);
      const auto& hist_name = metric_names_.back();

      descriptions_.emplace_back(yb::Format("Per-table Rocksdb Histogram for $0", hist_name));
      const auto& description = descriptions_.back();

      hist_prototypes_.emplace_back(std::make_unique<HistogramPrototype>(
          ::yb::MetricPrototype::CtorArgs(
              "table", hist_name.c_str(), description.c_str(), yb::MetricUnit::kMicroseconds,
              description.c_str(), yb::MetricLevel::kInfo),
          2, 1));
    }

    for (size_t i = 0; i < kNumTickers; i++) {
      CHECK_EQ(i, TickersNameMap[i].first);
      metric_names_.emplace_back(TickersNameMap[i].second);
      const auto& ticker_name = metric_names_.back();

      descriptions_.emplace_back(
          yb::Format("Per-tablet Regular Rocksdb Ticker for $0", ticker_name));
      const auto& description = descriptions_.back();

      regular_ticker_prototypes_.emplace_back(
          std::make_unique<GaugePrototype<uint64_t>>(::yb::MetricPrototype::CtorArgs(
              "tablet", ticker_name.c_str(), description.c_str(), yb::MetricUnit::kRequests,
              description.c_str(), yb::MetricLevel::kInfo)));
    }

    for (size_t i = 0; i < kNumTickers; i++) {
      CHECK_EQ(i, TickersNameMap[i].first);
      metric_names_.emplace_back(yb::Format("intentsdb_$0", TickersNameMap[i].second));
      const auto& ticker_name = metric_names_.back();

      descriptions_.emplace_back(
          yb::Format("Per-tablet Intents Rocksdb Ticker for $0", ticker_name));
      const auto& description = descriptions_.back();

      intents_ticker_prototypes_.emplace_back(
          std::make_unique<GaugePrototype<uint64_t>>(::yb::MetricPrototype::CtorArgs(
              "tablet", ticker_name.c_str(), description.c_str(), yb::MetricUnit::kRequests,
              description.c_str(), yb::MetricLevel::kInfo)));
    }
  }

  const std::vector<std::unique_ptr<HistogramPrototype>>& hist_prototypes() const {
    return hist_prototypes_;
  }
  const std::vector<std::unique_ptr<GaugePrototype<uint64_t>>>& regular_ticker_prototypes() const {
    return regular_ticker_prototypes_;
  }
  const std::vector<std::unique_ptr<GaugePrototype<uint64_t>>>& intents_ticker_prototypes() const {
    return intents_ticker_prototypes_;
  }

 private:
  std::vector<std::unique_ptr<HistogramPrototype>> hist_prototypes_;
  std::vector<std::unique_ptr<GaugePrototype<uint64_t>>> regular_ticker_prototypes_;
  std::vector<std::unique_ptr<GaugePrototype<uint64_t>>> intents_ticker_prototypes_;
  // store the names and descriptions generated because the MetricPrototypes only keep
  // a char * ptr to the name/description.
  std::vector<std::string> metric_names_;
  std::vector<std::string> descriptions_;

  DISALLOW_COPY_AND_ASSIGN(StatisticsMetricPrototypes);
};

StatisticsMetricImpl::StatisticsMetricImpl(
    const scoped_refptr<MetricEntity>& hist_entity,
    const scoped_refptr<MetricEntity>& tick_entity,
    const bool for_intents) {
  static StatisticsMetricPrototypes prototypes;
  const auto kNumHistograms = HistogramsNameMap.size();
  const auto kNumTickers = TickersNameMap.size();
  for (size_t i = 0; hist_entity && i < kNumHistograms; i++) {
    histograms_.push_back(prototypes.hist_prototypes()[i]->Instantiate(hist_entity));
  }

  for (size_t i = 0; tick_entity && i < kNumTickers; i++) {
    tickers_.push_back(
        for_intents ? prototypes.intents_ticker_prototypes()[i]->Instantiate(tick_entity, 0)
                    : prototypes.regular_ticker_prototypes()[i]->Instantiate(tick_entity, 0));
  }
}

StatisticsMetricImpl::~StatisticsMetricImpl() {}

uint64_t StatisticsMetricImpl::getTickerCount(uint32_t tickerType) const {
  if (!tickers_.empty()) {
    assert(tickerType < tickers_.size());
    // Return its own ticker version
    return tickers_[tickerType]->value();
  }

  return 0;
}

void StatisticsMetricImpl::histogramData(uint32_t histogramType, HistogramData* const data) const {
  assert(histogramType < histograms_.size());
  assert(data);
  if (histograms_.empty()) {
    return;
  }

  const yb::HdrHistogram* hist = histograms_[histogramType]->histogram();
  data->count = hist->CurrentCount();
  data->sum = hist->CurrentSum();
  data->min = hist->MinValue();
  data->max = hist->MaxValue();
  data->median = hist->ValueAtPercentile(50);
  data->percentile95 = hist->ValueAtPercentile(95);
  data->percentile99 = hist->ValueAtPercentile(99);
  data->average = hist->MeanValue();
  // Computing standard deviation is not supported by HdrHistogram.
  // We don't use it for Yugabyte anyways.
  data->standard_deviation = -1;
}

void StatisticsMetricImpl::setTickerCount(uint32_t tickerType, uint64_t count) {
  if (!tickers_.empty()) {
    assert(tickerType < tickers_.size());
    tickers_[tickerType]->set_value(count);
    if (tickerType == CURRENT_VERSION_SST_FILES_SIZE) {
      setTickerCount(OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE, count);
    }
  }
}

void StatisticsMetricImpl::recordTick(uint32_t tickerType, uint64_t count) {
  if (!tickers_.empty()) {
    assert(tickerType < tickers_.size());
    tickers_[tickerType]->IncrementBy(count);
    if (tickerType == CURRENT_VERSION_SST_FILES_SIZE) {
      recordTick(OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE, count);
    }
  }
}

void StatisticsMetricImpl::resetTickersForTest() {
  for (uint32_t i = 0; i < tickers_.size(); i++) {
    setTickerCount(i, 0);
  }
}

void StatisticsMetricImpl::measureTime(uint32_t histogramType, uint64_t value) {
  if (!histograms_.empty()) {
    assert(histogramType < histograms_.size());
    histograms_[histogramType]->Increment(value);
  }
}

} // namespace rocksdb
