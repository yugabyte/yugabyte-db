// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "yb/gutil/ref_counted.h"

namespace yb {
class MetricEntity;
class AggregateStats;
}

namespace rocksdb {

/**
 * Keep adding ticker's here.
 *  1. Any ticker should be added before TICKER_ENUM_MAX.
 *  2. Add a readable string in TickersNameMap below for the newly added ticker.
 *  3. Make sure to add the ticker to the list in src/yb/yql/pggate/pg_metrics_list.h and
 *     src/yb/docdb/docdb_statistics.cc as well.
 */
enum Tickers : uint32_t {
  // total block cache misses
  // REQUIRES: BLOCK_CACHE_MISS == BLOCK_CACHE_INDEX_MISS +
  //                               BLOCK_CACHE_FILTER_MISS +
  //                               BLOCK_CACHE_DATA_MISS;
  BLOCK_CACHE_MISS = 0,
  // total block cache hit
  // REQUIRES: BLOCK_CACHE_HIT == BLOCK_CACHE_INDEX_HIT +
  //                              BLOCK_CACHE_FILTER_HIT +
  //                              BLOCK_CACHE_DATA_HIT;
  BLOCK_CACHE_HIT,
  // # of blocks added to block cache.
  BLOCK_CACHE_ADD,
  // # of failures when adding blocks to block cache.
  BLOCK_CACHE_ADD_FAILURES,
  // # of times cache miss when accessing index block from block cache.
  BLOCK_CACHE_INDEX_MISS,
  // # of times cache hit when accessing index block from block cache.
  BLOCK_CACHE_INDEX_HIT,
  // # of times cache miss when accessing filter block from block cache.
  BLOCK_CACHE_FILTER_MISS,
  // # of times cache hit when accessing filter block from block cache.
  BLOCK_CACHE_FILTER_HIT,
  // # of times cache miss when accessing data block from block cache.
  BLOCK_CACHE_DATA_MISS,
  // # of times cache hit when accessing data block from block cache.
  BLOCK_CACHE_DATA_HIT,
  // # of bytes read from cache.
  BLOCK_CACHE_BYTES_READ,
  // # of bytes written into cache.
  BLOCK_CACHE_BYTES_WRITE,
  // # of times bloom filter has avoided file reads.
  BLOOM_FILTER_USEFUL,
  // # of times bloom filter has been checked.
  BLOOM_FILTER_CHECKED,

  // # of memtable hits.
  MEMTABLE_HIT,
  // # of memtable misses.
  MEMTABLE_MISS,

  // # of Get() queries served by L0
  GET_HIT_L0,
  // # of Get() queries served by L1
  GET_HIT_L1,
  // # of Get() queries served by L2 and up
  GET_HIT_L2_AND_UP,

  /**
   * COMPACTION_KEY_DROP_* count the reasons for key drop during compaction
   * There are 3 reasons currently.
   */
  COMPACTION_KEY_DROP_NEWER_ENTRY,  // key was written with a newer value.
  COMPACTION_KEY_DROP_OBSOLETE,     // The key is obsolete.
  COMPACTION_KEY_DROP_USER,  // user compaction function has dropped the key.

  // Number of keys written to the database via the Put and Write call's
  NUMBER_KEYS_WRITTEN,
  // Number of Keys read,
  NUMBER_KEYS_READ,
  // Number keys updated, if inplace update is enabled
  NUMBER_KEYS_UPDATED,
  // The number of uncompressed bytes issued by DB::Put(), DB::Delete(),
  // DB::Merge(), and DB::Write().
  BYTES_WRITTEN,
  // The number of uncompressed bytes read from DB::Get().  It could be
  // either from memtables, cache, or table files.
  // For the number of logical bytes read from DB::MultiGet(),
  // please use NUMBER_MULTIGET_BYTES_READ.
  BYTES_READ,
  // The number of calls to seek/next/prev
  NUMBER_DB_SEEK,
  NUMBER_DB_NEXT,
  NUMBER_DB_PREV,
  // The number of calls to seek/next/prev that returned data
  NUMBER_DB_SEEK_FOUND,
  NUMBER_DB_NEXT_FOUND,
  NUMBER_DB_PREV_FOUND,
  // The number of uncompressed bytes read from an iterator.
  // Includes size of key and value.
  ITER_BYTES_READ,
  NO_FILE_CLOSES,
  NO_FILE_OPENS,
  NO_FILE_ERRORS,
  // DEPRECATED Time system had to wait to do LO-L1 compactions
  STALL_L0_SLOWDOWN_MICROS,
  // DEPRECATED Time system had to wait to move memtable to L1.
  STALL_MEMTABLE_COMPACTION_MICROS,
  // DEPRECATED write throttle because of too many files in L0
  STALL_L0_NUM_FILES_MICROS,
  // Writer has to wait for compaction or flush to finish.
  STALL_MICROS,
  // The wait time for db mutex.
  // Disabled by default. To enable it set stats level to kAll
  DB_MUTEX_WAIT_MICROS,
  RATE_LIMIT_DELAY_MILLIS,
  NO_ITERATORS,  // number of iterators currently open

