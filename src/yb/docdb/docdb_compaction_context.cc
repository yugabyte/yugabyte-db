// Copyright (c) YugaByte, Inc.
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

#include "yb/docdb/docdb_compaction_context.h"

#include <memory>

#include <glog/logging.h>

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/packed_row.h"
#include "yb/docdb/schema_packing.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/rocksdb/compaction_filter.h"

#include "yb/util/memory/arena.h"
#include "yb/util/fast_varint.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

using namespace std::literals;
using std::shared_ptr;
using std::unique_ptr;
using std::unordered_set;
using rocksdb::VectorToString;

DECLARE_bool(ycql_enable_packed_row);
DECLARE_bool(ysql_enable_packed_row);

DECLARE_uint64(ycql_packed_row_size_limit);
DECLARE_uint64(ysql_packed_row_size_limit);

namespace yb {
namespace docdb {

namespace {

struct OverwriteData {
  DocHybridTime doc_ht;
  Expiration expiration;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(doc_ht, expiration);
  }
};

Result<SchemaVersion> ParseValueHeader(Slice* value) {
  value->consume_byte(); // TODO(packed_row) control_fields
  return narrow_cast<SchemaVersion>(VERIFY_RESULT(util::FastDecodeUnsignedVarInt(value)));
}

// Interface to pass packed rows to underlying key value feed.
class PackedRowFeed {
 public:
  virtual Status FeedPackedRow(const Slice& key, const Slice& value, size_t doc_key_serial) = 0;

  virtual ~PackedRowFeed() = default;
};

class PackedRowData {
 public:
  PackedRowData(PackedRowFeed* feed, SchemaPackingProvider* provider,
                HybridTime history_cutoff)
      : feed_(*feed), schema_packing_provider_(provider), history_cutoff_(history_cutoff) {
  }

  bool active() const {
    return !key_.empty();
  }

  bool active_coprefix_dropped() const {
    return active_coprefix_dropped_;
  }

  bool can_start_packing() const {
    return can_start_packing_;
  }

  bool ColumnDeleted(ColumnId column_id) const {
    return new_packing_.deleted_cols.count(column_id) != 0;
  }

  Status UpdateMeta(rocksdb::FileMetaData* meta) {
    if (!meta->smallest.user_frontier || !meta->largest.user_frontier) {
      // Relevant in tests only.
      return Status::OK();
    }
    auto& smallest = down_cast<ConsensusFrontier&>(*meta->smallest.user_frontier);
    smallest.ResetSchemaVersion();
    auto& largest = down_cast<ConsensusFrontier&>(*meta->largest.user_frontier);
    largest.ResetSchemaVersion();
    for (const auto& p : used_schema_versions_) {
      smallest.AddSchemaVersion(p.first, p.second.first);
      largest.AddSchemaVersion(p.first, p.second.second);
    }
    return Status::OK();
  }

  // Handle packed row that was forwarded to underlying feed w/o changes.
  Status ProcessForwardedPackedRow(Slice value) {
    UsedSchemaVersion(VERIFY_RESULT(ParseValueHeader(&value)));
    return Status::OK();
  }

  Status ProcessPackedRow(
      const Slice& internal_key, size_t doc_key_size,
      const ValueControlFields& row_control_fields, const Slice& value,
      const DocHybridTime& row_doc_ht, size_t new_doc_key_serial) {
    VLOG_WITH_FUNC(4) << "Key: " << internal_key.ToDebugHexString() << ", value: "
                      << value.ToDebugHexString() << ", row_doc_ht: " << row_doc_ht;
    RSTATUS_DCHECK(!active(), Corruption, Format(
        "Double packed rows: $0, $1", key_.AsSlice().ToDebugHexString(),
        internal_key.ToDebugHexString()));

    UsedSchemaVersion(new_packing_.schema_version);

    InitKey(internal_key, doc_key_size, new_doc_key_serial);
    control_fields_ = row_control_fields;
    doc_ht_ = row_doc_ht;

    old_value_.Assign(value);
    old_value_slice_ = old_value_.AsSlice();
    old_schema_version_ = VERIFY_RESULT(ParseValueHeader(&old_value_slice_));
    if (old_schema_version_ != new_packing_.schema_version) {
      return StartRepacking();
    }
    packing_started_ = false;
    return Status::OK();
  }

  void StartPacking(
      const Slice& internal_key, size_t doc_key_size, const DocHybridTime& doc_ht,
      size_t new_doc_key_serial) {
    UsedSchemaVersion(new_packing_.schema_version);

    InitKey(internal_key, doc_key_size, new_doc_key_serial);
    control_fields_ = ValueControlFields();
    doc_ht_ = doc_ht;
    old_value_slice_ = Slice();
    InitPacker();
  }

  void InitKey(
      const Slice& internal_key, size_t doc_key_size, size_t new_doc_key_serial) {
    doc_key_serial_ = new_doc_key_serial;
    key_.Assign(internal_key.cdata(), doc_key_size);
    memcpy(
        last_internal_component_, internal_key.end() - rocksdb::kLastInternalComponentSize,
        sizeof(last_internal_component_));
  }

