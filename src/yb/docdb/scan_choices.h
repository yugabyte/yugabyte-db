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

#pragma once

#include "yb/common/doc_hybrid_time.h"

#include "yb/qlexpr/ql_scanspec.h"

#include "yb/dockv/doc_key.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/dockv/value.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/value_type.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/result.h"

namespace yb::docdb {

class ScanChoices {
 public:
  ScanChoices() = default;

  ScanChoices(const ScanChoices&) = delete;
  void operator=(const ScanChoices&) = delete;

  virtual ~ScanChoices() = default;

  // Returns false if there are still target keys we need to scan, and true if we are done.
  virtual bool Finished() const = 0;

  // Returns true iff any of the scan choices is interested in specified row.
  // Returns non empty Status on unexpected error such as corruption.
  // Seeks on specified iterator to the next row of interest.
  // Rollbacks to max_seen_ht_checkpoint if not interested in the row before the Seek.
  virtual Result<bool> InterestedInRow(
      dockv::KeyBytes* row_key, IntentAwareIterator& iter,
      const EncodedDocHybridTime& max_seen_ht_checkpoint) = 0;
  // Call when done with the current row.
  // Advances iterator to the next row that is potentially of interest.
  // Returns false if the current scan choice is still looking for more rows.
  virtual Result<bool> AdvanceToNextRow(dockv::KeyBytes* row_key,
                                        IntentAwareIterator& iter,
                                        bool current_fetched_row_skipped) = 0;

  // Initialize iterator before iteration, returns true if upper bound was set by ScanChoices.
  virtual Result<bool> PrepareIterator(IntentAwareIterator& iter, Slice table_key_prefix) = 0;

  virtual docdb::BloomFilterOptions BloomFilterOptions() = 0;

  static Result<ScanChoicesPtr> Create(
      const DocReadContext& doc_read_context, const qlexpr::YQLScanSpec& doc_spec,
      const qlexpr::ScanBounds& bounds, Slice table_key_prefix,
      AllowVariableBloomFilter allow_variable_bloom_filter);

  static ScanChoicesPtr CreateEmpty();
};

}  // namespace yb::docdb
