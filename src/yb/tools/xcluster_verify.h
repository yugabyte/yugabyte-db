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

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "yb/client/schema.h"

#include "yb/common/constants.h"
#include "yb/common/entity_ids.h"

#include "yb/tools/table_hash.h"

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb::tools {

// Per-slice xCluster verify result. The JSON `result` field carries these enumerator spellings.
//
// kMatch: both sides hashed identically.
// kDiverged: both sides hashed successfully and still disagreed. The only verdict claiming data
//     loss, so it is reached only from two known-good hashes.
// kTryAgain: the requested read could not produce a verdict because a replica was behind, the read
//     time fell outside history retention, or an RPC timed out. Retry with a bounded policy and,
//     after SnapshotTooOld, a fresh read time.
// kSchemaMismatch: the catalogs disagree, so no hash comparison is meaningful.
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
  // A YCQL static column is written under the hash-key-only DocKey rather than the row's, so sides
  // differing here hold the same value in different places. Not implied by the key flags.
  bool is_static = false;
  // Decides the key encoding, so a bound names a different logical position on a side that sorts
  // the other way: the two then hash non-overlapping windows and can agree. Every other field here
  // guards against a false alarm; this one guards against a false match.
  SortingType sorting_type = SortingType::kNotSpecified;

  bool operator==(const ColumnFingerprint&) const = default;
};

// Comparable summary of how a table packs its rows.
//
// Column names and schema version are excluded: an xCluster pair may differ on both without
// changing a row hash. missing_value is excluded for the opposite reason -- a column with no rows
// predating it never has it read, so capturing it would report identically-hashing sides as
// kSchemaMismatch. The cost is that sides substituting different defaults are reported kDiverged.
//
// YBSchema::EquivalentForDataCopy answers a related question, but it compares column names and its
// type check sees only the top-level type, so it cannot tell map<int, text> from map<int, int>.
// Nor is it always applied: automatic-mode setup skips schema validation altogether, leaving this
// fingerprint the only thing standing between a column id difference and a false kMatch.
struct SchemaFingerprint {
  std::vector<ColumnFingerprint> columns;

  // Decides how a range boundary is encoded, so sides differing here read the same key bytes as
  // different logical positions. TableProperties::Equivalent ignores it, so an older source and a
  // freshly created target can differ without setup objecting.
  uint32_t partitioning_version = 0;
  // Decides which rows are live at a given read time; sides differing here hash different row sets.
  int64_t default_time_to_live = 0;

  bool operator==(const SchemaFingerprint&) const = default;
  std::string ToString() const;
};

Result<SchemaFingerprint> BuildSchemaFingerprint(const client::YBSchema& schema);

// Map a failed Status to kTryAgain (retention, timeout), kSchemaMismatch (missing table on a schema
// fetch), or kError. Never returns kMatch or kDiverged: neither is a claim a single Status can
// support.
XClusterVerifyResult ClassifyStatus(const Status& status, XClusterClassifyContext context);

// Row count is compared along with the xor, because an xor alone cannot distinguish an empty slice
// from one whose row hashes cancel out. Meaningful only once HashSchemesComparable says so.
inline bool HashesMatch(const TableHashTotals& left, const TableHashTotals& right) {
  return left.xor_hash == right.xor_hash && left.row_count == right.row_count;
}

// Whether two sides' hashes answer the same question. False only when both hashed under differing
// schemes -- a pair mid-upgrade. A side that hashed no tablets (nullopt) named no scheme and cannot
// clash. Not sufficient alone; see HashSchemeUsable.
inline bool HashSchemesComparable(const TableHashTotals& left, const TableHashTotals& right) {
  return !left.hash_scheme_version || !right.hash_scheme_version ||
         *left.hash_scheme_version == *right.hash_scheme_version;
}