  // Returns true if column was processed. Otherwise caller should handle this column.
  Result<bool> ProcessColumn(
      ColumnId column_id, const Slice& value, const DocHybridTime& column_doc_ht) {
    if (!packing_started_) {
      RETURN_NOT_OK(StartRepacking());
    }
    auto next_column_id = packer_->NextColumnId();
    VLOG(4) << "Next column id: " << next_column_id << ", current column id: " << column_id;
    while (next_column_id < column_id) {
      RETURN_NOT_OK(PackOldValue(next_column_id));
      next_column_id = packer_->NextColumnId();
    }
    if (next_column_id > column_id) {
      if (new_packing_.schema_packing->SkippedColumn(column_id)) {
        VLOG(4) << "Collection column: " << column_id;
        return false;
      }

      VLOG(4) << "Deleted column from schema: " << next_column_id << ", " << column_id;
      // Column was deleted.
      return true;
    }

    size_t tail_size = 0; // As usual, when not specified size is in bytes.
    if (!old_value_slice_.empty()) {
      auto old_value = old_packing_.schema_packing->GetValue(column_id, old_value_slice_);
      if (old_value) {
        tail_size = old_value_slice_.end() - old_value->end();
      }
    }

    if (!value.empty() && value[0] == ValueEntryTypeAsChar::kTombstone &&
        new_packing_.table_type == TableType::PGSQL_TABLE_TYPE) {
      // In a YSQL table, a tombstone for a specific column could be added only during PITR,
      // and we should just ignore all column updates for it.
      // Do not forget that we see only most recent entry to specified key.
      return true;
    }
    // TODO(packed_row) update control fields
    VLOG(4) << "Update value: " << column_id << ", " << value.ToDebugHexString() << ", tail size: "
            << tail_size;
    return packer_->AddValue(column_id, value, tail_size);
  }

  Status StartRepacking() {
    if (old_schema_version_ != old_packing_.schema_version) {
      old_packing_ = VERIFY_RESULT(schema_packing_provider_->CotablePacking(
          new_packing_.cotable_id, old_schema_version_, history_cutoff_));
    }
    InitPacker();
    return Status::OK();
  }

  void InitPacker() {
    packing_started_ = true;
    if (!packer_) {
      packer_.emplace(
          new_packing_.schema_version, *new_packing_.schema_packing, new_packing_.pack_limit());
    } else {
      packer_->Restart();
    }
  }

  Status Flush() {
    if (!active()) {
      return Status::OK();
    }
    key_.PushBack(KeyEntryTypeAsChar::kHybridTime);
    doc_ht_.AppendEncodedInDocDbFormat(&key_);
    key_.Append(last_internal_component_, sizeof(last_internal_component_));

    Slice value_slice;

    VLOG_WITH_FUNC(4)
        << "Has packer: " << (packer_ && !packer_->Empty()) << ", packing_started: "
        << packing_started_;
    if (packing_started_) {
      // TODO(packed_row) control_fields
      while (!packer_->Finished()) {
        RETURN_NOT_OK(PackOldValue(packer_->NextColumnId()));
      }
      value_slice = VERIFY_RESULT(packer_->Complete());
    } else {
      value_slice = old_value_.AsSlice();
    }
    VLOG_WITH_FUNC(4)
        << key_.AsSlice().ToDebugHexString() << " => " << value_slice.ToDebugHexString();
    RETURN_NOT_OK(feed_.FeedPackedRow(key_.AsSlice(), value_slice, doc_key_serial_));
    key_.Clear();
    return Status::OK();
  }

  Status PackOldValue(ColumnId column_id) {
    auto column_value = old_value_slice_.empty()
        ? std::optional<Slice>()
        : old_packing_.schema_packing->GetValue(column_id, old_value_slice_);
    if (!column_value) {
      const ColumnPackingData& column_data = VERIFY_RESULT(packer_->NextColumnData());
      RSTATUS_DCHECK(column_data.varlen(), Corruption, Format(
          "Don't have value for fixed size column: $0, in $1, schema_version: $2",
          column_id, old_value_slice_.ToDebugHexString(), old_packing_.schema_version));
      column_value = Slice();
    }
    VLOG(4) << "Keep value for column " << column_id << ": " << column_value->ToDebugHexString();
    // Use min ssize_t value to be sure that packing always succeed.
    constexpr auto kUnlimitedTail = std::numeric_limits<ssize_t>::min();
    auto result = VERIFY_RESULT(packer_->AddValue(column_id, *column_value, kUnlimitedTail));
    RSTATUS_DCHECK(result, Corruption, "Unable to pack old value for $0", column_id);
    return Status::OK();
  }

  void UsedSchemaVersion(SchemaVersion version) {
    if (used_schema_versions_it_ == used_schema_versions_.end()) {
      used_schema_versions_it_ = used_schema_versions_.emplace(
          new_packing_.cotable_id, std::make_pair(version, version)).first;
    } else {
      used_schema_versions_it_->second.first = std::min(
          used_schema_versions_it_->second.first, version);
      used_schema_versions_it_->second.second = std::max(
          used_schema_versions_it_->second.second, version);
    }
    old_packing_.schema_version = kLatestSchemaVersion;
    packer_.reset();
  }

  // Updates current coprefix. Coprefix is located at start of the key and identifies cotable or
  // colocation.
  Status UpdateCoprefix(const Slice& coprefix) {
    if (!schema_packing_provider_) {
      return Status::OK();
    }
    if (coprefix == active_coprefix_.AsSlice()) {
      return Status::OK();
    }
    RETURN_NOT_OK(Flush());

    auto packing = GetCompactionSchemaInfo(coprefix);
    if (!packing.ok()) {
      if (packing.status().IsNotFound()) {
        active_coprefix_ = coprefix;
        active_coprefix_dropped_ = true;
        return Status::OK();
      }
      return packing.status();
    }
    active_coprefix_ = coprefix;
    active_coprefix_dropped_ = false;
    new_packing_ = *packing;
    can_start_packing_ = packing->enabled();
    used_schema_versions_it_ = used_schema_versions_.find(new_packing_.cotable_id);
    return Status::OK();
  }

