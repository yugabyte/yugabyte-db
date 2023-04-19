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
#include "yb/common/ql_expr.h"
#include "yb/common/ql_type.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_ttl_util.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/subdocument.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/util/fast_varint.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"



namespace yb {
namespace docdb {

using dockv::Expiration;
using dockv::SubDocument;
using dockv::ValueControlFields;
using dockv::ValueEntryType;

namespace {

constexpr int64_t kNothingFound = -1;

YB_STRONGLY_TYPED_BOOL(CheckExistOnly);

// The struct that stores encoded doc hybrid time and decode it on demand.
class LazyDocHybridTime {
 public:
  void Assign(const EncodedDocHybridTime& value) {
    encoded_ = value;
    decoded_ = DocHybridTime();
  }

  const EncodedDocHybridTime& encoded() const {
    return encoded_;
  }

  EncodedDocHybridTime* RawPtr() {
    decoded_ = DocHybridTime();
    return &encoded_;
  }

  Result<DocHybridTime> decoded() const {
    if (!decoded_.is_valid()) {
      decoded_ = VERIFY_RESULT(encoded_.Decode());
    }
    return decoded_;
  }

  std::string ToString() const {
    return encoded_.ToString();
  }

 private:
  EncodedDocHybridTime encoded_;
  mutable DocHybridTime decoded_;
};

Slice NullSlice() {
  static char null_column_type = dockv::ValueEntryTypeAsChar::kNullLow;
  return Slice(&null_column_type, sizeof(null_column_type));
}

Expiration GetNewExpiration(
    const Expiration& parent_exp, MonoDelta ttl, HybridTime new_write_ht) {
  Expiration new_exp = parent_exp;
  // We may need to update the TTL in individual columns.
  if (new_write_ht >= new_exp.write_ht) {
    // We want to keep the default TTL otherwise.
    if (ttl != ValueControlFields::kMaxTtl) {
      new_exp.write_ht = new_write_ht;
      new_exp.ttl = ttl;
    } else if (new_exp.ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    }
  }

  // If the hybrid time is kMin, then we must be using default TTL.
  if (new_exp.write_ht == HybridTime::kMin) {
    new_exp.write_ht = new_write_ht;
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

YB_STRONGLY_TYPED_BOOL(NeedValue);

} // namespace

Result<DocHybridTime> GetTableTombstoneTime(
    const Slice& root_doc_key, const DocDB& doc_db,
    const TransactionOperationContext& txn_op_context,
    CoarseTimePoint deadline, const ReadHybridTime& read_time) {
  if (root_doc_key[0] == dockv::KeyEntryTypeAsChar::kColocationId ||
      root_doc_key[0] == dockv::KeyEntryTypeAsChar::kTableId) {
    dockv::DocKey table_id;
    RETURN_NOT_OK(table_id.DecodeFrom(root_doc_key, dockv::DocKeyPart::kUpToId));

    auto table_id_encoded = table_id.Encode();
    auto iter = CreateIntentAwareIterator(
        doc_db, BloomFilterMode::USE_BLOOM_FILTER, table_id_encoded.AsSlice(),
        rocksdb::kDefaultQueryId, txn_op_context, deadline, read_time);
    iter->Seek(table_id_encoded);

    Slice value;
    EncodedDocHybridTime doc_ht(EncodedDocHybridTime::kMin);
    RETURN_NOT_OK(iter->FindLatestRecord(table_id_encoded, &doc_ht, &value));
    if (VERIFY_RESULT(dockv::Value::IsTombstoned(value))) {
      SCHECK(!doc_ht.empty(), Corruption, "Invalid hybrid time for table tombstone");
      return doc_ht.Decode();
    }
  }
  return DocHybridTime::kInvalid;
}

  // TODO(dtxn) scan through all involved transactions first to cache statuses in a batch,
  // so during building subdocument we don't need to request them one by one.
  // TODO(dtxn) we need to restart read with scan_ht = commit_ht if some transaction was committed
  // at time commit_ht within [scan_ht; read_request_time + max_clock_skew). Also we need
  // to wait until time scan_ht = commit_ht passed.
  // TODO(dtxn) for each scanned key (and its subkeys) we need to avoid *new* values committed at
  // ht <= scan_ht (or just ht < scan_ht?)
  // Question: what will break if we allow later commit at ht <= scan_ht ? Need to write down
  // detailed example.

Result<std::optional<SubDocument>> TEST_GetSubDocument(
    const Slice& sub_doc_key,
    const DocDB& doc_db,
    const rocksdb::QueryId query_id,
    const TransactionOperationContext& txn_op_context,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    const ReaderProjection* projection) {
  auto iter = CreateIntentAwareIterator(
      doc_db, BloomFilterMode::USE_BLOOM_FILTER, sub_doc_key, query_id,
      txn_op_context, deadline, read_time);
  DOCDB_DEBUG_LOG("GetSubDocument for key $0 @ $1", sub_doc_key.ToDebugHexString(),
                  iter->read_time().ToString());
  iter->SeekToLastDocKey();

  iter->Seek(sub_doc_key);
  if (iter->IsOutOfRecords()) {
    return std::nullopt;
  }
  auto fetched = VERIFY_RESULT(iter->FetchKey());
  if (!fetched.key.starts_with(sub_doc_key)) {
    return std::nullopt;
  }

  dockv::SchemaPackingStorage schema_packing_storage(TableType::YQL_TABLE_TYPE);
  DocDBTableReader doc_reader(
      iter.get(), deadline, projection, TableType::YQL_TABLE_TYPE, schema_packing_storage);
  RETURN_NOT_OK(doc_reader.UpdateTableTombstoneTime(VERIFY_RESULT(GetTableTombstoneTime(
      sub_doc_key, doc_db, txn_op_context, deadline, read_time))));
  SubDocument result;
  if (VERIFY_RESULT(doc_reader.Get(sub_doc_key, &result)) != DocReaderResult::kNotFound) {
    return result;
  }
  return std::nullopt;
}

// Shared information about packed row. I.e. common for all columns in this row.
class DocDBTableReader::PackedRowData {
 public:
  PackedRowData(
      DocDBTableReader* reader,
      std::reference_wrapper<const dockv::SchemaPackingStorage> schema_packing_storage)
      : reader_(*reader), schema_packing_storage_(schema_packing_storage) {
  }

  Result<ValueControlFields> ObtainControlFields(bool liveness_column, Slice* value) {
    if (liveness_column) {
      return control_fields_;
    }

    return VERIFY_RESULT(ValueControlFields::Decode(value));
  }

  auto GetTimestamp(const ValueControlFields& control_fields) const {
    return control_fields.has_timestamp() ? control_fields.timestamp : control_fields_.timestamp;
  }

  const LazyDocHybridTime& doc_ht() const {
    return *doc_ht_;
  }

  // Whether packed data is actually present for current row or not.
  bool exist() const {
    return exist_;
  }

  void Reset() {
    exist_ = false;
  }

  Status Prepare(
      Slice value, const LazyDocHybridTime* doc_ht, const ValueControlFields& control_fields,
      CheckExistOnly check_exists_only) {
    if (!check_exists_only) {
      if (!schema_packing_version_.empty() &&
          value.starts_with(schema_packing_version_.AsSlice())) {
        value.remove_prefix(schema_packing_version_.size());
      } else {
        RETURN_NOT_OK(UpdateSchemaPacking(&value));
      }
      value_.Assign(value);
      schema_packing_->GetBounds(value_.AsSlice(), &bounds_);
    }
    doc_ht_ = doc_ht;
    control_fields_ = control_fields;
    exist_ = true;
    return Status::OK();
  }

  Status UpdateSchemaPacking(Slice* value) {
    const auto* start = value->cdata();
    value->consume_byte();
    schema_packing_ = &VERIFY_RESULT(schema_packing_storage_.GetPacking(value)).get();
    schema_packing_version_.Assign(start, value->cdata());

    packed_index_.clear();
    packed_index_.reserve(reader_.projection_->size());
    auto it = reader_.projection_->begin();
    DCHECK(it->subkey == dockv::KeyEntryValue::kLivenessColumn);
    packed_index_.push_back(dockv::SchemaPacking::kSkippedColumnIdx);
    while (++it != reader_.projection_->end()) {
      if (it->subkey.IsColumnId()) {
        packed_index_.push_back(
            schema_packing_->GetIndex(it->subkey.GetColumnId()));
      } else {
        packed_index_.push_back(dockv::SchemaPacking::kSkippedColumnIdx);
      }
    }
    return Status::OK();
  }

  Slice GetPackedLivenessColumn() {
    if (!exist_) {
      // Actual for tests only.
      return Slice();
    }

    return NullSlice();
  }

  Slice GetPackedColumnValue(size_t column_index) {
    if (column_index == 0) {
      return GetPackedLivenessColumn();
    }

    if (!exist_) {
      // Actual for tests only.
      return Slice();
    }

    const auto packed_index = packed_index_[column_index];
    if (packed_index != dockv::SchemaPacking::kSkippedColumnIdx) {
      Slice slice(bounds_[packed_index], bounds_[packed_index + 1]);
      return !slice.empty() ? slice : NullSlice();
    }

    return Slice();
  }

 private:
  DocDBTableReader& reader_;
  const dockv::SchemaPackingStorage& schema_packing_storage_;

  bool exist_ = false;
  const dockv::SchemaPacking* schema_packing_ = nullptr;
  ByteBuffer<0x10> schema_packing_version_;

  ValueBuffer value_;
  const LazyDocHybridTime* doc_ht_;
  ValueControlFields control_fields_;
  boost::container::small_vector<const uint8_t*, 0x10> bounds_;
  boost::container::small_vector<int64_t, 0x10> packed_index_;
};

DocDBTableReader::DocDBTableReader(
    IntentAwareIterator* iter, CoarseTimePoint deadline,
    const ReaderProjection* projection,
    TableType table_type,
    std::reference_wrapper<const dockv::SchemaPackingStorage> schema_packing_storage)
    : iter_(iter),
      deadline_info_(deadline),
      projection_(projection),
      table_type_(table_type),
      packed_row_(new PackedRowData(this, schema_packing_storage)) {
  if (projection_) {
    auto projection_size = projection_->size();
    encoded_projection_.resize(projection_size);
    for (size_t i = 0; i != projection_size; ++i) {
      (*projection_)[i].subkey.AppendToKey(&encoded_projection_[i]);
    }
  }
  VLOG_WITH_FUNC(4)
      << "Projection: " << AsString(projection_) << ", read time: " << iter_->read_time();
}

DocDBTableReader::~DocDBTableReader() = default;

void DocDBTableReader::SetTableTtl(const Schema& table_schema) {
  table_expiration_ = Expiration(dockv::TableTTL(table_schema));
}

Status DocDBTableReader::UpdateTableTombstoneTime(DocHybridTime doc_ht) {
  if (doc_ht.is_valid()) {
    table_tombstone_time_.Assign(doc_ht);
  }
  return Status::OK();
}

// Scan state entry. See state_ description below for details.
struct StateEntry {
  dockv::KeyBytes key_entry; // Represents the part of the key that is related to this state entry.
  LazyDocHybridTime write_time;
  Expiration expiration;
  dockv::KeyEntryValue key_value; // Decoded key_entry.
  SubDocument* out;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(write_time, expiration, key_value);
  }
};

// Returns true if value is NOT tombstone, false if value is tombstone.
Result<bool> TryDecodeValueOnly(
    const Slice& value_slice, const QLTypePtr& ql_type, QLValuePB* out) {
  if (dockv::DecodeValueEntryType(value_slice) == ValueEntryType::kTombstone) {
    if (out) {
      out->Clear();
    }
    return false;
  }
  if (out) {
    if (ql_type) {
      RETURN_NOT_OK(dockv::PrimitiveValue::DecodeToQLValuePB(value_slice, ql_type, out));
    } else {
      out->Clear();
    }
  }
  return true;
}

Result<bool> TryDecodeValueOnly(
    const Slice& value_slice, const QLTypePtr& ql_type, dockv::PrimitiveValue* out) {
  if (dockv::DecodeValueEntryType(value_slice) == ValueEntryType::kTombstone) {
    if (out) {
      *out = dockv::PrimitiveValue::kTombstone;
    }
    return false;
  }
  if (out) {
    RETURN_NOT_OK(out->DecodeFromValue(value_slice));
  }
  return true;
}

DocReaderResult FoundResult(bool iter_valid) {
  return iter_valid ? DocReaderResult::kFoundNotFinished : DocReaderResult::kFoundAndFinished;
}

// Implements main logic in the reader.
// Used keep scan state and avoid passing it between methods.
// It is less performant than FlatGetHelper, but handles the general case of nested documents.
// Not used for YSQL if FLAGS_ysql_use_flat_doc_reader is true.
template <bool is_flat_doc, bool ysql>
class DocDBTableReader::GetHelperBase {
 public:
  GetHelperBase(DocDBTableReader* reader, const Slice& root_doc_key)
      : reader_(*reader), root_doc_key_(root_doc_key) {}

