// Copyright (c) YugabyteDB, Inc.
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

#include <span>
#include <tuple>

#include "yb/common/pgsql_protocol.messages.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/util/statistics.h"
#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/yql/pggate/pg_metrics_list.h"

DEFINE_RUNTIME_bool(ysql_analyze_dump_intentsdb_metrics, false,
    "Whether to return changed intentsdb metrics for YSQL queries in RPC response.");

namespace yb::docdb {

namespace {

constexpr std::pair<uint32_t, uint32_t> kRegularDBTickers[] = {
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MISS, rocksdb::BLOCK_CACHE_MISS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_HIT, rocksdb::BLOCK_CACHE_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_ADD, rocksdb::BLOCK_CACHE_ADD},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_ADD_FAILURES, rocksdb::BLOCK_CACHE_ADD_FAILURES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_INDEX_MISS, rocksdb::BLOCK_CACHE_INDEX_MISS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_INDEX_HIT, rocksdb::BLOCK_CACHE_INDEX_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_FILTER_MISS, rocksdb::BLOCK_CACHE_FILTER_MISS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_FILTER_HIT, rocksdb::BLOCK_CACHE_FILTER_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_DATA_MISS, rocksdb::BLOCK_CACHE_DATA_MISS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_DATA_HIT, rocksdb::BLOCK_CACHE_DATA_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_BYTES_WRITE, rocksdb::BLOCK_CACHE_BYTES_WRITE},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOOM_FILTER_USEFUL, rocksdb::BLOOM_FILTER_USEFUL},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOOM_FILTER_CHECKED, rocksdb::BLOOM_FILTER_CHECKED},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_MEMTABLE_HIT, rocksdb::MEMTABLE_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_MEMTABLE_MISS, rocksdb::MEMTABLE_MISS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_GET_HIT_L0, rocksdb::GET_HIT_L0},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_GET_HIT_L1, rocksdb::GET_HIT_L1},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_GET_HIT_L2_AND_UP, rocksdb::GET_HIT_L2_AND_UP},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_KEYS_WRITTEN, rocksdb::NUMBER_KEYS_WRITTEN},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_KEYS_READ, rocksdb::NUMBER_KEYS_READ},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_KEYS_UPDATED, rocksdb::NUMBER_KEYS_UPDATED},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BYTES_WRITTEN, rocksdb::BYTES_WRITTEN},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BYTES_READ, rocksdb::BYTES_READ},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_SEEK, rocksdb::NUMBER_DB_SEEK},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_NEXT, rocksdb::NUMBER_DB_NEXT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_PREV, rocksdb::NUMBER_DB_PREV},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_SEEK_FOUND, rocksdb::NUMBER_DB_SEEK_FOUND},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_NEXT_FOUND, rocksdb::NUMBER_DB_NEXT_FOUND},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_PREV_FOUND, rocksdb::NUMBER_DB_PREV_FOUND},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_ITER_BYTES_READ, rocksdb::ITER_BYTES_READ},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NO_FILE_CLOSES, rocksdb::NO_FILE_CLOSES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NO_FILE_OPENS, rocksdb::NO_FILE_OPENS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NO_FILE_ERRORS, rocksdb::NO_FILE_ERRORS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_STALL_L0_SLOWDOWN_MICROS, rocksdb::STALL_L0_SLOWDOWN_MICROS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_STALL_MEMTABLE_COMPACTION_MICROS,
      rocksdb::STALL_MEMTABLE_COMPACTION_MICROS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_STALL_L0_NUM_FILES_MICROS,
      rocksdb::STALL_L0_NUM_FILES_MICROS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_STALL_MICROS, rocksdb::STALL_MICROS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_DB_MUTEX_WAIT_MICROS, rocksdb::DB_MUTEX_WAIT_MICROS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_RATE_LIMIT_DELAY_MILLIS, rocksdb::RATE_LIMIT_DELAY_MILLIS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NO_ITERATORS, rocksdb::NO_ITERATORS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_MULTIGET_CALLS, rocksdb::NUMBER_MULTIGET_CALLS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_MULTIGET_KEYS_READ,
      rocksdb::NUMBER_MULTIGET_KEYS_READ},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_MULTIGET_BYTES_READ,
      rocksdb::NUMBER_MULTIGET_BYTES_READ},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_FILTERED_DELETES, rocksdb::NUMBER_FILTERED_DELETES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_MERGE_FAILURES, rocksdb::NUMBER_MERGE_FAILURES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_SEQUENCE_NUMBER, rocksdb::SEQUENCE_NUMBER},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOOM_FILTER_PREFIX_CHECKED,
      rocksdb::BLOOM_FILTER_PREFIX_CHECKED},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOOM_FILTER_PREFIX_USEFUL,
      rocksdb::BLOOM_FILTER_PREFIX_USEFUL},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_OF_RESEEKS_IN_ITERATION,
      rocksdb::NUMBER_OF_RESEEKS_IN_ITERATION},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_GET_UPDATES_SINCE_CALLS, rocksdb::GET_UPDATES_SINCE_CALLS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_COMPRESSED_MISS,
      rocksdb::BLOCK_CACHE_COMPRESSED_MISS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_COMPRESSED_HIT,
      rocksdb::BLOCK_CACHE_COMPRESSED_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_COMPRESSED_ADD,
      rocksdb::BLOCK_CACHE_COMPRESSED_ADD},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_COMPRESSED_ADD_FAILURES,
      rocksdb::BLOCK_CACHE_COMPRESSED_ADD_FAILURES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_WAL_FILE_SYNCED, rocksdb::WAL_FILE_SYNCED},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_WAL_FILE_BYTES, rocksdb::WAL_FILE_BYTES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_WRITE_DONE_BY_SELF, rocksdb::WRITE_DONE_BY_SELF},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_WRITE_DONE_BY_OTHER, rocksdb::WRITE_DONE_BY_OTHER},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_WRITE_WITH_WAL, rocksdb::WRITE_WITH_WAL},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_COMPACT_READ_BYTES, rocksdb::COMPACT_READ_BYTES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_COMPACT_WRITE_BYTES, rocksdb::COMPACT_WRITE_BYTES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_FLUSH_WRITE_BYTES, rocksdb::FLUSH_WRITE_BYTES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DIRECT_LOAD_TABLE_PROPERTIES,
      rocksdb::NUMBER_DIRECT_LOAD_TABLE_PROPERTIES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_SUPERVERSION_ACQUIRES,
      rocksdb::NUMBER_SUPERVERSION_ACQUIRES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_SUPERVERSION_RELEASES,
      rocksdb::NUMBER_SUPERVERSION_RELEASES},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_SUPERVERSION_CLEANUPS,
      rocksdb::NUMBER_SUPERVERSION_CLEANUPS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_BLOCK_NOT_COMPRESSED,
      rocksdb::NUMBER_BLOCK_NOT_COMPRESSED},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_MERGE_OPERATION_TOTAL_TIME,
      rocksdb::MERGE_OPERATION_TOTAL_TIME},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_FILTER_OPERATION_TOTAL_TIME,
      rocksdb::FILTER_OPERATION_TOTAL_TIME},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_ROW_CACHE_HIT, rocksdb::ROW_CACHE_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_ROW_CACHE_MISS, rocksdb::ROW_CACHE_MISS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NO_TABLE_CACHE_ITERATORS,
      rocksdb::NO_TABLE_CACHE_ITERATORS},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_SINGLE_TOUCH_HIT,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_SINGLE_TOUCH_ADD,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_ADD},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MULTI_TOUCH_HIT,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_HIT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MULTI_TOUCH_ADD,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_ADD},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE},
};

