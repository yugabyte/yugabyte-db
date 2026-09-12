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

#include <string>

#include "yb/common/column_id.h"
#include "yb/common/hybrid_time.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/util/enums.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"

namespace yb::docdb {

// Per-tablet outcome of a deferred uniqueness verification scan (#33444).
// kClean: no DocKey group in the scanned range ever had two distinct live identities.
// kViolation: some group had a second distinct identity become live inside the window.
// kInconclusive: a physical record could not be interpreted conclusively; fail closed.
YB_DEFINE_ENUM(UniqueIndexVerificationOutcome, (kClean)(kViolation)(kInconclusive));

struct UniqueIndexVerifierOptions {
  // Inclusive verification window [backfill_read_ht, verify_upper_ht]. Sufficiency of this
  // window rests on the SKIP_ALL invariant: backfill unconditionally re-materializes every
  // identity live at backfill_read_ht *at* backfill_read_ht, so each DocKey group replays
  // from an empty live set. Any change that reintroduces conditional backfill writes
  // invalidates this scan range.
  HybridTime window_lower;
  HybridTime window_upper;

  // The ybidxbasectid column of the unique index (a regular value column; the identity).
  ColumnId ybidxbasectid_column_id;

  // Encoded DocKey to resume from (inclusive); empty scans from the beginning of the tablet's
  // key bounds. Pagination is DocKey-aligned: a group's history is never split across calls.
  std::string start_dockey;

  // Stop (with a resume key) after this many complete DocKey groups; 0 = unbounded.
  size_t max_dockey_groups = 0;

  // Stop (with a resume key) at the first group boundary past the deadline.
  CoarseTimePoint deadline = CoarseTimePoint::max();

  // Version-group buffering bound; a group exceeding it is replayed by the bounded-memory
  // reverse walk instead of being buffered.
  size_t max_buffered_versions_per_group = 1024;
};

struct UniqueIndexVerificationResult {
  UniqueIndexVerificationOutcome outcome = UniqueIndexVerificationOutcome::kClean;

  // Value-free context for kViolation / kInconclusive: encoding classes and counts only,
  // never key or value bytes.
  std::string reason;

  // Raw encoded DocKey group of the first violating group: the in-process input for the
  // caller's keyed diagnostic fingerprint. Never serialized, and deliberately excluded from
  // ToString() -- raw index key bytes must not reach logs, errors, or support bundles.
  std::string violating_group_prefix;

  // Non-empty when the scan stopped early (group budget or deadline): the encoded DocKey to
  // resume from. Empty means the scan reached the end of the key bounds.
  std::string resume_from_dockey;

  size_t dockey_groups_scanned = 0;
  // Physical versions in the scanned groups, counted exactly once per version regardless of
  // which replay path ran (the bounded-memory reverse walk's re-visits are not re-counted;
  // fallback_groups records that extra work instead).
  size_t versions_scanned = 0;
  // Groups that exceeded max_buffered_versions_per_group and took the bounded-memory
  // reverse walk, which re-reads the group (roughly doubling its scan cost).
  size_t fallback_groups = 0;

  std::string ToString() const;
};

// Scans the regular database of a unique-index tablet and replays every DocKey group's
// physical history in the window chronologically (hybrid time ascending; write ID ascending
// within one hybrid time, which orders foreground records before floored backfill records).
// Read-only. The caller is responsible for ensuring intents through window_upper are applied
// and history at window_lower is retained.
Result<UniqueIndexVerificationResult> VerifyUniqueIndexTablet(
    rocksdb::DB* regular_db,
    const KeyBounds& key_bounds,
    SchemaPackingProvider* schema_packing_provider,
    const UniqueIndexVerifierOptions& options);

}  // namespace yb::docdb
