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

#pragma once

#include <string>
#include <vector>

#include "yb/client/schema.h"

#include "yb/common/entity_ids.h"

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb::tools {

// Per-slice xCluster verify result. The JSON `result` field carries these enumerator spellings.
//
// kMatch: both sides hashed to the same xor and row count on the first attempt.
// kDiverged: both sides hashed successfully and still disagreed. The only result claiming data
//     loss, so it is reported only when both hashes are known good.
// kTryAgain: the requested read could not produce a verdict because a replica was behind, the read
//     time fell outside history retention, or an RPC timed out. Retry with a bounded policy and,
//     after SnapshotTooOld, a fresh read time.
// kSchemaMismatch: the two catalogs disagree, which makes any hash comparison meaningless.
// kError: another tool or cluster error prevented verification. Says nothing about the data.
YB_DEFINE_ENUM(XClusterVerifyResult,
    (kMatch)(kDiverged)(kTryAgain)(kError)(kSchemaMismatch));

// Whether a failed Status came from catalog lookup or from DumpTabletData hashing.
YB_DEFINE_ENUM(XClusterClassifyContext, (kHash)(kSchema));

// Packing-relevant column identity. Names and schema_version are diagnostic only.
struct ColumnFingerprint {
  int32_t id = 0;
  std::string type;
  bool is_key = false;
  bool is_hash_key = false;
  bool is_nullable = false;
  // A YCQL static column's value is written under the hash-key-only DocKey instead of the row's, so
  // two sides differing here hold the same value in different places. Not part of ColumnKind, so
  // the key flags cannot stand in for it.
  bool is_static = false;
  // Decides the key encoding, so a bound names a different logical position on a side that sorts
  // the other way: the two then hash different windows and can agree over non-overlapping data.
  // Every other field here guards against a false alarm; this one guards against a false match.
  SortingType sorting_type = SortingType::kNotSpecified;

  bool operator==(const ColumnFingerprint&) const = default;
};

// Comparable summary of how a table packs its rows.
//
// Column names and schema version are excluded: an xCluster pair may differ on both -- renames
// propagate, DDL lands at different times -- without changing a single row hash.
//
// missing_value (what a schema-versioned ADD COLUMN ... DEFAULT substitutes for rows written before
// the column existed) is excluded for the opposite reason. A column with no rows predating it never
// has its missing_value read, so capturing it would report sides that hash identically as
// kSchemaMismatch. The cost is that sides which do substitute different defaults hash differently
// and are reported kDiverged, naming data loss rather than the catalog difference responsible.
//
// YBSchema::EquivalentForDataCopy answers a related question, but it compares column names and its
// type check sees only the top-level type, so it cannot tell map<int, text> from map<int, int>.
// Nor is it always applied: automatic-mode setup skips schema validation altogether, leaving this
// fingerprint the only thing standing between a column id difference and a false kMatch.
struct SchemaFingerprint {
  std::vector<ColumnFingerprint> columns;

  // Decides how a range boundary is encoded, so sides differing here read the same key bytes as
  // different logical positions. It is fixed at table creation and TableProperties::Equivalent
  // ignores it, so an older source and a freshly created target can differ without setup objecting.
  uint32_t partitioning_version = 0;
  // Decides which rows are still live at a given read time. Sides differing here hash different row
  // sets, which without this field surfaces as kDiverged.
  int64_t default_time_to_live = 0;

  bool operator==(const SchemaFingerprint&) const = default;
  std::string ToString() const;
};

Result<SchemaFingerprint> BuildSchemaFingerprint(const client::YBSchema& schema);

// Map a failed Status to kTryAgain (retention, timeout), kSchemaMismatch (missing table on a schema
// fetch), or kError. Never returns kMatch or kDiverged: neither is a claim a single Status can
// support.
XClusterVerifyResult ClassifyStatus(
    const Status& status, XClusterClassifyContext context);

}  // namespace yb::tools