constexpr std::pair<uint32_t, uint32_t> kIntentsDBTickers[] = {
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MISS, rocksdb::BLOCK_CACHE_MISS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_HIT, rocksdb::BLOCK_CACHE_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_ADD, rocksdb::BLOCK_CACHE_ADD},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_ADD_FAILURES, rocksdb::BLOCK_CACHE_ADD_FAILURES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_INDEX_MISS, rocksdb::BLOCK_CACHE_INDEX_MISS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_INDEX_HIT, rocksdb::BLOCK_CACHE_INDEX_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_FILTER_MISS, rocksdb::BLOCK_CACHE_FILTER_MISS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_FILTER_HIT, rocksdb::BLOCK_CACHE_FILTER_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_DATA_MISS, rocksdb::BLOCK_CACHE_DATA_MISS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_DATA_HIT, rocksdb::BLOCK_CACHE_DATA_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_BYTES_WRITE, rocksdb::BLOCK_CACHE_BYTES_WRITE},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOOM_FILTER_USEFUL, rocksdb::BLOOM_FILTER_USEFUL},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOOM_FILTER_CHECKED, rocksdb::BLOOM_FILTER_CHECKED},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_MEMTABLE_HIT, rocksdb::MEMTABLE_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_MEMTABLE_MISS, rocksdb::MEMTABLE_MISS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_GET_HIT_L0, rocksdb::GET_HIT_L0},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_GET_HIT_L1, rocksdb::GET_HIT_L1},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_GET_HIT_L2_AND_UP, rocksdb::GET_HIT_L2_AND_UP},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_KEYS_WRITTEN, rocksdb::NUMBER_KEYS_WRITTEN},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_KEYS_READ, rocksdb::NUMBER_KEYS_READ},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_KEYS_UPDATED, rocksdb::NUMBER_KEYS_UPDATED},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BYTES_WRITTEN, rocksdb::BYTES_WRITTEN},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BYTES_READ, rocksdb::BYTES_READ},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_SEEK, rocksdb::NUMBER_DB_SEEK},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_NEXT, rocksdb::NUMBER_DB_NEXT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_PREV, rocksdb::NUMBER_DB_PREV},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_SEEK_FOUND, rocksdb::NUMBER_DB_SEEK_FOUND},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_NEXT_FOUND, rocksdb::NUMBER_DB_NEXT_FOUND},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_PREV_FOUND, rocksdb::NUMBER_DB_PREV_FOUND},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_ITER_BYTES_READ, rocksdb::ITER_BYTES_READ},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NO_FILE_CLOSES, rocksdb::NO_FILE_CLOSES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NO_FILE_OPENS, rocksdb::NO_FILE_OPENS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NO_FILE_ERRORS, rocksdb::NO_FILE_ERRORS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_STALL_L0_SLOWDOWN_MICROS, rocksdb::STALL_L0_SLOWDOWN_MICROS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_STALL_MEMTABLE_COMPACTION_MICROS,
      rocksdb::STALL_MEMTABLE_COMPACTION_MICROS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_STALL_L0_NUM_FILES_MICROS,
      rocksdb::STALL_L0_NUM_FILES_MICROS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_STALL_MICROS, rocksdb::STALL_MICROS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_DB_MUTEX_WAIT_MICROS, rocksdb::DB_MUTEX_WAIT_MICROS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_RATE_LIMIT_DELAY_MILLIS, rocksdb::RATE_LIMIT_DELAY_MILLIS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NO_ITERATORS, rocksdb::NO_ITERATORS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_MULTIGET_CALLS, rocksdb::NUMBER_MULTIGET_CALLS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_MULTIGET_KEYS_READ,
      rocksdb::NUMBER_MULTIGET_KEYS_READ},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_MULTIGET_BYTES_READ,
      rocksdb::NUMBER_MULTIGET_BYTES_READ},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_FILTERED_DELETES, rocksdb::NUMBER_FILTERED_DELETES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_MERGE_FAILURES, rocksdb::NUMBER_MERGE_FAILURES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_SEQUENCE_NUMBER, rocksdb::SEQUENCE_NUMBER},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOOM_FILTER_PREFIX_CHECKED,
      rocksdb::BLOOM_FILTER_PREFIX_CHECKED},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOOM_FILTER_PREFIX_USEFUL,
      rocksdb::BLOOM_FILTER_PREFIX_USEFUL},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_OF_RESEEKS_IN_ITERATION,
      rocksdb::NUMBER_OF_RESEEKS_IN_ITERATION},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_GET_UPDATES_SINCE_CALLS, rocksdb::GET_UPDATES_SINCE_CALLS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_COMPRESSED_MISS,
      rocksdb::BLOCK_CACHE_COMPRESSED_MISS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_COMPRESSED_HIT,
      rocksdb::BLOCK_CACHE_COMPRESSED_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_COMPRESSED_ADD,
      rocksdb::BLOCK_CACHE_COMPRESSED_ADD},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_COMPRESSED_ADD_FAILURES,
      rocksdb::BLOCK_CACHE_COMPRESSED_ADD_FAILURES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_WAL_FILE_SYNCED, rocksdb::WAL_FILE_SYNCED},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_WAL_FILE_BYTES, rocksdb::WAL_FILE_BYTES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_WRITE_DONE_BY_SELF, rocksdb::WRITE_DONE_BY_SELF},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_WRITE_DONE_BY_OTHER, rocksdb::WRITE_DONE_BY_OTHER},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_WRITE_WITH_WAL, rocksdb::WRITE_WITH_WAL},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_COMPACT_READ_BYTES, rocksdb::COMPACT_READ_BYTES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_COMPACT_WRITE_BYTES, rocksdb::COMPACT_WRITE_BYTES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_FLUSH_WRITE_BYTES, rocksdb::FLUSH_WRITE_BYTES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DIRECT_LOAD_TABLE_PROPERTIES,
      rocksdb::NUMBER_DIRECT_LOAD_TABLE_PROPERTIES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_SUPERVERSION_ACQUIRES,
      rocksdb::NUMBER_SUPERVERSION_ACQUIRES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_SUPERVERSION_RELEASES,
      rocksdb::NUMBER_SUPERVERSION_RELEASES},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_SUPERVERSION_CLEANUPS,
      rocksdb::NUMBER_SUPERVERSION_CLEANUPS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NUMBER_BLOCK_NOT_COMPRESSED,
      rocksdb::NUMBER_BLOCK_NOT_COMPRESSED},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_MERGE_OPERATION_TOTAL_TIME,
      rocksdb::MERGE_OPERATION_TOTAL_TIME},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_FILTER_OPERATION_TOTAL_TIME,
      rocksdb::FILTER_OPERATION_TOTAL_TIME},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_ROW_CACHE_HIT, rocksdb::ROW_CACHE_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_ROW_CACHE_MISS, rocksdb::ROW_CACHE_MISS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_NO_TABLE_CACHE_ITERATORS,
      rocksdb::NO_TABLE_CACHE_ITERATORS},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_SINGLE_TOUCH_HIT,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_SINGLE_TOUCH_ADD,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_ADD},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE,
      rocksdb::BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MULTI_TOUCH_HIT,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_HIT},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MULTI_TOUCH_ADD,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_ADD},
  {pggate::YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE,
      rocksdb::BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE},
};