  virtual ~GetHelperBase() {}

 protected:
  Result<DocReaderResult> DoRun(
      Expiration* root_expiration, LazyDocHybridTime* root_write_time,
      CheckExistOnly check_exists_only) {
    IntentAwareIteratorPrefixScope prefix_scope(root_doc_key_, reader_.iter_);

    auto fetched_key = VERIFY_RESULT(Prepare(root_expiration, root_write_time, check_exists_only));

    if (check_exists_only) {
      if (reader_.packed_row_->exist()) {
        return FoundResult(/* iter_valid= */ true);
      }
      auto iter_valid = VERIFY_RESULT(Scan(CheckExistOnly::kTrue, &fetched_key));
      return Found() ? FoundResult(iter_valid) : DocReaderResult::kNotFound;
    } else if (!reader_.projection_) {
      // projection could be null in tests only.
      cannot_scan_columns_ = true;
    }

    auto iter_valid = VERIFY_RESULT(Scan(CheckExistOnly::kFalse, &fetched_key));

    if (found_ ||
        CheckForRootValue()) { // Could only happen in tests.
      return FoundResult(iter_valid);
    }

    if (ysql || // YSQL always has liveness column, and it is always present in projection.
        !reader_.projection_) { // Could only happen in tests.
      return DocReaderResult::kNotFound;
    }

    reader_.iter_->Seek(root_doc_key_);
    if (reader_.iter_->IsOutOfRecords()) {
      return DocReaderResult::kNotFound;
    } else {
      fetched_key = VERIFY_RESULT(reader_.iter_->FetchKey());
      iter_valid = VERIFY_RESULT(Scan(CheckExistOnly::kTrue, &fetched_key));
    }
    auto result = Found();
    if (result) {
      EmptyDocFound();
      return FoundResult(iter_valid);
    }

    return DocReaderResult::kNotFound;
  }

