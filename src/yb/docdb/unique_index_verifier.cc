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

#include "yb/docdb/unique_index_verifier.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/value.messages.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/docdb_compaction_context.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/packed_value.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/rocksdb/db.h"

#include "yb/util/fast_varint.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/status_format.h"

DEFINE_test_flag(int32, unique_index_verify_delay_per_group_ms, 0,
    "Sleep this long after each scanned DocKey group, so tests can force deadline-driven "
    "pagination with small data.");

namespace yb::docdb {

namespace {

// Classification of one physical record inside a DocKey group.
YB_DEFINE_ENUM(RecordKind,
    (kTombstone)        // Row-level DEL.
    (kPackedInsert)     // Packed row without the update flag (V1, or V2 sans kIsUpdateFlag).
    (kPackedUpdate)     // Packed V2 row with kIsUpdateFlag.
    (kLivenessColumn)   // The system liveness column of a non-packed insert.
    (kTargetColumn)     // The ybidxbasectid column.
    (kOtherColumn));    // Any other column (INCLUDE columns etc.); carries no identity.

struct PhysicalRecord {
  DocHybridTime doc_ht;
  RecordKind kind;
  // The column of a kTargetColumn / kOtherColumn record.
  ColumnId column_id;
  // Normalized identity bytes for kPacked* / kTargetColumn records (PrimitiveValue
  // representation, comparable across packed and non-packed shapes).
  std::optional<dockv::PrimitiveValue> identity;
};

// The logical event one or more physical records assemble into.
YB_DEFINE_ENUM(EventKind, (kInsert)(kUpdate)(kDelete));

struct LogicalEvent {
  EventKind kind;
  std::optional<dockv::PrimitiveValue> identity;
};

// ybidxbasectid is never null for an index row. A null or tombstoned value is a column delete
// or an encoding the verifier does not expect; taken as an identity it could equate two distinct
// rows, so it fails closed. (The message is value-free: it becomes the result's reason.)
Result<dockv::PrimitiveValue> CheckIdentity(dockv::PrimitiveValue identity) {
  SCHECK(
      identity.value_type() != dockv::ValueEntryType::kNullLow && !identity.IsTombstone(),
      Corruption, "identity column without a value");
  return identity;
}

Result<dockv::PrimitiveValue> UnpackIdentity(dockv::PackedValueV1 value) {
  RETURN_NOT_OK(dockv::ValueControlFields::Decode(&*value));
  return CheckIdentity(VERIFY_RESULT(dockv::UnpackPrimitiveValue(value, DataType::BINARY)));
}

class Verifier {
 public:
  Verifier(
      rocksdb::DB* regular_db, const KeyBounds& key_bounds,
      SchemaPackingProvider* schema_packing_provider, const UniqueIndexVerifierOptions& options)
      : db_(regular_db),
        key_bounds_(key_bounds),
        schema_packing_provider_(schema_packing_provider),
        options_(options),
        iter_(regular_db, VerifierReadOptions(), &key_bounds_) {}

  // The scan is a one-shot pass over history that regular reads never touch; keep it from
  // evicting the working set.
  static rocksdb::ReadOptions VerifierReadOptions() {
    rocksdb::ReadOptions read_options;
    read_options.fill_cache = false;
    return read_options;
  }

