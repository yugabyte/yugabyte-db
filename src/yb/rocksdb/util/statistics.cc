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

#include "yb/util/aggregate_stats.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"

namespace rocksdb {

namespace {

using yb::GaugePrototype;
using yb::EventStatsPrototype;
using yb::MetricEntity;
using yb::MetricRegistry;

void PopulateHistogramData(const yb::AggregateStats& hist, HistogramData* const data) {
  data->count = hist.CurrentCount();
  data->sum = hist.CurrentSum();
  data->min = hist.MinValue();
  data->max = hist.MaxValue();
  data->average = hist.MeanValue();
}

} // namespace

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
    metric_names_.reserve(2 * kNumHistograms + 2 * kNumTickers);
    descriptions_.reserve(2 * kNumHistograms + 2 * kNumTickers);

    for (size_t i = 0; i < kNumHistograms; i++) {
      CHECK_EQ(i, HistogramsNameMap[i].first);
      metric_names_.emplace_back(HistogramsNameMap[i].second);
      const auto& hist_name_regular = metric_names_.back();

      descriptions_.emplace_back(
          yb::Format("Per-table Regular Rocksdb Histogram for $0", hist_name_regular));
      const auto& description_regular = descriptions_.back();

     regular_hist_prototypes_.emplace_back(std::make_unique<EventStatsPrototype>(
          ::yb::MetricPrototype::CtorArgs(
              "table", hist_name_regular.c_str(), description_regular.c_str(),
              yb::MetricUnit::kMicroseconds, description_regular.c_str(),
              yb::MetricLevel::kInfo)));

      metric_names_.emplace_back(yb::Format("intentsdb_$0", HistogramsNameMap[i].second));
      const auto& hist_name_intents = metric_names_.back();

      descriptions_.emplace_back(
          yb::Format("Per-table Intents Rocksdb Histogram for $0", hist_name_intents));
      const auto& description_intents = descriptions_.back();

      intents_hist_prototypes_.emplace_back(std::make_unique<EventStatsPrototype>(
          ::yb::MetricPrototype::CtorArgs(
              "table", hist_name_intents.c_str(), description_intents.c_str(),
              yb::MetricUnit::kMicroseconds, description_intents.c_str(),
              yb::MetricLevel::kInfo)));
    }

    for (size_t i = 0; i < kNumTickers; i++) {
      CHECK_EQ(i, TickersNameMap[i].first);
      metric_names_.emplace_back(TickersNameMap[i].second);
      const auto& ticker_name_regular = metric_names_.back();

      descriptions_.emplace_back(
          yb::Format("Per-tablet Regular Rocksdb Ticker for $0", ticker_name_regular));
      const auto& description_regular = descriptions_.back();

      regular_ticker_prototypes_.emplace_back(
          std::make_unique<GaugePrototype<uint64_t>>(::yb::MetricPrototype::CtorArgs(
              "tablet", ticker_name_regular.c_str(), description_regular.c_str(),
              yb::MetricUnit::kRequests, description_regular.c_str(),
              yb::MetricLevel::kInfo)));

      metric_names_.emplace_back(yb::Format("intentsdb_$0", TickersNameMap[i].second));
      const auto& ticker_name_intents = metric_names_.back();

      descriptions_.emplace_back(
          yb::Format("Per-tablet Intents Rocksdb Ticker for $0", ticker_name_intents));
      const auto& description_intents = descriptions_.back();

      intents_ticker_prototypes_.emplace_back(
          std::make_unique<GaugePrototype<uint64_t>>(::yb::MetricPrototype::CtorArgs(
              "tablet", ticker_name_intents.c_str(), description_intents.c_str(),
              yb::MetricUnit::kRequests, description_intents.c_str(),
              yb::MetricLevel::kInfo)));
    }
  }

  const std::vector<std::unique_ptr<EventStatsPrototype>>& regular_hist_prototypes() const {
    return regular_hist_prototypes_;
  }
  const std::vector<std::unique_ptr<EventStatsPrototype>>& intents_hist_prototypes() const {
    return intents_hist_prototypes_;
  }
  const std::vector<std::unique_ptr<GaugePrototype<uint64_t>>>& regular_ticker_prototypes() const {
    return regular_ticker_prototypes_;
  }
  const std::vector<std::unique_ptr<GaugePrototype<uint64_t>>>& intents_ticker_prototypes() const {
    return intents_ticker_prototypes_;
  }

 private:
  std::vector<std::unique_ptr<EventStatsPrototype>> regular_hist_prototypes_;
  std::vector<std::unique_ptr<EventStatsPrototype>> intents_hist_prototypes_;
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

  auto& hist_prototypes = for_intents ? prototypes.intents_hist_prototypes()
                                      : prototypes.regular_hist_prototypes();
  if (hist_entity) {
    histograms_.reserve(kNumHistograms);
    for (size_t i = 0; i < kNumHistograms; i++) {
      histograms_.push_back(hist_prototypes[i]->Instantiate(hist_entity));
    }
  }

  auto& ticker_prototypes = for_intents ? prototypes.intents_ticker_prototypes()
                                        : prototypes.regular_ticker_prototypes();
  if (tick_entity) {
    tickers_.reserve(kNumTickers);
    for (size_t i = 0; i < kNumTickers; i++) {
      tickers_.push_back(ticker_prototypes[i]->Instantiate(tick_entity, 0));
    }
  }
}

