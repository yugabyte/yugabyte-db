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

#include "yb/docdb/pgsql_operation.h"

#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/optional/optional_io.hpp>

#include "yb/common/partition.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/ql_value.h"

#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_pg_expr.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/docdb_pgapi.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/packed_row.h"
#include "yb/docdb/primitive_value_util.h"
#include "yb/docdb/ql_storage_interface.h"

#include "yb/util/algorithm_util.h"
#include "yb/util/flag_tags.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

using namespace std::literals;

DECLARE_bool(ysql_disable_index_backfill);

DEFINE_double(ysql_scan_timeout_multiplier, 0.5,
              "DEPRECATED. Has no affect, use ysql_scan_deadline_margin_ms to control the client "
              "timeout");

DEFINE_uint64(ysql_scan_deadline_margin_ms, 1000,
              "Scan deadline is calculated by adding client timeout to the time when the request "
              "was received. It defines the moment in time when client has definitely timed out "
              "and if the request is yet in processing after the deadline, it can be canceled. "
              "Therefore to prevent client timeout, the request handler should return partial "
              "result and paging information some time before the deadline. That's what the "
              "ysql_scan_deadline_margin_ms is for. It should account for network and processing "
              "delays.");

DEFINE_bool(pgsql_consistent_transactional_paging, true,
            "Whether to enforce consistency of data returned for second page and beyond for YSQL "
            "queries on transactional tables. If true, read restart errors could be returned to "
            "prevent inconsistency. If false, no read restart errors are returned but the data may "
            "be stale. The latter is preferable for long scans. The data returned for the first "
            "page of results is never stale regardless of this flag.");

DEFINE_test_flag(int32, slowdown_pgsql_aggregate_read_ms, 0,
                 "If set > 0, slows down the response to pgsql aggregate read by this amount.");

DEFINE_bool(ysql_enable_packed_row, false, "Whether packed row is enabled for YSQL.");

DEFINE_uint64(
    ysql_packed_row_size_limit, 0,
    "Packed row size limit for YSQL in bytes. 0 to make this equal to SSTable block size.");

DEFINE_test_flag(bool, ysql_suppress_ybctid_corruption_details, false,
                 "Whether to show less details on ybctid corruption error status message.  Useful "
                 "during tests that require consistent output.");

namespace yb {
namespace docdb {

namespace {

// Compatibility: accept column references from a legacy nodes as a list of column ids only
Status CreateProjection(const Schema& schema,
                                const PgsqlColumnRefsPB& column_refs,
                                Schema* projection) {
  // Create projection of non-primary key columns. Primary key columns are implicitly read by DocDB.
  // It will also sort the columns before scanning.
  vector<ColumnId> column_ids;
  column_ids.reserve(column_refs.ids_size());
  for (int32_t id : column_refs.ids()) {
    const ColumnId column_id(id);
    if (!schema.is_key_column(column_id)) {
      column_ids.emplace_back(column_id);
    }
  }
  return schema.CreateProjectionByIdsIgnoreMissing(column_ids, projection);
}

Status CreateProjection(
    const Schema& schema,
    const google::protobuf::RepeatedPtrField<PgsqlColRefPB> &column_refs,
    Schema* projection) {
  vector<ColumnId> column_ids;
  column_ids.reserve(column_refs.size());
  for (const PgsqlColRefPB& column_ref : column_refs) {
    const ColumnId column_id(column_ref.column_id());
    if (!schema.is_key_column(column_id)) {
      column_ids.emplace_back(column_id);
    }
  }
  return schema.CreateProjectionByIdsIgnoreMissing(column_ids, projection);
}

void AddIntent(const std::string& encoded_key, WaitPolicy wait_policy, KeyValueWriteBatchPB *out) {
  auto pair = out->mutable_read_pairs()->Add();
  pair->set_key(encoded_key);
  pair->set_value(std::string(1, ValueEntryTypeAsChar::kNullLow));
  // Since we don't batch read RPCs that lock rows, we can get away with using a singular
  // wait_policy field. Once we start batching read requests (issue #2495), we will need a repeated
  // wait policies field.
  out->set_wait_policy(wait_policy);
}

Status AddIntent(const PgsqlExpressionPB& ybctid, WaitPolicy wait_policy,
                         KeyValueWriteBatchPB* out) {
  const auto &val = ybctid.value().binary_value();
  SCHECK(!val.empty(), InternalError, "empty ybctid");
  AddIntent(val, wait_policy, out);
  return Status::OK();
}

template<class R, class Request, class DocKeyProcessor, class EncodedDocKeyProcessor>
Result<R> FetchDocKeyImpl(const Schema& schema,
                          const Request& req,
                          const DocKeyProcessor& dk_processor,
                          const EncodedDocKeyProcessor& edk_processor) {
  // Init DocDB key using either ybctid or partition and range values.
  if (req.has_ybctid_column_value()) {
    const auto& ybctid = req.ybctid_column_value().value().binary_value();
    SCHECK(!ybctid.empty(), InternalError, "empty ybctid");
    return edk_processor(ybctid);
  } else {
    auto hashed_components = VERIFY_RESULT(InitKeyColumnPrimitiveValues(
        req.partition_column_values(), schema, 0 /* start_idx */));
    auto range_components = VERIFY_RESULT(InitKeyColumnPrimitiveValues(
        req.range_column_values(), schema, schema.num_hash_key_columns()));
    return dk_processor(hashed_components.empty()
        ? DocKey(schema, std::move(range_components))
        : DocKey(
            schema, req.hash_code(), std::move(hashed_components), std::move(range_components)));
  }
}

Result<string> FetchEncodedDocKey(const Schema& schema, const PgsqlReadRequestPB& request) {
  return FetchDocKeyImpl<string>(
      schema, request,
      [](const auto& doc_key) { return doc_key.Encode().ToStringBuffer(); },
      [](const auto& encoded_doc_key) { return encoded_doc_key; });
}

Result<DocKey> FetchDocKey(const Schema& schema, const PgsqlWriteRequestPB& request) {
  return FetchDocKeyImpl<DocKey>(
      schema, request,
      [](const auto& doc_key) { return doc_key; },
      [&schema](const auto& encoded_doc_key) -> Result<DocKey> {
        DocKey key(schema);
        RETURN_NOT_OK(key.DecodeFrom(encoded_doc_key));
        return key;
      });
}

Result<YQLRowwiseIteratorIf::UniPtr> CreateIterator(
    const YQLStorageIf& ql_storage,
    const PgsqlReadRequestPB& request,
    const Schema& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    bool is_explicit_request_read_time) {
  VLOG_IF(2, request.is_for_backfill()) << "Creating iterator for " << yb::ToString(request);

  YQLRowwiseIteratorIf::UniPtr result;
  // TODO(neil) Remove the following IF block when it is completely obsolete.
  // The following IF block has not been used since 2.1 release.
  // We keep it here only for rolling upgrade purpose.
  if (request.has_ybctid_column_value()) {
    SCHECK(!request.has_paging_state(),
           InternalError,
           "Each ybctid value identifies one row in the table while paging state "
           "is only used for multi-row queries.");
    RETURN_NOT_OK(ql_storage.GetIterator(
        request.stmt_id(), projection, doc_read_context, txn_op_context,
        deadline, read_time, request.ybctid_column_value().value(), &result));
  } else {
    SubDocKey start_sub_doc_key;
    auto actual_read_time = read_time;
    // Decode the start SubDocKey from the paging state and set scan start key.
    if (request.has_paging_state() &&
        request.paging_state().has_next_row_key() &&
        !request.paging_state().next_row_key().empty()) {
      KeyBytes start_key_bytes(request.paging_state().next_row_key());
      RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
      // TODO(dmitry) Remove backward compatibility block when obsolete.
      if (!is_explicit_request_read_time) {
        if (request.paging_state().has_read_time()) {
          actual_read_time = ReadHybridTime::FromPB(request.paging_state().read_time());
        } else {
          actual_read_time.read = start_sub_doc_key.hybrid_time();
        }
      }
    } else if (request.is_for_backfill()) {
      RSTATUS_DCHECK(is_explicit_request_read_time, InvalidArgument,
                     "Backfill request should already be using explicit read times.");
      PgsqlBackfillSpecPB spec;
      spec.ParseFromString(a2b_hex(request.backfill_spec()));
      if (!spec.next_row_key().empty()) {
        KeyBytes start_key_bytes(spec.next_row_key());
        RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
      }
    }
    RETURN_NOT_OK(ql_storage.GetIterator(
        request, projection, doc_read_context, txn_op_context,
        deadline, read_time, start_sub_doc_key.doc_key(), &result));
  }
  return std::move(result);
}

class DocKeyColumnPathBuilder {
 public:
  explicit DocKeyColumnPathBuilder(const RefCntPrefix& doc_key)
      : doc_key_(doc_key.as_slice()) {
  }