constexpr std::pair<uint32_t, uint32_t> kRegularDBEventStats[] = {
  {pggate::YB_STORAGE_EVENT_REGULARDB_DB_GET,
      rocksdb::DB_GET},
  {pggate::YB_STORAGE_EVENT_REGULARDB_DB_WRITE,
      rocksdb::DB_WRITE},
  {pggate::YB_STORAGE_EVENT_REGULARDB_COMPACTION_TIME,
      rocksdb::COMPACTION_TIME},
  {pggate::YB_STORAGE_EVENT_REGULARDB_WAL_FILE_SYNC_MICROS,
      rocksdb::WAL_FILE_SYNC_MICROS},
  {pggate::YB_STORAGE_EVENT_REGULARDB_DB_MULTIGET,
      rocksdb::DB_MULTIGET},
  {pggate::YB_STORAGE_EVENT_REGULARDB_READ_BLOCK_COMPACTION_MICROS,
      rocksdb::READ_BLOCK_COMPACTION_MICROS},
  {pggate::YB_STORAGE_EVENT_REGULARDB_READ_BLOCK_GET_MICROS,
      rocksdb::READ_BLOCK_GET_MICROS},
  {pggate::YB_STORAGE_EVENT_REGULARDB_WRITE_RAW_BLOCK_MICROS,
      rocksdb::WRITE_RAW_BLOCK_MICROS},
  {pggate::YB_STORAGE_EVENT_REGULARDB_NUM_FILES_IN_SINGLE_COMPACTION,
      rocksdb::NUM_FILES_IN_SINGLE_COMPACTION},
  {pggate::YB_STORAGE_EVENT_REGULARDB_DB_SEEK,
      rocksdb::DB_SEEK},
  {pggate::YB_STORAGE_EVENT_REGULARDB_SST_READ_MICROS,
      rocksdb::SST_READ_MICROS},
  {pggate::YB_STORAGE_EVENT_REGULARDB_BYTES_PER_READ,
      rocksdb::BYTES_PER_READ},
  {pggate::YB_STORAGE_EVENT_REGULARDB_BYTES_PER_WRITE,
      rocksdb::BYTES_PER_WRITE},
  {pggate::YB_STORAGE_EVENT_REGULARDB_BYTES_PER_MULTIGET,
      rocksdb::BYTES_PER_MULTIGET},
  {pggate::YB_STORAGE_EVENT_REGULARDB_BLOOM_FILTER_TIME_NANOS,
      rocksdb::BLOOM_FILTER_TIME_NANOS},
  {pggate::YB_STORAGE_EVENT_REGULARDB_GET_FIXED_SIZE_FILTER_BLOCK_HANDLE_NANOS,
      rocksdb::GET_FIXED_SIZE_FILTER_BLOCK_HANDLE_NANOS},
  {pggate::YB_STORAGE_EVENT_REGULARDB_GET_FILTER_BLOCK_FROM_CACHE_NANOS,
      rocksdb::GET_FILTER_BLOCK_FROM_CACHE_NANOS},
};

