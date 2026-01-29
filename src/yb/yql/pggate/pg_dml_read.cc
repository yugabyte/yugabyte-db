//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_dml_read.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <boost/container/small_vector.hpp>

#include "yb/common/pg_system_attr.h"
#include "yb/common/ql_datatype.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"

#include "yb/dockv/partition.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/util/range.h"
#include "yb/util/slice.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_select_index.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

#include "ybgate/ybgate_api.h"

namespace yb::pggate {
namespace {

Result<dockv::DocKey> BuildDocKey(
    const dockv::PartitionSchema& partition_schema,
    dockv::KeyEntryValues hashed_components,
    const LWQLValuePB*const* hashed_values,
    dockv::KeyEntryValues range_components = {}) {

  if (!hashed_components.empty()) {
    DCHECK(hashed_values);
    auto hash = VERIFY_RESULT(partition_schema.PgsqlHashColumnCompoundValue(
        boost::make_iterator_range(hashed_values, hashed_values + hashed_components.size())));
    return dockv::DocKey(hash, std::move(hashed_components), std::move(range_components));
  }
  return dockv::DocKey(std::move(range_components));
}

[[nodiscard]] inline bool IsForInOperator(const LWPgsqlExpressionPB& expr) {
  // For IN operator expr->has_condition() returns 'true'.
  return expr.has_condition();
}

[[nodiscard]] bool InOpInversesOperandOrder(SortingType sorting_type, bool is_forward_scan) {
  switch(sorting_type) {
    case SortingType::kNotSpecified: return false;
    case SortingType::kDescending: [[fallthrough]];
    case SortingType::kDescendingNullsLast: return is_forward_scan;
    case SortingType::kAscending: [[fallthrough]];
    case SortingType::kAscendingNullsLast: return !is_forward_scan;
  }
  FATAL_INVALID_ENUM_VALUE(SortingType, sorting_type);
}

using LWQLValuePBContainer = boost::container::small_vector<LWQLValuePB*, 16>;

Result<dockv::KeyEntryValue> GetKeyValue(
    const PgColumn& col, PgExpr* expr,
    LWQLValuePB** ql_value_dest, std::optional<dockv::KeyEntryType> null_type = {}) {
  if (!expr) {
    RSTATUS_DCHECK(null_type, IllegalState, "Null expression is not expected");
    *ql_value_dest = nullptr;
    return dockv::KeyEntryValue(*null_type);
  }
  *ql_value_dest = VERIFY_RESULT(expr->Eval());
  return dockv::KeyEntryValue::FromQLValuePB(**ql_value_dest, col.desc().sorting_type());
}

auto GetKeyValue(
    const PgColumn& col, PgExpr* expr, std::optional<dockv::KeyEntryType> null_type = {}) {
  LWQLValuePB* tmp = nullptr;
  return GetKeyValue(col, expr, &tmp, null_type);
}

using Slices = std::vector<Slice>;

class SimpleYbctidProvider : public YbctidProvider {
 public:
  SimpleYbctidProvider(std::reference_wrapper<const Slices> ybctids, bool keep_order)
      : ybctids_(ybctids.get()), keep_order_(keep_order) {}

  Result<std::optional<YbctidBatch>> Fetch() override {
    if (fetched_) {
      return std::nullopt;
    }
    fetched_ = true;
    return YbctidBatch{ybctids_, keep_order_};
  }

  void Reset() override {
    fetched_ = false;
  }

 private:
  const Slices& ybctids_;
  bool fetched_ = false;
  bool keep_order_;

  DISALLOW_COPY_AND_ASSIGN(SimpleYbctidProvider);
};

struct HoldingYbctidProviderData {
 protected:
  explicit HoldingYbctidProviderData(ThreadSafeArena& arena) : arena_(arena) {}

  Slices ybctids_holder_;
  ThreadSafeArena& arena_;
};

struct HoldingYbctidProvider final : public HoldingYbctidProviderData, public SimpleYbctidProvider {
  explicit HoldingYbctidProvider(ThreadSafeArena& arena, bool keep_order)
      : HoldingYbctidProviderData(arena), SimpleYbctidProvider(ybctids_holder_, keep_order) {}

  void Reserve(size_t capacity) { ybctids_holder_.reserve(capacity); }
  void Append(Slice ybctid) { ybctids_holder_.push_back(arena_.DupSlice(ybctid)); }
};

// Helper class to generalize logic of ybctid's generation for regular and reverse iterators.
class InOperatorYbctidsGenerator {
 public:
  InOperatorYbctidsGenerator(
      dockv::DocKey& doc_key, size_t value_placeholder_idx, HoldingYbctidProvider& ybctids)
      : doc_key_(doc_key), value_placeholder_(doc_key_.range_group()[value_placeholder_idx]),
        ybctids_(ybctids) {}