  RefCntPrefix Build(ColumnIdRep column_id) {
    buffer_.Clear();
    buffer_.AppendKeyEntryType(KeyEntryType::kColumnId);
    buffer_.AppendColumnId(ColumnId(column_id));
    RefCntBuffer path(doc_key_.size() + buffer_.size());
    doc_key_.CopyTo(path.data());
    buffer_.AsSlice().CopyTo(path.data() + doc_key_.size());
    return path;
  }

 private:
  Slice doc_key_;
  KeyBytes buffer_;
};

struct RowPackerData {
  SchemaVersion schema_version;
  const SchemaPacking& packing;

  static Result<RowPackerData> Create(
      const PgsqlWriteRequestPB& request, const DocReadContext& read_context) {
    auto schema_version = request.schema_version();
    return RowPackerData {
      .schema_version = schema_version,
      .packing = VERIFY_RESULT(read_context.schema_packing_storage.GetPacking(schema_version)),
    };
  }
};

bool IsExpression(const PgsqlColumnValuePB& column_value) {
  return GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kPgEvalExprCall;
}

class ExpressionHelper {
 public:
  Status Init(
      const Schema& schema, const PgsqlWriteRequestPB& request, const QLTableRow& table_row) {
    DocPgExprExecutor expr_exec(&schema);
    size_t num_exprs = 0;
    for (const auto& column_value : request.column_new_values()) {
      if (IsExpression(column_value)) {
        RETURN_NOT_OK(expr_exec.AddTargetExpression(column_value.expr()));
        VLOG(1) << "Added target expression to the executor";
        num_exprs++;
      }
    }
    if (num_exprs == 0) {
      return Status::OK();
    }

    bool match;
    for (const PgsqlColRefPB& column_ref : request.col_refs()) {
      RETURN_NOT_OK(expr_exec.AddColumnRef(column_ref));
      VLOG(1) << "Added column reference to the executor";
    }
    results_.resize(num_exprs);
    return expr_exec.Exec(table_row, &results_, &match);
  }

  QLExprResult* NextResult(const PgsqlColumnValuePB& column_value) {
    if (!IsExpression(column_value)) {
      return nullptr;
    }

    return &results_[next_result_idx_++];
  }

 private:
  std::vector<QLExprResult> results_;
  size_t next_result_idx_ = 0;
};

} // namespace

class PgsqlWriteOperation::RowPackContext {
 public:
  RowPackContext(const PgsqlWriteRequestPB& request,
                 const DocOperationApplyData& data,
                 const RowPackerData& packer_data)
      : query_id_(request.stmt_id()),
        data_(data),
        write_id_(data.doc_write_batch->ReserveWriteId()),
        packer_(packer_data.schema_version, packer_data.packing, FLAGS_ysql_packed_row_size_limit) {
  }

  Result<bool> Add(ColumnId column_id, const QLValuePB& value) {
    return packer_.AddValue(column_id, value);
  }

  Status Complete(const RefCntPrefix& encoded_doc_key) {
    auto encoded_value = VERIFY_RESULT(packer_.Complete());
    return data_.doc_write_batch->SetPrimitive(
        DocPath(encoded_doc_key.as_slice()),
        ValueControlFields(), ValueRef(encoded_value),
        data_.read_time, data_.deadline, query_id_,
        write_id_);
  }