StatisticsMetricImpl::~StatisticsMetricImpl() {}

uint64_t StatisticsMetricImpl::getTickerCount(uint32_t ticker_type) const {
  if (!tickers_.empty()) {
    assert(ticker_type < tickers_.size());
    // Return its own ticker version
    return tickers_[ticker_type]->value();
  }

  return 0;
}

const char* StatisticsMetricImpl::GetTickerName(uint32_t ticker_type) const {
  CHECK_LT(ticker_type, tickers_.size());
  if (tickers_.empty()) {
    return "n/a";
  }
  return tickers_[ticker_type]->prototype()->name();
}

void StatisticsMetricImpl::histogramData(
    uint32_t histogram_type, HistogramData* const data) const {
  if (!histograms_.empty()) {
    assert(histogram_type < histograms_.size());
    assert(data);
    PopulateHistogramData(*histograms_[histogram_type]->underlying(), data);
  }
}

void StatisticsMetricImpl::setTickerCount(uint32_t ticker_type, uint64_t count) {
  if (!tickers_.empty()) {
    assert(ticker_type < tickers_.size());
    tickers_[ticker_type]->set_value(count);
    if (ticker_type == CURRENT_VERSION_SST_FILES_SIZE) {
      setTickerCount(OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE, count);
    }
  }
}

void StatisticsMetricImpl::recordTick(uint32_t ticker_type, uint64_t count) {
  if (!tickers_.empty()) {
    assert(ticker_type < tickers_.size());
    tickers_[ticker_type]->IncrementBy(count);
  }
  if (ticker_type == CURRENT_VERSION_SST_FILES_SIZE) {
    recordTick(OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE, count);
  }
}

void StatisticsMetricImpl::resetTickersForTest() {
  for (uint32_t i = 0; i < tickers_.size(); ++i) {
    setTickerCount(i, 0);
  }
}

void StatisticsMetricImpl::measureTime(uint32_t histogram_type, uint64_t value) {
  if (!histograms_.empty()) {
    assert(histogram_type < histograms_.size());
    histograms_[histogram_type]->Increment(value);
  }
}

ScopedStatistics::ScopedStatistics(): tickers_(TickersNameMap.size(), 0) {}

uint64_t ScopedStatistics::getTickerCount(uint32_t ticker_type) const {
  assert(ticker_type < tickers_.size());
  return tickers_[ticker_type];
}

const char* ScopedStatistics::GetTickerName(uint32_t ticker_type) const {
  YB_LOG_EVERY_N_SECS(DFATAL, 100)
      << "resetTickersForTest for scoped statistics is not supported.";
  return "";
}

void ScopedStatistics::histogramData(
    uint32_t histogram_type, HistogramData* const data) const {
  histogram_context_->histogramData(histogram_type, data);
}

void ScopedStatistics::setTickerCount(uint32_t ticker_type, uint64_t count) {
  YB_LOG_EVERY_N_SECS(DFATAL, 100)
      << "resetTickersForTest for scoped statistics is not supported.";
}

void ScopedStatistics::recordTick(uint32_t ticker_type, uint64_t count) {
  assert(ticker_type < tickers_.size());
  tickers_[ticker_type] += count;
  if (ticker_type == CURRENT_VERSION_SST_FILES_SIZE) {
    recordTick(OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE, count);
  }
}

void ScopedStatistics::resetTickersForTest() {
  YB_LOG_EVERY_N_SECS(DFATAL, 100)
      << "resetTickersForTest for scoped statistics is not supported.";
}

void ScopedStatistics::measureTime(uint32_t histogram_type, uint64_t value) {
  histogram_context_->measureTime(histogram_type, value);
}

void ScopedStatistics::SetHistogramContext(std::shared_ptr<Statistics> histogram_context) {
  histogram_context_ = std::move(histogram_context);
}

void ScopedStatistics::MergeAndClear(Statistics* target) {
  CHECK_NOTNULL(target);
  for (uint32_t i = 0; i < tickers_.size(); ++i) {
    target->recordTick(i, tickers_[i]);
    tickers_[i] = 0;
  }
  histogram_context_.reset();
}

} // namespace rocksdb
