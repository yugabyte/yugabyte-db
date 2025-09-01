
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

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <string>
#include <unordered_map>

#include <boost/preprocessor/stringize.hpp>

#include <gtest/gtest.h>

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/convenience.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/utilities/leveldb_options.h"
#include "yb/rocksdb/util/options_helper.h"
#include "yb/rocksdb/util/options_parser.h"
#include "yb/rocksdb/util/options_sanity_check.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/env.h"
#include "yb/util/test_macros.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/util/format.h"

#ifndef GFLAGS
bool ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_print) = false;
#else
#include "yb/util/flags.h"
using GFLAGS::ParseCommandLineFlags;
DEFINE_NON_RUNTIME_bool(enable_print, false, "Print options generated to console.");
#endif  // GFLAGS

namespace rocksdb {

class StderrLogger : public Logger {
 public:
  using Logger::Logv;
  void Logv(const char* format, va_list ap) override {
    vprintf(format, ap);
    printf("\n");
  }
};

Options PrintAndGetOptions(size_t total_write_buffer_limit,
                           int read_amplification_threshold,
                           int write_amplification_threshold,
                           uint64_t target_db_size = 68719476736) {
  StderrLogger logger;

  if (FLAGS_enable_print) {
    printf("---- total_write_buffer_limit: %" ROCKSDB_PRIszt
           " "
           "read_amplification_threshold: %d write_amplification_threshold: %d "
           "target_db_size %" PRIu64 " ----\n",
           total_write_buffer_limit, read_amplification_threshold,
           write_amplification_threshold, target_db_size);
  }

  Options options =
      GetOptions(total_write_buffer_limit, read_amplification_threshold,
                 write_amplification_threshold, target_db_size);
  if (FLAGS_enable_print) {
    options.Dump(&logger);
    printf("-------------------------------------\n\n\n");
  }
  return options;
}

class OptionsTest : public RocksDBTest {};

TEST_F(OptionsTest, LooseCondition) {
  Options options;
  PrintAndGetOptions(static_cast<size_t>(10) * 1024 * 1024 * 1024, 100, 100);

  // Less mem table memory budget
  PrintAndGetOptions(32 * 1024 * 1024, 100, 100);

  // Tight read amplification
  options = PrintAndGetOptions(128 * 1024 * 1024, 8, 100);
  ASSERT_EQ(options.compaction_style, kCompactionStyleLevel);

  // Tight write amplification
  options = PrintAndGetOptions(128 * 1024 * 1024, 64, 10);
  ASSERT_EQ(options.compaction_style, kCompactionStyleUniversal);

  // Both tight amplifications
  PrintAndGetOptions(128 * 1024 * 1024, 4, 8);
}

TEST_F(OptionsTest, GetOptionsFromMapTest) {
  std::unordered_map<std::string, std::string> cf_options_map = {
      {"write_buffer_size", "1"},
      {"max_write_buffer_number", "2"},
      {"min_write_buffer_number_to_merge", "3"},
      {"max_write_buffer_number_to_maintain", "99"},
      {"compression", "kSnappyCompression"},
      {"compression_per_level",
       "kNoCompression:"
       "kSnappyCompression:"
       "kZlibCompression:"
       "kBZip2Compression:"
       "kLZ4Compression:"
       "kLZ4HCCompression:"
       "kZSTDNotFinalCompression"},
      {"compression_opts", "4:5:6"},
      {"num_levels", "7"},
      {"level0_file_num_compaction_trigger", "8"},
      {"level0_slowdown_writes_trigger", "9"},
      {"level0_stop_writes_trigger", "10"},
      {"target_file_size_base", "12"},
      {"target_file_size_multiplier", "13"},
      {"max_bytes_for_level_base", "14"},
      {"level_compaction_dynamic_level_bytes", "true"},
      {"max_bytes_for_level_multiplier", "15"},
      {"max_bytes_for_level_multiplier_additional", "16:17:18"},
      {"expanded_compaction_factor", "19"},
      {"source_compaction_factor", "20"},
      {"max_grandparent_overlap_factor", "21"},
      {"soft_rate_limit", "1.1"},
      {"hard_rate_limit", "2.1"},
      {"hard_pending_compaction_bytes_limit", "211"},
      {"arena_block_size", "22"},
      {"disable_auto_compactions", "true"},
      {"compaction_style", "kCompactionStyleLevel"},
      {"verify_checksums_in_compaction", "false"},
      {"compaction_options_fifo", "23"},
      {"filter_deletes", "0"},
      {"max_sequential_skip_in_iterations", "24"},
      {"inplace_update_support", "true"},
      {"compaction_measure_io_stats", "true"},
      {"inplace_update_num_locks", "25"},
      {"memtable_prefix_bloom_bits", "26"},
      {"memtable_prefix_bloom_probes", "27"},
      {"memtable_prefix_bloom_huge_page_tlb_size", "28"},
      {"bloom_locality", "29"},
      {"max_successive_merges", "30"},
      {"min_partial_merge_operands", "31"},
      {"prefix_extractor", "fixed:31"},
      {"optimize_filters_for_hits", "true"},
  };

  std::unordered_map<std::string, std::string> db_options_map = {
      {"create_if_missing", "false"},
      {"create_missing_column_families", "true"},
      {"error_if_exists", "false"},
      {"paranoid_checks", "true"},
      {"max_open_files", "32"},
      {"max_total_wal_size", "33"},
      {"disable_data_sync", "false"},
      {"use_fsync", "true"},
      {"db_log_dir", "/db_log_dir"},
      {"wal_dir", "/wal_dir"},
      {"delete_obsolete_files_period_micros", "34"},
      {"max_background_compactions", "35"},
      {"max_background_flushes", "36"},
      {"max_log_file_size", "37"},
      {"log_file_time_to_roll", "38"},
      {"keep_log_file_num", "39"},
      {"recycle_log_file_num", "5"},
      {"max_manifest_file_size", "40"},
      {"table_cache_numshardbits", "41"},
      {"WAL_ttl_seconds", "43"},
      {"WAL_size_limit_MB", "44"},
      {"manifest_preallocation_size", "45"},
      {"allow_os_buffer", "false"},
      {"allow_mmap_reads", "true"},
      {"allow_mmap_writes", "false"},
      {"is_fd_close_on_exec", "true"},
      {"skip_log_error_on_recovery", "false"},
      {"stats_dump_period_sec", "46"},
      {"advise_random_on_open", "true"},
      {"use_adaptive_mutex", "false"},
      {"new_table_reader_for_compaction_inputs", "true"},
      {"compaction_readahead_size", "100"},
      {"random_access_max_buffer_size", "3145728"},
      {"writable_file_max_buffer_size", "314159"},
      {"bytes_per_sync", "47"},
      {"wal_bytes_per_sync", "48"},
  };

  ColumnFamilyOptions base_cf_opt;
  ColumnFamilyOptions new_cf_opt;
  ASSERT_OK(GetColumnFamilyOptionsFromMap(
            base_cf_opt, cf_options_map, &new_cf_opt));
  ASSERT_EQ(new_cf_opt.write_buffer_size, 1U);
  ASSERT_EQ(new_cf_opt.max_write_buffer_number, 2);
  ASSERT_EQ(new_cf_opt.min_write_buffer_number_to_merge, 3);
  ASSERT_EQ(new_cf_opt.max_write_buffer_number_to_maintain, 99);
  ASSERT_EQ(new_cf_opt.compression, kSnappyCompression);
  ASSERT_EQ(new_cf_opt.compression_per_level.size(), 7U);
  ASSERT_EQ(new_cf_opt.compression_per_level[0], kNoCompression);
  ASSERT_EQ(new_cf_opt.compression_per_level[1], kSnappyCompression);
  ASSERT_EQ(new_cf_opt.compression_per_level[2], kZlibCompression);
  ASSERT_EQ(new_cf_opt.compression_per_level[3], kBZip2Compression);
  ASSERT_EQ(new_cf_opt.compression_per_level[4], kLZ4Compression);
  ASSERT_EQ(new_cf_opt.compression_per_level[5], kLZ4HCCompression);
  ASSERT_EQ(new_cf_opt.compression_per_level[6], kZSTDNotFinalCompression);
  ASSERT_EQ(new_cf_opt.compression_opts.window_bits, 4);
  ASSERT_EQ(new_cf_opt.compression_opts.level, 5);
  ASSERT_EQ(new_cf_opt.compression_opts.strategy, 6);
  ASSERT_EQ(new_cf_opt.num_levels, 7);
  ASSERT_EQ(new_cf_opt.level0_file_num_compaction_trigger, 8);
  ASSERT_EQ(new_cf_opt.level0_slowdown_writes_trigger, 9);
  ASSERT_EQ(new_cf_opt.level0_stop_writes_trigger, 10);
  ASSERT_EQ(new_cf_opt.target_file_size_base, static_cast<uint64_t>(12));
  ASSERT_EQ(new_cf_opt.target_file_size_multiplier, 13);
  ASSERT_EQ(new_cf_opt.max_bytes_for_level_base, 14U);
  ASSERT_EQ(new_cf_opt.level_compaction_dynamic_level_bytes, true);
  ASSERT_EQ(new_cf_opt.max_bytes_for_level_multiplier, 15);
  ASSERT_EQ(new_cf_opt.max_bytes_for_level_multiplier_additional.size(), 3U);
  ASSERT_EQ(new_cf_opt.max_bytes_for_level_multiplier_additional[0], 16);
  ASSERT_EQ(new_cf_opt.max_bytes_for_level_multiplier_additional[1], 17);
  ASSERT_EQ(new_cf_opt.max_bytes_for_level_multiplier_additional[2], 18);
  ASSERT_EQ(new_cf_opt.expanded_compaction_factor, 19);
  ASSERT_EQ(new_cf_opt.source_compaction_factor, 20);
  ASSERT_EQ(new_cf_opt.max_grandparent_overlap_factor, 21);
  ASSERT_EQ(new_cf_opt.soft_rate_limit, 1.1);
  ASSERT_EQ(new_cf_opt.hard_pending_compaction_bytes_limit, 211);
  ASSERT_EQ(new_cf_opt.arena_block_size, 22U);
  ASSERT_EQ(new_cf_opt.disable_auto_compactions, true);
  ASSERT_EQ(new_cf_opt.compaction_style, kCompactionStyleLevel);
  ASSERT_EQ(new_cf_opt.verify_checksums_in_compaction, false);
  ASSERT_EQ(new_cf_opt.compaction_options_fifo.max_table_files_size,
            static_cast<uint64_t>(23));
  ASSERT_EQ(new_cf_opt.filter_deletes, false);
  ASSERT_EQ(new_cf_opt.max_sequential_skip_in_iterations,
            static_cast<uint64_t>(24));
  ASSERT_EQ(new_cf_opt.inplace_update_support, true);
  ASSERT_EQ(new_cf_opt.inplace_update_num_locks, 25U);
  ASSERT_EQ(new_cf_opt.memtable_prefix_bloom_bits, 26U);
  ASSERT_EQ(new_cf_opt.memtable_prefix_bloom_probes, 27U);
  ASSERT_EQ(new_cf_opt.memtable_prefix_bloom_huge_page_tlb_size, 28U);
  ASSERT_EQ(new_cf_opt.bloom_locality, 29U);
  ASSERT_EQ(new_cf_opt.max_successive_merges, 30U);
  ASSERT_EQ(new_cf_opt.min_partial_merge_operands, 31U);
  ASSERT_TRUE(new_cf_opt.prefix_extractor != nullptr);
  ASSERT_EQ(new_cf_opt.optimize_filters_for_hits, true);
  ASSERT_EQ(std::string(new_cf_opt.prefix_extractor->Name()),
            "rocksdb.FixedPrefix.31");

  cf_options_map["write_buffer_size"] = "hello";
  ASSERT_NOK(GetColumnFamilyOptionsFromMap(
             base_cf_opt, cf_options_map, &new_cf_opt));
  cf_options_map["write_buffer_size"] = "1";
  ASSERT_OK(GetColumnFamilyOptionsFromMap(
            base_cf_opt, cf_options_map, &new_cf_opt));
  cf_options_map["unknown_option"] = "1";
  ASSERT_NOK(GetColumnFamilyOptionsFromMap(
             base_cf_opt, cf_options_map, &new_cf_opt));

  DBOptions base_db_opt;
  DBOptions new_db_opt;
  ASSERT_OK(GetDBOptionsFromMap(base_db_opt, db_options_map, &new_db_opt));
  ASSERT_EQ(new_db_opt.create_if_missing, false);
  ASSERT_EQ(new_db_opt.create_missing_column_families, true);
  ASSERT_EQ(new_db_opt.error_if_exists, false);
  ASSERT_EQ(new_db_opt.paranoid_checks, true);
  ASSERT_EQ(new_db_opt.max_open_files, 32);
  ASSERT_EQ(new_db_opt.max_total_wal_size, static_cast<uint64_t>(33));
  ASSERT_EQ(new_db_opt.disableDataSync, false);
  ASSERT_EQ(new_db_opt.use_fsync, true);
  ASSERT_EQ(new_db_opt.db_log_dir, "/db_log_dir");
  ASSERT_EQ(new_db_opt.wal_dir, "/wal_dir");
  ASSERT_EQ(new_db_opt.delete_obsolete_files_period_micros,
            static_cast<uint64_t>(34));
  ASSERT_EQ(new_db_opt.max_background_compactions, 35);
  ASSERT_EQ(new_db_opt.max_background_flushes, 36);
  ASSERT_EQ(new_db_opt.max_log_file_size, 37U);
  ASSERT_EQ(new_db_opt.log_file_time_to_roll, 38U);
  ASSERT_EQ(new_db_opt.keep_log_file_num, 39U);
  ASSERT_EQ(new_db_opt.recycle_log_file_num, 5U);
  ASSERT_EQ(new_db_opt.max_manifest_file_size, static_cast<uint64_t>(40));
  ASSERT_EQ(new_db_opt.table_cache_numshardbits, 41);
  ASSERT_EQ(new_db_opt.WAL_ttl_seconds, static_cast<uint64_t>(43));
  ASSERT_EQ(new_db_opt.WAL_size_limit_MB, static_cast<uint64_t>(44));
  ASSERT_EQ(new_db_opt.manifest_preallocation_size, 45U);
  ASSERT_EQ(new_db_opt.allow_os_buffer, false);
  ASSERT_EQ(new_db_opt.allow_mmap_reads, true);
  ASSERT_EQ(new_db_opt.allow_mmap_writes, false);
  ASSERT_EQ(new_db_opt.is_fd_close_on_exec, true);
  ASSERT_EQ(new_db_opt.skip_log_error_on_recovery, false);
  ASSERT_EQ(new_db_opt.stats_dump_period_sec, 46U);
  ASSERT_EQ(new_db_opt.advise_random_on_open, true);
  ASSERT_EQ(new_db_opt.use_adaptive_mutex, false);
  ASSERT_EQ(new_db_opt.new_table_reader_for_compaction_inputs, true);
  ASSERT_EQ(new_db_opt.compaction_readahead_size, 100);
  ASSERT_EQ(new_db_opt.random_access_max_buffer_size, 3145728);
  ASSERT_EQ(new_db_opt.writable_file_max_buffer_size, 314159);
  ASSERT_EQ(new_db_opt.bytes_per_sync, static_cast<uint64_t>(47));
  ASSERT_EQ(new_db_opt.wal_bytes_per_sync, static_cast<uint64_t>(48));
}

TEST_F(OptionsTest, GetColumnFamilyOptionsFromStringTest) {
  ColumnFamilyOptions base_cf_opt;
  ColumnFamilyOptions new_cf_opt;
  base_cf_opt.table_factory.reset();
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt, "", &new_cf_opt));
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=5", &new_cf_opt));
  ASSERT_EQ(new_cf_opt.write_buffer_size, 5U);
  ASSERT_TRUE(new_cf_opt.table_factory == nullptr);
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=6;", &new_cf_opt));
  ASSERT_EQ(new_cf_opt.write_buffer_size, 6U);
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "  write_buffer_size =  7  ", &new_cf_opt));
  ASSERT_EQ(new_cf_opt.write_buffer_size, 7U);
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "  write_buffer_size =  8 ; ", &new_cf_opt));
  ASSERT_EQ(new_cf_opt.write_buffer_size, 8U);
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=9;max_write_buffer_number=10", &new_cf_opt));
  ASSERT_EQ(new_cf_opt.write_buffer_size, 9U);
  ASSERT_EQ(new_cf_opt.max_write_buffer_number, 10);
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=11; max_write_buffer_number  =  12 ;",
            &new_cf_opt));
  ASSERT_EQ(new_cf_opt.write_buffer_size, 11U);
  ASSERT_EQ(new_cf_opt.max_write_buffer_number, 12);
  // Wrong name "max_write_buffer_number_"
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=13;max_write_buffer_number_=14;",
              &new_cf_opt));
  // Wrong key/value pair
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=13;max_write_buffer_number;", &new_cf_opt));
  // Error Paring value
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=13;max_write_buffer_number=;", &new_cf_opt));
  // Missing option name
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=13; =100;", &new_cf_opt));

  const int64_t kilo = 1024UL;
  const int64_t mega = 1024 * kilo;
  const int64_t giga = 1024 * mega;
  const int64_t tera = 1024 * giga;

  // Units (k)
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "memtable_prefix_bloom_bits=14k;max_write_buffer_number=-15K",
            &new_cf_opt));
  ASSERT_EQ(new_cf_opt.memtable_prefix_bloom_bits, 14UL * kilo);
  ASSERT_EQ(new_cf_opt.max_write_buffer_number, -15 * kilo);
  // Units (m)
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "max_write_buffer_number=16m;inplace_update_num_locks=17M",
            &new_cf_opt));
  ASSERT_EQ(new_cf_opt.max_write_buffer_number, 16 * mega);
  ASSERT_EQ(new_cf_opt.inplace_update_num_locks, 17 * mega);
  // Units (g)
  ASSERT_OK(GetColumnFamilyOptionsFromString(
      base_cf_opt,
      "write_buffer_size=18g;prefix_extractor=capped:8;"
      "arena_block_size=19G",
      &new_cf_opt));

  ASSERT_EQ(new_cf_opt.write_buffer_size, 18 * giga);
  ASSERT_EQ(new_cf_opt.arena_block_size, 19 * giga);
  ASSERT_TRUE(new_cf_opt.prefix_extractor.get() != nullptr);
  std::string prefix_name(new_cf_opt.prefix_extractor->Name());
  ASSERT_EQ(prefix_name, "rocksdb.CappedPrefix.8");

  // Units (t)
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=20t;arena_block_size=21T", &new_cf_opt));
  ASSERT_EQ(new_cf_opt.write_buffer_size, 20 * tera);
  ASSERT_EQ(new_cf_opt.arena_block_size, 21 * tera);

  // Nested block based table options
  // Emtpy
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=10;max_write_buffer_number=16;"
            "block_based_table_factory={};arena_block_size=1024",
            &new_cf_opt));
  ASSERT_TRUE(new_cf_opt.table_factory != nullptr);
  // Non-empty
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=10;max_write_buffer_number=16;"
            "block_based_table_factory={block_cache=1M;block_size=4;};"
            "arena_block_size=1024",
            &new_cf_opt));
  ASSERT_TRUE(new_cf_opt.table_factory != nullptr);
  // Last one
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=10;max_write_buffer_number=16;"
            "block_based_table_factory={block_cache=1M;block_size=4;}",
            &new_cf_opt));
  ASSERT_TRUE(new_cf_opt.table_factory != nullptr);
  // Mismatch curly braces
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=10;max_write_buffer_number=16;"
             "block_based_table_factory={{{block_size=4;};"
             "arena_block_size=1024",
             &new_cf_opt));
  // Unexpected chars after closing curly brace
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=10;max_write_buffer_number=16;"
             "block_based_table_factory={block_size=4;}};"
             "arena_block_size=1024",
             &new_cf_opt));
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=10;max_write_buffer_number=16;"
             "block_based_table_factory={block_size=4;}xdfa;"
             "arena_block_size=1024",
             &new_cf_opt));
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=10;max_write_buffer_number=16;"
             "block_based_table_factory={block_size=4;}xdfa",
             &new_cf_opt));
  // Invalid block based table option
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
             "write_buffer_size=10;max_write_buffer_number=16;"
             "block_based_table_factory={xx_block_size=4;}",
             &new_cf_opt));
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
           "optimize_filters_for_hits=true",
           &new_cf_opt));
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "optimize_filters_for_hits=false",
            &new_cf_opt));
  ASSERT_NOK(GetColumnFamilyOptionsFromString(base_cf_opt,
              "optimize_filters_for_hits=junk",
              &new_cf_opt));

  // Nested plain table options
  // Emtpy
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=10;max_write_buffer_number=16;"
            "plain_table_factory={};arena_block_size=1024",
            &new_cf_opt));
  ASSERT_TRUE(new_cf_opt.table_factory != nullptr);
  ASSERT_EQ(std::string(new_cf_opt.table_factory->Name()), "PlainTable");
  // Non-empty
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=10;max_write_buffer_number=16;"
            "plain_table_factory={user_key_len=66;bloom_bits_per_key=20;};"
            "arena_block_size=1024",
            &new_cf_opt));
  ASSERT_TRUE(new_cf_opt.table_factory != nullptr);
  ASSERT_EQ(std::string(new_cf_opt.table_factory->Name()), "PlainTable");

  // memtable factory
  ASSERT_OK(GetColumnFamilyOptionsFromString(base_cf_opt,
            "write_buffer_size=10;max_write_buffer_number=16;"
            "memtable=skip_list:10;arena_block_size=1024",
            &new_cf_opt));
  ASSERT_TRUE(new_cf_opt.memtable_factory != nullptr);
  ASSERT_EQ(std::string(new_cf_opt.memtable_factory->Name()), "SkipListFactory");
}

