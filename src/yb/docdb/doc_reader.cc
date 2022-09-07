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

#include "yb/docdb/doc_reader.h"

#include <string>
#include <vector>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/schema_packing.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/util/fast_varint.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

using std::vector;

using yb::HybridTime;

namespace yb {
namespace docdb {

namespace {

constexpr int64_t kNothingFound = -1;

YB_STRONGLY_TYPED_BOOL(CheckExistOnly);

// Shared information about packed row. I.e. common for all columns in this row.
struct PackedRowData {
  DocHybridTime doc_ht;
  ValueControlFields control_fields;
};

struct PackedColumnData {
  const PackedRowData* row = nullptr;
  Slice encoded_value;

  explicit operator bool() const {
    return row != nullptr;
  }
};

Expiration GetNewExpiration(
    const Expiration& parent_exp, const MonoDelta& ttl,
    const DocHybridTime& new_write_time) {
  Expiration new_exp = parent_exp;
  // We may need to update the TTL in individual columns.
  if (new_write_time.hybrid_time() >= new_exp.write_ht) {
    // We want to keep the default TTL otherwise.
    if (ttl != ValueControlFields::kMaxTtl) {
      new_exp.write_ht = new_write_time.hybrid_time();
      new_exp.ttl = ttl;
    } else if (new_exp.ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    }
  }

  // If the hybrid time is kMin, then we must be using default TTL.
  if (new_exp.write_ht == HybridTime::kMin) {
    new_exp.write_ht = new_write_time.hybrid_time();
  }

  return new_exp;
}

int64_t GetTtlRemainingSeconds(
    HybridTime read_time, HybridTime ttl_write_time, const Expiration& expiration) {
  if (!expiration) {
    return -1;
  }

  int64_t expiration_time_us =
      ttl_write_time.GetPhysicalValueMicros() + expiration.ttl.ToMicroseconds();
  int64_t remaining_us = expiration_time_us - read_time.GetPhysicalValueMicros();
  if (remaining_us <= 0) {
    return 0;
  }
  return remaining_us / MonoTime::kMicrosecondsPerSecond;
}

Slice NullSlice() {
  static char null_column_type = ValueEntryTypeAsChar::kNullLow;
  return Slice(&null_column_type, sizeof(null_column_type));
}

} // namespace

  // TODO(dtxn) scan through all involved transactions first to cache statuses in a batch,
  // so during building subdocument we don't need to request them one by one.
  // TODO(dtxn) we need to restart read with scan_ht = commit_ht if some transaction was committed
  // at time commit_ht within [scan_ht; read_request_time + max_clock_skew). Also we need
  // to wait until time scan_ht = commit_ht passed.
  // TODO(dtxn) for each scanned key (and its subkeys) we need to avoid *new* values committed at
  // ht <= scan_ht (or just ht < scan_ht?)
  // Question: what will break if we allow later commit at ht <= scan_ht ? Need to write down
  // detailed example.

Result<boost::optional<SubDocument>> TEST_GetSubDocument(
    const Slice& sub_doc_key,
    const DocDB& doc_db,
    const rocksdb::QueryId query_id,
    const TransactionOperationContext& txn_op_context,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    const std::vector<KeyEntryValue>* projection) {
  auto iter = CreateIntentAwareIterator(
      doc_db, BloomFilterMode::USE_BLOOM_FILTER, sub_doc_key, query_id,
      txn_op_context, deadline, read_time);
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", sub_doc_key.ToDebugHexString(),
                  iter->read_time().ToString());
  iter->SeekToLastDocKey();
  SchemaPackingStorage schema_packing_storage;
  DocDBTableReader doc_reader(
      iter.get(), deadline, projection, TableType::YQL_TABLE_TYPE, schema_packing_storage);
  RETURN_NOT_OK(doc_reader.UpdateTableTombstoneTime(sub_doc_key));

