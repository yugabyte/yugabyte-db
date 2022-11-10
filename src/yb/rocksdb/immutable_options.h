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

#include <string>
#include <vector>
#include "yb/rocksdb/options.h"

namespace yb {

class MemTracker;

}

namespace rocksdb {

// ImmutableCFOptions is a data struct used by RocksDB internal. It contains a
// subset of Options that should not be changed during the entire lifetime
// of DB. You shouldn't need to access this data structure unless you are
// implementing a new TableFactory. Raw pointers defined in this struct do
// not have ownership to the data they point to. Options contains shared_ptr
// to these data.
struct ImmutableCFOptions {
  explicit ImmutableCFOptions(const Options& options);

  CompactionStyle compaction_style;

  CompactionOptionsUniversal compaction_options_universal;
  CompactionOptionsFIFO compaction_options_fifo;

  const SliceTransform* prefix_extractor;

  const Comparator* comparator;

  MergeOperator* merge_operator;

  CompactionFilter* compaction_filter;

  CompactionFilterFactory* compaction_filter_factory;

  bool inplace_update_support;

  UpdateStatus (*inplace_callback)(char* existing_value,
                                   uint32_t* existing_value_size,
                                   Slice delta_value,
                                   std::string* merged_value);

  Logger* info_log;

  Statistics* statistics;

  InfoLogLevel info_log_level;

  Env* env;

  uint64_t delayed_write_rate;

  // Allow the OS to mmap file for reading sst tables. Default: false
  bool allow_mmap_reads;

  // Allow the OS to mmap file for writing. Default: false
  bool allow_mmap_writes;

  std::vector<DbPath> db_paths;

  MemTableRepFactory* memtable_factory;

  TableFactory* table_factory;

  Options::TablePropertiesCollectorFactories
    table_properties_collector_factories;

  bool advise_random_on_open;

  // This options is required by PlainTableReader. May need to move it
  // to PlainTalbeOptions just like bloom_bits_per_key
  uint32_t bloom_locality;

  bool purge_redundant_kvs_while_flush;

  uint32_t min_partial_merge_operands;

  bool disable_data_sync;

  bool use_fsync;

  CompressionType compression;

  std::vector<CompressionType> compression_per_level;

  CompressionOptions compression_opts;

  bool level_compaction_dynamic_level_bytes;

  Options::AccessHint access_hint_on_compaction_start;

  bool new_table_reader_for_compaction_inputs;

  size_t compaction_readahead_size;

  int num_levels;

  bool optimize_filters_for_hits;

  // A vector of EventListeners which call-back functions will be called
  // when specific RocksDB event happens.
  std::vector<std::shared_ptr<EventListener>> listeners;

  std::shared_ptr<Cache> row_cache;

  std::shared_ptr<yb::MemTracker> mem_tracker;

  std::shared_ptr<yb::MemTracker> block_based_table_mem_tracker;

  std::shared_ptr<IteratorReplacer> iterator_replacer;

  CompactionFileFilterFactory* compaction_file_filter_factory;

  std::shared_ptr<RocksDBPriorityThreadPoolMetrics> priority_thread_pool_metrics;
};

}  // namespace rocksdb