TEST_F(OptionsTest, GetBlockBasedTableOptionsFromString) {
  BlockBasedTableOptions table_opt;
  BlockBasedTableOptions new_opt;
  // make sure default values are overwritten by something else
  ASSERT_OK(GetBlockBasedTableOptionsFromString(table_opt,
            "cache_index_and_filter_blocks=1;index_type=kHashSearch;"
            "checksum=kxxHash;hash_index_allow_collision=1;no_block_cache=1;"
            "block_cache=1M;block_cache_compressed=1k;block_size=1024;filter_block_size=4096;"
            "block_size_deviation=8;block_restart_interval=4;index_block_size=16384;"
            "min_keys_per_index_block=16;filter_policy=bloomfilter:4:true;whole_key_filtering=1;"
            "skip_table_builder_flush=1",
            &new_opt));
  ASSERT_TRUE(new_opt.cache_index_and_filter_blocks);
  ASSERT_EQ(new_opt.index_type, IndexType::kHashSearch);
  ASSERT_EQ(new_opt.checksum, ChecksumType::kxxHash);
  ASSERT_TRUE(new_opt.hash_index_allow_collision);
  ASSERT_TRUE(new_opt.no_block_cache);
  ASSERT_TRUE(new_opt.block_cache != nullptr);
  ASSERT_EQ(new_opt.block_cache->GetCapacity(), 1024UL*1024UL);
  ASSERT_TRUE(new_opt.block_cache_compressed != nullptr);
  ASSERT_EQ(new_opt.block_cache_compressed->GetCapacity(), 1024UL);
  ASSERT_EQ(new_opt.block_size, 1024UL);
  ASSERT_EQ(new_opt.filter_block_size, 4096UL);
  ASSERT_EQ(new_opt.block_size_deviation, 8);
  ASSERT_EQ(new_opt.block_restart_interval, 4);
  ASSERT_EQ(new_opt.index_block_size, 16384UL);
  ASSERT_EQ(new_opt.min_keys_per_index_block, 16);
  ASSERT_TRUE(new_opt.filter_policy != nullptr);
  ASSERT_TRUE(new_opt.skip_table_builder_flush);

  // unknown option
  ASSERT_NOK(GetBlockBasedTableOptionsFromString(table_opt,
             "cache_index_and_filter_blocks=1;index_type=kBinarySearch;"
             "bad_option=1",
             &new_opt));

  // unrecognized index type
  ASSERT_NOK(GetBlockBasedTableOptionsFromString(table_opt,
             "cache_index_and_filter_blocks=1;index_type=kBinarySearchXX",
             &new_opt));

  // unrecognized checksum type
  ASSERT_NOK(GetBlockBasedTableOptionsFromString(table_opt,
             "cache_index_and_filter_blocks=1;checksum=kxxHashXX",
             &new_opt));

  // unrecognized filter policy name
  ASSERT_NOK(GetBlockBasedTableOptionsFromString(table_opt,
             "cache_index_and_filter_blocks=1;"
             "filter_policy=bloomfilterxx:4:true",
             &new_opt));
  // unrecognized filter policy config
  ASSERT_NOK(GetBlockBasedTableOptionsFromString(table_opt,
             "cache_index_and_filter_blocks=1;"
             "filter_policy=bloomfilter:4",
             &new_opt));
}