// Whether a side's hash can found a verdict at all. Version 0 is what a server too old to report a
// version hashed under: a plain xor with no column or row identity (see
// tablet::kTabletDataHashSchemeVersion), so (1,5,7) and (1,7,5) hash alike and rows can cancel out.
// Comparability does not catch it, because both not-yet-upgraded sides report 0.
inline bool HashSchemeUsable(const TableHashTotals& totals) {
  return !totals.hash_scheme_version || *totals.hash_scheme_version > 0;
}

// One unit of verify work: a table pair, a read time, and a partition-key range to hash on both
// sides.
struct SliceVerifyRequest {
  TableId source_table_id;
  TableId target_table_id;
  // The hybrid time both sides are hashed at. 0 means "pick one for me". Never left at 0 for the
  // hash callbacks to interpret: each would resolve it against its own cluster's clock, pinning
  // different instants.
  uint64_t read_ht = 0;
  // Decoded form only, so the range hashed and the range reported cannot drift apart.
  std::string start_key;
  std::string end_key;
  // Cap source rows (0 = unlimited). Where the scan stopped becomes the end for the target hash.
  uint64_t max_rows = 0;
};

// What verify concluded, plus enough of the request echoed back to report the slice.
//
// end_key is the end actually hashed, which differs from the requested end when max_rows stopped
// the source scan early. An outcome that never hashed echoes the requested end, so it is not a
// continuation key there; the sweep only advances on a judged slice.
//
// source / target carry each side's totals when that side hashed successfully.
struct SliceVerifyOutcome {
  XClusterVerifyResult result = XClusterVerifyResult::kError;
  // The hybrid time the slice was pinned to, for replaying it at exactly that instant. 0 means none
  // was established, and the JSON omits the field rather than reporting it as an instant.
  uint64_t read_ht = 0;
  TableId source_table_id;
  TableId target_table_id;
  std::string start_key_hex;
  // Raw bytes, hex-encoded only on the way into the JSON. The sweep continues from this, so
  // advancing a range never encodes and decodes a key it was handed.
  std::string end_key;
  std::optional<TableHashTotals> source;
  std::optional<TableHashTotals> target;
  std::string detail;
};

std::string SliceVerifyOutcomeToJson(const SliceVerifyOutcome& outcome);

// The same object on one line, for the sweep's stream. See GroupVerifySummaryToJson.
std::string SliceVerifyOutcomeToJsonLine(const SliceVerifyOutcome& outcome);

// Outcome carrying only the fields every result echoes from the request, with no hash totals, so a
// caller rejecting a slice before VerifyXClusterSlice reports it in the same shape.
SliceVerifyOutcome BaseSliceOutcome(
    const SliceVerifyRequest& request, XClusterVerifyResult result, const std::string& detail);

// VerifyXClusterSlice reaches the two clusters only through these callbacks, so the decision logic
// can be unit tested without a cluster.
using FetchSchemaFn = std::function<Result<SchemaFingerprint>(const TableId&)>;
// table_id, read_ht, start_key, end_key, max_rows (0 = unlimited).
using HashSliceFn =
    std::function<Result<TableHashTotals>(const TableId&, uint64_t, Slice, Slice, uint64_t)>;
using ResolveReadTimeFn = std::function<Result<uint64_t>()>;

// Schema sandwich around hashing one slice: compare catalogs, hash both sides at the same T,
// compare catalogs again, with drift reported as kSchemaMismatch and the xor discarded.
//
// Hashes under differing schemes, or under one too weak to found a verdict on, are kError: not
// evidence in either direction.
//
// T comes from request.read_ht, or from resolve_read_time when that is 0. The caller must ensure T
// is at or below the target's xCluster safe time so a mismatch cannot be replication lag.
SliceVerifyOutcome VerifyXClusterSlice(
    const SliceVerifyRequest& request,
    const FetchSchemaFn& fetch_source_schema,
    const FetchSchemaFn& fetch_target_schema,
    const HashSliceFn& hash_source,
    const HashSliceFn& hash_target,
    const ResolveReadTimeFn& resolve_read_time = {});

// One replicated table, as the sweep below walks it.
struct TablePairToVerify {
  TableId source_table_id;
  TableId target_table_id;
};