  Result<UniqueIndexVerificationResult> Run() {
    if (!options_.start_dockey.empty()) {
      // A resume key inside a group would replay that group's tail from an empty live set and
      // could miss an overlap: accept exactly one encoded DocKey, which is what
      // resume_from_dockey hands out.
      const auto whole_key_size = dockv::DocKey::EncodedSize(
          options_.start_dockey, dockv::DocKeyPart::kWholeDocKey);
      SCHECK(
          whole_key_size.ok() && *whole_key_size == options_.start_dockey.size(), InvalidArgument,
          "Resume key is not a whole DocKey");
      iter_.Seek(options_.start_dockey);
    } else {
      // The regular DB also holds non-row records (transaction apply state, vector index
      // metadata), all sorting before every table row; starting there would read them as
      // undecodable rows in the table's first tablet.
      iter_.Seek(Slice(&dockv::kMinRegularDbTableRowFirstByte, 1));
    }

    while (iter_.Valid()) {
      const auto group_prefix_size = dockv::DocKey::EncodedSize(
          iter_.key(), dockv::DocKeyPart::kWholeDocKey);
      if (!group_prefix_size.ok()) {
        // The dockv status quotes the key bytes; the verifier's outputs stay value-free.
        Inconclusive("undecodable DocKey");
        return result_;
      }
      // Copied: the underlying slice is invalidated by iterator movement.
      const auto group_prefix_str = iter_.key().Prefix(*group_prefix_size).ToBuffer();
      const Slice group_prefix(group_prefix_str);

      if ((options_.max_dockey_groups != 0 &&
           result_.dockey_groups_scanned >= options_.max_dockey_groups) ||
          CoarseMonoClock::Now() >= options_.deadline) {
        result_.resume_from_dockey = group_prefix_str;
        return result_;
      }

      RETURN_NOT_OK(VerifyGroup(group_prefix));
      if (result_.outcome != UniqueIndexVerificationOutcome::kClean) {
        return result_;
      }
      ++result_.dockey_groups_scanned;
      if (PREDICT_FALSE(FLAGS_TEST_unique_index_verify_delay_per_group_ms > 0)) {
        SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_unique_index_verify_delay_per_group_ms));
      }
    }
    RETURN_NOT_OK(iter_.status());
    return result_;
  }

 private:
  // Replays one DocKey group chronologically.
  //
  // Storage order within a group is subkey-section-major, not time-major: all row-level
  // versions (packed rows, tombstones) come first, then each column's versions (the liveness
  // system column, then ybidxbasectid, then any other column). Within a section the order is
  // hybrid time descending, then write ID descending. Chronological replay therefore needs a
  // merge across sections, not a reversal of the scan order -- but because each section is
  // already in reverse chronological order, the merge is a k-way merge from the sections'
  // tails, never a sort. For the common packed-row layout the group is a single section and
  // the merge degenerates to a plain reversal.
  //
  // Full reversal of the within-section order is exactly "hybrid time ascending, write ID
  // ascending within one hybrid time": the replay order, with foreground write IDs (below the
  // marked-domain floor) sorting before floored backfill write IDs at an equal hybrid time.
  // That tie-break is a deliberate ruling, not an accident of the encoding. A backfill
  // candidate carries backfill_read_ht, and the backfill read observed every commit at or
  // below that time, so for a single identity the candidate already agrees with any
  // foreground event at that instant. The order can only matter for a foreground delete of
  // identity A and a backfill insert of identity B sharing one hybrid time: foreground-first
  // yields {B}, the true state at that instant; backfill-first would yield a momentary
  // {A, B} -- a false VIOLATION. Backfill never emits deletes, so no ordering of these two
  // event kinds can hide an overlap; the chosen order only avoids reporting one that does
  // not exist.
  //
  // Other-column records (INCLUDE columns and the like) carry no identity but still mark the
  // extent of a column run (see Consume), so they are buffered with the identity column. A
  // group exceeding the buffer bound falls back to a bounded-memory reverse walk over the same
  // three sections.
  Status VerifyGroup(Slice group_prefix) {
    BufferedSections sections;
    bool buffering = true;
    // The value columns with in-window records, in storage order; the fallback walk opens one
    // reverse cursor per column.
    std::vector<ColumnId> value_columns;

    while (iter_.Valid() && iter_.key().starts_with(group_prefix)) {
      // The forward pass visits every physical version of the group exactly once, whichever
      // replay path runs, so it owns the versions_scanned accounting; the reverse walk's
      // re-visits are deliberately not re-counted.
      ++result_.versions_scanned;
      auto record = VERIFY_RESULT(DecodeRecord(iter_.key(), iter_.value()));
      if (result_.outcome == UniqueIndexVerificationOutcome::kInconclusive) {
        return Status::OK();
      }
      if (record) {
        switch (record->kind) {
          case RecordKind::kLivenessColumn:
            if (buffering) {
              sections.liveness.push_back(std::move(*record));
            }
            break;
          case RecordKind::kTargetColumn: [[fallthrough]];
          case RecordKind::kOtherColumn:
            if (value_columns.empty() || value_columns.back() != record->column_id) {
              value_columns.push_back(record->column_id);
            }
            if (buffering) {
              if (sections.columns.empty() ||
                  sections.columns.back().column_id != record->column_id) {
                sections.columns.push_back({.column_id = record->column_id, .records = {}});
              }
              sections.columns.back().records.push_back(std::move(*record));
            }
            break;
          case RecordKind::kTombstone: [[fallthrough]];
          case RecordKind::kPackedInsert: [[fallthrough]];
          case RecordKind::kPackedUpdate:
            if (buffering) {
              sections.row_level.push_back(std::move(*record));
            }
            break;
        }
        if (buffering && ++sections.size > options_.max_buffered_versions_per_group) {
          buffering = false;
          sections = BufferedSections();
        }
      }
      iter_.Next();
    }
    RETURN_NOT_OK(iter_.status());

    if (buffering) {
      return ReplayBuffered(std::move(sections));
    }
    return ReverseWalkGroup(group_prefix, value_columns);
  }