TEST_F(OptionsTest, GetPlainTableOptionsFromString) {
  PlainTableOptions table_opt;
  PlainTableOptions new_opt;
  // make sure default values are overwritten by something else
  ASSERT_OK(GetPlainTableOptionsFromString(table_opt,
            "user_key_len=66;bloom_bits_per_key=20;hash_table_ratio=0.5;"
            "index_sparseness=8;huge_page_tlb_size=4;encoding_type=kPrefix;"
            "full_scan_mode=true;store_index_in_file=true",
            &new_opt));
  ASSERT_EQ(new_opt.user_key_len, 66);
  ASSERT_EQ(new_opt.bloom_bits_per_key, 20);
  ASSERT_EQ(new_opt.hash_table_ratio, 0.5);
  ASSERT_EQ(new_opt.index_sparseness, 8);
  ASSERT_EQ(new_opt.huge_page_tlb_size, 4);
  ASSERT_EQ(new_opt.encoding_type, EncodingType::kPrefix);
  ASSERT_TRUE(new_opt.full_scan_mode);
  ASSERT_TRUE(new_opt.store_index_in_file);

  // unknown option
  ASSERT_NOK(GetPlainTableOptionsFromString(table_opt,
             "user_key_len=66;bloom_bits_per_key=20;hash_table_ratio=0.5;"
             "bad_option=1",
             &new_opt));

  // unrecognized EncodingType
  ASSERT_NOK(GetPlainTableOptionsFromString(table_opt,
             "user_key_len=66;bloom_bits_per_key=20;hash_table_ratio=0.5;"
             "encoding_type=kPrefixXX",
             &new_opt));
}

TEST_F(OptionsTest, GetMemTableRepFactoryFromString) {
  std::unique_ptr<MemTableRepFactory> new_mem_factory = nullptr;

  ASSERT_OK(GetMemTableRepFactoryFromString("skip_list", &new_mem_factory));
  ASSERT_OK(GetMemTableRepFactoryFromString("skip_list:16", &new_mem_factory));
  ASSERT_EQ(std::string(new_mem_factory->Name()), "SkipListFactory");
  ASSERT_NOK(GetMemTableRepFactoryFromString("skip_list:16:invalid_opt",
                                             &new_mem_factory));

  ASSERT_OK(GetMemTableRepFactoryFromString("prefix_hash", &new_mem_factory));
  ASSERT_OK(GetMemTableRepFactoryFromString("prefix_hash:1000",
                                            &new_mem_factory));
  ASSERT_EQ(std::string(new_mem_factory->Name()), "HashSkipListRepFactory");
  ASSERT_NOK(GetMemTableRepFactoryFromString("prefix_hash:1000:invalid_opt",
                                             &new_mem_factory));

  ASSERT_OK(GetMemTableRepFactoryFromString("hash_linkedlist",
                                            &new_mem_factory));
  ASSERT_OK(GetMemTableRepFactoryFromString("hash_linkedlist:1000",
                                            &new_mem_factory));
  ASSERT_EQ(std::string(new_mem_factory->Name()), "HashLinkListRepFactory");
  ASSERT_NOK(GetMemTableRepFactoryFromString("hash_linkedlist:1000:invalid_opt",
                                             &new_mem_factory));

  ASSERT_OK(GetMemTableRepFactoryFromString("vector", &new_mem_factory));
  ASSERT_OK(GetMemTableRepFactoryFromString("vector:1024", &new_mem_factory));
  ASSERT_EQ(std::string(new_mem_factory->Name()), "VectorRepFactory");
  ASSERT_NOK(GetMemTableRepFactoryFromString("vector:1024:invalid_opt",
                                             &new_mem_factory));

  ASSERT_NOK(GetMemTableRepFactoryFromString("bad_factory", &new_mem_factory));
}

TEST_F(OptionsTest, GetOptionsFromStringTest) {
  Options base_options, new_options;
  base_options.write_buffer_size = 20;
  base_options.min_write_buffer_number_to_merge = 15;
  BlockBasedTableOptions block_based_table_options;
  block_based_table_options.cache_index_and_filter_blocks = true;
  base_options.table_factory.reset(
      NewBlockBasedTableFactory(block_based_table_options));
  ASSERT_OK(GetOptionsFromString(
      base_options,
      "write_buffer_size=10;max_write_buffer_number=16;"
      "block_based_table_factory={block_cache=1M;block_size=4;};"
      "create_if_missing=true;max_open_files=1;rate_limiter_bytes_per_sec=1024",
      &new_options));

  ASSERT_EQ(new_options.write_buffer_size, 10U);
  ASSERT_EQ(new_options.max_write_buffer_number, 16);
  BlockBasedTableOptions new_block_based_table_options =
      dynamic_cast<BlockBasedTableFactory*>(new_options.table_factory.get())
          ->table_options();
  ASSERT_EQ(new_block_based_table_options.block_cache->GetCapacity(), 1U << 20);
  ASSERT_EQ(new_block_based_table_options.block_size, 4U);
  // don't overwrite block based table options
  ASSERT_TRUE(new_block_based_table_options.cache_index_and_filter_blocks);

  ASSERT_EQ(new_options.create_if_missing, true);
  ASSERT_EQ(new_options.max_open_files, 1);
  ASSERT_TRUE(new_options.rate_limiter.get() != nullptr);
}

TEST_F(OptionsTest, DBOptionsSerialization) {
  Options base_options, new_options;
  Random rnd(301);

  // Phase 1: Make big change in base_options
  test::RandomInitDBOptions(&base_options, &rnd);

  // Phase 2: obtain a string from base_option
  std::string base_options_file_content;
  ASSERT_OK(GetStringFromDBOptions(&base_options_file_content, base_options));

  // Phase 3: Set new_options from the derived string and expect
  //          new_options == base_options
  ASSERT_OK(GetDBOptionsFromString(DBOptions(), base_options_file_content,
                                   &new_options));
  ASSERT_OK(RocksDBOptionsParser::VerifyDBOptions(base_options, new_options));
}

TEST_F(OptionsTest, ColumnFamilyOptionsSerialization) {
  ColumnFamilyOptions base_opt, new_opt;
  Random rnd(302);
  // Phase 1: randomly assign base_opt
  // custom type options
  test::RandomInitCFOptions(&base_opt, &rnd);

  // Phase 2: obtain a string from base_opt
  std::string base_options_file_content;
  ASSERT_OK(
      GetStringFromColumnFamilyOptions(&base_options_file_content, base_opt));

  // Phase 3: Set new_opt from the derived string and expect
  //          new_opt == base_opt
  ASSERT_OK(GetColumnFamilyOptionsFromString(
      ColumnFamilyOptions(), base_options_file_content, &new_opt));
  ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(base_opt, new_opt));
  if (base_opt.compaction_filter) {
    delete base_opt.compaction_filter;
  }
}


Status StringToMap(
    const std::string& opts_str,
    std::unordered_map<std::string, std::string>* opts_map);

