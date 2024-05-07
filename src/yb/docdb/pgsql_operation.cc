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

#include <algorithm>
#include <functional>
#include <limits>
#include <string>
#include <unordered_set>
#include <variant>
#include <utility>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/row_mark.h"

#include "yb/docdb/doc_pg_expr.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/docdb_pgapi.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/ql_storage_interface.h"

#include "yb/dockv/doc_path.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/primitive_value_util.h"
#include "yb/dockv/reader_projection.h"

#include "yb/gutil/macros.h"

#include "yb/qlexpr/ql_expr_util.h"

#include "yb/rpc/sidecars.h"

#include "yb/util/algorithm_util.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"
#include "yb/util/yb_pg_errcodes.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

using namespace std::literals;

DECLARE_bool(ysql_disable_index_backfill);

DEPRECATE_FLAG(double, ysql_scan_timeout_multiplier, "10_2022");

DEFINE_UNKNOWN_uint64(ysql_scan_deadline_margin_ms, 1000,
              "Scan deadline is calculated by adding client timeout to the time when the request "
              "was received. It defines the moment in time when client has definitely timed out "
              "and if the request is yet in processing after the deadline, it can be canceled. "
              "Therefore to prevent client timeout, the request handler should return partial "
              "result and paging information some time before the deadline. That's what the "
              "ysql_scan_deadline_margin_ms is for. It should account for network and processing "
              "delays.");

DEFINE_UNKNOWN_bool(pgsql_consistent_transactional_paging, true,
            "Whether to enforce consistency of data returned for second page and beyond for YSQL "
            "queries on transactional tables. If true, read restart errors could be returned to "
            "prevent inconsistency. If false, no read restart errors are returned but the data may "
            "be stale. The latter is preferable for long scans. The data returned for the first "
            "page of results is never stale regardless of this flag.");

DEFINE_test_flag(int32, slowdown_pgsql_aggregate_read_ms, 0,
                 "If set > 0, slows down the response to pgsql aggregate read by this amount.");