  // The in-window records of one group, bucketed by subkey section, each in storage order
  // (hybrid time descending, write ID descending), i.e. oldest at the back.
  struct ColumnSection {
    ColumnId column_id;
    std::vector<PhysicalRecord> records;
  };
  struct BufferedSections {
    std::vector<PhysicalRecord> row_level;  // Packed rows and tombstones.
    std::vector<PhysicalRecord> liveness;   // The liveness system column.
    std::vector<ColumnSection> columns;     // One per value column, in storage order.
    size_t size = 0;
  };

  // Buffered path: a k-way merge from the sections' tails, which is chronological order.
  Status ReplayBuffered(BufferedSections sections) {
    BeginGroup();
    std::vector<std::vector<PhysicalRecord>*> tails{&sections.row_level, &sections.liveness};
    for (auto& column : sections.columns) {
      tails.push_back(&column.records);
    }
    while (result_.outcome == UniqueIndexVerificationOutcome::kClean) {
      std::vector<PhysicalRecord>* next = nullptr;
      for (auto* section : tails) {
        if (section->empty()) {
          continue;
        }
        if (next == nullptr || section->back().doc_ht < next->back().doc_ht) {
          next = section;
        }
      }
      if (next == nullptr) {
        break;
      }
      auto record = std::move(next->back());
      next->pop_back();
      RETURN_NOT_OK(Consume(std::move(record)));
    }
    return FinishGroup();
  }

  // A reverse cursor over one subkey section of a DocKey group (the row-level section or one
  // column's section). Within a section, storage order is hybrid time descending / write ID
  // descending, so a Prev() walk from the section's end is natively chronological.
  class SectionCursor {
   public:
    SectionCursor(rocksdb::DB* db, const KeyBounds* key_bounds, std::string section_prefix)
        : section_prefix_(std::move(section_prefix)),
          iter_(db, Verifier::VerifierReadOptions(), key_bounds) {
      // No valid key byte is kMaxByte, so prefix + kMaxByte seeks past every key of the
      // section (same construction as the compaction code's ranged deletes).
      std::string past_section = section_prefix_;
      past_section.push_back(dockv::KeyEntryTypeAsChar::kMaxByte);
      iter_.Seek(past_section);
      if (iter_.Valid()) {
        iter_.Prev();
      } else if (ConsumeStatus().ok()) {
        iter_.SeekToLast();  // Past the end of the DB.
      }
      Revalidate();
    }

    bool valid() const { return valid_; }
    Slice key() const { return iter_.key(); }
    Slice value() const { return iter_.value(); }

    void Advance() {
      iter_.Prev();
      Revalidate();
    }

    Status status() const { return status_; }

   private:
    // The iterator contract (enforced by debug builds at every move and at destruction) is that
    // status() is read whenever Valid() has returned false; the merge above stops consulting a
    // cursor once it leaves its section, so the read happens here, at the transition.
    const Status& ConsumeStatus() {
      status_ = iter_.status();
      return status_;
    }

    void Revalidate() {
      if (!iter_.Valid()) {
        valid_ = false;
        status_ = iter_.status();
        return;
      }
      valid_ = iter_.key().starts_with(section_prefix_);
    }