  iter->Seek(sub_doc_key);
  SubDocument result;
  if (VERIFY_RESULT(doc_reader.Get(sub_doc_key, &result))) {
    return result;
  }
  return boost::none;
}

DocDBTableReader::DocDBTableReader(
    IntentAwareIterator* iter, CoarseTimePoint deadline,
    const std::vector<KeyEntryValue>* projection,
    TableType table_type,
    std::reference_wrapper<const SchemaPackingStorage> schema_packing_storage)
    : iter_(iter),
      deadline_info_(deadline),
      projection_(projection),
      table_type_(table_type),
      schema_packing_storage_(schema_packing_storage) {
  if (projection_) {
    auto projection_size = projection_->size();
    encoded_projection_.resize(projection_size);
    for (size_t i = 0; i != projection_size; ++i) {
      (*projection_)[i].AppendToKey(&encoded_projection_[i]);
    }
  }
  VLOG_WITH_FUNC(4)
      << "Projection: " << AsString(projection_) << ", read time: " << iter_->read_time();
}

void DocDBTableReader::SetTableTtl(const Schema& table_schema) {
  table_expiration_ = Expiration(TableTTL(table_schema));
}

Status DocDBTableReader::UpdateTableTombstoneTime(const Slice& root_doc_key) {
  if (root_doc_key[0] == KeyEntryTypeAsChar::kColocationId ||
      root_doc_key[0] == KeyEntryTypeAsChar::kTableId) {
    // Update table_tombstone_time based on what is written to RocksDB if its not already set.
    // Otherwise, just accept its value.
    // TODO -- this is a bit of a hack to allow DocRowwiseIterator to pass along the table tombstone
    // time read at a previous invocation of this same code. If instead the DocRowwiseIterator owned
    // an instance of SubDocumentReaderBuilder, and this method call was hoisted up to that level,
    // passing around this table_tombstone_time would no longer be necessary.
    DocKey table_id;
    RETURN_NOT_OK(table_id.DecodeFrom(root_doc_key, DocKeyPart::kUpToId));
    iter_->Seek(table_id);

    Slice value;
    auto table_id_encoded = table_id.Encode();
    DocHybridTime doc_ht = DocHybridTime::kMin;

    RETURN_NOT_OK(iter_->FindLatestRecord(table_id_encoded, &doc_ht, &value));
    if (VERIFY_RESULT(Value::IsTombstoned(value))) {
      SCHECK_NE(doc_ht, DocHybridTime::kInvalid, Corruption,
                "Invalid hybrid time for table tombstone");
      table_tombstone_time_ = doc_ht;
    }
  }
  return Status::OK();
}

// Scan state entry. See state_ description below for details.
struct StateEntry {
  KeyBytes key_entry; // Represents the part of the key that is related to this state entry.
  DocHybridTime write_time;
  Expiration expiration;
  KeyEntryValue key_value; // Decoded key_entry.
  SubDocument* out;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(write_time, expiration, key_value);
  }
};

// Implements main logic in the reader.
// Used keep scan state and avoid passing it between methods.
class DocDBTableReader::GetHelper {
 public:
  GetHelper(DocDBTableReader* reader, const Slice& root_doc_key, SubDocument* result)
      : reader_(*reader), root_doc_key_(root_doc_key), result_(*result) {
    state_.emplace_back(StateEntry {
      .key_entry = KeyBytes(),
      .write_time = DocHybridTime(),
      .expiration = reader_.table_expiration_,
      .key_value = {},
      .out = &result_,
    });
  }

  Result<bool> Run() {
    IntentAwareIteratorPrefixScope prefix_scope(root_doc_key_, reader_.iter_);

    RETURN_NOT_OK(Prepare());

    // projection could be null in tests only.
    if (reader_.projection_) {
      if (reader_.projection_->empty()) {
        packed_column_data_ = GetPackedColumn(KeyEntryValue::kLivenessColumn.GetColumnId());
        RETURN_NOT_OK(Scan(CheckExistOnly::kTrue));
        return Found();
      }
      UpdatePackedColumnData();
    }
    RETURN_NOT_OK(Scan(CheckExistOnly::kFalse));

    if (last_found_ >= 0) {
      return true;
    }
    if (has_root_value_) { // For tests only.
      if (IsCollectionType(result_.type())) {
        result_.object_container().clear();
      }
      return true;
    }
    if (!reader_.projection_) { // For tests only.
      return false;
    }

    reader_.iter_->Seek(root_doc_key_);
    RETURN_NOT_OK(Scan(CheckExistOnly::kTrue));
    if (Found()) {
      for (const auto& key : *reader_.projection_) {
        result_.AllocateChild(key);
      }
      reader_.iter_->SeekOutOfSubDoc(root_doc_key_);
      return true;
    }

    return false;
  }

  // Whether document was found or not.
  bool Found() const {
    return last_found_ >= 0 || has_root_value_;
  }