// TODO: only enabled for new installs only for now, will enable it for upgrades in 2.22+ release.
#ifndef NDEBUG
// Disable packed row by default in debug builds.
DEFINE_RUNTIME_bool(ysql_enable_packed_row, false,
#else
DEFINE_RUNTIME_AUTO_bool(ysql_enable_packed_row, kNewInstallsOnly, false, true,
#endif
                    "Whether packed row is enabled for YSQL.");

DEFINE_RUNTIME_bool(ysql_enable_packed_row_for_colocated_table, false,
                    "Whether to enable packed row for colocated tables.");

DEFINE_UNKNOWN_uint64(
    ysql_packed_row_size_limit, 0,
    "Packed row size limit for YSQL in bytes. 0 to make this equal to SSTable block size.");

DEFINE_test_flag(bool, ysql_suppress_ybctid_corruption_details, false,
                 "Whether to show less details on ybctid corruption error status message.  Useful "
                 "during tests that require consistent output.");

DEFINE_RUNTIME_bool(ysql_enable_pack_full_row_update, false,
                    "Whether to enable packed row for full row update.");

DEFINE_RUNTIME_PREVIEW_bool(ysql_use_packed_row_v2, false,
                            "Whether to use packed row V2 when row packing is enabled.");

DEFINE_RUNTIME_AUTO_bool(ysql_skip_row_lock_for_update, kExternal, true, false,
    "By default DocDB operations for YSQL take row-level locks. If set to true, DocDB will instead "
    "take finer column-level locks instead of locking the whole row. This may cause issues with "
    "data integrity for operations with implicit dependencies between columns.");

namespace yb::docdb {

using dockv::DocKey;
using dockv::DocPath;
using dockv::KeyEntryValue;
using dockv::SubDocKey;
using qlexpr::QLExprResult;
using qlexpr::QLTableRow;

bool ShouldYsqlPackRow(bool is_colocated) {
  return FLAGS_ysql_enable_packed_row &&
         (!is_colocated || FLAGS_ysql_enable_packed_row_for_colocated_table);
}

namespace {

class DocKeyColumnPathBuilder {
 public:
  DocKeyColumnPathBuilder(dockv::KeyBytes* buffer, Slice encoded_doc_key)
      : buffer_(*buffer), encoded_doc_key_length_(encoded_doc_key.size()) {
      DCHECK(buffer_.empty());
      buffer_.AppendRawBytes(encoded_doc_key);
  }

  [[nodiscard]] Slice Build(ColumnIdRep column_id) {
    return Build(dockv::KeyEntryType::kColumnId, column_id);
  }

  [[nodiscard]] Slice Build(dockv::SystemColumnIds column_id) {
    return Build(dockv::KeyEntryType::kSystemColumnId, to_underlying(column_id));
  }

 private:
  [[nodiscard]] Slice Build(dockv::KeyEntryType key_entry_type, ColumnIdRep column_id) {
    buffer_.Truncate(encoded_doc_key_length_);
    buffer_.AppendKeyEntryType(key_entry_type);
    buffer_.AppendColumnId(ColumnId(column_id));
    return buffer_.AsSlice();
  }

  dockv::KeyBytes& buffer_;
  const size_t encoded_doc_key_length_;

  DISALLOW_COPY_AND_ASSIGN(DocKeyColumnPathBuilder);
};

class DocKeyColumnPathBuilderHolder {
 public:
  template<class... Args>
  explicit DocKeyColumnPathBuilderHolder(Args&&... args)
      : builder_(&buffer_, std::forward<Args>(args)...) {}

  [[nodiscard]] DocKeyColumnPathBuilder& builder() { return builder_; }

 private:
  dockv::KeyBytes buffer_;
  DocKeyColumnPathBuilder builder_;
};

YB_STRONGLY_TYPED_BOOL(KeyOnlyRequested);

YB_DEFINE_ENUM(IntentFeature, (kRegularRead)(kKeyOnlyRead)(kRowLock));

// Helper class to describe what kind of intents must be created on particular doc key:
// - regular_read - intent for regular read is required
// - key_only_read - intent for key column only read is required
// - row_lock - intent for row lock is required.
// Note: regular_read and key_only_read can't be both equaled true.
class IntentMode {
 public:
  IntentMode(IsolationLevel level, RowMarkType row_mark, KeyOnlyRequested key_only_requested) {
    auto intent_types = dockv::GetIntentTypesForRead(level, row_mark);
    const auto& read_intents = intent_types.read;
    auto& lock_intents = intent_types.row_mark;
    if (!read_intents.None()) {
      auto all_read_intents = MakeWeak(read_intents);
      if (key_only_requested) {
        features_.Set(IntentFeature::kKeyOnlyRead);
      } else {
        all_read_intents |= read_intents;
        features_.Set(IntentFeature::kRegularRead);
      }
      lock_intents &= ~(all_read_intents);
    }
    if (!lock_intents.None()) {
      features_.Set(IntentFeature::kRowLock);
    }
  }

  [[nodiscard]] bool regular_read() const {
    return features_.Test(IntentFeature::kRegularRead);
  }

  [[nodiscard]] bool key_only_read() const {
    return features_.Test(IntentFeature::kKeyOnlyRead);
  }

  [[nodiscard]] bool row_lock() const {
    return features_.Test(IntentFeature::kRowLock);
  }

 private:
  EnumBitSet<IntentFeature> features_;
};

class IntentInserter {
 public:
  explicit IntentInserter(LWKeyValueWriteBatchPB* out)
      : out_(*out) {}

  void Add(Slice encoded_key, const IntentMode& mode) {
    if (mode.regular_read()) {
      Add(encoded_key);
    } else if (mode.key_only_read()) {
      auto& buf = buffer();
      buf.Clear();
      DocKeyColumnPathBuilder doc_key_builder(&buf, encoded_key);
      Add(doc_key_builder.Build(dockv::SystemColumnIds::kLivenessColumn));
    }

    if (mode.row_lock()) {
      Add(encoded_key, /*row_lock=*/ true);
    }
  }

 private:
  [[nodiscard]] dockv::KeyBytes& buffer() {
    if (!buffer_) {
      buffer_.emplace();
    }
    return *buffer_;
  }

  void Add(Slice encoded_key, bool is_row_lock = false) {
    auto& pair = *out_.add_read_pairs();
    pair.dup_key(encoded_key);
    pair.dup_value(Slice(
        is_row_lock ? &dockv::ValueEntryTypeAsChar::kRowLock
                    : &dockv::ValueEntryTypeAsChar::kNullLow,
        1));
  }

  std::optional<dockv::KeyBytes> buffer_;
  LWKeyValueWriteBatchPB& out_;

  DISALLOW_COPY_AND_ASSIGN(IntentInserter);
};

class DocKeyAccessor {
 public:
  explicit DocKeyAccessor(std::reference_wrapper<const Schema> schema)
      : schema_(schema.get()), source_(std::nullopt), result_holder_(std::nullopt) {}

  Result<Slice> GetEncoded(std::reference_wrapper<const PgsqlExpressionPB> ybctid) {
    RETURN_NOT_OK(Apply(ybctid.get()));
    return Encoded();
  }

  Result<Slice> GetEncoded(std::reference_wrapper<const yb::PgsqlReadRequestPB> req) {
    RETURN_NOT_OK(Apply(req.get()));
    return Encoded();
  }

  Result<DocKey&> GetDecoded(std::reference_wrapper<const yb::PgsqlWriteRequestPB> req) {
    RETURN_NOT_OK(Apply(req.get()));
    return Decoded();
  }

 private:
  Result<DocKey&> Decoded() {
    DCHECK(!std::holds_alternative<std::nullopt_t>(source_));
    if (std::holds_alternative<DocKey>(source_)) {
      return std::get<DocKey>(source_);
    }
    auto& doc_key = GetResultHolder<DocKey>();
    RETURN_NOT_OK(doc_key.DecodeFrom(std::get<Slice>(source_)));
    return doc_key;
  }

  Result<Slice> Encoded() {
    DCHECK(!std::holds_alternative<std::nullopt_t>(source_));
    if (std::holds_alternative<DocKey>(source_)) {
      return GetResultHolder<EncodedKey>().Append(std::get<DocKey>(source_)).AsSlice();
    }

    auto& src = std::get<Slice>(source_);
    if (!schema_.is_colocated()) {
      return src;
    }
    return GetResultHolder<EncodedKey>().Append(src).AsSlice();
  }

  template<class Req>
  Status Apply(const Req& req) {
    // Init DocDB key using either ybctid or partition and range values.
    if (req.has_ybctid_column_value()) {
      return Apply(req.ybctid_column_value());
    }

    auto hashed_components = VERIFY_RESULT(qlexpr::InitKeyColumnPrimitiveValues(
        req.partition_column_values(), schema_, 0 /* start_idx */));
    auto range_components = VERIFY_RESULT(qlexpr::InitKeyColumnPrimitiveValues(
        req.range_column_values(), schema_, schema_.num_hash_key_columns()));
    if (hashed_components.empty()) {
      source_.emplace<DocKey>(schema_, std::move(range_components));
    } else {
      source_.emplace<DocKey>(
          schema_, req.hash_code(), std::move(hashed_components), std::move(range_components));
    }
    return Status::OK();
  }

  Status Apply(const PgsqlExpressionPB& ybctid) {
    const auto& value = ybctid.value().binary_value();
    SCHECK(!value.empty(), InternalError, "empty ybctid");
    source_.emplace<Slice>(value);
    return Status::OK();
  }

  template<class T>
  T& GetResultHolder() {
    return std::holds_alternative<T>(result_holder_)
        ? std::get<T>(result_holder_)
        : result_holder_.emplace<T>(schema_);
  }

  class EncodedKey {
   public:
    explicit EncodedKey(std::reference_wrapper<const Schema> schema)
        : schema_(schema),
          prefix_length_(0) {
      if (schema_.is_colocated()) {
        DocKey prefix_builder(schema_);
        prefix_builder.AppendTo(&data_);
        DCHECK_GT(data_.size(), 1);
        prefix_length_ = data_.size() - 1;
        data_.Truncate(prefix_length_);
      }
    }

    const dockv::KeyBytes& Append(const DocKey& doc_key) {
      DCHECK(doc_key.colocation_id() == schema_.colocation_id() &&
             doc_key.cotable_id() == schema_.cotable_id());
      // Because doc_key uses same schema all encoded doc key will contain same prefix as generated
      // in constructor (if any). So it is safe to reuse it in future.
      data_.Clear();
      doc_key.AppendTo(&data_);
      return data_;
    }

    const dockv::KeyBytes& Append(Slice ybctid) {
      // Current function must be called only in case ybctid requires prefix.
      DCHECK(prefix_length_);
      data_.Truncate(prefix_length_);
      data_.AppendRawBytes(ybctid);
      return data_;
    }

   private:
    const Schema& schema_;
    dockv::KeyBytes data_;
    size_t prefix_length_;
  };

  const Schema& schema_;
  std::variant<std::nullopt_t, Slice, DocKey> source_;
  std::variant<std::nullopt_t, EncodedKey, DocKey> result_holder_;
};

Result<YQLRowwiseIteratorIf::UniPtr> CreateIterator(
    const YQLStorageIf& ql_storage,
    const PgsqlReadRequestPB& request,
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data,
    bool is_explicit_request_read_time,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    const DocDBStatistics* statistics) {
  VLOG_IF(2, request.is_for_backfill()) << "Creating iterator for " << yb::ToString(request);

  YQLRowwiseIteratorIf::UniPtr result;
  // TODO(neil) Remove the following IF block when it is completely obsolete.
  // The following IF block gets used in the CREATE INDEX codepath.
  if (request.has_ybctid_column_value()) {
    RETURN_NOT_OK(ql_storage.GetIteratorForYbctid(
        request.stmt_id(), projection, doc_read_context, txn_op_context, read_operation_data,
        request.ybctid_column_value().value(), request.ybctid_column_value().value(), pending_op,
        &result, statistics));
  } else {
    SubDocKey start_sub_doc_key;
    auto actual_read_time = read_operation_data.read_time;
    // Decode the start SubDocKey from the paging state and set scan start key.
    if (request.has_paging_state() &&
        request.paging_state().has_next_row_key() &&
        !request.paging_state().next_row_key().empty()) {
      dockv::KeyBytes start_key_bytes(request.paging_state().next_row_key());
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
        dockv::KeyBytes start_key_bytes(spec.next_row_key());
        RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
      }
    }
    RETURN_NOT_OK(ql_storage.GetIterator(
        request, projection, doc_read_context, txn_op_context, read_operation_data,
        start_sub_doc_key.doc_key(), pending_op, &result, statistics));
  }

  return std::move(result);
}

template <class ColumnIds>
Status VerifyNoColsMarkedForDeletion(const TableId& table_id,
                                     const Schema& schema,
                                     ColumnIds&& column_refs) {
  for (const auto& col : column_refs) {
    const auto& id = dockv::GetColumnId(col);
    int idx = schema.find_column_by_id(id);
    if (idx == -1) {
      return STATUS_FORMAT(IllegalState, "Column not found: $0 in table $1", id, table_id);
    }
    SCHECK(!schema.IsColMarkedForDeletion(idx),
           InvalidArgument,
           "Column with id $0 marked for deletion in table $1",
           id, table_id);
  }
  return Status::OK();
}

template<class PB>
Status VerifyNoRefColsMarkedForDeletion(const Schema& schema, const PB& request) {
  if (!request.col_refs().empty()) {
    RETURN_NOT_OK(VerifyNoColsMarkedForDeletion(request.table_id(), schema, request.col_refs()));
  }
  // Compatibility: Either request indeed has no column refs, or it comes from a legacy node.
  return VerifyNoColsMarkedForDeletion(request.table_id(), schema, request.column_refs().ids());
}

Status VerifyNoColsMarkedForDeletion(const Schema& schema, const PgsqlWriteRequestPB& request) {
  // Verify that the request does not refer any columns marked for deletion.
  RETURN_NOT_OK(VerifyNoRefColsMarkedForDeletion(schema, request));
  // Verify columns being updated are not marked for deletion.
  RETURN_NOT_OK(VerifyNoColsMarkedForDeletion(request.table_id(),
                                              schema,
                                              request.column_new_values()));
  // Verify columns being inserted are not marked for deletion.
  return VerifyNoColsMarkedForDeletion(request.table_id(), schema, request.column_values());
}

template<class PB>
void InitProjection(const Schema& schema, const PB& request, dockv::ReaderProjection* projection) {
  if (!request.col_refs().empty()) {
    projection->Init(schema, request.col_refs());
  } else {
    // Compatibility: Either request indeed has no column refs, or it comes from a legacy node.
    projection->Init(schema, request.column_refs().ids());
  }
}

template<class PB>
dockv::ReaderProjection CreateProjection(const Schema& schema, const PB& request) {
  dockv::ReaderProjection result;
  InitProjection(schema, request, &result);
  return result;
}

YB_DEFINE_ENUM(FetchResult, (NotFound)(FilteredOut)(Found));

class FilteringIterator {
 public:
  explicit FilteringIterator(std::unique_ptr<YQLRowwiseIteratorIf>* iterator_holder)
      : iterator_holder_(*iterator_holder) {}

  Status Init(
      const YQLStorageIf& ql_storage,
      const PgsqlReadRequestPB& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      bool is_explicit_request_read_time,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      const DocDBStatistics* statistics) {
    RETURN_NOT_OK(InitCommon(request, read_context.get().schema(), projection));
    iterator_holder_ = VERIFY_RESULT(CreateIterator(
        ql_storage, request, projection, read_context, txn_op_context, read_operation_data,
        is_explicit_request_read_time, pending_op, statistics));
    return Status::OK();
  }

  Status InitForYbctid(
      const YQLStorageIf& ql_storage,
      const PgsqlReadRequestPB& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      const QLValuePB& min_ybctid,
      const QLValuePB& max_ybctid,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      const docdb::DocDBStatistics* statistics,
      SkipSeek skip_seek) {
    RETURN_NOT_OK(InitCommon(request, read_context.get().schema(), projection));
    return ql_storage.GetIteratorForYbctid(
        request.stmt_id(), projection, read_context, txn_op_context, read_operation_data,
        min_ybctid, max_ybctid, pending_op, &iterator_holder_, statistics, skip_seek);
  }

  Result<FetchResult> FetchNext(dockv::PgTableRow* table_row) {
    if (!VERIFY_RESULT(iterator_holder_->PgFetchNext(table_row))) {
      return FetchResult::NotFound;
    }
    return CheckFilter(*table_row);
  }

  Result<FetchResult> FetchTuple(const Slice& tuple_id, dockv::PgTableRow* row) {
    iterator_holder_->SeekTuple(tuple_id);
    if (!VERIFY_RESULT(iterator_holder_->PgFetchNext(row)) ||
        iterator_holder_->GetTupleId() != tuple_id) {
      return FetchResult::NotFound;
    }
    return CheckFilter(*row);
  }

 private:
  Status InitCommon(
      const PgsqlReadRequestPB& request, const Schema& schema,
      const dockv::ReaderProjection& projection) {
    const auto& where_clauses = request.where_clauses();
    if (where_clauses.empty()) {
      return Status::OK();
    }
    DocPgExprExecutorBuilder builder(schema, projection);
    for (const auto& exp : where_clauses) {
      RETURN_NOT_OK(builder.AddWhere(exp));
    }
    filter_.emplace(VERIFY_RESULT(builder.Build(request.col_refs())));
    return Status::OK();
  }

  Result<FetchResult> CheckFilter(const dockv::PgTableRow& row) {
    return filter_ && !VERIFY_RESULT(filter_->Exec(row))
        ? FetchResult::FilteredOut : FetchResult::Found;
  }

  std::unique_ptr<YQLRowwiseIteratorIf>& iterator_holder_;
  boost::optional<DocPgExprExecutor> filter_;
};

struct IndexState {
  IndexState(
      const Schema& schema, const PgsqlReadRequestPB& request,
      std::unique_ptr<YQLRowwiseIteratorIf>* iterator_holder, ColumnId ybbasectid_id)
      : projection(CreateProjection(schema, request)), row(projection), iter(iterator_holder),
        ybbasectid_idx(projection.ColumnIdxById(ybbasectid_id)), scanned_rows(0) {
  }

  dockv::ReaderProjection projection;
  dockv::PgTableRow row;
  FilteringIterator iter;
  const size_t ybbasectid_idx;
  uint64_t scanned_rows;
};

Result<FetchResult> FetchTableRow(
    const std::string& table_id, FilteringIterator* table_iter,
    IndexState* index, dockv::PgTableRow* row) {
  Slice tuple_id;
  if (index) {
    auto& index_row = index->row;
    switch(VERIFY_RESULT(index->iter.FetchNext(&index_row))) {
      case FetchResult::NotFound:
        return FetchResult::NotFound;
      case FetchResult::FilteredOut:
        VLOG(1) << "Row filtered out by colocated index condition";
        ++index->scanned_rows;
        return FetchResult::FilteredOut;
      case FetchResult::Found:
        ++index->scanned_rows;
        break;
    }

    auto optional_tuple_id = index_row.GetValueByIndex(index->ybbasectid_idx);
    SCHECK(optional_tuple_id, Corruption, "ybbasectid not found in index row");
    tuple_id = optional_tuple_id->binary_value();
  }

  const auto fetch_result = VERIFY_RESULT(
      index ? table_iter->FetchTuple(tuple_id, row) : table_iter->FetchNext(row));
  switch(fetch_result) {
    case FetchResult::NotFound: {
      if (!index) {
        break;
      }
      if (FLAGS_TEST_ysql_suppress_ybctid_corruption_details) {
        return STATUS(Corruption, "ybctid not found in indexed table");
      }
      return STATUS_FORMAT(
          Corruption, "ybctid $0 not found in indexed table. index table id is $1",
          DocKey::DebugSliceToString(tuple_id), table_id);
    }
    case FetchResult::FilteredOut:
      VLOG(1) << "Row filtered out by the condition";
      break;
    case FetchResult::Found:
      break;
  }
  return fetch_result;
}

struct RowPackerData {
  SchemaVersion schema_version;
  const dockv::SchemaPacking& packing;
  const Schema& schema;

  static Result<RowPackerData> Create(
      const PgsqlWriteRequestPB& request, const DocReadContext& read_context) {
    auto schema_version = request.schema_version();
    return RowPackerData {
      .schema_version = schema_version,
      .packing = VERIFY_RESULT(read_context.schema_packing_storage.GetPacking(schema_version)),
      .schema = read_context.schema()
    };
  }

  dockv::RowPackerVariant MakePacker() const {
    if (FLAGS_ysql_use_packed_row_v2) {
      return MakePackerHelper<dockv::RowPackerV2>();
    }
    return MakePackerHelper<dockv::RowPackerV1>();
  }

 private:
  template <class T>
  dockv::RowPackerVariant MakePackerHelper() const {
    return dockv::RowPackerVariant(
        std::in_place_type_t<T>(), schema_version, packing, FLAGS_ysql_packed_row_size_limit,
        Slice(), schema);
  }
};

bool IsExpression(const PgsqlColumnValuePB& column_value) {
  return qlexpr::GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kPgEvalExprCall;
}

class ExpressionHelper {
 public:
  Status Init(
      const Schema& schema, const dockv::ReaderProjection& projection,
      const PgsqlWriteRequestPB& request, const dockv::PgTableRow& table_row) {
    DocPgExprExecutorBuilder builder(schema, projection);
    for (const auto& column_value : request.column_new_values()) {
      if (IsExpression(column_value)) {
        RETURN_NOT_OK(builder.AddTarget(column_value.expr()));
      }
    }
    auto executor = VERIFY_RESULT(builder.Build(request.col_refs()));
    return ResultToStatus(executor.Exec(table_row, &results_));
  }

  QLExprResult* NextResult(const PgsqlColumnValuePB& column_value) {
    if (!IsExpression(column_value)) {
      return nullptr;
    }
    DCHECK_LT(next_result_idx_, results_.size());
    return &results_[next_result_idx_++];
  }

 private:
  std::vector<QLExprResult> results_;
  size_t next_result_idx_ = 0;
};

[[nodiscard]] inline bool NewValuesHaveExpression(const PgsqlWriteRequestPB& request) {
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

  return std::any_of(
      request.column_new_values().begin(), request.column_new_values().end(),
      [](const auto& cv) { return !cv.expr().has_value(); });
}

void WriteNumRows(size_t result_rows, const WriteBufferPos& pos, WriteBuffer* buffer) {
  char encoded_rows[sizeof(uint64_t)];
  NetworkByteOrder::Store64(encoded_rows, result_rows);
  CHECK_OK(buffer->Write(pos, encoded_rows, sizeof(encoded_rows)));
}

[[nodiscard]] inline bool IsNonKeyColumn(const Schema& schema, int32_t column_id) {
  return !schema.is_key_column(ColumnId(column_id));
}

template<class Container, class Functor>
[[nodiscard]] inline bool Find(const Container& c, const Functor& f) {
  auto end = std::end(c);
  return std::find_if(std::begin(c), end, f) != end;
}

// Note: function return true in case request reads only key columns.
// Such kind of request is used for explicit row locking via separate RPC from postgres.
[[nodiscard]] bool IsOnlyKeyColumnsRequested(const Schema& schema, const PgsqlReadRequestPB& req) {
  if (!req.col_refs().empty()) {
    return !Find(
        req.col_refs(),
        [&schema](const PgsqlColRefPB& col) { return IsNonKeyColumn(schema, col.column_id()); });
  }

  return !Find(
      req.column_refs().ids(),
      [&schema](int32_t col_id) { return IsNonKeyColumn(schema, col_id); });
}

} // namespace

class PgsqlWriteOperation::RowPackContext {
 public:
  RowPackContext(const PgsqlWriteRequestPB& request,
                 const DocOperationApplyData& data,
                 const RowPackerData& packer_data)
      : query_id_(request.stmt_id()),
        data_(data),
        write_id_(data.doc_write_batch->ReserveWriteId()),
        packer_(packer_data.MakePacker()) {
  }

  Result<bool> Add(ColumnId column_id, const QLValuePB& value) {
    return std::visit([column_id, &value](auto& packer) {
      return packer.AddValue(column_id, value);
    }, packer_);
  }

  Status Complete(const RefCntPrefix& encoded_doc_key) {
    auto encoded_value = VERIFY_RESULT(std::visit([](auto& packer) {
      return packer.Complete();
    }, packer_));
    return data_.doc_write_batch->SetPrimitive(
        DocPath(encoded_doc_key.as_slice()), dockv::ValueControlFields(),
        ValueRef(encoded_value), data_.read_operation_data, query_id_, write_id_);
  }

 private:
  rocksdb::QueryId query_id_;
  const DocOperationApplyData& data_;
  const IntraTxnWriteId write_id_;
  dockv::RowPackerVariant packer_;
};

//--------------------------------------------------------------------------------------------------

PgsqlWriteOperation::PgsqlWriteOperation(
    std::reference_wrapper<const PgsqlWriteRequestPB> request, DocReadContextPtr doc_read_context,
    const TransactionOperationContext& txn_op_context, rpc::Sidecars* sidecars)
    : DocOperationBase(request),
      doc_read_context_(std::move(doc_read_context)),
      txn_op_context_(txn_op_context),
      sidecars_(sidecars),
      ysql_skip_row_lock_for_update_(FLAGS_ysql_skip_row_lock_for_update) {}

Status PgsqlWriteOperation::Init(PgsqlResponsePB* response) {
  // Initialize operation inputs.
  response_ = response;

  DocKeyAccessor accessor(doc_read_context_->schema());
  doc_key_ = std::move(VERIFY_RESULT_REF(accessor.GetDecoded(request_)));
  encoded_doc_key_ = doc_key_.EncodeAsRefCntPrefix(Slice(&dockv::KeyEntryTypeAsChar::kHighest, 1));
  encoded_doc_key_.Resize(encoded_doc_key_.size() - 1);

  return Status::OK();
}

// Check if a duplicate value is inserted into a unique index.
Result<bool> PgsqlWriteOperation::HasDuplicateUniqueIndexValue(const DocOperationApplyData& data) {
  VLOG(3) << "Looking for collisions in\n" << DocDBDebugDumpToStr(data);
  // We need to check backwards only for backfilled entries.
  bool ret =
      VERIFY_RESULT(HasDuplicateUniqueIndexValue(data, data.read_time())) ||
      (request_.is_backfill() &&
       VERIFY_RESULT(HasDuplicateUniqueIndexValueBackward(data)));
  if (!ret) {
    VLOG(3) << "No collisions found";
  }
  return ret;
}

Result<bool> PgsqlWriteOperation::HasDuplicateUniqueIndexValueBackward(
    const DocOperationApplyData& data) {
  VLOG(2) << "Looking for collision while going backward. Trying to insert " << doc_key_;

  auto iter = CreateIntentAwareIterator(
      data.doc_write_batch->doc_db(),
      BloomFilterMode::USE_BLOOM_FILTER,
      encoded_doc_key_.as_slice(),
      rocksdb::kDefaultQueryId,
      txn_op_context_,
      data.read_operation_data.WithAlteredReadTime(ReadHybridTime::Max()));

  HybridTime oldest_past_min_ht = VERIFY_RESULT(FindOldestOverwrittenTimestamp(
      iter.get(), SubDocKey(doc_key_), data.read_time().read));
  const HybridTime oldest_past_min_ht_liveness =
      VERIFY_RESULT(FindOldestOverwrittenTimestamp(
          iter.get(),
          SubDocKey(doc_key_, KeyEntryValue::kLivenessColumn),
          data.read_time().read));
  oldest_past_min_ht.MakeAtMost(oldest_past_min_ht_liveness);
  if (!oldest_past_min_ht.is_valid()) {
    return false;
  }
  return HasDuplicateUniqueIndexValue(
      data, ReadHybridTime::SingleTime(oldest_past_min_ht));
}

Result<bool> PgsqlWriteOperation::HasDuplicateUniqueIndexValue(
    const DocOperationApplyData& data, const ReadHybridTime& read_time) {
  // Set up the iterator to read the current primary key associated with the index key.
  DocPgsqlScanSpec spec(doc_read_context_->schema(), request_.stmt_id(), doc_key_);
  dockv::ReaderProjection projection(doc_read_context_->schema());
  auto iterator = DocRowwiseIterator(
      projection,
      *doc_read_context_,
      txn_op_context_,
      data.doc_write_batch->doc_db(),
      data.read_operation_data.WithAlteredReadTime(read_time),
      data.doc_write_batch->pending_op());
  RETURN_NOT_OK(iterator.Init(spec));

  // It is a duplicate value if the index key exists already and the index value (corresponding to
  // the indexed table's primary key) is not the same.
  dockv::PgTableRow table_row(projection);
  if (!VERIFY_RESULT(iterator.PgFetchNext(&table_row))) {
    VLOG(2) << "No collision found while checking at " << yb::ToString(read_time);
    return false;
  }

  ValueBuffer new_value_buffer;
  ValueBuffer existing_value_buffer;

  for (const auto& column_value : request_.column_values()) {
    // Get the column.
    if (!column_value.has_column_id()) {
      return STATUS(InternalError, "column id missing", column_value.DebugString());
    }
    const ColumnId column_id(column_value.column_id());

    // Check column-write operator.
    CHECK(qlexpr::GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert)
      << "Illegal write instruction";

    size_t column_idx = table_row.projection().ColumnIdxById(column_id);
    if (column_idx == dockv::ReaderProjection::kNotFoundIndex) {
      continue;
    }
    auto existing_value = table_row.GetValueByIndex(column_idx);
    if (!existing_value) {
      continue;
    }
    existing_value_buffer.Clear();
    existing_value->AppendTo(
        table_row.projection().columns[column_idx].data_type, &existing_value_buffer);

    // Evaluate column value.
    QLExprResult expr_result;
    RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, expr_result.Writer()));
    new_value_buffer.Clear();
    RETURN_NOT_OK(pggate::WriteColumn(expr_result.Value(), &new_value_buffer));
    if (new_value_buffer.AsSlice() != existing_value_buffer.AsSlice()) {
      VLOG(2) << "Found collision while checking at " << AsString(read_time)
              << "\nExisting: " << AsString(existing_value_buffer)
              << " vs New: " << AsString(new_value_buffer) << "\nUsed read time as "
              << AsString(data.read_time());
      DVLOG(3) << "DocDB is now:\n" << DocDBDebugDumpToStr(data);
      return true;
    }
  }

  VLOG(2) << "No collision while checking at " << AsString(read_time);
  return false;
}