  virtual void EmptyDocFound() = 0;

  // Whether document was found or not.
  virtual bool Found() const = 0;

  // Scans DocDB for entries related to root_doc_key_.
  // Iterator should already point to the first such entry.
  // Changes nearly all internal state fields.
  Result<bool> Scan(CheckExistOnly check_exist_only, FetchKeyResult* fetched_key) {
    for (;;) {
      if (reader_.deadline_info_.CheckAndSetDeadlinePassed()) {
        return STATUS(Expired, "Deadline for query passed");
      }

      if (!VERIFY_RESULT(HandleRecord(check_exist_only, *fetched_key))) {
        return true;
      }

      if (reader_.iter_->IsOutOfRecords()) {
        break;
      }
      *fetched_key = VERIFY_RESULT(reader_.iter_->FetchKey());
      DVLOG_WITH_PREFIX_AND_FUNC(4)
          << "(" << check_exist_only << "), new position: "
          << dockv::SubDocKey::DebugSliceToString(fetched_key->key) << ", value: "
          << dockv::Value::DebugSliceToString(reader_.iter_->value());
    }
    if (!cannot_scan_columns_ && !check_exist_only) {
      while (VERIFY_RESULT(NextColumn())) {}
    }
    DVLOG_WITH_PREFIX_AND_FUNC(4)
        << "(" << check_exist_only << "), found: " << found_ << ", column index: "
        << column_index_ << ", finished: " << reader_.iter_->IsOutOfRecords() << ", "
        << GetResultAsString();
    return false;
  }