 private:
  rocksdb::QueryId query_id_;
  const DocOperationApplyData& data_;
  const IntraTxnWriteId write_id_;
  RowPacker packer_;
};

//--------------------------------------------------------------------------------------------------

Status PgsqlWriteOperation::Init(PgsqlResponsePB* response) {
  // Initialize operation inputs.
  response_ = response;

  doc_key_ = VERIFY_RESULT(FetchDocKey(doc_read_context_->schema, request_));
  encoded_doc_key_ = doc_key_->EncodeAsRefCntPrefix();

  return Status::OK();
}

// Check if a duplicate value is inserted into a unique index.
Result<bool> PgsqlWriteOperation::HasDuplicateUniqueIndexValue(const DocOperationApplyData& data) {
  VLOG(3) << "Looking for collisions in\n" << docdb::DocDBDebugDumpToStr(
      data.doc_write_batch->doc_db(), SchemaPackingStorage());
  // We need to check backwards only for backfilled entries.
  bool ret =
      VERIFY_RESULT(HasDuplicateUniqueIndexValue(data, Direction::kForward)) ||
      (request_.is_backfill() &&
       VERIFY_RESULT(HasDuplicateUniqueIndexValue(data, Direction::kBackward)));
  if (!ret) {
    VLOG(3) << "No collisions found";
  }
  return ret;
}

Result<bool> PgsqlWriteOperation::HasDuplicateUniqueIndexValue(
    const DocOperationApplyData& data, Direction direction) {
  VLOG(2) << "Looking for collision while going " << yb::ToString(direction)
          << ". Trying to insert " << *doc_key_;
  auto requested_read_time = data.read_time;
  if (direction == Direction::kForward) {
    return HasDuplicateUniqueIndexValue(data, requested_read_time);
  }

  auto iter = CreateIntentAwareIterator(
      data.doc_write_batch->doc_db(),
      BloomFilterMode::USE_BLOOM_FILTER,
      doc_key_->Encode().AsSlice(),
      rocksdb::kDefaultQueryId,
      txn_op_context_,
      data.deadline,
      ReadHybridTime::Max());

  HybridTime oldest_past_min_ht = VERIFY_RESULT(FindOldestOverwrittenTimestamp(
      iter.get(), SubDocKey(*doc_key_), requested_read_time.read));
  const HybridTime oldest_past_min_ht_liveness =
      VERIFY_RESULT(FindOldestOverwrittenTimestamp(
          iter.get(),
          SubDocKey(*doc_key_, KeyEntryValue::kLivenessColumn),
          requested_read_time.read));
  oldest_past_min_ht.MakeAtMost(oldest_past_min_ht_liveness);
  if (!oldest_past_min_ht.is_valid()) {
    return false;
  }
  return HasDuplicateUniqueIndexValue(
      data, ReadHybridTime::SingleTime(oldest_past_min_ht));
}

Result<bool> PgsqlWriteOperation::HasDuplicateUniqueIndexValue(
    const DocOperationApplyData& data, ReadHybridTime read_time) {
  // Set up the iterator to read the current primary key associated with the index key.
  DocPgsqlScanSpec spec(doc_read_context_->schema, request_.stmt_id(), *doc_key_);
  DocRowwiseIterator iterator(doc_read_context_->schema,
                              *doc_read_context_,
                              txn_op_context_,
                              data.doc_write_batch->doc_db(),
                              data.deadline,
                              read_time);
  RETURN_NOT_OK(iterator.Init(spec));

  // It is a duplicate value if the index key exists already and the index value (corresponding to
  // the indexed table's primary key) is not the same.
  if (!VERIFY_RESULT(iterator.HasNext())) {
    VLOG(2) << "No collision found while checking at " << yb::ToString(read_time);
    return false;
  }

  QLTableRow table_row;
  RETURN_NOT_OK(iterator.NextRow(&table_row));
  for (const auto& column_value : request_.column_values()) {
    // Get the column.
    if (!column_value.has_column_id()) {
      return STATUS(InternalError, "column id missing", column_value.DebugString());
    }
    const ColumnId column_id(column_value.column_id());

    // Check column-write operator.
    CHECK(GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert)
      << "Illegal write instruction";

    // Evaluate column value.
    QLExprResult expr_result;
    RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, expr_result.Writer()));

    boost::optional<const QLValuePB&> existing_value = table_row.GetValue(column_id);
    const QLValuePB& new_value = expr_result.Value();
    if (existing_value && *existing_value != new_value) {
      VLOG(2) << "Found collision while checking at " << yb::ToString(read_time)
              << "\nExisting: " << yb::ToString(*existing_value)
              << " vs New: " << yb::ToString(new_value)
              << "\nUsed read time as " << yb::ToString(data.read_time);
      DVLOG(3) << "DocDB is now:\n" << docdb::DocDBDebugDumpToStr(
          data.doc_write_batch->doc_db(), SchemaPackingStorage());
      return true;
    }
  }

  VLOG(2) << "No collision while checking at " << yb::ToString(read_time);
  return false;
}

Result<HybridTime> PgsqlWriteOperation::FindOldestOverwrittenTimestamp(
    IntentAwareIterator* iter,
    const SubDocKey& sub_doc_key,
    HybridTime min_read_time) {
  HybridTime result;
  VLOG(3) << "Doing iter->Seek " << *doc_key_;
  iter->Seek(*doc_key_);
  if (iter->valid()) {
    const KeyBytes bytes = sub_doc_key.EncodeWithoutHt();
    const Slice& sub_key_slice = bytes.AsSlice();
    result = VERIFY_RESULT(
        iter->FindOldestRecord(sub_key_slice, min_read_time));
    VLOG(2) << "iter->FindOldestRecord returned " << result << " for "
            << SubDocKey::DebugSliceToString(sub_key_slice);
  } else {
    VLOG(3) << "iter->Seek " << *doc_key_ << " turned out to be invalid";
  }
  return result;
}

Status PgsqlWriteOperation::Apply(const DocOperationApplyData& data) {
  VLOG(4) << "Write, read time: " << data.read_time << ", txn: " << txn_op_context_;

  auto scope_exit = ScopeExit([this] {
    if (!result_buffer_.empty()) {
      NetworkByteOrder::Store64(result_buffer_.data(), result_rows_);
    }
  });

  switch (request_.stmt_type()) {
    case PgsqlWriteRequestPB::PGSQL_INSERT:
      return ApplyInsert(data, IsUpsert::kFalse);

    case PgsqlWriteRequestPB::PGSQL_UPDATE:
      return ApplyUpdate(data);

    case PgsqlWriteRequestPB::PGSQL_DELETE:
      return ApplyDelete(data, request_.is_delete_persist_needed());

    case PgsqlWriteRequestPB::PGSQL_UPSERT: {
      // Upserts should not have column refs (i.e. require read).
      RSTATUS_DCHECK(request_.col_refs().empty(),
              IllegalState,
              "Upsert operation should not have column references");
      return ApplyInsert(data, IsUpsert::kTrue);
    }

    case PgsqlWriteRequestPB::PGSQL_TRUNCATE_COLOCATED:
      return ApplyTruncateColocated(data);
  }
  return Status::OK();
}

Status PgsqlWriteOperation::InsertColumn(
    const DocOperationApplyData& data, const QLTableRow& table_row,
    const PgsqlColumnValuePB& column_value, RowPackContext* pack_context) {
  // Get the column.
  SCHECK(column_value.has_column_id(), InternalError, "Column id missing: $0", column_value);
  // Check column-write operator.
  auto write_instruction = GetTSWriteInstruction(column_value.expr());
  SCHECK_EQ(write_instruction, bfpg::TSOpcode::kScalarInsert, InternalError,
            "Illegal write instruction");

  const ColumnId column_id(column_value.column_id());
  const ColumnSchema& column = VERIFY_RESULT(doc_read_context_->schema.column_by_id(column_id));

  // Evaluate column value.
  QLExprResult expr_result;
  RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, expr_result.Writer()));

  if (!pack_context || !VERIFY_RESULT(pack_context->Add(column_id, expr_result.Value()))) {
    // Inserting into specified column.
    DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path, ValueRef(expr_result.Value(), column.sorting_type()),
        data.read_time, data.deadline, request_.stmt_id()));
  }

  return Status::OK();
}

