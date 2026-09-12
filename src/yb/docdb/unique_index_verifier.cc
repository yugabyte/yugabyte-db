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
#include <array>
#include <map>
#include <optional>
#include <string>
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
  // Normalized identity bytes for kPacked* / kTargetColumn records (PrimitiveValue
  // representation, comparable across packed and non-packed shapes).
  std::optional<dockv::PrimitiveValue> identity;
};

// The logical event a batch of same-hybrid-time records assembles into.
YB_DEFINE_ENUM(EventKind, (kInsert)(kUpdate)(kDelete)(kNoOp));

struct LogicalEvent {
  EventKind kind;
  std::optional<dockv::PrimitiveValue> identity;
};

Result<dockv::PrimitiveValue> UnpackIdentity(dockv::PackedValueV1 value) {
  RETURN_NOT_OK(dockv::ValueControlFields::Decode(&*value));
  return dockv::UnpackPrimitiveValue(value, DataType::BINARY);
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
      iter_.Seek(options_.start_dockey);
    } else {
      iter_.SeekToFirst();
    }

    while (iter_.Valid()) {
      const auto group_prefix_size = VERIFY_RESULT(dockv::DocKey::EncodedSize(
          iter_.key(), dockv::DocKeyPart::kWholeDocKey));
      // Copied: the underlying slice is invalidated by iterator movement.
      const auto group_prefix_str = iter_.key().Prefix(group_prefix_size).ToBuffer();
      const Slice group_prefix(group_prefix_str);

      if ((options_.max_dockey_groups != 0 &&
           result_.dockey_groups_scanned >= options_.max_dockey_groups) ||
          CoarseMonoClock::Now() >= options_.deadline) {
        result_.resume_from_dockey = group_prefix_str;
        return result_;
      }

      RETURN_NOT_OK(VerifyGroup(group_prefix));
      if (result_.outcome != UniqueIndexVerificationOutcome::kClean) {
        if (result_.outcome == UniqueIndexVerificationOutcome::kViolation) {
          result_.violating_group_prefix = group_prefix_str;
        }
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
  // Replays one DocKey group chronologically. Storage order within a group is hybrid time
  // descending, then write ID descending; full reversal of that order is exactly
  // "hybrid time ascending, write ID ascending within one hybrid time" -- the required
  // replay order, with foreground write IDs (below the marked-domain floor) sorting before
  // floored backfill write IDs at an equal hybrid time. The group is buffered and replayed
  // in reverse; a group exceeding the buffer bound falls back to a bounded-memory reverse
  // walk (Prev() from the group's end is natively chronological).
  Status VerifyGroup(Slice group_prefix) {
    std::vector<PhysicalRecord> buffered;
    bool buffering = true;

    while (iter_.Valid() && iter_.key().starts_with(group_prefix)) {
      // The forward pass visits every physical version of the group exactly once, whichever
      // replay path runs, so it owns the versions_scanned accounting; the reverse walk's
      // re-visits are deliberately not re-counted (fallback_groups records that extra work).
      ++result_.versions_scanned;
      if (buffering) {
        auto record = VERIFY_RESULT(DecodeRecord(group_prefix, iter_.key(), iter_.value()));
        if (record) {
          buffered.push_back(std::move(*record));
        }
        if (result_.outcome == UniqueIndexVerificationOutcome::kInconclusive) {
          return Status::OK();
        }
        if (buffered.size() > options_.max_buffered_versions_per_group) {
          buffering = false;
          buffered.clear();
        }
      }
      iter_.Next();
    }
    RETURN_NOT_OK(iter_.status());

    if (buffering) {
      return ReplayBuffered(std::move(buffered));
    }
    ++result_.fallback_groups;
    return ReverseWalkGroup(group_prefix);
  }

  // Buffered path. Storage order within a group is subkey-section-major (all row-level
  // versions, then each column's versions), not hybrid-time-major, so the buffer is sorted
  // into chronological order -- hybrid time ascending, write ID ascending -- before batching
  // records that share one hybrid time.
  Status ReplayBuffered(std::vector<PhysicalRecord> buffered) {
    live_.reset();
    std::sort(
        buffered.begin(), buffered.end(), [](const PhysicalRecord& a, const PhysicalRecord& b) {
          return a.doc_ht < b.doc_ht;
        });
    std::vector<PhysicalRecord> ht_batch;
    for (auto& record : buffered) {
      if (!ht_batch.empty() &&
          ht_batch.back().doc_ht.hybrid_time() != record.doc_ht.hybrid_time()) {
        RETURN_NOT_OK(ReplayHybridTimeBatch(ht_batch));
        if (result_.outcome != UniqueIndexVerificationOutcome::kClean) {
          return Status::OK();
        }
        ht_batch.clear();
      }
      ht_batch.push_back(std::move(record));
    }
    if (!ht_batch.empty()) {
      RETURN_NOT_OK(ReplayHybridTimeBatch(ht_batch));
    }
    return Status::OK();
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
      } else {
        iter_.SeekToLast();
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

    Status status() const { return iter_.status(); }

   private:
    void Revalidate() {
      valid_ = iter_.Valid() && iter_.key().starts_with(section_prefix_);
    }

    std::string section_prefix_;
    BoundedRocksDbIterator iter_;
    bool valid_ = false;
  };

  // Bounded-memory fallback for oversized groups: merge three reverse section cursors --
  // row-level records (packed rows, tombstones), the liveness column, and the ybidxbasectid
  // column -- in chronological order. Other columns never affect the live-identity state, so
  // their sections are not walked. Memory is O(records at one hybrid time).
  Status ReverseWalkGroup(Slice group_prefix) {
    live_.reset();

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
    std::array<SectionCursor, 3> cursors{
        SectionCursor(db_, &key_bounds_, row_level_prefix),
        SectionCursor(db_, &key_bounds_, make_column_prefix(dockv::KeyEntryValue::kLivenessColumn)),
        SectionCursor(
            db_, &key_bounds_,
            make_column_prefix(
                dockv::KeyEntryValue::MakeColumnId(options_.ybidxbasectid_column_id)))};

    std::vector<PhysicalRecord> ht_batch;
    while (true) {
      // Pick the chronologically smallest current record among valid cursors.
      SectionCursor* next = nullptr;
      DocHybridTime next_doc_ht;
      std::optional<PhysicalRecord> next_record;
      for (auto& cursor : cursors) {
        while (cursor.valid()) {
          auto record = VERIFY_RESULT(DecodeRecord(group_prefix, cursor.key(), cursor.value()));
          if (result_.outcome == UniqueIndexVerificationOutcome::kInconclusive) {
            return Status::OK();
          }
          if (!record) {  // Outside the window.
            cursor.Advance();
            continue;
          }
          if (next == nullptr || record->doc_ht < next_doc_ht) {
            next = &cursor;
            next_doc_ht = record->doc_ht;
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

      if (!ht_batch.empty() &&
          ht_batch.back().doc_ht.hybrid_time() != next_record->doc_ht.hybrid_time()) {
        RETURN_NOT_OK(ReplayHybridTimeBatch(ht_batch));
        if (result_.outcome != UniqueIndexVerificationOutcome::kClean) {
          return Status::OK();
        }
        ht_batch.clear();
      }
      ht_batch.push_back(std::move(*next_record));
    }
    if (!ht_batch.empty()) {
      RETURN_NOT_OK(ReplayHybridTimeBatch(ht_batch));
    }
    return Status::OK();
  }

  // One non-packed logical event being assembled from column records.
  struct ColumnGroup {
    IntraTxnWriteId position = 0;  // Write ID of the group's first record (event order).
    bool has_records = false;
    bool saw_liveness = false;
    std::optional<dockv::PrimitiveValue> identity;
  };

  // Assembles the records of one hybrid time (write ID ascending) into logical events and
  // applies them in write ID order. Packed rows and tombstones are self-contained events at
  // their own write IDs. Non-packed column records group into events by write-ID structure:
  //  - Marked records (write ID at or above the marked-domain floor) come from marked
  //    backfill batches, where every record of one operation shares the operation's
  //    Raft-index-derived write ID -- so column records group by exact write ID, one event
  //    per backfill operation. This is what lets two non-packed backfill candidates at the
  //    fixed hybrid time surface as two inserts (a Violation) rather than an ambiguity.
  //  - Unmarked records come from foreground transactions, whose intents take consecutive
  //    write IDs from the intra-transaction sequence -- so all unmarked column records of
  //    one hybrid time assemble into a single event (two foreground writers of one DocKey
  //    at one commit hybrid time would collide physically; conflicting shapes fail closed).
  // Group semantics: liveness present -> insert (identity from the ybidxbasectid record; an
  // insert always carries it); a lone ybidxbasectid record -> in-place update (yb_lsm.c
  // doAssignForIdxUpdate); other-column records alone (e.g. INCLUDE updates) -> no event.
  Status ReplayHybridTimeBatch(const std::vector<PhysicalRecord>& batch) {
    std::vector<std::pair<IntraTxnWriteId, LogicalEvent>> events;
    ColumnGroup unmarked_group;
    std::map<IntraTxnWriteId, ColumnGroup> marked_groups;

    for (const auto& record : batch) {
      const auto write_id = record.doc_ht.write_id();
      switch (record.kind) {
        case RecordKind::kTombstone:
          events.emplace_back(write_id, LogicalEvent{EventKind::kDelete, std::nullopt});
          continue;
        case RecordKind::kPackedInsert:
          events.emplace_back(write_id, LogicalEvent{EventKind::kInsert, record.identity});
          continue;
        case RecordKind::kPackedUpdate:
          events.emplace_back(write_id, LogicalEvent{EventKind::kUpdate, record.identity});
          continue;
        case RecordKind::kLivenessColumn: [[fallthrough]];
        case RecordKind::kTargetColumn:   [[fallthrough]];
        case RecordKind::kOtherColumn:
          break;
      }
      auto& group = write_id >= kBackfillWriteIdFloor ? marked_groups[write_id]
                                                      : unmarked_group;
      if (!group.has_records) {
        group.has_records = true;
        group.position = write_id;
      }
      if (record.kind == RecordKind::kLivenessColumn) {
        group.saw_liveness = true;
      } else if (record.kind == RecordKind::kTargetColumn) {
        if (group.identity && *group.identity != *record.identity) {
          Inconclusive("multiple distinct identities in one non-packed event group");
          return Status::OK();
        }
        group.identity = record.identity;
      }
    }

    const auto emit_group = [&events, this](const ColumnGroup& group) {
      if (!group.has_records) {
        return true;
      }
      if (group.saw_liveness) {
        if (!group.identity) {
          Inconclusive("insert-shaped event group without an identity column");
          return false;
        }
        events.emplace_back(group.position, LogicalEvent{EventKind::kInsert, group.identity});
      } else if (group.identity) {
        events.emplace_back(group.position, LogicalEvent{EventKind::kUpdate, group.identity});
      }
      return true;
    };
    if (!emit_group(unmarked_group)) {
      return Status::OK();
    }
    for (const auto& [_, group] : marked_groups) {
      if (!emit_group(group)) {
        return Status::OK();
      }
    }

    std::stable_sort(
        events.begin(), events.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [_, event] : events) {
      RETURN_NOT_OK(ApplyEvent(event));
      if (result_.outcome != UniqueIndexVerificationOutcome::kClean) {
        return Status::OK();
      }
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
      case EventKind::kNoOp:
        return Status::OK();
    }
    FATAL_INVALID_ENUM_VALUE(EventKind, event.kind);
  }

  // Decodes one physical record; returns nullopt for records outside the verification
  // window. Sets kInconclusive (and returns nullopt) for encodings the verifier cannot
  // interpret conclusively.
  Result<std::optional<PhysicalRecord>> DecodeRecord(
      Slice group_prefix, Slice key, Slice value) {
    dockv::SubDocKey subdoc_key;
    RETURN_NOT_OK(subdoc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kTrue));
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
    if (subkey.GetColumnId() != options_.ybidxbasectid_column_id) {
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
  // and unflagged V2 rows are inserts).
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
        record->identity =
            VERIFY_RESULT(dockv::UnpackPrimitiveValue(column_value, DataType::BINARY));
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
};

}  // namespace

std::string UniqueIndexVerificationResult::ToString() const {
  // resume_from_dockey is raw index key bytes -- the same contract as violating_group_prefix:
  // never in logs. Its size still tells whether (and roughly where) the scan stopped early.
  const auto resume_from_dockey_size = resume_from_dockey.size();
  return YB_STRUCT_TO_STRING(outcome, reason, dockey_groups_scanned, versions_scanned,
                             fallback_groups, resume_from_dockey_size);
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