  virtual std::string GetResultAsString() const = 0;

  Result<bool> HandleRecord(CheckExistOnly check_exist_only, const FetchKeyResult& key_result) {
    DVLOG_WITH_PREFIX_AND_FUNC(4)
        << "check_exist_only: " << check_exist_only << ", key: "
        << dockv::SubDocKey::DebugSliceToString(key_result.key) << ", write time: "
        << key_result.write_time.ToString() << ", value: "
        << reader_.iter_->value().ToDebugHexString();
    DCHECK(key_result.key.starts_with(root_doc_key_));
    auto subkeys = key_result.key.WithoutPrefix(root_doc_key_.size());

    return DoHandleRecord(key_result, subkeys, check_exist_only);
  }

  Result<bool> DoHandleRecord(
      const FetchKeyResult& key_result, const Slice& subkeys, CheckExistOnly check_exist_only) {
    if (!check_exist_only && reader_.projection_) {
      auto projection_column_encoded_key_prefix =
          reader_.encoded_projection_[column_index_].AsSlice();
      int compare_result = subkeys.compare_prefix(projection_column_encoded_key_prefix);
      DVLOG_WITH_PREFIX_AND_FUNC(4) << "Subkeys: " << subkeys.ToDebugHexString()
                                    << ", column: " << (*reader_.projection_)[column_index_].subkey
                                    << ", compare_result: " << compare_result;
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

      if (is_flat_doc) {
        SCHECK_EQ(
            subkeys.size(), projection_column_encoded_key_prefix.size(), IllegalState,
            "DocDBTableReader::FlatGetHelper supports at most 1 subkey");
      }
    }

    if (VERIFY_RESULT(ProcessEntry(
            subkeys, reader_.iter_->value(), key_result.write_time, check_exist_only))) {
      last_column_value_index_ = column_index_;
    }
    if (check_exist_only && Found()) {
      return false;
    }
    reader_.iter_->SeekPastSubKey(key_result.key);
    return true;
  }