Status PgsqlWriteOperation::ApplyInsert(const DocOperationApplyData& data, IsUpsert is_upsert) {
  QLTableRow table_row;
  if (!is_upsert) {
    if (request_.is_backfill()) {
      if (VERIFY_RESULT(HasDuplicateUniqueIndexValue(data))) {
        // Unique index value conflict found.
        response_->set_status(PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR);
        response_->set_error_message("Duplicate key found in unique index");
        return Status::OK();
      }
    } else {
      // Non-backfill requests shouldn't use HasDuplicateUniqueIndexValue because
      // - they should error even if the conflicting row matches
      // - retrieving and calculating whether the conflicting row matches is a waste
      RETURN_NOT_OK(ReadColumns(data, &table_row));
      if (!table_row.IsEmpty()) {
        VLOG(4) << "Duplicate row: " << table_row.ToString();
        // Primary key or unique index value found.
        response_->set_status(PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR);
        response_->set_error_message("Duplicate key found in primary key or unique index");
        return Status::OK();
      }
    }
  }

  if (FLAGS_ysql_enable_packed_row) {
    RowPackContext pack_context(
        request_, data, VERIFY_RESULT(RowPackerData::Create(request_, *doc_read_context_)));

    auto column_id_extractor = [](const PgsqlColumnValuePB& column_value) {
      return column_value.column_id();
    };

    if (IsMonotonic(request_.column_values(), column_id_extractor)) {
      for (const auto& column_value : request_.column_values()) {
        RETURN_NOT_OK(InsertColumn(data, table_row, column_value, &pack_context));
      }
    } else {
      auto column_order = StableSorted(request_.column_values(), column_id_extractor);

      for (const auto& key_and_index : column_order) {
        RETURN_NOT_OK(InsertColumn(
            data, table_row, request_.column_values()[key_and_index.original_index],
            &pack_context));
      }
    }

    RETURN_NOT_OK(pack_context.Complete(encoded_doc_key_));
  } else {
    RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
        DocPath(encoded_doc_key_.as_slice(), KeyEntryValue::kLivenessColumn),
        ValueControlFields(), ValueRef(ValueEntryType::kNullLow), data.read_time, data.deadline,
        request_.stmt_id()));

    for (const auto& column_value : request_.column_values()) {
      RETURN_NOT_OK(InsertColumn(data, table_row, column_value, /* pack_context=*/ nullptr));
    }
  }

  RETURN_NOT_OK(PopulateResultSet(table_row));

  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::UpdateColumn(
    const DocOperationApplyData& data, const QLTableRow& table_row,
    const PgsqlColumnValuePB& column_value, QLTableRow* returning_table_row,
    QLExprResult* result, RowPackContext* pack_context) {
  // Get the column.
  if (!column_value.has_column_id()) {
    return STATUS(InternalError, "column id missing", column_value.DebugString());
  }
  const ColumnId column_id(column_value.column_id());
  const ColumnSchema& column = VERIFY_RESULT(doc_read_context_->schema.column_by_id(column_id));

  DCHECK(!doc_read_context_->schema.is_key_column(column_id));

  // Evaluate column value.
  QLExprResult result_holder;
  if (!result) {
    // Check column-write operator.
    SCHECK(GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert,
           InternalError,
           "Unsupported DocDB Expression");

    RETURN_NOT_OK(EvalExpr(
        column_value.expr(), table_row, result_holder.Writer(), &doc_read_context_->schema));
    result = &result_holder;
  }

  // Update RETURNING values
  if (request_.targets_size()) {
    returning_table_row->AllocColumn(column_id, result->Value());
  }

  if (!pack_context || !VERIFY_RESULT(pack_context->Add(column_id, result->Value()))) {
    // Inserting into specified column.
    DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path, ValueRef(result->Value(), column.sorting_type()), data.read_time,
        data.deadline, request_.stmt_id()));
  }

  return Status::OK();
}