  template <class It>
  void Generate(It it, const It& end) const {
    for (; it != end; ++it) {
      value_placeholder_ = *it;
      ybctids_.Append(doc_key_.Encode().AsSlice());
    }
  }

 private:
  dockv::DocKey& doc_key_;
  dockv::KeyEntryValue& value_placeholder_;
  HoldingYbctidProvider& ybctids_;

  DISALLOW_COPY_AND_ASSIGN(InOperatorYbctidsGenerator);
};

} // namespace

PgDmlRead::PgDmlRead(const PgSession::ScopedRefPtr& pg_session)
    : PgDml(pg_session) {
}

void PgDmlRead::PrepareBinds() {
  if (!bind_) {
    // This statement doesn't have bindings.
    return;
  }

  for (auto& col : bind_.columns()) {
    col.AllocPrimaryBindPB(read_req_.get());
  }
}

void PgDmlRead::SetForwardScan(bool is_forward_scan) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    secondary_index->SetForwardScan(is_forward_scan);
    return;
  }
  if (!read_req_->has_is_forward_scan()) {
    read_req_->set_is_forward_scan(is_forward_scan);
  } else {
    DCHECK(read_req_->is_forward_scan() == is_forward_scan) << "Cannot change scan direction";
  }
}

void PgDmlRead::SetDistinctPrefixLength(int distinct_prefix_length) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    secondary_index->SetDistinctPrefixLength(distinct_prefix_length);
  } else {
    read_req_->set_prefix_length(distinct_prefix_length);
  }
}

//--------------------------------------------------------------------------------------------------
// DML support.
// TODO(neil) WHERE clause is not yet supported. Revisit this function when it is.

Result<LWPgsqlExpressionPB*> PgDmlRead::AllocColumnBindPB(PgColumn* col, PgExpr* expr) {
  return col->AllocBindPB(read_req_.get(), expr);
}

LWPgsqlExpressionPB* PgDmlRead::AllocColumnBindConditionExprPB(PgColumn* col) {
  return col->AllocBindConditionExprPB(read_req_.get());
}

LWPgsqlExpressionPB* PgDmlRead::AllocColumnAssignPB(PgColumn* col) {
  // SELECT statement should not have an assign expression (SET clause).
  LOG(FATAL) << "Pure virtual function is being called";
  return nullptr;
}

LWPgsqlExpressionPB* PgDmlRead::AllocTargetPB() {
  return read_req_->add_targets();
}

ArenaList<LWPgsqlColRefPB>& PgDmlRead::ColRefPBs() {
  return *read_req_->mutable_col_refs();
}

Status PgDmlRead::AppendColumnRef(PgColumnRef* colref, bool is_for_secondary_index) {
  return PgDml::AppendColumnRef(
      colref, ActualValueForIsForSecondaryIndexArg(is_for_secondary_index));
}

Status PgDmlRead::AppendQual(PgExpr* qual, bool is_for_secondary_index) {
  if (ActualValueForIsForSecondaryIndexArg(is_for_secondary_index)) {
    return DCHECK_NOTNULL(SecondaryIndexQuery())->AppendQual(
        qual, /* is_for_secondary_index= */ false);
  }

  auto version = yb_major_version_upgrade_compatibility > 0
      ? yb_major_version_upgrade_compatibility
      : YbgGetPgVersion();
  read_req_->set_expression_serialization_version(version);

  // Populate the expr_pb with data from the qual expression.
  // Side effect of PrepareForRead is to call PrepareColumnForRead on "this" being passed in
  // for any column reference found in the expression. However, the serialized Postgres expressions,
  // the only kind of Postgres expressions supported as quals, can not be searched.
  // Their column references should be explicitly appended with AppendColumnRef()
  return qual->PrepareForRead(this, read_req_->add_where_clauses());
}

//--------------------------------------------------------------------------------------------------
// RESULT SET SUPPORT.
// For now, selected expressions are just a list of column names (ref).
//   SELECT column_l, column_m, column_n FROM ...

void PgDmlRead::SetColumnRefs() {
  read_req_->set_is_aggregate(has_aggregate_targets());
  // Populate column references in the read request
  ColRefsToPB();
  // Compatibility: set column ids in a form that is expected by legacy nodes
  ColumnRefsToPB(read_req_->mutable_column_refs());
}