  // We are not yet reached next projection subkey, seek to it.
  void SeekProjectionColumn() {
    if (root_key_entry_->empty()) {
      // Lazily fill root doc key buffer.
      root_key_entry_->AppendRawBytes(root_doc_key_);
    }
    root_key_entry_->AppendRawBytes(
        reader_.encoded_projection_[column_index_].AsSlice());
    DVLOG_WITH_PREFIX_AND_FUNC(4)
        << "Seek next column: " << dockv::SubDocKey::DebugSliceToString(*root_key_entry_);
    reader_.iter_->SeekForward(root_key_entry_);
    root_key_entry_->Truncate(root_doc_key_.size());
  }

  // Process DB entry.
  // Return true if entry value was accepted.
  virtual Result<bool> ProcessEntry(
      Slice subkeys, Slice value_slice, const EncodedDocHybridTime& write_time,
      CheckExistOnly check_exist_only) = 0;

  Result<bool> NextColumn() {
    if (last_column_value_index_ < make_signed(column_index_)) {
      if (VERIFY_RESULT(DecodePackedColumn())) {
        found_ = true;
      } else {
        NoValueForColumnIndex();
      }
    }
    ++column_index_;
    if (column_index_ == reader_.projection_->size()) {
      return false;
    }
    return true;
  }

  virtual Result<bool> DecodePackedColumn() = 0;

  virtual void NoValueForColumnIndex() = 0;

  template <class GetValueAddressFunc>
  Result<bool> DoDecodePackedColumn(
      const Expiration& parent_exp, GetValueAddressFunc get_value_address) {
    DVLOG_WITH_PREFIX_AND_FUNC(4)
        << "Expiration: " << AsString(parent_exp);
    auto value = reader_.packed_row_->GetPackedColumnValue(column_index_);
    if (value.empty()) {
      return false;
    }
    auto& projected_column = (*reader_.projection_)[column_index_];
    if (ysql) {
      // Remove buggy intent_doc_ht from start of the column. See #16650 for details.
      if (value.TryConsumeByte(dockv::KeyEntryTypeAsChar::kHybridTime)) {
        RETURN_NOT_OK(DocHybridTime::EncodedFromStart(&value));
      }
      return TryDecodeValueOnly(
          value, projected_column.type, get_value_address());
    }
    auto control_fields = VERIFY_RESULT(reader_.packed_row_->ObtainControlFields(
        projected_column.subkey == dockv::KeyEntryValue::kLivenessColumn, &value));
    const auto& write_time = reader_.packed_row_->doc_ht();
    const auto expiration = GetNewExpiration(
          parent_exp, control_fields.ttl, VERIFY_RESULT(write_time.decoded()).hybrid_time());
    if (IsObsolete(expiration)) {
      return false;
    }
    return TryDecodeValue(
        reader_.packed_row_->GetTimestamp(control_fields),
        write_time, expiration, value, get_value_address());
  }

  virtual Status SetRootValue(ValueEntryType row_value_type, const Slice& row_value) = 0;

  virtual bool CheckForRootValue() = 0;

  Result<FetchKeyResult> Prepare(
      Expiration* root_expiration, LazyDocHybridTime* root_write_time,
      CheckExistOnly check_exists_only) {
    DVLOG_WITH_PREFIX_AND_FUNC(4) << "Pos: " << reader_.iter_->DebugPosToString();

    root_key_entry_->AppendRawBytes(root_doc_key_);
    reader_.packed_row_->Reset();

    auto key_result = VERIFY_RESULT(reader_.iter_->FetchKey());
    DCHECK(key_result.key.starts_with(root_doc_key_));

    Slice value;
    root_write_time->Assign(reader_.table_tombstone_time_);
    if (root_doc_key_.size() == key_result.key.size() &&
        key_result.write_time >= root_write_time->encoded()) {
      root_write_time->Assign(key_result.write_time);
      value = reader_.iter_->value();
    }

    auto control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value));