Status PgsqlWriteOperation::ApplyUpdate(const DocOperationApplyData& data) {
  QLTableRow table_row;
  RETURN_NOT_OK(ReadColumns(data, &table_row));
  if (table_row.IsEmpty()) {
    // Row not found.
    response_->set_skipped(true);
    return Status::OK();
  }
  QLTableRow returning_table_row;
  if (request_.targets_size()) {
    returning_table_row = table_row;
  }

  // skipped is set to false if this operation produces some data to write.
  bool skipped = true;

  if (request_.has_ybctid_column_value()) {
    const auto& schema = doc_read_context_->schema;
    ExpressionHelper expression_helper;
    RETURN_NOT_OK(expression_helper.Init(schema, request_, table_row));

    skipped = request_.column_new_values().empty();
    const size_t num_non_key_columns = schema.num_columns() - schema.num_key_columns();
    if (FLAGS_ysql_enable_packed_row &&
        make_unsigned(request_.column_new_values().size()) == num_non_key_columns) {
      RowPackContext pack_context(
          request_, data, VERIFY_RESULT(RowPackerData::Create(request_, *doc_read_context_)));

      auto column_id_extractor = [](const PgsqlColumnValuePB& column_value) {
        return column_value.column_id();
      };

      if (IsMonotonic(request_.column_new_values(), column_id_extractor)) {
        for (const auto& column_value : request_.column_new_values()) {
          RETURN_NOT_OK(UpdateColumn(
              data, table_row, column_value, &returning_table_row,
              expression_helper.NextResult(column_value), &pack_context));
        }
      } else {
        auto column_id_and_result_extractor =
            [&expression_helper](const PgsqlColumnValuePB& column_value) {
          return std::pair(column_value.column_id(), expression_helper.NextResult(column_value));
        };
        auto column_order = StableSorted(
            request_.column_new_values(), column_id_and_result_extractor);

        for (const auto& entry : column_order) {
          RETURN_NOT_OK(UpdateColumn(
              data, table_row, request_.column_values()[entry.original_index], &returning_table_row,
              entry.key.second, &pack_context));
        }
      }

      RETURN_NOT_OK(pack_context.Complete(encoded_doc_key_));
    } else {
      for (const auto& column_value : request_.column_new_values()) {
        RETURN_NOT_OK(UpdateColumn(
            data, table_row, column_value, &returning_table_row,
            expression_helper.NextResult(column_value), /* pack_context=*/ nullptr));
      }
    }
  } else {
    // This UPDATE is calling PGGATE directly without going thru PostgreSQL layer.
    // Keep it here as we might need it.

    // Very limited support for where expressions. Only used for updates to the sequences data
    // table.
    bool is_match = true;
    if (request_.has_where_expr()) {
      QLExprResult match;
      RETURN_NOT_OK(EvalExpr(request_.where_expr(), table_row, match.Writer()));
      is_match = match.Value().bool_value();
    }

    if (is_match) {
      for (const auto &column_value : request_.column_new_values()) {
        // Get the column.
        if (!column_value.has_column_id()) {
          return STATUS(InternalError, "column id missing", column_value.DebugString());
        }
        const ColumnId column_id(column_value.column_id());
        const ColumnSchema& column = VERIFY_RESULT(
            doc_read_context_->schema.column_by_id(column_id));

        // Check column-write operator.
        CHECK(GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert)
        << "Illegal write instruction";

        // Evaluate column value.
        QLExprResult expr_result;
        RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, expr_result.Writer()));

        // Inserting into specified column.
        DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));
        RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
            sub_path, ValueRef(expr_result.Value(), column.sorting_type()), data.read_time,
            data.deadline, request_.stmt_id()));
        skipped = false;
      }
    }
  }

  if (request_.targets_size()) {
    // Returning the values after the update.
    RETURN_NOT_OK(PopulateResultSet(returning_table_row));
  } else {
    // Returning the values before the update.
    RETURN_NOT_OK(PopulateResultSet(table_row));
  }

  if (skipped) {
    response_->set_skipped(true);
  }
  response_->set_rows_affected_count(1);
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::ApplyDelete(
    const DocOperationApplyData& data,
    const bool is_persist_needed) {
  int num_deleted = 1;
  QLTableRow table_row;
  RETURN_NOT_OK(ReadColumns(data, &table_row));
  if (table_row.IsEmpty()) {
    // Row not found.
    // Return early unless we still want to apply the delete for backfill purposes.  Deletes to
    // nonexistent rows are expected to get written to the index when the index has the delete
    // permission during an online schema migration.  num_deleted should be 0 because we don't want
    // to report back to the user that we deleted 1 row; response_ should not set skipped because it
    // will prevent tombstone intents from getting applied.
    if (!is_persist_needed) {
      response_->set_skipped(true);
      return Status::OK();
    }
    num_deleted = 0;
  }

  // TODO(neil) Add support for WHERE clause.
  CHECK(request_.column_values_size() == 0) << "WHERE clause condition is not yet fully supported";

  // Otherwise, delete the referenced row (all columns).
  RETURN_NOT_OK(data.doc_write_batch->DeleteSubDoc(
      DocPath(encoded_doc_key_.as_slice()), data.read_time, data.deadline));

  RETURN_NOT_OK(PopulateResultSet(table_row));

  response_->set_rows_affected_count(num_deleted);
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::ApplyTruncateColocated(const DocOperationApplyData& data) {
  RETURN_NOT_OK(data.doc_write_batch->DeleteSubDoc(DocPath(
      encoded_doc_key_.as_slice()), data.read_time, data.deadline));
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::ReadColumns(const DocOperationApplyData& data,
                                        QLTableRow* table_row) {
  // Filter the columns using primary key.
  if (doc_key_) {
    Schema projection;
    RETURN_NOT_OK(CreateProjection(doc_read_context_->schema, request_.column_refs(), &projection));
    DocPgsqlScanSpec spec(projection, request_.stmt_id(), *doc_key_);
    DocRowwiseIterator iterator(projection,
                                *doc_read_context_,
                                txn_op_context_,
                                data.doc_write_batch->doc_db(),
                                data.deadline,
                                data.read_time);
    RETURN_NOT_OK(iterator.Init(spec));
    if (VERIFY_RESULT(iterator.HasNext())) {
      RETURN_NOT_OK(iterator.NextRow(table_row));
    } else {
      table_row->Clear();
    }
    data.restart_read_ht->MakeAtLeast(iterator.RestartReadHt());
  }

  return Status::OK();
}

Status PgsqlWriteOperation::PopulateResultSet(const QLTableRow& table_row) {
  if (result_buffer_.empty()) {
    // Reserve space for num rows.
    pggate::PgWire::WriteInt64(0, &result_buffer_);
  }
  ++result_rows_;
  int rscol_index = 0;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    if (expr.has_column_id()) {
      QLExprResult value;
      if (expr.column_id() == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
        // Strip cotable ID / colocation ID from the serialized DocKey before returning it
        // as ybctid.
        Slice tuple_id = encoded_doc_key_.as_slice();
        if (tuple_id.starts_with(KeyEntryTypeAsChar::kTableId)) {
          tuple_id.remove_prefix(1 + kUuidSize);
        } else if (tuple_id.starts_with(KeyEntryTypeAsChar::kColocationId)) {
          tuple_id.remove_prefix(1 + sizeof(ColocationId));
        }
        value.Writer().NewValue().set_binary_value(tuple_id.data(), tuple_id.size());
      } else {
        RETURN_NOT_OK(EvalExpr(expr, table_row, value.Writer()));
      }
      RETURN_NOT_OK(pggate::WriteColumn(value.Value(), &result_buffer_));
    }
    rscol_index++;
  }
  return Status::OK();
}

Status PgsqlWriteOperation::GetDocPaths(GetDocPathsMode mode,
                                        DocPathsToLock *paths,
                                        IsolationLevel *level) const {
  if (!encoded_doc_key_) {
    return Status::OK();
  }

  // When this write operation requires a read, it requires a read snapshot so paths will be locked
  // in snapshot isolation for consistency. Otherwise, pure writes will happen in serializable
  // isolation so that they will serialize but do not conflict with one another.
  //
  // Currently, only keys that are being written are locked, no lock is taken on read at the
  // snapshot isolation level.
  *level = RequireReadSnapshot() ? IsolationLevel::SNAPSHOT_ISOLATION
                                 : IsolationLevel::SERIALIZABLE_ISOLATION;

  switch (mode) {
    case GetDocPathsMode::kLock: {
      if (request_.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPDATE) {
        // In case of UPDATE row need to be protected from removing. Weak intent is enough for
        // this purpose. To achieve this the path for row's SystemColumnIds::kLivenessColumn column
        // is returned. The caller code will create strong intent for returned path
        // (row's column doc key) and weak intents for all its prefixes (including row's doc key).
        // Note: UPDATE operation may have expressions instead of exact value.
        // These expressions may read column value.
        // Potentially expression for updating column v1 may read value of column v2.
        //
        // UPDATE t SET v = v + 10 WHERE k = 1
        // UPDATE t SET v1 = v2 + 10 WHERE k = 1
        //
        // Strong intent for the whole row is required in this case as it may be too expensive to
        // determine what exact columns are read by the expression.

        bool has_expression = false;
        for (const auto& column_value : request_.column_new_values()) {
          if (!column_value.expr().has_value()) {
            has_expression = true;
            break;
          }
        }
        if (!has_expression) {
          DocKeyColumnPathBuilder builder(encoded_doc_key_);
          paths->push_back(builder.Build(to_underlying(SystemColumnIds::kLivenessColumn)));
          return Status::OK();
        }
      }
      break;
    }
    case GetDocPathsMode::kIntents: {
      const google::protobuf::RepeatedPtrField<PgsqlColumnValuePB>* column_values = nullptr;
      if (request_.stmt_type() == PgsqlWriteRequestPB::PGSQL_INSERT ||
          request_.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPSERT) {
        column_values = &request_.column_values();
      } else if (request_.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPDATE) {
        column_values = &request_.column_new_values();
      }

      if (column_values != nullptr && !column_values->empty()) {
        DocKeyColumnPathBuilder builder(encoded_doc_key_);
        for (const auto& column_value : *column_values) {
          paths->push_back(builder.Build(column_value.column_id()));
        }
        return Status::OK();
      }
      break;
    }
  }
  // Add row's doc key. Caller code will create strong intent for the whole row in this case.
  paths->push_back(encoded_doc_key_);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Result<size_t> PgsqlReadOperation::Execute(const YQLStorageIf& ql_storage,
                                           CoarseTimePoint deadline,
                                           const ReadHybridTime& read_time,
                                           bool is_explicit_request_read_time,
                                           const DocReadContext& doc_read_context,
                                           const DocReadContext* index_doc_read_context,
                                           faststring *result_buffer,
                                           HybridTime *restart_read_ht) {
  size_t fetched_rows = 0;
  // Reserve space for fetched rows count.
  pggate::PgWire::WriteInt64(0, result_buffer);
  auto se = ScopeExit([&fetched_rows, result_buffer] {
    NetworkByteOrder::Store64(result_buffer->data(), fetched_rows);
  });
  VLOG(4) << "Read, read time: " << read_time << ", txn: " << txn_op_context_;

  // Fetching data.
  bool has_paging_state = false;
  if (request_.batch_arguments_size() > 0) {
    SCHECK(request_.has_ybctid_column_value(),
           InternalError,
           "ybctid arguments can be batched only");
    fetched_rows = VERIFY_RESULT(ExecuteBatchYbctid(
        ql_storage, deadline, read_time, doc_read_context, result_buffer, restart_read_ht));
  } else if (request_.has_sampling_state()) {
    fetched_rows = VERIFY_RESULT(ExecuteSample(
        ql_storage, deadline, read_time, is_explicit_request_read_time, doc_read_context,
        result_buffer, restart_read_ht, &has_paging_state));
  } else {
    fetched_rows = VERIFY_RESULT(ExecuteScalar(
        ql_storage, deadline, read_time, is_explicit_request_read_time, doc_read_context,
        index_doc_read_context, result_buffer, restart_read_ht, &has_paging_state));
  }

  VTRACE(1, "Fetched $0 rows. $1 paging state", fetched_rows, (has_paging_state ? "No" : "Has"));
  SCHECK(table_iter_ != nullptr, InternalError, "table iterator is invalid");

  *restart_read_ht = table_iter_->RestartReadHt();
  return fetched_rows;
}

Result<size_t> PgsqlReadOperation::ExecuteSample(const YQLStorageIf& ql_storage,
                                                 CoarseTimePoint deadline,
                                                 const ReadHybridTime& read_time,
                                                 bool is_explicit_request_read_time,
                                                 const DocReadContext& doc_read_context,
                                                 faststring *result_buffer,
                                                 HybridTime *restart_read_ht,
                                                 bool *has_paging_state) {
  *has_paging_state = false;
  size_t scanned_rows = 0;
  PgsqlSamplingStatePB sampling_state = request_.sampling_state();
  // Requested total number of rows to collect
  int targrows = sampling_state.targrows();
  // Number of rows collected so far
  int numrows = sampling_state.numrows();
  // Total number of rows scanned
  double samplerows = sampling_state.samplerows();
  // Current number of rows to skip before collecting next one for sample
  double rowstoskip = sampling_state.rowstoskip();
  // Variables for the random numbers generator
  YbgPrepareMemoryContext();
  YbgReservoirState rstate = NULL;
  YbgSamplerCreate(sampling_state.rstate_w(), sampling_state.rand_state(), &rstate);
  // Buffer to hold selected row ids from the current page
  std::unique_ptr<QLValuePB[]> reservoir = std::make_unique<QLValuePB[]>(targrows);
  // Number of rows to scan for the current page.
  // Too low row count limit is inefficient since we have to allocate and initialize a reservoir
  // capable to hold potentially large (targrows) number of tuples. The row count has to be at least
  // targrows for a chance to fill up the reservoir. Actually, the algorithm selects targrows only
  // for very first page of the table, then it starts to skip tuples, the further it goes, the more
  // it skips. For a large enough table it eventually starts to select less than targrows per page,
  // regardless of the row_count_limit.
  // Anyways, double targrows seems like reasonable minimum for the row_count_limit.
  size_t row_count_limit = 2 * targrows;
  if (request_.has_limit() && request_.limit() > row_count_limit) {
    row_count_limit = request_.limit();
  }
  // Request is not supposed to contain any column refs, we just need the liveness column.
  Schema projection;
  RETURN_NOT_OK(CreateProjection(doc_read_context.schema, request_.column_refs(), &projection));
  // Request may carry paging state, CreateIterator takes care of positioning
  table_iter_ = VERIFY_RESULT(CreateIterator(
      ql_storage, request_, projection, doc_read_context, txn_op_context_,
      deadline, read_time, is_explicit_request_read_time));
  bool scan_time_exceeded = false;
  CoarseTimePoint stop_scan = deadline - FLAGS_ysql_scan_deadline_margin_ms * 1ms;
  while (scanned_rows++ < row_count_limit &&
         VERIFY_RESULT(table_iter_->HasNext()) &&
         !scan_time_exceeded) {
    if (numrows < targrows) {
      // Select first targrows of the table. If first partition(s) have less than that, next
      // partition starts to continue populating it's reservoir starting from the numrows' position:
      // the numrows, as well as other sampling state variables is returned and copied over to the
      // next sampling request
      Slice ybctid = VERIFY_RESULT(table_iter_->GetTupleId());
      reservoir[numrows++].set_binary_value(ybctid.data(), ybctid.size());
    } else {
      // At least targrows tuples have already been collected, now algorithm skips increasing number
      // of row before taking next one into the reservoir
      if (rowstoskip <= 0) {
        // Take ybctid of the current row
        Slice ybctid = VERIFY_RESULT(table_iter_->GetTupleId());
        // Pick random tuple in the reservoir to replace
        double rvalue;
        int k;
        YbgSamplerRandomFract(rstate, &rvalue);
        k = static_cast<int>(targrows * rvalue);
        // Overwrite previous value with new one
        reservoir[k].set_binary_value(ybctid.data(), ybctid.size());
        // Choose next number of rows to skip
        YbgReservoirGetNextS(rstate, samplerows, targrows, &rowstoskip);
      } else {
        rowstoskip -= 1;
      }
    }
    // Taking tuple ID does not advance the table iterator. Move it now.
    table_iter_->SkipRow();
    // Check if we are running out of time
    scan_time_exceeded = CoarseMonoClock::now() >= stop_scan;
  }
  // Count live rows we have scanned TODO how to count dead rows?
  samplerows += scanned_rows - 1;
  // Return collected tuples from the reservoir.
  // Tuples are returned as (index, ybctid) pairs, where index is in [0..targrows-1] range.
  // As mentioned above, for large tables reservoirs become increasingly sparse from page to page.
  // So we hope to save by sending variable number of index/ybctid pairs vs exactly targrows of
  // nullable ybctids. It also helps in case of extremely small table or partition.
  int fetched_rows = 0;
  for (int i = 0; i < numrows; i++) {
    QLValuePB index;
    if (reservoir[i].has_binary_value()) {
      index.set_int32_value(i);
      RETURN_NOT_OK(pggate::WriteColumn(index, result_buffer));
      RETURN_NOT_OK(pggate::WriteColumn(reservoir[i], result_buffer));
      fetched_rows++;
    }
  }

  // Return sampling state to continue with next page
  PgsqlSamplingStatePB *new_sampling_state = response_.mutable_sampling_state();
  new_sampling_state->set_numrows(numrows);
  new_sampling_state->set_targrows(targrows);
  new_sampling_state->set_samplerows(samplerows);
  new_sampling_state->set_rowstoskip(rowstoskip);
  uint64_t randstate = 0;
  double rstate_w = 0;
  YbgSamplerGetState(rstate, &rstate_w, &randstate);
  new_sampling_state->set_rstate_w(rstate_w);
  new_sampling_state->set_rand_state(randstate);
  YbgDeleteMemoryContext();

  // Return paging state if scan has not been completed
  RETURN_NOT_OK(SetPagingStateIfNecessary(
      table_iter_.get(), scanned_rows, row_count_limit, scan_time_exceeded,
      doc_read_context.schema, read_time, has_paging_state));
  return fetched_rows;
}

Result<size_t> PgsqlReadOperation::ExecuteScalar(const YQLStorageIf& ql_storage,
                                                 CoarseTimePoint deadline,
                                                 const ReadHybridTime& read_time,
                                                 bool is_explicit_request_read_time,
                                                 const DocReadContext& doc_read_context,
                                                 const DocReadContext *index_doc_read_context,
                                                 faststring *result_buffer,
                                                 HybridTime *restart_read_ht,
                                                 bool *has_paging_state) {
  const auto& schema = doc_read_context.schema;
  *has_paging_state = false;

  size_t fetched_rows = 0;
  size_t row_count_limit = std::numeric_limits<std::size_t>::max();
  if (request_.has_limit()) {
    if (request_.limit() == 0) {
      return fetched_rows;
    }
    row_count_limit = request_.limit();
  }

  // Create the projection of regular columns selected by the row block plus any referenced in
  // the WHERE condition. When DocRowwiseIterator::NextRow() populates the value map, it uses this
  // projection only to scan sub-documents. The query schema is used to select only referenced
  // columns and key columns.
  Schema projection;
  Schema index_projection;
  YQLRowwiseIteratorIf *iter;
  const Schema* scan_schema;
  DocPgExprExecutor expr_exec(&schema);
  for (const PgsqlColRefPB& column_ref : request_.col_refs()) {
    RETURN_NOT_OK(expr_exec.AddColumnRef(column_ref));
    VLOG(1) << "Added column reference to the executor";
  }
  for (const PgsqlExpressionPB& expr : request_.where_clauses()) {
    RETURN_NOT_OK(expr_exec.AddWhereExpression(expr));
    VLOG(1) << "Added where expression to the executor";
  }

  if (!request_.col_refs().empty()) {
    RETURN_NOT_OK(CreateProjection(schema, request_.col_refs(), &projection));
  } else {
    // Compatibility: Either request indeed has no column refs, or it comes from a legacy node.
    RETURN_NOT_OK(CreateProjection(schema, request_.column_refs(), &projection));
  }
  table_iter_ = VERIFY_RESULT(CreateIterator(
      ql_storage, request_, projection, doc_read_context, txn_op_context_,
      deadline, read_time, is_explicit_request_read_time));

  ColumnId ybbasectid_id;
  if (request_.has_index_request()) {
    scan_schema = &index_doc_read_context->schema;
    const PgsqlReadRequestPB& index_request = request_.index_request();
    RETURN_NOT_OK(CreateProjection(
        *scan_schema, index_request.column_refs(), &index_projection));
    index_iter_ = VERIFY_RESULT(CreateIterator(
        ql_storage, index_request, index_projection, *index_doc_read_context,
        txn_op_context_, deadline, read_time, is_explicit_request_read_time));
    iter = index_iter_.get();
    const auto idx = scan_schema->find_column("ybidxbasectid");
    SCHECK_NE(idx, Schema::kColumnNotFound, Corruption, "ybidxbasectid not found in index schema");
    ybbasectid_id = scan_schema->column_id(idx);
  } else {
    iter = table_iter_.get();
    scan_schema = &schema;
  }

  VLOG(1) << "Started iterator";

  // Set scan start time.
  bool scan_time_exceeded = false;
  CoarseTimePoint stop_scan = deadline - FLAGS_ysql_scan_deadline_margin_ms * 1ms;

  // Fetching data.
  int match_count = 0;
  QLTableRow row;
  while (fetched_rows < row_count_limit && VERIFY_RESULT(iter->HasNext()) &&
         !scan_time_exceeded) {
    row.Clear();

    // If there is an index request, fetch ybbasectid from the index and use it as ybctid
    // to fetch from the base table. Otherwise, fetch from the base table directly.
    if (request_.has_index_request()) {
      RETURN_NOT_OK(iter->NextRow(&row));
      const auto& tuple_id = row.GetValue(ybbasectid_id);
      SCHECK_NE(tuple_id, boost::none, Corruption, "ybbasectid not found in index row");
      if (!VERIFY_RESULT(table_iter_->SeekTuple(tuple_id->binary_value()))) {
        DocKey doc_key;
        RETURN_NOT_OK(doc_key.DecodeFrom(tuple_id->binary_value()));
        if (FLAGS_TEST_ysql_suppress_ybctid_corruption_details) {
          return STATUS(Corruption, "ybctid not found in indexed table");
        } else {
          return STATUS_FORMAT(
              Corruption,
              "ybctid $0 not found in indexed table. index table id is $1",
              doc_key,
              request_.index_request().table_id());
        }
      }
      row.Clear();
      RETURN_NOT_OK(table_iter_->NextRow(projection, &row));
    } else {
      RETURN_NOT_OK(iter->NextRow(projection, &row));
    }

    // Match the row with the where condition before adding to the row block.
    bool is_match = true;
    RETURN_NOT_OK(expr_exec.Exec(row, nullptr, &is_match));
    if (is_match) {
      match_count++;
      if (request_.is_aggregate()) {
        RETURN_NOT_OK(EvalAggregate(row));
      } else {
        RETURN_NOT_OK(PopulateResultSet(row, result_buffer));
        ++fetched_rows;
      }
    }

    // Check if we are running out of time
    scan_time_exceeded = CoarseMonoClock::now() >= stop_scan;
  }

  VLOG(1) << "Stopped iterator after " << match_count << " matches, "
          << fetched_rows << " rows fetched";
  VLOG(1) << "Deadline is " << (scan_time_exceeded ? "" : "not ") << "exceeded";

  if (request_.is_aggregate() && match_count > 0) {
    RETURN_NOT_OK(PopulateAggregate(row, result_buffer));
    ++fetched_rows;
  }

  if (PREDICT_FALSE(FLAGS_TEST_slowdown_pgsql_aggregate_read_ms > 0) && request_.is_aggregate()) {
    TRACE("Sleeping for $0 ms", FLAGS_TEST_slowdown_pgsql_aggregate_read_ms);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_pgsql_aggregate_read_ms));
  }

  RETURN_NOT_OK(SetPagingStateIfNecessary(
      iter, fetched_rows, row_count_limit, scan_time_exceeded, *scan_schema,
      read_time, has_paging_state));
  return fetched_rows;
}