constexpr std::pair<uint32_t, uint32_t> kIntentsDBEventStats[] = {
  {pggate::YB_STORAGE_EVENT_INTENTSDB_DB_GET,
      rocksdb::DB_GET},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_DB_WRITE,
      rocksdb::DB_WRITE},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_COMPACTION_TIME,
      rocksdb::COMPACTION_TIME},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_WAL_FILE_SYNC_MICROS,
      rocksdb::WAL_FILE_SYNC_MICROS},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_DB_MULTIGET,
      rocksdb::DB_MULTIGET},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_READ_BLOCK_COMPACTION_MICROS,
      rocksdb::READ_BLOCK_COMPACTION_MICROS},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_READ_BLOCK_GET_MICROS,
      rocksdb::READ_BLOCK_GET_MICROS},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_WRITE_RAW_BLOCK_MICROS,
      rocksdb::WRITE_RAW_BLOCK_MICROS},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_NUM_FILES_IN_SINGLE_COMPACTION,
      rocksdb::NUM_FILES_IN_SINGLE_COMPACTION},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_DB_SEEK,
      rocksdb::DB_SEEK},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_SST_READ_MICROS,
      rocksdb::SST_READ_MICROS},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_BYTES_PER_READ,
      rocksdb::BYTES_PER_READ},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_BYTES_PER_WRITE,
      rocksdb::BYTES_PER_WRITE},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_BYTES_PER_MULTIGET,
      rocksdb::BYTES_PER_MULTIGET},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_BLOOM_FILTER_TIME_NANOS,
      rocksdb::BLOOM_FILTER_TIME_NANOS},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_GET_FIXED_SIZE_FILTER_BLOCK_HANDLE_NANOS,
      rocksdb::GET_FIXED_SIZE_FILTER_BLOCK_HANDLE_NANOS},
  {pggate::YB_STORAGE_EVENT_INTENTSDB_GET_FILTER_BLOCK_FROM_CACHE_NANOS,
      rocksdb::GET_FILTER_BLOCK_FROM_CACHE_NANOS},
};