  Result<CompactionSchemaInfo> GetCompactionSchemaInfo(Slice coprefix) {
    if (coprefix.empty()) {
      return schema_packing_provider_->CotablePacking(
          Uuid::Nil(), kLatestSchemaVersion, history_cutoff_);
    } else if (coprefix.TryConsumeByte(KeyEntryTypeAsChar::kColocationId)) {
      if (coprefix.size() != sizeof(ColocationId)) {
        return STATUS_FORMAT(Corruption, "Wrong colocation size: $0", coprefix.ToDebugHexString());
      }
      uint32_t colocation_id = BigEndian::Load32(coprefix.data());
      return schema_packing_provider_->ColocationPacking(
          colocation_id, kLatestSchemaVersion, history_cutoff_);
    } else if (coprefix.TryConsumeByte(KeyEntryTypeAsChar::kTableId)) {
      auto cotable_id = VERIFY_RESULT(Uuid::FromComparable(coprefix));
      return schema_packing_provider_->CotablePacking(
          cotable_id, kLatestSchemaVersion, history_cutoff_);
    } else {
      return STATUS_FORMAT(Corruption, "Wrong coprefix: $0", coprefix.ToDebugHexString());
    }
  }

 private:
  PackedRowFeed& feed_;
  SchemaPackingProvider* schema_packing_provider_; // Owned externally.

  ValueControlFields control_fields_;
  size_t doc_key_serial_ = std::numeric_limits<size_t>::max();
  KeyBuffer key_;
  DocHybridTime doc_ht_;
  char last_internal_component_[rocksdb::kLastInternalComponentSize];

  // All old_ fields are releated to original row packing.
  // I.e. row state that had place before the compaction.
  ValueBuffer old_value_;
  Slice old_value_slice_;
  SchemaVersion old_schema_version_;
  CompactionSchemaInfo old_packing_;

  bool packing_started_ = false; // Whether we have started packing the row.

  // Use fake coprefix as default value.
  // So we will trigger table change on the first record.
  ByteBuffer<1 + kUuidSize> active_coprefix_{"FAKE_PREFIX"s};

  // True if the active coprefix is for a dropped table.
  bool active_coprefix_dropped_ = false;

  CompactionSchemaInfo new_packing_;
  boost::optional<RowPacker> packer_;
  bool can_start_packing_ = false; // Whether we could start packing row with current schema.

  HybridTime history_cutoff_;

  using UsedSchemaVersionsMap =
      std::unordered_map<Uuid, std::pair<SchemaVersion, SchemaVersion>, UuidHash>;

  // Schema version ranges for each found table.
  // That could be a surprise, but when we are talking about range and use pair to represent range
  // the first usually means min value of the range, while the second means max value of the range.
  // The range is inclusive.
  UsedSchemaVersionsMap used_schema_versions_;

  // Iterator into used_schema_versions for the active coprefix.
  UsedSchemaVersionsMap::iterator used_schema_versions_it_ = used_schema_versions_.end();
};

class DocDBCompactionFeed : public rocksdb::CompactionFeed, public PackedRowFeed {
 public:
  DocDBCompactionFeed(
      rocksdb::CompactionFeed* next_feed,
      HistoryRetentionDirective retention,
      HybridTime min_input_hybrid_time,
      HybridTime min_other_data_ht,
      rocksdb::BoundaryValuesExtractor* boundary_extractor,
      const KeyBounds* key_bounds,
      SchemaPackingProvider* schema_packing_provider)
      : next_feed_(*next_feed), retention_(std::move(retention)), key_bounds_(key_bounds),
        min_other_data_ht_(retention_.retain_delete_markers_in_major_compaction
            ? HybridTime::kMin : min_other_data_ht),
        could_change_key_range_(!CanHaveOtherDataBefore(min_input_hybrid_time)),
        boundary_extractor_(boundary_extractor),
        packed_row_(this, schema_packing_provider, retention_.history_cutoff) {
  }

  Status Feed(const Slice& internal_key, const Slice& value) override;

  Status UpdateMeta(rocksdb::FileMetaData* meta) {
    if (could_change_key_range_) {
      meta->smallest.user_values = smallest_;
      meta->largest.user_values = largest_;
    }
    return packed_row_.UpdateMeta(meta);
  }

  Status ForwardToNextFeed(const Slice& key, const Slice& value) {
    if (!packed_row_.active()) {
      return PassToNextFeed(key, value, doc_key_serial_);
    }

    auto* pending_row = static_cast<PendingEntry*>(pending_rows_arena_.AllocateBytesAligned(
        sizeof(PendingEntry) + key.size() + value.size(), alignof(PendingEntry)));
    pending_row->next = nullptr;
    pending_row->doc_key_serial = doc_key_serial_;
    pending_row->key_size = key.size();
    pending_row->value_size = value.size();
    *last_pending_row_next_ = pending_row;
    last_pending_row_next_ = &pending_row->next;
    char* data = pointer_cast<char*>(pending_row) + sizeof(PendingEntry);
    memcpy(data, key.data(), key.size());
    data += key.size();
    memcpy(data, value.data(), value.size());
    return Status::OK();
  }

  Status FeedPackedRow(const Slice& key, const Slice& value, size_t doc_key_serial) override {
    RETURN_NOT_OK(PassToNextFeed(key, value, doc_key_serial));

    auto* pending_row = first_pending_row_;
    if (!pending_row) {
      return Status::OK();
    }

    while (pending_row) {
      char* data = pointer_cast<char*>(pending_row) + sizeof(PendingEntry);
      Slice pending_key(data, pending_row->key_size);
      data += pending_row->key_size;
      Slice pending_value(data, pending_row->value_size);
      RETURN_NOT_OK(PassToNextFeed(pending_key, pending_value, pending_row->doc_key_serial));
      pending_row = pending_row->next;
    }

    pending_rows_arena_.Reset();
    first_pending_row_ = nullptr;
    last_pending_row_next_ = &first_pending_row_;
    return Status::OK();
  }

