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

#include "yb/docdb/pgsql_operation.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <ranges>
#include <string>
#include <unordered_set>
#include <variant>
#include <utility>
#include <vector>

#include <boost/logic/tribool.hpp>

#include "yb/common/common.pb.h"
#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/row_mark.h"

#include "yb/common/transaction_error.h"

#include "yb/docdb/doc_pg_expr.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/docdb_pgapi.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/ql_storage_interface.h"

#include "yb/dockv/doc_path.h"
#include "yb/dockv/doc_vector_id.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/packed_value.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/primitive_value_util.h"
#include "yb/dockv/reader_projection.h"

#include "yb/gutil/macros.h"

#include "yb/qlexpr/ql_expr_util.h"

#include "yb/rpc/sidecars.h"

#include "yb/util/algorithm_util.h"
#include "yb/util/debug.h"
#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/range.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"
#include "yb/util/yb_pg_errcodes.h"

#include "yb/vector_index/vector_index_if.h"

#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

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

// Disable packed row by default in debug builds.
constexpr bool kYsqlEnablePackedRowTargetVal = !yb::kIsDebug;
DEFINE_RUNTIME_AUTO_bool(ysql_enable_packed_row, kExternal,
                         !kYsqlEnablePackedRowTargetVal, kYsqlEnablePackedRowTargetVal,
                         "Whether packed row is enabled for YSQL.");

DEFINE_RUNTIME_bool(ysql_enable_packed_row_for_colocated_table, true,
                    "Whether to enable packed row for colocated tables.");

DEFINE_UNKNOWN_uint64(
    ysql_packed_row_size_limit, 0,
    "Packed row size limit for YSQL in bytes. 0 to make this equal to SSTable block size.");

DEFINE_RUNTIME_bool(ysql_enable_pack_full_row_update, false,
                    "Whether to enable packed row for full row update.");

DEFINE_RUNTIME_bool(ysql_mark_update_packed_row, false,
                    "Whether to mark packed rows created from UPDATE operations with a flag. "
                    "This allows CDC to differentiate between INSERT and UPDATE packed rows."
                    "Default is false.");
DEFINE_RUNTIME_PREVIEW_bool(ysql_use_packed_row_v2, false,
                            "Whether to use packed row V2 when row packing is enabled.");

DEFINE_RUNTIME_AUTO_bool(ysql_skip_row_lock_for_update, kExternal, true, false,
    "By default DocDB operations for YSQL take row-level locks. If set to true, DocDB will instead "
    "take finer column-level locks instead of locking the whole row. This may cause issues with "
    "data integrity for operations with implicit dependencies between columns.");

DEFINE_RUNTIME_bool(vector_index_skip_filter_check, false,
                    "Whether to skip filter check during vector index search.");

DEFINE_RUNTIME_bool(vector_index_no_deletions_skip_filter_check, true,
    "Whether to skip filter check during vector index search if table does not have "
    "updates/deletions.");

DECLARE_uint64(rpc_max_message_size);
DECLARE_double(max_buffer_size_to_rpc_limit_ratio);
DECLARE_bool(vector_index_dump_stats);

namespace yb::docdb {

bool TEST_vector_index_filter_allowed = true;
size_t TEST_vector_index_max_checked_entries = std::numeric_limits<size_t>::max();

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
    return Build(dockv::KeyEntryType::kSystemColumnId, std::to_underlying(column_id));
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

  void Add(Slice encoded_key, bool pk_is_known, const IntentMode& mode) {
    if (mode.regular_read()) {
      Add(encoded_key, pk_is_known);
    } else if (mode.key_only_read()) {
      auto& buf = buffer();
      buf.Clear();
      DocKeyColumnPathBuilder doc_key_builder(&buf, encoded_key);
      Add(doc_key_builder.Build(dockv::SystemColumnIds::kLivenessColumn), pk_is_known);
    }

    if (mode.row_lock()) {
      Add(encoded_key, pk_is_known, /*row_lock=*/ true);
    }
  }

 private:
  [[nodiscard]] dockv::KeyBytes& buffer() {
    if (!buffer_) {
      buffer_.emplace();
    }
    return *buffer_;
  }

  void Add(Slice encoded_key, bool pk_is_known, bool is_row_lock = false) {
    auto& pair = *out_.add_read_pairs();
    pair.dup_key(encoded_key);
    pair.dup_value(Slice(
        is_row_lock ? &dockv::ValueEntryTypeAsChar::kRowLock
                    : &dockv::ValueEntryTypeAsChar::kNullLow,
        1));
    pair.set_pk_is_known(pk_is_known);
  }

  std::optional<dockv::KeyBytes> buffer_;
  LWKeyValueWriteBatchPB& out_;

  DISALLOW_COPY_AND_ASSIGN(IntentInserter);
};

class DocKeyAccessor {
 public:
  explicit DocKeyAccessor(std::reference_wrapper<const Schema> schema)
      : schema_(schema.get()), source_(std::nullopt), result_holder_(std::nullopt) {}

  Result<Slice> GetEncoded(std::reference_wrapper<const PgsqlExpressionMsg> ybctid) {
    RETURN_NOT_OK(Apply(ybctid.get()));
    return Encoded();
  }

  Result<Slice> GetEncoded(std::reference_wrapper<const PgsqlReadRequestMsg> req) {
    RETURN_NOT_OK(Apply(req.get()));
    return Encoded();
  }

  Result<DocKey&> GetDecoded(std::reference_wrapper<const PgsqlWriteRequestMsg> req) {
    RETURN_NOT_OK(Apply(req.get()));
    return Decoded();
  }

  // Should be invoked only after GetEncoded or GetDecoded
  bool pk_is_known() {
    return pk_is_known_;
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

    auto hashed_components = VERIFY_RESULT(qlexpr::InitKeyColumnValues(
        req.partition_column_values(), schema_, 0 /* start_idx */));
    auto range_components = VERIFY_RESULT(qlexpr::InitKeyColumnValues(
        req.range_column_values(), schema_, schema_.num_hash_key_columns()));
    // A SELECT on the unique index column currently takes a strong lock on the top-level key at the
    // serializable isolation level. TODO: Optimize with weak lock.
    pk_is_known_ = schema_.num_hash_key_columns() == hashed_components.size() &&
                   schema_.num_range_key_columns() == range_components.size();
    if (hashed_components.empty()) {
      source_.emplace<DocKey>(schema_, std::move(range_components));
    } else {
      source_.emplace<DocKey>(
          schema_, req.hash_code(), std::move(hashed_components), std::move(range_components));
    }
    return Status::OK();
  }