    std::string section_prefix_;
    BoundedRocksDbIterator iter_;
    bool valid_ = false;
    Status status_;
  };

  // Bounded-memory fallback for oversized groups: merge reverse section cursors -- row-level
  // records (packed rows, tombstones), the liveness column, and every value column present in
  // the group -- in chronological order. Memory is O(sections). Other columns are walked too:
  // they carry no identity, but a column run's extent (see Consume) is defined by every
  // column record of the transaction.
  Status ReverseWalkGroup(Slice group_prefix, const std::vector<ColumnId>& value_columns) {
    BeginGroup();

    const auto group = group_prefix.ToBuffer();
    auto make_column_prefix = [&group](const dockv::KeyEntryValue& column) {
      dockv::KeyBytes key;
      key.AppendRawBytes(group);
      column.AppendToKey(&key);
      return key.ToStringBuffer();
    };
    // Row-level keys are group + kHybridTime + inverted doc hybrid time; column keys are
    // group + <subkey> + kHybridTime + ..., so prefixing with the group plus the hybrid-time
    // terminator scopes the first cursor to exactly the row-level section.
    std::string row_level_prefix = group;
    row_level_prefix.push_back(dockv::KeyEntryTypeAsChar::kHybridTime);
    std::vector<SectionCursor> cursors;
    cursors.reserve(2 + value_columns.size());
    cursors.emplace_back(db_, &key_bounds_, row_level_prefix);
    cursors.emplace_back(
        db_, &key_bounds_, make_column_prefix(dockv::KeyEntryValue::kLivenessColumn));
    for (const auto& column_id : value_columns) {
      cursors.emplace_back(
          db_, &key_bounds_, make_column_prefix(dockv::KeyEntryValue::MakeColumnId(column_id)));
    }

    while (result_.outcome == UniqueIndexVerificationOutcome::kClean) {
      // Pick the chronologically smallest current record among valid cursors.
      SectionCursor* next = nullptr;
      std::optional<PhysicalRecord> next_record;
      for (auto& cursor : cursors) {
        while (cursor.valid()) {
          auto record = VERIFY_RESULT(DecodeRecord(cursor.key(), cursor.value()));
          if (result_.outcome == UniqueIndexVerificationOutcome::kInconclusive) {
            return Status::OK();
          }
          if (!record) {  // Outside the window.
            cursor.Advance();
            continue;
          }
          if (next == nullptr || record->doc_ht < next_record->doc_ht) {
            next = &cursor;
            next_record = std::move(*record);
          }
          break;
        }
        RETURN_NOT_OK(cursor.status());
      }
      if (next == nullptr) {
        break;
      }
      next->Advance();
      RETURN_NOT_OK(Consume(std::move(*next_record)));
    }
    return FinishGroup();
  }

  // One non-packed logical event being assembled from adjacent column records.
  struct ColumnRun {
    bool active = false;
    HybridTime hybrid_time;
    bool marked = false;
    IntraTxnWriteId write_id = 0;  // Meaningful for marked runs only.
    bool saw_liveness = false;
    std::optional<dockv::PrimitiveValue> identity;
  };

  void BeginGroup() {
    live_.reset();
    run_ = ColumnRun();
  }

  Status FinishGroup() {
    if (result_.outcome != UniqueIndexVerificationOutcome::kClean) {
      // The merge stopped on a verdict with records still unread; the run left open is
      // incomplete (its identity record may be among them), not evidence of anything.
      run_ = ColumnRun();
      return Status::OK();
    }
    return FlushRun();
  }