TEST_F(OptionsTest, StringToMapTest) {
  std::unordered_map<std::string, std::string> opts_map;
  // Regular options
  ASSERT_OK(StringToMap("k1=v1;k2=v2;k3=v3", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_EQ(opts_map["k2"], "v2");
  ASSERT_EQ(opts_map["k3"], "v3");
  // Value with '='
  opts_map.clear();
  ASSERT_OK(StringToMap("k1==v1;k2=v2=;", &opts_map));
  ASSERT_EQ(opts_map["k1"], "=v1");
  ASSERT_EQ(opts_map["k2"], "v2=");
  // Overwrriten option
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k1=v2;k3=v3", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v2");
  ASSERT_EQ(opts_map["k3"], "v3");
  // Empty value
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2=;k3=v3;k4=", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_TRUE(opts_map.find("k2") != opts_map.end());
  ASSERT_EQ(opts_map["k2"], "");
  ASSERT_EQ(opts_map["k3"], "v3");
  ASSERT_TRUE(opts_map.find("k4") != opts_map.end());
  ASSERT_EQ(opts_map["k4"], "");
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2=;k3=v3;k4=   ", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_TRUE(opts_map.find("k2") != opts_map.end());
  ASSERT_EQ(opts_map["k2"], "");
  ASSERT_EQ(opts_map["k3"], "v3");
  ASSERT_TRUE(opts_map.find("k4") != opts_map.end());
  ASSERT_EQ(opts_map["k4"], "");
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2=;k3=", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_TRUE(opts_map.find("k2") != opts_map.end());
  ASSERT_EQ(opts_map["k2"], "");
  ASSERT_TRUE(opts_map.find("k3") != opts_map.end());
  ASSERT_EQ(opts_map["k3"], "");
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2=;k3=;", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_TRUE(opts_map.find("k2") != opts_map.end());
  ASSERT_EQ(opts_map["k2"], "");
  ASSERT_TRUE(opts_map.find("k3") != opts_map.end());
  ASSERT_EQ(opts_map["k3"], "");
  // Regular nested options
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2={nk1=nv1;nk2=nv2};k3=v3", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_EQ(opts_map["k2"], "nk1=nv1;nk2=nv2");
  ASSERT_EQ(opts_map["k3"], "v3");
  // Multi-level nested options
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2={nk1=nv1;nk2={nnk1=nnk2}};"
                        "k3={nk1={nnk1={nnnk1=nnnv1;nnnk2;nnnv2}}};k4=v4",
                        &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_EQ(opts_map["k2"], "nk1=nv1;nk2={nnk1=nnk2}");
  ASSERT_EQ(opts_map["k3"], "nk1={nnk1={nnnk1=nnnv1;nnnk2;nnnv2}}");
  ASSERT_EQ(opts_map["k4"], "v4");
  // Garbage inside curly braces
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2={dfad=};k3={=};k4=v4",
                        &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_EQ(opts_map["k2"], "dfad=");
  ASSERT_EQ(opts_map["k3"], "=");
  ASSERT_EQ(opts_map["k4"], "v4");
  // Empty nested options
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2={};", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_EQ(opts_map["k2"], "");
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2={{{{}}}{}{}};", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_EQ(opts_map["k2"], "{{{}}}{}{}");
  // With random spaces
  opts_map.clear();
  ASSERT_OK(StringToMap("  k1 =  v1 ; k2= {nk1=nv1; nk2={nnk1=nnk2}}  ; "
                        "k3={  {   } }; k4= v4  ",
                        &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_EQ(opts_map["k2"], "nk1=nv1; nk2={nnk1=nnk2}");
  ASSERT_EQ(opts_map["k3"], "{   }");
  ASSERT_EQ(opts_map["k4"], "v4");

  // Empty key
  ASSERT_NOK(StringToMap("k1=v1;k2=v2;=", &opts_map));
  ASSERT_NOK(StringToMap("=v1;k2=v2", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2v2;", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2=v2;fadfa", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2=v2;;", &opts_map));
  // Mismatch curly braces
  ASSERT_NOK(StringToMap("k1=v1;k2={;k3=v3", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2={{};k3=v3", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2={}};k3=v3", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2={{}{}}};k3=v3", &opts_map));
  // However this is valid!
  opts_map.clear();
  ASSERT_OK(StringToMap("k1=v1;k2=};k3=v3", &opts_map));
  ASSERT_EQ(opts_map["k1"], "v1");
  ASSERT_EQ(opts_map["k2"], "}");
  ASSERT_EQ(opts_map["k3"], "v3");

  // Invalid chars after closing curly brace
  ASSERT_NOK(StringToMap("k1=v1;k2={{}}{};k3=v3", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2={{}}cfda;k3=v3", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2={{}}  cfda;k3=v3", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2={{}}  cfda", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2={{}}{}", &opts_map));
  ASSERT_NOK(StringToMap("k1=v1;k2={{dfdl}adfa}{}", &opts_map));
}

TEST_F(OptionsTest, StringToMapRandomTest) {
  std::unordered_map<std::string, std::string> opts_map;
  // Make sure segfault is not hit by semi-random strings

  std::vector<std::string> bases = {
      "a={aa={};tt={xxx={}}};c=defff",
      "a={aa={};tt={xxx={}}};c=defff;d={{}yxx{}3{xx}}",
      "abc={{}{}{}{{{}}}{{}{}{}{}{}{}{}"};

  for (std::string base : bases) {
    for (int rand_seed = 301; rand_seed < 401; rand_seed++) {
      Random rnd(rand_seed);
      for (int attempt = 0; attempt < 10; attempt++) {
        std::string str = base;
        // Replace random position to space
        size_t pos = static_cast<size_t>(
            rnd.Uniform(static_cast<int>(base.size())));
        str[pos] = ' ';
        Status s = StringToMap(str, &opts_map);
        ASSERT_TRUE(s.ok() || s.IsInvalidArgument());
        opts_map.clear();
      }
    }
  }

  // Random Construct a string
  std::vector<char> chars = {'{', '}', ' ', '=', ';', 'c'};
  for (int rand_seed = 301; rand_seed < 1301; rand_seed++) {
    Random rnd(rand_seed);
    int len = rnd.Uniform(30);
    std::string str = "";
    for (int attempt = 0; attempt < len; attempt++) {
      // Add a random character
      size_t pos = static_cast<size_t>(
          rnd.Uniform(static_cast<int>(chars.size())));
      str.append(1, chars[pos]);
    }
    Status s = StringToMap(str, &opts_map);
    ASSERT_TRUE(s.ok() || s.IsInvalidArgument());
    s = StringToMap("name=" + str, &opts_map);
    ASSERT_TRUE(s.ok() || s.IsInvalidArgument());
    opts_map.clear();
  }
}

TEST_F(OptionsTest, ConvertOptionsTest) {
  LevelDBOptions leveldb_opt;
  Options converted_opt = ConvertOptions(leveldb_opt);

  ASSERT_EQ(converted_opt.create_if_missing, leveldb_opt.create_if_missing);
  ASSERT_EQ(converted_opt.error_if_exists, leveldb_opt.error_if_exists);
  ASSERT_EQ(converted_opt.paranoid_checks, leveldb_opt.paranoid_checks);
  ASSERT_EQ(converted_opt.env, leveldb_opt.env);
  ASSERT_EQ(converted_opt.info_log.get(), leveldb_opt.info_log);
  ASSERT_EQ(converted_opt.write_buffer_size, leveldb_opt.write_buffer_size);
  ASSERT_EQ(converted_opt.max_open_files, leveldb_opt.max_open_files);
  ASSERT_EQ(converted_opt.compression, leveldb_opt.compression);

  std::shared_ptr<BlockBasedTableFactory> table_factory =
      std::dynamic_pointer_cast<BlockBasedTableFactory>(
          converted_opt.table_factory);

  ASSERT_TRUE(table_factory.get() != nullptr);

  const BlockBasedTableOptions table_opt = table_factory->table_options();

  ASSERT_EQ(table_opt.block_cache->GetCapacity(), 8UL << 20);
  ASSERT_EQ(table_opt.block_size, leveldb_opt.block_size);
  ASSERT_EQ(table_opt.block_restart_interval,
            leveldb_opt.block_restart_interval);
  ASSERT_EQ(table_opt.filter_policy.get(), leveldb_opt.filter_policy);
}

class OptionsParserTest : public RocksDBTest {
 public:
  OptionsParserTest() { env_.reset(new test::StringEnv(Env::Default())); }

 protected:
  std::unique_ptr<test::StringEnv> env_;
};

TEST_F(OptionsParserTest, Comment) {
  DBOptions db_opt;
  db_opt.max_open_files = 12345;
  db_opt.max_background_flushes = 301;
  db_opt.max_total_wal_size = 1024;
  ColumnFamilyOptions cf_opt;

  std::string options_file_content =
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[Version]\n"
      "  rocksdb_version=3.14.0\n"
      "  options_file_version=1\n"
      "[ DBOptions ]\n"
      "  # note that we don't support space around \"=\"\n"
      "  max_open_files=12345;\n"
      "  max_background_flushes=301  # comment after a statement is fine\n"
      "  # max_background_flushes=1000  # this line would be ignored\n"
      "  # max_background_compactions=2000 # so does this one\n"
      "  max_total_wal_size=1024  # keep_log_file_num=1000\n"
      "[CFOptions   \"default\"]  # column family must be specified\n"
      "                     # in the correct order\n"
      "  # if a section is blank, we will use the default\n";

  const std::string kTestFileName = "test-rocksdb-options.ini";
  ASSERT_OK(env_->WriteToNewFile(kTestFileName, options_file_content));
  RocksDBOptionsParser parser;
  ASSERT_OK(parser.Parse(kTestFileName, env_.get()));

  ASSERT_OK(RocksDBOptionsParser::VerifyDBOptions(*parser.db_opt(), db_opt));
  ASSERT_EQ(parser.NumColumnFamilies(), 1U);
  ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(
      *parser.GetCFOptions("default"), cf_opt));
}

TEST_F(OptionsParserTest, ExtraSpace) {
  std::string options_file_content =
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[      Version   ]\n"
      "  rocksdb_version     = 3.14.0      \n"
      "  options_file_version=1   # some comment\n"
      "[DBOptions  ]  # some comment\n"
      "max_open_files=12345   \n"
      "    max_background_flushes   =    301   \n"
      " max_total_wal_size     =   1024  # keep_log_file_num=1000\n"
      "        [CFOptions      \"default\"     ]\n"
      "  # if a section is blank, we will use the default\n";

  const std::string kTestFileName = "test-rocksdb-options.ini";
  ASSERT_OK(env_->WriteToNewFile(kTestFileName, options_file_content));
  RocksDBOptionsParser parser;
  ASSERT_OK(parser.Parse(kTestFileName, env_.get()));
}

TEST_F(OptionsParserTest, NewLineAbsenceAtTheEnd) {
  DBOptions db_opt;
  db_opt.max_open_files = 12345;
  db_opt.max_background_flushes = 301;
  db_opt.max_total_wal_size = 1024;
  ColumnFamilyOptions cf_opt;

  // The following conditions must be met to hit the issue:
  // 1) Last read from the file must be less than read buffer size.
  // 2) Last read from the file must contain the remaining fields of the section,
  //    the last section must not be a default section.
  // 3) There must be no new line char at the end of the file.
  // 4) No new line char must be in the last chunk.
  std::stringstream options_content;
  options_content <<
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[Version]\n"
      "  rocksdb_version=3.14.0\n"
      "  options_file_version=1\n"
      "[DBOptions]\n"
      "  max_open_files=12345\n"
      "  max_background_flushes=301\n"
      "  max_total_wal_size=1024\n"
      "  # ";

  std::string options_content_trailing_comment = "this is a comment ";
  size_t num_repeats = yb::ceil_div<size_t>(
      (RocksDBOptionsParser::kReadBufferSize - options_content.tellp()),
      options_content_trailing_comment.size());

  // Fill content till the size of the read chunk.
  while (num_repeats-- > 0) {
    options_content << options_content_trailing_comment;
  }
  // Add more data to make parser read with two chunks.
  options_content << options_content_trailing_comment;

  const std::string kTestFileName = "test-rocksdb-options.ini";
  ASSERT_OK(env_->WriteToNewFile(kTestFileName, options_content.str()));

  RocksDBOptionsParser parser;
  ASSERT_NOK(parser.Parse(kTestFileName, env_.get()));
}

TEST_F(OptionsParserTest, MissingDBOptions) {
  std::string options_file_content =
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[Version]\n"
      "  rocksdb_version=3.14.0\n"
      "  options_file_version=1\n"
      "[CFOptions \"default\"]\n"
      "  # if a section is blank, we will use the default\n";

  const std::string kTestFileName = "test-rocksdb-options.ini";
  ASSERT_OK(env_->WriteToNewFile(kTestFileName, options_file_content));
  RocksDBOptionsParser parser;
  ASSERT_NOK(parser.Parse(kTestFileName, env_.get()));
}

TEST_F(OptionsParserTest, DoubleDBOptions) {
  DBOptions db_opt;
  db_opt.max_open_files = 12345;
  db_opt.max_background_flushes = 301;
  db_opt.max_total_wal_size = 1024;
  ColumnFamilyOptions cf_opt;

  std::string options_file_content =
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[Version]\n"
      "  rocksdb_version=3.14.0\n"
      "  options_file_version=1\n"
      "[DBOptions]\n"
      "  max_open_files=12345\n"
      "  max_background_flushes=301\n"
      "  max_total_wal_size=1024  # keep_log_file_num=1000\n"
      "[DBOptions]\n"
      "[CFOptions \"default\"]\n"
      "  # if a section is blank, we will use the default\n";

  const std::string kTestFileName = "test-rocksdb-options.ini";
  ASSERT_OK(env_->WriteToNewFile(kTestFileName, options_file_content));
  RocksDBOptionsParser parser;
  ASSERT_NOK(parser.Parse(kTestFileName, env_.get()));
}

TEST_F(OptionsParserTest, NoDefaultCFOptions) {
  DBOptions db_opt;
  db_opt.max_open_files = 12345;
  db_opt.max_background_flushes = 301;
  db_opt.max_total_wal_size = 1024;
  ColumnFamilyOptions cf_opt;

  std::string options_file_content =
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[Version]\n"
      "  rocksdb_version=3.14.0\n"
      "  options_file_version=1\n"
      "[DBOptions]\n"
      "  max_open_files=12345\n"
      "  max_background_flushes=301\n"
      "  max_total_wal_size=1024  # keep_log_file_num=1000\n"
      "[CFOptions \"something_else\"]\n"
      "  # if a section is blank, we will use the default\n";

  const std::string kTestFileName = "test-rocksdb-options.ini";
  ASSERT_OK(env_->WriteToNewFile(kTestFileName, options_file_content));
  RocksDBOptionsParser parser;
  ASSERT_NOK(parser.Parse(kTestFileName, env_.get()));
}

TEST_F(OptionsParserTest, DefaultCFOptionsMustBeTheFirst) {
  DBOptions db_opt;
  db_opt.max_open_files = 12345;
  db_opt.max_background_flushes = 301;
  db_opt.max_total_wal_size = 1024;
  ColumnFamilyOptions cf_opt;

  std::string options_file_content =
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[Version]\n"
      "  rocksdb_version=3.14.0\n"
      "  options_file_version=1\n"
      "[DBOptions]\n"
      "  max_open_files=12345\n"
      "  max_background_flushes=301\n"
      "  max_total_wal_size=1024  # keep_log_file_num=1000\n"
      "[CFOptions \"something_else\"]\n"
      "  # if a section is blank, we will use the default\n"
      "[CFOptions \"default\"]\n"
      "  # if a section is blank, we will use the default\n";

  const std::string kTestFileName = "test-rocksdb-options.ini";
  ASSERT_OK(env_->WriteToNewFile(kTestFileName, options_file_content));
  RocksDBOptionsParser parser;
  ASSERT_NOK(parser.Parse(kTestFileName, env_.get()));
}

TEST_F(OptionsParserTest, DuplicateCFOptions) {
  DBOptions db_opt;
  db_opt.max_open_files = 12345;
  db_opt.max_background_flushes = 301;
  db_opt.max_total_wal_size = 1024;
  ColumnFamilyOptions cf_opt;

  std::string options_file_content =
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[Version]\n"
      "  rocksdb_version=3.14.0\n"
      "  options_file_version=1\n"
      "[DBOptions]\n"
      "  max_open_files=12345\n"
      "  max_background_flushes=301\n"
      "  max_total_wal_size=1024  # keep_log_file_num=1000\n"
      "[CFOptions \"default\"]\n"
      "[CFOptions \"something_else\"]\n"
      "[CFOptions \"something_else\"]\n";

  const std::string kTestFileName = "test-rocksdb-options.ini";
  ASSERT_OK(env_->WriteToNewFile(kTestFileName, options_file_content));
  RocksDBOptionsParser parser;
  ASSERT_NOK(parser.Parse(kTestFileName, env_.get()));
}

TEST_F(OptionsParserTest, ParseVersion) {
  DBOptions db_opt;
  db_opt.max_open_files = 12345;
  db_opt.max_background_flushes = 301;
  db_opt.max_total_wal_size = 1024;
  ColumnFamilyOptions cf_opt;

  std::string file_template =
      "# This is a testing option string.\n"
      "# Currently we only support \"#\" styled comment.\n"
      "\n"
      "[Version]\n"
      "  rocksdb_version=3.13.1\n"
      "  options_file_version=%s\n"
      "[DBOptions]\n"
      "[CFOptions \"default\"]\n";
  const int kLength = 1000;
  char buffer[kLength];
  RocksDBOptionsParser parser;

  const std::vector<std::string> invalid_versions = {
      "a.b.c", "3.2.2b", "3.-12", "3. 1",  // only digits and dots are allowed
      "1.2.3.4",
      "1.2.3"  // can only contains at most one dot.
      "0",     // options_file_version must be at least one
      "3..2",
      ".", ".1.2",             // must have at least one digit before each dot
      "1.2.", "1.", "2.34."};  // must have at least one digit after each dot
  for (auto iv : invalid_versions) {
    snprintf(buffer, kLength - 1, file_template.c_str(), iv.c_str());

    parser.Reset();
    ASSERT_OK(env_->WriteToNewFile(iv, buffer));
    ASSERT_NOK(parser.Parse(iv, env_.get()));
  }

  const std::vector<std::string> valid_versions = {
      "1.232", "100", "3.12", "1", "12.3  ", "  1.25  "};
  for (auto vv : valid_versions) {
    snprintf(buffer, kLength - 1, file_template.c_str(), vv.c_str());
    parser.Reset();
    ASSERT_OK(env_->WriteToNewFile(vv, buffer));
    ASSERT_OK(parser.Parse(vv, env_.get()));
  }
}

void VerifyCFPointerTypedOptions(
    ColumnFamilyOptions* base_cf_opt, const ColumnFamilyOptions* new_cf_opt,
    const std::unordered_map<std::string, std::string>* new_cf_opt_map) {
  std::string name_buffer;
  ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(*base_cf_opt, *new_cf_opt,
                                                  new_cf_opt_map));

  // change the name of merge operator back-and-forth
  {
    auto* merge_operator = dynamic_cast<test::ChanglingMergeOperator*>(
        base_cf_opt->merge_operator.get());
    if (merge_operator != nullptr) {
      name_buffer = merge_operator->Name();
      // change the name  and expect non-ok status
      merge_operator->SetName("some-other-name");
      ASSERT_NOK(RocksDBOptionsParser::VerifyCFOptions(
          *base_cf_opt, *new_cf_opt, new_cf_opt_map));
      // change the name back and expect ok status
      merge_operator->SetName(name_buffer);
      ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(*base_cf_opt, *new_cf_opt,
                                                      new_cf_opt_map));
    }
  }

  // change the name of the compaction filter factory back-and-forth
  {
    auto* compaction_filter_factory =
        dynamic_cast<test::ChanglingCompactionFilterFactory*>(
            base_cf_opt->compaction_filter_factory.get());
    if (compaction_filter_factory != nullptr) {
      name_buffer = compaction_filter_factory->Name();
      // change the name and expect non-ok status
      compaction_filter_factory->SetName("some-other-name");
      ASSERT_NOK(RocksDBOptionsParser::VerifyCFOptions(
          *base_cf_opt, *new_cf_opt, new_cf_opt_map));
      // change the name back and expect ok status
      compaction_filter_factory->SetName(name_buffer);
      ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(*base_cf_opt, *new_cf_opt,
                                                      new_cf_opt_map));
    }
  }

  // test by setting compaction_filter to nullptr
  {
    auto* tmp_compaction_filter = base_cf_opt->compaction_filter;
    if (tmp_compaction_filter != nullptr) {
      base_cf_opt->compaction_filter = nullptr;
      // set compaction_filter to nullptr and expect non-ok status
      ASSERT_NOK(RocksDBOptionsParser::VerifyCFOptions(
          *base_cf_opt, *new_cf_opt, new_cf_opt_map));
      // set the value back and expect ok status
      base_cf_opt->compaction_filter = tmp_compaction_filter;
      ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(*base_cf_opt, *new_cf_opt,
                                                      new_cf_opt_map));
    }
  }

  // test by setting table_factory to nullptr
  {
    auto tmp_table_factory = base_cf_opt->table_factory;
    if (tmp_table_factory != nullptr) {
      base_cf_opt->table_factory.reset();
      // set table_factory to nullptr and expect non-ok status
      ASSERT_NOK(RocksDBOptionsParser::VerifyCFOptions(
          *base_cf_opt, *new_cf_opt, new_cf_opt_map));
      // set the value back and expect ok status
      base_cf_opt->table_factory = tmp_table_factory;
      ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(*base_cf_opt, *new_cf_opt,
                                                      new_cf_opt_map));
    }
  }

  // test by setting memtable_factory to nullptr
  {
    auto tmp_memtable_factory = base_cf_opt->memtable_factory;
    if (tmp_memtable_factory != nullptr) {
      base_cf_opt->memtable_factory.reset();
      // set memtable_factory to nullptr and expect non-ok status
      ASSERT_NOK(RocksDBOptionsParser::VerifyCFOptions(
          *base_cf_opt, *new_cf_opt, new_cf_opt_map));
      // set the value back and expect ok status
      base_cf_opt->memtable_factory = tmp_memtable_factory;
      ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(*base_cf_opt, *new_cf_opt,
                                                      new_cf_opt_map));
    }
  }
}