    auto value_type = dockv::DecodeValueEntryType(value);
    if (value_type == ValueEntryType::kPackedRow) {
      RETURN_NOT_OK(reader_.packed_row_->Prepare(
          value, root_write_time, control_fields, check_exists_only));
      if (TtlCheckRequired()) {
        *root_expiration = GetNewExpiration(
            *root_expiration, ValueControlFields::kMaxTtl,
            VERIFY_RESULT(root_write_time->decoded()).hybrid_time());
      }
    } else if (value_type != ValueEntryType::kTombstone && value_type != ValueEntryType::kInvalid) {
      // Used in tests only
      RETURN_NOT_OK(SetRootValue(value_type, value));
    }

    DVLOG_WITH_PREFIX_AND_FUNC(4)
        << "Write time: " << root_write_time->ToString() << ", control fields: "
        << control_fields.ToString();
    return key_result;
  }

  Result<bool> TryDecodeValue(
      UserTimeMicros timestamp, const LazyDocHybridTime& write_time, const Expiration& expiration,
      const Slice& value_slice, dockv::PrimitiveValue* out) {
    auto has_value = VERIFY_RESULT(TryDecodeValueOnly(value_slice, /* ql_type= */ nullptr, out));
    if (has_value && out) {
      auto write_ht = VERIFY_RESULT(write_time.decoded()).hybrid_time();
      if (timestamp != ValueControlFields::kInvalidTimestamp) {
        out->SetWriteTime(timestamp);
      } else {
        out->SetWriteTime(write_ht.GetPhysicalValueMicros());
      }
      out->SetTtl(GetTtlRemainingSeconds(reader_.iter_->read_time().read, write_ht, expiration));
    }

    return has_value;
  }

  Result<bool> TryDecodeValue(
      UserTimeMicros timestamp, const LazyDocHybridTime& write_time, const Expiration& expiration,
      const Slice& value_slice, QLValuePB* out) {
    return TryDecodeValueOnly(
        value_slice, (*reader_.projection_)[column_index_].type, out);
  }

  bool IsObsolete(const Expiration& expiration) {
    if (expiration.ttl == ValueControlFields::kMaxTtl) {
      return false;
    }

    return dockv::HasExpiredTTL(
        expiration.write_ht, expiration.ttl, reader_.iter_->read_time().read);
  }

  std::string LogPrefix() const {
    return dockv::DocKey::DebugSliceToString(root_doc_key_) + ": ";
  }

  static constexpr bool TtlCheckRequired() {
    // TODO(scanperf) also avoid checking TTL for YCQL tables w/o TTL.
    return !ysql;
  }

  DocDBTableReader& reader_;
  const Slice root_doc_key_;
  // Pointer to root key entry that is owned by subclass. Can't be nullptr.
  dockv::KeyBytes* root_key_entry_;

  // Index of the current column in projection.
  size_t column_index_ = 0;

  // Set to true when there is no projection or root is not an object (that only can happen when
  // called from the tests).
  bool cannot_scan_columns_ = false;

  // Index of the last found individual (not packed) column value in projection.
  int64_t last_column_value_index_ = kNothingFound;

