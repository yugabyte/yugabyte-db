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

#include "yb/common/ql_expr.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/doc_scanspec_util.h"
#include "yb/docdb/docdb_compaction_context.h"
#include "yb/dockv/expiration.h"
#include "yb/docdb/scan_choices.h"
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

// Primary key update in table group creates copy of existing data
// in same tablet (which uses a single RocksDB instance). During this
// update, we are updating the source schema as well (which is not required).
// Until we figure out the correct approach to handle it, we are disabling
// offset based key decoding by default.
DEFINE_RUNTIME_bool(
    use_offset_based_key_decoding, false, "Use Offset based key decoding for reader.");

#define ASSIGN_AND_RETURN_NOT_OK(s) do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return AssignHasNextStatus(MoveStatus(std::move(_s))); \
  } while (false)

namespace yb::docdb {

using dockv::DocKey;

DocRowwiseIteratorBase::DocRowwiseIteratorBase(
    const Schema& projection,
    std::reference_wrapper<const DocReadContext>
        doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter,
    boost::optional<size_t>
        end_referenced_key_column_index)
    : projection_(projection),
      doc_read_context_(doc_read_context),
      txn_op_context_(txn_op_context),
      deadline_(deadline),
      read_time_(read_time),
      doc_db_(doc_db),
      pending_op_(pending_op_counter),
      end_referenced_key_column_index_(end_referenced_key_column_index.get_value_or(
          doc_read_context_.schema.num_key_columns())) {
  SetupProjectionSubkeys();
}

DocRowwiseIteratorBase::DocRowwiseIteratorBase(
    std::unique_ptr<Schema> projection,
    std::reference_wrapper<const DocReadContext>
        doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter,
    boost::optional<size_t>
        end_referenced_key_column_index)
    : projection_owner_(std::move(projection)),
      projection_(*projection_owner_),
      doc_read_context_(doc_read_context),
      txn_op_context_(txn_op_context),
      deadline_(deadline),
      read_time_(read_time),
      doc_db_(doc_db),
      pending_op_(pending_op_counter),
      end_referenced_key_column_index_(end_referenced_key_column_index.get_value_or(
          doc_read_context_.schema.num_key_columns())) {
  SetupProjectionSubkeys();
}

DocRowwiseIteratorBase::DocRowwiseIteratorBase(
    std::unique_ptr<Schema> projection,
    std::shared_ptr<DocReadContext>
        doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter,
    boost::optional<size_t>
        end_referenced_key_column_index)
    : projection_owner_(std::move(projection)),
      projection_(*projection_owner_),
      doc_read_context_holder_(std::move(doc_read_context)),
      doc_read_context_(*doc_read_context_holder_),
      txn_op_context_(txn_op_context),
      deadline_(deadline),
      read_time_(read_time),
      doc_db_(doc_db),
      pending_op_(pending_op_counter),
      end_referenced_key_column_index_(end_referenced_key_column_index.get_value_or(
          doc_read_context_.schema.num_key_columns())) {
  SetupProjectionSubkeys();
}

void DocRowwiseIteratorBase::SetupProjectionSubkeys() {
  reader_projection_.reserve(projection_.num_columns() + 1);
  reader_projection_.push_back({dockv::KeyEntryValue::kLivenessColumn, nullptr});
  for (size_t i = projection_.num_key_columns(); i < projection_.num_columns(); i++) {
    reader_projection_.push_back({
        .subkey = dockv::KeyEntryValue::MakeColumnId(projection_.column_id(i)),
        .type = projection_.column(i).type(),
    });
  }
  std::sort(
      reader_projection_.begin(), reader_projection_.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.subkey < rhs.subkey; });
}

DocRowwiseIteratorBase::~DocRowwiseIteratorBase() = default;

void DocRowwiseIteratorBase::CheckInitOnce() {
  if (is_initialized_) {
    YB_LOG_EVERY_N_SECS(DFATAL, 3600)
        << "DocRowwiseIterator(" << this << ") has been already initialized\n"
        << GetStackTrace();
  }
  is_initialized_ = true;
}

void DocRowwiseIteratorBase::Init(TableType table_type, const Slice& sub_doc_key) {
  CheckInitOnce();
  table_type_ = table_type;
  ignore_ttl_ = (table_type_ == TableType::PGSQL_TABLE_TYPE);
  InitIterator();

  if (!sub_doc_key.empty()) {
    row_key_ = sub_doc_key;
  } else {
    dockv::DocKeyEncoder(&iter_key_).Schema(doc_read_context_.schema);
    row_key_ = iter_key_;
  }
  Seek(row_key_);
  has_bound_key_ = false;
}