TEST_F(OptionsParserTest, DumpAndParse) {
  DBOptions base_db_opt;
  std::vector<ColumnFamilyOptions> base_cf_opts;
  std::vector<std::string> cf_names = {"default", "cf1", "cf2", "cf3",
                                       "c:f:4:4:4"
                                       "p\\i\\k\\a\\chu\\\\\\",
                                       "###rocksdb#1-testcf#2###"};
  const int num_cf = static_cast<int>(cf_names.size());
  Random rnd(302);
  test::RandomInitDBOptions(&base_db_opt, &rnd);
  base_db_opt.db_log_dir += "/#odd #but #could #happen #path #/\\\\#OMG";
  for (int c = 0; c < num_cf; ++c) {
    ColumnFamilyOptions cf_opt;
    Random cf_rnd(0xFB + c);
    test::RandomInitCFOptions(&cf_opt, &cf_rnd);
    if (c < 4) {
      cf_opt.prefix_extractor.reset(test::RandomSliceTransform(&rnd, c));
    }
    if (c < 3) {
      cf_opt.table_factory.reset(test::RandomTableFactory(&rnd, c));
    }
    base_cf_opts.emplace_back(cf_opt);
  }

  const std::string kOptionsFileName = "test-persisted-options.ini";
  ASSERT_OK(PersistRocksDBOptions(base_db_opt, cf_names, base_cf_opts,
                                  kOptionsFileName, env_.get()));

  RocksDBOptionsParser parser;
  ASSERT_OK(parser.Parse(kOptionsFileName, env_.get()));

  ASSERT_OK(RocksDBOptionsParser::VerifyRocksDBOptionsFromFile(
      base_db_opt, cf_names, base_cf_opts, kOptionsFileName, env_.get()));

  ASSERT_OK(
      RocksDBOptionsParser::VerifyDBOptions(*parser.db_opt(), base_db_opt));
  for (int c = 0; c < num_cf; ++c) {
    const auto* cf_opt = parser.GetCFOptions(cf_names[c]);
    ASSERT_NE(cf_opt, nullptr);
    ASSERT_OK(RocksDBOptionsParser::VerifyCFOptions(
        base_cf_opts[c], *cf_opt, &(parser.cf_opt_maps()->at(c))));
  }

  // Further verify pointer-typed options
  for (int c = 0; c < num_cf; ++c) {
    const auto* cf_opt = parser.GetCFOptions(cf_names[c]);
    ASSERT_NE(cf_opt, nullptr);
    VerifyCFPointerTypedOptions(&base_cf_opts[c], cf_opt,
                                &(parser.cf_opt_maps()->at(c)));
  }

  ASSERT_EQ(parser.GetCFOptions("does not exist"), nullptr);

  base_db_opt.max_open_files++;
  ASSERT_NOK(RocksDBOptionsParser::VerifyRocksDBOptionsFromFile(
      base_db_opt, cf_names, base_cf_opts, kOptionsFileName, env_.get()));

  for (int c = 0; c < num_cf; ++c) {
    if (base_cf_opts[c].compaction_filter) {
      delete base_cf_opts[c].compaction_filter;
    }
  }
}