  Status Flush() override {
    RETURN_NOT_OK(packed_row_.Flush());
    if (first_pending_row_) {
      return STATUS(IllegalState, "Have pending rows after packed row flush");
    }
    return next_feed_.Flush();
  }

 private:
  // Assigns prev_subdoc_key_ from memory addressed by data. The length of key is taken from
  // sub_key_ends_ and same_bytes are reused.
  void AssignPrevSubDocKey(const char* data, size_t same_bytes);

  Status PassToNextFeed(const Slice& key, const Slice& value, size_t doc_key_serial) {
    if (last_passed_doc_key_serial_ != doc_key_serial) {
      RSTATUS_DCHECK_GT(doc_key_serial, last_passed_doc_key_serial_, InternalError,
                        Format("Doc key serial stream failure for key $0", key.ToDebugHexString()));
      RETURN_NOT_OK(UpdateBoundaryValues(key));
      last_passed_doc_key_serial_ = doc_key_serial;
    }
    return next_feed_.Feed(key, value);
  }

  Status UpdateBoundaryValues(const Slice& key) {
    if (!could_change_key_range_) {
      return Status::OK();
    }
    user_values_.clear();
    RETURN_NOT_OK(boundary_extractor_->Extract(rocksdb::ExtractUserKey(key), &user_values_));
    rocksdb::UpdateUserValues(user_values_, rocksdb::UpdateUserValueType::kSmallest, &smallest_);
    rocksdb::UpdateUserValues(user_values_, rocksdb::UpdateUserValueType::kLargest, &largest_);
    return Status::OK();
  }

  bool CanHaveOtherDataBefore(HybridTime ht) const {
    return ht >= min_other_data_ht_;
  }

  rocksdb::CompactionFeed& next_feed_;
  const HistoryRetentionDirective retention_;
  const KeyBounds* key_bounds_;
  const HybridTime min_other_data_ht_;
  const bool could_change_key_range_;
  rocksdb::BoundaryValuesExtractor* boundary_extractor_;
  ValueBuffer new_value_buffer_;

  std::vector<char> prev_subdoc_key_;

  // Result of DecodeDocKeyAndSubKeyEnds for prev_subdoc_key_.
  boost::container::small_vector<size_t, 16> sub_key_ends_;

  size_t last_passed_doc_key_serial_ = 0;
  // Serial number of doc key in processed key stream.
  size_t doc_key_serial_ = 0;
  boost::container::small_vector<rocksdb::UserBoundaryValueRef, 0x10> user_values_;
  boost::container::small_vector<rocksdb::UserBoundaryValue, 0x10> smallest_;
  boost::container::small_vector<rocksdb::UserBoundaryValue, 0x10> largest_;

  // A stack of highest hybrid_times lower than or equal to history_cutoff_ at which parent
  // subdocuments of the key that has just been processed, or the subdocument / primitive value
  // itself stored at that key itself, were fully overwritten or deleted. A full overwrite of a
  // parent document is considered a full overwrite of all its subdocuments at every level for the
  // purpose of this definition. Therefore, the following inequalities hold:
  //
  // overwrite_ht_[0] <= ... <= overwrite_ht_[N - 1] <= history_cutoff_
  //
  // The following example shows contents of RocksDB being compacted, as well as the state of the
  // overwrite_ht_ stack and how it is being updated at each step. history_cutoff_ is 25 in this
  // example.
  //
  // RocksDB key/value                    | overwrite_ht_ | Feed logic
  // ----------------------------------------------------------------------------------------------
  // doc_key1 HT(30) -> {}                | [MinHT]       | Always keeping the first entry
  // doc_key1 HT(20) -> DEL               | [20]          | 20 >= MinHT, keeping the entry
  //                                      |               | ^^    ^^^^^
  //                                      | Note: we're comparing the hybrid_time in this key with
  //                                      | the previous stack top of overwrite_ht_.
  //                                      |               |
  // doc_key1 HT(10) -> {}                | [20]          | 10 < 20, deleting the entry
  // doc_key1 subkey1 HT(35) -> "value4"  | [20, 20]      | 35 >= 20, keeping the entry
  //                     ^^               |      ^^       |
  //                      \----------------------/ HT(35) is higher than history_cutoff_, so we're
  //                                      |        just duplicating the top value on the stack
  //                                      |        HT(20) this step.
  //                                      |               |
  // doc_key1 subkey1 HT(23) -> "value3"  | [20, 23]      | 23 >= 20, keeping the entry
  //                                      |      ^^       |
  //                                      |      Now we have actually found a hybrid_time that is
  //                                      |      <= history_cutoff_, so we're replacing the stack
  //                                      |      top with that hybrid_time.
  //                                      |               |
  // doc_key1 subkey1 HT(21) -> "value2"  | [20, 23]      | 21 < 23, deleting the entry
  // doc_key1 subkey1 HT(15) -> "value1"  | [20, 23]      | 15 < 23, deleting the entry

  std::vector<OverwriteData> overwrite_;

  // We use this to only log a message that the feed is being used once on the first call to
  // the Filter function.
  bool feed_usage_logged_ = false;
  bool within_merge_block_ = false;

  PackedRowData packed_row_;
  Arena pending_rows_arena_;

  struct PendingEntry {
    PendingEntry* next;
    size_t doc_key_serial; // Serial number of stored doc key.
    size_t key_size;
    size_t value_size;
  };

