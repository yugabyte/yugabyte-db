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

#include "yb/docdb/docdb_statistics.h"

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/util/statistics.h"
#include "yb/yql/pggate/pg_metrics_list.h"

namespace yb::docdb {

namespace {

constexpr std::pair<uint32_t, uint32_t> kTickers[] = {
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_MISS, rocksdb::BLOCK_CACHE_MISS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_HIT, rocksdb::BLOCK_CACHE_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_ADD, rocksdb::BLOCK_CACHE_ADD},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_ADD_FAILURES, rocksdb::BLOCK_CACHE_ADD_FAILURES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_INDEX_MISS, rocksdb::BLOCK_CACHE_INDEX_MISS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_INDEX_HIT, rocksdb::BLOCK_CACHE_INDEX_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_FILTER_MISS, rocksdb::BLOCK_CACHE_FILTER_MISS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_FILTER_HIT, rocksdb::BLOCK_CACHE_FILTER_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_DATA_MISS, rocksdb::BLOCK_CACHE_DATA_MISS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_DATA_HIT, rocksdb::BLOCK_CACHE_DATA_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_BYTES_READ, rocksdb::BLOCK_CACHE_BYTES_READ},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_BYTES_WRITE, rocksdb::BLOCK_CACHE_BYTES_WRITE},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOOM_FILTER_USEFUL, rocksdb::BLOOM_FILTER_USEFUL},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOOM_FILTER_CHECKED, rocksdb::BLOOM_FILTER_CHECKED},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_MEMTABLE_HIT, rocksdb::MEMTABLE_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_MEMTABLE_MISS, rocksdb::MEMTABLE_MISS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_GET_HIT_L0, rocksdb::GET_HIT_L0},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_GET_HIT_L1, rocksdb::GET_HIT_L1},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_GET_HIT_L2_AND_UP, rocksdb::GET_HIT_L2_AND_UP},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_KEYS_WRITTEN, rocksdb::NUMBER_KEYS_WRITTEN},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_KEYS_READ, rocksdb::NUMBER_KEYS_READ},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_KEYS_UPDATED, rocksdb::NUMBER_KEYS_UPDATED},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BYTES_WRITTEN, rocksdb::BYTES_WRITTEN},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BYTES_READ, rocksdb::BYTES_READ},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_DB_SEEK, rocksdb::NUMBER_DB_SEEK},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_DB_NEXT, rocksdb::NUMBER_DB_NEXT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_DB_PREV, rocksdb::NUMBER_DB_PREV},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_DB_SEEK_FOUND, rocksdb::NUMBER_DB_SEEK_FOUND},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_DB_NEXT_FOUND, rocksdb::NUMBER_DB_NEXT_FOUND},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_DB_PREV_FOUND, rocksdb::NUMBER_DB_PREV_FOUND},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_ITER_BYTES_READ, rocksdb::ITER_BYTES_READ},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NO_FILE_CLOSES, rocksdb::NO_FILE_CLOSES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NO_FILE_OPENS, rocksdb::NO_FILE_OPENS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NO_FILE_ERRORS, rocksdb::NO_FILE_ERRORS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_STALL_L0_SLOWDOWN_MICROS, rocksdb::STALL_L0_SLOWDOWN_MICROS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_STALL_MEMTABLE_COMPACTION_MICROS,
      rocksdb::STALL_MEMTABLE_COMPACTION_MICROS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_STALL_L0_NUM_FILES_MICROS,
      rocksdb::STALL_L0_NUM_FILES_MICROS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_STALL_MICROS, rocksdb::STALL_MICROS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_DB_MUTEX_WAIT_MICROS, rocksdb::DB_MUTEX_WAIT_MICROS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_RATE_LIMIT_DELAY_MILLIS, rocksdb::RATE_LIMIT_DELAY_MILLIS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NO_ITERATORS, rocksdb::NO_ITERATORS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_MULTIGET_CALLS, rocksdb::NUMBER_MULTIGET_CALLS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_MULTIGET_KEYS_READ,
      rocksdb::NUMBER_MULTIGET_KEYS_READ},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_MULTIGET_BYTES_READ,
      rocksdb::NUMBER_MULTIGET_BYTES_READ},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_FILTERED_DELETES, rocksdb::NUMBER_FILTERED_DELETES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_MERGE_FAILURES, rocksdb::NUMBER_MERGE_FAILURES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_SEQUENCE_NUMBER, rocksdb::SEQUENCE_NUMBER},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOOM_FILTER_PREFIX_CHECKED,
      rocksdb::BLOOM_FILTER_PREFIX_CHECKED},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOOM_FILTER_PREFIX_USEFUL,
      rocksdb::BLOOM_FILTER_PREFIX_USEFUL},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_OF_RESEEKS_IN_ITERATION,
      rocksdb::NUMBER_OF_RESEEKS_IN_ITERATION},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_GET_UPDATES_SINCE_CALLS, rocksdb::GET_UPDATES_SINCE_CALLS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_COMPRESSED_MISS,
      rocksdb::BLOCK_CACHE_COMPRESSED_MISS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_COMPRESSED_HIT,
      rocksdb::BLOCK_CACHE_COMPRESSED_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_COMPRESSED_ADD,
      rocksdb::BLOCK_CACHE_COMPRESSED_ADD},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_COMPRESSED_ADD_FAILURES,
      rocksdb::BLOCK_CACHE_COMPRESSED_ADD_FAILURES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_WAL_FILE_SYNCED, rocksdb::WAL_FILE_SYNCED},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_WAL_FILE_BYTES, rocksdb::WAL_FILE_BYTES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_WRITE_DONE_BY_SELF, rocksdb::WRITE_DONE_BY_SELF},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_WRITE_DONE_BY_OTHER, rocksdb::WRITE_DONE_BY_OTHER},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_WRITE_WITH_WAL, rocksdb::WRITE_WITH_WAL},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_COMPACT_READ_BYTES, rocksdb::COMPACT_READ_BYTES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_COMPACT_WRITE_BYTES, rocksdb::COMPACT_WRITE_BYTES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_FLUSH_WRITE_BYTES, rocksdb::FLUSH_WRITE_BYTES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_DIRECT_LOAD_TABLE_PROPERTIES,
      rocksdb::NUMBER_DIRECT_LOAD_TABLE_PROPERTIES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_SUPERVERSION_ACQUIRES,
      rocksdb::NUMBER_SUPERVERSION_ACQUIRES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_SUPERVERSION_RELEASES,
      rocksdb::NUMBER_SUPERVERSION_RELEASES},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_SUPERVERSION_CLEANUPS,
      rocksdb::NUMBER_SUPERVERSION_CLEANUPS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NUMBER_BLOCK_NOT_COMPRESSED,
      rocksdb::NUMBER_BLOCK_NOT_COMPRESSED},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_MERGE_OPERATION_TOTAL_TIME,
      rocksdb::MERGE_OPERATION_TOTAL_TIME},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_FILTER_OPERATION_TOTAL_TIME,
      rocksdb::FILTER_OPERATION_TOTAL_TIME},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_ROW_CACHE_HIT, rocksdb::ROW_CACHE_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_ROW_CACHE_MISS, rocksdb::ROW_CACHE_MISS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_NO_TABLE_CACHE_ITERATORS,
      rocksdb::NO_TABLE_CACHE_ITERATORS},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_SINGLE_TOUCH_HIT,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_SINGLE_TOUCH_ADD,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_ADD},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_SINGLE_TOUCH_BYTES_READ,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_BYTES_READ},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_MULTI_TOUCH_HIT,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_HIT},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_MULTI_TOUCH_ADD,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_ADD},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_MULTI_TOUCH_BYTES_READ,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_BYTES_READ},
  {pggate::YB_ANALYZE_METRIC_ROCKSDB_BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE},
};

} // namespace