Result<size_t> PgsqlReadOperation::ExecuteBatchYbctid(const YQLStorageIf& ql_storage,
                                                      CoarseTimePoint deadline,
                                                      const ReadHybridTime& read_time,
                                                      const DocReadContext& doc_read_context,
                                                      faststring *result_buffer,
                                                      HybridTime *restart_read_ht) {
  const auto& schema = doc_read_context.schema;
  Schema projection;
  RETURN_NOT_OK(CreateProjection(schema, request_.column_refs(), &projection));

  QLTableRow row;
  size_t row_count = 0;
  DocPgExprExecutor expr_exec(&schema);
  for (const PgsqlColRefPB& column_ref : request_.col_refs()) {
    RETURN_NOT_OK(expr_exec.AddColumnRef(column_ref));
    VLOG(1) << "Added column reference to the executor";
  }
  for (const PgsqlExpressionPB& expr : request_.where_clauses()) {
    RETURN_NOT_OK(expr_exec.AddWhereExpression(expr));
    VLOG(1) << "Added where expression to the executor";
  }

  for (const PgsqlBatchArgumentPB& batch_argument : request_.batch_arguments()) {
    // Get the row.
    RETURN_NOT_OK(ql_storage.GetIterator(
        request_.stmt_id(), projection, doc_read_context, txn_op_context_,
        deadline, read_time, batch_argument.ybctid().value(), &table_iter_));

    if (VERIFY_RESULT(table_iter_->HasNext())) {
      row.Clear();
      RETURN_NOT_OK(table_iter_->NextRow(projection, &row));
      bool is_match = true;
      RETURN_NOT_OK(expr_exec.Exec(row, nullptr, &is_match));
      if (is_match) {
        // Populate result set.
        RETURN_NOT_OK(PopulateResultSet(row, result_buffer));
        response_.add_batch_orders(batch_argument.order());
        row_count++;
      }
    }
  }

  // Set status for this batch.
  // Mark all rows were processed even in case some of the ybctids were not found.
  response_.set_batch_arg_count(request_.batch_arguments_size());

  return row_count;
}