Result<HybridTime> PgsqlWriteOperation::FindOldestOverwrittenTimestamp(
    IntentAwareIterator* iter,
    const SubDocKey& sub_doc_key,
    HybridTime min_read_time) {
  HybridTime result;
  VLOG(3) << "Doing iter->Seek " << doc_key_;
  iter->Seek(doc_key_);
  if (VERIFY_RESULT_REF(iter->Fetch())) {
    const auto bytes = sub_doc_key.EncodeWithoutHt();
    const Slice& sub_key_slice = bytes.AsSlice();
    result = VERIFY_RESULT(iter->FindOldestRecord(sub_key_slice, min_read_time));
    VLOG(2) << "iter->FindOldestRecord returned " << result << " for "
            << SubDocKey::DebugSliceToString(sub_key_slice);
  } else {
    VLOG(3) << "iter->Seek " << doc_key_ << " turned out to be out of records";
  }
  return result;
}

Status PgsqlWriteOperation::Apply(const DocOperationApplyData& data) {
  VLOG(4) << "Write, read time: " << data.read_time() << ", txn: " << txn_op_context_;

  RETURN_NOT_OK(VerifyNoColsMarkedForDeletion(doc_read_context_->schema(), request_));

  auto scope_exit = ScopeExit([this] {
    if (write_buffer_) {
      WriteNumRows(result_rows_, row_num_pos_, write_buffer_);
      response_->set_rows_data_sidecar(narrow_cast<int32_t>(sidecars_->Complete()));
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

    case PgsqlWriteRequestPB::PGSQL_FETCH_SEQUENCE:
      return ApplyFetchSequence(data);
  }
  return Status::OK();
}

Status PgsqlWriteOperation::InsertColumn(
    const DocOperationApplyData& data, const PgsqlColumnValuePB& column_value,
    RowPackContext* pack_context) {
  // Get the column.
  SCHECK(column_value.has_column_id(), InternalError, "Column id missing: $0", column_value);
  // Check column-write operator.
  auto write_instruction = qlexpr::GetTSWriteInstruction(column_value.expr());
  SCHECK_EQ(write_instruction, bfpg::TSOpcode::kScalarInsert, InternalError,
            "Illegal write instruction");

  const ColumnId column_id(column_value.column_id());
  const ColumnSchema& column = VERIFY_RESULT(doc_read_context_->schema().column_by_id(column_id));

  // Evaluate column value.
  RSTATUS_DCHECK_EQ(column_value.expr().expr_case(), PgsqlExpressionPB::ExprCase::kValue,
                    InvalidArgument, "Only values support during insert");
  const auto& value = column_value.expr().value();

  if (!pack_context || !VERIFY_RESULT(pack_context->Add(column_id, value))) {
    // Inserting into specified column.
    DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));
    // If the column has a missing value, we don't want any null values that are inserted to be
    // compacted away. So we store kNullLow instead of kTombstone.
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path,
        IsNull(value) && !IsNull(column.missing_value()) ?
            ValueRef(dockv::ValueEntryType::kNullLow) : ValueRef(value, column.sorting_type()),
        data.read_operation_data, request_.stmt_id()));
  }

  return Status::OK();
}