  PendingEntry* first_pending_row_ = nullptr;
  PendingEntry** last_pending_row_next_ = &first_pending_row_;
};

// ------------------------------------------------------------------------------------------------

Status DocDBCompactionFeed::Feed(const Slice& internal_key, const Slice& value) {
  const HybridTime history_cutoff = retention_.history_cutoff;

  if (!feed_usage_logged_) {
    // TODO: switch this to VLOG if it becomes too chatty.
    LOG(INFO) << "DocDB compaction feed, min_other_data_ht: "
              << min_other_data_ht_
              << ", history_cutoff=" << history_cutoff;
    feed_usage_logged_ = true;
  }

  auto key = internal_key.WithoutSuffix(rocksdb::kLastInternalComponentSize);

  VLOG(4) << "Feed: " << internal_key.ToDebugHexString() << "/"
          << SubDocKey::DebugSliceToString(key) << " => " << value.ToDebugHexString();

  if (!IsWithinBounds(key_bounds_, key) &&
      DecodeKeyEntryType(key) != KeyEntryType::kTransactionApplyState) {
    // If we reach this point, then we're processing a record which should have been excluded by
    // proper use of GetLiveRanges(). We include this as a sanity check, but we should never get
    // here.
    LOG(DFATAL) << "Unexpectedly filtered out-of-bounds key during compaction: "
        << SubDocKey::DebugSliceToString(key)
        << " with bounds: " << key_bounds_->ToString();
    return Status::OK();
  }

  // Just remove intent records from regular DB, because it was beta feature.
  // Currently intents are stored in separate DB.
  if (DecodeKeyEntryType(key) == KeyEntryType::kObsoleteIntentPrefix) {
    return Status::OK();
  }

  auto same_bytes = strings::MemoryDifferencePos(
      key.data(), prev_subdoc_key_.data(), std::min(key.size(), prev_subdoc_key_.size()));

  // The number of initial components (including cotable_id, document key and subkeys) that this
  // SubDocKey shares with previous one. This does not care about the hybrid_time field.
  size_t num_shared_components;
  if (!same_bytes) {
    // There is special case, when we move from key w/o cotable to key with cotable.
    // sub_key_ends_[0] will be 0, and logic below would never update it.
    num_shared_components = 0;
  } else {
    num_shared_components = sub_key_ends_.size();
    VLOG_WITH_FUNC(4) << "Old sub_key_ends: " << AsString(sub_key_ends_) << ", same bytes: "
                      << same_bytes;
    while (num_shared_components > 0 && sub_key_ends_[num_shared_components - 1] > same_bytes) {
      --num_shared_components;
    }
  }

  VLOG_WITH_FUNC(4) << "num_shared_components: " << num_shared_components << ", overwrite: "
                    << AsString(overwrite_);
  // First component is cotable and second component doc_key, so num_shared_components <= 1 means
  // new row.
  if (num_shared_components <= 1) {
    VLOG_WITH_FUNC(4) << "Flush on num_shared_components: " << num_shared_components;
    RETURN_NOT_OK(packed_row_.Flush());
    ++doc_key_serial_;
  }

  sub_key_ends_.resize(num_shared_components);

  RETURN_NOT_OK(SubDocKey::DecodeDocKeyAndSubKeyEnds(key, &sub_key_ends_));
  RETURN_NOT_OK(packed_row_.UpdateCoprefix(key.Prefix(sub_key_ends_[0])));

  if (packed_row_.active_coprefix_dropped()) {
    return Status::OK();
  }

  const size_t new_stack_size = sub_key_ends_.size();

  // Remove overwrite hybrid_times for components that are no longer relevant for the current
  // SubDocKey.
  overwrite_.resize(std::min(overwrite_.size(), num_shared_components));
  auto ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(key));
  // We're comparing the hybrid time in this key with the stack top of overwrite_ht_ after
  // truncating the stack to the number of components in the common prefix of previous and current
  // key.
  //
  // Example (history_cutoff_ = 12):
  // --------------------------------------------------------------------------------------------
  // Key          overwrite_ht_ stack and relevant notes
  // --------------------------------------------------------------------------------------------
  // k1 T10       [MinHT]
  //
  // k1 T5        [T10]
  //
  // k1 col1 T11  [T10, T11]
  //
  // k1 col1 T7   The stack does not get truncated (shared prefix length is 2), so
  //              prev_overwrite_ht = 11. Removing this entry because 7 < 11.
  //              The stack stays at [T10, T11].
  //
  // k1 col2 T9   Truncating the stack to [T10], setting prev_overwrite_ht to 10, and therefore
  //              deciding to remove this entry because 9 < 10.
  //
  const DocHybridTime prev_overwrite_ht =
      overwrite_.empty() ? DocHybridTime::kMin : overwrite_.back().doc_ht;
  const Expiration prev_exp =
      overwrite_.empty() ? Expiration() : overwrite_.back().expiration;

  // We only keep entries with hybrid_time equal to or later than the latest time the subdocument
  // was fully overwritten or deleted prior to or at the history cutoff time. The intuition is that
  // key/value pairs that were overwritten at or before history cutoff time will not be visible at
  // history cutoff time or any later time anyway.
  //
  // Furthermore, we only need to update the overwrite hybrid_time stack in case we have decided to
  // keep the new entry. Otherwise, the current entry's hybrid time ht is less than the previous
  // overwrite hybrid_time prev_overwrite_ht, and therefore it does not provide any new information
  // about key/value pairs that follow being overwritten at a particular hybrid time. Another way to
  // explain this is to look at the logic that follows. If we don't early-exit here while ht is less
  // than prev_overwrite_ht, we'll end up adding more prev_overwrite_ht values to the overwrite
  // hybrid_time stack, and we might as well do that while handling the next key/value pair that
  // does not get cleaned up the same way as this one.
  //
  // TODO: When more merge records are supported, isTtlRow should be redefined appropriately.
  bool is_ttl_row = IsMergeRecord(value);
  VLOG_WITH_FUNC(4) << "Ht: " << ht << ", prev_overwrite_ht: " << prev_overwrite_ht;
  if (ht < prev_overwrite_ht && !is_ttl_row) {
    return Status::OK();
  }