  // Number of MultiGet calls, keys read, and bytes read
  NUMBER_MULTIGET_CALLS,
  NUMBER_MULTIGET_KEYS_READ,
  NUMBER_MULTIGET_BYTES_READ,

  // Number of deletes records that were not required to be
  // written to storage because key does not exist
  NUMBER_FILTERED_DELETES,
  NUMBER_MERGE_FAILURES,
  SEQUENCE_NUMBER,

  // number of times bloom was checked before creating iterator on a
  // file, and the number of times the check was useful in avoiding
  // iterator creation (and thus likely IOPs).
  BLOOM_FILTER_PREFIX_CHECKED,
  BLOOM_FILTER_PREFIX_USEFUL,

  // Number of times we had to reseek inside an iteration to skip
  // over large number of keys with same userkey.
  NUMBER_OF_RESEEKS_IN_ITERATION,

  // Record the number of calls to GetUpadtesSince. Useful to keep track of
  // transaction log iterator refreshes
  GET_UPDATES_SINCE_CALLS,
  BLOCK_CACHE_COMPRESSED_MISS,  // miss in the compressed block cache
  BLOCK_CACHE_COMPRESSED_HIT,   // hit in the compressed block cache
  // Number of blocks added to comopressed block cache
  BLOCK_CACHE_COMPRESSED_ADD,
  // Number of failures when adding blocks to compressed block cache
  BLOCK_CACHE_COMPRESSED_ADD_FAILURES,
  WAL_FILE_SYNCED,  // Number of times WAL sync is done
  WAL_FILE_BYTES,   // Number of bytes written to WAL

  // Writes can be processed by requesting thread or by the thread at the
  // head of the writers queue.
  WRITE_DONE_BY_SELF,
  WRITE_DONE_BY_OTHER,  // Equivalent to writes done for others
  WRITE_WITH_WAL,       // Number of Write calls that request WAL
  COMPACT_READ_BYTES,   // Bytes read during compaction
  COMPACT_WRITE_BYTES,  // Bytes written during compaction
  FLUSH_WRITE_BYTES,    // Bytes written during flush

  // Number of table's properties loaded directly from file, without creating
  // table reader object.
  NUMBER_DIRECT_LOAD_TABLE_PROPERTIES,
  NUMBER_SUPERVERSION_ACQUIRES,
  NUMBER_SUPERVERSION_RELEASES,
  NUMBER_SUPERVERSION_CLEANUPS,
  NUMBER_BLOCK_NOT_COMPRESSED,
  // Size of all the SST Files for the current version.
  CURRENT_VERSION_SST_FILES_SIZE,
  OLD_BK_COMPAT_CURRENT_VERSION_SST_FILES_SIZE,
  CURRENT_VERSION_SST_FILES_UNCOMPRESSED_SIZE,
  CURRENT_VERSION_NUM_SST_FILES,
  MERGE_OPERATION_TOTAL_TIME,
  FILTER_OPERATION_TOTAL_TIME,

  // Row cache.
  ROW_CACHE_HIT,
  ROW_CACHE_MISS,

  // Table cache.
  NO_TABLE_CACHE_ITERATORS,