  // Replays one in-window record. The merges above deliver a group's records in chronological
  // order (hybrid time ascending, write ID ascending), so events are applied as they close and
  // nothing is buffered per hybrid time. Packed rows and tombstones are self-contained events.
  // Non-packed column records assemble into events by adjacency:
  //  - Marked records (write ID at or above the marked-domain floor) come from marked
  //    backfill batches, where every record of one operation shares the operation's
  //    Raft-index-derived write ID -- so a run is one exact write ID, one event per backfill
  //    operation. This is what lets two non-packed backfill candidates at the fixed hybrid
  //    time surface as two inserts (a Violation) rather than an ambiguity.
  //  - Unmarked records come from foreground transactions, whose intents take consecutive
  //    write IDs from the intra-transaction sequence, and two committed transactions cannot
  //    both write the same DocKey at one commit hybrid time (their writes take conflicting
  //    strong intents, conflict resolution serializes them, and HLC causality makes the later
  //    commit strictly greater) -- so a maximal run of unmarked column records at one hybrid
  //    time is one transaction's. A row-level record inside the run splits it: a transaction
  //    that deletes and re-inserts a row (unpacked) writes columns, a tombstone, then columns
  //    again, and folding both column sets into one event placed before the tombstone would
  //    replay the re-insert as deleted -- an empty live set that hides a later overlap.
  //    Conflicting shapes within one run (two distinct identities) fail closed as
  //    INCONCLUSIVE.
  // Run semantics: liveness present -> insert (identity from the ybidxbasectid record; an
  // insert always carries it); a lone ybidxbasectid record -> in-place update (yb_lsm.c
  // doAssignForIdxUpdate); other-column records alone (e.g. INCLUDE updates) -> no event.
  Status Consume(PhysicalRecord record) {
    switch (record.kind) {
      case RecordKind::kTombstone:
        RETURN_NOT_OK(FlushRun());
        return ApplyEvent(LogicalEvent{EventKind::kDelete, std::nullopt});
      case RecordKind::kPackedInsert:
        RETURN_NOT_OK(FlushRun());
        return ApplyEvent(LogicalEvent{EventKind::kInsert, std::move(record.identity)});
      case RecordKind::kPackedUpdate:
        RETURN_NOT_OK(FlushRun());
        return ApplyEvent(LogicalEvent{EventKind::kUpdate, std::move(record.identity)});
      case RecordKind::kLivenessColumn: [[fallthrough]];
      case RecordKind::kTargetColumn:   [[fallthrough]];
      case RecordKind::kOtherColumn:
        break;
    }
    const auto write_id = record.doc_ht.write_id();
    const bool marked = write_id >= kBackfillWriteIdFloor;
    if (run_.active &&
        (run_.hybrid_time != record.doc_ht.hybrid_time() || run_.marked != marked ||
         (marked && run_.write_id != write_id))) {
      RETURN_NOT_OK(FlushRun());
    }
    if (!run_.active) {
      run_.active = true;
      run_.hybrid_time = record.doc_ht.hybrid_time();
      run_.marked = marked;
      run_.write_id = write_id;
    }
    if (record.kind == RecordKind::kLivenessColumn) {
      run_.saw_liveness = true;
    } else if (record.kind == RecordKind::kTargetColumn) {
      if (run_.identity && *run_.identity != *record.identity) {
        Inconclusive("multiple distinct identities in one non-packed event group");
        return Status::OK();
      }
      run_.identity = std::move(record.identity);
    }
    return Status::OK();
  }

  Status FlushRun() {
    if (!run_.active) {
      return Status::OK();
    }
    ColumnRun run = std::exchange(run_, ColumnRun());
    if (run.saw_liveness) {
      if (!run.identity) {
        Inconclusive("insert-shaped event group without an identity column");
        return Status::OK();
      }
      return ApplyEvent(LogicalEvent{EventKind::kInsert, std::move(run.identity)});
    }
    if (run.identity) {
      return ApplyEvent(LogicalEvent{EventKind::kUpdate, std::move(run.identity)});
    }
    return Status::OK();
  }

  // The live-identity state machine (design document 3.2.4 / scan-algorithm-0812 Finding 4).
  Status ApplyEvent(const LogicalEvent& event) {
    switch (event.kind) {
      case EventKind::kInsert:
        if (!live_) {
          live_ = event.identity;
          return Status::OK();
        }
        if (*live_ == *event.identity) {
          return Status::OK();  // Idempotent retry (chunk retry, maintenance rewrite).
        }
        result_.outcome = UniqueIndexVerificationOutcome::kViolation;
        result_.reason = "second distinct identity became live in a DocKey group";
        return Status::OK();
      case EventKind::kUpdate:
        // Replace, or establish on an empty set: an update can only be issued against an
        // entry that was already live, and it cannot mask a duplicate because SKIP_ALL
        // re-materializes every window-start identity as an insert inside the window.
        live_ = event.identity;
        return Status::OK();
      case EventKind::kDelete:
        live_.reset();
        return Status::OK();
    }
    FATAL_INVALID_ENUM_VALUE(EventKind, event.kind);
  }