// Method normalizes the primary binds: partition_column_values (hash) and
// range_column_values (range) on the request to the shape supported by DocDB.
// Postgres may bind the key to any of the following: scalar value (equality condition),
// tuple of values (IN condition), tuple of tuples (ROW IN condition).
// DocDB supports only scalar value, so other variants need to be transformed.
// There are two available transformations for the primary binds.
// One is to move the condition into condition_expr as an operand of the top level AND operator.
// Other is to build permutations: if there're multiple IN and equality conditions like these:
//   k1 IN (a, b, c)
//   k2 = z
//   ROW(k3, k4) IN ((1, 11), (2, 12))
// we can make all possible combinations of one value from each condition, like these:
//   k1 = a AND k2 = z AND k3 = 1 AND k4 = 11
//   k1 = b AND k2 = z AND k3 = 1 AND k4 = 11
//   k1 = c AND k2 = z AND k3 = 1 AND k4 = 11
//   k1 = a AND k2 = z AND k3 = 2 AND k4 = 12
//   k1 = b AND k2 = z AND k3 = 2 AND k4 = 12
//   k1 = c AND k2 = z AND k3 = 2 AND k4 = 12
// put those sets into separate requests, combine their results to make result equivalent to the
// original condition.
// The permutations transformation is currently used if there are IN conditions on the hash columns
// and for merge sort. While the permutations transformation occurs later in the execution process,
// the PgDmlRead::ProcessEmptyPrimaryBinds must keep relevant original conditions in
// partition_column_values and range_column_values to set up the permutations transformation.
// Irrelevant condition must be moved into condition_expr regardless.
// TODO revisit to find out if we can combine the parts of such split logic.
// There are couple more important limitations enforced here.
// DocDB does not support partial condition on the hash columns. It is an error if some, but not
// all the hash columns are bound.
// DocDB takes values in the range_column_values as the prefix of the range key columns, no null
// values allowed. So if one of the key columns are not bound, all conditions on following columns
// must be moved to the condition_expr, even if they are simple scalars.
Status PgDmlRead::ProcessEmptyPrimaryBinds() {
  if (!bind_) {
    // This query does not have any binds.
    read_req_->mutable_partition_column_values()->clear();
    read_req_->mutable_range_column_values()->clear();
    return Status::OK();
  }

  VLOG_WITH_FUNC(3) << "Request before: " << read_req_->ShortDebugString();
  // NOTE: ybctid is a system column and not processed as bind.
  bool miss_partition_columns = false;
  bool has_partition_columns = false;

  // Collecting column indexes that are involved in a tuple
  std::vector<size_t> tuple_col_ids;

  bool preceding_key_column_missed = false;

  for (size_t index = 0; index != bind_->num_hash_key_columns(); ++index) {
    const auto& col = bind_.ColumnForIndex(index);
    auto expr = col.bind_pb();
    auto colid = col.id();
    if (!expr || (!IsForInOperator(*expr) && !col.ValueBound() &&
                  (std::find(tuple_col_ids.begin(), tuple_col_ids.end(), colid) ==
                       tuple_col_ids.end()))) {
      miss_partition_columns = true;
      continue;
    } else {
      has_partition_columns = true;
    }

    if (IsForInOperator(*expr)) {
      // Move any range column binds into the 'condition_expr' field if
      // we are batching hash columns.
      preceding_key_column_missed = pg_session_->IsHashBatchingEnabled();
      const auto& lhs = *expr->condition().operands().begin();
      if (lhs.has_tuple()) {
        const auto& tuple = lhs.tuple();
        for (const auto& elem : tuple.elems()) {
          tuple_col_ids.push_back(elem.column_id());
        }
      }
    }
  }

  SCHECK(!has_partition_columns || !miss_partition_columns, InvalidArgument,
      "Partition key must be fully specified");

  if (miss_partition_columns) {
    VLOG(1) << "Full scan is needed";
    read_req_->mutable_partition_column_values()->clear();

    // Move all range column binds (if any) into the 'condition_expr' field.
    preceding_key_column_missed = true;
  }

  auto range_column_values_it = read_req_->mutable_range_column_values()->begin();
  for (auto index = bind_->num_hash_key_columns(); index < bind_->num_key_columns(); ++index) {
    auto& col = bind_.ColumnForIndex(index);
    auto expr = col.bind_pb();
    auto merge_sort_col_type = IsMergeSortColumn(index);
    if (merge_sort_col_type == MergeSortColumnType::kStreamKey) {
      DCHECK(expr && (expr->has_value() || IsForInOperator(*expr)));
      preceding_key_column_missed = true;
      ++range_column_values_it;
      continue;
    } else if (merge_sort_col_type == MergeSortColumnType::kSortKey) {
      preceding_key_column_missed = true;
    }
    if (expr && IsForInOperator(*expr)) {
      preceding_key_column_missed = true;
      RETURN_NOT_OK(col.MoveBoundKeyInOperator(read_req_.get()));
    } else if (!col.ValueBound()) {
      preceding_key_column_missed = true;
    } else if (preceding_key_column_missed) {
      // Move current bind into the 'condition_expr' field.
      auto* condition_expr_pb = AllocColumnBindConditionExprPB(&col);
      condition_expr_pb->mutable_condition()->set_op(QL_OP_EQUAL);

      auto op1_pb = condition_expr_pb->mutable_condition()->add_operands();
      auto op2_pb = condition_expr_pb->mutable_condition()->add_operands();

      op1_pb->set_column_id(col.id());

      col.MoveBoundValueTo(op2_pb);
    } else {
      ++range_column_values_it;
      continue;
    }
    range_column_values_it =
        read_req_->mutable_range_column_values()->erase(range_column_values_it);
  }

  VLOG_WITH_FUNC(3) << "Request after: " << read_req_->ShortDebugString();
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

bool PgDmlRead::IsConcreteRowRead() const {
  // Operation reads a concrete row at least one of the following conditions is met:
  // - ybctid is explicitly bound
  // - ybctid is used implicitly by using secondary index
  // - all hash and range key components are bound (Note: each key component can be bound only once)
  return bind_ &&
         (ybctid_bind_ ||
          ybctid_provider() ||
          (bind_->num_key_columns() ==
              static_cast<size_t>(read_req_->partition_column_values().size() +
                                  read_req_->range_column_values().size())));
}

Status PgDmlRead::InitDocOp(const YbcPgExecParameters* params) {
  std::optional<YbcPgExecParameters> alternative_params;
  if (!IsConcreteRowRead() && GetRowMarkType(params) == RowMarkType::ROW_MARK_KEYSHARE) {
    // ROW_MARK_KEYSHARE creates a weak read intent on DocDB side. As a result it is only
    // applicable when the read operation reads a concrete row (by using ybctid or by specifying
    // all primary key columns). In case some columns of the primary key are not specified,
    // a strong read intent is required to prevent rows from being deleted by another
    // transaction. For this purpose ROW_MARK_KEYSHARE must be replaced with ROW_MARK_SHARE.
    alternative_params.emplace(*params);
    alternative_params->rowmark = RowMarkType::ROW_MARK_SHARE;
    params = &*alternative_params;
  }
  return doc_op_->ExecuteInit(params);
}

void PgDmlRead::SetRequestedYbctids(std::reference_wrapper<const std::vector<Slice>> ybctids) {
  SetYbctidProvider(std::make_unique<SimpleYbctidProvider>(ybctids, /* keep_order */ false));
}

void PgDmlRead::SetRequestedYbctids(const YbctidGenerator& generator) {
  auto ybctid_holder = std::make_unique<HoldingYbctidProvider>(arena(), false);
  ybctid_holder->Reserve(generator.capacity);
  while (true) {
    const auto ybctid = generator.next();
    if (ybctid.empty()) {
      break;
    }
    ybctid_holder->Append(ybctid);
  }
  SetYbctidProvider(std::move(ybctid_holder));
}

Status PgDmlRead::ANNBindVector(PgExpr* vector) {
  auto vec_options = read_req_->mutable_vector_idx_options();
  return vector->EvalTo(vec_options->mutable_vector());
}

Status PgDmlRead::ANNSetPrefetchSize(int32_t prefetch_size) {
  read_req_->mutable_vector_idx_options()->set_prefetch_size(prefetch_size);
  return Status::OK();
}

Status PgDmlRead::HnswSetReadOptions(int ef_search) {
  read_req_->mutable_vector_idx_options()->mutable_hnsw_options()->set_ef_search(ef_search);
  return Status::OK();
}

Status PgDmlRead::Exec(const YbcPgExecParameters* exec_params) {
  RSTATUS_DCHECK(
      !pg_exec_params_ || pg_exec_params_ == exec_params,
      IllegalState, "Unexpected change of exec params");
  const YbcPgExecParameters* doc_op_init_params = nullptr;
  if (!pg_exec_params_) {
    pg_exec_params_ = exec_params;
    doc_op_init_params = pg_exec_params_;
  }

  SetColumnRefs();

  if (doc_op_ && !ybctid_provider() && IsAllPrimaryKeysBound()) {
    RETURN_NOT_OK(SubstitutePrimaryBindsWithYbctids());
  } else if (!primary_binds_processed_) {
    RETURN_NOT_OK(ProcessEmptyPrimaryBinds());
    primary_binds_processed_ = true;
  }

  if (!doc_op_) {
    return Status::OK();
  }

  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    RETURN_NOT_OK(secondary_index->AddBaseYbctidTarget());
  }

  RETURN_NOT_OK(InitDocOp(doc_op_init_params));

  if (targets_) {
    doc_op_->SetFetchedTargets(targets_);
  }
  if (merge_sort_keys_) {
    // Create requests for the merge streams
    if (!VERIFY_RESULT(doc_op_->PopulateMergeStreams(merge_sort_keys_))) {
      doc_op_->AbandonExecution();
      return Status::OK();
    }
  }

  const auto has_ybctid = VERIFY_RESULT(ProcessProvidedYbctids());

  if (!has_ybctid && ybctid_provider()) {
    // No ybctids are provided. Instruct "doc_op_" to abandon the execution and not querying
    // any data from tablet server.
    doc_op_->AbandonExecution();
  } else if (VERIFY_RESULT(doc_op_->Execute()) != RequestSent::kTrue) {
    // Requests weren't sent, there is no data to fetch
    doc_op_->AbandonExecution();
  }

  return Status::OK();
}