constexpr std::pair<uint32_t, uint32_t> kRegularDBTickersForPgStatStatements[] = {
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_SEEK, rocksdb::NUMBER_DB_SEEK},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_NEXT, rocksdb::NUMBER_DB_NEXT},
  {pggate::YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_PREV, rocksdb::NUMBER_DB_PREV},
};

struct MetricsLists {
  std::span<const std::pair<uint32_t, uint32_t>> regular_db_tickers;
  std::span<const std::pair<uint32_t, uint32_t>> regular_db_event_stats;
  std::span<const std::pair<uint32_t, uint32_t>> intents_db_tickers;
  std::span<const std::pair<uint32_t, uint32_t>> intents_db_event_stats;
};

const MetricsLists& GetMetricsForCaptureType(PgsqlMetricsCaptureType metrics_capture) {
  static const MetricsLists kAllMetrics = {
      kRegularDBTickers,
      kRegularDBEventStats,
      kIntentsDBTickers,
      kIntentsDBEventStats,
  };
  static const MetricsLists kPgssMetrics = {
      kRegularDBTickersForPgStatStatements,
      {},
      {},
      {},
  };

  if (metrics_capture == PgsqlMetricsCaptureType::PGSQL_METRICS_CAPTURE_PGSS) {
    return kPgssMetrics;
  }
  return kAllMetrics;
}