TEST_F(OptionsParserTest, DifferentDefault) {
  const std::string kOptionsFileName = "test-persisted-options.ini";

  ColumnFamilyOptions cf_level_opts;
  cf_level_opts.OptimizeLevelStyleCompaction();

  ColumnFamilyOptions cf_univ_opts;
  cf_univ_opts.OptimizeUniversalStyleCompaction();

  ASSERT_OK(PersistRocksDBOptions(DBOptions(), {"default", "universal"},
                                  {cf_level_opts, cf_univ_opts},
                                  kOptionsFileName, env_.get()));

  RocksDBOptionsParser parser;
  ASSERT_OK(parser.Parse(kOptionsFileName, env_.get()));
}

class OptionsSanityCheckTest : public OptionsParserTest {
 public:
  OptionsSanityCheckTest() {}

 protected:
  Status SanityCheckCFOptions(const ColumnFamilyOptions& cf_opts,
                              OptionsSanityCheckLevel level) {
    return RocksDBOptionsParser::VerifyRocksDBOptionsFromFile(
        DBOptions(), {"default"}, {cf_opts}, kOptionsFileName, env_.get(),
        level);
  }

  Status PersistCFOptions(const ColumnFamilyOptions& cf_opts) {
    Status s = env_->DeleteFile(kOptionsFileName);
    if (!s.ok()) {
      return s;
    }
    return PersistRocksDBOptions(DBOptions(), {"default"}, {cf_opts},
                                 kOptionsFileName, env_.get());
  }

  const std::string kOptionsFileName = "OPTIONS";
};

TEST_F(OptionsSanityCheckTest, SanityCheck) {
  ColumnFamilyOptions opts;
  Random rnd(301);

  // default ColumnFamilyOptions
  {
    ASSERT_OK(PersistCFOptions(opts));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));
  }

  // prefix_extractor
  {
    // Okay to change prefix_extractor form nullptr to non-nullptr
    ASSERT_EQ(opts.prefix_extractor.get(), nullptr);
    opts.prefix_extractor.reset(NewCappedPrefixTransform(10));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelNone));

    // persist the change
    ASSERT_OK(PersistCFOptions(opts));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));

    // use same prefix extractor but with different parameter
    opts.prefix_extractor.reset(NewCappedPrefixTransform(15));
    // expect pass only in kSanityLevelNone
    ASSERT_NOK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelNone));

    // repeat the test with FixedPrefixTransform
    opts.prefix_extractor.reset(NewFixedPrefixTransform(10));
    ASSERT_NOK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelNone));

    // persist the change of prefix_extractor
    ASSERT_OK(PersistCFOptions(opts));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));

    // use same prefix extractor but with different parameter
    opts.prefix_extractor.reset(NewFixedPrefixTransform(15));
    // expect pass only in kSanityLevelNone
    ASSERT_NOK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelNone));

    // Change prefix extractor from non-nullptr to nullptr
    opts.prefix_extractor.reset();
    // expect pass as it's safe to change prefix_extractor
    // from non-null to null
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));
    ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelNone));
  }
  // persist the change
  ASSERT_OK(PersistCFOptions(opts));
  ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));

  // table_factory
  {
    for (int tb = 0; tb <= 1; ++tb) {
      // change the table factory
      opts.table_factory.reset(test::RandomTableFactory(&rnd, tb));
      ASSERT_NOK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));
      ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelNone));

      // persist the change
      ASSERT_OK(PersistCFOptions(opts));
      ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));
    }
  }

  // merge_operator
  {
    for (int test = 0; test < 5; ++test) {
      // change the merge operator
      opts.merge_operator.reset(test::RandomMergeOperator(&rnd));
      ASSERT_NOK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));
      ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelNone));

      // persist the change
      ASSERT_OK(PersistCFOptions(opts));
      ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));
    }
  }

  // compaction_filter
  {
    for (int test = 0; test < 5; ++test) {
      // change the compaction filter
      opts.compaction_filter = test::RandomCompactionFilter(&rnd);
      ASSERT_NOK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));
      ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));

      // persist the change
      ASSERT_OK(PersistCFOptions(opts));
      ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));
      delete opts.compaction_filter;
      opts.compaction_filter = nullptr;
    }
  }

  // compaction_filter_factory
  {
    for (int test = 0; test < 5; ++test) {
      // change the compaction filter factory
      opts.compaction_filter_factory.reset(
          test::RandomCompactionFilterFactory(&rnd));
      ASSERT_NOK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));
      ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelLooselyCompatible));

      // persist the change
      ASSERT_OK(PersistCFOptions(opts));
      ASSERT_OK(SanityCheckCFOptions(opts, kSanityLevelExactMatch));
    }
  }
}

namespace {
bool IsEscapedString(const std::string& str) {
  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '\\') {
      // since we already handle those two consecutive '\'s in
      // the next if-then branch, any '\' appear at the end
      // of an escaped string in such case is not valid.
      if (i == str.size() - 1) {
        return false;
      }
      if (str[i + 1] == '\\') {
        // if there're two consecutive '\'s, skip the second one.
        i++;
        continue;
      }
      switch (str[i + 1]) {
        case ':':
        case '\\':
        case '#':
          continue;
        default:
          // if true, '\' together with str[i + 1] is not a valid escape.
          if (UnescapeChar(str[i + 1]) == str[i + 1]) {
            return false;
          }
      }
    } else if (isSpecialChar(str[i]) && (i == 0 || str[i - 1] != '\\')) {
      return false;
    }
  }
  return true;
}
}  // namespace

TEST_F(OptionsParserTest, EscapeOptionString) {
  ASSERT_EQ(UnescapeOptionString(
                "This is a test string with \\# \\: and \\\\ escape chars."),
            "This is a test string with # : and \\ escape chars.");

  ASSERT_EQ(
      EscapeOptionString("This is a test string with # : and \\ escape chars."),
      "This is a test string with \\# \\: and \\\\ escape chars.");

  std::string readible_chars =
      "A String like this \"1234567890-=_)(*&^%$#@!ertyuiop[]{POIU"
      "YTREWQasdfghjkl;':LKJHGFDSAzxcvbnm,.?>"
      "<MNBVCXZ\\\" should be okay to \\#\\\\\\:\\#\\#\\#\\ "
      "be serialized and deserialized";

  std::string escaped_string = EscapeOptionString(readible_chars);
  ASSERT_TRUE(IsEscapedString(escaped_string));
  // This two transformations should be canceled and should output
  // the original input.
  ASSERT_EQ(UnescapeOptionString(escaped_string), readible_chars);

  std::string all_chars;
  for (unsigned char c = 0;; ++c) {
    all_chars += c;
    if (c == 255) {
      break;
    }
  }
  escaped_string = EscapeOptionString(all_chars);
  ASSERT_TRUE(IsEscapedString(escaped_string));
  ASSERT_EQ(UnescapeOptionString(escaped_string), all_chars);

  ASSERT_EQ(RocksDBOptionsParser::TrimAndRemoveComment(
                "     A simple statement with a comment.  # like this :)"),
            "A simple statement with a comment.");

  ASSERT_EQ(RocksDBOptionsParser::TrimAndRemoveComment(
                "Escape \\# and # comment together   ."),
            "Escape \\# and");
}