Status PgDmlRead::BindColumnCondBetween(
    int attr_num, PgExpr* attr_value, bool start_inclusive,
    PgExpr* attr_value_end, bool end_inclusive) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->BindColumnCondBetween(
        attr_num, attr_value, start_inclusive, attr_value_end, end_inclusive);
  }

  DCHECK(attr_num != static_cast<int>(PgSystemAttrNum::kYBTupleId))
      << "Operator BETWEEN cannot be applied to ROWID";

  // Find column.
  auto& col = VERIFY_RESULT_REF(bind_.ColumnForAttr(attr_num));

  // Check datatype.
  if (attr_value) {
    SCHECK_EQ(col.internal_type(), attr_value->internal_type(), Corruption,
              "Attribute value type does not match column type");
  }

  if (attr_value_end) {
    SCHECK_EQ(col.internal_type(), attr_value_end->internal_type(), Corruption,
              "Attribute value type does not match column type");
  }

  CHECK(!col.is_partition()) << "This method cannot be used for binding partition column!";

  // Alloc the protobuf.
  auto* condition_pb = AllocColumnBindConditionExprPB(&col)->mutable_condition();

  if (attr_value) {
    if (attr_value_end) {
      condition_pb->set_op(QL_OP_BETWEEN);

      auto* op1_pb = condition_pb->add_operands();
      auto* op2_pb = condition_pb->add_operands();
      auto* op3_pb = condition_pb->add_operands();

      op1_pb->set_column_id(col.id());

      RETURN_NOT_OK(attr_value->EvalTo(op2_pb));
      RETURN_NOT_OK(attr_value_end->EvalTo(op3_pb));

      if (yb_pushdown_strict_inequality) {
        auto* op4_pb = condition_pb->add_operands();
        auto* op5_pb = condition_pb->add_operands();
        op4_pb->mutable_value()->set_bool_value(start_inclusive);
        op5_pb->mutable_value()->set_bool_value(end_inclusive);
      }
    } else {
      condition_pb->set_op(start_inclusive ? QL_OP_GREATER_THAN_EQUAL : QL_OP_GREATER_THAN);

      auto* op1_pb = condition_pb->add_operands();
      auto* op2_pb = condition_pb->add_operands();

      op1_pb->set_column_id(col.id());

      RETURN_NOT_OK(attr_value->EvalTo(op2_pb));
    }
  } else {
    if (attr_value_end) {
      condition_pb->set_op(end_inclusive ? QL_OP_LESS_THAN_EQUAL : QL_OP_LESS_THAN);

      auto* op1_pb = condition_pb->add_operands();
      auto* op2_pb = condition_pb->add_operands();

      op1_pb->set_column_id(col.id());

      RETURN_NOT_OK(attr_value_end->EvalTo(op2_pb));
    } else {
      // Unreachable.
    }
  }

  return Status::OK();
}