struct GroupVerifyOptions {
  // Rows one slice may hash before it stops and reports where it stopped (0 = a range is one
  // slice). This bounds a single scan, so no read time is held open past history retention.
  uint64_t max_rows = 0;
  // Key ranges verified at once (0 or 1 = sequential). A sweep runs to completion either way; this
  // only decides how much of it is in flight.
  int max_concurrent_ranges = 1;
};

// What a sweep concluded.
struct GroupVerifySummary {
  // Worst verdict any slice reported, by the ordering a caller has to act on: kDiverged (data is
  // gone) beats kError (the tool broke, so the rest is unknown) beats kSchemaMismatch beats
  // kTryAgain (not verified yet) beats kMatch.
  XClusterVerifyResult result = XClusterVerifyResult::kMatch;
  int slices = 0;
  int tables_started = 0;
  std::map<XClusterVerifyResult, int> counts;
  // Source tables with at least one key range abandoned part-way because a slice reached no
  // verdict. A sweep always runs to completion, so this is the only thing that says a table was not
  // wholly covered, and a driver re-runs these itself.
  std::vector<TableId> unfinished;
  // Tables the caller found on one cluster and not the other, described for a human. Divergence
  // comparing pairs cannot see, since every pair can hash to kMatch while one universe holds a
  // table the other does not, so it forces the verdict away from kMatch through ApplyVerdict.
  std::vector<std::string> unpaired;
};

// Folds in a verdict reached outside the slice loop, keeping whichever is worse, so conclusions a
// caller draws on its own weigh on the summary by the same ordering the slices do.
void ApplyVerdict(GroupVerifySummary* summary, XClusterVerifyResult result);

// A half-open key range, in the byte space slice bounds use. An empty end runs to the end of the
// table; a single {"", ""} range is the whole table.
struct KeyRange {
  std::string start;
  std::string end;
};

// Verifies one slice. Called from several threads when max_concurrent_ranges > 1, so an
// implementation has to be safe to call concurrently.
using VerifySliceFn = std::function<Result<SliceVerifyOutcome>(const SliceVerifyRequest&)>;
// The ranges to split a source table into, ordinarily its tablet boundaries. Called once per pair
// before any slice runs, from the calling thread only.
using ListKeyRangesFn = std::function<Result<std::vector<KeyRange>>(const TableId&)>;
// Called with each slice's outcome as it happens, so a long sweep reports progress. The sweep
// serializes these, so an implementation does not need its own lock.
using ReportSliceFn = std::function<void(const SliceVerifyOutcome&)>;

// Verifies every pair to completion. There is no resume protocol and no budget: an invocation
// either covers the group or reports which tables it could not.
//
// Each pair is split into ranges by list_key_ranges, and up to options.max_concurrent_ranges ranges
// are verified at once. Ranges are independent, which is what makes them safe to run concurrently:
// a range is a logical key interval, interpreted identically on both clusters however either one is
// split, so the source's tablet boundaries are usable as bounds against a target split differently.
//
// Within a range slices stay sequential, because a slice's start is the previous slice's end --
// get_table_hash's Next key protocol, one level up. A range is done when a slice stops exactly at
// the range's end rather than short of it.
//
// A slice that could not be judged ends its range rather than advancing, since there is no
// trustworthy position to continue from. The other ranges of that table still run, and the table is
// named in summary.unfinished.
Result<GroupVerifySummary> VerifyXClusterTablePairs(
    const std::vector<TablePairToVerify>& pairs,
    const GroupVerifyOptions& options,
    const VerifySliceFn& verify_slice,
    const ListKeyRangesFn& list_key_ranges,
    const ReportSliceFn& report_slice = {});

// The sweep's verdict as JSON. This and SliceVerifyOutcomeToJsonLine write one line each, because a
// sweep emits a stream of them and pretty-printed objects run together into something only a
// streaming parser can split. A single-slice run prints one document and stays pretty.
std::string GroupVerifySummaryToJson(const GroupVerifySummary& summary);

}  // namespace yb::tools