// Only run the tests to verify new fields in options are settable through
// string on limited platforms as it depends on behavior of compilers.
#if defined(__linux__) && !defined(__clang__)

struct OffsetGap {
  size_t begin_offset;
  size_t end_offset;
  const char* name;

  std::string ToString() const {
    return yb::Format("{$0, $1, $2}", begin_offset, end_offset, name);
  }
};

typedef std::vector<OffsetGap> OffsetGaps;

std::vector<Slice> SettableSlices(const char* start_ptr,
                                  size_t total_size,
                                  const OffsetGaps& blacklist) {
  size_t offset = 0;
  std::vector<Slice> result;
  result.reserve(blacklist.size() + 1);
  for (auto& tuple : blacklist) {
    result.emplace_back(start_ptr + offset, tuple.begin_offset - offset);
    offset = tuple.end_offset;
  }
  result.emplace_back(start_ptr + offset, total_size - offset);
  return result;
}

constexpr char kUnsetMark1 = 'R';
constexpr char kUnsetMark2 = ~kUnsetMark1;

void FillWithSpecialChar(char* start_ptr,
                         size_t total_size,
                         const OffsetGaps& blacklist,
                         char unset_mark) {
  for (auto& slice : SettableSlices(start_ptr, total_size, blacklist)) {
    std::memset(slice.mutable_data(), unset_mark, slice.size());
  }
}

// Finds offsets of bytes that are different for
// [start_ptr1, start_ptr1 + total_size) and [start_ptr2, start_ptr2 + total_size)
// Do not check offsets specified by blacklist
std::vector<size_t> DifferentBytes(const char* start_ptr1,
                                   const char* start_ptr2,
                                   size_t total_size,
                                   const OffsetGaps& blacklist) {
  std::vector<size_t> result;
  for (auto& slice : SettableSlices(start_ptr1, total_size, blacklist)) {
    for (const char* ptr = slice.cdata(); ptr != slice.cend(); ++ptr) {
      if (*ptr != start_ptr2[ptr - start_ptr1]) {
        result.push_back(ptr - start_ptr1);
      }
    }
  }
  return result;
}

// Stores object of T in POD-buffer. After object is created all its bytes, except blacklist are
// filled with unset_mark.
template<class T>
class UnsetHolder {
 public:
  template<class... Args>
  UnsetHolder(char unset_mark, const OffsetGaps& blacklist, Args&&... args)
      : blacklist_(blacklist) {
    new (&storage_) T(std::forward<Args>(args)...);
    FillWithSpecialChar(raw_data(), sizeof(T), blacklist, unset_mark);
  }

  UnsetHolder(const UnsetHolder&) = delete;
  void operator=(const UnsetHolder&) = delete;

  ~UnsetHolder() {
    get()->~T();
  }

  std::vector<size_t> DifferentBytes(const UnsetHolder& rhs) const {
    return rocksdb::DifferentBytes(raw_data(), rhs.raw_data(), sizeof(T), blacklist_);
  }

  T* operator->() {
    return get();
  }

  T* get() {
    return reinterpret_cast<T*>(&storage_);;
  }

  char* raw_data() {
    return reinterpret_cast<char*>(&storage_);;
  }

  const char* raw_data() const {
    return reinterpret_cast<const char*>(&storage_);;
  }

  T& operator*() {
    return *get();
  }

 private:
  const OffsetGaps& blacklist_;
  typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
};

void InitDefault(BlockBasedTableOptions*) {}

Status GetFromString(BlockBasedTableOptions* source, BlockBasedTableOptions* destination) {
  const char* const kOptionsString =
      "cache_index_and_filter_blocks=1;index_type=kHashSearch;"
      "checksum=kxxHash;hash_index_allow_collision=1;no_block_cache=1;"
      "block_cache=1M;block_cache_compressed=1k;block_size=1024;filter_block_size=16384;"
      "block_size_deviation=8;block_restart_interval=4; "
      "index_block_restart_interval=4;index_block_size=16384;min_keys_per_index_block=16;"
      "filter_policy=bloomfilter:4:true;whole_key_filtering=1;"
      "skip_table_builder_flush=1;format_version=1;"
      "hash_index_allow_collision=false;";

  RETURN_NOT_OK(GetBlockBasedTableOptionsFromString(*source, kOptionsString, destination));

  // This option is not setable:
  destination->use_delta_encoding = false;

  EXPECT_NE(nullptr, destination->block_cache.get());
  EXPECT_NE(nullptr, destination->block_cache_compressed.get());
  EXPECT_NE(nullptr, destination->filter_policy.get());

  return Status::OK();
}

void InitDefault(DBOptions*) {}

Status GetFromString(DBOptions* source, DBOptions* destination) {
  const char* const kOptionsString =
      "wal_bytes_per_sync=4295048118;"
      "delete_obsolete_files_period_micros=4294967758;"
      "WAL_ttl_seconds=4295008036;"
      "WAL_size_limit_MB=4295036161;"
      "wal_dir=path/to/wal_dir;"
      "db_write_buffer_size=2587;"
      "max_subcompactions=64330;"
      "table_cache_numshardbits=28;"
      "max_open_files=72;"
      "max_file_opening_threads=35;"
      "base_background_compactions=3;"
      "max_background_compactions=33;"
      "use_fsync=true;"
      "use_adaptive_mutex=true;"
      "max_total_wal_size=4295005604;"
      "compaction_readahead_size=0;"
      "new_table_reader_for_compaction_inputs=true;"
      "keep_log_file_num=4890;"
      "skip_stats_update_on_db_open=true;"
      "max_manifest_file_size=4295009941;"
      "db_log_dir=path/to/db_log_dir;"
      "skip_log_error_on_recovery=true;"
      "writable_file_max_buffer_size=1048576;"
      "paranoid_checks=false;"
      "is_fd_close_on_exec=true;"
      "bytes_per_sync=4295013613;"
      "enable_thread_tracking=true;"
      "disable_data_sync=true;"
      "recycle_log_file_num=0;"
      "disableDataSync=true;"
      "create_missing_column_families=true;"
      "log_file_time_to_roll=3097;"
      "max_background_flushes=35;"
      "create_if_missing=true;"
      "error_if_exists=true;"
      "allow_os_buffer=true;"
      "delayed_write_rate=4294976214;"
      "manifest_preallocation_size=1222;"
      "allow_mmap_writes=true;"
      "stats_dump_period_sec=70127;"
      "allow_fallocate=true;"
      "allow_mmap_reads=true;"
      "max_log_file_size=4607;"
      "random_access_max_buffer_size=1048576;"
      "advise_random_on_open=true;"
      "fail_if_options_file_error=true;"
      "allow_concurrent_memtable_write=true;"
      "wal_recovery_mode=kPointInTimeRecovery;"
      "enable_write_thread_adaptive_yield=true;"
      "write_thread_slow_yield_usec=5;"
      "write_thread_max_yield_usec=1000;"
      "access_hint_on_compaction_start=NONE;"
      "max_file_size_for_compaction=123;"
      "initial_seqno=432;"
      "num_reserved_small_compaction_threads=-1;"
      "compaction_size_threshold_bytes=18446744073709551615;"
      "info_log_level=DEBUG_LEVEL;";

  return GetDBOptionsFromString(*source, kOptionsString, destination);
}

// We want padding bytes to have the same values for test purposes, therefore we need to use
// exactly the same saved default value instead of using CompactionOptionsUniversal().
static CompactionOptionsUniversal kCompactionOptionsUniversalDefault;

void InitDefault(ColumnFamilyOptions* options) {
  options->compaction_options_universal = kCompactionOptionsUniversalDefault;
  // Deprecatd option which is not initialized. Need to set it to avoid
  // Valgrind error
  options->max_mem_compaction_level = 0;
}

Status GetFromString(ColumnFamilyOptions* source, ColumnFamilyOptions* destination) {
  // Need to update the option string if a new option is added.
  const char* const kOptionsString =
      "compaction_filter_factory=mpudlojcujCompactionFilterFactory;"
      "table_factory=PlainTable;"
      "prefix_extractor=rocksdb.CappedPrefix.13;"
      "comparator=leveldb.BytewiseComparator;"
      "compression_per_level=kBZip2Compression:kBZip2Compression:"
      "kBZip2Compression:kNoCompression:kZlibCompression:kBZip2Compression:"
      "kSnappyCompression;"
      "max_bytes_for_level_base=986;"
      "bloom_locality=8016;"
      "target_file_size_base=4294976376;"
      "memtable_prefix_bloom_huge_page_tlb_size=2557;"
      "max_successive_merges=5497;"
      "max_sequential_skip_in_iterations=4294971408;"
      "arena_block_size=1893;"
      "target_file_size_multiplier=35;"
      "source_compaction_factor=54;"
      "min_write_buffer_number_to_merge=9;"
      "max_write_buffer_number=84;"
      "write_buffer_size=1653;"
      "max_grandparent_overlap_factor=64;"
      "max_bytes_for_level_multiplier=60;"
      "memtable_factory=SkipListFactory;"
      "compression=kNoCompression;"
      "min_partial_merge_operands=7576;"
      "level0_stop_writes_trigger=33;"
      "num_levels=99;"
      "level0_slowdown_writes_trigger=22;"
      "level0_file_num_compaction_trigger=14;"
      "expanded_compaction_factor=34;"
      "compaction_filter=urxcqstuwnCompactionFilter;"
      "soft_rate_limit=530.615385;"
      "soft_pending_compaction_bytes_limit=0;"
      "max_write_buffer_number_to_maintain=84;"
      "verify_checksums_in_compaction=false;"
      "merge_operator=aabcxehazrMergeOperator;"
      "memtable_prefix_bloom_bits=4642;"
      "paranoid_file_checks=true;"
      "inplace_update_num_locks=7429;"
      "optimize_filters_for_hits=false;"
      "level_compaction_dynamic_level_bytes=false;"
      "inplace_update_support=false;"
      "compaction_style=kCompactionStyleFIFO;"
      "memtable_prefix_bloom_probes=2511;"
      "purge_redundant_kvs_while_flush=true;"
      "filter_deletes=false;"
      "hard_pending_compaction_bytes_limit=0;"
      "disable_auto_compactions=false;"
      "compaction_measure_io_stats=true;";

  RETURN_NOT_OK(GetColumnFamilyOptionsFromString(*source, kOptionsString, destination));

  // Following options are not settable through
  // GetColumnFamilyOptionsFromString():
  destination->rate_limit_delay_max_milliseconds = 33;
  destination->compaction_pri = CompactionPri::kOldestSmallestSeqFirst;
  destination->compaction_options_universal = kCompactionOptionsUniversalDefault;
  destination->compression_opts = CompressionOptions();
  destination->hard_rate_limit = 0;
  destination->soft_rate_limit = 0;
  destination->compaction_options_fifo = CompactionOptionsFIFO();
  destination->max_mem_compaction_level = 0;
  destination->max_flushing_bytes = 0;

  return Status::OK();
}