Status PgDmlRead::BindColumnCondIn(PgExpr* lhs, int n_attr_values, PgExpr** attr_values) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->BindColumnCondIn(lhs, n_attr_values, attr_values);
  }

  auto cols = VERIFY_RESULT(lhs->GetColumns(&bind_));
  for (const PgColumn& col : cols) {
    SCHECK(col.attr_num() != static_cast<int>(PgSystemAttrNum::kYBTupleId),
           InvalidArgument,
           "Operator IN cannot be applied to ROWID");
  }

  // Check datatype.
  // TODO(neil) Current code combine TEXT and BINARY datatypes into ONE representation.  Once that
  // is fixed, we can remove the special if() check for BINARY type.
  for (int i = 0; i < n_attr_values; i++) {
    if (attr_values[i]) {
      auto vals = attr_values[i]->Unpack();
      auto curr_val_it = vals.begin();
      for (const PgColumn& curr_col : cols) {
        const PgExpr& curr_val = *curr_val_it++;

        const auto curr_col_type = curr_col.internal_type();
        if (curr_col_type == InternalType::kBinaryValue) {
            continue;
        }
        SCHECK_EQ(
            curr_col_type, curr_val.internal_type(), Corruption,
            "Attribute value type does not match column type");
      }
    }
  }

  for (const PgColumn& curr_col : cols) {
    // Check primary column bindings
    if (curr_col.is_primary() && curr_col.ValueBound()) {
      LOG(DFATAL) << Format("Column $0 is already bound to another value", curr_col.attr_num());
    }
  }

  // Find column.
  // Note that in the case that we are dealing with a tuple IN,
  // we only bind this condition to the first column in the IN. The nature of that
  // column (hash or range) will decide how this tuple IN condition will be processed.
  PgColumn& col = cols.front();
  bool col_is_primary = col.is_primary();

  if (col_is_primary) {
    // Alloc the protobuf.
    auto* bind_pb = col.bind_pb();
    if (!bind_pb) {
      bind_pb = VERIFY_RESULT(AllocColumnBindPB(&col, nullptr));
    }

    bind_pb->mutable_condition()->set_op(QL_OP_IN);
    auto lhs_bind = bind_pb->mutable_condition()->add_operands();

    RETURN_NOT_OK(lhs->PrepareForRead(this, lhs_bind));

    RETURN_NOT_OK(col.SetSubExprs(this, attr_values, n_attr_values));
  } else {
    // Alloc the protobuf.
    auto* condition_expr_pb = AllocColumnBindConditionExprPB(&col);

    condition_expr_pb->mutable_condition()->set_op(QL_OP_IN);

    auto op1_pb = condition_expr_pb->mutable_condition()->add_operands();
    auto op2_pb = condition_expr_pb->mutable_condition()->add_operands();

    op1_pb->set_column_id(col.id());

    for (int i = 0; i < n_attr_values; i++) {
      // Link the given expression "attr_value" with the allocated protobuf.
      // Note that except for constants and place_holders, all other expressions can be setup
      // just one time during prepare.
      // Examples:
      // - Bind values for primary columns in where clause.
      //     WHERE hash = ?
      // - Bind values for a column in INSERT statement.
      //     INSERT INTO a_table(hash, key, col) VALUES(?, ?, ?)

      if (attr_values[i]) {
        RETURN_NOT_OK(attr_values[i]->EvalTo(
            op2_pb->mutable_value()->mutable_list_value()->add_elems()));
      }
    }
  }
  return Status::OK();
}