template <class T>
Status DocRowwiseIteratorBase::DoInit(const T& doc_spec) {
  CheckInitOnce();
  is_forward_scan_ = doc_spec.is_forward_scan();

  VLOG(4) << "Initializing iterator direction: " << (is_forward_scan_ ? "FORWARD" : "BACKWARD");

  auto lower_doc_key = VERIFY_RESULT(doc_spec.LowerBound());
  auto upper_doc_key = VERIFY_RESULT(doc_spec.UpperBound());
  VLOG(4) << "DocKey Bounds " << DocKey::DebugSliceToString(lower_doc_key.AsSlice()) << ", "
          << DocKey::DebugSliceToString(upper_doc_key.AsSlice());

  // TODO(bogdan): decide if this is a good enough heuristic for using blooms for scans.
  const bool is_fixed_point_get =
      !lower_doc_key.empty() &&
      VERIFY_RESULT(HashedOrFirstRangeComponentsEqual(lower_doc_key, upper_doc_key));
  const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER
                                       : BloomFilterMode::DONT_USE_BLOOM_FILTER;

  if (is_forward_scan_) {
    has_bound_key_ = !upper_doc_key.empty();
    if (has_bound_key_) {
      bound_key_ = std::move(upper_doc_key);
    }
  } else {
    has_bound_key_ = !lower_doc_key.empty();
    if (has_bound_key_) {
      bound_key_ = std::move(lower_doc_key);
    }
  }

  InitIterator(mode, lower_doc_key.AsSlice(), doc_spec.QueryId(), doc_spec.CreateFileFilter());

  scan_choices_ = ScanChoices::Create(
      doc_read_context_.schema, doc_spec,
      !is_forward_scan_ && has_bound_key_ ? bound_key_ : lower_doc_key,
      is_forward_scan_ && has_bound_key_ ? bound_key_ : upper_doc_key);
  if (is_forward_scan_) {
    Seek(lower_doc_key);
  } else {
    PrevDocKey(upper_doc_key);
  }

  return Status::OK();
}