Status PgsqlWriteOperation::ApplyInsert(const DocOperationApplyData& data, IsUpsert is_upsert) {
  if (!is_upsert) {
    if (request_.is_backfill()) {
      if (VERIFY_RESULT(HasDuplicateUniqueIndexValue(data))) {
        // Unique index value conflict found.
        response_->set_status(PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR);
        response_->set_error_message("Duplicate key found in unique index");
        return Status::OK();
      }
    } else {
      dockv::PgTableRow table_row(projection());
      // Non-backfill requests shouldn't use HasDuplicateUniqueIndexValue because
      // - they should error even if the conflicting row matches
      // - retrieving and calculating whether the conflicting row matches is a waste
      if (VERIFY_RESULT(ReadColumns(data, &table_row))) {
        VLOG(4) << "Duplicate row: " << table_row.ToString();
        // Primary key or unique index value found.
        response_->set_status(PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR);
        response_->set_error_message("Duplicate key found in primary key or unique index");
        return Status::OK();
      }
    }
  }

  if (ShouldYsqlPackRow(doc_read_context_->schema().is_colocated())) {
    RowPackContext pack_context(
        request_, data, VERIFY_RESULT(RowPackerData::Create(request_, *doc_read_context_)));

    auto column_id_extractor = [](const PgsqlColumnValuePB& column_value) {
      return column_value.column_id();
    };

    if (IsMonotonic(request_.column_values(), column_id_extractor)) {
      for (const auto& column_value : request_.column_values()) {
        RETURN_NOT_OK(InsertColumn(data, column_value, &pack_context));
      }
    } else {
      auto column_order = StableSorted(request_.column_values(), column_id_extractor);

      for (const auto& key_and_index : column_order) {
        RETURN_NOT_OK(InsertColumn(
            data, request_.column_values()[key_and_index.original_index], &pack_context));
      }
    }

    RETURN_NOT_OK(pack_context.Complete(encoded_doc_key_));
  } else {
    RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
        DocPath(encoded_doc_key_.as_slice(), KeyEntryValue::kLivenessColumn),
        dockv::ValueControlFields(), ValueRef(dockv::ValueEntryType::kNullLow),
        data.read_operation_data, request_.stmt_id()));

    for (const auto& column_value : request_.column_values()) {
      RETURN_NOT_OK(InsertColumn(data, column_value, /* pack_context=*/ nullptr));
    }
  }

  RETURN_NOT_OK(PopulateResultSet(nullptr));

  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::UpdateColumn(
    const DocOperationApplyData& data, const dockv::PgTableRow& table_row,
    const PgsqlColumnValuePB& column_value, dockv::PgTableRow* returning_table_row,
    QLExprResult* result, RowPackContext* pack_context) {
  // Get the column.
  if (!column_value.has_column_id()) {
    return STATUS(InternalError, "column id missing", column_value.DebugString());
  }
  const ColumnId column_id(column_value.column_id());
  const ColumnSchema& column = VERIFY_RESULT(doc_read_context_->schema().column_by_id(column_id));

  DCHECK(!doc_read_context_->schema().is_key_column(column_id));

  // Evaluate column value.
  QLExprResult result_holder;
  if (!result) {
    // Check column-write operator.
    SCHECK(qlexpr::GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kScalarInsert,
           InternalError,
           "Unsupported DocDB Expression");

    RETURN_NOT_OK(EvalExpr(
        column_value.expr(), table_row, result_holder.Writer(), &doc_read_context_->schema()));
    result = &result_holder;
  }

  // Update RETURNING values
  if (request_.targets_size()) {
    RETURN_NOT_OK(returning_table_row->SetValue(column_id, result->Value()));
  }

  if (!pack_context || !VERIFY_RESULT(pack_context->Add(column_id, result->Value()))) {
    // Inserting into specified column.
    DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));
    // If the column has a missing value, we don't want any null values that are inserted to be
    // compacted away. So we store kNullLow instead of kTombstone.
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path,
        IsNull(result->Value()) && !IsNull(column.missing_value()) ?
            ValueRef(dockv::ValueEntryType::kNullLow) :
            ValueRef(result->Value(), column.sorting_type()),
        data.read_operation_data, request_.stmt_id()));
  }

  return Status::OK();
}

