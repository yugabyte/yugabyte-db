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

constexpr std::pair<Tickers, const char *> TickersNameMap[] = {
    {BLOCK_CACHE_MISS, "rocksdb_block_cache_miss"},
    {BLOCK_CACHE_HIT, "rocksdb_block_cache_hit"},
    {BLOCK_CACHE_ADD, "rocksdb_block_cache_add"},
    {BLOCK_CACHE_ADD_FAILURES, "rocksdb_block_cache_add_failures"},
    {BLOCK_CACHE_INDEX_MISS, "rocksdb_block_cache_index_miss"},
    {BLOCK_CACHE_INDEX_HIT, "rocksdb_block_cache_index_hit"},
    {BLOCK_CACHE_FILTER_MISS, "rocksdb_block_cache_filter_miss"},
    {BLOCK_CACHE_FILTER_HIT, "rocksdb_block_cache_filter_hit"},
    {BLOCK_CACHE_DATA_MISS, "rocksdb_block_cache_data_miss"},
    {BLOCK_CACHE_DATA_HIT, "rocksdb_block_cache_data_hit"},
    {BLOCK_CACHE_BYTES_READ, "rocksdb_block_cache_bytes_read"},
    {BLOCK_CACHE_BYTES_WRITE, "rocksdb_block_cache_bytes_write"},
    {BLOOM_FILTER_USEFUL, "rocksdb_bloom_filter_useful"},
    {BLOOM_FILTER_CHECKED, "rocksdb_bloom_filter_checked"},
    {MEMTABLE_HIT, "rocksdb_memtable_hit"},
    {MEMTABLE_MISS, "rocksdb_memtable_miss"},
    {GET_HIT_L0, "rocksdb_l0_hit"},
    {GET_HIT_L1, "rocksdb_l1_hit"},
    {GET_HIT_L2_AND_UP, "rocksdb_l2andup_hit"},
    {COMPACTION_KEY_DROP_NEWER_ENTRY, "rocksdb_compaction_key_drop_new"},
    {COMPACTION_KEY_DROP_OBSOLETE, "rocksdb_compaction_key_drop_obsolete"},
    {COMPACTION_KEY_DROP_USER, "rocksdb_compaction_key_drop_user"},
    {NUMBER_KEYS_WRITTEN, "rocksdb_number_keys_written"},
    {NUMBER_KEYS_READ, "rocksdb_number_keys_read"},
    {NUMBER_KEYS_UPDATED, "rocksdb_number_keys_updated"},
    {BYTES_WRITTEN, "rocksdb_bytes_written"},
    {BYTES_READ, "rocksdb_bytes_read"},
    {NUMBER_DB_SEEK, "rocksdb_number_db_seek"},
    {NUMBER_DB_NEXT, "rocksdb_number_db_next"},
    {NUMBER_DB_PREV, "rocksdb_number_db_prev"},
    {NUMBER_DB_SEEK_FOUND, "rocksdb_number_db_seek_found"},
    {NUMBER_DB_NEXT_FOUND, "rocksdb_number_db_next_found"},
    {NUMBER_DB_PREV_FOUND, "rocksdb_number_db_prev_found"},
    {ITER_BYTES_READ, "rocksdb_db_iter_bytes_read"},
    {NO_FILE_CLOSES, "rocksdb_no_file_closes"},
    {NO_FILE_OPENS, "rocksdb_no_file_opens"},
    {NO_FILE_ERRORS, "rocksdb_no_file_errors"},
    {STALL_L0_SLOWDOWN_MICROS, "rocksdb_l0_slowdown_micros"},
    {STALL_MEMTABLE_COMPACTION_MICROS, "rocksdb_memtable_compaction_micros"},
    {STALL_L0_NUM_FILES_MICROS, "rocksdb_l0_num_files_stall_micros"},
    {STALL_MICROS, "rocksdb_stall_micros"},
    {DB_MUTEX_WAIT_MICROS, "rocksdb_db_mutex_wait_micros"},
    {RATE_LIMIT_DELAY_MILLIS, "rocksdb_rate_limit_delay_millis"},
    {NO_ITERATORS, "rocksdb_num_iterators"},
    {NUMBER_MULTIGET_CALLS, "rocksdb_number_multiget_get"},
    {NUMBER_MULTIGET_KEYS_READ, "rocksdb_number_multiget_keys_read"},
    {NUMBER_MULTIGET_BYTES_READ, "rocksdb_number_multiget_bytes_read"},
    {NUMBER_FILTERED_DELETES, "rocksdb_number_deletes_filtered"},
    {NUMBER_MERGE_FAILURES, "rocksdb_number_merge_failures"},
    {SEQUENCE_NUMBER, "rocksdb_sequence_number"},
    {BLOOM_FILTER_PREFIX_CHECKED, "rocksdb_bloom_filter_prefix_checked"},
    {BLOOM_FILTER_PREFIX_USEFUL, "rocksdb_bloom_filter_prefix_useful"},
    {NUMBER_OF_RESEEKS_IN_ITERATION, "rocksdb_number_reseeks_iteration"},
    {GET_UPDATES_SINCE_CALLS, "rocksdb_getupdatessince_calls"},
    {BLOCK_CACHE_COMPRESSED_MISS, "rocksdb_block_cachecompressed_miss"},
    {BLOCK_CACHE_COMPRESSED_HIT, "rocksdb_block_cachecompressed_hit"},
    {BLOCK_CACHE_COMPRESSED_ADD, "rocksdb_block_cachecompressed_add"},
    {BLOCK_CACHE_COMPRESSED_ADD_FAILURES,
     "rocksdb_block_cachecompressed_add_failures"},
    {WAL_FILE_SYNCED, "rocksdb_wal_synced"},
    {WAL_FILE_BYTES, "rocksdb_wal_bytes"},
    {WRITE_DONE_BY_SELF, "rocksdb_write_self"},
    {WRITE_DONE_BY_OTHER, "rocksdb_write_other"},
    {WRITE_WITH_WAL, "rocksdb_write_wal"},
    {COMPACT_READ_BYTES, "rocksdb_compact_read_bytes"},
    {COMPACT_WRITE_BYTES, "rocksdb_compact_write_bytes"},
    {FLUSH_WRITE_BYTES, "rocksdb_flush_write_bytes"},
    {NUMBER_DIRECT_LOAD_TABLE_PROPERTIES,
     "rocksdb_number_direct_load_table_properties"},
    {NUMBER_SUPERVERSION_ACQUIRES, "rocksdb_number_superversion_acquires"},
    {NUMBER_SUPERVERSION_RELEASES, "rocksdb_number_superversion_releases"},
    {NUMBER_SUPERVERSION_CLEANUPS, "rocksdb_number_superversion_cleanups"},
    {NUMBER_BLOCK_NOT_COMPRESSED, "rocksdb_number_block_not_compressed"},
    {CURRENT_VERSION_SST_FILES_SIZE, "rocksdb_current_version_sst_files_size"},
    {OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE, "rocksdb_total_sst_files_size"},

    {CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE,
          "rocksdb_current_version_sst_files_uncompressed_size"},

    {CURRENT_VERSION_NUM_SST_FILES, "rocksdb_current_version_num_sst_files"},
    {MERGE_OPERATION_TOTAL_TIME, "rocksdb_merge_operation_time_nanos"},
    {FILTER_OPERATION_TOTAL_TIME, "rocksdb_filter_operation_time_nanos"},
    {ROW_CACHE_HIT, "rocksdb_row_cache_hit"},
    {ROW_CACHE_MISS, "rocksdb_row_cache_miss"},
    {NO_TABLE_CACHE_ITERATORS, "rocksdb_no_table_cache_iterators"},
    {BLOCK_CACHE_SINGLE_TOUCH_HIT, "rocksdb_block_cache_single_touch_hit"},
    {BLOCK_CACHE_SINGLE_TOUCH_ADD, "rocksdb_block_cache_single_touch_add"},
    {BLOCK_CACHE_SINGLE_TOUCH_BYTES_READ, "rocksdb_block_cache_single_touch_bytes_read"},
    {BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE, "rocksdb_block_cache_single_touch_bytes_write"},
    {BLOCK_CACHE_MULTI_TOUCH_HIT, "rocksdb_block_cache_multi_touch_hit"},
    {BLOCK_CACHE_MULTI_TOUCH_ADD, "rocksdb_block_cache_multi_touch_add"},
    {BLOCK_CACHE_MULTI_TOUCH_BYTES_READ, "rocksdb_block_cache_multi_touch_bytes_read"},
    {BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE, "rocksdb_block_cache_multi_touch_bytes_write"},

    {COMPACTION_FILES_FILTERED, "rocksdb_compaction_files_filtered"},
    {COMPACTION_FILES_NOT_FILTERED, "rocksdb_compaction_files_not_filtered"},
};