Status PgsqlReadOperation::SetPagingStateIfNecessary(const YQLRowwiseIteratorIf* iter,
                                                     size_t fetched_rows,
                                                     const size_t row_count_limit,
                                                     const bool scan_time_exceeded,
                                                     const Schema& schema,
                                                     const ReadHybridTime& read_time,
                                                     bool *has_paging_state) {
  *has_paging_state = false;
  if (!request_.return_paging_state()) {
    return Status::OK();
  }

  // Set the paging state for next row.
  if (fetched_rows >= row_count_limit || scan_time_exceeded) {
    SubDocKey next_row_key;
    RETURN_NOT_OK(iter->GetNextReadSubDocKey(&next_row_key));
    // When the "limit" number of rows are returned and we are asked to return the paging state,
    // return the partition key and row key of the next row to read in the paging state if there are
    // still more rows to read. Otherwise, leave the paging state empty which means we are done
    // reading from this tablet.
    if (!next_row_key.doc_key().empty()) {
      const auto& keybytes = next_row_key.Encode();
      PgsqlPagingStatePB* paging_state = response_.mutable_paging_state();
      if (schema.num_hash_key_columns() > 0) {
        paging_state->set_next_partition_key(
           PartitionSchema::EncodeMultiColumnHashValue(next_row_key.doc_key().hash()));
      } else {
        paging_state->set_next_partition_key(keybytes.ToStringBuffer());
      }
      paging_state->set_next_row_key(keybytes.ToStringBuffer());
      *has_paging_state = true;
    }
  }
  if (*has_paging_state) {
    if (FLAGS_pgsql_consistent_transactional_paging) {
      read_time.AddToPB(response_.mutable_paging_state());
    } else {
      // Using SingleTime will help avoid read restarts on second page and later but will
      // potentially produce stale results on those pages.
      auto per_row_consistent_read_time = ReadHybridTime::SingleTime(read_time.read);
      per_row_consistent_read_time.AddToPB(response_.mutable_paging_state());
    }
  }

  return Status::OK();
}

