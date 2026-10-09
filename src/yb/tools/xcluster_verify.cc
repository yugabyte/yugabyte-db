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

#include "yb/tools/xcluster_verify.h"

#include <memory>
#include <mutex>
#include <set>

#include <boost/algorithm/string.hpp>
#include <rapidjson/document.h>

#include "yb/common/json_util.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/status_format.h"
#include "yb/util/threadpool.h"

namespace yb::tools {
namespace {

bool ContainsIgnoreCase(const std::string& haystack, const char* needle) {
  return boost::algorithm::ifind_first(haystack, needle);
}

// A side's scheme as it belongs in an operator-facing message. nullopt is not 0: it means no server
// was asked, so no scheme was named.
std::string SchemeVersionText(const TableHashTotals& totals) {
  return totals.hash_scheme_version ? Format("$0", *totals.hash_scheme_version)
                                    : "none (no tablets hashed)";
}

// One reading of the two catalogs. An empty `failure` is the only case in which the caller may go
// on to compare hashes; the fingerprints are carried out so a later reading can compare each side
// against its own earlier self.
struct SchemaCheck {
  // Set when this reading is already conclusive: kSchemaMismatch for drift, or whatever a failed
  // fetch classifies as.
  std::optional<SliceVerifyOutcome> failure;
  // Set for each side whose fetch succeeded, so both are set when only the comparison failed.
  SchemaFingerprint source;
  SchemaFingerprint target;
};

SchemaCheck CompareSchemas(
    const SliceVerifyRequest& req,
    const FetchSchemaFn& fetch_source_schema,
    const FetchSchemaFn& fetch_target_schema) {
  SchemaCheck check;
  auto source = fetch_source_schema(req.source_table_id);
  if (!source.ok()) {
    auto result = ClassifyStatus(source.status(), XClusterClassifyContext::kSchema);
    check.failure = BaseSliceOutcome(req, result, source.status().ToString());
    return check;
  }
  check.source = std::move(*source);
  auto target = fetch_target_schema(req.target_table_id);
  if (!target.ok()) {
    auto result = ClassifyStatus(target.status(), XClusterClassifyContext::kSchema);
    check.failure = BaseSliceOutcome(req, result, target.status().ToString());
    return check;
  }
  check.target = std::move(*target);
  if (!(check.source == check.target)) {
    check.failure = BaseSliceOutcome(
        req, XClusterVerifyResult::kSchemaMismatch,
        Format("schema fingerprint differs source=$0 target=$1", check.source.ToString(),
               check.target.ToString()));
  }
  return check;
}

// A side that never hashed is omitted rather than reported as zero, so a failed side cannot be
// mistaken for an empty one. next_key is not emitted: both sides hash the same window, so the only
// continuation is the outcome's top-level end_key_hex.
void AddHashSide(
    const char* name, const std::optional<TableHashTotals>& totals, rapidjson::Value* out,
    rapidjson::Value::AllocatorType* alloc) {
  if (!totals) {
    return;
  }
  rapidjson::Value side(rapidjson::kObjectType);
  side.AddMember("xor_hash", totals->xor_hash, *alloc);
  side.AddMember("row_count", totals->row_count, *alloc);
  // Emitted so a reader can confirm from the output alone that both sides hashed at one instant.
  side.AddMember("read_ht", totals->read_ht, *alloc);
  // Lets a reader comparing two runs, or archiving a hash to compare later, tell whether the two
  // are commensurable. Omitted when this side hashed no tablets and named no scheme.
  if (totals->hash_scheme_version) {
    side.AddMember("hash_scheme_version", *totals->hash_scheme_version, *alloc);
  }
  out->AddMember(rapidjson::StringRef(name), side, *alloc);
}

}  // namespace

SliceVerifyOutcome BaseSliceOutcome(
    const SliceVerifyRequest& req, XClusterVerifyResult result, const std::string& detail) {
  SliceVerifyOutcome outcome;
  outcome.result = result;
  outcome.read_ht = req.read_ht;
  outcome.source_table_id = req.source_table_id;
  outcome.target_table_id = req.target_table_id;
  outcome.start_key_hex = strings::b2a_hex(req.start_key);
  outcome.end_key = req.end_key;
  outcome.detail = detail;
  return outcome;
}

std::string SchemaFingerprint::ToString() const {
  std::string out = "[";
  for (size_t i = 0; i < columns.size(); ++i) {
    if (i > 0) {
      out += ", ";
    }
    const auto& col = columns[i];
    out += Format(
        "($0,$1,key=$2,hash=$3,null=$4,static=$5,sort=$6)", col.id, col.type, col.is_key,
        col.is_hash_key, col.is_nullable, col.is_static, col.sorting_type);
  }
  // Outside the column list, so a mismatch caused by one of them is distinguishable from identical
  // column lists differing for no visible reason.
  out += Format("] partitioning_version=$0 ttl=$1", partitioning_version, default_time_to_live);
  return out;
}

Result<SchemaFingerprint> BuildSchemaFingerprint(const client::YBSchema& schema) {
  // YBSchema::ColumnId reads Schema::col_ids_ behind nothing but a DCHECK, so an id-less schema
  // (YBSchemaBuilder assembles exactly that) walks off the end of an empty vector in a release
  // build. Rejected rather than read as positions: positions compare equal across a pair whose ids
  // differ, which is the mismatch this fingerprint exists to catch.
  SCHECK(
      client::internal::GetSchema(schema).has_column_ids(), InvalidArgument,
      "Cannot fingerprint a schema that carries no column ids");
  SchemaFingerprint fingerprint;
  fingerprint.partitioning_version = schema.table_properties().partitioning_version();
  fingerprint.default_time_to_live = schema.table_properties().DefaultTimeToLive();
  fingerprint.columns.reserve(schema.num_columns());
  for (size_t i = 0; i < schema.num_columns(); ++i) {
    auto col = schema.Column(i);
    ColumnFingerprint row;
    row.id = schema.ColumnId(i);
    row.type = col.type() ? col.type()->ToString() : "";
    row.is_key = col.is_key();
    row.is_hash_key = col.is_hash_key();
    row.is_nullable = col.is_nullable();
    row.is_static = col.is_static();
    row.sorting_type = col.sorting_type();
    fingerprint.columns.push_back(std::move(row));
  }
  return fingerprint;
}

XClusterVerifyResult ClassifyStatus(
    const Status& status, XClusterClassifyContext context) {
  // An OK status falls through to the verdict that claims nothing; no single Status can support
  // kMatch.
  DCHECK(!status.ok()) << "ClassifyStatus classifies failures; OK is not a verdict";
  if (status.ok()) {
    return XClusterVerifyResult::kError;
  }
  // Read before the timeout below. Rpc::Finished (rpc.cc) reports a deadline expiry by folding the
  // last error's text into a fresh TimedOut, so a table dropped mid-verify arrives with its inner
  // NotFound code gone and the message the only surviving evidence. Read as a plain timeout it
  // would be kTryAgain, which a driver retries forever on a table that is never coming back. The
  // cost is that an unrelated master failure whose message happens to name something not found is
  // called a schema mismatch.
  if (context == XClusterClassifyContext::kSchema) {
    if (status.IsNotFound()) {
      return XClusterVerifyResult::kSchemaMismatch;
    }
    // Without the file and line ToString prepends by default, which the match would otherwise read
    // as though it were part of the message.
    const auto message = status.ToString(/* include_file_and_line = */ false);
    if (ContainsIgnoreCase(message, "not found") ||
        ContainsIgnoreCase(message, "does not exist")) {
      return XClusterVerifyResult::kSchemaMismatch;
    }
  }
  // A replica that has not caught up to the read time reports TryAgain, which no generic Status
  // predicate distinguishes from an unrelated retryable failure, so the tserver code is what names
  // it (tablet_dump_helper.cc, ReadTimeNotReachedStatus).
  if (tserver::TabletServerError(status) == tserver::TabletServerErrorPB::READ_TIME_NOT_REACHED) {
    return XClusterVerifyResult::kTryAgain;
  }
  // kTryAgain rather than kError because the common cause is a replica still catching up, and a
  // driver told "infra" would stop retrying a slice that would succeed. A master or tserver that is
  // simply down times out identically, which is why kTryAgain does not bound its own retries.
  if (status.IsSnapshotTooOld() || status.IsTimedOut()) {
    return XClusterVerifyResult::kTryAgain;
  }
  return XClusterVerifyResult::kError;
}

namespace {

void BuildSliceOutcomeDocument(
    const SliceVerifyOutcome& outcome, rapidjson::Document* document_ptr) {
  auto& document = *document_ptr;
  document.SetObject();
  auto* alloc = &document.GetAllocator();
  common::AddMember("result", std::string(ToCString(outcome.result)), &document,
                    alloc);
  // Omitted when no read time was ever established. Emitting the 0 placeholder would name an
  // instant that was never read at, and a driver checkpointing read_ht would record it as one.
  if (outcome.read_ht) {
    document.AddMember("read_ht", outcome.read_ht, *alloc);
  }
  common::AddMember("source_table_id", outcome.source_table_id, &document, alloc);
  common::AddMember("target_table_id", outcome.target_table_id, &document, alloc);
  common::AddMember("start_key_hex", outcome.start_key_hex, &document, alloc);
  common::AddMember("end_key_hex", strings::b2a_hex(outcome.end_key), &document, alloc);
  common::AddMember("detail", outcome.detail, &document, alloc);
  AddHashSide("source", outcome.source, &document, alloc);
  AddHashSide("target", outcome.target, &document, alloc);
}

}  // namespace

std::string SliceVerifyOutcomeToJson(const SliceVerifyOutcome& outcome) {
  rapidjson::Document document;
  BuildSliceOutcomeDocument(outcome, &document);
  return common::PrettyWriteRapidJsonToString(document);
}

std::string SliceVerifyOutcomeToJsonLine(const SliceVerifyOutcome& outcome) {
  rapidjson::Document document;
  BuildSliceOutcomeDocument(outcome, &document);
  return common::WriteRapidJsonToString(document);
}

namespace {

// Gives req a concrete read time when the caller named none, and returns the outcome to report when
// one could not be established.
std::optional<SliceVerifyOutcome> EnsureReadTime(
    SliceVerifyRequest* req, const ResolveReadTimeFn& resolve_read_time) {
  if (req->read_ht) {
    return std::nullopt;
  }
  if (!resolve_read_time) {
    return BaseSliceOutcome(
        *req, XClusterVerifyResult::kError,
        "no read time was supplied and no way to resolve one was provided");
  }
  auto resolved = resolve_read_time();
  if (!resolved.ok()) {
    return BaseSliceOutcome(
        *req, ClassifyStatus(resolved.status(), XClusterClassifyContext::kHash),
        Format("unable to resolve a read time for this slice: $0", resolved.status().ToString()));
  }
  if (!*resolved) {
    return BaseSliceOutcome(
        *req, XClusterVerifyResult::kError,
        "read time resolver returned 0, which is not a valid hybrid time");
  }
  req->read_ht = *resolved;
  return std::nullopt;
}

// The two ways a pair of hashes can fail to be a basis for any verdict, agreement included.
std::optional<std::string> RejectHashSchemes(
    const TableHashTotals& source, const TableHashTotals& target) {
  // Checked before equality, because the case it exists for is two sides agreeing on 0: a pair on
  // which neither universe has been upgraded yet.
  if (!HashSchemeUsable(source) || !HashSchemeUsable(target)) {
    return Format(
        "unusable row hash scheme: source hashed under version $0, target under version $1. "
        "Version 0 is what a server too old to report a scheme hashes under, and that scheme "
        "gives a row's values no column or row identity, so two hashes agreeing under it is "
        "not evidence that the rows agree. Upgrade both universes to a version that reports "
        "a hash scheme, then verify. Nothing is known about the data.",
        SchemeVersionText(source), SchemeVersionText(target));
  }
  // Different schemes turn identical data into unrelated words, so a disagreement is not evidence
  // of anything. This is the ordinary state of a pair mid-upgrade, and calling it kDiverged would
  // announce total data loss at the moment an operator is least equipped to dismiss it.
  if (!HashSchemesComparable(source, target)) {
    return Format(
        "hash scheme mismatch: source hashed under version $0, target under version $1. The "
        "two sides are running binaries that hash rows differently (an upgrade in progress, "
        "most likely), so their hashes cannot be compared. Nothing is known about the data; "
        "re-run once both sides are on the same version.",
        *source.hash_scheme_version, *target.hash_scheme_version);
  }
  return std::nullopt;
}

// Closes the schema sandwich, replacing the hash verdict when the catalogs no longer support it.
SliceVerifyOutcome ApplyClosingSchemaCheck(
    const SliceVerifyRequest& req, const SchemaCheck& before, const SliceVerifyOutcome& outcome,
    const FetchSchemaFn& fetch_source_schema, const FetchSchemaFn& fetch_target_schema) {
  auto after = CompareSchemas(req, fetch_source_schema, fetch_target_schema);
  // Both readings answer only "do the sides agree right now?", so alone they miss a DDL that landed
  // on BOTH clusters mid-hash -- under automatic DDL replication the ordinary shape of propagation,
  // not a rare race. Comparing each side against its own pre-hash self is what catches it.
  if (!after.failure && (!(after.source == before.source) || !(after.target == before.target))) {
    after.failure = BaseSliceOutcome(
        req, XClusterVerifyResult::kSchemaMismatch,
        Format(
            "schema changed while hashing: source $0 -> $1, target $2 -> $3",
            before.source.ToString(), after.source.ToString(), before.target.ToString(),
            after.target.ToString()));
  }
  if (!after.failure) {
    return outcome;
  }
  // A failed closing fetch overrides the hash verdict too: with no closing reading, nothing rules
  // out a DDL having landed mid-hash, so kMatch must not be claimed. The cost is that a flaky
  // master turns clean matches into slices a driver has to re-run.
  //
  // Over a kDiverged hash that failure must not read as kTryAgain, which would have the sweep
  // abandon the rest of a table already known to disagree; kError keeps it from being promoted.
  if (outcome.result == XClusterVerifyResult::kDiverged &&
      after.failure->result != XClusterVerifyResult::kSchemaMismatch) {
    after.failure->result = XClusterVerifyResult::kError;
  }
  after.failure->source = outcome.source;
  after.failure->target = outcome.target;
  after.failure->end_key = outcome.end_key;
  // Always, not only when the hash left a detail: kMatch carries an empty one, so gating on it
  // would drop the annotation from exactly the override worth naming.
  after.failure->detail =
      Format("$0; hash=$1", after.failure->detail, ToCString(outcome.result));
  return *after.failure;
}

}  // namespace

SliceVerifyOutcome VerifyXClusterSlice(
    const SliceVerifyRequest& request,
    const FetchSchemaFn& fetch_source_schema,
    const FetchSchemaFn& fetch_target_schema,
    const HashSliceFn& hash_source,
    const HashSliceFn& hash_target,
    const ResolveReadTimeFn& resolve_read_time) {
  // Everything below works off this copy, so the read time is concrete from here on and both sides
  // read the one field holding it.
  SliceVerifyRequest req = request;
  if (auto unresolved = EnsureReadTime(&req, resolve_read_time)) {
    return *unresolved;
  }

  auto before = CompareSchemas(req, fetch_source_schema, fetch_target_schema);
  if (before.failure) {
    return *before.failure;
  }

  SliceVerifyOutcome outcome;
  std::optional<TableHashTotals> last_source;
  std::optional<TableHashTotals> last_target;
  std::string hashed_end = req.end_key;

  auto finish = [&](XClusterVerifyResult result, const std::string& detail) {
    outcome = BaseSliceOutcome(req, result, detail);
    outcome.source = last_source;
    outcome.target = last_target;
    outcome.end_key = hashed_end;
  };

  do {
    auto source_hash =
        hash_source(req.source_table_id, req.read_ht, req.start_key, hashed_end, req.max_rows);
    if (!source_hash.ok()) {
      finish(
          ClassifyStatus(source_hash.status(), XClusterClassifyContext::kHash),
          source_hash.status().ToString());
      break;
    }
    last_source = *source_hash;
    if (!last_source->next_key.empty()) {
      hashed_end = last_source->next_key;
    }

    auto target_hash =
        hash_target(req.target_table_id, req.read_ht, req.start_key, hashed_end, /*max_rows=*/0);
    if (!target_hash.ok()) {
      finish(
          ClassifyStatus(target_hash.status(), XClusterClassifyContext::kHash),
          target_hash.status().ToString());
      break;
    }
    last_target = *target_hash;

    // Whether the two hashes can found a verdict at all, which has to be settled before whether
    // they agree.
    if (auto rejection = RejectHashSchemes(*last_source, *last_target)) {
      finish(XClusterVerifyResult::kError, *rejection);
      break;
    }

    if (HashesMatch(*last_source, *last_target)) {
      finish(XClusterVerifyResult::kMatch, /* detail = */ "");
      break;
    }
    finish(
        XClusterVerifyResult::kDiverged, "xor_hash/row_count mismatch");
  } while (false);

  return ApplyClosingSchemaCheck(
      req, before, outcome, fetch_source_schema, fetch_target_schema);
}

namespace {

// How much a verdict weighs on the sweep's single answer, ordered by what the reader has to do
// about it. See GroupVerifySummary::result.
int VerdictSeverity(XClusterVerifyResult result) {
  switch (result) {
    case XClusterVerifyResult::kDiverged: return 4;
    case XClusterVerifyResult::kError: return 3;
    case XClusterVerifyResult::kSchemaMismatch: return 2;
    case XClusterVerifyResult::kTryAgain: return 1;
    case XClusterVerifyResult::kMatch: return 0;
  }
  FATAL_INVALID_ENUM_VALUE(XClusterVerifyResult, result);
}

// Whether a verdict says anything about this range's data. An unjudged slice leaves no position
// worth continuing from: its end key is the requested range end, not one it hashed.
bool SliceWasJudged(XClusterVerifyResult result) {
  return result == XClusterVerifyResult::kMatch || result == XClusterVerifyResult::kDiverged;
}

}  // namespace

void ApplyVerdict(GroupVerifySummary* summary, XClusterVerifyResult result) {
  if (VerdictSeverity(result) > VerdictSeverity(summary->result)) {
    summary->result = result;
  }
}

namespace {

// One range of one pair: the unit of work a thread takes off the queue.
struct VerifyWorkUnit {
  const TablePairToVerify* pair;
  KeyRange range;
};

struct RangeResult {
  // A slice reached no verdict, so this range is not covered and its table is unfinished.
  bool abandoned = false;
  // A failure that ends the whole sweep, rather than news about the data.
  Status status;
};

// Verifies one range to its end. Slices within a range stay sequential because each starts where
// the last stopped; only whole ranges run concurrently.
RangeResult VerifyOneRange(
    const VerifyWorkUnit& unit, const GroupVerifyOptions& options,
    const VerifySliceFn& verify_slice,
    const std::function<void(const SliceVerifyOutcome&)>& on_outcome) {
  RangeResult result;
  // Where the next slice starts, in the byte space the bounds already use. Nothing here converts to
  // hex: an outcome carries both forms, and comparing the bytes avoids inventing a second encoding
  // of a position this function was handed.
  std::string position = unit.range.start;

  while (true) {
    SliceVerifyRequest req;
    req.source_table_id = unit.pair->source_table_id;
    req.target_table_id = unit.pair->target_table_id;
    req.start_key = position;
    req.end_key = unit.range.end;
    // Every slice resolves its own read time, which is why a sweep long enough to outlive the
    // source's history retention still finishes.
    req.max_rows = options.max_rows;

    // A bad Status ends the whole sweep; a bad verdict does not. The first means the sweep could
    // not run, the second is the news it was sent to bring back.
    auto outcome = verify_slice(req);
    if (!outcome.ok()) {
      result.status = outcome.status();
      return result;
    }
    on_outcome(*outcome);

    // Nothing trustworthy to continue from, so give up on this range. The table's other ranges are
    // independent and still run.
    if (!SliceWasJudged(outcome->result)) {
      result.abandoned = true;
      return result;
    }
    // Reaching the range's end covers it; stopping short is where the row cap stopped the scan. An
    // open-ended range ends where a slice reports no end at all, having hashed to the end of the
    // table.
    const bool reached_end = unit.range.end.empty() ? outcome->end_key.empty()
                                                    : outcome->end_key >= unit.range.end;
    if (reached_end) {
      return result;
    }
    // The continuation comes from a tserver and this loop has no bound of its own, so a key that
    // does not advance would spin here forever, emitting slices.
    if (!(outcome->end_key > position)) {
      result.status = STATUS_FORMAT(
          IllegalState, "table $0: continuation key did not advance past $1",
          unit.pair->source_table_id,
          position.empty() ? "the start of the table" : strings::b2a_hex(position));
      return result;
    }
    position = outcome->end_key;
  }
}

}  // namespace

// Splits every pair into ranges, then verifies ranges concurrently. Why ranges are independent, and
// what `unfinished` means, are on the declaration.
Result<GroupVerifySummary> VerifyXClusterTablePairs(
    const std::vector<TablePairToVerify>& pairs,
    const GroupVerifyOptions& options,
    const VerifySliceFn& verify_slice,
    const ListKeyRangesFn& list_key_ranges,
    const ReportSliceFn& report_slice) {
  SCHECK(verify_slice != nullptr, InvalidArgument, "verify_slice callback is required");
  SCHECK(list_key_ranges != nullptr, InvalidArgument, "list_key_ranges callback is required");

  // Every range is enumerated before any is verified, so a table that cannot be split fails the
  // sweep before the others have spent time on it.
  std::vector<VerifyWorkUnit> units;
  for (const auto& pair : pairs) {
    auto ranges = VERIFY_RESULT_PREPEND(
        list_key_ranges(pair.source_table_id),
        Format("Unable to split source table $0 into key ranges", pair.source_table_id));
    // No ranges at all would verify nothing and still report a clean sweep.
    SCHECK_FORMAT(
        !ranges.empty(), IllegalState, "source table $0 produced no key ranges to verify",
        pair.source_table_id);
    for (auto& range : ranges) {
      units.push_back(VerifyWorkUnit{.pair = &pair, .range = std::move(range)});
    }
  }

  // Starts at kMatch and only moves to something worse.
  GroupVerifySummary summary;
  std::set<TableId> tables_seen;
  std::set<TableId> unfinished;
  Status sweep_status;
  // Guards everything above, and report_slice, whose writes have to stay one record per line.
  std::mutex mutex;

  auto record = [&](const TableId& source_table_id, const SliceVerifyOutcome& outcome) {
    std::lock_guard<std::mutex> lock(mutex);
    ++summary.slices;
    ++summary.counts[outcome.result];
    ApplyVerdict(&summary, outcome.result);
    tables_seen.insert(source_table_id);
    if (report_slice) {
      report_slice(outcome);
    }
  };

  auto run_unit = [&](const VerifyWorkUnit& unit) {
    {
      // Once the sweep has failed the remaining ranges say nothing, so stop spending time on them.
      std::lock_guard<std::mutex> lock(mutex);
      if (!sweep_status.ok()) {
        return;
      }
    }
    auto result = VerifyOneRange(
        unit, options, verify_slice,
        [&](const SliceVerifyOutcome& outcome) { record(unit.pair->source_table_id, outcome); });
    std::lock_guard<std::mutex> lock(mutex);
    if (result.abandoned) {
      unfinished.insert(unit.pair->source_table_id);
    }
    if (!result.status.ok() && sweep_status.ok()) {
      sweep_status = std::move(result.status);
    }
  };

  const int concurrency = std::max(1, options.max_concurrent_ranges);
  if (concurrency == 1) {
    // No pool at all, so a sequential sweep behaves exactly as it did before and stays trivially
    // ordered for tests.
    for (const auto& unit : units) {
      run_unit(unit);
    }
  } else {
    std::unique_ptr<ThreadPool> pool;
    RETURN_NOT_OK(ThreadPoolBuilder("xcluster_verify")
                      .set_min_threads(1)
                      .set_max_threads(concurrency)
                      .Build(&pool));
    for (const auto& unit : units) {
      RETURN_NOT_OK(pool->SubmitFunc([&run_unit, &unit]() { run_unit(unit); }));
    }
    pool->Wait();
  }

  RETURN_NOT_OK(sweep_status);
  summary.tables_started = narrow_cast<int>(tables_seen.size());
  // Sorted rather than in the order the ranges gave up, which concurrency makes non-deterministic.
  summary.unfinished.assign(unfinished.begin(), unfinished.end());
  return summary;
}

std::string GroupVerifySummaryToJson(const GroupVerifySummary& summary) {
  rapidjson::Document document;
  document.SetObject();
  auto* alloc = &document.GetAllocator();
  common::AddMember(
      "result", std::string(ToCString(summary.result)), &document, alloc);
  document.AddMember("slices", summary.slices, *alloc);
  document.AddMember("tables", summary.tables_started, *alloc);
  rapidjson::Value counts(rapidjson::kObjectType);
  for (const auto& [result, count] : summary.counts) {
    rapidjson::Value name(ToCString(result), *alloc);
    counts.AddMember(name, count, *alloc);
  }
  document.AddMember("counts", counts, *alloc);
  // Present only when there is something to act on: these are tables a driver has to re-run itself,
  // since a sweep that ran to completion covered everything else.
  if (!summary.unfinished.empty()) {
    rapidjson::Value unfinished(rapidjson::kArrayType);
    for (const auto& table_id : summary.unfinished) {
      unfinished.PushBack(rapidjson::Value(table_id.c_str(), *alloc), *alloc);
    }
    document.AddMember("unfinished", unfinished, *alloc);
  }
  // Likewise, and separate from unfinished: these were never verifiable, rather than started and
  // abandoned, so a driver cannot re-run them -- it has to reconcile the two catalogs.
  if (!summary.unpaired.empty()) {
    rapidjson::Value unpaired(rapidjson::kArrayType);
    for (const auto& description : summary.unpaired) {
      unpaired.PushBack(rapidjson::Value(description.c_str(), *alloc), *alloc);
    }
    document.AddMember("unpaired", unpaired, *alloc);
  }
  return common::WriteRapidJsonToString(document);
}

}  // namespace yb::tools