Status PgDmlRead::BindColumnCondIsNotNull(int attr_num) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->BindColumnCondIsNotNull(attr_num);
  }

  DCHECK(attr_num != static_cast<int>(PgSystemAttrNum::kYBTupleId))
      << "Operator IS NOT NULL cannot be applied to ROWID in DocDB";

  // Find column.
  auto& col = VERIFY_RESULT_REF(bind_.ColumnForAttr(attr_num));

  CHECK(!col.is_partition()) << "This method cannot be used for binding partition column!";

  // Alloc the protobuf.
  auto* condition_expr_pb = AllocColumnBindConditionExprPB(&col);
  condition_expr_pb->mutable_condition()->set_op(QL_OP_IS_NOT_NULL);
  condition_expr_pb->mutable_condition()->add_operands()->set_column_id(col.id());
  return Status::OK();
}

Result<dockv::DocKey> PgDmlRead::EncodeRowKeyForBound(
    YbcPgStatement handle, size_t n_col_values, PgExpr** col_values, bool for_lower_bound) {
  const auto num_hash_key_columns = bind_->num_hash_key_columns();
  dockv::KeyEntryValues hashed_components;
  hashed_components.reserve(num_hash_key_columns);
  LWQLValuePBContainer hashed_values(num_hash_key_columns);
  size_t i = 0;
  for (; i < num_hash_key_columns; ++i) {
    hashed_components.push_back(VERIFY_RESULT(GetKeyValue(
        bind_.ColumnForIndex(i), col_values[i], &hashed_values[i])));
  }

  dockv::KeyEntryValues range_components;
  n_col_values = std::max(std::min(n_col_values, bind_->num_key_columns()), num_hash_key_columns);
  range_components.reserve(n_col_values - num_hash_key_columns);
  const auto null_type = for_lower_bound
      ? dockv::KeyEntryType::kLowest : dockv::KeyEntryType::kHighest;
  for (; i < n_col_values; ++i) {
    range_components.push_back(VERIFY_RESULT(GetKeyValue(
        bind_.ColumnForIndex(i), col_values[i], null_type)));
  }

  return BuildDocKey(
      bind_->partition_schema(), std::move(hashed_components), hashed_values.data(),
      std::move(range_components));
}