Status PgsqlWriteOperation::ApplyUpdate(const DocOperationApplyData& data) {
  dockv::PgTableRow table_row(projection());

  if (!VERIFY_RESULT(ReadColumns(data, &table_row))) {
    // Row not found.
    response_->set_skipped(true);
    return Status::OK();
  }
  dockv::PgTableRow returning_table_row(projection());
  if (request_.targets_size()) {
    returning_table_row = table_row;
  }

  // skipped is set to false if this operation produces some data to write.
  bool skipped = true;

  if (request_.has_ybctid_column_value()) {
    const auto& schema = doc_read_context_->schema();
    ExpressionHelper expression_helper;
    RETURN_NOT_OK(expression_helper.Init(schema, projection(), request_, table_row));

    skipped = request_.column_new_values().empty();
    const size_t num_non_key_columns = schema.num_columns() - schema.num_key_columns();
    if (FLAGS_ysql_enable_pack_full_row_update &&
        ShouldYsqlPackRow(schema.is_colocated()) &&
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
          return STATUS(InternalError, "column id missing", column_value.ShortDebugString());
        }
        const ColumnId column_id(column_value.column_id());
        const ColumnSchema& column = VERIFY_RESULT(
            doc_read_context_->schema().column_by_id(column_id));

        // Check column-write operator.
        RSTATUS_DCHECK_EQ(
            qlexpr::GetTSWriteInstruction(column_value.expr()), bfpg::TSOpcode::kScalarInsert,
            InternalError, "Illegal write instruction");

        // Evaluate column value.
        QLExprResult expr_result;
        RETURN_NOT_OK(EvalExpr(column_value.expr(), table_row, expr_result.Writer()));

        // Inserting into specified column.
        DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));
        // If the column has a missing value, we don't want any null values that are inserted to be
        // compacted away. So we store kNullLow instead of kTombstone.
        RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
            sub_path,
            IsNull(expr_result.Value()) && !IsNull(column.missing_value()) ?
                ValueRef(dockv::ValueEntryType::kNullLow) :
                ValueRef(expr_result.Value(), column.sorting_type()),
            data.read_operation_data, request_.stmt_id()));
        skipped = false;
      }
    }
  }

  if (request_.targets_size()) {
    // Returning the values after the update.
    RETURN_NOT_OK(PopulateResultSet(&returning_table_row));
  } else {
    // Returning the values before the update.
    RETURN_NOT_OK(PopulateResultSet(&table_row));
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
  dockv::PgTableRow table_row(projection());
  if (!VERIFY_RESULT(ReadColumns(data, &table_row))) {
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
      DocPath(encoded_doc_key_.as_slice()), data.read_operation_data));

  RETURN_NOT_OK(PopulateResultSet(&table_row));

  response_->set_rows_affected_count(num_deleted);
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::ApplyTruncateColocated(const DocOperationApplyData& data) {
  RETURN_NOT_OK(data.doc_write_batch->DeleteSubDoc(DocPath(
      encoded_doc_key_.as_slice()), data.read_operation_data));
  response_->set_status(PgsqlResponsePB::PGSQL_STATUS_OK);
  return Status::OK();
}

Status PgsqlWriteOperation::ApplyFetchSequence(const DocOperationApplyData& data) {
  dockv::PgTableRow table_row(projection());
  DCHECK(request_.has_fetch_sequence_params()) << "Invalid input: fetch sequence without params";
  if (!VERIFY_RESULT(ReadColumns(data, &table_row))) {
    // Row not found.
    return STATUS(NotFound, "Unable to find relation for sequence");
  }

  // Retrieve fetch parameters from the request
  auto fetch_count = request_.fetch_sequence_params().fetch_count();
  DCHECK(fetch_count > 0) << "Invalid input: sequence fetch count must be positive";
  auto inc_by = static_cast<__int128_t>(request_.fetch_sequence_params().inc_by());
  DCHECK(inc_by != 0) << "Invalid input: sequence step must not be zero";
  auto min_value = static_cast<__int128_t>(request_.fetch_sequence_params().min_value());
  auto max_value = static_cast<__int128_t>(request_.fetch_sequence_params().max_value());
  auto cycle = request_.fetch_sequence_params().cycle();

  // Prepare to write response data
  if (write_buffer_ == nullptr && sidecars_) {
    // Reserve space for num rows.
    write_buffer_ = &sidecars_->Start();
    row_num_pos_ = write_buffer_->Position();
    pggate::PgWire::WriteInt64(0, write_buffer_);
  }

  // Read last_value and is_called from the sequence record
  ColumnId last_value_column_id(request_.col_refs()[0].column_id());
  ColumnId is_called_column_id(request_.col_refs()[1].column_id());
  const auto& last_value_column = VERIFY_RESULT_REF(
      doc_read_context_->schema().column_by_id(last_value_column_id));
  const auto& is_called_column = VERIFY_RESULT_REF(
      doc_read_context_->schema().column_by_id(is_called_column_id));
  const auto& last_value = table_row.GetValueByColumnId(last_value_column_id);
  const auto& is_called = table_row.GetValueByColumnId(is_called_column_id);

  auto first_fetched = static_cast<__int128_t>(last_value->int64_value());
  // If last value is called, advance the first value one step
  if (is_called->bool_value()) {
    // Check for the limit
    if (inc_by > 0 && first_fetched > max_value - inc_by) {
      // At the limit, have to cycle
      if (!cycle) {
        return STATUS(QLError,
                      "nextval: reached maximum value of sequence \"%s\" (%s)",
                      Slice(),
                      PgsqlError(YBPgErrorCode::YB_PG_SEQUENCE_GENERATOR_LIMIT_EXCEEDED));
      }
      first_fetched = min_value;
    } else if (inc_by < 0 && first_fetched < min_value - inc_by) {
      // At the limit, have to cycle
      if (!cycle) {
        return STATUS(QLError,
                      "nextval: reached minimum value of sequence \"%s\" (%s)",
                      Slice(),
                      PgsqlError(YBPgErrorCode::YB_PG_SEQUENCE_GENERATOR_LIMIT_EXCEEDED));
      }
      first_fetched = max_value;
    } else { // single fetch does not go over the limit
      first_fetched += inc_by;
    }
  }
  --fetch_count;
  // If fetching of the requested number of values would make the value to go over
  // the boundary, reduce fetch count to the highest possible value
  if (inc_by > 0 && first_fetched + (inc_by * fetch_count) > max_value) {
    fetch_count = static_cast<uint32>((max_value - first_fetched) / inc_by);
  } else if (inc_by < 0 && first_fetched + (inc_by * fetch_count) < min_value) {
    fetch_count = static_cast<uint32>((min_value - first_fetched) / inc_by);
  }
  // set last value of the range
  auto last_fetched = first_fetched + (inc_by * fetch_count);

  // Update the sequence row
  if (!is_called->bool_value()) {
    QLValuePB new_is_called;
    new_is_called.set_bool_value(true);
    DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(is_called_column_id));
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path, ValueRef(new_is_called, is_called_column.sorting_type()),
        data.read_operation_data, request_.stmt_id()));
  }
  if (last_value->int64_value() != last_fetched) {
    QLValuePB new_last_value;
    new_last_value.set_int64_value(last_fetched);
    DocPath sub_path(encoded_doc_key_.as_slice(),
                     KeyEntryValue::MakeColumnId(last_value_column_id));
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path, ValueRef(new_last_value, last_value_column.sorting_type()),
        data.read_operation_data, request_.stmt_id()));
  }
  // Return fetched range to the client
  QLValuePB fetch_value;
  fetch_value.set_int64_value(first_fetched);
  RETURN_NOT_OK(pggate::WriteColumn(fetch_value, write_buffer_));
  fetch_value.set_int64_value(last_fetched);
  RETURN_NOT_OK(pggate::WriteColumn(fetch_value, write_buffer_));
  result_rows_ = 2;

  return Status::OK();
}

