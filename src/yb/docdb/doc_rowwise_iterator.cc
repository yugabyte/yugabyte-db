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

#include "yb/docdb/doc_rowwise_iterator.h"

#include <cstdint>
#include <iterator>
#include <ostream>
#include <string>
#include <vector>

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/doc_ql_filefilter.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/scan_choices.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/expiration.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/qlexpr/ql_expr.h"

// TODO(sergei) Wrong dependency
#include "yb/tablet/tablet_metrics.h"

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/strongly_typed_bool.h"

DEFINE_RUNTIME_bool(ysql_use_flat_doc_reader, true,
    "Use DocDBTableReader optimization that relies on having at most 1 subkey for YSQL.");

DEFINE_test_flag(int32, fetch_next_delay_ms, 0, "Amount of time to delay inside FetchNext");
DEFINE_test_flag(string, fetch_next_delay_column, "", "Only delay when schema has specific column");

DECLARE_bool(use_fast_backward_scan);

DEPRECATE_FLAG(bool, use_offset_based_key_decoding, "02_2024");

DEFINE_RUNTIME_bool(enable_colocated_table_tombstone_cache, true,
    "When set, colocated reads will cache table tombstones to avoid repeated tombstone checks.");

using namespace std::chrono_literals;

namespace yb::docdb {

using dockv::DocKey;

namespace {

Status StoreValue(
    const Schema& schema, const dockv::ProjectedColumn& column, size_t col_idx,
    dockv::DocKeyDecoder* decoder, qlexpr::QLTableRow* row) {
  dockv::KeyEntryValue key_entry_value;
  RETURN_NOT_OK(decoder->DecodeKeyEntryValue(&key_entry_value));
  const auto& column_schema = VERIFY_RESULT_REF(schema.column_by_id(column.id));
  key_entry_value.ToQLValuePB(column_schema.type(), &row->AllocColumn(column.id).value);
  return Status::OK();
}

} // namespace

DocRowwiseIterator::DocRowwiseIterator(
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    const ReadOperationData& read_operation_data,
    std::reference_wrapper<const ScopedRWOperation> pending_op)
    : doc_read_context_(doc_read_context),
      schema_(&doc_read_context_.schema()),
      txn_op_context_(txn_op_context),
      read_operation_data_(read_operation_data),
      doc_db_(doc_db),
      pending_op_ref_(pending_op),
      projection_(projection),
      deadline_info_(read_operation_data.deadline) {
}

DocRowwiseIterator::DocRowwiseIterator(
    const dockv::ReaderProjection& projection,
    std::shared_ptr<DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    const ReadOperationData& read_operation_data,
    ScopedRWOperation&& pending_op)
    : doc_read_context_holder_(std::move(doc_read_context)),
      doc_read_context_(*doc_read_context_holder_),
      schema_(&doc_read_context_.schema()),
      txn_op_context_(txn_op_context),
      read_operation_data_(read_operation_data),
      doc_db_(doc_db),
      pending_op_holder_(std::move(pending_op)),
      pending_op_ref_(pending_op_holder_),
      projection_(projection),
      deadline_info_(read_operation_data.deadline) {
}

DocRowwiseIterator::~DocRowwiseIterator() {
  FinalizeKeyFoundStats();
}

void DocRowwiseIterator::CheckInitOnce() {
  if (is_initialized_) {
    YB_LOG_EVERY_N_SECS(DFATAL, 3600)
        << "DocRowwiseIterator(" << this << ") has been already initialized\n"
        << GetStackTrace();
  }
  is_initialized_ = true;
}

void DocRowwiseIterator::SetSchema(const Schema& schema) {
  LOG_IF(DFATAL, is_initialized_) << "Iterator has been already initialized in " << __func__;
  schema_ = &schema;
}

Status DocRowwiseIterator::InitForTableType(
    TableType table_type, Slice sub_doc_key, SkipSeek skip_seek,
    AddTablePrefixToKey add_table_prefix_to_key) {
  CheckInitOnce();
  table_type_ = table_type;
  ignore_ttl_ = (table_type_ == TableType::PGSQL_TABLE_TYPE);
  RETURN_NOT_OK(
      InitIterator(BloomFilterOptions::Inactive(), AvoidUselessNextInsteadOfSeek::kFalse));

  if (sub_doc_key.empty() || add_table_prefix_to_key) {
    dockv::DocKeyEncoder(&row_key_).Schema(*schema_);
  }
  row_key_.AppendRawBytes(sub_doc_key);
  if (!skip_seek) {
    Seek(row_key_);
  }
  has_bound_key_ = false;

  scan_choices_ = ScanChoices::CreateEmpty();

  return Status::OK();
}

Status DocRowwiseIterator::Init(
    const qlexpr::YQLScanSpec& doc_spec, SkipSeek skip_seek,
    AllowVariableBloomFilter allow_variable_bloom_filter,
    AvoidUselessNextInsteadOfSeek avoid_useless_next_instead_of_seek) {
  table_type_ = doc_spec.client_type() == YQL_CLIENT_CQL ? TableType::YQL_TABLE_TYPE
                                                         : TableType::PGSQL_TABLE_TYPE;
  ignore_ttl_ = table_type_ == TableType::PGSQL_TABLE_TYPE;

  CheckInitOnce();
  is_forward_scan_ = doc_spec.is_forward_scan();

  VLOG(2) << "Initializing iterator direction: " << (is_forward_scan_ ? "FORWARD" : "BACKWARD")
          << ", read operation data: " << read_operation_data_.ToString();

  auto bounds = doc_spec.bounds();
  VLOG(2) << "DocKey Bounds " << DocKey::DebugSliceToString(bounds.lower.AsSlice()) << ", "
          << DocKey::DebugSliceToString(bounds.upper.AsSlice());

  if (is_forward_scan_) {
    has_bound_key_ = !bounds.upper.empty();
    if (has_bound_key_) {
      bound_key_ = bounds.upper;
    }
  } else {
    has_bound_key_ = !bounds.lower.empty();
    if (has_bound_key_) {
      // We use kLowest = 0 to mark -Inf bound. But there are special entries like
      // transaction apply record with value > 0.
      if (bounds.lower.data()[0] != dockv::KeyEntryTypeAsChar::kLowest) {
        bound_key_ = bounds.lower;
      } else {
        // Having kLowest set for the very first byte and backward scan direction, means all
        // the records will meet the bound as there is no record starting with kLowest. Since real
        // table rows cannot have a key before kNullLow, we could use it as bound instead of -Inf.
        bound_key_.Clear();
        bound_key_.AppendKeyEntryType(KeyEntryType::kNullLow);
        VLOG(2) << "Adjusted lower bound to " << bound_key_.ToString() << "";
      }
    }
  }

  if (has_bound_key_) {
    if (is_forward_scan_) {
      bounds.upper = bound_key_;
    } else {
      bounds.lower = bound_key_;
    }
  }

  scan_choices_ = VERIFY_RESULT(ScanChoices::Create(
      doc_read_context_, doc_spec, bounds, doc_read_context_.table_key_prefix(),
      allow_variable_bloom_filter));

  RETURN_NOT_OK(InitIterator(
      scan_choices_->BloomFilterOptions(), avoid_useless_next_instead_of_seek, doc_spec.QueryId(),
      CreateFileFilter(doc_spec)));

  if (!skip_seek) {
    if (is_forward_scan_) {
      Seek(bounds.lower);
    } else {
      SeekPrevDocKey(bounds.upper);
    }
  }

  return Status::OK();
}

void DocRowwiseIterator::IncrementKeyFoundStats(
    const bool obsolete, const EncodedDocHybridTime& write_time) {
  if (doc_db_.metrics) {
    ++keys_found_;
    if (obsolete) {
      ++obsolete_keys_found_;
      if (history_cutoff_.empty() && doc_db_.retention_policy) {
        // Lazy initialization to avoid extra steps in most cases.
        // It is expected that we will find obsolete keys quite rarely.
        history_cutoff_.Assign(DocHybridTime(doc_db_.retention_policy->ProposedHistoryCutoff()));
      }
      if (write_time < history_cutoff_) {
        // If the obsolete key found was written before the history cutoff, then count
        // record this in addition (since it can be removed via compaction).
        ++obsolete_keys_found_past_cutoff_;
      }
    }
  }
}

void DocRowwiseIterator::FinalizeKeyFoundStats() {
  if (!doc_db_.metrics || !keys_found_) {
    return;
  }

  doc_db_.metrics->IncrementBy(tablet::TabletCounters::kDocDBKeysFound, keys_found_);
  if (obsolete_keys_found_) {
    doc_db_.metrics->IncrementBy(
        tablet::TabletCounters::kDocDBObsoleteKeysFound, obsolete_keys_found_);
    if (obsolete_keys_found_past_cutoff_) {
      doc_db_.metrics->IncrementBy(
          tablet::TabletCounters::kDocDBObsoleteKeysFoundPastCutoff,
          obsolete_keys_found_past_cutoff_);
    }
  }
}

bool DocRowwiseIterator::IsFetchedRowStatic() const {
  return fetched_row_static_;
}

Result<dockv::SubDocKey> DocRowwiseIterator::GetSubDocKey(ReadKey read_key) {
  if (!is_initialized_) {
    return STATUS(Corruption, "Iterator not initialized.");
  }

  bool handled = false;
  switch (read_key) {
    case ReadKey::kNext:
      if (!VERIFY_RESULT(table_type_ == TableType::PGSQL_TABLE_TYPE
                           ? PgFetchNext(nullptr) : FetchNext(nullptr))) {
        DVLOG(3) << "No Next SubDocKey";
        return dockv::SubDocKey();
      }
      handled = true;
      break;
    case ReadKey::kCurrent:
      handled = true;
      break;
  }
  if (!handled) {
    FATAL_INVALID_ENUM_VALUE(ReadKey, read_key);
  }

  DocKey doc_key;
  RETURN_NOT_OK(doc_key.FullyDecodeFrom(row_key_));
  auto sub_doc_key = dockv::SubDocKey(doc_key, read_operation_data_.read_time.read);
  DVLOG(3) << "Next SubDocKey: " << sub_doc_key.ToString();
  return sub_doc_key;
}

Slice DocRowwiseIterator::GetTupleId() const {
  // Return tuple id without cotable id / colocation id if any.
  Slice tuple_id = row_key_;
  if (tuple_id.starts_with(dockv::KeyEntryTypeAsChar::kTableId)) {
    tuple_id.remove_prefix(1 + kUuidSize);
  } else if (tuple_id.starts_with(dockv::KeyEntryTypeAsChar::kColocationId)) {
    tuple_id.remove_prefix(1 + sizeof(ColocationId));
  }
  return tuple_id;
}

Slice DocRowwiseIterator::GetRowKey() const {
  return row_key_;
}

void DocRowwiseIterator::SeekTuple(Slice tuple_id, docdb::UpdateFilterKey update_filter_key) {
  // If cotable id / colocation id is present in the table schema, then
  // we need to prepend it in the tuple key to seek.
  if (schema_->has_cotable_id() || schema_->has_colocation_id()) {
    uint32_t size = schema_->has_colocation_id() ? sizeof(ColocationId) : kUuidSize;
    if (!tuple_key_) {
      tuple_key_.emplace();
      tuple_key_->Reserve(1 + size + tuple_id.size());

      if (schema_->has_cotable_id()) {
        std::string bytes;
        schema_->cotable_id().EncodeToComparable(&bytes);
        tuple_key_->AppendKeyEntryType(dockv::KeyEntryType::kTableId);
        tuple_key_->AppendRawBytes(bytes);
      } else {
        tuple_key_->AppendKeyEntryType(dockv::KeyEntryType::kColocationId);
        tuple_key_->AppendUInt32(schema_->colocation_id());
      }
    } else {
      tuple_key_->Truncate(1 + size);
    }
    tuple_key_->AppendRawBytes(tuple_id);
    tuple_id = *tuple_key_;
  }
  if (update_filter_key) {
    UpdateFilterKey(tuple_id);
  }
  Seek(tuple_id);

  row_key_.Clear();
}

Result<bool> DocRowwiseIterator::FetchTuple(Slice tuple_id, qlexpr::QLTableRow* row) {
  return VERIFY_RESULT(FetchNext(row)) && GetTupleId() == tuple_id;
}

Slice DocRowwiseIterator::shared_key_prefix() const {
  return doc_read_context_.shared_key_prefix();
}

Slice DocRowwiseIterator::upperbound() const {
  return doc_read_context_.upperbound();
}

Status DocRowwiseIterator::InitIterKey(Slice key, bool full_row) {
  row_key_.Reset(key);
  VLOG_WITH_FUNC(4) << " Current row key is " << row_key_ << ", full_row: " << full_row;

  constexpr auto kUninitializedHashPartSize = std::numeric_limits<size_t>::max();

  size_t hash_part_size = kUninitializedHashPartSize;
  if (!full_row) {
    const auto dockey_sizes = VERIFY_RESULT(DocKey::EncodedHashPartAndDocKeySizes(
        row_key_.AsSlice()));
    row_key_.data().Truncate(dockey_sizes.doc_key_size);
    hash_part_size = dockey_sizes.hash_part_size;
    key = row_key_;
  }

  if (!schema_->has_statics()) {
    fetched_row_static_ = false;
  } else {
    // There are hash group part finished with kGroupEnd and range group part finished with
    // kGroupEnd.
    // Static row has empty range group.
    // So there are no bytes between hash group end and range groups end.
    // And we have 2 kGroupEnds at the end.
    // So row_key_ always has one kGroupEnd mark at the end. So we are checking only for
    // previous mark, that would mean that we 2 kGroupEnd at the end.
    if (key.size() < 2 || key.end()[-2] != dockv::KeyEntryTypeAsChar::kGroupEnd) {
      fetched_row_static_ = false;
    } else {
      // It is not guaranteed that previous mark belongs to key entry type, it could be
      // just the last part of the range column value. So have to decode key from the start to be
      // sure that we have empty range part.
      if (hash_part_size == kUninitializedHashPartSize) {
        hash_part_size = VERIFY_RESULT(DocKey::EncodedHashPartAndDocKeySizes(key)).hash_part_size;
      }

      // If range group is empty, then it contains just kGroupEnd.
      fetched_row_static_ = hash_part_size + 1 == key.size();
    }
  }

  return Status::OK();
}

Status DocRowwiseIterator::CopyKeyColumnsToRow(
    const dockv::ReaderProjection& projection, qlexpr::QLTableRow* row) {
  return DoCopyKeyColumnsToRow(projection, row);
}

Status DocRowwiseIterator::CopyKeyColumnsToRow(
    const dockv::ReaderProjection& projection, dockv::PgTableRow* row) {
  if (!row) {
    return Status::OK();
  }
  if (!pg_key_decoder_) {
    pg_key_decoder_.emplace(doc_read_context_.schema(), projection);
  }
  return pg_key_decoder_->Decode(
      row_key_.AsSlice().WithoutPrefix(doc_read_context_.key_prefix_encoded_len()), row);
}

template <class Row>
Status DocRowwiseIterator::DoCopyKeyColumnsToRow(
    const dockv::ReaderProjection& projection, Row* row) {
  if (projection.num_key_columns == 0 || !row) {
    return Status::OK();
  }

  const auto& schema = *schema_;
  // In the release mode we just skip key prefix encoded len, in debug mode we decode this prefix,
  // and check that number of decoded bytes matches key prefix encoded len.
#ifdef NDEBUG
  dockv::DocKeyDecoder decoder(
      row_key_.AsSlice().WithoutPrefix(doc_read_context_.key_prefix_encoded_len()));
#else
  dockv::DocKeyDecoder decoder(row_key_);
  RETURN_NOT_OK(decoder.DecodeCotableId());
  RETURN_NOT_OK(decoder.DecodeColocationId());
  RETURN_NOT_OK(decoder.DecodeHashCode());
  CHECK_EQ(doc_read_context_.key_prefix_encoded_len(),
           decoder.left_input().data() - row_key_.data().data());
#endif

  // Populate the key column values from the doc key. The key column values in doc key were
  // written in the same order as in the table schema (see DocKeyFromQLKey). If the range columns
  // are present, read them also.
  const auto projected_key_begin = projection.columns.begin();
  auto projected_column = projected_key_begin;
  const auto projected_key_end = projected_column + projection.num_key_columns;
  dockv::KeyEntryValue key_entry_value;
  if (schema.num_hash_key_columns()) {
    for (size_t schema_idx = 0; schema_idx != schema.num_hash_key_columns(); ++schema_idx) {
      if (projected_column->id == schema.column_id(schema_idx)) {
        RETURN_NOT_OK(StoreValue(
            schema, *projected_column, projected_column - projected_key_begin, &decoder, row));
        if (++projected_column == projected_key_end) {
          return Status::OK();
        }
      } else {
        RETURN_NOT_OK(decoder.DecodeKeyEntryValue());
      }
    }
    RETURN_NOT_OK(decoder.ConsumeGroupEnd());
  }
  if (fetched_row_static_) {
    // Don't have range columns in static rows.
    return Status::OK();
  }

  for (size_t schema_idx = schema.num_hash_key_columns(); schema_idx != schema.num_key_columns();
       ++schema_idx) {
    if (projected_column->id == schema.column_id(schema_idx)) {
      RETURN_NOT_OK(StoreValue(
          schema, *projected_column, projected_column - projected_key_begin, &decoder, row));
      if (++projected_column == projected_key_end) {
        return Status::OK();
      }
    } else {
      RETURN_NOT_OK(decoder.DecodeKeyEntryValue());
    }
  }

  return STATUS_FORMAT(
      Corruption, "Fully decoded doc key $0 but part of key columns were not decoded: $1",
      row_key_,
      boost::make_iterator_range(projected_column, projected_key_end));
}

const dockv::SchemaPackingStorage& DocRowwiseIterator::schema_packing_storage() {
  return doc_read_context_.schema_packing_storage;
}

Result<DocHybridTime> DocRowwiseIterator::GetTableTombstoneTime(Slice root_doc_key) const {
  if (!doc_read_context_.schema().has_colocation_id() ||
      !GetAtomicFlag(&FLAGS_enable_colocated_table_tombstone_cache)) {
    return docdb::GetTableTombstoneTime(
        root_doc_key, doc_db_, txn_op_context_, read_operation_data_);
  }

  auto cached_tombstone_time = doc_read_context_.table_tombstone_time();
  if (cached_tombstone_time.has_value()) {
    return *cached_tombstone_time;
  }

  auto doc_ht = VERIFY_RESULT(docdb::GetTableTombstoneTime(
      root_doc_key, doc_db_, txn_op_context_, read_operation_data_));
  doc_read_context_.set_table_tombstone_time(doc_ht);
  return doc_ht;
}

Status DocRowwiseIterator::InitIterator(
    const BloomFilterOptions& bloom_filter,
    AvoidUselessNextInsteadOfSeek avoid_useless_next_instead_of_seek,
    const rocksdb::QueryId query_id,
    std::shared_ptr<rocksdb::ReadFileFilter> file_filter) {
  if (table_type_ == TableType::PGSQL_TABLE_TYPE) {
    ConfigureForYsql();
  }

  // Configure usage of fast backward scan. This must be done before creating of the intent
  // aware iterator and when doc_mode_ is already set.
  // TODO(#22371) fast-backward-scan is supported for flat doc reader only.
  if (FLAGS_use_fast_backward_scan && !is_forward_scan_ && doc_mode_ == DocMode::kFlat) {
    use_fast_backward_scan_ = true;
    VLOG_WITH_FUNC(1) << "Using FAST BACKWARD scan";
  }

  DCHECK(!db_iter_) << "InitIterator should be called only once";

  db_iter_ = CreateIntentAwareIterator(
      doc_db_,
      bloom_filter,
      query_id,
      txn_op_context_,
      read_operation_data_,
      file_filter,
      nullptr /* iterate_upper_bound */,
      FastBackwardScan{use_fast_backward_scan_},
      avoid_useless_next_instead_of_seek);
  InitResult();

  const auto scan_choices_has_upperbound =
      scan_choices_ &&
      VERIFY_RESULT(scan_choices_->PrepareIterator(
          *db_iter_, doc_read_context_.table_key_prefix()));

  if (!scan_choices_has_upperbound) {
    auto prefix = shared_key_prefix();
    if (is_forward_scan_ && has_bound_key_ &&
        bound_key_.data().data()[0] != dockv::KeyEntryTypeAsChar::kHighest) {
      DCHECK(bound_key_.AsSlice().starts_with(prefix))
          << "Bound key: " << bound_key_.AsSlice().ToDebugHexString()
          << ", prefix: " << prefix.ToDebugHexString();
      upperbound_scope_.emplace(bound_key_, db_iter_.get());
    } else {
      DCHECK(!upperbound().empty());
      upperbound_scope_.emplace(upperbound(), db_iter_.get());
    }
  }

  if (use_fast_backward_scan_) {
    auto lower_bound = shared_key_prefix();
    // TODO(fast-backward-scan): do we need to consider bound_key_ here?
    if (lower_bound.empty()) {
      static const auto kMinByte = dockv::KeyEntryTypeAsChar::kLowest;
      lower_bound = Slice(&kMinByte, 1);
    }
    lowerbound_scope_.emplace(lower_bound, db_iter_.get());
  }

  VLOG_WITH_FUNC(4) << "Initialization done";
  return Status::OK();
}

void DocRowwiseIterator::ConfigureForYsql() {
  ignore_ttl_ = true;
  if (FLAGS_ysql_use_flat_doc_reader) {
    doc_mode_ = DocMode::kFlat;
  }
}

void DocRowwiseIterator::InitResult() {
  if (doc_mode_ == DocMode::kFlat) {
    row_ = std::nullopt;
  } else {
    row_.emplace();
  }
}

void DocRowwiseIterator::Refresh(SeekFilter seek_filter) {
  done_ = false;
  seek_filter_ = seek_filter;
}

void DocRowwiseIterator::UpdateFilterKey(Slice user_key_for_filter) {
  DCHECK(!scan_choices_ || scan_choices_->BloomFilterOptions().mode() != BloomFilterMode::kInactive)
      << "Mode: " << scan_choices_->BloomFilterOptions().mode();
  db_iter_->UpdateFilterKey(user_key_for_filter);
}

void DocRowwiseIterator::Seek(Slice key) {
  VLOG_WITH_FUNC(3) << key << "/" << dockv::DocKey::DebugSliceToString(key);

  DCHECK(!done_);

  prev_doc_found_ = DocReaderResult::kNotFound;

  // We do not have values before dockv::KeyEntryTypeAsChar::kNullLow, but there is
  // kLowest = 0 that is used to mark -Inf bound.
  // Here we could safely interpret any key before kNullLow as empty.
  // Another option would be changing kLowest value to kNullLow. But there are much more scenarios
  // that could be affected and should be tested.
  if (!key.empty() && key[0] >= dockv::KeyEntryTypeAsChar::kNullLow) {
    db_iter_->Seek(key, seek_filter_, Full::kTrue);
    return;
  }

  auto shared_prefix = shared_key_prefix();
  if (!shared_prefix.empty()) {
    db_iter_->Seek(shared_prefix, seek_filter_, Full::kFalse);
    return;
  }

  const auto null_low = dockv::KeyEntryTypeAsChar::kNullLow;
  db_iter_->Seek(Slice(&null_low, 1), seek_filter_, Full::kFalse);
}

inline void DocRowwiseIterator::SeekToDocKeyPrefix(Slice doc_key_prefix) {
  VLOG_WITH_FUNC(3) << "Seeking to " << doc_key_prefix.ToDebugHexString();
  prev_doc_found_ = DocReaderResult::kNotFound;
  db_iter_->Seek(doc_key_prefix, SeekFilter::kAll, Full::kFalse);
  row_key_.Clear();
}

inline void DocRowwiseIterator::SeekPrevDocKey(Slice key) {
  // TODO consider adding an operator bool to DocKey to use instead of empty() here.
  // TODO(fast-backward-scan) do we need to play with prev_doc_found_?
  if (!key.empty()) {
    db_iter_->SeekPrevDocKey(key);
  } else {
    db_iter_->SeekToLastDocKey();
  }
}

Status DocRowwiseIterator::AdvanceIteratorToNextDesiredRow(bool row_finished,
                                                           bool current_fetched_row_skipped) {
  if (seek_filter_ == SeekFilter::kAll && !IsFetchedRowStatic() &&
      VERIFY_RESULT(scan_choices_->AdvanceToNextRow(&row_key_, *db_iter_,
                                                    current_fetched_row_skipped))) {
    return Status::OK();
  }
  if (!is_forward_scan_) {
    VLOG(4) << __PRETTY_FUNCTION__ << " setting as PrevDocKey";
    RSTATUS_DCHECK_EQ(seek_filter_, SeekFilter::kAll, IllegalState,
                      "Backward scan is not supported with this filter");
    db_iter_->PrevDocKey(row_key_);
  } else if (row_finished) {
    db_iter_->Revalidate(seek_filter_);
  } else {
    db_iter_->SeekOutOfSubDoc(seek_filter_, &row_key_);
  }

  return Status::OK();
}

Result<bool> DocRowwiseIterator::PgFetchNext(dockv::PgTableRow* table_row) {
  if (table_row) {
    table_row->Reset();
  }
  return FetchNextImpl(table_row);
}

Result<bool> DocRowwiseIterator::DoFetchNext(
    qlexpr::QLTableRow* table_row,
    const dockv::ReaderProjection* projection,
    qlexpr::QLTableRow* static_row,
    const dockv::ReaderProjection* static_projection) {
  return FetchNextImpl(QLTableRowPair{table_row, projection, static_row, static_projection});
}

template <class TableRow>
Result<bool> DocRowwiseIterator::FetchNextImpl(TableRow table_row) {
  VLOG_WITH_FUNC(4) << "done_: " << done_;

  if (done_) {
    return false;
  }

  if (prev_doc_found_ != DocReaderResult::kNotFound) {
    RETURN_NOT_OK(AdvanceIteratorToNextDesiredRow(
        prev_doc_found_ == DocReaderResult::kFoundAndFinished, false));
    prev_doc_found_ = DocReaderResult::kNotFound;
  }

  RETURN_NOT_OK(pending_op_ref_.GetAbortedStatus());

  if (PREDICT_FALSE(FLAGS_TEST_fetch_next_delay_ms > 0)) {
    const auto column_names = schema().column_names();
    if (FLAGS_TEST_fetch_next_delay_column.empty() ||
        std::find(column_names.begin(), column_names.end(), FLAGS_TEST_fetch_next_delay_column) !=
            column_names.end()) {
      YB_LOG_EVERY_N_SECS(INFO, 1)
          << "Delaying read for " << FLAGS_TEST_fetch_next_delay_ms << " ms"
          << ", schema column names: " << AsString(column_names);
      SleepFor(FLAGS_TEST_fetch_next_delay_ms * 1ms);
    }
  }

  bool first_iteration = true;
  for (;;) {
    RETURN_NOT_OK(deadline_info_.CheckDeadlinePassed());

    if (scan_choices_->Finished()) {
      done_ = true;
      return false;
    }

    const auto& key_data = VERIFY_RESULT_REF(db_iter_->Fetch());
    if (!key_data) {
      // It could happen that iterator did not find anything because of upper bound limit from
      // scan choices. So need to update it and retry.
      if (seek_filter_ == SeekFilter::kAll && !IsFetchedRowStatic() &&
          VERIFY_RESULT(scan_choices_->AdvanceToNextRow(nullptr, *db_iter_, true))) {
        continue;
      }
      done_ = true;
      return false;
    }

    VLOG(4) << "*fetched_key is " << dockv::SubDocKey::DebugSliceToString(key_data.key);
    if (debug_dump_) {
      LOG(INFO)
          << __func__ << ", fetched key: " << dockv::SubDocKey::DebugSliceToString(key_data.key)
          << ", " << key_data.key.ToDebugHexString();
    }

    // The iterator is positioned by the previous GetSubDocument call (which places the iterator
    // outside the previous doc_key). Ensure the iterator is pushed forward/backward indeed. We
    // check it here instead of after GetSubDocument() below because we want to avoid the extra
    // expensive FetchKey() call just to fetch and validate the key.
    auto row_key = row_key_.AsSlice();
    if (!first_iteration &&
        (is_forward_scan_ ? row_key.compare(key_data.key) >= 0
                          : row_key.compare(key_data.key) <= 0)) {
      // TODO -- could turn this check off in TPCC?
      auto status = STATUS_FORMAT(
          Corruption, "Infinite loop detected at $0, row key: $1",
          key_data.key.ToDebugString(), row_key.ToDebugString());
      LOG(DFATAL) << status;
      return status;
    }
    first_iteration = false;

    RETURN_NOT_OK(InitIterKey(key_data.key, dockv::IsFullRowValue(key_data.value)));
    row_key = row_key_.AsSlice();

    if (has_bound_key_ && is_forward_scan_ == (row_key.compare(bound_key_) >= 0)) {
      VLOG(3) << "Done since " << dockv::SubDocKey::DebugSliceToString(key_data.key)
              << " out of bound: " << dockv::SubDocKey::DebugSliceToString(bound_key_);
      done_ = true;
      return false;
    }

    VLOG(4) << " sub_doc_key part of iter_key_ is " << dockv::DocKey::DebugSliceToString(row_key);

    bool is_static_column = IsFetchedRowStatic();
    if (!is_static_column &&
        !VERIFY_RESULT(scan_choices_->InterestedInRow(&row_key_, *db_iter_))) {
      continue;
    }

    if (doc_reader_ == nullptr) {
      doc_reader_ = std::make_unique<DocDBTableReader>(
          db_iter_.get(), deadline_info_, &projection_, table_type_,
          schema_packing_storage(), schema(), use_fast_backward_scan_);
      RETURN_NOT_OK(doc_reader_->UpdateTableTombstoneTime(
          VERIFY_RESULT(GetTableTombstoneTime(row_key))));
      if (!ignore_ttl_) {
        doc_reader_->SetTableTtl(schema());
      }
    }

    if (doc_mode_ == DocMode::kGeneric) {
      DCHECK_EQ(row_->type(), dockv::ValueEntryType::kObject);
      row_->object_container().clear();
    }

    const auto write_time = key_data.write_time;
    const auto doc_found = VERIFY_RESULT(FetchRow(key_data, table_row));
    // Use the write_time of the entire row.
    // May lose some precision by not examining write time of every column.
    IncrementKeyFoundStats(doc_found == DocReaderResult::kNotFound, write_time);

    if (doc_found != DocReaderResult::kNotFound) {
      RETURN_NOT_OK(FillRow(table_row));
      prev_doc_found_ = doc_found;
      break;
    }

    RETURN_NOT_OK(AdvanceIteratorToNextDesiredRow(/* row_finished= */ false,
                                                  /* current_fetched_row_skipped */ true));
  }
  return true;
}

Result<DocReaderResult> DocRowwiseIterator::FetchRow(
    const FetchedEntry& fetched_entry, dockv::PgTableRow* table_row) {
  CHECK_NE(doc_mode_, DocMode::kGeneric) << "Table type: " << table_type_;
  return doc_reader_->GetFlat(&row_key_.data(), fetched_entry, table_row);
}

Result<DocReaderResult> DocRowwiseIterator::FetchRow(
    const FetchedEntry& fetched_entry, QLTableRowPair table_row) {
  return doc_mode_ == DocMode::kFlat
      ? doc_reader_->GetFlat(&row_key_.data(), fetched_entry, table_row.table_row)
      : doc_reader_->Get(&row_key_.data(), fetched_entry, &*row_);
}

Status DocRowwiseIterator::FillRow(dockv::PgTableRow* out) {
  return CopyKeyColumnsToRow(projection_, out);
}

Status DocRowwiseIterator::FillRow(QLTableRowPair out) {
  if (!out.table_row) {
    return Status::OK();
  }

  if (!out.static_row) {
    return FillRow(out.table_row, out.projection);
  }
  if (IsFetchedRowStatic()) {
    return FillRow(out.static_row, out.static_projection);
  }

  out.table_row->Clear();
  return FillRow(out.table_row, out.projection);
}

std::string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

Result<ReadRestartData> DocRowwiseIterator::GetReadRestartData() {
  return db_iter_->GetReadRestartData();
}

HybridTime DocRowwiseIterator::TEST_MaxSeenHt() {
  return db_iter_->TEST_MaxSeenHt();
}

Status DocRowwiseIterator::FillRow(
    qlexpr::QLTableRow* table_row, const dockv::ReaderProjection* projection_opt) {
  VLOG(4) << __PRETTY_FUNCTION__;

  const auto& projection = projection_opt ? *projection_opt : projection_;

  if (projection.columns.empty()) {
    return Status::OK();
  }

  // Copy required key columns to table_row.
  RETURN_NOT_OK(CopyKeyColumnsToRow(projection, table_row));

  if (doc_mode_ == DocMode::kFlat) {
    return Status::OK();
  }

  DVLOG_WITH_FUNC(4) << "subdocument: " << AsString(*row_);
  const auto& schema = this->schema();
  for (const auto& column : projection.value_columns()) {
    const auto* source = row_->GetChild(column.subkey);
    auto& dest = table_row->AllocColumn(column.id);
    if (!source) {
      dest.value.Clear();
      continue;
    }
    source->ToQLValuePB(VERIFY_RESULT_REF(schema.column_by_id(column.id)).type(), &dest.value);
    dest.ttl_seconds = source->GetTtl();
    if (source->IsWriteTimeSet()) {
      dest.write_time = source->GetWriteTime();
    }
  }

  VLOG_WITH_FUNC(4) << "Returning row: " << table_row->ToString();

  return Status::OK();
}

bool DocRowwiseIterator::LivenessColumnExists() const {
  CHECK_NE(doc_mode_, DocMode::kFlat) << "Flat doc mode not supported yet";
  const auto* subdoc = row_->GetChild(dockv::KeyEntryValue::kLivenessColumn);
  return subdoc != nullptr && subdoc->value_type() != dockv::ValueEntryType::kInvalid;
}

}  // namespace yb::docdb
