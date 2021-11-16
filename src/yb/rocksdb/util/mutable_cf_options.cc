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

#include "yb/rocksdb/util/mutable_cf_options.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <limits>
#include <string>
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/immutable_options.h"

namespace rocksdb {

// Multiple two operands. If they overflow, return op1.
uint64_t MultiplyCheckOverflow(uint64_t op1, int op2) {
  if (op1 == 0) {
    return 0;
  }
  if (op2 <= 0) {
    return op1;
  }
  uint64_t casted_op2 = (uint64_t) op2;
  if (std::numeric_limits<uint64_t>::max() / op1 < casted_op2) {
    return op1;
  }
  return op1 * casted_op2;
}

void MutableCFOptions::RefreshDerivedOptions(
    const ImmutableCFOptions& ioptions) {
  max_file_size.resize(ioptions.num_levels);
  for (int i = 0; i < ioptions.num_levels; ++i) {
    if (i == 0 && ioptions.compaction_style == kCompactionStyleUniversal) {
      max_file_size[i] = ULLONG_MAX;
    } else if (i > 1) {
      max_file_size[i] = MultiplyCheckOverflow(max_file_size[i - 1],
                                               target_file_size_multiplier);
    } else {
      max_file_size[i] = target_file_size_base;
    }
  }
}

uint64_t MutableCFOptions::MaxFileSizeForLevel(int level) const {
  assert(level >= 0);
  assert(level < static_cast<int>(max_file_size.size()));
  return max_file_size[level];
}

uint64_t MutableCFOptions::MaxFileSizeForCompaction() const {
  if (!max_file_size_for_compaction) {
    return std::numeric_limits<uint64_t>::max();
  }
  return (*max_file_size_for_compaction)();
}

uint64_t MutableCFOptions::MaxGrandParentOverlapBytes(int level) const {
  return MaxFileSizeForLevel(level) * max_grandparent_overlap_factor;
}
uint64_t MutableCFOptions::ExpandedCompactionByteSizeLimit(int level) const {
  return MaxFileSizeForLevel(level) * expanded_compaction_factor;
}

void MutableCFOptions::Dump(Logger* log) const {
  // Memtable related options
  RLOG(log, "                        write_buffer_size: %" ROCKSDB_PRIszt,
      write_buffer_size);
  RLOG(log, "                  max_write_buffer_number: %d",
      max_write_buffer_number);
  RLOG(log, "                         arena_block_size: %" ROCKSDB_PRIszt,
      arena_block_size);
  RLOG(log, "               memtable_prefix_bloom_bits: %" PRIu32,
      memtable_prefix_bloom_bits);
  RLOG(log, "             memtable_prefix_bloom_probes: %" PRIu32,
      memtable_prefix_bloom_probes);
  RLOG(log, " memtable_prefix_bloom_huge_page_tlb_size: %" ROCKSDB_PRIszt,
      memtable_prefix_bloom_huge_page_tlb_size);
  RLOG(log, "                    max_successive_merges: %" ROCKSDB_PRIszt,
      max_successive_merges);
  RLOG(log, "                           filter_deletes: %d",
      filter_deletes);
  RLOG(log, "                 disable_auto_compactions: %d",
      disable_auto_compactions);
  RLOG(log, "      soft_pending_compaction_bytes_limit: %" PRIu64,
      soft_pending_compaction_bytes_limit);
  RLOG(log, "      hard_pending_compaction_bytes_limit: %" PRIu64,
      hard_pending_compaction_bytes_limit);
  RLOG(log, "       level0_file_num_compaction_trigger: %d",
      level0_file_num_compaction_trigger);
  RLOG(log, "           level0_slowdown_writes_trigger: %d",
      level0_slowdown_writes_trigger);
  RLOG(log, "               level0_stop_writes_trigger: %d",
      level0_stop_writes_trigger);
  RLOG(log, "           max_grandparent_overlap_factor: %d",
      max_grandparent_overlap_factor);
  RLOG(log, "               expanded_compaction_factor: %d",
      expanded_compaction_factor);
  RLOG(log, "                 source_compaction_factor: %d",
      source_compaction_factor);
  RLOG(log, "                    target_file_size_base: %" PRIu64,
      target_file_size_base);
  RLOG(log, "              target_file_size_multiplier: %d",
      target_file_size_multiplier);
  RLOG(log, "                 max_bytes_for_level_base: %" PRIu64,
      max_bytes_for_level_base);
  RLOG(log, "           max_bytes_for_level_multiplier: %d",
      max_bytes_for_level_multiplier);
  std::string result;
  char buf[10];
  for (const auto m : max_bytes_for_level_multiplier_additional) {
    snprintf(buf, sizeof(buf), "%d, ", m);
    result += buf;
  }
  result.resize(result.size() - 2);
  RLOG(log, "max_bytes_for_level_multiplier_additional: %s", result.c_str());
  RLOG(log, "           verify_checksums_in_compaction: %d",
      verify_checksums_in_compaction);
  RLOG(log, "        max_sequential_skip_in_iterations: %" PRIu64,
      max_sequential_skip_in_iterations);
}

}  // namespace rocksdb