  // Single-touch and multi-touch statistics.
  BLOCK_CACHE_SINGLE_TOUCH_HIT,
  BLOCK_CACHE_SINGLE_TOUCH_ADD,
  BLOCK_CACHE_SINGLE_TOUCH_BYTES_READ,
  BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE,
  BLOCK_CACHE_MULTI_TOUCH_HIT,
  BLOCK_CACHE_MULTI_TOUCH_ADD,
  BLOCK_CACHE_MULTI_TOUCH_BYTES_READ,
  BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE,

  // Files filtered during compaction due to TTL expiration
  COMPACTION_FILES_FILTERED,
  COMPACTION_FILES_NOT_FILTERED,

  READAHEAD_RESET,
  READAHEAD_CALLS,
  READAHEAD_BYTES_READ,

  // End of ticker enum.
  TICKER_ENUM_MAX,
};

// The order of items listed in  Tickers should be the same as
// the order listed in TickersNameMap
extern const std::pair<Tickers, const char *> TickersNameMap[];

/**
 * Keep adding histogram's here.
 * Any histogram whould have value less than HISTOGRAM_ENUM_MAX
 * Add a new Histogram by assigning it the current value of HISTOGRAM_ENUM_MAX
 * Add a string representation in HistogramsNameMap below
 * And increment HISTOGRAM_ENUM_MAX
 */
enum Histograms : uint32_t {
  DB_GET = 0,
  DB_WRITE,
  COMPACTION_TIME,
  WAL_FILE_SYNC_MICROS,
  DB_MULTIGET,
  READ_BLOCK_COMPACTION_MICROS,
  READ_BLOCK_GET_MICROS,
  WRITE_RAW_BLOCK_MICROS,
  NUM_FILES_IN_SINGLE_COMPACTION,
  DB_SEEK,
  SST_READ_MICROS,
  // Value size distribution in each operation
  BYTES_PER_READ,
  BYTES_PER_WRITE,
  BYTES_PER_MULTIGET,
  HISTOGRAM_ENUM_MAX,  // TODO(ldemailly): enforce HistogramsNameMap match
};

extern const std::pair<Histograms, const char *> HistogramsNameMap[];

struct HistogramData {
  double count;
  double min;
  double max;
  double sum;
  double average;
};

enum StatsLevel {
  // Collect all stats except the counters requiring to get time inside the
  // mutex lock.
  kExceptTimeForMutex,
  // Collect all stats, including measuring duration of mutex operations.
  // If getting time is expensive on the platform to run, it can
  // reduce scalability to more threads, especialy for writes.
  kAll,
};

// Analyze the performance of a db
class Statistics {
 public:
  virtual ~Statistics() {}

  virtual uint64_t getTickerCount(uint32_t tickerType) const = 0;
  virtual void histogramData(uint32_t type,
                             HistogramData* const data) const = 0;
  virtual const yb::AggregateStats& getAggregateStats(uint32_t histogramType) const = 0;
  virtual void recordTick(uint32_t tickerType, uint64_t count = 0) = 0;
  virtual void setTickerCount(uint32_t tickerType, uint64_t count) = 0;
  virtual void measureTime(uint32_t histogramType, uint64_t time) = 0;
  virtual void addHistogram(uint32_t histogramType, const yb::AggregateStats& stats) = 0;
  virtual void resetTickersForTest() = 0;

  virtual const char* GetTickerName(uint32_t ticker_type) const {
    return "tickerName(): not implemented";
  }

  // String representation of the statistic object.
  virtual std::string ToString() const {
    // Do nothing by default
    return std::string("ToString(): not implemented");
  }

  // Override this function to disable particular histogram collection
  virtual bool HistEnabledForType(uint32_t type) const {
    return type < HISTOGRAM_ENUM_MAX;
  }

  StatsLevel stats_level_ = kExceptTimeForMutex;
};

// Create a concrete DBStatistics object
std::shared_ptr<Statistics> CreateDBStatistics(
    const scoped_refptr<yb::MetricEntity>& hist_entity,
    const scoped_refptr<yb::MetricEntity>& tick_entity,
    const bool for_intents = false);
std::shared_ptr<Statistics> CreateDBStatisticsForTests(bool for_intents = false);

}  // namespace rocksdb