Status DocRowwiseIteratorBase::Init(const dockv::YQLScanSpec& spec) {
  table_type_ = spec.client_type() == YQL_CLIENT_CQL ? TableType::YQL_TABLE_TYPE
                                                     : TableType::PGSQL_TABLE_TYPE;
  ignore_ttl_ = (table_type_ == TableType::PGSQL_TABLE_TYPE);
  return DoInit(spec);
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

void DocRowwiseIteratorBase::Done() {
  done_ = true;
  if (!doc_db_.metrics || !keys_found_) {
    return;
  }

  doc_db_.metrics->docdb_keys_found->IncrementBy(keys_found_);
  if (obsolete_keys_found_) {
    doc_db_.metrics->docdb_obsolete_keys_found->IncrementBy(obsolete_keys_found_);
    if (obsolete_keys_found_past_cutoff_) {
      doc_db_.metrics->docdb_obsolete_keys_found_past_cutoff->IncrementBy(
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
  if (!VERIFY_RESULT(FetchNext(nullptr))) {
    DVLOG(3) << "No Next SubDocKey";
    return Status::OK();
  }

  DocKey doc_key;
  RETURN_NOT_OK(doc_key.FullyDecodeFrom(row_key_));
  *sub_doc_key = dockv::SubDocKey(doc_key, read_time_.read);
  DVLOG(3) << "Next SubDocKey: " << sub_doc_key->ToString();
  return Status::OK();
}

Result<Slice> DocRowwiseIteratorBase::GetTupleId() const {
  // Return tuple id without cotable id / colocation id if any.
  Slice tuple_id = row_key_;
  if (tuple_id.starts_with(dockv::KeyEntryTypeAsChar::kTableId)) {
    tuple_id.remove_prefix(1 + kUuidSize);
  } else if (tuple_id.starts_with(dockv::KeyEntryTypeAsChar::kColocationId)) {
    tuple_id.remove_prefix(1 + sizeof(ColocationId));
  }
  return tuple_id;
}

void DocRowwiseIteratorBase::SeekTuple(const Slice& tuple_id) {
  // If cotable id / colocation id is present in the table schema, then
  // we need to prepend it in the tuple key to seek.
  if (doc_read_context_.schema.has_cotable_id() || doc_read_context_.schema.has_colocation_id()) {
    uint32_t size = doc_read_context_.schema.has_colocation_id() ? sizeof(ColocationId) : kUuidSize;
    if (!tuple_key_) {
      tuple_key_.emplace();
      tuple_key_->Reserve(1 + size + tuple_id.size());

      if (doc_read_context_.schema.has_cotable_id()) {
        std::string bytes;
        doc_read_context_.schema.cotable_id().EncodeToComparable(&bytes);
        tuple_key_->AppendKeyEntryType(dockv::KeyEntryType::kTableId);
        tuple_key_->AppendRawBytes(bytes);
      } else {
        tuple_key_->AppendKeyEntryType(dockv::KeyEntryType::kColocationId);
        tuple_key_->AppendUInt32(doc_read_context_.schema.colocation_id());
      }
    } else {
      tuple_key_->Truncate(1 + size);
    }
    tuple_key_->AppendRawBytes(tuple_id);
    Seek(*tuple_key_);
  } else {
    Seek(tuple_id);
  }

  iter_key_.Clear();
}

Result<bool> DocRowwiseIteratorBase::FetchTuple(const Slice& tuple_id, QLTableRow* row) {
  return VERIFY_RESULT(FetchNext(row)) && VERIFY_RESULT(GetTupleId()) == tuple_id;
}

Status DocRowwiseIteratorBase::AssignHasNextStatus(const Status& status) {
  has_next_status_ = status;
  return status;
}

Status DocRowwiseIteratorBase::InitIterKey(const Slice& key, bool full_row) {
  iter_key_.Reset(key);
  VLOG_WITH_FUNC(4) << " Current iter_key_ is " << iter_key_ << ", full_row: " << full_row;

  constexpr auto kUninitializedHashPartSize = std::numeric_limits<size_t>::max();

  size_t hash_part_size = kUninitializedHashPartSize;
  if (full_row) {
    row_key_ = iter_key_.AsSlice();
  } else {
    const auto dockey_sizes = DocKey::EncodedHashPartAndDocKeySizes(iter_key_);
    ASSIGN_AND_RETURN_NOT_OK(dockey_sizes);
    row_key_ = iter_key_.AsSlice().Prefix(dockey_sizes->doc_key_size);
    hash_part_size = dockey_sizes->hash_part_size;
  }

  if (!doc_read_context_.schema.has_statics()) {
    fetched_row_static_ = false;
  } else {
    // There are hash group part finished with kGroupEnd and range group part finished with
    // kGroupEnd.
    // Static row has empty range group.
    // So there are no bytes between hash group end and range groups end.
    // And we have 2 kGroupEnds at the end.
    // So row_key_ always has one kGroupEnd mark at the end. So we are checking only for
    // previous mark, that would mean that we 2 kGroupEnd at the end.
    if (row_key_.size() < 2 || row_key_.end()[-2] != dockv::KeyEntryTypeAsChar::kGroupEnd) {
      fetched_row_static_ = false;
    } else {
      // It is not guaranteed that previous mark belongs to key entry type, it could be
      // just the last part of the range column value. So have to decode key from the start to be
      // sure that we have empty range part.
      if (hash_part_size == kUninitializedHashPartSize) {
        auto sizes = DocKey::EncodedHashPartAndDocKeySizes(row_key_);
        ASSIGN_AND_RETURN_NOT_OK(sizes);
        hash_part_size = sizes->hash_part_size;
      }

      // If range group is empty, then it contains just kGroupEnd.
      fetched_row_static_ = hash_part_size + 1 == row_key_.size();
    }
  }

  return Status::OK();
}

namespace {

// Set primary key column values (hashed or range columns) in a QL row value map.
Status SetQLPrimaryKeyColumnValues(
    const Schema& schema,
    const size_t begin_index,
    const size_t column_count,
    const char* column_type,
    const size_t end_referenced_key_column_index,
    dockv::DocKeyDecoder* decoder,
    QLTableRow* table_row) {
  const auto end_group_index = begin_index + column_count;
  SCHECK_LE(
      end_group_index, schema.num_columns(), InvalidArgument,
      Format(
          "$0 primary key columns between positions $1 and $2 go beyond table columns $3",
          column_type, begin_index, begin_index + column_count - 1, schema.num_columns()));
  SCHECK_LE(
      end_referenced_key_column_index, schema.num_key_columns(), InvalidArgument,
      Format(
          "End reference key column index $0 is higher than num of key columns in schema $1",
          end_referenced_key_column_index, schema.num_key_columns()));

  dockv::KeyEntryValue key_entry_value;
  size_t col_idx = begin_index;
  for (; col_idx < std::min(end_group_index, end_referenced_key_column_index); ++col_idx) {
    const auto ql_type = schema.column(col_idx).type();
    QLTableColumn& column = table_row->AllocColumn(schema.column_id(col_idx));
    RETURN_NOT_OK(decoder->DecodeKeyEntryValue(&key_entry_value));
    key_entry_value.ToQLValuePB(ql_type, &column.value);
  }

  return col_idx == end_group_index ? decoder->ConsumeGroupEnd() : Status::OK();
}

}  // namespace

Status DocRowwiseIteratorBase::CopyKeyColumnsToQLTableRow(QLTableRow* row) {
  if (end_referenced_key_column_index_ == 0) return Status::OK();

  dockv::DocKeyDecoder decoder(row_key_);
  RETURN_NOT_OK(decoder.DecodeCotableId());
  RETURN_NOT_OK(decoder.DecodeColocationId());
  bool has_hash_components = VERIFY_RESULT(decoder.DecodeHashCode());

  // Populate the key column values from the doc key. The key column values in doc key were
  // written in the same order as in the table schema (see DocKeyFromQLKey). If the range columns
  // are present, read them also.
  if (has_hash_components) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        doc_read_context_.schema, 0, doc_read_context_.schema.num_hash_key_columns(), "hash",
        end_referenced_key_column_index_, &decoder, row));
  }
  if (!decoder.GroupEnded()) {
    RETURN_NOT_OK(SetQLPrimaryKeyColumnValues(
        doc_read_context_.schema, doc_read_context_.schema.num_hash_key_columns(),
        doc_read_context_.schema.num_range_key_columns(), "range", end_referenced_key_column_index_,
        &decoder, row));
  }

  return Status::OK();
}

}  // namespace yb::docdb