  // Every subdocument was fully overwritten at least at the time any of its parents was fully
  // overwritten.
  if (overwrite_.size() < new_stack_size - 1) {
    overwrite_.resize(new_stack_size - 1, {prev_overwrite_ht, prev_exp});
  }

  Expiration popped_exp = overwrite_.empty() ? Expiration() : overwrite_.back().expiration;
  // This will happen in case previous key has the same document key and subkeys as the current
  // key, and the only difference is in the hybrid_time. We want to replace the hybrid_time at the
  // top of the overwrite_ht stack in this case.
  if (overwrite_.size() == new_stack_size) {
    overwrite_.pop_back();
  }

  // Check whether current key is the same as the previous key, except for the timestamp.
  if (same_bytes != sub_key_ends_.back()) {
    within_merge_block_ = false;
  }

  // See if we found a higher hybrid time not exceeding the history cutoff hybrid time at which the
  // subdocument (including a primitive value) rooted at the current key was fully overwritten.
  // In case of ht > history_cutoff_, we just keep the parent document's highest known overwrite
  // hybrid time that does not exceed the cutoff hybrid time. In that case this entry is obviously
  // too new to be garbage-collected.
  if (ht.hybrid_time() > history_cutoff) {
    AssignPrevSubDocKey(key.cdata(), same_bytes);
    overwrite_.push_back({prev_overwrite_ht, prev_exp});
    VLOG_WITH_FUNC(4)
        << "Feed to next because of history cutoff: " << ht.hybrid_time() << ", " << history_cutoff;
    auto value_slice = value;
    RETURN_NOT_OK(ValueControlFields::Decode(&value_slice));
    if (DecodeValueEntryType(value_slice) == ValueEntryType::kPackedRow) {
      // Check packed row version for rows left untouched.
      RETURN_NOT_OK(packed_row_.ProcessForwardedPackedRow(value_slice));
    }
    return ForwardToNextFeed(internal_key, value);
  }