Status PgsqlReadOperation::PopulateResultSet(const QLTableRow& table_row,
                                             faststring *result_buffer) {
  QLExprResult result;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    RETURN_NOT_OK(EvalExpr(expr, table_row, result.Writer()));
    RETURN_NOT_OK(pggate::WriteColumn(result.Value(), result_buffer));
  }
  return Status::OK();
}

Status PgsqlReadOperation::GetTupleId(QLValuePB *result) const {
  // Get row key and save to QLValue.
  // TODO(neil) Check if we need to append a table_id and other info to TupleID. For example, we
  // might need info to make sure the TupleId by itself is a valid reference to a specific row of
  // a valid table.
  const Slice tuple_id = VERIFY_RESULT(table_iter_->GetTupleId());
  result->set_binary_value(tuple_id.data(), tuple_id.size());
  return Status::OK();
}

Status PgsqlReadOperation::EvalAggregate(const QLTableRow& table_row) {
  if (aggr_result_.empty()) {
    int column_count = request_.targets().size();
    aggr_result_.resize(column_count);
  }

  int aggr_index = 0;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    RETURN_NOT_OK(EvalExpr(expr, table_row, aggr_result_[aggr_index++].Writer()));
  }
  return Status::OK();
}

Status PgsqlReadOperation::PopulateAggregate(const QLTableRow& table_row,
                                             faststring *result_buffer) {
  int column_count = request_.targets().size();
  for (int rscol_index = 0; rscol_index < column_count; rscol_index++) {
    RETURN_NOT_OK(pggate::WriteColumn(aggr_result_[rscol_index].Value(), result_buffer));
  }
  return Status::OK();
}

Status PgsqlReadOperation::GetIntents(const Schema& schema, KeyValueWriteBatchPB* out) {
  if (request_.batch_arguments_size() > 0 && request_.has_ybctid_column_value()) {
    for (const auto& batch_argument : request_.batch_arguments()) {
      SCHECK(batch_argument.has_ybctid(), InternalError, "ybctid batch argument is expected");
      RETURN_NOT_OK(AddIntent(batch_argument.ybctid(), request_.wait_policy(), out));
    }
  } else {
    AddIntent(VERIFY_RESULT(FetchEncodedDocKey(schema, request_)), request_.wait_policy(), out);
  }
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