 private:
  // Scans DocDB for entries related to root_doc_key_.
  // Iterator should already point to the first such entry.
  // Changes nearly all internal state fields.
  Status Scan(CheckExistOnly check_exist_only) {
    while (reader_.iter_->valid()) {
      if (reader_.deadline_info_.CheckAndSetDeadlinePassed()) {
        return STATUS(Expired, "Deadline for query passed");
      }

      if (!VERIFY_RESULT(HandleRecord(check_exist_only))) {
        return Status::OK();
      }
    }
    if (!check_exist_only && result_.value_type() == ValueEntryType::kObject &&
        reader_.projection_) {
      while (VERIFY_RESULT(NextColumn())) {}
    }
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "(" << check_exist_only << "), found: " << last_found_ << ", column index: "
        << column_index_ << ", " << result_.ToString();
    return Status::OK();
  }

  Result<bool> HandleRecord(CheckExistOnly check_exist_only) {
    auto key_result = VERIFY_RESULT(reader_.iter_->FetchKey());
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "check_exist_only: " << check_exist_only << ", key: "
        << SubDocKey::DebugSliceToString(key_result.key) << ", write time: "
        << key_result.write_time << ", value: " << reader_.iter_->value().ToDebugHexString();
    DCHECK(key_result.key.starts_with(root_doc_key_));
    auto subkeys = key_result.key.WithoutPrefix(root_doc_key_.size());

    return DoHandleRecord(key_result, subkeys, check_exist_only);
  }

  Result<bool> DoHandleRecord(
      const FetchKeyResult& key_result, const Slice& subkeys, CheckExistOnly check_exist_only) {
    if (!check_exist_only && reader_.projection_) {
      int compare_result = subkeys.compare_prefix(
          reader_.encoded_projection_[column_index_].AsSlice());
      VLOG_WITH_PREFIX_AND_FUNC(4)
          << "Subkeys: " << subkeys.ToDebugHexString() << ", column: "
          << (*reader_.projection_)[column_index_] << ", compare_result: " << compare_result;
      if (compare_result < 0) {
        SeekProjectionColumn();
        return true;
      }

      if (compare_result > 0) {
        if (!VERIFY_RESULT(NextColumn())) {
          return false;
        }

        return DoHandleRecord(key_result, subkeys, check_exist_only);
      }
    }

    if (VERIFY_RESULT(ProcessEntry(
            subkeys, reader_.iter_->value(), key_result.write_time, check_exist_only))) {
      packed_column_data_.row = nullptr;
    }
    if (check_exist_only && Found()) {
      return false;
    }
    reader_.iter_->SeekPastSubKey(key_result.key);
    return true;
  }

  // We are not yet reached next projection subkey, seek to it.
  void SeekProjectionColumn() {
    if (state_.front().key_entry.empty()) {
      // Lazily fill root doc key buffer.
      state_.front().key_entry.AppendRawBytes(root_doc_key_);
    }
    state_.front().key_entry.AppendRawBytes(
        reader_.encoded_projection_[column_index_].AsSlice());
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Seek next column: " << SubDocKey::DebugSliceToString(state_.front().key_entry);
    reader_.iter_->SeekForward(&state_.front().key_entry);
    state_.front().key_entry.Truncate(root_doc_key_.size());
  }