constexpr std::pair<Histograms, const char *> HistogramsNameMap[] = {
    {DB_GET, "rocksdb_db_get_micros"},
    {DB_WRITE, "rocksdb_db_write_micros"},
    {COMPACTION_TIME, "rocksdb_compaction_times_micros"},
    {WAL_FILE_SYNC_MICROS, "rocksdb_wal_file_sync_micros"},
    {DB_MULTIGET, "rocksdb_db_multiget_micros"},
    {READ_BLOCK_COMPACTION_MICROS, "rocksdb_read_block_compaction_micros"},
    {READ_BLOCK_GET_MICROS, "rocksdb_read_block_get_micros"},
    {WRITE_RAW_BLOCK_MICROS, "rocksdb_write_raw_block_micros"},
    {NUM_FILES_IN_SINGLE_COMPACTION, "rocksdb_numfiles_in_singlecompaction"},
    {DB_SEEK, "rocksdb_db_seek_micros"},
    {SST_READ_MICROS, "rocksdb_sst_read_micros"},
    {BYTES_PER_READ, "rocksdb_bytes_per_read"},
    {BYTES_PER_WRITE, "rocksdb_bytes_per_write"},
    {BYTES_PER_MULTIGET, "rocksdb_bytes_per_multiget"},
};

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
    const auto kNumHistograms = arraysize(HistogramsNameMap);
    const auto kNumTickers = arraysize(TickersNameMap);
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
  const auto kNumHistograms = arraysize(HistogramsNameMap);
  const auto kNumTickers = arraysize(TickersNameMap);

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
    DCHECK(ticker_type < tickers_.size());
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
    DCHECK(histogram_type < histograms_.size());
    DCHECK(data);
    PopulateHistogramData(*histograms_[histogram_type]->underlying(), data);
  }
}