  // Decodes one physical record; returns nullopt for records outside the verification
  // window. Sets kInconclusive (and returns nullopt) for encodings the verifier cannot
  // interpret conclusively. Decode failures are reported without the key or value bytes.
  Result<std::optional<PhysicalRecord>> DecodeRecord(Slice key, Slice value) {
    dockv::SubDocKey subdoc_key;
    if (!subdoc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kTrue).ok()) {
      Inconclusive("undecodable SubDocKey");
      return std::nullopt;
    }
    const auto doc_ht = subdoc_key.doc_hybrid_time();

    const auto ht = doc_ht.hybrid_time();
    if (ht < options_.window_lower || ht > options_.window_upper) {
      // Below the window: superseded pre-backfill maintenance, re-materialized by SKIP_ALL
      // inside the window. Above the window: outside this verification's responsibility.
      return std::nullopt;
    }

    PhysicalRecord record;
    record.doc_ht = doc_ht;

    const auto& subkeys = subdoc_key.subkeys();
    if (subkeys.empty()) {
      return DecodeRowLevelRecord(subdoc_key, value, std::move(record));
    }
    if (subkeys.size() != 1) {
      Inconclusive("unexpected subkey depth");
      return std::nullopt;
    }
    const auto& subkey = subkeys.front();
    if (subkey.type() == dockv::KeyEntryType::kSystemColumnId) {
      if (subkey != dockv::KeyEntryValue::kLivenessColumn) {
        Inconclusive("unexpected system column");
        return std::nullopt;
      }
      record.kind = RecordKind::kLivenessColumn;
      return record;
    }
    if (subkey.type() != dockv::KeyEntryType::kColumnId) {
      Inconclusive("unexpected subkey type");
      return std::nullopt;
    }
    record.column_id = subkey.GetColumnId();
    if (record.column_id != options_.ybidxbasectid_column_id) {
      record.kind = RecordKind::kOtherColumn;
      return record;
    }
    record.kind = RecordKind::kTargetColumn;
    auto identity = UnpackIdentity(dockv::PackedValueV1(value));
    if (!identity.ok()) {
      Inconclusive("undecodable identity column value");
      return std::nullopt;
    }
    record.identity = std::move(*identity);
    return record;
  }

  Result<std::optional<PhysicalRecord>> DecodeRowLevelRecord(
      const dockv::SubDocKey& subdoc_key, Slice value, PhysicalRecord record) {
    auto value_copy = value;
    auto control_fields_result = dockv::ValueControlFields::Decode(&value_copy);
    if (!control_fields_result.ok() || value_copy.empty()) {
      Inconclusive("undecodable row-level value");
      return std::nullopt;
    }
    const auto value_type = dockv::DecodeValueEntryType(value_copy);
    if (value_type == dockv::ValueEntryType::kTombstone) {
      record.kind = RecordKind::kTombstone;
      return std::move(record);
    }
    const auto packed_version = dockv::GetPackedRowVersion(value_type);
    if (!packed_version) {
      Inconclusive("unexpected row-level value type");
      return std::nullopt;
    }

    value_copy.consume_byte();
    auto identity_result = DecodePackedIdentity(
        subdoc_key.doc_key(), *packed_version, &value_copy, &record);
    if (!identity_result.ok()) {
      Inconclusive("undecodable packed row");
      return std::nullopt;
    }
    return std::move(record);
  }

  // Extracts the ybidxbasectid column from a packed row and classifies insert vs update
  // (packed V2 rows carry kIsUpdateFlag when written by full-row-update packing; V1 rows
  // and unflagged V2 rows are classified as inserts).
  //
  // Treating an update as an insert is fail-closed: an insert of the identity already live
  // is idempotent, and an insert of a different identity raises a VIOLATION. The latter is a
  // false alarm only if an in-place index-row update changed ybidxbasectid without a
  // preceding delete, which YSQL index maintenance never does (an identity change is a
  // delete plus an insert). Returning INCONCLUSIVE for V1 instead would fail every build on a
  // cluster still writing V1 packed rows, for no gain in soundness.
  Status DecodePackedIdentity(
      const dockv::DocKey& doc_key, dockv::PackedRowVersion version, Slice* value,
      PhysicalRecord* record) {
    SchemaVersion schema_version;
    bool is_update = false;
    if (version == dockv::PackedRowVersion::kV2) {
      // The V2 header parse does not advance the slice; consume the schema-version varint so
      // the slice lands on the flags byte, which is where PackedRowDecoderV2 expects it.
      const auto header = VERIFY_RESULT(dockv::ParsePackedRowV2Header(*value));
      schema_version = header.schema_version;
      is_update = header.IsUpdate();
      RETURN_NOT_OK(FastDecodeUnsignedVarInt(value));
    } else {
      schema_version = narrow_cast<SchemaVersion>(
          VERIFY_RESULT(FastDecodeUnsignedVarInt(value)));
    }

    CompactionSchemaInfo packing_info;
    if (doc_key.colocation_id() != kColocationIdNotSet) {
      packing_info = VERIFY_RESULT(schema_packing_provider_->ColocationPacking(
          doc_key.colocation_id(), schema_version, HybridTime::kMax));
    } else {
      packing_info = VERIFY_RESULT(schema_packing_provider_->CotablePacking(
          doc_key.has_cotable_id() ? doc_key.cotable_id() : Uuid::Nil(), schema_version,
          HybridTime::kMax));
    }
    const auto& packing = *packing_info.schema_packing;

    switch (version) {
      case dockv::PackedRowVersion::kV1: {
        record->kind = RecordKind::kPackedInsert;
        auto column_value = packing.GetValue(options_.ybidxbasectid_column_id, *value);
        SCHECK(column_value.has_value(), NotFound, "identity column absent from packed row");
        record->identity = VERIFY_RESULT(UnpackIdentity(dockv::PackedValueV1(*column_value)));
        return Status::OK();
      }
      case dockv::PackedRowVersion::kV2: {
        record->kind = is_update ? RecordKind::kPackedUpdate : RecordKind::kPackedInsert;
        dockv::PackedRowDecoderV2 decoder(packing, value->data());
        auto column_value = decoder.FetchValue(options_.ybidxbasectid_column_id);
        SCHECK(!column_value.IsNull(), NotFound, "identity column null in packed row");
        record->identity = VERIFY_RESULT(CheckIdentity(
            VERIFY_RESULT(dockv::UnpackPrimitiveValue(column_value, DataType::BINARY))));
        return Status::OK();
      }
    }
    FATAL_INVALID_ENUM_VALUE(dockv::PackedRowVersion, version);
  }

  void Inconclusive(const std::string& reason) {
    result_.outcome = UniqueIndexVerificationOutcome::kInconclusive;
    result_.reason = reason;
  }

  rocksdb::DB* db_;
  const KeyBounds& key_bounds_;
  SchemaPackingProvider* schema_packing_provider_;
  const UniqueIndexVerifierOptions& options_;
  BoundedRocksDbIterator iter_;
  UniqueIndexVerificationResult result_;
  std::optional<dockv::PrimitiveValue> live_;
  ColumnRun run_;
};

}  // namespace

std::string UniqueIndexVerificationResult::ToString() const {
  // The resume key is an index key (user data): report its presence and size only.
  const auto resume_from_dockey_size = resume_from_dockey.size();
  return YB_STRUCT_TO_STRING(outcome, reason, dockey_groups_scanned, versions_scanned,
                             resume_from_dockey_size);
}

Result<UniqueIndexVerificationResult> VerifyUniqueIndexTablet(
    rocksdb::DB* regular_db,
    const KeyBounds& key_bounds,
    SchemaPackingProvider* schema_packing_provider,
    const UniqueIndexVerifierOptions& options) {
  SCHECK(options.window_lower && options.window_upper, InvalidArgument,
         "Verification window bounds must be valid hybrid times");
  SCHECK_LE(options.window_lower, options.window_upper, InvalidArgument,
            "Verification window is inverted");
  return Verifier(regular_db, key_bounds, schema_packing_provider, options).Run();
}

}  // namespace yb::docdb