template <class PB>
void CopyRocksDBStatisticsToPgsqlResponse(
    const rocksdb::Statistics& statistics,
    std::span<const std::pair<uint32_t, uint32_t>> tickers,
    std::span<const std::pair<uint32_t, uint32_t>> event_stats,
    PB* metrics) {
  for (const auto& [pggate_index, rocksdb_index] : tickers) {
    auto ticker = statistics.getTickerCount(rocksdb_index);
    // Don't send unchanged statistics.
    if (ticker == 0) {
      continue;
    }
    auto* metric = metrics->add_gauge_metrics();
    metric->set_metric(pggate_index);
    metric->set_value(ticker);
  }
  for (const auto& [pggate_index, rocksdb_index] : event_stats) {
    const auto& stats = statistics.getAggregateStats(rocksdb_index);
    // Don't send unchanged statistics.
    if (stats.TotalCount() == 0) {
      continue;
    }
    auto* metric = metrics->add_event_metrics();
    metric->set_metric(pggate_index);
    metric->set_sum(stats.TotalSum());
    metric->set_count(stats.TotalCount());
  }
}

size_t DumpRocksDBStatistics(
    const rocksdb::Statistics& statistics,
    std::span<const std::pair<uint32_t, uint32_t>> tickers,
    std::span<const std::pair<uint32_t, uint32_t>> event_stats,
    const char* name_prefix,
    std::stringstream* out) {
  size_t dumped = 0;
  for (const auto& [_, rocksdb_index] : tickers) {
    auto ticker = statistics.getTickerCount(rocksdb_index);
    // Don't dump unchanged statistics.
    if (ticker == 0) {
      continue;
    }
    const auto& ticker_name = rocksdb::TickersNameMap[rocksdb_index].second;
    (*out) << name_prefix << ticker_name << ": " << ticker << '\n';
    ++dumped;
  }
  for (const auto& [_, rocksdb_index] : event_stats) {
    const auto& stats = statistics.getAggregateStats(rocksdb_index);
    // Don't dump unchanged statistics.
    if (stats.TotalCount() == 0) {
      continue;
    }
    const auto& ticker_name = rocksdb::TickersNameMap[rocksdb_index].second;
    (*out) << name_prefix << ticker_name << ": "
           << "sum: " << stats.TotalSum() << ", "
           << "count: " << stats.TotalCount() << ", "
           << "min: " << stats.MinValue() << ", "
           << "max: " << stats.MaxValue() << '\n';
    ++dumped;
  }
  return dumped;
}

} // namespace

rocksdb::Statistics* DocDBStatistics::RegularDBStatistics() {
  return &regulardb_statistics_;
}

rocksdb::Statistics* DocDBStatistics::IntentsDBStatistics() {
  return &intentsdb_statistics_;
}

void DocDBStatistics::MergeAndClear(
    rocksdb::Statistics* regulardb_statistics,
    rocksdb::Statistics* intentsdb_statistics) {
  regulardb_statistics_.MergeAndClear(regulardb_statistics);
  intentsdb_statistics_.MergeAndClear(intentsdb_statistics);
}

size_t DocDBStatistics::Dump(std::stringstream* out) const {
  size_t dumped = 0;
  dumped += DumpRocksDBStatistics(
      regulardb_statistics_, std::span{kRegularDBTickers}, std::span{kRegularDBEventStats},
      "" /* name_prefix */, out);
  dumped += DumpRocksDBStatistics(
      intentsdb_statistics_, std::span{kIntentsDBTickers}, std::span{kIntentsDBEventStats},
      "intentsdb_" /* name_prefix */, out);
  return dumped;
}

void DocDBStatistics::CopyToPgsqlResponse(
    PgsqlResponsePB* response, PgsqlMetricsCaptureType metrics_capture) const {
  DoCopyToPgsqlResponse(response, metrics_capture);
}

void DocDBStatistics::CopyToPgsqlResponse(
    LWPgsqlResponsePB* response, PgsqlMetricsCaptureType metrics_capture) const {
  DoCopyToPgsqlResponse(response, metrics_capture);
}

template <class PB>
void DocDBStatistics::DoCopyToPgsqlResponse(
    PB* response, PgsqlMetricsCaptureType metrics_capture) const {
  auto* metrics = response->mutable_metrics();
  const auto& metrics_lists = GetMetricsForCaptureType(metrics_capture);
  CopyRocksDBStatisticsToPgsqlResponse(
      regulardb_statistics_, metrics_lists.regular_db_tickers,
      metrics_lists.regular_db_event_stats, metrics);
  if (FLAGS_ysql_analyze_dump_intentsdb_metrics) {
    CopyRocksDBStatisticsToPgsqlResponse(
        intentsdb_statistics_, metrics_lists.intents_db_tickers,
        metrics_lists.intents_db_event_stats, metrics);
  }
}

} // namespace yb::docdb