const dockv::ReaderProjection& PgsqlWriteOperation::projection() const {
  if (!projection_) {
    projection_.emplace();
    InitProjection(doc_read_context_->schema(), request_, &*projection_);
    VLOG_WITH_FUNC(4)
        << "projection: " << projection_->ToString() << ", request: " << AsString(request_);
  }

  return *projection_;
}

Result<bool> PgsqlWriteOperation::ReadColumns(
    const DocOperationApplyData& data, dockv::PgTableRow* table_row) {
  // Filter the columns using primary key.
  DocPgsqlScanSpec spec(doc_read_context_->schema(), request_.stmt_id(), doc_key_);
  auto iterator = DocRowwiseIterator(
      projection(),
      *doc_read_context_,
      txn_op_context_,
      data.doc_write_batch->doc_db(),
      data.read_operation_data,
      data.doc_write_batch->pending_op());
  RETURN_NOT_OK(iterator.Init(spec));
  if (!VERIFY_RESULT(iterator.PgFetchNext(table_row))) {
    return false;
  }
  data.restart_read_ht->MakeAtLeast(VERIFY_RESULT(iterator.RestartReadHt()));

  return true;
}

Status PgsqlWriteOperation::PopulateResultSet(const dockv::PgTableRow* table_row) {
  if (write_buffer_ == nullptr && sidecars_) {
    // Reserve space for num rows.
    write_buffer_ = &sidecars_->Start();
    row_num_pos_ = write_buffer_->Position();
    pggate::PgWire::WriteInt64(0, write_buffer_);
  }
  ++result_rows_;
  for (const PgsqlExpressionPB& expr : request_.targets()) {
    if (expr.has_column_id()) {
      QLExprResult value;
      if (expr.column_id() == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
        // Strip cotable ID / colocation ID from the serialized DocKey before returning it
        // as ybctid.
        Slice tuple_id = encoded_doc_key_.as_slice();
        if (tuple_id.starts_with(dockv::KeyEntryTypeAsChar::kTableId)) {
          tuple_id.remove_prefix(1 + kUuidSize);
        } else if (tuple_id.starts_with(dockv::KeyEntryTypeAsChar::kColocationId)) {
          tuple_id.remove_prefix(1 + sizeof(ColocationId));
        }
        value.Writer().NewValue().set_binary_value(tuple_id.data(), tuple_id.size());
      } else {
        RETURN_NOT_OK(EvalExpr(expr, *table_row, value.Writer()));
      }
      RETURN_NOT_OK(pggate::WriteColumn(value.Value(), write_buffer_));
    }
  }
  return Status::OK();
}