  // Whether we found row related value or not.
  bool found_ = false;
};

// Implements main logic in the reader.
// Used keep scan state and avoid passing it between methods.
class DocDBTableReader::GetHelper :
    public DocDBTableReader::GetHelperBase</* is_flat_doc = */ false, /* ysql = */ false> {
 public:
  using Base = DocDBTableReader::GetHelperBase</* is_flat_doc = */ false, /* ysql = */ false>;

  GetHelper(DocDBTableReader* reader, const Slice& root_doc_key, SubDocument* result)
      : Base(reader, root_doc_key), result_(*result) {
    state_.emplace_back(StateEntry {
      .key_entry = dockv::KeyBytes(),
      .write_time = LazyDocHybridTime(),
      .expiration = reader_.table_expiration_,
      .key_value = {},
      .out = &result_,
    });
    root_key_entry_ = &state_.front().key_entry;
  }

  Result<DocReaderResult> Run() {
    auto& root = state_.front();
    return DoRun(&root.expiration, &root.write_time, CheckExistOnly::kFalse);
  }

  void EmptyDocFound() override {
    for (const auto& key : *reader_.projection_) {
      result_.AllocateChild(key.subkey);
    }
  }

  bool Found() const override {
    return found_ || has_root_value_;
  }

  std::string GetResultAsString() const override { return AsString(result_); }

 private:
  Result<bool> ProcessEntry(
      Slice subkeys, Slice value_slice, const EncodedDocHybridTime& write_time,
      CheckExistOnly check_exist_only) override {
    subkeys = CleanupState(subkeys);
    if (state_.back().write_time.encoded() >= write_time) {
      DVLOG_WITH_PREFIX_AND_FUNC(4)
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
      Slice subkeys, const EncodedDocHybridTime& write_time, CheckExistOnly check_exist_only,
      MonoDelta ttl) {
    LazyDocHybridTime lazy_write_time;
    lazy_write_time.Assign(write_time);
    while (!subkeys.empty()) {
      auto start = subkeys.data();
      state_.emplace_back();
      auto& parent = state_[state_.size() - 2];
      auto& entry = state_.back();
      RETURN_NOT_OK(entry.key_value.DecodeFromKey(&subkeys));
      entry.key_entry.AppendRawBytes(Slice(start, subkeys.data()));
      entry.write_time = subkeys.empty() ? lazy_write_time : parent.write_time;
      entry.out = check_exist_only ? nullptr : &parent.out->AllocateChild(entry.key_value);
      if (TtlCheckRequired()) {
        entry.expiration = GetNewExpiration(
            parent.expiration, ttl, VERIFY_RESULT(entry.write_time.decoded()).hybrid_time());
      }
    }
    return Status::OK();
  }

  // Return true if entry value was accepted.
  Result<bool> ApplyEntryValue(
      const Slice& value_slice, const ValueControlFields& control_fields,
      CheckExistOnly check_exist_only) {
    auto& current = state_.back();
    DVLOG_WITH_PREFIX_AND_FUNC(4)
        << "State: " << AsString(state_) << ", value: " << value_slice.ToDebugHexString()
        << ", obsolete: " << IsObsolete(current.expiration);

    if (!IsObsolete(current.expiration)) {
      if (VERIFY_RESULT(TryDecodeValue(
              control_fields.timestamp, current.write_time, current.expiration, value_slice,
              current.out))) {
        found_ = true;
        return true;
      }
    }

    // When projection is specified we should always report projection columns, even when they are
    // nulls.
    if (!check_exist_only && state_.size() > (reader_.projection_ ? 2 : 1)) {
      state_[state_.size() - 2].out->DeleteChild(current.key_value);
    }
    return true;
  }

  void NoValueForColumnIndex() override {
    DVLOG_WITH_PREFIX_AND_FUNC(4) << Format(
        "Did not have value for column_index $0 ($1), allocate invalid value for it, will be "
        "converted to null value by KeyEntryValue::ToQLValuePB.",
        column_index_, (*reader_.projection_)[column_index_].subkey);
    result_.AllocateChild((*reader_.projection_)[column_index_].subkey);
  }

  Result<bool> DecodePackedColumn() override {
    state_.resize(1);
    return DoDecodePackedColumn(state_.back().expiration, [this] {
      return &result_.AllocateChild((*reader_.projection_)[column_index_].subkey);
    });
  }

  Status SetRootValue(ValueEntryType root_value_type, const Slice& root_value) override {
    has_root_value_ = true;
    if (root_value_type != ValueEntryType::kObject) {
      SubDocument temp(root_value_type);
      RETURN_NOT_OK(temp.DecodeFromValue(root_value));
      result_ = temp;
      cannot_scan_columns_ = true;
    }
    return Status::OK();
  }

  bool CheckForRootValue() override {
    if (!has_root_value_) {
      return false;
    }
    if (IsCollectionType(result_.type())) {
      result_.object_container().clear();
    }
    return true;
  }


  SubDocument& result_;

  // Scanning stack.
  // I.e. the first entry is related to whole document (i.e. row).
  // The second entry corresponds to column.
  // And other entries are list/map entries in case of complex documents.
  boost::container::small_vector<StateEntry, 4> state_;

  // Used in tests only, when we have value for root_doc_key_ itself.
  // In actual DB we don't have values for pure doc key.
  // Only delete marker, that is handled in a different way.
  bool has_root_value_ = false;
};

// It is more performant than DocDBTableReader::GetHelper, but can't handle the general case of
// nested documents that is possible in YCQL.
// Used for YSQL if FLAGS_ysql_use_flat_doc_reader is true.
class DocDBTableReader::FlatGetHelper :
    public DocDBTableReader::GetHelperBase</* is_flat_doc= */ true, /* ysql= */ true> {
 public:
  using Base = DocDBTableReader::GetHelperBase</* is_flat_doc= */ true, /* ysql= */ true>;

  FlatGetHelper(
      DocDBTableReader* reader, const Slice& root_doc_key, QLTableRow* result)
      : Base(reader, root_doc_key), result_(result) {
    row_expiration_ = reader_.table_expiration_;
    root_key_entry_ = &row_key_;
  }

  Result<DocReaderResult> Run() {
    CheckExistOnly check_exist_only(
        result_ == nullptr
        || reader_.projection_->empty()
        || (reader_.projection_->size() == 1
            && (*reader_.projection_).front().subkey == dockv::KeyEntryValue::kLivenessColumn));
    return DoRun(&row_expiration_, &row_write_time_, check_exist_only);
  }

  void EmptyDocFound() override {}

  bool Found() const override {
    return found_;
  }

  std::string GetResultAsString() const override { return result_ ? AsString(*result_) : "<NULL>"; }

 private:
  // Return true if entry is more recent than packed row.
  Result<bool> ProcessEntry(
      Slice /* subkeys */, Slice value_slice, const EncodedDocHybridTime& write_time,
      CheckExistOnly check_exist_only) override {
    if (row_write_time_.encoded() >= write_time) {
      DVLOG_WITH_PREFIX_AND_FUNC(4) << "write_time: " << write_time.ToString();
      return false;
    }

    auto* column_value = check_exist_only ? nullptr : GetValueAddress();

    if (TtlCheckRequired()) {
      auto control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value_slice));

      LazyDocHybridTime lazy_write_time;
      lazy_write_time.Assign(write_time);

      Expiration column_expiration = GetNewExpiration(
            row_expiration_, control_fields.ttl,
            VERIFY_RESULT(lazy_write_time.decoded()).hybrid_time());

        DVLOG_WITH_PREFIX_AND_FUNC(4) << "column_index_: " << column_index_
                                      << ", value: " << value_slice.ToDebugHexString();
      if (IsObsolete(column_expiration)) {
        return true;
      }

      if (VERIFY_RESULT(TryDecodeValue(
              control_fields.timestamp, lazy_write_time, column_expiration, value_slice,
              column_value))) {
        found_ = true;
      }
    } else {
      if (VERIFY_RESULT(TryDecodeValueOnly(
              value_slice, (*reader_.projection_)[column_index_].type, column_value))) {
        found_ = true;
      }
    }

    return true;
  }