  // Process DB entry.
  // Return true if entry value was accepted.
  Result<bool> ProcessEntry(
      Slice subkeys, Slice value_slice, const DocHybridTime& write_time,
      CheckExistOnly check_exist_only) {
    subkeys = CleanupState(subkeys);
    if (state_.back().write_time >= write_time) {
      VLOG_WITH_PREFIX_AND_FUNC(4)
          << "State: " << AsString(state_) << ", write_time: " << write_time;
      return false;
    }
    auto control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value_slice));
    RETURN_NOT_OK(AllocateNewStateEntries(
        subkeys, write_time, check_exist_only, control_fields.ttl));
    return ApplyEntryValue(value_slice, control_fields, check_exist_only);
  }

  // Removes state_ elements that are that are not related to the passed in subkeys.
  // Returns remaining part of subkeys, that not represented in state_.
  Slice CleanupState(Slice subkeys) {
    for (size_t i = 1; i != state_.size(); ++i) {
      if (!subkeys.starts_with(state_[i].key_entry)) {
        state_.resize(i);
        break;
      }
      subkeys.remove_prefix(state_[i].key_entry.size());
    }
    return subkeys;
  }

  Status AllocateNewStateEntries(
      Slice subkeys, const DocHybridTime& write_time, CheckExistOnly check_exist_only,
      MonoDelta ttl) {
    while (!subkeys.empty()) {
      auto start = subkeys.data();
      state_.emplace_back();
      auto& parent = state_[state_.size() - 2];
      auto& entry = state_.back();
      RETURN_NOT_OK(entry.key_value.DecodeFromKey(&subkeys));
      entry.key_entry.AppendRawBytes(Slice(start, subkeys.data()));
      entry.write_time = subkeys.empty() ? write_time : parent.write_time;
      entry.out = check_exist_only ? nullptr : &parent.out->AllocateChild(entry.key_value);
      entry.expiration = GetNewExpiration(parent.expiration, ttl, write_time);
    }
    return Status::OK();
  }

  // Return true if entry value was accepted.
  Result<bool> ApplyEntryValue(
      const Slice& value_slice, const ValueControlFields& control_fields,
      CheckExistOnly check_exist_only) {
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "State: " << AsString(state_) << ", value: " << value_slice.ToDebugHexString();
    auto& current = state_.back();
    if (!IsObsolete(current.expiration)) {
      if (VERIFY_RESULT(TryDecodeValue(
              control_fields.timestamp, current.write_time.hybrid_time(), current.expiration,
              value_slice, current.out))) {
        last_found_ = column_index_;
        return true;
      }
      if (reader_.table_type_ == TableType::PGSQL_TABLE_TYPE) {
        return false;
      }
    }

    // When projection is specified we should always report projection columns, even when they are
    // nulls.
    if (!check_exist_only && state_.size() > (reader_.projection_ ? 2 : 1)) {
      state_[state_.size() - 2].out->DeleteChild(current.key_value);
    }
    return true;
  }

  Result<bool> NextColumn() {
    if (VERIFY_RESULT(DecodePackedColumn())) {
      last_found_ = column_index_;
    } else if (last_found_ < static_cast<int64_t>(column_index_)) {
      // Did not have value for column, allocate null value for it.
      result_.AllocateChild((*reader_.projection_)[column_index_]);
    }
    ++column_index_;
    if (column_index_ == reader_.projection_->size()) {
      reader_.iter_->SeekOutOfSubDoc(root_doc_key_);
      return false;
    }
    UpdatePackedColumnData();
    return true;
  }

  Result<bool> DecodePackedColumn() {
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Packed data " << (packed_column_data_ ? "present" : "missing") << ", expiration: "
        << state_.back().expiration.ToString();
    if (!packed_column_data_) {
      return false;
    }
    Slice value = packed_column_data_.encoded_value;
    auto control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value));
    const auto& write_time = packed_column_data_.row->doc_ht;
    auto expiration = GetNewExpiration(
        state_.back().expiration, control_fields.ttl, write_time);
    if (IsObsolete(expiration)) {
      return false;
    }
    return TryDecodeValue(
        control_fields.has_timestamp()
            ? control_fields.timestamp
            : packed_column_data_.row->control_fields.timestamp,
        write_time.hybrid_time(),
        expiration,
        value,
        &result_.AllocateChild((*reader_.projection_)[column_index_]));
  }

  // Updates information about the current column packed data.
  // Before calling, all fields should have correct values, especially column_index_ that points
  // to the current column in projection.
  void UpdatePackedColumnData() {
    auto& column = (*reader_.projection_)[column_index_];
    if (column.IsColumnId()) {
      packed_column_data_ = GetPackedColumn(column.GetColumnId());
    } else {
      // Used in tests only.
      packed_column_data_.row = nullptr;
    }
  }

  Status Prepare() {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Pos: " << reader_.iter_->DebugPosToString();

    state_.front().key_entry.AppendRawBytes(root_doc_key_);
    reader_.iter_->SeekForward(&state_.front().key_entry);

    Slice value;
    DocHybridTime doc_ht = reader_.table_tombstone_time_;
    RETURN_NOT_OK(reader_.iter_->FindLatestRecord(root_doc_key_, &doc_ht, &value));

    auto& root = state_.front();
    if (!reader_.iter_->valid()) {
      root.write_time = reader_.table_tombstone_time_;
      return Status::OK();
    }
    auto control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value));

    auto value_type = DecodeValueEntryType(value);
    if (value_type == ValueEntryType::kPackedRow) {
      value.consume_byte();
      schema_packing_ = &VERIFY_RESULT(reader_.schema_packing_storage_.GetPacking(&value)).get();
      packed_row_.Assign(value);
      packed_row_data_.doc_ht = doc_ht;
      packed_row_data_.control_fields = control_fields;
      auto& expiration = root.expiration;
      expiration = GetNewExpiration(expiration, control_fields.ttl, doc_ht);
    } else if (value_type != ValueEntryType::kTombstone && value_type != ValueEntryType::kInvalid) {
      // Used in tests only
      has_root_value_ = true;
      if (value_type != ValueEntryType::kObject) {
        SubDocument temp(value_type);
        RETURN_NOT_OK(temp.DecodeFromValue(value));
        result_ = temp;
      }
    }

    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Write time: " << doc_ht << ", control fields: " << control_fields.ToString();
    root.write_time = doc_ht;
    return Status::OK();
  }

  PackedColumnData GetPackedColumn(ColumnId column_id) {
    if (!schema_packing_) {
      // Actual for tests only.
      return PackedColumnData();
    }

    if (column_id == KeyEntryValue::kLivenessColumn.GetColumnId()) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "Packed row for liveness column";
      return PackedColumnData {
        .row = &packed_row_data_,
        .encoded_value = NullSlice(),
      };
    }

    auto slice = schema_packing_->GetValue(column_id, packed_row_.AsSlice());
    if (!slice) {
      VLOG_WITH_PREFIX_AND_FUNC(4) << "No packed row data for " << column_id;
      return PackedColumnData();
    }

    VLOG_WITH_PREFIX_AND_FUNC(4) << "Packed row " << column_id << ": " << slice->ToDebugHexString();
    return PackedColumnData {
      .row = &packed_row_data_,
      .encoded_value = slice->empty() ? NullSlice() : *slice,
    };
  }

  Result<bool> TryDecodeValue(
      UserTimeMicros timestamp, HybridTime write_time,
      const Expiration& expiration, const Slice& value_slice, SubDocument* out) {
    if (!out) {
      return DecodeValueEntryType(value_slice) != ValueEntryType::kTombstone;
    }
    RETURN_NOT_OK(out->DecodeFromValue(value_slice));
    if (timestamp != ValueControlFields::kInvalidTimestamp) {
      out->SetWriteTime(timestamp);
    } else {
      out->SetWriteTime(write_time.GetPhysicalValueMicros());
    }
    out->SetTtl(GetTtlRemainingSeconds(reader_.iter_->read_time().read, write_time, expiration));

    return !out->IsTombstone();
  }

  bool IsObsolete(const Expiration& expiration) {
    if (expiration.ttl == ValueControlFields::kMaxTtl) {
      return false;
    }

    return HasExpiredTTL(expiration.write_ht, expiration.ttl, reader_.iter_->read_time().read);
  }

  std::string LogPrefix() const {
    return DocKey::DebugSliceToString(root_doc_key_) + ": ";
  }

  DocDBTableReader& reader_;
  const Slice root_doc_key_;
  SubDocument& result_;

  // Packed row related fields. Not changed after initialization.
  ValueBuffer packed_row_;
  PackedRowData packed_row_data_;
  const SchemaPacking* schema_packing_ = nullptr;

  // Scanning stack.
  // I.e. the first entry is related to whole document (i.e. row).
  // The second entry corresponds to column.
  // And other entries are list/map entries in case of complex documents.
  boost::container::small_vector<StateEntry, 4> state_;

  // If packed row is found, this field contains data related to currently scanned column.
  PackedColumnData packed_column_data_;

  // Index of the current column in projection.
  size_t column_index_ = 0;

  // Index of the last found column in projection.
  int64_t last_found_ = kNothingFound;

  // Used in tests only, when we have value for root_doc_key_ itself.
  // In actual DB we don't have values for pure doc key.
  // Only delete marker, that is handled in a different way.
  bool has_root_value_ = false;
};

Result<bool> DocDBTableReader::Get(const Slice& root_doc_key, SubDocument* result) {
  GetHelper helper(this, root_doc_key, result);
  return helper.Run();
}

}  // namespace docdb
}  // namespace yb