const yb::AggregateStats& StatisticsMetricImpl::getAggregateStats(uint32_t histogram_type) const {
  DCHECK(histogram_type < histograms_.size());
  return *histograms_[histogram_type]->underlying();
}

void StatisticsMetricImpl::setTickerCount(uint32_t ticker_type, uint64_t count) {
  if (!tickers_.empty()) {
    DCHECK(ticker_type < tickers_.size());
    tickers_[ticker_type]->set_value(count);
    if (ticker_type == CURRENT_VERSION_SST_FILES_SIZE) {
      setTickerCount(OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE, count);
    }
  }
}

void StatisticsMetricImpl::recordTick(uint32_t ticker_type, uint64_t count) {
  if (!tickers_.empty()) {
    DCHECK(ticker_type < tickers_.size());
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
    DCHECK(histogram_type < histograms_.size());
    histograms_[histogram_type]->Increment(value);
  }
}

void StatisticsMetricImpl::addHistogram(uint32_t histogram_type, const yb::AggregateStats& stats) {
  DCHECK(histogram_type < histograms_.size());
  histograms_[histogram_type]->Add(stats);
}

ScopedStatistics::ScopedStatistics()
    : tickers_(arraysize(TickersNameMap), 0),
      histograms_(arraysize(HistogramsNameMap)) {}

uint64_t ScopedStatistics::getTickerCount(uint32_t ticker_type) const {
  DCHECK(ticker_type < tickers_.size());
  return tickers_[ticker_type];
}

const char* ScopedStatistics::GetTickerName(uint32_t ticker_type) const {
  YB_LOG_EVERY_N_SECS(DFATAL, 100)
      << "resetTickersForTest for scoped statistics is not supported.";
  return "";
}

void ScopedStatistics::histogramData(
    uint32_t histogram_type, HistogramData* const data) const {
  if (!histograms_.empty()) {
    DCHECK(histogram_type < histograms_.size());
    DCHECK(data);
    PopulateHistogramData(histograms_[histogram_type], data);
  }
}

const yb::AggregateStats& ScopedStatistics::getAggregateStats(uint32_t histogram_type) const {
  DCHECK(histogram_type < histograms_.size());
  return histograms_[histogram_type];
}

void ScopedStatistics::setTickerCount(uint32_t ticker_type, uint64_t count) {
  YB_LOG_EVERY_N_SECS(DFATAL, 100)
      << "resetTickersForTest for scoped statistics is not supported.";
}

void ScopedStatistics::recordTick(uint32_t ticker_type, uint64_t count) {
  DCHECK(ticker_type < tickers_.size());
  tickers_[ticker_type] += count;
  if (ticker_type == CURRENT_VERSION_SST_FILES_SIZE) {
    recordTick(OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE, count);
  }
}

void ScopedStatistics::addHistogram(uint32_t histogram_type, const yb::AggregateStats& stats) {
  DCHECK(histogram_type < histograms_.size());
  histograms_[histogram_type].Add(stats);
}

void ScopedStatistics::resetTickersForTest() {
  YB_LOG_EVERY_N_SECS(DFATAL, 100)
      << "resetTickersForTest for scoped statistics is not supported.";
}

void ScopedStatistics::measureTime(uint32_t histogram_type, uint64_t value) {
  if (!histograms_.empty()) {
    DCHECK(histogram_type < histograms_.size());
    histograms_[histogram_type].Increment(value);
  }
}

void ScopedStatistics::MergeAndClear(Statistics* target) {
  CHECK_NOTNULL(target);
  for (uint32_t i = 0; i < tickers_.size(); ++i) {
    target->recordTick(i, tickers_[i]);
    tickers_[i] = 0;
  }
  for (uint32_t i = 0; i < histograms_.size(); ++i) {
    target->addHistogram(i, histograms_[i]);
    histograms_[i].Reset(yb::PreserveTotalStats::kFalse);
  }
}

} // namespace rocksdb