  Slice value_slice = value;
  ValueControlFields control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value_slice));

  // Check for columns deleted from the schema. This is done regardless of whether this is a
  // major or minor compaction.
  //
  // TODO: could there be a case when there is still a read request running that uses an old schema,
  //       and we end up removing some data that the client expects to see?
  VLOG(4) << "Sub key ends: " << AsString(sub_key_ends_);
  if (sub_key_ends_.size() > 1) {
    // 0 - end of cotable id section.
    // 1 - end of doc key section.
    // Column ID is the first subkey in every row.
    auto doc_key_size = sub_key_ends_[1];
    auto key_type = DecodeKeyEntryType(key[doc_key_size]);
    VLOG(4) << "First subkey type: " << key_type;
    if (key_type == KeyEntryType::kColumnId || key_type == KeyEntryType::kSystemColumnId) {
      Slice column_id_slice = key.WithoutPrefix(doc_key_size + 1);
      auto column_id_as_int64 = VERIFY_RESULT(util::FastDecodeSignedVarIntUnsafe(&column_id_slice));
      ColumnId column_id;
      RETURN_NOT_OK(ColumnId::FromInt64(column_id_as_int64, &column_id));

      if (packed_row_.ColumnDeleted(column_id)) {
        return Status::OK();
      }

      VLOG(4) << "Packed row active: " << packed_row_.active();
      // TODO(packed_row) remove control fields from value
      if (!packed_row_.active() && packed_row_.can_start_packing() &&
          !CanHaveOtherDataBefore(ht.hybrid_time())) {
        packed_row_.StartPacking(internal_key, doc_key_size, ht, doc_key_serial_);
        AssignPrevSubDocKey(key.cdata(), same_bytes);
      }
      if (packed_row_.active()) {
        if (key_type == KeyEntryType::kSystemColumnId &&
            column_id == KeyEntryValue::kLivenessColumn.GetColumnId()) {
          return Status::OK();
        }
        // Return if column was processed by packed row.
        if (VERIFY_RESULT(packed_row_.ProcessColumn(column_id, value_slice, ht))) {
          return Status::OK();
        }
      }
    }
  }

  auto overwrite_ht = is_ttl_row ? prev_overwrite_ht : std::max(prev_overwrite_ht, ht);

  const auto value_type = static_cast<ValueEntryType>(
      value_slice.FirstByteOr(ValueEntryTypeAsChar::kInvalid));

  const Expiration curr_exp(ht.hybrid_time(), control_fields.ttl);

  // If within the merge block.
  //     If the row is a TTL row, delete it.
  //     Otherwise, replace it with the cached TTL (i.e., apply merge).
  // Otherwise,
  //     If this is a TTL row, cache TTL (start merge block).
  //     If normal row, compute its ttl and continue.

  Expiration expiration;
  if (within_merge_block_) {
    expiration = popped_exp;
  } else if (ht.hybrid_time() >= prev_exp.write_ht &&
             (curr_exp.ttl != ValueControlFields::kMaxTtl || is_ttl_row)) {
    expiration = curr_exp;
  } else {
    expiration = prev_exp;
  }

  overwrite_.push_back({overwrite_ht, expiration});

  if (overwrite_.size() != new_stack_size) {
    return STATUS_FORMAT(Corruption, "Overwrite size does not match new_stack_size: $0 vs $1",
                         overwrite_.size(), new_stack_size);
  }
  AssignPrevSubDocKey(key.cdata(), same_bytes);

  // If we are backfilling an index table, we want to preserve the delete markers in the table
  // until the backfill process is completed. For other normal use cases, delete markers/tombstones
  // can be cleaned up on a major compaction.
  // retention_.retain_delete_markers_in_major_compaction will be set to true until the index
  // backfill is complete.
  //
  // Tombstones at or below the history cutoff hybrid_time can always be cleaned up on full (major)
  // compactions. However, we do need to update the overwrite hybrid time stack in this case (as we
  // just did), because this deletion (tombstone) entry might be the only reason for cleaning up
  // more entries appearing at earlier hybrid times.
  if (value_type == ValueEntryType::kTombstone &&
      !CanHaveOtherDataBefore(ht.hybrid_time())) {
    return Status::OK();
  }

  // TODO(packed_row) combine non packed columns into packed row
  if (value_type == ValueEntryType::kPackedRow) {
    return packed_row_.ProcessPackedRow(
        internal_key, sub_key_ends_.back(), control_fields, value_slice, ht, doc_key_serial_);
  }

  // If the entry has the TTL flag, delete the entry.
  if (is_ttl_row) {
    within_merge_block_ = true;
    return Status::OK();
  }

  // Only check for expiration if the current hybrid time is at or below history cutoff.
  // The key could not have possibly expired by history_cutoff_ otherwise.
  MonoDelta true_ttl = ComputeTTL(expiration.ttl, retention_.table_ttl);
  const auto has_expired = HasExpiredTTL(
      true_ttl == expiration.ttl ? expiration.write_ht : ht.hybrid_time(),
      true_ttl,
      history_cutoff);
  // As of 02/2017, we don't have init markers for top level documents in QL. As a result, we can
  // compact away each column if it has expired, including the liveness system column. The init
  // markers in Redis wouldn't be affected since they don't have any TTL associated with them and
  // the TTL would default to kMaxTtl which would make has_expired false.
  Slice new_value = value;
  if (has_expired) {
    // This is consistent with the condition we're testing for deletes at the bottom of the function
    // because ht_at_or_below_cutoff is implied by has_expired.
    if (!CanHaveOtherDataBefore(ht.hybrid_time())) {
      return Status::OK();
    }

    // During minor compactions, expired values are written back as tombstones because removing the
    // record might expose earlier values which would be incorrect.
    new_value = Value::EncodedTombstone();
  } else if (within_merge_block_) {
    if (expiration.ttl != ValueControlFields::kMaxTtl) {
      expiration.ttl += MonoDelta::FromMicroseconds(
          overwrite_.back().expiration.write_ht.PhysicalDiff(ht.hybrid_time()));
      overwrite_.back().expiration.ttl = expiration.ttl;
    }

    control_fields.ttl = expiration.ttl;
    new_value_buffer_.Clear();

    // We are reusing the existing encoded value without decoding/encoding it.
    control_fields.AppendEncoded(&new_value_buffer_);
    new_value_buffer_.Append(value_slice);
    new_value = new_value_buffer_.AsSlice();
    within_merge_block_ = false;
  } else if (control_fields.intent_doc_ht.is_valid()) {
    // Cleanup intent doc hybrid time when we don't need it anymore.
    // See https://github.com/yugabyte/yugabyte-db/issues/4535 for details.
    control_fields.intent_doc_ht = DocHybridTime::kInvalid;

    new_value_buffer_.Clear();

    // We are reusing the existing encoded value without decoding/encoding it.
    control_fields.AppendEncoded(&new_value_buffer_);
    new_value_buffer_.Append(value_slice);
    new_value = new_value_buffer_.AsSlice();
  }

  VLOG_WITH_FUNC(4) << "Feed next at the end";
  return ForwardToNextFeed(internal_key, new_value);
}

void DocDBCompactionFeed::AssignPrevSubDocKey(
    const char* data, size_t same_bytes) {
  size_t size = sub_key_ends_.back();
  prev_subdoc_key_.resize(size);
  memcpy(prev_subdoc_key_.data() + same_bytes, data + same_bytes, size - same_bytes);
}

// DocDB compaction feed. A new instance of this class is created for every compaction.
class DocDBCompactionContext : public rocksdb::CompactionContext {
 public:
  DocDBCompactionContext(
      rocksdb::CompactionFeed* next_feed,
      HistoryRetentionDirective retention,
      HybridTime min_input_hybrid_time,
      HybridTime min_other_data_ht,
      rocksdb::BoundaryValuesExtractor* boundary_extractor,
      const KeyBounds* key_bounds,
      SchemaPackingProvider* schema_packing_provider);

  ~DocDBCompactionContext() = default;

  rocksdb::CompactionFeed* Feed() override {
    return feed_.get();
  }

  // This indicates we don't have a cached TTL. We need this to be different from kMaxTtl
  // and kResetTtl because a PERSIST call would lead to a cached TTL of kMaxTtl, and kResetTtl
  // indicates no TTL in Cassandra.
  const MonoDelta kNoTtl = MonoDelta::FromNanoseconds(-1);

  // This is used to provide the history_cutoff timestamp to the compaction as a field in the
  // ConsensusFrontier, so that it can be persisted in RocksDB metadata and recovered on bootstrap.
  rocksdb::UserFrontierPtr GetLargestUserFrontier() const override;

  // Returns an empty list when key_ranges_ is not set, denoting that the whole key range of the
  // tablet should be considered live.
  //
  // When key_ranges_ is set, returns two live ranges:
  // (1) A range covering any ApplyTransactionState records which may have been written
  // (2) A range covering all valid keys in key_ranges_, i.e. all user data this tablet is
  //     responsible for.
  std::vector<std::pair<Slice, Slice>> GetLiveRanges() const override;

