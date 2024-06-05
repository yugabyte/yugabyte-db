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

#include "yb/docdb/doc_rowwise_iterator_base.h"
#include <iterator>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "yb/docdb/doc_ql_filefilter.h"
#include "yb/docdb/docdb_compaction_context.h"
#include "yb/docdb/scan_choices.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/expiration.h"
#include "yb/dockv/pg_row.h"

#include "yb/qlexpr/doc_scanspec_util.h"
#include "yb/qlexpr/ql_expr.h"

#include "yb/tablet/tablet_metrics.h"

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/strongly_typed_bool.h"

using std::string;

DEPRECATE_FLAG(bool, use_offset_based_key_decoding, "02_2024");

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

/*Status StoreValue(
    const Schema& schema, const dockv::ProjectedColumn& column, size_t col_idx,
    dockv::DocKeyDecoder* decoder, dockv::PgTableRow* row) {
  return row->DecodeKey(col_idx, decoder->mutable_input());
}*/

} // namespace

DocRowwiseIteratorBase::DocRowwiseIteratorBase(
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
      projection_(projection) {
}

DocRowwiseIteratorBase::DocRowwiseIteratorBase(
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    const ReadOperationData& read_operation_data,
    ScopedRWOperation&& pending_op)
    : doc_read_context_(doc_read_context),
      schema_(&doc_read_context_.schema()),
      txn_op_context_(txn_op_context),
      read_operation_data_(read_operation_data),
      doc_db_(doc_db),
      pending_op_holder_(std::move(pending_op)),
      pending_op_ref_(pending_op_holder_),
      projection_(projection) {
}

DocRowwiseIteratorBase::DocRowwiseIteratorBase(
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
      projection_(projection) {
}

DocRowwiseIteratorBase::~DocRowwiseIteratorBase() {
  FinalizeKeyFoundStats();
}

void DocRowwiseIteratorBase::CheckInitOnce() {
  if (is_initialized_) {
    YB_LOG_EVERY_N_SECS(DFATAL, 3600)
        << "DocRowwiseIterator(" << this << ") has been already initialized\n"
        << GetStackTrace();
  }
  is_initialized_ = true;
}

void DocRowwiseIteratorBase::SetSchema(const Schema& schema) {
  LOG_IF(DFATAL, is_initialized_) << "Iterator has been already initialized in " << __func__;
  schema_ = &schema;
}

void DocRowwiseIteratorBase::InitForTableType(
    TableType table_type, Slice sub_doc_key, SkipSeek skip_seek) {
  CheckInitOnce();
  table_type_ = table_type;
  ignore_ttl_ = (table_type_ == TableType::PGSQL_TABLE_TYPE);
  InitIterator();

  if (!sub_doc_key.empty()) {
    row_key_.Reset(sub_doc_key);
  } else {
    dockv::DocKeyEncoder(&row_key_).Schema(*schema_);
  }
  if (!skip_seek) {
    Seek(row_key_);
  }
  has_bound_key_ = false;

  scan_choices_ = ScanChoices::CreateEmpty();
}

Status DocRowwiseIteratorBase::Init(const qlexpr::YQLScanSpec& doc_spec, SkipSeek skip_seek) {
  table_type_ = doc_spec.client_type() == YQL_CLIENT_CQL ? TableType::YQL_TABLE_TYPE
                                                         : TableType::PGSQL_TABLE_TYPE;
  ignore_ttl_ = table_type_ == TableType::PGSQL_TABLE_TYPE;

  CheckInitOnce();
  is_forward_scan_ = doc_spec.is_forward_scan();

  VLOG(4) << "Initializing iterator direction: " << (is_forward_scan_ ? "FORWARD" : "BACKWARD");

  auto bounds = doc_spec.bounds();
  VLOG(4) << "DocKey Bounds " << DocKey::DebugSliceToString(bounds.lower.AsSlice()) << ", "
          << DocKey::DebugSliceToString(bounds.upper.AsSlice());

  // TODO(bogdan): decide if this is a good enough heuristic for using blooms for scans.
  const bool is_fixed_point_get =
      !bounds.lower.empty() &&
      VERIFY_RESULT(HashedOrFirstRangeComponentsEqual(bounds.lower, bounds.upper));
  const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER
                                       : BloomFilterMode::DONT_USE_BLOOM_FILTER;

  if (is_forward_scan_) {
    has_bound_key_ = !bounds.upper.empty();
    if (has_bound_key_) {
      bound_key_ = bounds.upper;
    }
  } else {
    has_bound_key_ = !bounds.lower.empty();
    if (has_bound_key_) {
      bound_key_ = bounds.lower;
    }
  }

  InitIterator(mode, bounds.lower.AsSlice(), doc_spec.QueryId(), CreateFileFilter(doc_spec));

  if (has_bound_key_) {
    if (is_forward_scan_) {
      bounds.upper = bound_key_;
    } else {
      bounds.lower = bound_key_;
    }
  }
  scan_choices_ = ScanChoices::Create(*schema_, doc_spec, bounds);
  if (!skip_seek) {
    if (is_forward_scan_) {
      Seek(bounds.lower);
    } else {
      PrevDocKey(bounds.upper);
    }
  }

  return Status::OK();
}

void DocRowwiseIteratorBase::IncrementKeyFoundStats(
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

void DocRowwiseIteratorBase::FinalizeKeyFoundStats() {
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

bool DocRowwiseIteratorBase::IsFetchedRowStatic() const {
  return fetched_row_static_;
}

Status DocRowwiseIteratorBase::GetNextReadSubDocKey(dockv::SubDocKey* sub_doc_key) {
  if (!is_initialized_) {
    return STATUS(Corruption, "Iterator not initialized.");
  }

  // There are no more rows to fetch, so no next SubDocKey to read.
  auto res = table_type_ == TableType::PGSQL_TABLE_TYPE ? PgFetchNext(nullptr) : FetchNext(nullptr);
  if (!VERIFY_RESULT(std::move(res))) {
    DVLOG(3) << "No Next SubDocKey";
    return Status::OK();
  }

  DocKey doc_key;
  RETURN_NOT_OK(doc_key.FullyDecodeFrom(row_key_));
  *sub_doc_key = dockv::SubDocKey(doc_key, read_operation_data_.read_time.read);
  DVLOG(3) << "Next SubDocKey: " << sub_doc_key->ToString();
  return Status::OK();
}

Slice DocRowwiseIteratorBase::GetTupleId() const {
  // Return tuple id without cotable id / colocation id if any.
  Slice tuple_id = row_key_;
  if (tuple_id.starts_with(dockv::KeyEntryTypeAsChar::kTableId)) {
    tuple_id.remove_prefix(1 + kUuidSize);
  } else if (tuple_id.starts_with(dockv::KeyEntryTypeAsChar::kColocationId)) {
    tuple_id.remove_prefix(1 + sizeof(ColocationId));
  }
  return tuple_id;
}

void DocRowwiseIteratorBase::SeekTuple(Slice tuple_id) {
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
    Seek(*tuple_key_);
  } else {
    Seek(tuple_id);
  }

  row_key_.Clear();
}

Result<bool> DocRowwiseIteratorBase::FetchTuple(Slice tuple_id, qlexpr::QLTableRow* row) {
  return VERIFY_RESULT(FetchNext(row)) && GetTupleId() == tuple_id;
}

Slice DocRowwiseIteratorBase::shared_key_prefix() const {
  return doc_read_context_.shared_key_prefix();
}

Slice DocRowwiseIteratorBase::upperbound() const {
  return doc_read_context_.upperbound();
}

Status DocRowwiseIteratorBase::InitIterKey(Slice key, bool full_row) {
  row_key_.Reset(key);
  VLOG_WITH_FUNC(4) << " Current row key is " << row_key_ << ", full_row: " << full_row;

  constexpr auto kUninitializedHashPartSize = std::numeric_limits<size_t>::max();

  size_t hash_part_size = kUninitializedHashPartSize;
  if (!full_row) {
    const auto dockey_sizes = VERIFY_RESULT(DocKey::EncodedHashPartAndDocKeySizes(
        row_key_.AsSlice()));
    row_key_.mutable_data()->Truncate(dockey_sizes.doc_key_size);
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

Status DocRowwiseIteratorBase::CopyKeyColumnsToRow(
    const dockv::ReaderProjection& projection, qlexpr::QLTableRow* row) {
  return DoCopyKeyColumnsToRow(projection, row);
}

Status DocRowwiseIteratorBase::CopyKeyColumnsToRow(
    const dockv::ReaderProjection& projection, dockv::PgTableRow* row) {
//  RETURN_NOT_OK(DoCopyKeyColumnsToRow(projection, row));
  if (!row) {
    return Status::OK();
  }
  if (!pg_key_decoder_) {
    pg_key_decoder_.emplace(doc_read_context_.schema(), projection);
  }
  return pg_key_decoder_->Decode(
      row_key_.AsSlice().WithoutPrefix(doc_read_context_.key_prefix_encoded_len()), row);
/*
  dockv::PgTableRow temp_row(row->projection());
  CHECK_OK(doc_read_context_.pg_row_key_decoder().Decode(
      row_key_.AsSlice().WithoutPrefix(doc_read_context_.key_prefix_encoded_len()),
      &temp_row));
  for (size_t i = 0; i != row->projection().num_key_columns; ++i) {
    auto o1 = row->GetValueByIndex(i);
    auto o2 = temp_row.GetValueByIndex(i);
    if (!o1 || !o2) {
      CHECK(!o1 && !o2) << "index: " << i;
      continue;
    }
    auto v1 = o1->ToQLValuePB(row->projection().columns[i].data_type);
    auto v2 = o1->ToQLValuePB(row->projection().columns[i].data_type);
    CHECK_EQ(v1.ShortDebugString(), v2.ShortDebugString()) << "index: " << i;
  }
  return Status::OK();*/
}

template <class Row>
Status DocRowwiseIteratorBase::DoCopyKeyColumnsToRow(
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

const dockv::SchemaPackingStorage& DocRowwiseIteratorBase::schema_packing_storage() {
  return doc_read_context_.schema_packing_storage;
}

Result<DocHybridTime> DocRowwiseIteratorBase::GetTableTombstoneTime(Slice root_doc_key) const {
  return docdb::GetTableTombstoneTime(
      root_doc_key, doc_db_, txn_op_context_, read_operation_data_);
}

}  // namespace yb::docdb