  Status Apply(const PgsqlExpressionMsg& ybctid) {
    const auto& value = ybctid.value().binary_value();
    SCHECK(!value.empty(), InternalError, "empty ybctid");
    source_.emplace<Slice>(value);
    pk_is_known_ = true;
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

  // Whether the assoicated key includes a PK. It is valid only after GetEncoded or GetDecoded.
  bool pk_is_known_ = false;
};

Result<YQLRowwiseIteratorIf::UniPtr> CreateIterator(
    const PgsqlReadOperationData& data,
    const PgsqlReadRequestMsg& request,
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context) {
  VLOG_IF(2, request.is_for_backfill()) << "Creating iterator for " << AsString(request);

  YQLRowwiseIteratorIf::UniPtr result;
  // TODO(neil) Remove the following IF block when it is completely obsolete.
  // The following IF block gets used in the CREATE INDEX codepath.
  if (request.has_ybctid_column_value()) {
    Slice value = request.ybctid_column_value().value().binary_value();
    result = VERIFY_RESULT(data.ql_storage.GetIteratorForYbctid(
        request.stmt_id(), projection, doc_read_context, data.txn_op_context,
        data.read_operation_data, {value, value}, data.pending_op));
  } else {
    SubDocKey start_sub_doc_key;
    auto actual_read_time = data.read_operation_data.read_time;
    // Decode the start SubDocKey from the paging state and set scan start key.
    if (request.has_paging_state() &&
        request.paging_state().has_next_row_key() &&
        !request.paging_state().next_row_key().empty()) {
      dockv::KeyBytes start_key_bytes(request.paging_state().next_row_key());
      RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
      // TODO(dmitry) Remove backward compatibility block when obsolete.
      if (!data.is_explicit_request_read_time) {
        if (request.paging_state().has_read_time()) {
          actual_read_time = ReadHybridTime::FromPB(request.paging_state().read_time());
        } else {
          actual_read_time.read = start_sub_doc_key.hybrid_time();
        }
      }
    } else if (request.is_for_backfill()) {
      RSTATUS_DCHECK(data.is_explicit_request_read_time, InvalidArgument,
                     "Backfill request should already be using explicit read times.");
      PgsqlBackfillSpecPB spec;
      spec.ParseFromString(a2b_hex(request.backfill_spec()));
      if (!spec.next_row_key().empty()) {
        dockv::KeyBytes start_key_bytes(spec.next_row_key());
        RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
      }
    }
    RETURN_NOT_OK(data.ql_storage.GetIterator(
        request, projection, doc_read_context, data.txn_op_context, data.read_operation_data,
        start_sub_doc_key.doc_key(), data.pending_op, &result));
  }

  return std::move(result);
}

template <class ColumnIds>
Status VerifyNoColsMarkedForDeletion(TableIdView table_id,
                                     const Schema& schema,
                                     const ColumnIds& column_refs) {
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

Status VerifyNoColsMarkedForDeletion(const Schema& schema, const PgsqlWriteRequestMsg& request) {
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
  if (schema.has_vectors()) {
    projection->Init(schema, request.col_refs(), schema.vector_column_ids());
  } else if (!request.col_refs().empty()) {
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

Result<std::unique_ptr<YQLRowwiseIteratorIf>> CreateYbctidIterator(
    const PgsqlReadOperationData& data, const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> read_context, const YbctidBounds& bounds,
      SkipSeek skip_seek) {
  return data.ql_storage.GetIteratorForYbctid(
      data.request.stmt_id(), projection, read_context, data.txn_op_context,
      data.read_operation_data, bounds, data.pending_op, skip_seek);
}

class FilteringIterator {
 public:
  explicit FilteringIterator(std::unique_ptr<YQLRowwiseIteratorIf>* iterator_holder)
      : iterator_holder_(*iterator_holder) {}

  Status Init(
      const PgsqlReadOperationData& data,
      const PgsqlReadRequestMsg& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> read_context) {
    RETURN_NOT_OK(InitCommon(request, read_context.get().schema(), projection));
    iterator_holder_ = VERIFY_RESULT(CreateIterator(data, request, projection, read_context));
    return Status::OK();
  }

  Status InitForYbctid(
      const PgsqlReadOperationData& data,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> read_context,
      const YbctidBounds& bounds) {
    RETURN_NOT_OK(InitCommon(data.request, read_context.get().schema(), projection));
    if (!iterator_holder_) {
      iterator_holder_ = VERIFY_RESULT(CreateYbctidIterator(
          data, projection, read_context, bounds, SkipSeek::kTrue));
    } else {
      Refresh();
    }
    return Status::OK();
  }

  void Refresh() {
    down_cast<DocRowwiseIterator&>(*iterator_holder_).Refresh(SeekFilter::kAll);
  }

  Result<FetchResult> FetchNext(dockv::PgTableRow* table_row) {
    if (!VERIFY_RESULT(iterator_holder_->PgFetchNext(table_row))) {
      return FetchResult::NotFound;
    }
    return CheckFilter(*table_row);
  }

  Result<bool> FetchNextMatch(dockv::PgTableRow* table_row) {
    for (;;) {
      if (!VERIFY_RESULT(iterator_holder_->PgFetchNext(table_row))) {
        return false;
      }
      if (VERIFY_RESULT(MatchFilter(*table_row))) {
        return true;
      }
    }
  }

  Result<FetchResult> FetchTuple(Slice tuple_id, dockv::PgTableRow* row) {
    iterator_holder_->SeekTuple(tuple_id);
    if (!VERIFY_RESULT(iterator_holder_->PgFetchNext(row)) ||
        iterator_holder_->GetTupleId() != tuple_id) {
      return FetchResult::NotFound;
    }
    return CheckFilter(*row);
  }

  YQLRowwiseIteratorIf& impl() const {
    return *iterator_holder_;
  }

  bool has_filter() const {
    DCHECK(iterator_holder_ != nullptr);
    return filter_.has_value();
  }

  static bool NeedFilter(const PgsqlReadRequestMsg& request) {
    return !request.where_clauses().empty();
  }

 private:
  Status InitCommon(
      const PgsqlReadRequestMsg& request, const Schema& schema,
      const dockv::ReaderProjection& projection) {
    if (!NeedFilter(request)) {
      return Status::OK();
    }

    std::optional<int> version = request.has_expression_serialization_version()
      ? std::optional<int>(request.expression_serialization_version())
      : std::nullopt;

    DocPgExprExecutorBuilder builder(schema, projection);
    for (const auto& exp : request.where_clauses()) {
      RETURN_NOT_OK(builder.AddWhere(exp, version));
    }
    filter_.emplace(VERIFY_RESULT(builder.Build(request.col_refs())));
    return Status::OK();
  }

  Result<bool> MatchFilter(const dockv::PgTableRow& row) {
    return !filter_ || VERIFY_RESULT(filter_->Exec(row));
  }

  Result<FetchResult> CheckFilter(const dockv::PgTableRow& row) {
    return VERIFY_RESULT(MatchFilter(row)) ? FetchResult::Found : FetchResult::FilteredOut;
  }

  std::unique_ptr<YQLRowwiseIteratorIf>& iterator_holder_;
  std::optional<DocPgExprExecutor> filter_;
};

template <typename T>
concept Prefetcher = requires(
    T& key_provider, FilteringIterator& iterator, const dockv::ReaderProjection& projection) {
    { key_provider.Prefetch(iterator, projection) } -> std::same_as<Status>;
};

template <typename T>
concept BoundsProvider = requires(T& key_provider) {
    { key_provider.Bounds() } -> std::same_as<YbctidBounds>;
};

template <typename T>
YbctidBounds Bounds(const T& key_provider) {
  if constexpr (BoundsProvider<T>) {
    return key_provider.Bounds();
  }
  return {};
}

struct IndexState {
  IndexState(
      const Schema& schema, const PgsqlReadRequestMsg& request,
      std::unique_ptr<YQLRowwiseIteratorIf>* iterator_holder, ColumnId ybbasectid_id)
      : projection(CreateProjection(schema, request)), row(projection), iter(iterator_holder),
        ybbasectid_idx(projection.ColumnIdxById(ybbasectid_id)), scanned_rows(0) {
  }

  dockv::ReaderProjection projection;
  dockv::PgTableRow row;
  FilteringIterator iter;
  const size_t ybbasectid_idx;
  uint64_t scanned_rows;
  Status delayed_failure;
};

Result<FetchResult> FetchTableRow(
    TableIdView table_id, FilteringIterator* table_iter,
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
      if (index && index->delayed_failure.ok()) {
        const auto* fmt = FLAGS_TEST_hide_details_for_pg_regress
            ? "ybctid not found in indexed table"
            : "$0 not found in indexed table. Index table id is $1, row $2";
        index->delayed_failure = STATUS_FORMAT(
            Corruption, fmt,
            DocKey::DebugSliceToString(tuple_id), table_id,
            DocKey::DebugSliceToString(index->iter.impl().GetTupleId()));
      }
      break;
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

  static Result<RowPackerData> Create(
      const PgsqlWriteRequestMsg& request, const DocReadContext& read_context) {
    auto schema_version = request.schema_version();
    return RowPackerData {
      .schema_version = schema_version,
      .packing = VERIFY_RESULT(read_context.schema_packing_storage.GetPacking(schema_version)),
    };
  }

  dockv::RowPackerVariant MakePacker(bool is_update = false) const {
    if (FLAGS_ysql_use_packed_row_v2) {
      return MakePackerHelper<dockv::RowPackerV2>(is_update);
    }
    return MakePackerHelper<dockv::RowPackerV1>(is_update);
  }

 private:
  template <class T>
  dockv::RowPackerVariant MakePackerHelper(bool is_update) const {
    return dockv::RowPackerVariant(
        std::in_place_type_t<T>(), schema_version, packing, FLAGS_ysql_packed_row_size_limit,
        Slice(), is_update);
  }
};

bool IsExpression(const PgsqlColumnValueMsg& column_value) {
  return qlexpr::GetTSWriteInstruction(column_value.expr()) == bfpg::TSOpcode::kPgEvalExprCall;
}

class ExpressionHelper {
 public:
  Status Init(
      const Schema& schema, const dockv::ReaderProjection& projection,
      const PgsqlWriteRequestMsg& request, const dockv::PgTableRow& table_row) {
    DocPgExprExecutorBuilder builder(schema, projection);
    for (const auto& column_value : request.column_new_values()) {
      if (IsExpression(column_value)) {
        RETURN_NOT_OK(builder.AddTarget(column_value.expr(), std::nullopt /* version */));
      }
    }
    auto executor = VERIFY_RESULT(builder.Build(request.col_refs()));
    return ResultToStatus(executor.Exec(table_row, &results_));
  }

  QLExprResult* NextResult(const PgsqlColumnValueMsg& column_value) {
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

[[nodiscard]] inline bool NewValuesHaveExpression(const PgsqlWriteRequestMsg& request) {
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
[[nodiscard]] bool IsOnlyKeyColumnsRequested(
    const Schema& schema, const PgsqlReadRequestMsg& req) {
  if (!req.col_refs().empty()) {
    return !Find(
        req.col_refs(),
        [&schema](const PgsqlColRefMsg& col) { return IsNonKeyColumn(schema, col.column_id()); });
  }

  return !Find(
      req.column_refs().ids(),
      [&schema](int32_t col_id) { return IsNonKeyColumn(schema, col_id); });
}

std::string DocDbStatsToString(const DocDBStatistics* doc_db_stats) {
  if (!doc_db_stats) {
    return "<nullptr>";
  }
  std::stringstream ss;
  doc_db_stats->Dump(&ss);
  return ss.str();
}

class VectorIndexKeyProvider {
 public:
  VectorIndexKeyProvider(
      DocVectorIndexSearchResult& search_result, PgsqlResponseMsg& response,
      DocVectorIndex& vector_index, Slice vector_slice, size_t num_top_vectors_to_remove,
      size_t max_results, WriteBuffer& result_buffer)
      : could_have_more_data_(search_result.could_have_more_data),
        result_entries_(search_result.entries), response_(response), vector_index_(vector_index),
        vector_slice_(vector_slice), num_top_vectors_to_remove_(num_top_vectors_to_remove),
        max_results_(max_results), result_buffer_(result_buffer) {}

  Slice FetchKey() {
    if (index_ >= result_entries_.size()) {
      VLOG_WITH_FUNC(4) << vector_index_.ToString()
                        << ", returned: " << response_.vector_index_distances().size()
                        << " out of " << result_entries_.size();
      return Slice();
    }
    // TODO(vector_index) When row came from intents, we already have all necessary data,
    // so could avoid refetching it.
    return result_entries_[index_++].key.AsSlice();
  }

  void AddedKeyToResultSet() {
    response_.add_vector_index_distances(result_entries_[index_ - 1].encoded_distance);
    response_.add_vector_index_ends(result_buffer_.size());
  }

  Status Prefetch(FilteringIterator& iterator, const dockv::ReaderProjection& projection) {
    auto& iterator_impl = down_cast<DocRowwiseIterator&>(iterator.impl());
    iterator_impl.Refresh(SeekFilter::kIntentsOnly);
    iterator_impl.Seek(Slice());

    auto indexed_column_index = projection.ColumnIdxById(vector_index_.column_id());
    RSTATUS_DCHECK(indexed_column_index != dockv::ReaderProjection::kNotFoundIndex, Corruption,
                   "Indexed column ($0) not found in projection: $1",
                   vector_index_.column_id(), projection);
    // TODO(vector_index) Use limit during prefetch.
    dockv::PgTableRow table_row(projection);
    size_t old_size = result_entries_.size();
    while (VERIFY_RESULT(iterator.FetchNextMatch(&table_row))) {
      auto vector_value = table_row.GetValueByIndex(indexed_column_index);
      RSTATUS_DCHECK(vector_value, Corruption, "Vector column ($0) missing in row: $1",
                     vector_index_.column_id(), table_row.ToString());
      auto encoded_value = dockv::EncodedDocVectorValue::FromSlice(vector_value->binary_value());
      result_entries_.push_back(DocVectorIndexSearchResultEntry {
        // TODO(vector_index) Avoid decoding vector_slice for each vector
        .encoded_distance = VERIFY_RESULT(vector_index_.Distance(
            vector_slice_, encoded_value.data)),
        .key = KeyBuffer(iterator.impl().GetTupleId()),
      });
    }
    found_intents_ = result_entries_.size() - old_size;
    prefetch_done_time_ = MonoTime::NowIf(FLAGS_vector_index_dump_stats);

    auto cmp_keys = [](const auto& lhs, const auto& rhs) {
      return lhs.key < rhs.key;
    };
    std::ranges::sort(result_entries_, cmp_keys);

    if (found_intents_) {
      auto range = std::ranges::unique(result_entries_, [](const auto& lhs, const auto& rhs) {
        return lhs.key == rhs.key;
      });
      result_entries_.erase(range.begin(), range.end());
    }

    VLOG_WITH_FUNC(4) << vector_index_.ToString()
                      << ", could_have_more_data_: " << could_have_more_data_
                      << ", result_entries_.size(): " << result_entries_.size()
                      << ", max_results_: " << max_results_
                      << ", num_top_vectors_to_remove_: " << num_top_vectors_to_remove_;
    could_have_more_data_ = could_have_more_data_ || result_entries_.size() >= max_results_;
    if (result_entries_.size() > max_results_ || num_top_vectors_to_remove_ != 0) {
      std::ranges::sort(result_entries_, [](const auto& lhs, const auto& rhs) {
        return lhs.encoded_distance < rhs.encoded_distance;
      });
      result_entries_.erase(
          result_entries_.begin(), result_entries_.begin() + num_top_vectors_to_remove_);
      std::ranges::sort(result_entries_, cmp_keys);
    }

    merge_done_time_ = MonoTime::NowIf(FLAGS_vector_index_dump_stats);

    response_.set_vector_index_could_have_more_data(could_have_more_data_);
    return Status::OK();
  }

  size_t found_intents() const {
    return found_intents_;
  }

  MonoTime prefetch_done_time() const {
    return prefetch_done_time_;
  }

  MonoTime merge_done_time() const {
    return merge_done_time_;
  }

 private:
  bool could_have_more_data_;
  DocVectorIndexSearchResultEntries& result_entries_;
  PgsqlResponseMsg& response_;
  DocVectorIndex& vector_index_;
  Slice vector_slice_;
  const size_t num_top_vectors_to_remove_;
  // Please note that max_results_ includes vectors that should be removed.
  const size_t max_results_;
  WriteBuffer& result_buffer_;
  size_t index_ = 0;
  size_t found_intents_ = 0;
  MonoTime prefetch_done_time_;
  MonoTime merge_done_time_;
};

// NotReached - iteration finished at the upper bound or the end of the tablet
// Reached - a limit was reached and current record made it into result
// Exceeded - a limit was reached and current record did not make it into result (no room)
YB_DEFINE_ENUM(FetchLimit, (kNotReached)(kReached)(kExceeded));

class PgsqlVectorFilter {
 public:
  explicit PgsqlVectorFilter(YQLRowwiseIteratorIf::UniPtr* table_iter)
      : iter_(table_iter) {
  }

  ~PgsqlVectorFilter() {
    LOG_IF(INFO, FLAGS_vector_index_dump_stats && row_)
        << "VI_STATS: PgsqlVectorFilter, checked: " << num_checked_entries_ << ", accepted: "
        << num_accepted_entries_ << ", num removed: " << num_removed_
        << (iter_.has_filter() ? Format(", found: $0", num_found_entries_) : "");
    DCHECK_LE(num_checked_entries_, TEST_vector_index_max_checked_entries);
  }

  Result<bool> Init(const PgsqlReadOperationData& data) {
    if (FLAGS_vector_index_skip_filter_check) {
      return false;
    }
    if (!data.table_has_vector_deletion && !FilteringIterator::NeedFilter(data.request) &&
        FLAGS_vector_index_no_deletions_skip_filter_check) {
      LOG_IF(INFO, FLAGS_vector_index_dump_stats)
          << "VI_STATS: PgsqlVectorFilter, "
             "skip because filter not specified and table does not have deletions";
      return false;
    }
    CHECK(TEST_vector_index_filter_allowed);
    std::vector<ColumnId> columns;
    ColumnId index_column = data.vector_index->column_id();
    for (const auto& col_ref : data.request.col_refs()) {
      if (col_ref.column_id() == index_column) {
        index_column_index_ = columns.size();
      }
      columns.push_back(ColumnId(col_ref.column_id()));
    }
    if (index_column_index_ == std::numeric_limits<size_t>::max()) {
      index_column_index_ = columns.size();
      columns.push_back(index_column);
    }
    dockv::ReaderProjection projection;
    projection_.Init(data.doc_read_context.schema(), columns);
    row_.emplace(projection_);
    RETURN_NOT_OK(iter_.InitForYbctid(data, projection_, data.doc_read_context, {}));

    reverse_mapping_reader_ = VERIFY_RESULT(
        data.vector_index->context().CreateReverseMappingReader(
            data.read_operation_data.read_time));

    return true;
  }

  bool operator()(const vector_index::VectorId& vector_id) {
    if (!row_) {
      return true;
    }
    ++num_checked_entries_;

    // TODO(vector_index) handle failure
    auto ybctid = CHECK_RESULT(reverse_mapping_reader_->FetchYbctid(vector_id));
    if (ybctid.empty()) {
      ++num_removed_;
      return false;
    }
    if (!iter_.has_filter()) {
      ++num_accepted_entries_;
      return true;
    }
    if (need_refresh_) {
      iter_.Refresh();
    }
    auto fetch_result = CHECK_RESULT(iter_.FetchTuple(ybctid, &*row_));
    // TODO(vector_index) Actually we already have all necessary info to generate response,
    // but we don't know whether this row will be in top or not.
    // So need to extend usearch interface to also provide us ability to store fetched row.

    need_refresh_ = fetch_result == FetchResult::NotFound;
    if (fetch_result != FetchResult::Found) {
      return false;
    }
    ++num_found_entries_;
    auto vector_value = row_->GetValueByIndex(index_column_index_);
    if (!vector_value) {
      return false;
    }
    auto encoded_value = dockv::EncodedDocVectorValue::FromSlice(vector_value->binary_value());
    if (vector_id.AsSlice() != encoded_value.id) {
      LOG(DFATAL)
          << "Referenced row with wrong vector id: " << encoded_value.DecodeId()
          << ", expected: " << vector_id;
      return false;
    }
    ++num_accepted_entries_;
    return true;
  }
 private:
  FilteringIterator iter_;
  dockv::ReaderProjection projection_;
  docdb::DocVectorIndexReverseMappingReaderPtr reverse_mapping_reader_;
  size_t index_column_index_ = std::numeric_limits<size_t>::max();
  std::optional<dockv::PgTableRow> row_;
  bool need_refresh_ = false;
  size_t num_checked_entries_ = 0;
  size_t num_found_entries_ = 0;
  size_t num_accepted_entries_ = 0;
  size_t num_removed_ = 0;
};

std::string DebugKeySliceToString(Slice key) {
  return Format("$0 ($1)", key.ToDebugHexString(), DocKey::DebugSliceToString(key));
}

template <typename T>
Result<size_t> WriteRowsReservoir(const T& reservoir, int num_rows, WriteBuffer* buffer) {
  // Return collected tuples from the reservoir.
  // Tuples are returned as (index, ybctid) pairs, where index is in [0..targrows-1] range.
  // As mentioned above, for large tables reservoirs become increasingly sparse from page to page.
  // So we hope to save by sending variable number of index/ybctid pairs vs exactly targrows of
  // nullable ybctids. It also helps in case of extremely small table or partition.
  size_t fetched_rows = 0;
  QLValuePB index;
  QLValuePB row;
  for (auto i = 0; i < num_rows; i++) {
    if (reservoir[i].empty()) {
      VLOG_WITH_FUNC(4) << "skipped empty ybctid at index: " << i;
      continue;
    }
    index.set_int32_value(i);
    row.set_binary_value(reservoir[i]);
    RETURN_NOT_OK(pggate::WriteColumn(index, buffer));
    RETURN_NOT_OK(pggate::WriteColumn(row, buffer));
    VLOG_WITH_FUNC(4) << "picked ybctid: " << DebugKeySliceToString(row.binary_value())
                      << " index: " << i;
    fetched_rows++;
  }
  return fetched_rows;
}

Result<size_t> WriteBlocksReservoir(
    const YQLStorageIf::SampleBlocksReservoir& sample_blocks, WriteBuffer* buffer) {
  size_t fetched_blocks = 0;
  for (size_t i = 0; i < sample_blocks.size(); ++i) {
    QLValuePB index;
    const auto& sample_block = sample_blocks[i];
    if (sample_block.first.empty() && sample_block.second.empty()) {
      continue;
    }

    VLOG_WITH_FUNC(3) << "Sample block #" << i << ": "
                      << DebugKeySliceToString(sample_block.first.AsSlice()) << " - "
                      << DebugKeySliceToString(sample_block.second.AsSlice());

    index.set_int32_value(static_cast<int32_t>(i));
    RETURN_NOT_OK(pggate::WriteColumn(index, buffer));
    pggate::WriteBinaryColumn(sample_block.first.AsSlice(), buffer);
    pggate::WriteBinaryColumn(sample_block.second.AsSlice(), buffer);
    ++fetched_blocks;
  }
  return fetched_blocks;
}

} // namespace

class PgsqlWriteOperation::RowPackContext {
 public:
  RowPackContext(const PgsqlWriteRequestMsg& request,
                 const DocOperationApplyData& data,
                 const RowPackerData& packer_data,
                 bool is_update = false)
      : query_id_(request.stmt_id()),
        data_(data),
        write_id_(data.doc_write_batch->ReserveWriteId()),
        packer_(packer_data.MakePacker(is_update)) {
  }

  template <class Value>
  Result<bool> Add(ColumnId column_id, const Value& value) {
    return std::visit([column_id, &value](auto& packer) {
      return packer.AddValue(column_id, value);
    }, packer_);
  }

  Status Complete(const RefCntPrefix& encoded_doc_key) {
    return Complete(encoded_doc_key.as_slice());
  }

  Status Complete(Slice encoded_doc_key) {
    auto encoded_value = VERIFY_RESULT(std::visit([](auto& packer) {
      return packer.Complete();
    }, packer_));
    return data_.doc_write_batch->SetPrimitive(
        DocPath(encoded_doc_key), dockv::ValueControlFields(),
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
    std::reference_wrapper<const PgsqlWriteRequestMsg> request, DocReadContextPtr doc_read_context,
    const TransactionOperationContext& txn_op_context, rpc::Sidecars* sidecars)
    : DocOperationBase(request),
      doc_read_context_(std::move(doc_read_context)),
      txn_op_context_(txn_op_context),
      sidecars_(sidecars),
      ysql_skip_row_lock_for_update_(FLAGS_ysql_skip_row_lock_for_update) {}

Status PgsqlWriteOperation::Init(PgsqlResponseMsg* response) {
  // Initialize operation inputs.
  response_ = response;

  if (!request_.packed_rows().empty()) {
    // When packed_rows are specified, operation could contain multiple rows.
    // So we use table root key for doc_key, and have to use special handling for packed_rows in
    // other places.
    doc_key_ = dockv::DocKey(doc_read_context_->schema());
    encoded_doc_key_ = doc_key_.EncodeAsRefCntPrefix();
    pk_is_known_ = true;
  } else {
    DocKeyAccessor accessor(doc_read_context_->schema());
    doc_key_ = std::move(VERIFY_RESULT_REF(accessor.GetDecoded(request_)));
    encoded_doc_key_ = doc_key_.EncodeAsRefCntPrefix(
        Slice(&dockv::KeyEntryTypeAsChar::kHighest, 1));
    pk_is_known_ = accessor.pk_is_known();
  }

  encoded_doc_key_.Resize(encoded_doc_key_.size() - 1);

  return Status::OK();
}

bool PgsqlWriteOperation::RequireReadSnapshot() const {
  // For YSQL the the standard operations (INSERT/UPDATE/DELETE) will read/check the primary key.
  // We use UPSERT stmt type for specific requests when we can guarantee we can skip the read.
  return request_.stmt_type() != PgsqlWriteRequestPB::PGSQL_UPSERT;
}

void PgsqlWriteOperation::ClearResponse() {
  if (response_) {
    response_->Clear();
  }
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
  VLOG_WITH_FUNC(2) << "doc key: " << doc_key_;

  auto iter = CreateIntentAwareIterator(
      data.doc_write_batch->doc_db(),
      BloomFilterOptions::Fixed(encoded_doc_key_.as_slice()),
      rocksdb::kDefaultQueryId,
      txn_op_context_,
      data.read_operation_data.WithAlteredReadTime(ReadHybridTime::Max()));

  VLOG_WITH_FUNC(4) << "whole row: " << doc_key_;
  HybridTime oldest_past_min_ht = VERIFY_RESULT(FindOldestOverwrittenTimestamp(
      iter.get(), SubDocKey(doc_key_), data.read_time().read));
  VLOG_WITH_FUNC(4) << "liveness column: " << SubDocKey(doc_key_, KeyEntryValue::kLivenessColumn);
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
      return STATUS_FORMAT(InternalError, "column id missing: $0", column_value);
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
  VLOG_WITH_FUNC(3) << doc_key_;
  iter->Seek(doc_key_);
  if (VERIFY_RESULT_REF(iter->Fetch())) {
    const auto bytes = sub_doc_key.EncodeWithoutHt();
    const Slice& sub_key_slice = bytes.AsSlice();
    result = VERIFY_RESULT(iter->FindOldestRecord(sub_key_slice, min_read_time));
    VLOG_WITH_FUNC(2) << "iter->FindOldestRecord returned " << result << " for "
                      << SubDocKey::DebugSliceToString(sub_key_slice);
  } else {
    VLOG_WITH_FUNC(3) << "iter->Seek " << doc_key_ << " turned out to be out of records";
  }
  return result;
}

Status PgsqlWriteOperation::Apply(const DocOperationApplyData& data) {
  VLOG(4) << "Write, read time: " << data.read_time() << ", txn: " << txn_op_context_;

  data.doc_write_batch->SetIncludesPk(pk_is_known_);
  RETURN_NOT_OK(VerifyNoColsMarkedForDeletion(doc_read_context_->schema(), request_));

  auto scope_exit = ScopeExit([this] {
    if (write_buffer_) {
      CHECK_OK(pggate::PgWire::WriteInt64(result_rows_, write_buffer_, row_num_pos_));
      response_->set_rows_data_sidecar(narrow_cast<int32_t>(sidecars_->Complete()));
    }
  });

  // TODO(vector_index) Don't send write to the vector index itself. Just write to the main table.
  if (doc_read_context_->vector_idx_options &&
      doc_read_context_->vector_idx_options->idx_type() == PgVectorIndexType::HNSW) {
    return Status::OK();
  }

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
    const DocOperationApplyData& data, const PgsqlColumnValueMsg& column_value,
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

  if (!column.is_vector()) {
    return DoInsertColumn(data, column_id, column, value, pack_context);
  }

  dockv::DocVectorValue vector_value(value, vector_index::VectorId::GenerateRandom());
  return DoInsertColumn(data, column_id, column, vector_value, pack_context);
}

template <class Value>
Status PgsqlWriteOperation::DoInsertColumn(
    const DocOperationApplyData& data, ColumnId column_id, const ColumnSchema& column,
    Value&& value, RowPackContext* pack_context) {
  if (pack_context && VERIFY_RESULT(pack_context->Add(column_id, value))) {
    return Status::OK();
  }

  // Inserting into specified column.
  DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));

  // If the column has a missing value, we don't want any null values that are inserted to be
  // compacted away. So we store kNullLow instead of kTombstone.
  return data.doc_write_batch->InsertSubDocument(
      sub_path,
      IsNull(value) && !IsNull(column.missing_value()) ?
          ValueRef(dockv::ValueEntryType::kNullLow) : ValueRef(value, column.sorting_type()),
      data.read_operation_data, request_.stmt_id());
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
      if (VERIFY_RESULT(ReadRow(data, &table_row))) {
        VLOG(4) << "Duplicate row: " << table_row.ToString();
        // Primary key or unique index value found.
        response_->set_status(PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR);
        response_->set_error_message("Duplicate key found in primary key or unique index");
        return Status::OK();
      }
    }
  }

  const auto& schema = doc_read_context_->schema();
  auto pack_row = ShouldYsqlPackRow(schema.is_colocated());
  if (!request_.packed_rows().empty()) {
    RETURN_NOT_OK(VerifyNoColsMarkedForDeletion(
        request_.table_id(), schema, schema.value_column_ids()));
    dockv::KeyBytes key_bytes(encoded_doc_key_.as_slice());
    for (auto it = request_.packed_rows().begin(); it != request_.packed_rows().end();) {
      key_bytes.Truncate(encoded_doc_key_.size());
      key_bytes.AppendRawBytes(*it++);
      auto key = key_bytes.AsSlice();
      Slice packed_value(*it++);
      if (pack_row &&
          packed_value.size() < dockv::PackedSizeLimit(FLAGS_ysql_packed_row_size_limit)) {
        RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
            DocPath(key), dockv::ValueControlFields(),
            ValueRef(packed_value), data.read_operation_data, request_.stmt_id()));
      } else {
        SCHECK(packed_value.TryConsumeByte(dockv::ValueEntryTypeAsChar::kPackedRowV1),
               InvalidArgument,
               "Packed value in a wrong format: $0", packed_value.ToDebugHexString());
        const auto& packing = VERIFY_RESULT_REF(
            doc_read_context_->schema_packing_storage.GetPacking(&packed_value));

        std::optional<RowPackContext> pack_context;
        if (pack_row) {
          pack_context.emplace(
              request_, data, VERIFY_RESULT(RowPackerData::Create(request_, *doc_read_context_)));
        } else {
          RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
              DocPath(key, KeyEntryValue::kLivenessColumn),
              dockv::ValueControlFields(), ValueRef(dockv::ValueEntryType::kNullLow),
              data.read_operation_data, request_.stmt_id()));
        }
        for (size_t idx = 0; idx != packing.columns(); ++idx) {
          auto value = dockv::PackedValueV1(packing.GetValue(idx, packed_value));
          auto column_id = packing.column_packing_data(idx).id;
          if (!pack_context || !VERIFY_RESULT(pack_context->Add(column_id, value))) {
            if (value->empty()) {
              static char null_column_type = dockv::ValueEntryTypeAsChar::kNullLow;
              value = dockv::PackedValueV1(Slice(&null_column_type, sizeof(null_column_type)));
            }
            DocPath sub_path(key, KeyEntryValue::MakeColumnId(column_id));
            RETURN_NOT_OK(data.doc_write_batch->SetPrimitive(
                sub_path, ValueRef(*value), data.read_operation_data, request_.stmt_id()));
          }
        }
        if (pack_context) {
          RETURN_NOT_OK(pack_context->Complete(key));
        }
      }
    }
  } else if (pack_row) {
    RowPackContext pack_context(
        request_, data, VERIFY_RESULT(RowPackerData::Create(request_, *doc_read_context_)));

    auto column_id_extractor = [](const PgsqlColumnValueMsg& column_value) {
      return column_value.column_id();
    };

    if (IsMonotonic(request_.column_values(), column_id_extractor)) {
      for (const auto& column_value : request_.column_values()) {
        RETURN_NOT_OK(InsertColumn(data, column_value, &pack_context));
      }
    } else {
      auto column_order = StableSorted(request_.column_values(), column_id_extractor);

      for (const auto& key_and_index : column_order) {
        RETURN_NOT_OK(InsertColumn(data, *key_and_index.pointer, &pack_context));
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
    const PgsqlColumnValueMsg& column_value, dockv::PgTableRow* returning_table_row,
    QLExprResult* result, RowPackContext* pack_context) {
  // Get the column.
  if (!column_value.has_column_id()) {
    return STATUS_FORMAT(InternalError, "column id missing: $0", column_value);
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

  if (!column.is_vector() || IsNull(result->Value())) {
    return DoUpdateColumn(data, column_id, column, result->Value(), pack_context);
  }

  dockv::DocVectorValue vector_value(result->Value(), vector_index::VectorId::GenerateRandom());
  return DoUpdateColumn(data, column_id, column, vector_value, pack_context);
}

template <class Value>
Status PgsqlWriteOperation::DoUpdateColumn(
    const DocOperationApplyData& data, ColumnId column_id, const ColumnSchema& column,
    Value&& value, RowPackContext* pack_context) {
  if (pack_context && VERIFY_RESULT(pack_context->Add(column_id, value))) {
    return Status::OK();
  }
  // Inserting into specified column.
  DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));
  // If the column has a missing value, we don't want any null values that are inserted to be
  // compacted away. So we store kNullLow instead of kTombstone.
  return data.doc_write_batch->InsertSubDocument(
      sub_path,
      IsNull(value) && !IsNull(column.missing_value()) ?
          ValueRef(dockv::ValueEntryType::kNullLow) :
          ValueRef(value, column.sorting_type()),
      data.read_operation_data, request_.stmt_id());
}

Status PgsqlWriteOperation::ApplyUpdate(const DocOperationApplyData& data) {
  dockv::PgTableRow table_row(projection());

  if (!VERIFY_RESULT(ReadRow(data, &table_row))) {
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

  // This function is invoked by three different callers:
  // 1. Main table updates: requests have the YBCTID column field populated.
  // 2. Secondary index updates: requests do not have the YBCTID column field populated.
  //      The tuple identifier is constructed from partition and range key columns.
  // 3. Sequence updates: requests are similar to secondary index updates. These updates are sent
  //      by calling pggate directly, without going through the PostgreSQL layer.
  // Since we cannot distinguish between (2) and (3), we make use of the table ID to determine the
  // type of UPDATE to be performed. Sequence updates are always on the PG sequences data table.
  bool is_sequence_update =
      GetPgsqlTableId(kPgSequencesDataDatabaseOid, kPgSequencesDataTableOid) == request_.table_id();

  if (!is_sequence_update) {
    const auto& schema = doc_read_context_->schema();
    ExpressionHelper expression_helper;
    RETURN_NOT_OK(expression_helper.Init(schema, projection(), request_, table_row));

    if (schema.has_vectors()) {
      RETURN_NOT_OK(HandleUpdatedVectorIds(data, table_row));
    }

    skipped = request_.column_new_values().empty();
    const size_t num_non_key_columns = schema.num_columns() - schema.num_key_columns();
    if (FLAGS_ysql_enable_pack_full_row_update &&
        ShouldYsqlPackRow(schema.is_colocated()) &&
        make_unsigned(request_.column_new_values().size()) == num_non_key_columns) {
      RowPackContext pack_context(
          request_, data, VERIFY_RESULT(RowPackerData::Create(request_, *doc_read_context_)),
          FLAGS_ysql_mark_update_packed_row /* is_update */);

      auto column_id_extractor = [](const PgsqlColumnValueMsg& column_value) {
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
            [&expression_helper](const PgsqlColumnValueMsg& column_value) {
          return std::pair(column_value.column_id(), expression_helper.NextResult(column_value));
        };
        auto column_order = StableSorted(
            request_.column_new_values(), column_id_and_result_extractor);

        for (const auto& entry : column_order) {
          RETURN_NOT_OK(UpdateColumn(
              data, table_row, *entry.pointer, &returning_table_row,
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
      QLExprResult match(&request_);
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
        QLExprResult expr_result(&request_);
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
  if (!VERIFY_RESULT(ReadRow(data, &table_row))) {
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

  if (doc_read_context_->schema().has_vectors()) {
    RETURN_NOT_OK(HandleDeletedVectorIds(data, table_row));
  }

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
  if (!VERIFY_RESULT(ReadRow(data, &table_row))) {
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
  auto it = request_.col_refs().begin();
  ColumnId last_value_column_id(it->column_id());
  ColumnId is_called_column_id(std::next(it)->column_id());
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

Result<bool> PgsqlWriteOperation::ReadRow(
    const DocOperationApplyData& data, dockv::PgTableRow* table_row) {
  if (request_.packed_rows().empty()) {
    return ReadRow(data, doc_key_, table_row);
  }

  dockv::KeyBytes key_bytes(encoded_doc_key_.as_slice());
  dockv::DocKey doc_key;
  // 2 entries per row, even for key, odd for value.
  for (auto it = request_.packed_rows().begin(); it != request_.packed_rows().end(); it += 2) {
    key_bytes.Truncate(encoded_doc_key_.size());
    key_bytes.AppendRawBytes(*it);
    RETURN_NOT_OK(doc_key.FullyDecodeFrom(key_bytes.AsSlice()));
    if (VERIFY_RESULT(ReadRow(data, doc_key, table_row))) {
      return true;
    }
  }
  return false;
}

Result<bool> PgsqlWriteOperation::ReadRow(
    const DocOperationApplyData& data, const dockv::DocKey& doc_key, dockv::PgTableRow* table_row) {
  // Filter the columns using primary key.
  DocPgsqlScanSpec spec(doc_read_context_->schema(), request_.stmt_id(), doc_key);
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
  data.read_restart_data->MakeAtLeast(VERIFY_RESULT(iterator.GetReadRestartData()));

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
  for (const auto& expr : request_.targets()) {
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
  if (!request_.packed_rows().empty()) {
    for (auto it = request_.packed_rows().begin(); it != request_.packed_rows().end(); it += 2) {
      paths->emplace_back(*it);
    }
    return Status::OK();
  }
  // Add row's doc key. Caller code will create strong intent for the whole row in this case.
  paths->push_back(encoded_doc_key_);
  return Status::OK();
}

Status PgsqlWriteOperation::HandleUpdatedVectorIds(
    const DocOperationApplyData& data, const dockv::PgTableRow& table_row) {
  const auto& schema = doc_read_context_->schema();
  for (const auto& column_value : request_.column_new_values()) {
    ColumnId column_id(column_value.column_id());
    const auto& column = VERIFY_RESULT_REF(schema.column_by_id(column_id));
    if (!column.is_vector()) {
      continue;
    }
    RETURN_NOT_OK(FillRemovedVectorId(data, table_row, column_id));
  }
  return Status::OK();
}

Status PgsqlWriteOperation::HandleDeletedVectorIds(
    const DocOperationApplyData& data, const dockv::PgTableRow& table_row) {
  for (const auto& column_id : doc_read_context_->schema().vector_column_ids()) {
    RETURN_NOT_OK(FillRemovedVectorId(data, table_row, column_id));
  }
  return Status::OK();
}

Status PgsqlWriteOperation::FillRemovedVectorId(
    const DocOperationApplyData& data, const dockv::PgTableRow& table_row, ColumnId column_id) {
  auto old_vector_value = table_row.GetValueByColumnId(column_id);
  if (!old_vector_value) {
    return Status::OK();
  }
  auto vector_value = dockv::EncodedDocVectorValue::FromSlice(old_vector_value->binary_value());
  VLOG_WITH_FUNC(4) << "Old vector id: " << AsString(vector_value.DecodeId());
  data.doc_write_batch->DeleteVectorId(VERIFY_RESULT(vector_value.DecodeId()));
  return Status::OK();
}

class PgsqlReadRequestYbctidProvider {
 public:
  explicit PgsqlReadRequestYbctidProvider(
      const DocReadContext& doc_read_context, const PgsqlReadRequestMsg& request,
      PgsqlResponseMsg& response)
      : request_(request), response_(response) {
    const auto& batch_args = request_.batch_arguments();

    Slice min_arg;
    Slice max_arg;

    batch_arg_it_ = batch_args.begin();

    if (!batch_args.empty()) {
      min_arg = batch_arg_it_->ybctid().value().binary_value();
      max_arg = min_arg;
    }

    for (auto& batch_arg_it : batch_args) {
      const auto& val = batch_arg_it.ybctid().value().binary_value();
      if (min_arg > val) {
        min_arg = val;
      }

      if (max_arg < val) {
        max_arg = val;
      }
    }

    bounds_ = {min_arg, max_arg};
  }

  YbctidBounds Bounds() const { return bounds_; }

  Slice FetchKey() {
    if (!valid_ || batch_arg_it_ == request_.batch_arguments().end()) {
      valid_ = false;
      return Slice();
    }

    Slice ret = batch_arg_it_->ybctid().value().binary_value();
    if (batch_arg_it_->has_order()) {
      current_order_ = batch_arg_it_->order();
    }

    ++batch_arg_it_;
    return ret;
  }

  void AddedKeyToResultSet() {
    if (current_order_.has_value()) {
      response_.add_batch_orders(*current_order_);
    }
  }

 private:
  bool valid_ = true;
  const PgsqlReadRequestMsg& request_;
  PgsqlResponseMsg& response_;
  PgsqlBatchArgumentMsgs::const_iterator batch_arg_it_;
  YbctidBounds bounds_;
  std::optional<int64> current_order_;
};

Result<size_t> PgsqlReadOperation::Execute() {
  // Verify that this request references no columns marked for deletion.
  RETURN_NOT_OK(VerifyNoRefColsMarkedForDeletion(data_.doc_read_context.schema(), request_));
  size_t fetched_rows = 0;
  auto num_rows_pos = result_buffer_->Position();
  // Reserve space for fetched rows count.
  pggate::PgWire::WriteInt64(0, result_buffer_);
  auto se = ScopeExit([&fetched_rows, num_rows_pos, this] {
    CHECK_OK(pggate::PgWire::WriteInt64(fetched_rows, result_buffer_, num_rows_pos));
  });
  VLOG(4) << "Read, read operation data: " << data_.read_operation_data.ToString() << ", txn: "
          << data_.txn_op_context;

  // Fetching data.
  bool has_paging_state = false;
  if (request_.batch_arguments_size() > 0) {
    PgsqlReadRequestYbctidProvider key_provider(data_.doc_read_context, request_, response_);
    fetched_rows = VERIFY_RESULT(ExecuteBatchKeys(key_provider));
  } else if (request_.has_sampling_state()) {
    if (data_.doc_read_context.is_index) {
      return STATUS_FORMAT(
          InvalidArgument, "Sampling of index table ($0) is not supported and not expected",
          request_.table_id());
    }
    if (request_.sampling_state().sampling_algorithm() ==
        YsqlSamplingAlgorithm::BLOCK_BASED_SAMPLING) {
      if (request_.sampling_state().is_blocks_sampling_stage() ||
          request_.sample_blocks().size() > 0) {
        std::tie(fetched_rows, has_paging_state) = VERIFY_RESULT(ExecuteSampleBlockBased());
      } else {
        // Only for backward compatibility.
        std::tie(fetched_rows, has_paging_state) =
            VERIFY_RESULT(DEPRECATED_ExecuteSampleBlockBasedColocated());
      }
    } else {
      std::tie(fetched_rows, has_paging_state) = VERIFY_RESULT(ExecuteSample());
    }
  } else if (request_.index_request().has_vector_idx_options()) {
    fetched_rows = VERIFY_RESULT(ExecuteVectorLSMSearch(
        request_.index_request().vector_idx_options()));
    has_paging_state = false;
  } else {
    std::tie(fetched_rows, has_paging_state) = VERIFY_RESULT(ExecuteScalar());
  }

  VTRACE(1, "Fetched $0 rows. $1 paging state", fetched_rows, (has_paging_state ? "No" : "Has"));
  if (table_iter_) {
    *read_restart_data_ = VERIFY_RESULT(table_iter_->GetReadRestartData());
  } else {
    *read_restart_data_ = ReadRestartData();
  }
  if (index_iter_) {
    read_restart_data_->MakeAtLeast(VERIFY_RESULT(index_iter_->GetReadRestartData()));
  }
  if (!read_restart_data_->is_valid()) {
    RETURN_NOT_OK(delayed_failure_);
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

namespace {

void SamplerRandomStateToPb(YbgReservoirState rstate, PgsqlSamplingStateMsg* sampling_state) {
  uint64_t randstate_s0 = 0;
  uint64_t randstate_s1 = 0;
  double rstate_w = 0;
  YbgSamplerGetState(rstate, &rstate_w, &randstate_s0, &randstate_s1);
  sampling_state->set_rstate_w(rstate_w);
  auto* pg_prng_state = sampling_state->mutable_rand_state();
  pg_prng_state->set_s0(randstate_s0);
  pg_prng_state->set_s1(randstate_s1);
}

} // namespace

Result<std::tuple<size_t, bool>> PgsqlReadOperation::ExecuteSample() {
  const auto& sampling_state = request_.sampling_state();
  // Requested total number of rows to collect
  int targrows = sampling_state.targrows();
  // Number of rows collected so far
  int numrows = sampling_state.numrows();
  // Total number of rows scanned
  double samplerows = sampling_state.samplerows();
  // Current number of rows to skip before collecting next one for sample
  double rowstoskip = sampling_state.rowstoskip();
  // Variables for the random numbers generator
  SCHECK(sampling_state.has_rand_state(), InvalidArgument,
         "Invalid sampling state, random state is missing");
  YbgPrepareMemoryContext();
  YbgReservoirState rstate = NULL;
  YbgSamplerCreate(
      sampling_state.rstate_w(), sampling_state.rand_state().s0(), sampling_state.rand_state().s1(),
      &rstate);
  // Buffer to hold selected row ids from the current page
  std::unique_ptr<std::string[]> reservoir = std::make_unique<std::string[]>(targrows);
  // Number of rows to scan for the current page.
  // Too low row count limit is inefficient since we have to allocate and initialize a reservoir
  // capable to hold potentially large (targrows) number of tuples. The row count has to be at least
  // targrows for a chance to fill up the reservoir. Actually, the algorithm selects targrows only
  // for very first page of the table, then it starts to skip tuples, the further it goes, the more
  // it skips. For a large enough table it eventually starts to select less than targrows per page,
  // regardless of the row_count_limit.
  // Anyways, double targrows seems like reasonable minimum for the row_count_limit.

  VLOG(2) << "Start sampling tablet with sampling_state: " << sampling_state.ShortDebugString();

  auto projection = CreateProjection(data_.doc_read_context.schema(), request_);
  table_iter_ = VERIFY_RESULT(CreateIterator(data_, request_, projection, data_.doc_read_context));
  bool scan_time_exceeded = false;
  auto stop_scan = data_.read_operation_data.deadline - FLAGS_ysql_scan_deadline_margin_ms * 1ms;
  while (VERIFY_RESULT(table_iter_->FetchNext(nullptr))) {
    samplerows++;
    if (numrows < targrows) {
      // Select first targrows of the table. If first partition(s) have less than that, next
      // partition starts to continue populating it's reservoir starting from the numrows' position:
      // the numrows, as well as other sampling state variables is returned and copied over to the
      // next sampling request
      Slice ybctid = table_iter_->GetTupleId();
      reservoir[numrows++] = ybctid;
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
        reservoir[k] = ybctid;
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

  const auto fetched_rows =
      VERIFY_RESULT(WriteRowsReservoir(reservoir, numrows, result_buffer_));

  // Return sampling state to continue with next page
  auto& new_sampling_state = *response_.mutable_sampling_state();
  new_sampling_state.set_numrows(numrows);
  new_sampling_state.set_targrows(targrows);
  new_sampling_state.set_samplerows(samplerows);
  new_sampling_state.set_rowstoskip(rowstoskip);
  SamplerRandomStateToPb(rstate, &new_sampling_state);
  YbgDeleteMemoryContext();

  // Return paging state if scan has not been completed
  bool has_paging_state = false;
  if (request_.return_paging_state() && scan_time_exceeded) {
    has_paging_state = VERIFY_RESULT(SetPagingState(
        table_iter_.get(),
        data_.doc_read_context.schema(),
        data_.read_operation_data.read_time,
        ReadKey::kNext));
  }

  VLOG(2) << "End sampling with new_sampling_state: " << new_sampling_state.ShortDebugString()
          << " paging_state: " << response_.paging_state().ShortDebugString();

  return std::tuple<size_t, bool>{fetched_rows, has_paging_state};
}

namespace {

std::pair<Slice, Slice> GetSampleBlockBounds(const PgsqlSampleBlockMsg& sample_block) {
  return {sample_block.lower_bound(), sample_block.upper_bound()};
}

std::pair<Slice, Slice> GetSampleBlockBounds(const std::pair<KeyBuffer, KeyBuffer>& sample_block) {
  return {sample_block.first.AsSlice(), sample_block.second.AsSlice()};
}

// REQUIRES: table_iter is positioned to the first key for the table, sample_blocks are
// non-overlapping and ordered consistently with Slice::compare order.
template <typename T>
Status SampleRowsFromBlocks(
    YQLRowwiseIteratorIf* table_iter,
    const T& sample_blocks,
    Slice table_upperbound, const CoarseTimePoint& stop_scan_time,
    PgsqlSamplingStateMsg* sampling_state, YbgReservoirState rstate,
    std::vector<std::string>* reservoir) {
  if (!VERIFY_RESULT(table_iter->PgFetchNext(nullptr))) {
    LOG(INFO) << "Nothing to sample";
    return Status::OK();
  }

  const auto targrows = sampling_state->targrows();
  auto num_rows_collected = sampling_state->numrows();
  auto num_sample_rows = sampling_state->samplerows();
  auto rows_to_skip = sampling_state->rowstoskip();

  auto row_key = table_iter->GetRowKey();
  bool fetch_next_needed = false;

  size_t sample_block_idx = 0;
  size_t num_blocks_with_rows = 0;
  for (const auto& sample_block : sample_blocks) {
    const auto[lower_bound_key_inclusive, upper_bound_key_exclusive] =
        GetSampleBlockBounds(sample_block);
    const auto is_seek_needed = row_key.compare(lower_bound_key_inclusive) < 0;
    VLOG(3) << "Sorted sample block #" << sample_block_idx
            << " lower_bound_key_inclusive: " << DebugKeySliceToString(lower_bound_key_inclusive)
            << " upper_bound_key_exclusive: " << DebugKeySliceToString(upper_bound_key_exclusive)
            << " row_key: " << DebugKeySliceToString(row_key)
            << " is_seek_needed: " << is_seek_needed;
    if (is_seek_needed) {
      table_iter->SeekToDocKeyPrefix(lower_bound_key_inclusive);
      fetch_next_needed = true;
    }

    bool reached_end_of_tablet = false;
    bool found_row = false;
    for (;; ++num_sample_rows, fetch_next_needed = true) {
      if (fetch_next_needed) {
        if (!VERIFY_RESULT(table_iter->PgFetchNext(nullptr))) {
          reached_end_of_tablet = true;
          break;
        }
        row_key = table_iter->GetRowKey();
      }
      if (row_key.compare(
              !upper_bound_key_exclusive.empty() ? upper_bound_key_exclusive
                                                 : table_upperbound) >= 0) {
        fetch_next_needed = false;
        break;
      }

      found_row = true;
      VLOG(4) << "ybctid: " << DebugKeySliceToString(table_iter->GetTupleId())
              << " num_rows_collected: " << num_rows_collected
              << " num_sample_rows: " << num_sample_rows
              << " rows_to_skip: " << rows_to_skip
              << " row_key: " << DebugKeySliceToString(row_key);
      if (num_rows_collected < targrows) {
        const auto ybctid = table_iter->GetTupleId();
        (*reservoir)[num_rows_collected++] = ybctid;
      } else if (rows_to_skip > 0) {
        // At least targrows tuples have already been collected, now algorithm skips increasing
        // number of rows before taking next one into the reservoir.
        --rows_to_skip;
      } else {
        const auto ybctid = table_iter->GetTupleId();
        // Pick random tuple in the reservoir to replace
        double rvalue;
        YbgSamplerRandomFract(rstate, &rvalue);
        const auto k = static_cast<int>(targrows * rvalue);
        // Replace previous candidate with the new one.
        (*reservoir)[k] = ybctid;
        // Choose next number of rows to skip.
        YbgReservoirGetNextS(rstate, num_sample_rows, targrows, &rows_to_skip);
        VLOG(3) << "Next reservoir sampling rows_to_skip: " << rows_to_skip;
      }

      if (CoarseMonoClock::now() >= stop_scan_time) {
        LOG_WITH_FUNC(INFO) << "ANALYZE sampling scan exceeded deadline";
        // TODO(analyze_sampling): https://github.com/yugabyte/yugabyte-db/issues/25306
        return STATUS(TimedOut, "ANALYZE block-based sampling scan exceeded deadline");
      }
    }

    if (found_row) {
      ++num_blocks_with_rows;
    }

    VLOG(3) << "num_sample_rows: " << size_t(num_sample_rows) << ", ybctid after the sample block #"
            << sample_block_idx << ": " << DebugKeySliceToString(table_iter->GetTupleId())
            << " row_key: " << DebugKeySliceToString(row_key)
            << " found_row: " << found_row
            << " num_blocks_with_rows: " << num_blocks_with_rows;
    if (reached_end_of_tablet) {
      break;
    }
    ++sample_block_idx;
  }

  VLOG(2) << "num_blocks_with_rows: " << num_blocks_with_rows;
  sampling_state->set_samplerows(num_sample_rows);
  sampling_state->set_rowstoskip(rows_to_skip);
  sampling_state->set_numrows(num_rows_collected);
  return Status::OK();
}

} // namespace

Result<std::tuple<size_t, bool>> PgsqlReadOperation::DEPRECATED_ExecuteSampleBlockBasedColocated() {
  if (!data_.doc_read_context.schema().is_colocated()) {
    return STATUS_FORMAT(
        NotSupported,
        "ExecuteSampleBlockBased is only supported for colocated tables, table_id: $0",
        request_.table_id());
  }

  VLOG(2) << "DocDB stats:\n" << DocDbStatsToString(data_.read_operation_data.statistics);

  // Will return sampling state with info for upper layer.
  auto& new_sampling_state = *response_.mutable_sampling_state();
  new_sampling_state = request_.sampling_state();

  if (new_sampling_state.rowstoskip() > 0) {
    return STATUS_FORMAT(
        NotSupported,
        "ExecuteSampleBlockBased sampling_state.rowstoskip is not expected to be set for colocated "
        "tables, but got: $0",
        new_sampling_state.rowstoskip());
  }

  if (new_sampling_state.numrows() != 0) {
    return STATUS_FORMAT(
        NotSupported,
        "ExecuteSampleBlockBased expected sampling_state.numrows to be 0 for colocated tables, but "
        "got: $0",
        new_sampling_state.numrows());
  }

  if (new_sampling_state.samplerows() > 0) {
    return STATUS_FORMAT(
        NotSupported,
        "ExecuteSampleBlockBased expected sampling_state.samplerows to be 0 for colocated tables, "
        "but got: $0",
        new_sampling_state.samplerows());
  }

  const auto targrows = new_sampling_state.targrows();

  YQLStorageIf::BlocksSamplingState blocks_sampling_state = {
      .num_blocks_processed = 0,
      .num_blocks_collected = 0,
  };
  auto sample_blocks = VERIFY_RESULT(data_.ql_storage.GetSampleBlocks(
      data_.doc_read_context, new_sampling_state.docdb_blocks_sampling_method(), targrows,
      &blocks_sampling_state));
  new_sampling_state.set_num_blocks_processed(blocks_sampling_state.num_blocks_processed);
  new_sampling_state.set_num_blocks_collected(blocks_sampling_state.num_blocks_collected);

  LOG_WITH_FUNC(INFO) << "Sorting sample blocks";
  std::sort(
      sample_blocks.begin(), sample_blocks.end(),
      [](const std::pair<KeyBuffer, KeyBuffer>& b1, const std::pair<KeyBuffer, KeyBuffer>& b2) {
        // Keep not-initialized/non-bounded blocks at the end.
        if (b1.first.empty() && b1.second.empty()) {
          return false;
        }
        if (b2.first.empty() && b2.second.empty()) {
          return true;
        }
        return b1.first < b2.first;
      });
  sample_blocks.resize(blocks_sampling_state.num_blocks_collected);

  VLOG(2) << "DocDB stats:\n" << DocDbStatsToString(data_.read_operation_data.statistics);

  YbgPrepareMemoryContext();
  YbgReservoirState rstate = nullptr;
  YbgSamplerCreate(
      new_sampling_state.rstate_w(), new_sampling_state.rand_state().s0(),
      new_sampling_state.rand_state().s1(), &rstate);

  std::vector<std::string> reservoir(targrows);

  LOG_WITH_FUNC(INFO) << "Start sampling tablet with sampling_state: "
                      << new_sampling_state.ShortDebugString();

  auto projection = CreateProjection(data_.doc_read_context.schema(), request_);

  table_iter_ = VERIFY_RESULT(CreateIterator(data_, request_, projection, data_.doc_read_context));

  const auto stop_scan_time =
      data_.read_operation_data.deadline - FLAGS_ysql_scan_deadline_margin_ms * 1ms;

  RETURN_NOT_OK(SampleRowsFromBlocks(
      table_iter_.get(), sample_blocks, data_.doc_read_context.upperbound(), stop_scan_time,
      &new_sampling_state, rstate, &reservoir));
  reservoir.resize(new_sampling_state.numrows());

  // It is correct for colocated tables since there is only one reservoir per table.
  // In the final rows sample, sort by scan order is important to calculate pg_stats.correlation
  // properly.
  // Having keys sorted helps with utilizing disk read-ahead mechanism and improve performance
  // for reading rows that are located nearby.
  VLOG_WITH_FUNC(1) << "Reservoir sort start";
  std::ranges::sort(reservoir);
  VLOG_WITH_FUNC(1) << "Reservoir sort completed";

  const auto fetched_rows =
      VERIFY_RESULT(WriteRowsReservoir(reservoir, new_sampling_state.numrows(), result_buffer_));

  const auto num_blocks_processed = new_sampling_state.num_blocks_processed();
  const auto num_blocks_collected = new_sampling_state.num_blocks_collected();
  new_sampling_state.set_deprecated_estimated_total_rows(
      num_blocks_collected >= num_blocks_processed
          ? new_sampling_state.samplerows()
          : 1.0 * new_sampling_state.samplerows() * num_blocks_processed / num_blocks_collected);
  SamplerRandomStateToPb(rstate, &new_sampling_state);
  YbgDeleteMemoryContext();

  LOG_WITH_FUNC(INFO) << "End sampling with new_sampling_state: "
                      << new_sampling_state.ShortDebugString();
  VLOG(2) << "DocDB stats:\n" << DocDbStatsToString(data_.read_operation_data.statistics);

  return std::tuple<size_t, bool>{fetched_rows, false};
}

Result<std::tuple<size_t, bool>> PgsqlReadOperation::ExecuteSampleBlockBased() {
  VLOG(2) << "DocDB stats:\n" << DocDbStatsToString(data_.read_operation_data.statistics);

  // Will return sampling state with info for upper layer.
  auto& new_sampling_state = *response_.mutable_sampling_state();
  new_sampling_state = request_.sampling_state();

  VLOG_WITH_FUNC(2) << "timeout: "
                    << AsString(data_.read_operation_data.deadline - CoarseMonoClock::now());

  const auto targrows = new_sampling_state.targrows();
  // Items are either sample blocks or sample rows.
  size_t fetched_items;

  LOG_WITH_FUNC(INFO) << "Start sampling tablet with sampling_state: "
                      << new_sampling_state.ShortDebugString()
                      << " num_sample_blocks: " << request_.sample_blocks().size()
                      << " tablet: " << data_.ql_storage.ToString();

  if (new_sampling_state.is_blocks_sampling_stage()) {
    YQLStorageIf::BlocksSamplingState blocks_sampling_state = {
        .num_blocks_processed = new_sampling_state.num_blocks_processed(),
        .num_blocks_collected = new_sampling_state.num_blocks_collected(),
    };
    auto sample_blocks = VERIFY_RESULT(data_.ql_storage.GetSampleBlocks(
        data_.doc_read_context, new_sampling_state.docdb_blocks_sampling_method(), targrows,
        &blocks_sampling_state));
    new_sampling_state.set_num_blocks_processed(blocks_sampling_state.num_blocks_processed);
    new_sampling_state.set_num_blocks_collected(blocks_sampling_state.num_blocks_collected);

    fetched_items = VERIFY_RESULT(WriteBlocksReservoir(sample_blocks, result_buffer_));
  } else {
    // We are at the second stage, sample blocks are ready, need to get sample rows from them.
    VLOG(2) << "DocDB stats:\n" << DocDbStatsToString(data_.read_operation_data.statistics);

    YbgPrepareMemoryContext();
    YbgReservoirState rstate = nullptr;
    YbgSamplerCreate(
        new_sampling_state.rstate_w(), new_sampling_state.rand_state().s0(),
        new_sampling_state.rand_state().s1(), &rstate);

    std::vector<std::string> reservoir(targrows);

    auto projection = CreateProjection(data_.doc_read_context.schema(), request_);

    table_iter_ =
        VERIFY_RESULT(CreateIterator(data_, request_, projection, data_.doc_read_context));

    const auto stop_scan_time =
        data_.read_operation_data.deadline - FLAGS_ysql_scan_deadline_margin_ms * 1ms;

    RETURN_NOT_OK(SampleRowsFromBlocks(
        table_iter_.get(), request_.sample_blocks(), data_.doc_read_context.upperbound(),
        stop_scan_time, &new_sampling_state, rstate, &reservoir));

    fetched_items =
        VERIFY_RESULT(WriteRowsReservoir(reservoir, new_sampling_state.numrows(), result_buffer_));

    SamplerRandomStateToPb(rstate, &new_sampling_state);
    YbgDeleteMemoryContext();
  }

  // TODO(analyze_sampling): https://github.com/yugabyte/yugabyte-db/issues/25306 - return paging
  // state if scan has not been completed within deadline.

  LOG_WITH_FUNC(INFO) << "End sampling with new_sampling_state: "
                      << new_sampling_state.ShortDebugString()
                      << " fetched_items: " << fetched_items;
  VLOG(2) << "DocDB stats:\n" << DocDbStatsToString(data_.read_operation_data.statistics);

  return std::tuple<size_t, bool>{fetched_items, false};
}

Result<size_t> PgsqlReadOperation::ExecuteVectorLSMSearch(const PgVectorReadOptionsMsg& options) {
  RSTATUS_DCHECK(
      data_.vector_index, IllegalState, "Search vector when vector index is null: $0", request_);

  Slice vector_slice(options.vector().binary_value());
  size_t max_results = options.num_top_vectors_to_remove() + options.prefetch_size();

  table_iter_.reset();
  PgsqlVectorFilter filter(&table_iter_);
  auto could_have_missing_entries = !VERIFY_RESULT(filter.Init(data_));
  RSTATUS_DCHECK(
      data_.vector_index->BackfillDone(), IllegalState,
      "Vector index query on non ready index: $0", *data_.vector_index);
  auto result = VERIFY_RESULT(data_.vector_index->Search(
      vector_slice,
      vector_index::SearchOptions {
        .max_num_results = max_results,
        .ef = options.hnsw_options().ef_search(),
        .filter = std::ref(filter),
      },
      could_have_missing_entries
  ));
  VLOG_WITH_FUNC(2) << "Search results: " << result.ToString();

  // TODO(vector_index) Order keys by ybctid for fetching.
  auto dump_stats = FLAGS_vector_index_dump_stats;
  auto read_start_time = MonoTime::NowIf(dump_stats);
  VectorIndexKeyProvider key_provider(
      result, response_, *data_.vector_index, vector_slice,
      options.num_top_vectors_to_remove(), max_results, *result_buffer_);
  auto res = ExecuteBatchKeys(key_provider);
  LOG_IF(INFO, dump_stats)
      << "VI_STATS: Read rows data time: "
      << (MonoTime::Now() - key_provider.merge_done_time()).ToPrettyString()
      << ", read intents time: "
      << (key_provider.prefetch_done_time() - read_start_time).ToPrettyString()
      << ", merge with intents time: "
      << (key_provider.merge_done_time() - key_provider.prefetch_done_time()).ToPrettyString()
      << ", found intents: " << key_provider.found_intents() << ", size: " << res;
  return res;
}

void PgsqlReadOperation::BindReadTimeToPagingState(const ReadHybridTime& read_time) {
  auto paging_state = response_.mutable_paging_state();
  if (FLAGS_pgsql_consistent_transactional_paging) {
    read_time.AddToPB(paging_state);
  } else {
    // Using SingleTime will help avoid read restarts on second page and later but will
    // potentially produce stale results on those pages.
    auto per_row_consistent_read_time = ReadHybridTime::SingleTime(read_time.read);
    per_row_consistent_read_time.AddToPB(paging_state);
  }
}

Result<std::tuple<size_t, bool>> PgsqlReadOperation::ExecuteScalar() {
  // Requests normally have a limit on how many rows to return
  auto row_count_limit = std::numeric_limits<std::size_t>::max();
  if (request_.has_limit() && request_.limit() > 0) {
    row_count_limit = request_.limit();
  }

  // The response size should not exceed the rpc_max_message_size, so use it as a default size
  // limit. Reduce it to the number requested by the client, if it is stricter.
  uint64_t response_size_limit = GetAtomicFlag(&FLAGS_rpc_max_message_size) *
                                 GetAtomicFlag(&FLAGS_max_buffer_size_to_rpc_limit_ratio);
  if (request_.has_size_limit() && request_.size_limit() > 0) {
    response_size_limit = std::min(response_size_limit, request_.size_limit());
  }

  VLOG_WITH_FUNC(4)
      << "Row count limit: " << row_count_limit << ", size limit: " << response_size_limit;

  // Create the projection of regular columns selected by the row block plus any referenced in
  // the WHERE condition. When DocRowwiseIterator::NextRow() populates the value map, it uses this
  // projection only to scan sub-documents. The query schema is used to select only referenced
  // columns and key columns.
  auto doc_projection = CreateProjection(data_.doc_read_context.schema(), request_);
  FilteringIterator table_iter(&table_iter_);

  std::optional<IndexState> index_state;
  if (data_.index_doc_read_context) {
    RETURN_NOT_OK(table_iter.InitForYbctid(data_, doc_projection, data_.doc_read_context, {}));

    const auto& index_schema = data_.index_doc_read_context->schema();
    const auto idx = index_schema.find_column("ybidxbasectid");
    SCHECK_NE(idx, Schema::kColumnNotFound, Corruption, "ybidxbasectid not found in index schema");
    index_state.emplace(
        index_schema, request_.index_request(), &index_iter_, index_schema.column_id(idx));
    RETURN_NOT_OK(index_state->iter.Init(
        data_, request_.index_request(), index_state->projection, *data_.index_doc_read_context));
  } else {
    RETURN_NOT_OK(table_iter.Init(data_, request_, doc_projection, data_.doc_read_context));
  }

  // Set scan end time. We want to iterate as long as we can, but stop before client timeout.
  // The more rows we do per request, the less RPCs will be needed, but if client times out,
  // efforts are wasted.
  bool scan_time_exceeded = false;
  auto stop_scan = data_.read_operation_data.deadline - FLAGS_ysql_scan_deadline_margin_ms * 1ms;
  size_t match_count = 0;
  auto fetch_limit = FetchLimit::kNotReached;
  size_t fetched_rows = 0;
  dockv::PgTableRow row(doc_projection);
  const auto& table_id = request_.index_request().table_id();
  do {
    const auto fetch_result = VERIFY_RESULT(FetchTableRow(
        table_id, &table_iter, index_state ? &*index_state : nullptr, &row));
    // If changing this code, see also PgsqlReadOperation::ExecuteBatchKeys.
    if (fetch_result == FetchResult::NotFound) {
      break;
    }
    ++scanned_table_rows_;
    if (fetch_result == FetchResult::Found) {
      ++match_count;
      if (request_.is_aggregate()) {
        RETURN_NOT_OK(EvalAggregate(row));
      } else {
        auto row_start = result_buffer_->Position();
        RETURN_NOT_OK(PopulateResultSet(row, result_buffer_));
        if (fetched_rows > 0 && result_buffer_->size() > response_size_limit) {
          RETURN_NOT_OK(result_buffer_->Truncate(row_start));
          fetch_limit = FetchLimit::kExceeded;
          // skips the fetched_rows increment and the other limit's check, which may change
          // the fetch_limit value
          break;
        }
        ++fetched_rows;
      }
    }
    scan_time_exceeded = CoarseMonoClock::now() >= stop_scan;
    if (scan_time_exceeded ||
        fetched_rows >= row_count_limit ||
        result_buffer_->size() >= response_size_limit) {
      fetch_limit = FetchLimit::kReached;
    }
  } while (fetch_limit == FetchLimit::kNotReached);

  // Output aggregate values accumulated while looping over rows
  if (request_.is_aggregate() && match_count > 0) {
    RETURN_NOT_OK(PopulateAggregate(result_buffer_));
    ++fetched_rows;
  }

  VLOG_WITH_FUNC(3)
          << "Stopped iterator after " << match_count << " matches, " << fetched_rows
          << " rows fetched. Response buffer size: " << result_buffer_->size()
          << ", response size limit: " << response_size_limit
          << ", deadline is " << (scan_time_exceeded ? "" : "not ") << "exceeded";

  if (PREDICT_FALSE(FLAGS_TEST_slowdown_pgsql_aggregate_read_ms > 0) && request_.is_aggregate()) {
    TRACE("Sleeping for $0 ms", FLAGS_TEST_slowdown_pgsql_aggregate_read_ms);
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_pgsql_aggregate_read_ms));
  }

  bool has_paging_state = false;
  // Unless iterated to the end, pack current iterator position into response, so follow up request
  // can seek to correct position and continue
  if (request_.return_paging_state() && fetch_limit != FetchLimit::kNotReached) {
    auto* iterator = table_iter_.get();
    const auto* read_context = &data_.doc_read_context;
    if (index_state) {
      iterator = index_iter_.get();
      read_context = data_.index_doc_read_context;
    }
    DCHECK(iterator && read_context);
    has_paging_state = VERIFY_RESULT(SetPagingState(
        iterator, read_context->schema(), data_.read_operation_data.read_time,
        fetch_limit == FetchLimit::kExceeded ? ReadKey::kCurrent : ReadKey::kNext));
  }

  if (index_state) {
    scanned_index_rows_ += index_state->scanned_rows;
    if (delayed_failure_.ok()) {
      delayed_failure_ = index_state->delayed_failure;
    }
  }
  return std::tuple<size_t, bool>{fetched_rows, has_paging_state};
}

template <class KeyProvider>
Result<size_t> PgsqlReadOperation::ExecuteBatchKeys(KeyProvider& key_provider) {
  // We limit the response's size.
  uint64_t response_size_limit = GetAtomicFlag(&FLAGS_rpc_max_message_size) *
                                 GetAtomicFlag(&FLAGS_max_buffer_size_to_rpc_limit_ratio);
  if (request_.has_size_limit() && request_.size_limit() > 0) {
    response_size_limit = std::min(response_size_limit, request_.size_limit());
  }
  VLOG_WITH_FUNC(3) << "Started, response size limit: " << response_size_limit
                    << " request_.batch_arguments_size(): " << request_.batch_arguments_size()
                    << " tablet: " << data_.ql_storage.ToString();

  const auto& doc_read_context = data_.doc_read_context;
  auto projection = CreateProjection(doc_read_context.schema(), request_);
  dockv::PgTableRow row(projection);

  std::optional<FilteringIterator> iter;
  size_t found_rows = 0;
  size_t fetched_rows = 0;
  size_t filtered_rows = 0;
  size_t not_found_rows = 0;
  size_t processed_keys = 0;

  table_iter_.reset();
  if constexpr (Prefetcher<KeyProvider>) {
    iter.emplace(&table_iter_);
    RETURN_NOT_OK(iter->InitForYbctid(data_, projection, doc_read_context, Bounds(key_provider)));
    RETURN_NOT_OK(key_provider.Prefetch(*iter, projection));
    iter->Refresh();
  }

  for (;;) {
    auto key = key_provider.FetchKey();
    if (key.empty()) {
      break;
    }

    if (!iter) {
      // It can be the case like when there is a tablet split that we still want
      // to continue seeking through all the given batch arguments even though one
      // of them wasn't found. If it wasn't found, table_iter_ becomes invalid
      // and we have to make a new iterator.
      // TODO (dmitry): In case of iterator recreation info from ReadRestartData field will be lost.
      //                The #17159 issue is created for this problem.
      iter.emplace(&table_iter_);
      RETURN_NOT_OK(iter->InitForYbctid(data_, projection, doc_read_context, Bounds(key_provider)));
    }

    // If changing this code, see also PgsqlReadOperation::ExecuteScalar.
    switch (VERIFY_RESULT(iter->FetchTuple(key, &row))) {
      case FetchResult::NotFound:
        ++not_found_rows;
        // rebuild iterator on next iteration
        iter = std::nullopt;
        break;
      case FetchResult::FilteredOut:
        ++filtered_rows;
        ++scanned_table_rows_;
        break;
      case FetchResult::Found:
        ++found_rows;
        ++scanned_table_rows_;
        if (request_.is_aggregate()) {
          RETURN_NOT_OK(EvalAggregate(row));
        } else {
          auto row_start = result_buffer_->Position();
          RETURN_NOT_OK(PopulateResultSet(row, result_buffer_));
          if (fetched_rows > 0 && result_buffer_->size() > response_size_limit) {
            VLOG(1) << "Response size exceeded after " << found_rows << " rows fetched (out of "
                    << request_.batch_arguments_size() << " matches). Also " << filtered_rows
                    << " rows filtered, " << not_found_rows << " rows not found. "
                    << "Response buffer size: " << result_buffer_->size()
                    << ", response size limit: " << response_size_limit;
            RETURN_NOT_OK(result_buffer_->Truncate(row_start));
            // TODO GHI #25788, for now fail instead of returning incomplete results
            RSTATUS_DCHECK(!request_.batch_arguments().empty(),
                           IllegalState, "Pagination is required, but not supported");
            DCHECK(processed_keys < static_cast<size_t>(request_.batch_arguments_size()));
            response_.set_batch_arg_count(processed_keys);
            return fetched_rows;
          }
          key_provider.AddedKeyToResultSet();
          ++fetched_rows;
        }
        break;
    }
    ++processed_keys;
  }

  // Output aggregate values accumulated while looping over rows
  if (request_.is_aggregate() && found_rows > 0) {
    RETURN_NOT_OK(PopulateAggregate(result_buffer_));
    ++fetched_rows;
  }

  VLOG(1) << "Request completed after " << found_rows << " rows fetched (out of "
          << request_.batch_arguments_size() << " matches). Also " << filtered_rows
          << " rows filtered, " << not_found_rows << " rows not found. "
          << "Response buffer size: " << result_buffer_->size()
          << ", response size limit: " << response_size_limit;

  // TODO GHI #25789 the KeyProvider should track processed_keys and paginate
  if (request_.batch_arguments_size() > 0) {
    DCHECK(processed_keys == static_cast<size_t>(request_.batch_arguments_size()));
    response_.set_batch_arg_count(processed_keys);
  }

  VLOG_WITH_FUNC(3) << "Stopped, filtered_rows: " << filtered_rows << " found_rows: " << found_rows
                    << ". DocDB stats:\n"
                    << DocDbStatsToString(data_.read_operation_data.statistics);
  return fetched_rows;
}

Result<bool> PgsqlReadOperation::SetPagingState(
    YQLRowwiseIteratorIf* iter, const Schema& schema, const ReadHybridTime& read_time,
    ReadKey page_from_read_key) {
  // Get the key of the requested row
  SubDocKey row_key = VERIFY_RESULT(iter->GetSubDocKey(page_from_read_key));

  // If iterator is at the last row and next row is requested, no need to send back paging info
  if (row_key.doc_key().empty()) {
    return false;
  }

  auto* paging_state = response_.mutable_paging_state();
  auto encoded_row_key = row_key.Encode().ToStringBuffer();
  if (schema.num_hash_key_columns() > 0) {
    paging_state->set_next_partition_key(
        dockv::PartitionSchema::EncodeMultiColumnHashValue(row_key.doc_key().hash()));
  } else {
    paging_state->set_next_partition_key(encoded_row_key);
  }
  paging_state->set_next_row_key(std::move(encoded_row_key));

  BindReadTimeToPagingState(read_time);

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
    const PgsqlExpressionMsg& expr, bool last) {
  if (expr.expr_case() == PgsqlExpressionPB::kColumnId) {
    if (expr.column_id() != std::to_underlying(PgSystemAttrNum::kYBTupleId)) {
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

void PgsqlReadOperation::InitTargetEncoders(
    const PgsqlExpressionMsgs& targets,
    const dockv::PgTableRow& table_row) {
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
    target_encoders_.push_back(dockv::PgWireEncoderEntry{
        .encoder = NopEncoder,
        .data = 0,
    });
  }
}

Status PgsqlReadOperation::PopulateResultSet(const dockv::PgTableRow& table_row,
                                             WriteBuffer *result_buffer) {
  if (target_encoders_.empty()) {
    InitTargetEncoders(request_.targets(), table_row);
  }
  target_encoders_.front().Invoke(table_row, result_buffer);
  return Status::OK();
}

Result<Slice> PgsqlReadOperation::GetSpecialColumn(ColumnIdRep column_id) {
  // Get row key and save to QLValue.
  // TODO(neil) Check if we need to append a table_id and other info to TupleID. For example, we
  // might need info to make sure the TupleId by itself is a valid reference to a specific row of
  // a valid table.
  if (column_id == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
    return table_iter_->GetTupleId();
  }

  return STATUS_SUBSTITUTE(InvalidArgument, "Invalid column ID: $0", column_id);
}

Status PgsqlReadOperation::EvalAggregate(const dockv::PgTableRow& table_row) {
  if (aggr_result_.empty()) {
    int column_count = request_.targets().size();
    aggr_result_.resize(column_count);
  }

  int aggr_index = 0;
  for (const auto& expr : request_.targets()) {
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

Status GetIntents(
    const PgsqlReadRequestPB& request, const Schema& schema, IsolationLevel level,
    LWKeyValueWriteBatchPB* out) {
  const auto row_mark = request.has_row_mark_type() ? request.row_mark_type() : ROW_MARK_ABSENT;
  if (IsValidRowMarkType(row_mark)) {
    RSTATUS_DCHECK(request.has_wait_policy(), IllegalState, "wait policy is expected");
    out->set_wait_policy(request.wait_policy());
  }

  IntentInserter inserter(out);
  const auto has_batch_arguments = !request.batch_arguments().empty();

  DocKeyAccessor accessor(schema);
  if (!(has_batch_arguments || request.has_ybctid_column_value())) {
    auto slice = VERIFY_RESULT(accessor.GetEncoded(request));
    bool inlcudes_pk = static_cast<bool>(accessor.pk_is_known());
    inserter.Add(slice, inlcudes_pk, {level, row_mark, KeyOnlyRequested::kFalse});
    return Status::OK();
  }

  const IntentMode mode{
      level, row_mark, KeyOnlyRequested(IsOnlyKeyColumnsRequested(schema, request))};
  for (const auto& batch_argument : request.batch_arguments()) {
    auto slice = VERIFY_RESULT(accessor.GetEncoded(batch_argument.ybctid()));
    bool inlcudes_pk = static_cast<bool>(accessor.pk_is_known());
    inserter.Add(slice, inlcudes_pk, mode);
  }
  if (!has_batch_arguments) {
    DCHECK(request.has_ybctid_column_value());
    auto slice = VERIFY_RESULT(accessor.GetEncoded(request.ybctid_column_value()));
    bool inlcudes_pk = static_cast<bool>(accessor.pk_is_known());
    inserter.Add(slice, inlcudes_pk, mode);
  }
  return Status::OK();
}

PgsqlLockOperation::PgsqlLockOperation(
    std::reference_wrapper<const PgsqlLockRequestMsg> request,
    const TransactionOperationContext& txn_op_context)
        : DocOperationBase(request), txn_op_context_(txn_op_context) {
}

Status PgsqlLockOperation::Init(
    PgsqlResponseMsg* response, const DocReadContextPtr& doc_read_context) {
  response_ = response;

  auto& schema = doc_read_context->schema();

  auto hashed_components = VERIFY_RESULT(dockv::QLKeyColumnValuesToPrimitiveValues(
      request_.lock_id().lock_partition_column_values(), schema, 0,
      schema.num_hash_key_columns()));
  auto range_components = VERIFY_RESULT(dockv::QLKeyColumnValuesToPrimitiveValues(
      request_.lock_id().lock_range_column_values(), schema,
      schema.num_hash_key_columns(), schema.num_range_key_columns()));
  SCHECK(!hashed_components.empty(), InvalidArgument, "No hashed column values provided");
  doc_key_ = DocKey(
      schema, request_.hash_code(), std::move(hashed_components), std::move(range_components));
  encoded_doc_key_ = doc_key_.EncodeAsRefCntPrefix();
  return Status::OK();
}

Status PgsqlLockOperation::GetDocPaths(GetDocPathsMode mode,
    DocPathsToLock *paths, IsolationLevel *level) const {
  // Behaviour of advisory lock is regardless of isolation level.
  *level = IsolationLevel::NON_TRANSACTIONAL;
  // kStrongReadIntents is used for acquring locks on the entire row.
  // It's duplicate with the primary intent for the advisory lock.
  if (mode != GetDocPathsMode::kStrongReadIntents) {
    paths->emplace_back(encoded_doc_key_);
  }
  return Status::OK();
}

std::string PgsqlLockOperation::ToString() const {
  return Format("$0 $1, original request: $2",
                request_.is_lock() ? "LOCK" : "UNLOCK",
                doc_key_.ToString(),
                request_.ShortDebugString());
}

void PgsqlLockOperation::ClearResponse() {
  if (response_) {
    response_->Clear();
  }
}

Result<bool> PgsqlLockOperation::LockExists(const DocOperationApplyData& data) {
  dockv::KeyBytes advisory_lock_key(encoded_doc_key_.as_slice());
  advisory_lock_key.AppendKeyEntryType(dockv::KeyEntryType::kIntentTypeSet);
  advisory_lock_key.AppendIntentTypeSet(GetIntentTypes(IsolationLevel::NON_TRANSACTIONAL));
  dockv::KeyBytes txn_reverse_index_prefix;
  AppendTransactionKeyPrefix(txn_op_context_.transaction_id, &txn_reverse_index_prefix);
  txn_reverse_index_prefix.AppendKeyEntryType(dockv::KeyEntryType::kMaxByte);
  auto reverse_index_upperbound = txn_reverse_index_prefix.AsSlice();
  auto iter = CreateRocksDBIterator(
      data.doc_write_batch->doc_db().intents, &KeyBounds::kNoBounds,
      BloomFilterOptions::Inactive(), rocksdb::kDefaultQueryId, nullptr, &reverse_index_upperbound,
      rocksdb::CacheRestartBlockKeys::kFalse);
  Slice key_prefix = txn_reverse_index_prefix.AsSlice();
  key_prefix.remove_suffix(1);
  iter.Seek(key_prefix);
  bool found = false;
  while (iter.Valid()) {
    if (!iter.key().starts_with(key_prefix)) {
      break;
    }
    if (iter.value().starts_with(advisory_lock_key.AsSlice())) {
      found = true;
      break;
    }
    iter.Next();
  }
  RETURN_NOT_OK(iter.status());
  return found;
}

Status PgsqlLockOperation::Apply(const DocOperationApplyData& data) {
  if (!request_.is_lock() && !VERIFY_RESULT(LockExists(data))) {
    return STATUS_EC_FORMAT(InternalError, TransactionError(TransactionErrorCode::kLockNotFound),
                            "Try to release non-existing lock $0", doc_key_.ToString());
  }
  Slice value(&(dockv::ValueEntryTypeAsChar::kRowLock), 1);
  auto& entry = data.doc_write_batch->AddLock();
  entry.lock.key = encoded_doc_key_.as_slice();
  entry.lock.value = value;
  entry.mode = request_.lock_mode();
  return Status::OK();
}

dockv::IntentTypeSet PgsqlLockOperation::GetIntentTypes(IsolationLevel isolation_level) const {
  switch (request_.lock_mode()) {
    case PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE:
      return dockv::GetIntentTypesForLock(dockv::DocdbLockMode::DOCDB_LOCK_EXCLUSIVE);
    case PgsqlLockRequestPB::PG_LOCK_SHARE:
      return dockv::GetIntentTypesForLock(dockv::DocdbLockMode::DOCDB_LOCK_SHARE);
  }
  FATAL_INVALID_ENUM_VALUE(PgsqlLockRequestPB::PgsqlAdvisoryLockMode, request_.lock_mode());
}

}  // namespace yb::docdb