// Takes 2 sorted collections and removes all entries that is present in both of them.
// After execution of this function c1 contains entries there were not contained in c2.
// And vise versa.
template<class Collection>
void FindDelta(Collection* c1, Collection* c2) {
  auto w1 = c1->begin();
  auto w2 = c2->begin();
  auto i1 = c1->begin();
  auto i2 = c2->begin();
  while (i1 != c1->end() && i2 != c2->end()) {
    if (*i1 < *i2) {
      *w1 = *i1;
      ++w1;
      ++i1;
    } else if (*i2 < *i1) {
      *w2 = *i2;
      ++w2;
      ++i2;
    } else {
      ++i1;
      ++i2;
    }
  }
  c1->erase(w1, i1);
  c2->erase(w2, i2);
}

template<class T>
void TestAllFieldsSettable(const OffsetGaps& blacklist) {
  std::vector<size_t> unset_bytes_base;
  {
    UnsetHolder<T> value1(kUnsetMark1, blacklist);
    UnsetHolder<T> value2(kUnsetMark2, blacklist);

    *value1 = T();
    *value2 = T();

    InitDefault(value1.get());
    InitDefault(value2.get());

    // Count padding bytes by setting all bytes in the memory to a special char,
    // copy a well constructed struct to this memory and see how many special
    // bytes left.

    unset_bytes_base = value1.DifferentBytes(value2);
  }

  {
    UnsetHolder<T> value1(kUnsetMark1, blacklist);
    UnsetHolder<T> value2(kUnsetMark2, blacklist);

    UnsetHolder<T> new_value1(kUnsetMark1, blacklist);
    UnsetHolder<T> new_value2(kUnsetMark2, blacklist);

    // Need to update the option string if a new option is added.
    ASSERT_OK(GetFromString(value1.get(), new_value1.get()));
    ASSERT_OK(GetFromString(value2.get(), new_value2.get()));

    auto actual_unset_bytes = new_value1.DifferentBytes(new_value2);
    auto unset_bytes_copy = unset_bytes_base;
    FindDelta(&unset_bytes_copy, &actual_unset_bytes);
    ASSERT_EQ(unset_bytes_copy, actual_unset_bytes) << yb::Format("Blacklist: $0", blacklist);

    // The byte could be one of 3 types:
    // 1) Padding byte, then it is same in new_value1 and value1.
    // 2) Settable by from string, then it should differ in new_value1 and value1.
    // 3) Blacklisted
    auto parsed_bytes = new_value1.DifferentBytes(value1);
    auto all_non_blacklisted = parsed_bytes;
    all_non_blacklisted.insert(all_non_blacklisted.end(),
                               unset_bytes_base.begin(),
                               unset_bytes_base.end());
    std::sort(all_non_blacklisted.begin(), all_non_blacklisted.end());
    all_non_blacklisted.erase(std::unique(all_non_blacklisted.begin(), all_non_blacklisted.end()),
                              all_non_blacklisted.end());

    // Check that (1) and (2) does not overlap.
    ASSERT_EQ(unset_bytes_base.size() + parsed_bytes.size(), all_non_blacklisted.size())
         << yb::Format("Blacklist: $0", blacklist);

    // Check that (1) + (2) is all that is not (3).
    std::vector<Slice> settable_slices = SettableSlices(value1.raw_data(), sizeof(T), blacklist);
    std::vector<size_t> all_settable;
    for (auto& slice : settable_slices) {
      for (const char* p = slice.cdata(); p != slice.cend(); ++p)
        all_settable.push_back(p - value1.raw_data());
    }
    FindDelta(&all_settable, &all_non_blacklisted);
    ASSERT_EQ(all_settable, all_non_blacklisted) << yb::Format("Blacklist: $0", blacklist);
  }
}

#define BLACKLIST_ENTRY(type, field) \
    {offsetof(type, field), \
     offsetof(type, field) + sizeof(static_cast<type*>(nullptr)->field), \
     BOOST_PP_STRINGIZE(field)}

// If the test fails, likely a new option is added to BlockBasedTableOptions
// but it cannot be set through GetBlockBasedTableOptionsFromString(), or the
// test is not updated accordingly.
// After adding an option, we need to make sure it is settable by
// GetBlockBasedTableOptionsFromString() and add the option to the input string
// passed to the GetBlockBasedTableOptionsFromString() in this test.
// If it is a complicated type, you also need to add the field to
// kBbtoBlacklist, and maybe add customized verification for it.
TEST_F(OptionsParserTest, BlockBasedTableOptionsAllFieldsSettable) {
  // Items in the form of <offset, size>. Need to be in ascending order
  // and not overlapping. Need to updated if new pointer-option is added.
  const OffsetGaps kBbtoBlacklist = {
      BLACKLIST_ENTRY(BlockBasedTableOptions, flush_block_policy_factory),
      BLACKLIST_ENTRY(BlockBasedTableOptions, block_cache),
      BLACKLIST_ENTRY(BlockBasedTableOptions, block_cache_compressed),
      BLACKLIST_ENTRY(BlockBasedTableOptions, data_block_key_value_encoding_format),
      BLACKLIST_ENTRY(BlockBasedTableOptions, filter_policy),
      BLACKLIST_ENTRY(BlockBasedTableOptions, supported_filter_policies),
  };

  // In this test, we catch a new option of BlockBasedTableOptions that is not
  // settable through GetBlockBasedTableOptionsFromString().
  // We count padding bytes of the option struct, and assert it to be the same
  // as unset bytes of an option struct initialized by
  // GetBlockBasedTableOptionsFromString().
  TestAllFieldsSettable<BlockBasedTableOptions>(kBbtoBlacklist);
}

// If the test fails, likely a new option is added to DBOptions
// but it cannot be set through GetDBOptionsFromString(), or the test is not
// updated accordingly.
// After adding an option, we need to make sure it is settable by
// GetDBOptionsFromString() and add the option to the input string passed to
// DBOptionsFromString()in this test.
// If it is a complicated type, you also need to add the field to
// kDBOptionsBlacklist, and maybe add customized verification for it.
TEST_F(OptionsParserTest, DBOptionsAllFieldsSettable) {
  const OffsetGaps kDBOptionsBlacklist = {
      BLACKLIST_ENTRY(DBOptions, env),
      BLACKLIST_ENTRY(DBOptions, checkpoint_env),
      BLACKLIST_ENTRY(DBOptions, priority_thread_pool_for_compactions_and_flushes),
      BLACKLIST_ENTRY(DBOptions, rate_limiter),
      BLACKLIST_ENTRY(DBOptions, sst_file_manager),
      BLACKLIST_ENTRY(DBOptions, info_log),
      BLACKLIST_ENTRY(DBOptions, statistics),
      BLACKLIST_ENTRY(DBOptions, db_paths),
      BLACKLIST_ENTRY(DBOptions, db_log_dir),
      BLACKLIST_ENTRY(DBOptions, wal_dir),
      BLACKLIST_ENTRY(DBOptions, memory_monitor),
      BLACKLIST_ENTRY(DBOptions, listeners),
      BLACKLIST_ENTRY(DBOptions, row_cache),
      BLACKLIST_ENTRY(DBOptions, wal_filter),
      BLACKLIST_ENTRY(DBOptions, boundary_extractor),
      BLACKLIST_ENTRY(DBOptions, compaction_context_factory),
      BLACKLIST_ENTRY(DBOptions, max_file_size_for_compaction),
      BLACKLIST_ENTRY(DBOptions, mem_table_flush_filter_factory),
      BLACKLIST_ENTRY(DBOptions, log_prefix),
      BLACKLIST_ENTRY(DBOptions, tablet_id),
      BLACKLIST_ENTRY(DBOptions, mem_tracker),
      BLACKLIST_ENTRY(DBOptions, block_based_table_mem_tracker),
      BLACKLIST_ENTRY(DBOptions, iterator_replacer),
      BLACKLIST_ENTRY(DBOptions, compaction_file_filter_factory),
      BLACKLIST_ENTRY(DBOptions, priority_thread_pool_metrics),
      BLACKLIST_ENTRY(DBOptions, disk_group_no),
  };

  TestAllFieldsSettable<DBOptions>(kDBOptionsBlacklist);
}

// If the test fails, likely a new option is added to ColumnFamilyOptions
// but it cannot be set through GetColumnFamilyOptionsFromString(), or the
// test is not updated accordingly.
// After adding an option, we need to make sure it is settable by
// GetColumnFamilyOptionsFromString() and add the option to the input
// string passed to GetColumnFamilyOptionsFromString()in this test.
// If it is a complicated type, you also need to add the field to
// kColumnFamilyOptionsBlacklist, and maybe add customized verification
// for it.
TEST_F(OptionsParserTest, ColumnFamilyOptionsAllFieldsSettable) {
  const OffsetGaps kColumnFamilyOptionsBlacklist = {
      BLACKLIST_ENTRY(ColumnFamilyOptions, comparator),
      BLACKLIST_ENTRY(ColumnFamilyOptions, merge_operator),
      BLACKLIST_ENTRY(ColumnFamilyOptions, compaction_filter),
      BLACKLIST_ENTRY(ColumnFamilyOptions, compaction_filter_factory),
      BLACKLIST_ENTRY(ColumnFamilyOptions, compression_per_level),
      BLACKLIST_ENTRY(ColumnFamilyOptions, prefix_extractor),
      BLACKLIST_ENTRY(ColumnFamilyOptions, max_bytes_for_level_multiplier_additional),
      BLACKLIST_ENTRY(ColumnFamilyOptions, memtable_factory),
      BLACKLIST_ENTRY(ColumnFamilyOptions, table_factory),
      BLACKLIST_ENTRY(ColumnFamilyOptions, table_properties_collector_factories),
      BLACKLIST_ENTRY(ColumnFamilyOptions, inplace_callback),
  };

  TestAllFieldsSettable<ColumnFamilyOptions>(kColumnFamilyOptionsBlacklist);
}
#endif // __linux__ && !clang

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
#ifdef GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#endif  // GFLAGS
  return RUN_ALL_TESTS();
}