  void NoValueForColumnIndex() override {
    if (result_) {
      result_->MarkTombstoned((*reader_.projection_)[column_index_].subkey.GetColumnId());
    }
  }

  Result<bool> DecodePackedColumn() override {
    return DoDecodePackedColumn(row_expiration_, [this] {
      return GetValueAddress();
    });
  }

  QLValuePB* GetValueAddress() {
    if (column_index_ == 0) {
      return nullptr;
    }
    return &result_->AllocColumn((*reader_.projection_)[column_index_].subkey.GetColumnId()).value;
  }

  Status SetRootValue(ValueEntryType row_value_type, const Slice& row_value) override {
    return Status::OK();
  };

  bool CheckForRootValue() override {
    return false;
  }

  // Owned by the DocDBTableReader::FlatGetHelper user.
  QLTableRow* result_;

  dockv::KeyBytes row_key_;
  LazyDocHybridTime row_write_time_;
  Expiration row_expiration_;
};

Result<DocReaderResult> DocDBTableReader::Get(const Slice& root_doc_key, SubDocument* result) {
  GetHelper helper(this, root_doc_key, result);
  return helper.Run();
}

Result<DocReaderResult> DocDBTableReader::GetFlat(const Slice& root_doc_key, QLTableRow* result) {
  FlatGetHelper helper(this, root_doc_key, result);
  return helper.Run();
}

std::string ProjectedColumn::ToString() const {
  return YB_STRUCT_TO_STRING(subkey, type);
}

}  // namespace docdb
}  // namespace yb