Status PgsqlWriteOperation::GetDocPaths(GetDocPathsMode mode,
                                        DocPathsToLock *paths,
                                        IsolationLevel *level) const {
  // When this write operation requires a read, it requires a read snapshot so paths will be locked
  // in snapshot isolation for consistency. Otherwise, pure writes will happen in serializable
  // isolation so that they will serialize but do not conflict with one another.
  //
  // Currently, only keys that are being written are locked, no lock is taken on read at the
  // snapshot isolation level.
  *level = RequireReadSnapshot() ? IsolationLevel::SNAPSHOT_ISOLATION
                                 : IsolationLevel::SERIALIZABLE_ISOLATION;

  const auto is_update = request_.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPDATE;
  switch (mode) {
    case GetDocPathsMode::kLock: {
      if (PREDICT_FALSE(ysql_skip_row_lock_for_update_) && is_update &&
          !NewValuesHaveExpression(request_)) {
        DocKeyColumnPathBuilderHolder holder(encoded_doc_key_.as_slice());
        paths->emplace_back(holder.builder().Build(dockv::SystemColumnIds::kLivenessColumn));
        return Status::OK();
      }
      break;
    }
    case GetDocPathsMode::kIntents: {
      if (!is_update) {
        break;
      }
      const auto& column_values = request_.column_new_values();

      if (column_values.empty()) {
        break;
      }

      DocKeyColumnPathBuilderHolder holder(encoded_doc_key_.as_slice());
      auto& builder = holder.builder();
      for (const auto& column_value : column_values) {
        paths->emplace_back(builder.Build(column_value.column_id()));
      }
      return Status::OK();
    }
    case GetDocPathsMode::kStrongReadIntents: {
      if (!is_update || PREDICT_FALSE(ysql_skip_row_lock_for_update_)) {
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

Result<size_t> PgsqlReadOperation::Execute(
    const YQLStorageIf& ql_storage,
    const ReadOperationData& read_operation_data,
    bool is_explicit_request_read_time,
    const DocReadContext& doc_read_context,
    const DocReadContext* index_doc_read_context,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    WriteBuffer* result_buffer,
    HybridTime* restart_read_ht,
    const DocDBStatistics* statistics) {
  // Verify that this request references no columns marked for deletion.
  RETURN_NOT_OK(VerifyNoRefColsMarkedForDeletion(doc_read_context.schema(),
                                                 request_));
  size_t fetched_rows = 0;
  auto num_rows_pos = result_buffer->Position();
  // Reserve space for fetched rows count.
  pggate::PgWire::WriteInt64(0, result_buffer);
  auto se = ScopeExit([&fetched_rows, num_rows_pos, result_buffer] {
    WriteNumRows(fetched_rows, num_rows_pos, result_buffer);
  });
  VLOG(4) << "Read, read operation data: " << read_operation_data.ToString() << ", txn: "
          << txn_op_context_;

  // Fetching data.
  bool has_paging_state = false;
  if (request_.batch_arguments_size() > 0) {
    fetched_rows = VERIFY_RESULT(ExecuteBatchYbctid(
        ql_storage, read_operation_data, doc_read_context, pending_op, result_buffer,
        restart_read_ht, statistics));
  } else if (request_.has_sampling_state()) {
    fetched_rows = VERIFY_RESULT(ExecuteSample(
        ql_storage, read_operation_data, is_explicit_request_read_time, doc_read_context,
        pending_op, result_buffer, restart_read_ht, &has_paging_state, statistics));
  } else {
    fetched_rows = VERIFY_RESULT(ExecuteScalar(
        ql_storage, read_operation_data, is_explicit_request_read_time, doc_read_context,
        index_doc_read_context, pending_op, result_buffer, restart_read_ht, &has_paging_state,
        statistics));
  }

  VTRACE(1, "Fetched $0 rows. $1 paging state", fetched_rows, (has_paging_state ? "No" : "Has"));
  SCHECK(table_iter_ != nullptr, InternalError, "table iterator is invalid");

  *restart_read_ht = VERIFY_RESULT(table_iter_->RestartReadHt());
  if (index_iter_) {
    restart_read_ht->MakeAtLeast(VERIFY_RESULT(index_iter_->RestartReadHt()));
  }

  // Versions of pggate < 2.17.1 are not capable of processing response protos that contain RPC
  // sidecars holding data other than the rows returned by DocDB. During an upgrade, it is possible
  // that a pggate of version < 2.17.1 may receive a response from a tserver containing the metrics
  // sidecar, causing a crash. To prevent this, send the scanned row count only if the incoming
  // request object contains the metrics capture field. This serves to validate that the requesting
  // pggate has indeed upgraded to a version that is capable of unpacking the metric sidecar.
  if (request_.has_metrics_capture()) {
    response_.mutable_metrics()->set_scanned_table_rows(scanned_table_rows_);
    if (scanned_index_rows_ > 0) {
      response_.mutable_metrics()->set_scanned_index_rows(scanned_index_rows_);
    }
  }

  return fetched_rows;
}

Result<size_t> PgsqlReadOperation::ExecuteSample(
    const YQLStorageIf& ql_storage,
    const ReadOperationData& read_operation_data,
    bool is_explicit_request_read_time,
    const DocReadContext& doc_read_context,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    WriteBuffer* result_buffer,
    HybridTime* restart_read_ht,
    bool* has_paging_state,
    const DocDBStatistics* statistics) {
  *has_paging_state = false;
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

  VLOG(2) << "Start sampling tablet with sampling_state=" << sampling_state.ShortDebugString();

  auto projection = CreateProjection(doc_read_context.schema(), request_);
  table_iter_ = VERIFY_RESULT(CreateIterator(
      ql_storage, request_, projection, doc_read_context, txn_op_context_,
      read_operation_data, is_explicit_request_read_time, pending_op, statistics));
  bool scan_time_exceeded = false;
  auto stop_scan = read_operation_data.deadline - FLAGS_ysql_scan_deadline_margin_ms * 1ms;
  while (VERIFY_RESULT(table_iter_->FetchNext(nullptr))) {
    samplerows++;
    if (numrows < targrows) {
      // Select first targrows of the table. If first partition(s) have less than that, next
      // partition starts to continue populating it's reservoir starting from the numrows' position:
      // the numrows, as well as other sampling state variables is returned and copied over to the
      // next sampling request
      Slice ybctid = table_iter_->GetTupleId();
      reservoir[numrows++].set_binary_value(ybctid.data(), ybctid.size());
    } else {
      // At least targrows tuples have already been collected, now algorithm skips increasing number
      // of row before taking next one into the reservoir
      if (rowstoskip <= 0) {
        // Take ybctid of the current row
        Slice ybctid = table_iter_->GetTupleId();
        // Pick random tuple in the reservoir to replace
        double rvalue;
        int k;
        YbgSamplerRandomFract(rstate, &rvalue);
        k = static_cast<int>(targrows * rvalue);
        // Overwrite previous value with new one
        reservoir[k].set_binary_value(ybctid.data(), ybctid.size());
        // Choose next number of rows to skip
        YbgReservoirGetNextS(rstate, samplerows, targrows, &rowstoskip);
        VLOG(3) << "Next reservoir sampling rowstoskip=" << rowstoskip;
      } else {
        rowstoskip -= 1;
      }
    }

    // Check if we are running out of time
    scan_time_exceeded = CoarseMonoClock::now() >= stop_scan;
    if (scan_time_exceeded) {
      VLOG(1) << "ANALYZE sampling scan exceeded deadline";
      break;
    }
  }

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
  if (request_.return_paging_state() && scan_time_exceeded) {
    *has_paging_state = VERIFY_RESULT(SetPagingState(
        table_iter_.get(), doc_read_context.schema(), read_operation_data.read_time));
  }

  VLOG(2) << "End sampling with new_sampling_state=" << new_sampling_state->ShortDebugString();

  return fetched_rows;
}

Result<size_t> PgsqlReadOperation::ExecuteScalar(
    const YQLStorageIf& ql_storage,
    const ReadOperationData& read_operation_data,
    bool is_explicit_request_read_time,
    const DocReadContext& doc_read_context,
    const DocReadContext* index_doc_read_context,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    WriteBuffer* result_buffer,
    HybridTime* restart_read_ht,
    bool* has_paging_state,
    const DocDBStatistics* statistics) {
  // Requests normally have a limit on how many rows to return
  auto row_count_limit = std::numeric_limits<std::size_t>::max();

  if (request_.has_limit() && request_.limit() > 0) {
    row_count_limit = request_.limit();
  }

  // We also limit the response's size.
  auto response_size_limit = std::numeric_limits<std::size_t>::max();

  if (request_.has_size_limit() && request_.size_limit() > 0) {
    response_size_limit = request_.size_limit();
  }

  VLOG(4) << "Row count limit: " << row_count_limit << ", size limit: " << response_size_limit;

  // Create the projection of regular columns selected by the row block plus any referenced in
  // the WHERE condition. When DocRowwiseIterator::NextRow() populates the value map, it uses this
  // projection only to scan sub-documents. The query schema is used to select only referenced
  // columns and key columns.
  auto doc_projection = CreateProjection(doc_read_context.schema(), request_);
  FilteringIterator table_iter(&table_iter_);
  RETURN_NOT_OK(table_iter.Init(
      ql_storage, request_, doc_projection, doc_read_context, txn_op_context_, read_operation_data,
      is_explicit_request_read_time, pending_op, statistics));

  std::optional<IndexState> index_state;
  if (index_doc_read_context) {
    const auto& index_schema = index_doc_read_context->schema();
    const auto idx = index_schema.find_column("ybidxbasectid");
    SCHECK_NE(idx, Schema::kColumnNotFound, Corruption, "ybidxbasectid not found in index schema");
    index_state.emplace(
        index_schema, request_.index_request(), &index_iter_, index_schema.column_id(idx));
    RETURN_NOT_OK(index_state->iter.Init(
        ql_storage, request_.index_request(), index_state->projection, *index_doc_read_context,
        txn_op_context_, read_operation_data, is_explicit_request_read_time, pending_op,
        statistics));
  }

  // Set scan end time. We want to iterate as long as we can, but stop before client timeout.
  // The more rows we do per request, the less RPCs will be needed, but if client times out,
  // efforts are wasted.
  bool scan_time_exceeded = false;
  auto stop_scan = read_operation_data.deadline - FLAGS_ysql_scan_deadline_margin_ms * 1ms;
  size_t match_count = 0;
  bool limit_exceeded = false;
  size_t fetched_rows = 0;
  dockv::PgTableRow row(doc_projection);
  const auto& table_id = request_.index_request().table_id();
  do {
    const auto fetch_result = VERIFY_RESULT(FetchTableRow(
        table_id, &table_iter, index_state ? &*index_state : nullptr, &row));
    // If changing this code, see also PgsqlReadOperation::ExecuteBatchYbctid.
    if (fetch_result == FetchResult::NotFound) {
      break;
    }
    ++scanned_table_rows_;
    if (fetch_result == FetchResult::Found) {
      ++match_count;
      if (request_.is_aggregate()) {
        RETURN_NOT_OK(EvalAggregate(row));
      } else {
        RETURN_NOT_OK(PopulateResultSet(row, result_buffer));
        ++fetched_rows;
      }
    }
    scan_time_exceeded = CoarseMonoClock::now() >= stop_scan;
    limit_exceeded =
      (scan_time_exceeded ||
       fetched_rows >= row_count_limit ||
       result_buffer->size() >= response_size_limit);
  } while (!limit_exceeded);

  // Output aggregate values accumulated while looping over rows
  if (request_.is_aggregate() && match_count > 0) {
    RETURN_NOT_OK(PopulateAggregate(result_buffer));
    ++fetched_rows;
  }

  VLOG(1) << "Stopped iterator after " << match_count << " matches, " << fetched_rows
          << " rows fetched. Response buffer size: " << result_buffer->size()
          << ", response size limit: " << response_size_limit
          << ", deadline is " << (scan_time_exceeded ? "" : "not ") << "exceeded";

  if (PREDICT_FALSE(FLAGS_TEST_slowdown_pgsql_aggregate_read_ms > 0) && request_.is_aggregate()) {
    TRACE("Sleeping for $0 ms", FLAGS_TEST_slowdown_pgsql_aggregate_read_ms);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_pgsql_aggregate_read_ms));
  }

  *has_paging_state = false;
  // Unless iterated to the end, pack current iterator position into response, so follow up request
  // can seek to correct position and continue
  if (request_.return_paging_state() && limit_exceeded) {
    auto* iterator = table_iter_.get();
    const auto* read_context = &doc_read_context;
    if (index_state) {
      iterator = index_iter_.get();
      read_context = index_doc_read_context;
    }
    DCHECK(iterator && read_context);
    *has_paging_state = VERIFY_RESULT(SetPagingState(
        iterator, read_context->schema(), read_operation_data.read_time));
  }

  if (index_state) {
    scanned_index_rows_ += index_state->scanned_rows;
  }
  return fetched_rows;
}

Result<size_t> PgsqlReadOperation::ExecuteBatchYbctid(
    const YQLStorageIf& ql_storage,
    const ReadOperationData& read_operation_data,
    const DocReadContext& doc_read_context,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    WriteBuffer* result_buffer,
    HybridTime* restart_read_ht,
    const DocDBStatistics* statistics) {
  const auto& batch_args = request_.batch_arguments();
  auto min_arg = batch_args.begin();
  auto max_arg = batch_args.begin();
  for(auto batch_arg_it = batch_args.begin() + 1;
      batch_arg_it != batch_args.end(); ++batch_arg_it) {
    SCHECK(batch_arg_it->has_ybctid(), InternalError, "ybctid arguments can be batched only");
    if (min_arg->ybctid().value() > batch_arg_it->ybctid().value()) {
      min_arg = batch_arg_it;
    } else if (max_arg->ybctid().value() < batch_arg_it->ybctid().value()) {
      max_arg = batch_arg_it;
    }
  }

  // We limit the response's size.
  auto response_size_limit = std::numeric_limits<std::size_t>::max();

  if (request_.has_size_limit() && request_.size_limit() > 0) {
    response_size_limit = request_.size_limit();
  }

  auto projection = CreateProjection(doc_read_context.schema(), request_);
  dockv::PgTableRow row(projection);
  std::optional<FilteringIterator> iter;
  size_t row_count = 0;
  size_t fetched_rows = 0;
  size_t filtered_rows = 0;
  for (const auto& batch_argument : batch_args) {
    if (!iter) {
      // It can be the case like when there is a tablet split that we still want
      // to continue seeking through all the given batch arguments even though one
      // of them wasn't found. If it wasn't found, table_iter_ becomes invalid
      // and we have to make a new iterator.
      // TODO (dmitry): In case of iterator recreation info from RestartReadHt field will be lost.
      //                The #17159 issue is created for this problem.
      iter.emplace(&table_iter_);
      RETURN_NOT_OK(iter->InitForYbctid(
          ql_storage, request_, projection, doc_read_context, txn_op_context_, read_operation_data,
          min_arg->ybctid().value(), max_arg->ybctid().value(), pending_op, statistics,
          SkipSeek::kTrue));
    }

    // If changing this code, see also PgsqlReadOperation::ExecuteScalar.
    switch (VERIFY_RESULT(
        iter->FetchTuple(batch_argument.ybctid().value().binary_value(), &row))) {
      case FetchResult::NotFound:
        // rebuild iterator on next iteration
        iter = std::nullopt;
        break;
      case FetchResult::FilteredOut:
        ++filtered_rows;
        break;
      case FetchResult::Found:
        ++row_count;
        if (request_.is_aggregate()) {
          RETURN_NOT_OK(EvalAggregate(row));
        } else {
          RETURN_NOT_OK(PopulateResultSet(row, result_buffer));
          response_.add_batch_orders(batch_argument.order());
          ++fetched_rows;
        }
        break;
    }

    if (result_buffer->size() >= response_size_limit) {
      VLOG(1) << "Stopped iterator after " << row_count << " rows fetched (out of "
              << request_.batch_arguments_size() << " matches). Response buffer size: "
              << result_buffer->size() << ", response size limit: " << response_size_limit;
      break;
    }
  }

  // Output aggregate values accumulated while looping over rows
  if (request_.is_aggregate() && row_count > 0) {
    RETURN_NOT_OK(PopulateAggregate(result_buffer));
    ++fetched_rows;
  }

  // Set status for this batch.
  if (result_buffer->size() >= response_size_limit)
    response_.set_batch_arg_count(row_count);
  else
    // Mark all rows were processed even in case some of the ybctids were not found.
    response_.set_batch_arg_count(request_.batch_arguments_size());

  scanned_table_rows_ += filtered_rows + row_count;
  return fetched_rows;
}

Result<bool> PgsqlReadOperation::SetPagingState(
    YQLRowwiseIteratorIf* iter, const Schema& schema, const ReadHybridTime& read_time) {
  // Set the paging state for next row.
  SubDocKey next_row_key;
  RETURN_NOT_OK(iter->GetNextReadSubDocKey(&next_row_key));
  // When the "limit" number of rows are returned and we are asked to return the paging state,
  // return the partition key and row key of the next row to read in the paging state if there are
  // still more rows to read. Otherwise, leave the paging state empty which means we are done
  // reading from this tablet.
  if (next_row_key.doc_key().empty()) {
    return false;
  }

  auto* paging_state = response_.mutable_paging_state();
  auto encoded_next_row_key = next_row_key.Encode().ToStringBuffer();
  if (schema.num_hash_key_columns() > 0) {
    paging_state->set_next_partition_key(
        dockv::PartitionSchema::EncodeMultiColumnHashValue(next_row_key.doc_key().hash()));
  } else {
    paging_state->set_next_partition_key(encoded_next_row_key);
  }
  paging_state->set_next_row_key(std::move(encoded_next_row_key));

  if (FLAGS_pgsql_consistent_transactional_paging) {
    read_time.AddToPB(paging_state);
  } else {
    // Using SingleTime will help avoid read restarts on second page and later but will
    // potentially produce stale results on those pages.
    auto per_row_consistent_read_time = ReadHybridTime::SingleTime(read_time.read);
    per_row_consistent_read_time.AddToPB(paging_state);
  }

  return true;
}

void NopEncoder(
    const dockv::PgTableRow& row, WriteBuffer* buffer, const dockv::PgWireEncoderEntry* chain) {
}

template <bool kLast>
void NullEncoder(
    const dockv::PgTableRow& row, WriteBuffer* buffer, const dockv::PgWireEncoderEntry* chain) {
  buffer->PushBack(1);
  dockv::CallNextEncoder<kLast>(row, buffer, chain);
}

dockv::PgWireEncoderEntry MakeNullEncoder(bool last) {
  return dockv::PgWireEncoderEntry {
    .encoder = last ? NullEncoder<true> : NullEncoder<false>,
    .data = 0,
  };
}

template <bool kLast>
void TupleIdEncoder(
    const dockv::PgTableRow& row, WriteBuffer* buffer, const dockv::PgWireEncoderEntry* chain) {
  auto table_iter = reinterpret_cast<YQLRowwiseIteratorIf::UniPtr*>(chain->data)->get();
  pggate::WriteBinaryColumn(table_iter->GetTupleId(), buffer);
  dockv::CallNextEncoder<kLast>(row, buffer, chain);
}

dockv::PgWireEncoderEntry GetEncoder(
    YQLRowwiseIteratorIf::UniPtr* table_iter, const dockv::PgTableRow& table_row,
    const PgsqlExpressionPB& expr, bool last) {
  if (expr.expr_case() == PgsqlExpressionPB::kColumnId) {
    if (expr.column_id() != to_underlying(PgSystemAttrNum::kYBTupleId)) {
      auto index = table_row.projection().ColumnIdxById(ColumnId(expr.column_id()));
      if (index != dockv::ReaderProjection::kNotFoundIndex) {
        return table_row.GetEncoder(index, last);
      }
      return MakeNullEncoder(last);
    }
    return dockv::PgWireEncoderEntry {
      .encoder = last ? TupleIdEncoder<true> : TupleIdEncoder<false>,
      .data = reinterpret_cast<size_t>(table_iter),
    };
  }
  return MakeNullEncoder(last);
}

Status PgsqlReadOperation::PopulateResultSet(const dockv::PgTableRow& table_row,
                                             WriteBuffer *result_buffer) {
  if (target_encoders_.empty()) {
    const auto& targets = request_.targets();
    const auto size = targets.size();
    if (size) {
      target_encoders_.reserve(size);
      for (auto it = targets.begin(), end = targets.end();;) {
        const auto& expr = *it;
        bool last = ++it == end;
        target_encoders_.push_back(GetEncoder(&table_iter_, table_row, expr, last));
        if (last) {
          break;
        }
      }
    } else {
      target_encoders_.push_back(dockv::PgWireEncoderEntry {
        .encoder = NopEncoder,
        .data = 0,
      });
    }
  }
  target_encoders_.front().Invoke(table_row, result_buffer);
  return Status::OK();
}

Status PgsqlReadOperation::GetSpecialColumn(ColumnIdRep column_id, QLValuePB* result) {
  // Get row key and save to QLValue.
  // TODO(neil) Check if we need to append a table_id and other info to TupleID. For example, we
  // might need info to make sure the TupleId by itself is a valid reference to a specific row of
  // a valid table.
  if (column_id == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
    const Slice tuple_id = table_iter_->GetTupleId();
    result->set_binary_value(tuple_id.data(), tuple_id.size());
    return Status::OK();
  }

  return STATUS_SUBSTITUTE(InvalidArgument, "Invalid column ID: $0", column_id);
}

Status PgsqlReadOperation::EvalAggregate(const dockv::PgTableRow& table_row) {
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

Status PgsqlReadOperation::PopulateAggregate(WriteBuffer *result_buffer) {
  int column_count = request_.targets().size();
  for (int rscol_index = 0; rscol_index < column_count; rscol_index++) {
    RETURN_NOT_OK(pggate::WriteColumn(aggr_result_[rscol_index].Value(), result_buffer));
  }
  return Status::OK();
}

Status PgsqlReadOperation::GetIntents(
    const Schema& schema, IsolationLevel level, LWKeyValueWriteBatchPB* out) {
  const auto row_mark = request_.has_row_mark_type() ? request_.row_mark_type() : ROW_MARK_ABSENT;
  if (IsValidRowMarkType(row_mark)) {
    RSTATUS_DCHECK(request_.has_wait_policy(), IllegalState, "wait policy is expected");
    out->set_wait_policy(request_.wait_policy());
  }

  IntentInserter inserter(out);
  const auto has_batch_arguments = !request_.batch_arguments().empty();

  DocKeyAccessor accessor(schema);
  if (!(has_batch_arguments || request_.has_ybctid_column_value())) {
    inserter.Add(
        VERIFY_RESULT(accessor.GetEncoded(request_)), {level, row_mark, KeyOnlyRequested::kFalse});
    return Status::OK();
  }

  const IntentMode mode{
      level, row_mark, KeyOnlyRequested(IsOnlyKeyColumnsRequested(schema, request_))};
  for (const auto& batch_argument : request_.batch_arguments()) {
    inserter.Add(VERIFY_RESULT(accessor.GetEncoded(batch_argument.ybctid())), mode);
  }
  if (!has_batch_arguments) {
    DCHECK(request_.has_ybctid_column_value());
    inserter.Add(VERIFY_RESULT(accessor.GetEncoded(request_.ybctid_column_value())), mode);
  }
  return Status::OK();
}

}  // namespace yb::docdb