  Status UpdateMeta(rocksdb::FileMetaData* meta) override {
    return feed_->UpdateMeta(meta);
  }

 private:
  HybridTime history_cutoff_;
  const KeyBounds* key_bounds_;
  std::unique_ptr<DocDBCompactionFeed> feed_;
};

DocDBCompactionContext::DocDBCompactionContext(
    rocksdb::CompactionFeed* next_feed,
    HistoryRetentionDirective retention,
    HybridTime min_input_hybrid_time,
    HybridTime min_other_data_ht,
    rocksdb::BoundaryValuesExtractor* boundary_extractor,
    const KeyBounds* key_bounds,
    SchemaPackingProvider* schema_packing_provider)
    : history_cutoff_(retention.history_cutoff),
      key_bounds_(key_bounds),
      feed_(std::make_unique<DocDBCompactionFeed>(
          next_feed, std::move(retention), min_input_hybrid_time, min_other_data_ht,
          boundary_extractor, key_bounds, schema_packing_provider)) {
}

rocksdb::UserFrontierPtr DocDBCompactionContext::GetLargestUserFrontier() const {
  auto* consensus_frontier = new ConsensusFrontier();
  consensus_frontier->set_history_cutoff(history_cutoff_);
  return rocksdb::UserFrontierPtr(consensus_frontier);
}

std::vector<std::pair<Slice, Slice>> DocDBCompactionContext::GetLiveRanges() const {
  static constexpr char kApplyStateEndChar = KeyEntryTypeAsChar::kTransactionApplyState + 1;
  if (!key_bounds_ || (key_bounds_->lower.empty() && key_bounds_->upper.empty())) {
    return {};
  }
  auto end_apply_state_region = Slice(&kApplyStateEndChar, 1);
  auto first_range = std::make_pair(Slice(), end_apply_state_region);
  auto second_range = std::make_pair(
    key_bounds_->lower.AsSlice().Less(end_apply_state_region)
        ? end_apply_state_region
        : key_bounds_->lower.AsSlice(),
    key_bounds_->upper.AsSlice());

  return {first_range, second_range};
}

HybridTime MinHybridTime(const std::vector<rocksdb::FileMetaData*>& inputs) {
  auto result = HybridTime::kMax;
  for (const auto& file : inputs) {
    if (!file->smallest.user_frontier) {
      continue;
    }
    auto smallest = down_cast<ConsensusFrontier&>(*file->smallest.user_frontier);
    // Hybrid time is defined by Raft hybrid time and commit hybrid time of all records.
    result = std::min(result, smallest.hybrid_time());
  }
  return result;
}

} // namespace

bool CompactionSchemaInfo::enabled() const {
  switch (table_type) {
    case TableType::YQL_TABLE_TYPE:
      return FLAGS_ycql_enable_packed_row;
    case TableType::PGSQL_TABLE_TYPE:
      return FLAGS_ysql_enable_packed_row;
    case TableType::REDIS_TABLE_TYPE: [[fallthrough]];
    case TableType::TRANSACTION_STATUS_TABLE_TYPE:
      return false;
  }
  FATAL_INVALID_ENUM_VALUE(TableType, table_type);
}

size_t CompactionSchemaInfo::pack_limit() const {
  switch (table_type) {
    case TableType::YQL_TABLE_TYPE:
      return FLAGS_ycql_packed_row_size_limit;
    case TableType::PGSQL_TABLE_TYPE:
      return FLAGS_ysql_packed_row_size_limit;
    case TableType::REDIS_TABLE_TYPE: [[fallthrough]];
    case TableType::TRANSACTION_STATUS_TABLE_TYPE:
      return false;
  }
  FATAL_INVALID_ENUM_VALUE(TableType, table_type);
}

// ------------------------------------------------------------------------------------------------

std::shared_ptr<rocksdb::CompactionContextFactory> CreateCompactionContextFactory(
    std::shared_ptr<HistoryRetentionPolicy> retention_policy,
    const KeyBounds* key_bounds,
    const DeleteMarkerRetentionTimeProvider& delete_marker_retention_provider,
    SchemaPackingProvider* schema_packing_provider) {
  return std::make_shared<rocksdb::CompactionContextFactory>(
      [retention_policy, key_bounds, delete_marker_retention_provider, schema_packing_provider](
      rocksdb::CompactionFeed* next_feed, const rocksdb::CompactionContextOptions& options) {
    return std::make_unique<DocDBCompactionContext>(
        next_feed,
        retention_policy->GetRetentionDirective(),
        MinHybridTime(options.level0_inputs),
        delete_marker_retention_provider
            ? delete_marker_retention_provider(options.level0_inputs)
            : HybridTime::kMax,
        options.boundary_extractor,
        key_bounds,
        schema_packing_provider);
  });
}

// ------------------------------------------------------------------------------------------------

HistoryRetentionDirective ManualHistoryRetentionPolicy::GetRetentionDirective() {
  return {history_cutoff_.load(std::memory_order_acquire),
          table_ttl_.load(std::memory_order_acquire),
          ShouldRetainDeleteMarkersInMajorCompaction::kFalse};
}

void ManualHistoryRetentionPolicy::SetHistoryCutoff(HybridTime history_cutoff) {
  history_cutoff_.store(history_cutoff, std::memory_order_release);
}

void ManualHistoryRetentionPolicy::SetTableTTLForTests(MonoDelta ttl) {
  table_ttl_.store(ttl, std::memory_order_release);
}

}  // namespace docdb
}  // namespace yb
