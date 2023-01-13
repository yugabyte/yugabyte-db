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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/options.h"
#include "yb/rocksdb/immutable_options.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <limits>

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/sst_file_manager.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/merge_operator.h"
#include "yb/util/slice.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/wal_filter.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/util/compression.h"
#include "yb/rocksdb/util/statistics.h"

namespace rocksdb {

ImmutableCFOptions::ImmutableCFOptions(const Options& options)
    : compaction_style(options.compaction_style),
      compaction_options_universal(options.compaction_options_universal),
      compaction_options_fifo(options.compaction_options_fifo),
      prefix_extractor(options.prefix_extractor.get()),
      comparator(options.comparator),
      merge_operator(options.merge_operator.get()),
      compaction_filter(options.compaction_filter),
      compaction_filter_factory(options.compaction_filter_factory.get()),
      inplace_update_support(options.inplace_update_support),
      inplace_callback(options.inplace_callback),
      info_log(options.info_log.get()),
      statistics(options.statistics.get()),
      env(options.env),
      delayed_write_rate(options.delayed_write_rate),
      allow_mmap_reads(options.allow_mmap_reads),
      allow_mmap_writes(options.allow_mmap_writes),
      db_paths(options.db_paths),
      memtable_factory(options.memtable_factory.get()),
      table_factory(options.table_factory.get()),
      table_properties_collector_factories(
          options.table_properties_collector_factories),
      advise_random_on_open(options.advise_random_on_open),
      bloom_locality(options.bloom_locality),
      purge_redundant_kvs_while_flush(options.purge_redundant_kvs_while_flush),
      min_partial_merge_operands(options.min_partial_merge_operands),
      disable_data_sync(options.disableDataSync),
      use_fsync(options.use_fsync),
      compression(options.compression),
      compression_per_level(options.compression_per_level),
      compression_opts(options.compression_opts),
      level_compaction_dynamic_level_bytes(
          options.level_compaction_dynamic_level_bytes),
      access_hint_on_compaction_start(options.access_hint_on_compaction_start),
      new_table_reader_for_compaction_inputs(
          options.new_table_reader_for_compaction_inputs),
      compaction_readahead_size(options.compaction_readahead_size),
      num_levels(options.num_levels),
      optimize_filters_for_hits(options.optimize_filters_for_hits),
      listeners(options.listeners),
      row_cache(options.row_cache),
      mem_tracker(options.mem_tracker),
      block_based_table_mem_tracker(options.block_based_table_mem_tracker),
      iterator_replacer(options.iterator_replacer),
      compaction_file_filter_factory(options.compaction_file_filter_factory.get()),
      priority_thread_pool_metrics(options.priority_thread_pool_metrics) {}

ColumnFamilyOptions::ColumnFamilyOptions()
    : comparator(BytewiseComparator()),
      merge_operator(nullptr),
      compaction_filter(nullptr),
      compaction_filter_factory(nullptr),
      write_buffer_size(4_MB),  // Option expects bytes.
      max_write_buffer_number(2),
      min_write_buffer_number_to_merge(1),
      max_write_buffer_number_to_maintain(0),
      compression(Snappy_Supported() ? kSnappyCompression : kNoCompression),
      prefix_extractor(nullptr),
      num_levels(7),
      level0_file_num_compaction_trigger(4),
      level0_slowdown_writes_trigger(20),
      level0_stop_writes_trigger(24),
      target_file_size_base(2 * 1048576),
      target_file_size_multiplier(1),
      max_bytes_for_level_base(10 * 1048576),
      level_compaction_dynamic_level_bytes(false),
      max_bytes_for_level_multiplier(10),
      max_bytes_for_level_multiplier_additional(num_levels, 1),
      expanded_compaction_factor(25),
      source_compaction_factor(1),
      max_grandparent_overlap_factor(10),
      soft_rate_limit(0.0),
      hard_rate_limit(0.0),
      soft_pending_compaction_bytes_limit(0),
      hard_pending_compaction_bytes_limit(0),
      rate_limit_delay_max_milliseconds(1000),
      arena_block_size(0),
      disable_auto_compactions(false),
      purge_redundant_kvs_while_flush(true),
      compaction_style(kCompactionStyleLevel),
      compaction_pri(kByCompensatedSize),
      verify_checksums_in_compaction(true),
      filter_deletes(false),
      max_sequential_skip_in_iterations(8),
      memtable_factory(std::shared_ptr<SkipListFactory>(new SkipListFactory)),
      table_factory(
          std::shared_ptr<TableFactory>(new BlockBasedTableFactory())),
      inplace_update_support(false),
      inplace_update_num_locks(10000),
      inplace_callback(nullptr),
      memtable_prefix_bloom_bits(0),
      memtable_prefix_bloom_probes(6),
      memtable_prefix_bloom_huge_page_tlb_size(0),
      bloom_locality(0),
      max_successive_merges(0),
      min_partial_merge_operands(2),
      optimize_filters_for_hits(false),
      paranoid_file_checks(false),
      compaction_measure_io_stats(false) {
  assert(memtable_factory.get() != nullptr);
}

ColumnFamilyOptions::ColumnFamilyOptions(const Options& options)
    : comparator(options.comparator),
      merge_operator(options.merge_operator),
      compaction_filter(options.compaction_filter),
      compaction_filter_factory(options.compaction_filter_factory),
      write_buffer_size(options.write_buffer_size),
      max_write_buffer_number(options.max_write_buffer_number),
      min_write_buffer_number_to_merge(
          options.min_write_buffer_number_to_merge),
      max_write_buffer_number_to_maintain(
          options.max_write_buffer_number_to_maintain),
      compression(options.compression),
      compression_per_level(options.compression_per_level),
      compression_opts(options.compression_opts),
      prefix_extractor(options.prefix_extractor),
      num_levels(options.num_levels),
      level0_file_num_compaction_trigger(
          options.level0_file_num_compaction_trigger),
      level0_slowdown_writes_trigger(options.level0_slowdown_writes_trigger),
      level0_stop_writes_trigger(options.level0_stop_writes_trigger),
      target_file_size_base(options.target_file_size_base),
      target_file_size_multiplier(options.target_file_size_multiplier),
      max_bytes_for_level_base(options.max_bytes_for_level_base),
      level_compaction_dynamic_level_bytes(
          options.level_compaction_dynamic_level_bytes),
      max_bytes_for_level_multiplier(options.max_bytes_for_level_multiplier),
      max_bytes_for_level_multiplier_additional(
          options.max_bytes_for_level_multiplier_additional),
      expanded_compaction_factor(options.expanded_compaction_factor),
      source_compaction_factor(options.source_compaction_factor),
      max_grandparent_overlap_factor(options.max_grandparent_overlap_factor),
      soft_rate_limit(options.soft_rate_limit),
      soft_pending_compaction_bytes_limit(
          options.soft_pending_compaction_bytes_limit),
      hard_pending_compaction_bytes_limit(
          options.hard_pending_compaction_bytes_limit),
      rate_limit_delay_max_milliseconds(
          options.rate_limit_delay_max_milliseconds),
      arena_block_size(options.arena_block_size),
      disable_auto_compactions(options.disable_auto_compactions),
      purge_redundant_kvs_while_flush(options.purge_redundant_kvs_while_flush),
      compaction_style(options.compaction_style),
      compaction_pri(options.compaction_pri),
      verify_checksums_in_compaction(options.verify_checksums_in_compaction),
      compaction_options_universal(options.compaction_options_universal),
      compaction_options_fifo(options.compaction_options_fifo),
      filter_deletes(options.filter_deletes),
      max_sequential_skip_in_iterations(
          options.max_sequential_skip_in_iterations),
      memtable_factory(options.memtable_factory),
      table_factory(options.table_factory),
      table_properties_collector_factories(
          options.table_properties_collector_factories),
      inplace_update_support(options.inplace_update_support),
      inplace_update_num_locks(options.inplace_update_num_locks),
      inplace_callback(options.inplace_callback),
      memtable_prefix_bloom_bits(options.memtable_prefix_bloom_bits),
      memtable_prefix_bloom_probes(options.memtable_prefix_bloom_probes),
      memtable_prefix_bloom_huge_page_tlb_size(
          options.memtable_prefix_bloom_huge_page_tlb_size),
      bloom_locality(options.bloom_locality),
      max_successive_merges(options.max_successive_merges),
      min_partial_merge_operands(options.min_partial_merge_operands),
      optimize_filters_for_hits(options.optimize_filters_for_hits),
      paranoid_file_checks(options.paranoid_file_checks),
      compaction_measure_io_stats(options.compaction_measure_io_stats) {
  assert(memtable_factory.get() != nullptr);
  if (max_bytes_for_level_multiplier_additional.size() <
      static_cast<unsigned int>(num_levels)) {
    max_bytes_for_level_multiplier_additional.resize(num_levels, 1);
  }
}

DBOptions::DBOptions()
    : create_if_missing(false),
      create_missing_column_families(false),
      error_if_exists(false),
      paranoid_checks(true),
      env(Env::Default()),
      checkpoint_env(nullptr),
      rate_limiter(nullptr),
      sst_file_manager(nullptr),
      info_log(nullptr),
#ifdef NDEBUG
      info_log_level(INFO_LEVEL),
#else
      info_log_level(DEBUG_LEVEL),
#endif  // NDEBUG
      max_open_files(5000),
      max_file_opening_threads(1),
      max_total_wal_size(0),
      statistics(nullptr),
      disableDataSync(false),
      use_fsync(false),
      db_log_dir(""),
      wal_dir(""),
      delete_obsolete_files_period_micros(6ULL * 60 * 60 * 1000000),
      base_background_compactions(-1),
      max_background_compactions(1),
      num_reserved_small_compaction_threads(-1),
      compaction_size_threshold_bytes(std::numeric_limits<uint64_t>::max()),
      max_subcompactions(1),
      max_background_flushes(1),
      max_log_file_size(0),
      log_file_time_to_roll(0),
      keep_log_file_num(1000),
      recycle_log_file_num(0),
      max_manifest_file_size(std::numeric_limits<uint64_t>::max()),
      table_cache_numshardbits(4),
      WAL_ttl_seconds(0),
      WAL_size_limit_MB(0),
      manifest_preallocation_size(64 * 1024),
      allow_os_buffer(true),
      allow_mmap_reads(false),
      allow_mmap_writes(false),
      allow_fallocate(true),
      is_fd_close_on_exec(true),
      skip_log_error_on_recovery(false),
      stats_dump_period_sec(600),
      advise_random_on_open(true),
      db_write_buffer_size(0),
      access_hint_on_compaction_start(NORMAL),
      new_table_reader_for_compaction_inputs(false),
      compaction_readahead_size(0),
      random_access_max_buffer_size(1024 * 1024),
      writable_file_max_buffer_size(1024 * 1024),
      use_adaptive_mutex(false),
      bytes_per_sync(0),
      wal_bytes_per_sync(0),
      listeners(),
      enable_thread_tracking(false),
      delayed_write_rate(2 * 1024U * 1024U),
      allow_concurrent_memtable_write(false),
      enable_write_thread_adaptive_yield(false),
      write_thread_max_yield_usec(100),
      write_thread_slow_yield_usec(3),
      skip_stats_update_on_db_open(false),
      wal_recovery_mode(WALRecoveryMode::kTolerateCorruptedTailRecords),
      row_cache(nullptr),
      wal_filter(nullptr),
      fail_if_options_file_error(false) {
}

static const char* const access_hints[] = {
  "NONE", "NORMAL", "SEQUENTIAL", "WILLNEED"
};

void DBOptions::Dump(Logger* log) const {
  RHEADER(log, "         Options.error_if_exists: %d", error_if_exists);
  RHEADER(log, "       Options.create_if_missing: %d", create_if_missing);
  RHEADER(log, "         Options.paranoid_checks: %d", paranoid_checks);
  RHEADER(log, "                     Options.env: %p", env);
  RHEADER(log, "                Options.info_log: %p", info_log.get());
  RHEADER(log, "          Options.max_open_files: %d", max_open_files);
  RHEADER(log,
      "Options.max_file_opening_threads: %d", max_file_opening_threads);
  RHEADER(log,
      "      Options.max_total_wal_size: %" PRIu64, max_total_wal_size);
  RHEADER(log, "       Options.disableDataSync: %d", disableDataSync);
  RHEADER(log, "             Options.use_fsync: %d", use_fsync);
  RHEADER(log, "     Options.max_log_file_size: %" ROCKSDB_PRIszt,
         max_log_file_size);
  RHEADER(log, "Options.max_manifest_file_size: %" PRIu64,
      max_manifest_file_size);
  RHEADER(log, "     Options.log_file_time_to_roll: %" ROCKSDB_PRIszt,
         log_file_time_to_roll);
  RHEADER(log, "     Options.keep_log_file_num: %" ROCKSDB_PRIszt,
         keep_log_file_num);
  RHEADER(log, "  Options.recycle_log_file_num: %" ROCKSDB_PRIszt,
           recycle_log_file_num);
  RHEADER(log, "       Options.allow_os_buffer: %d", allow_os_buffer);
  RHEADER(log, "      Options.allow_mmap_reads: %d", allow_mmap_reads);
  RHEADER(log, "      Options.allow_fallocate: %d", allow_fallocate);
  RHEADER(log, "     Options.allow_mmap_writes: %d", allow_mmap_writes);
  RHEADER(log, "         Options.create_missing_column_families: %d",
      create_missing_column_families);
  RHEADER(log, "                             Options.db_log_dir: %s",
      db_log_dir.c_str());
  RHEADER(log, "                                Options.wal_dir: %s",
      wal_dir.c_str());
  RHEADER(log, "               Options.table_cache_numshardbits: %d",
      table_cache_numshardbits);
  RHEADER(log, "    Options.delete_obsolete_files_period_micros: %" PRIu64,
      delete_obsolete_files_period_micros);
  RHEADER(log, "             Options.base_background_compactions: %d",
      base_background_compactions);
  RHEADER(log, "             Options.max_background_compactions: %d",
      max_background_compactions);
  RHEADER(log, "                     Options.max_subcompactions: %" PRIu32,
      max_subcompactions);
  RHEADER(log, "                 Options.max_background_flushes: %d",
      max_background_flushes);
  RHEADER(log, "                        Options.WAL_ttl_seconds: %" PRIu64,
      WAL_ttl_seconds);
  RHEADER(log, "                      Options.WAL_size_limit_MB: %" PRIu64,
      WAL_size_limit_MB);
  RHEADER(log,
      "            Options.manifest_preallocation_size: %" ROCKSDB_PRIszt,
         manifest_preallocation_size);
  RHEADER(log, "                         Options.allow_os_buffer: %d",
      allow_os_buffer);
  RHEADER(log, "                        Options.allow_mmap_reads: %d",
      allow_mmap_reads);
  RHEADER(log, "                       Options.allow_mmap_writes: %d",
      allow_mmap_writes);
  RHEADER(log, "                     Options.is_fd_close_on_exec: %d",
      is_fd_close_on_exec);
  RHEADER(log, "                   Options.stats_dump_period_sec: %u",
      stats_dump_period_sec);
  RHEADER(log, "                   Options.advise_random_on_open: %d",
      advise_random_on_open);
  RHEADER(log,
      "                    Options.db_write_buffer_size: %" ROCKSDB_PRIszt
         "d",
         db_write_buffer_size);
  RHEADER(log, "         Options.access_hint_on_compaction_start: %s",
      access_hints[access_hint_on_compaction_start]);
  RHEADER(log, "  Options.new_table_reader_for_compaction_inputs: %d",
      new_table_reader_for_compaction_inputs);
  RHEADER(log,
      "               Options.compaction_readahead_size: %" ROCKSDB_PRIszt
         "d",
         compaction_readahead_size);
  RHEADER(
      log,
      "               Options.random_access_max_buffer_size: %" ROCKSDB_PRIszt
        "d",
        random_access_max_buffer_size);
  RHEADER(log,
      "              Options.writable_file_max_buffer_size: %" ROCKSDB_PRIszt
         "d",
         writable_file_max_buffer_size);
  RHEADER(log, "                      Options.use_adaptive_mutex: %d",
      use_adaptive_mutex);
  RHEADER(log, "                            Options.rate_limiter: %p",
      rate_limiter.get());
  RHEADER(
      log, "     Options.sst_file_manager.rate_bytes_per_sec: %" PRIi64,
      sst_file_manager ? sst_file_manager->GetDeleteRateBytesPerSecond() : 0);
  RHEADER(log, "                          Options.bytes_per_sync: %" PRIu64,
      bytes_per_sync);
  RHEADER(log, "                      Options.wal_bytes_per_sync: %" PRIu64,
      wal_bytes_per_sync);
  RHEADER(log, "                       Options.wal_recovery_mode: %d",
      wal_recovery_mode);
  RHEADER(log, "                  Options.enable_thread_tracking: %d",
      enable_thread_tracking);
  RHEADER(log, "         Options.allow_concurrent_memtable_write: %d",
      allow_concurrent_memtable_write);
  RHEADER(log, "      Options.enable_write_thread_adaptive_yield: %d",
      enable_write_thread_adaptive_yield);
  RHEADER(log, "             Options.write_thread_max_yield_usec: %" PRIu64,
      write_thread_max_yield_usec);
  RHEADER(log, "            Options.write_thread_slow_yield_usec: %" PRIu64,
      write_thread_slow_yield_usec);
    if (row_cache) {
      RHEADER(log, "                               Options.row_cache: %" PRIu64,
          row_cache->GetCapacity());
    } else {
      RHEADER(log, "                               Options.row_cache: None");
    }
  RHEADER(log, "                           Options.initial_seqno: %" PRIu64, initial_seqno);
  RHEADER(log, "       Options.wal_filter: %s",
      wal_filter ? wal_filter->Name() : "None");
}  // DBOptions::Dump

void ColumnFamilyOptions::Dump(Logger* log) const {
  RHEADER(log, "              Options.comparator: %s", comparator->Name());
  RHEADER(log, "          Options.merge_operator: %s",
      merge_operator ? merge_operator->Name() : "None");
  RHEADER(log, "       Options.compaction_filter: %s",
      compaction_filter ? compaction_filter->Name() : "None");
  RHEADER(log, "       Options.compaction_filter_factory: %s",
      compaction_filter_factory ? compaction_filter_factory->Name() : "None");
  RHEADER(log, "        Options.memtable_factory: %s", memtable_factory->Name());
  RHEADER(log, "           Options.table_factory: %s", table_factory->Name());
  RHEADER(log, "           table_factory options: %s",
      table_factory->GetPrintableTableOptions().c_str());
  RHEADER(log, "       Options.write_buffer_size: %" ROCKSDB_PRIszt,
       write_buffer_size);
  RHEADER(log, " Options.max_write_buffer_number: %d", max_write_buffer_number);
    if (!compression_per_level.empty()) {
      for (unsigned int i = 0; i < compression_per_level.size(); i++) {
        RHEADER(log, "       Options.compression[%d]: %s", i,
            CompressionTypeToString(compression_per_level[i]).c_str());
      }
    } else {
      RHEADER(log, "         Options.compression: %s",
          CompressionTypeToString(compression).c_str());
    }
  RHEADER(log, "      Options.prefix_extractor: %s",
      prefix_extractor == nullptr ? "nullptr" : prefix_extractor->Name());
  RHEADER(log, "            Options.num_levels: %d", num_levels);
  RHEADER(log, "       Options.min_write_buffer_number_to_merge: %d",
      min_write_buffer_number_to_merge);
  RHEADER(log, "    Options.max_write_buffer_number_to_maintain: %d",
      max_write_buffer_number_to_maintain);
  RHEADER(log, "           Options.compression_opts.window_bits: %d",
      compression_opts.window_bits);
  RHEADER(log, "                 Options.compression_opts.level: %d",
      compression_opts.level);
  RHEADER(log, "              Options.compression_opts.strategy: %d",
      compression_opts.strategy);
  RHEADER(log, "     Options.level0_file_num_compaction_trigger: %d",
      level0_file_num_compaction_trigger);
  RHEADER(log, "         Options.level0_slowdown_writes_trigger: %d",
      level0_slowdown_writes_trigger);
  RHEADER(log, "             Options.level0_stop_writes_trigger: %d",
      level0_stop_writes_trigger);
  RHEADER(log, "                  Options.target_file_size_base: %" PRIu64,
      target_file_size_base);
  RHEADER(log, "            Options.target_file_size_multiplier: %d",
      target_file_size_multiplier);
  RHEADER(log, "               Options.max_bytes_for_level_base: %" PRIu64,
      max_bytes_for_level_base);
  RHEADER(log, "Options.level_compaction_dynamic_level_bytes: %d",
      level_compaction_dynamic_level_bytes);
  RHEADER(log, "         Options.max_bytes_for_level_multiplier: %d",
      max_bytes_for_level_multiplier);
    for (size_t i = 0; i < max_bytes_for_level_multiplier_additional.size();
         i++) {
      RHEADER(log,
          "Options.max_bytes_for_level_multiplier_addtl[%" ROCKSDB_PRIszt
                "]: %d",
           i, max_bytes_for_level_multiplier_additional[i]);
    }
  RHEADER(log, "      Options.max_sequential_skip_in_iterations: %" PRIu64,
      max_sequential_skip_in_iterations);
  RHEADER(log, "             Options.expanded_compaction_factor: %d",
      expanded_compaction_factor);
  RHEADER(log, "               Options.source_compaction_factor: %d",
      source_compaction_factor);
  RHEADER(log, "         Options.max_grandparent_overlap_factor: %d",
      max_grandparent_overlap_factor);

  RHEADER(log,
      "                       Options.arena_block_size: %" ROCKSDB_PRIszt,
         arena_block_size);
  RHEADER(log, "  Options.soft_pending_compaction_bytes_limit: %" PRIu64,
      soft_pending_compaction_bytes_limit);
  RHEADER(log, "  Options.hard_pending_compaction_bytes_limit: %" PRIu64,
      hard_pending_compaction_bytes_limit);
  RHEADER(log, "      Options.rate_limit_delay_max_milliseconds: %u",
      rate_limit_delay_max_milliseconds);
  RHEADER(log, "               Options.disable_auto_compactions: %d",
      disable_auto_compactions);
  RHEADER(log, "                          Options.filter_deletes: %d",
      filter_deletes);
  RHEADER(log, "          Options.verify_checksums_in_compaction: %d",
      verify_checksums_in_compaction);
  RHEADER(log, "                        Options.compaction_style: %d",
      compaction_style);
  RHEADER(log, "                          Options.compaction_pri: %d",
      compaction_pri);
  RHEADER(log, " Options.compaction_options_universal.size_ratio: %u",
      compaction_options_universal.size_ratio);
  RHEADER(log, "Options.compaction_options_universal."
          "always_include_size_threshold: %" ROCKSDB_PRIszt,
          compaction_options_universal.always_include_size_threshold);
  RHEADER(log, "Options.compaction_options_universal.min_merge_width: %u",
      compaction_options_universal.min_merge_width);
  RHEADER(log, "Options.compaction_options_universal.max_merge_width: %u",
      compaction_options_universal.max_merge_width);
  RHEADER(log, "Options.compaction_options_universal."
          "max_size_amplification_percent: %u",
      compaction_options_universal.max_size_amplification_percent);
  RHEADER(log,
      "Options.compaction_options_universal.compression_size_percent: %d",
      compaction_options_universal.compression_size_percent);
  RHEADER(log,
      "Options.compaction_options_fifo.max_table_files_size: %" PRIu64,
      compaction_options_fifo.max_table_files_size);
    std::string collector_names;
    for (const auto& collector_factory : table_properties_collector_factories) {
      collector_names.append(collector_factory->Name());
      collector_names.append("; ");
    }
  RHEADER(log, "                  Options.table_properties_collectors: %s",
      collector_names.c_str());
  RHEADER(log, "                  Options.inplace_update_support: %d",
      inplace_update_support);
  RHEADER(log,
      "                Options.inplace_update_num_locks: %" ROCKSDB_PRIszt,
         inplace_update_num_locks);
  RHEADER(log, "              Options.min_partial_merge_operands: %u",
      min_partial_merge_operands);
    // TODO: easier config for bloom (maybe based on avg key/value size)
  RHEADER(log, "              Options.memtable_prefix_bloom_bits: %d",
      memtable_prefix_bloom_bits);
  RHEADER(log, "            Options.memtable_prefix_bloom_probes: %d",
      memtable_prefix_bloom_probes);

  RHEADER(log,
      "  Options.memtable_prefix_bloom_huge_page_tlb_size: %" ROCKSDB_PRIszt,
         memtable_prefix_bloom_huge_page_tlb_size);
  RHEADER(log, "                          Options.bloom_locality: %d",
      bloom_locality);

  RHEADER(log,
      "                   Options.max_successive_merges: %" ROCKSDB_PRIszt,
         max_successive_merges);
  RHEADER(log, "               Options.optimize_filters_for_hits: %d",
      optimize_filters_for_hits);
  RHEADER(log, "               Options.paranoid_file_checks: %d",
      paranoid_file_checks);
  RHEADER(log, "               Options.compaction_measure_io_stats: %d",
      compaction_measure_io_stats);
}  // ColumnFamilyOptions::Dump

void Options::Dump(Logger* log) const {
  DBOptions::Dump(log);
  ColumnFamilyOptions::Dump(log);
}   // Options::Dump

void Options::DumpCFOptions(Logger* log) const {
  ColumnFamilyOptions::Dump(log);
}  // Options::DumpCFOptions

//
// The goal of this method is to create a configuration that
// allows an application to write all files into L0 and
// then do a single compaction to output all files into L1.
Options*
Options::PrepareForBulkLoad() {
  // never slowdown ingest.
  level0_file_num_compaction_trigger = (1<<30);
  level0_slowdown_writes_trigger = (1<<30);
  level0_stop_writes_trigger = (1<<30);

  // no auto compactions please. The application should issue a
  // manual compaction after all data is loaded into L0.
  disable_auto_compactions = true;
  disableDataSync = true;

  // A manual compaction run should pick all files in L0 in
  // a single compaction run.
  source_compaction_factor = (1<<30);

  // It is better to have only 2 levels, otherwise a manual
  // compaction would compact at every possible level, thereby
  // increasing the total time needed for compactions.
  num_levels = 2;

  // Need to allow more write buffers to allow more parallism
  // of flushes.
  max_write_buffer_number = 6;
  min_write_buffer_number_to_merge = 1;

  // When compaction is disabled, more parallel flush threads can
  // help with write throughput.
  max_background_flushes = 4;

  // Prevent a memtable flush to automatically promote files
  // to L1. This is helpful so that all files that are
  // input to the manual compaction are all at L0.
  max_background_compactions = 2;
  base_background_compactions = 2;

  // The compaction would create large files in L1.
  target_file_size_base = 256 * 1024 * 1024;
  return this;
}

// Optimization functions
ColumnFamilyOptions* ColumnFamilyOptions::OptimizeForPointLookup(
    uint64_t block_cache_size_mb) {
  prefix_extractor.reset(NewNoopTransform());
  BlockBasedTableOptions block_based_options;
  block_based_options.index_type = IndexType::kHashSearch;
  block_based_options.filter_policy.reset(NewBloomFilterPolicy(10));
  block_based_options.block_cache =
      NewLRUCache(static_cast<size_t>(block_cache_size_mb * 1024 * 1024));
  table_factory.reset(new BlockBasedTableFactory(block_based_options));
  memtable_factory.reset(NewHashLinkListRepFactory());
  return this;
}

ColumnFamilyOptions* ColumnFamilyOptions::OptimizeLevelStyleCompaction(
    uint64_t memtable_memory_budget) {
  write_buffer_size = static_cast<size_t>(memtable_memory_budget / 4);
  // merge two memtables when flushing to L0
  min_write_buffer_number_to_merge = 2;
  // this means we'll use 50% extra memory in the worst case, but will reduce
  // write stalls.
  max_write_buffer_number = 6;
  // start flushing L0->L1 as soon as possible. each file on level0 is
  // (memtable_memory_budget / 2). This will flush level 0 when it's bigger than
  // memtable_memory_budget.
  level0_file_num_compaction_trigger = 2;
  // doesn't really matter much, but we don't want to create too many files
  target_file_size_base = memtable_memory_budget / 8;
  // make Level1 size equal to Level0 size, so that L0->L1 compactions are fast
  max_bytes_for_level_base = memtable_memory_budget;

  // level style compaction
  compaction_style = kCompactionStyleLevel;

  // only compress levels >= 2
  compression_per_level.resize(num_levels);
  for (int i = 0; i < num_levels; ++i) {
    if (i < 2) {
      compression_per_level[i] = kNoCompression;
    } else {
      compression_per_level[i] = kSnappyCompression;
    }
  }
  return this;
}

ColumnFamilyOptions* ColumnFamilyOptions::OptimizeUniversalStyleCompaction(
    uint64_t memtable_memory_budget) {
  write_buffer_size = static_cast<size_t>(memtable_memory_budget / 4);
  // merge two memtables when flushing to L0
  min_write_buffer_number_to_merge = 2;
  // this means we'll use 50% extra memory in the worst case, but will reduce
  // write stalls.
  max_write_buffer_number = 6;
  // universal style compaction
  compaction_style = kCompactionStyleUniversal;
  compaction_options_universal.compression_size_percent = 80;
  return this;
}

DBOptions* DBOptions::IncreaseParallelism(int total_threads) {
  max_background_compactions = total_threads - 1;
  max_background_flushes = 1;
  env->SetBackgroundThreads(total_threads, Env::LOW);
  env->SetBackgroundThreads(1, Env::HIGH);
  return this;
}


const ReadOptions ReadOptions::kDefault;

ReadOptions::ReadOptions()
    : verify_checksums(true),
      fill_cache(true),
      snapshot(nullptr),
      iterate_upper_bound(nullptr),
      read_tier(kReadAllTier),
      tailing(false),
      managed(false),
      total_order_seek(false),
      prefix_same_as_start(false),
      pin_data(false),
      query_id(rocksdb::kDefaultQueryId) {
}

ReadOptions::ReadOptions(bool cksum, bool cache)
    : verify_checksums(cksum),
      fill_cache(cache),
      snapshot(nullptr),
      iterate_upper_bound(nullptr),
      read_tier(kReadAllTier),
      tailing(false),
      managed(false),
      total_order_seek(false),
      prefix_same_as_start(false),
      pin_data(false),
      query_id(rocksdb::kDefaultQueryId) {
}

std::atomic<int64_t> flush_tick_(1);

int64_t FlushTick() {
  return flush_tick_.fetch_add(1, std::memory_order_acq_rel);
}

}  // namespace rocksdb