DocDBStatistics::DocDBStatistics():
    regulardb_statistics_(std::make_unique<rocksdb::ScopedStatistics>()),
    intentsdb_statistics_(std::make_unique<rocksdb::ScopedStatistics>()) {}

DocDBStatistics::~DocDBStatistics() {}

rocksdb::Statistics* DocDBStatistics::RegularDBStatistics() const {
  return regulardb_statistics_.get();
}

rocksdb::Statistics* DocDBStatistics::IntentsDBStatistics() const {
  return intentsdb_statistics_.get();
}

void DocDBStatistics::SetHistogramContext(
    std::shared_ptr<rocksdb::Statistics> regulardb_statistics,
    std::shared_ptr<rocksdb::Statistics> intentsdb_statistics) {
  regulardb_statistics_->SetHistogramContext(std::move(regulardb_statistics));
  intentsdb_statistics_->SetHistogramContext(std::move(intentsdb_statistics));
}

void DocDBStatistics::MergeAndClear(
    rocksdb::Statistics* regulardb_statistics,
    rocksdb::Statistics* intentsdb_statistics) {
  regulardb_statistics_->MergeAndClear(regulardb_statistics);
  intentsdb_statistics_->MergeAndClear(intentsdb_statistics);
}

void DocDBStatistics::CopyToPgsqlResponse(PgsqlResponsePB* response) const {
  auto* metrics = response->mutable_metrics();
  for (const auto& [pggate_index, rocksdb_index] : kTickers) {
    const auto ticker = regulardb_statistics_->getTickerCount(rocksdb_index);
    // Don't send unchanged statistics.
    if (ticker == 0) {
      continue;
    }
    auto* metric = metrics->add_gauge_metrics();
    metric->set_metric(pggate_index);
    metric->set_value(ticker);
  }
}

} // namespace yb::docdb