Status PgDmlRead::AddRowUpperBound(
    YbcPgStatement handle, int n_col_values, PgExpr **col_values, bool is_inclusive) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->AddRowUpperBound(handle, n_col_values, col_values, is_inclusive);
  }

  auto dockey = VERIFY_RESULT(EncodeRowKeyForBound(handle, n_col_values, col_values, false));
  ApplyUpperBound(*read_req_, dockey.Encode().AsSlice(), is_inclusive);
  return Status::OK();
}

Status PgDmlRead::AddRowLowerBound(
    YbcPgStatement handle, int n_col_values, PgExpr **col_values, bool is_inclusive) {

  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->AddRowLowerBound(handle, n_col_values, col_values, is_inclusive);
  }

  auto dockey = VERIFY_RESULT(EncodeRowKeyForBound(handle, n_col_values, col_values, true));
  ApplyLowerBound(*read_req_, dockey.Encode().AsSlice(), is_inclusive);
  return Status::OK();
}

Status PgDmlRead::SetMergeSortKeys(int num_keys, const YbcSortKey* sort_keys) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->SetMergeSortKeys(num_keys, sort_keys);
  }

  DCHECK(!merge_sort_keys_) << "Merge sort keys are already set";
  merge_sort_keys_ = std::make_shared<MergeSortKeys>(sort_keys, sort_keys + num_keys);
  return Status::OK();
}

Status PgDmlRead::SubstitutePrimaryBindsWithYbctids() {
  SetYbctidProvider(VERIFY_RESULT(BuildYbctidsFromPrimaryBinds()));
  for (auto& col : bind_.columns()) {
    col.UnbindValue();
  }
  read_req_->mutable_partition_column_values()->clear();
  read_req_->mutable_range_column_values()->clear();
  return Status::OK();
}

// Function builds vector of ybctids from primary key binds.
// Required precondition that not more than one range key component has the IN operator and all
// other key components are set must be checked by caller code.
Result<std::unique_ptr<YbctidProvider>> PgDmlRead::BuildYbctidsFromPrimaryBinds() {
  auto num_hash_key_columns = bind_->num_hash_key_columns();
  LWQLValuePBContainer hashed_values(num_hash_key_columns);
  dockv::KeyEntryValues hashed_components;
  hashed_components.reserve(num_hash_key_columns);
  for (size_t i = 0; i < num_hash_key_columns; ++i) {
    auto& col = bind_.ColumnForIndex(i);
    hashed_components.push_back(VERIFY_RESULT(col.BuildKeyColumnValue(&hashed_values[i])));
  }

  auto doc_key = VERIFY_RESULT(BuildDocKey(
      bind_->partition_schema(), std::move(hashed_components), hashed_values.data()));
  auto& range_components = doc_key.range_group();
  range_components.reserve(bind_->num_key_columns() - num_hash_key_columns);
  struct InOperatorInfo {
    InOperatorInfo(const PgColumn& column_, size_t placeholder_idx_)
        : column(column_), placeholder_idx(placeholder_idx_) {}

    const PgColumn& column;
    const size_t placeholder_idx;
  };

  std::optional<InOperatorInfo> in_operator_info;
  auto ybctid_holder = std::make_unique<HoldingYbctidProvider>(
      arena(), read_req_->has_is_forward_scan());
  for (auto i = num_hash_key_columns; i < bind_->num_key_columns(); ++i) {
    auto& col = bind_.ColumnForIndex(i);
    auto& expr = *col.bind_pb();
    if (IsForInOperator(expr)) {
      DCHECK(!in_operator_info);
      const auto value_placeholder_idx = range_components.size();
      range_components.emplace_back();
      in_operator_info.emplace(col, value_placeholder_idx);
    } else {
      range_components.push_back(VERIFY_RESULT(col.BuildKeyColumnValue()));
    }
  }
  if (in_operator_info) {
    // Form ybctid for each argument in the IN operator.
    const auto& column = in_operator_info->column;
    const auto provider = column.BuildSubExprKeyColumnValueProvider();
    ybctid_holder->Reserve(provider.size());
    InOperatorYbctidsGenerator generator(
        doc_key, in_operator_info->placeholder_idx, *ybctid_holder);
    // In some cases scan are sensitive to key values order. On DocDB side IN operator processes
    // based on column sort order and scan direction. It is necessary to preserve same order for
    // the constructed ybctids.
    auto begin = provider.cbegin();
    auto end = provider.cend();
    if (InOpInversesOperandOrder(column.desc().sorting_type(), read_req_->is_forward_scan())) {
      generator.Generate(boost::make_reverse_iterator(end), boost::make_reverse_iterator(begin));
    } else {
      generator.Generate(begin, end);
    }
  } else {
    ybctid_holder->Append(doc_key.Encode().AsSlice());
  }
  return ybctid_holder;
}

// Returns true in case not more than one range key component has the IN operator
// and all other key components are set.
bool PgDmlRead::IsAllPrimaryKeysBound() const {
  if (!bind_) {
    return false;
  }

  int range_components_in_operators_remain = 1;

  for (size_t i = 0; i < bind_->num_key_columns(); ++i) {
    auto& col = bind_.ColumnForIndex(i);
    const auto* expr = col.bind_pb();

    if (IsForInOperator(*expr)) {
      if ((i < bind_->num_hash_key_columns()) || (--range_components_in_operators_remain < 0)) {
        // unsupported IN operator
        return false;
      }
    } else if (!col.ValueBound()) {
      // missing key component found
      return false;
    }
  }
  return true;
}

void PgDmlRead::BindHashCode(const std::optional<Bound>& start, const std::optional<Bound>& end) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    secondary_index->BindHashCode(start, end);
    return;
  }

  if (start) {
    const auto& lower_bound = HashCodeToDocKeyBound(
        bind_->schema(), start->value, start->is_inclusive, /* is_lower =*/true);
    ApplyLowerBound(*read_req_, lower_bound.Encode().AsSlice(), /* is_inclusive =*/false);
  }

  if (end) {
    const auto& upper_bound = HashCodeToDocKeyBound(
        bind_->schema(), end->value, end->is_inclusive, /* is_lower =*/false);
    ApplyUpperBound(*read_req_, upper_bound.Encode().AsSlice(), /* is_inclusive =*/false);
  }
}

Status PgDmlRead::BindRange(
    Slice lower_bound, bool lower_bound_inclusive, Slice upper_bound, bool upper_bound_inclusive) {
  // Clean up operations remaining from the previous range's scan
  if (doc_op_) {
    RETURN_NOT_OK(down_cast<PgDocReadOp*>(doc_op_.get())->ResetPgsqlOps());
  }
  if (auto* provider = ybctid_provider(); provider) {
    provider->Reset();
  }
  if (auto* secondary_index = SecondaryIndex(); secondary_index) {
    secondary_index->RequireReExecution();
    return secondary_index->query().BindRange(
        lower_bound, lower_bound_inclusive, upper_bound, upper_bound_inclusive);
  }
  // Override the lower bound
  read_req_->clear_lower_bound();
  ApplyLowerBound(*read_req_, lower_bound, lower_bound_inclusive);

  // Override the upper bound
  read_req_->clear_upper_bound();
  ApplyUpperBound(*read_req_, upper_bound, upper_bound_inclusive);

  return Status::OK();
}

void PgDmlRead::BindBounds(
    const Slice lower_bound, bool lower_bound_inclusive, const Slice upper_bound,
    bool upper_bound_inclusive) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->BindBounds(
        lower_bound, lower_bound_inclusive, upper_bound, upper_bound_inclusive);
  }
  ApplyBounds(*read_req_, lower_bound, lower_bound_inclusive, upper_bound, upper_bound_inclusive);
}

void PgDmlRead::UpgradeDocOp(PgDocOp::SharedPtr doc_op) {
  CHECK(!original_doc_op_) << "DocOp can be upgraded only once";
  CHECK(doc_op_) << "No DocOp object for upgrade";
  original_doc_op_.swap(doc_op_);
  doc_op_.swap(doc_op);
  if (targets_) {
    doc_op_->SetFetchedTargets(targets_);
  }
}

bool PgDmlRead::IsReadFromYsqlCatalog() const {
  return target_->schema().table_properties().is_ysql_catalog_table();
}

bool PgDmlRead::IsIndexOrderedScan() const {
  auto* secondary_index = SecondaryIndexQuery();
  return secondary_index && !secondary_index->IsAllPrimaryKeysBound();
}

bool PgDmlRead::ActualValueForIsForSecondaryIndexArg(bool is_for_secondary_index) const {
  // The usage of secondary index of current object is only required in case current object itself
  // is not intended to read from secondary index. The PgSelectIndex class is used to read from
  // index only (secondary or primary).
  return is_for_secondary_index && !IsPgSelectIndex();
}

}  // namespace yb::pggate
