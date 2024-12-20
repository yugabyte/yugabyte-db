//--------------------------------------------------------------------------------------------------
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

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/substitute.h"

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

inline void ApplyBound(
    ::yb::LWPgsqlReadRequestPB* req, const std::optional<Bound>& bound, bool is_lower) {
  if (bound) {
    auto* mutable_bound = is_lower ? req->mutable_lower_bound() : req->mutable_upper_bound();
    mutable_bound->dup_key(dockv::PartitionSchema::EncodeMultiColumnHashValue(bound->value));
    mutable_bound->set_is_inclusive(bound->is_inclusive);
  }
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

// Helper class to generalize logic of ybctid's generation for regular and reverse iterators.
class InOperatorYbctidsGenerator {
 public:
  using Ybctids = std::vector<Slice>;

  InOperatorYbctidsGenerator(ThreadSafeArena* arena, dockv::DocKey* doc_key,
                             size_t value_placeholder_idx, Ybctids* ybctids)
      : arena_(*arena),
        doc_key_(*doc_key),
        value_placeholder_(doc_key_.range_group()[value_placeholder_idx]),
        ybctids_(*ybctids) {}

  template <class It>
  void Generate(It it, const It& end) const {
    for (; it != end; ++it) {
      value_placeholder_ = *it;
      ybctids_.push_back(arena_.DupSlice(doc_key_.Encode().AsSlice()));
    }
  }

 private:
  ThreadSafeArena& arena_;
  dockv::DocKey& doc_key_;
  dockv::KeyEntryValue& value_placeholder_;
  Ybctids& ybctids_;
};

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

class SimpleYbctidProvider : public YbctidProvider {
 public:
  explicit SimpleYbctidProvider(std::reference_wrapper<const std::vector<Slice>> ybctids)
      : ybctids_(&ybctids.get()) {}

 private:
  Result<std::optional<YbctidBatch>> Fetch() override {
    if (!ybctids_) {
      return std::nullopt;
    }
    YbctidBatch result{*ybctids_, /* keep_order= */ true};
    ybctids_ = nullptr;
    return result;
  }

  const std::vector<Slice>* ybctids_;
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

void PgDmlRead::SetHashBounds(uint16_t low_bound, uint16_t high_bound) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    secondary_index->SetHashBounds(low_bound, high_bound);
    return;
  }
  read_req_->set_hash_code(low_bound);
  read_req_->set_max_hash_code(high_bound);
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

// Method removes empty primary binds and moves tailing non empty range primary binds
// which are following after empty binds into the 'condition_expr' field.
Status PgDmlRead::ProcessEmptyPrimaryBinds() {
  if (!bind_) {
    // This query does not have any binds.
    read_req_->mutable_partition_column_values()->clear();
    read_req_->mutable_range_column_values()->clear();
    return Status::OK();
  }

  // NOTE: ybctid is a system column and not processed as bind.
  bool miss_partition_columns = false;
  bool has_partition_columns = false;

  // Collecting column indexes that are involved in a tuple
  std::vector<size_t> tuple_col_ids;

  bool preceding_key_column_missed = false;

  for (size_t index = 0; index != bind_->num_hash_key_columns(); ++index) {
    const auto& column = bind_.ColumnForIndex(index);
    auto expr = column.bind_pb();
    auto colid = column.id();
    if (!expr || (!IsForInOperator(*expr) && !column.ValueBound() &&
                  (std::find(tuple_col_ids.begin(), tuple_col_ids.end(), colid) ==
                       tuple_col_ids.end()))) {
      miss_partition_columns = true;
      continue;
    } else {
      has_partition_columns = true;
    }

    if (expr && expr->has_condition()) {
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

  size_t num_bound_range_columns = 0;

  for (auto index = bind_->num_hash_key_columns(); index < bind_->num_key_columns(); ++index) {
    auto& col = bind_.ColumnForIndex(index);
    auto expr = col.bind_pb();
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
      ++num_bound_range_columns;
    }
  }

  auto& range_column_values = *read_req_->mutable_range_column_values();
  while (range_column_values.size() > num_bound_range_columns) {
    range_column_values.pop_back();
  }
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

Status PgDmlRead::InitDocOp(const PgExecParameters* params, bool is_concrete_row_read) {
  std::optional<PgExecParameters> alternative_params;
  if (!is_concrete_row_read && GetRowMarkType(params) == RowMarkType::ROW_MARK_KEYSHARE) {
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
  SetYbctidProvider(std::make_unique<SimpleYbctidProvider>(ybctids));
}

Status PgDmlRead::ANNBindVector(PgExpr* vector) {
  auto vec_options = read_req_->mutable_vector_idx_options();
  return vector->EvalTo(vec_options->mutable_vector());
}

Status PgDmlRead::ANNSetPrefetchSize(int32_t prefetch_size) {
  read_req_->mutable_vector_idx_options()->set_prefetch_size(prefetch_size);
  return Status::OK();
}

Status PgDmlRead::Exec(const PgExecParameters* exec_params) {
  RSTATUS_DCHECK(
      !pg_exec_params_ || pg_exec_params_ == exec_params,
      IllegalState, "Unexpected change of exec params");
  const PgExecParameters* doc_op_init_params = nullptr;
  if (!pg_exec_params_) {
    pg_exec_params_ = exec_params;
    doc_op_init_params = pg_exec_params_;
  }

  SetColumnRefs();

  if (doc_op_ && !ybctid_provider() && IsAllPrimaryKeysBound() &&
      !(read_req_->has_lower_bound() || read_req_->has_upper_bound())) {
    RETURN_NOT_OK(SubstitutePrimaryBindsWithYbctids(doc_op_init_params));
  } else {
    RETURN_NOT_OK(ProcessEmptyPrimaryBinds());
    if (doc_op_) {
      RETURN_NOT_OK(InitDocOp(doc_op_init_params));
    }
  }

  if (!doc_op_) {
    return Status::OK();
  }

  const auto has_ybctid = VERIFY_RESULT(ProcessProvidedYbctids());

  if (!has_ybctid && ybctid_provider()) {
    // No ybctids are provided. Instruct "doc_op_" to abandon the execution and not querying
    // any data from tablet server.
    doc_op_->AbandonExecution();
  } else {
    RSTATUS_DCHECK_EQ(
        VERIFY_RESULT(doc_op_->Execute()),
        RequestSent::kTrue, IllegalState, "YSQL read operation was not sent");
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
    YBCPgStatement handle, size_t n_col_values, PgExpr** col_values, bool for_lower_bound) {
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
    YBCPgStatement handle, int n_col_values, PgExpr **col_values, bool is_inclusive) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->AddRowUpperBound(handle, n_col_values, col_values, is_inclusive);
  }

  auto dockey = VERIFY_RESULT(EncodeRowKeyForBound(handle, n_col_values, col_values, false));

  if (read_req_->has_upper_bound()) {
      dockv::DocKey current_upper_bound_key;
      RETURN_NOT_OK(current_upper_bound_key.DecodeFrom(
                    read_req_->upper_bound().key(),
                    dockv::DocKeyPart::kWholeDocKey,
                    dockv::AllowSpecial::kTrue));

      if (current_upper_bound_key < dockey) {
        return Status::OK();
      }

      if (current_upper_bound_key == dockey) {
          is_inclusive = is_inclusive & read_req_->upper_bound().is_inclusive();
          read_req_->mutable_upper_bound()->set_is_inclusive(is_inclusive);
          return Status::OK();
      }

      // current_upper_bound_key > dockey
  }
  read_req_->mutable_upper_bound()->dup_key(dockey.Encode().AsSlice());
  read_req_->mutable_upper_bound()->set_is_inclusive(is_inclusive);

  return Status::OK();
}

Status PgDmlRead::AddRowLowerBound(
    YBCPgStatement handle, int n_col_values, PgExpr **col_values, bool is_inclusive) {

  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->AddRowLowerBound(handle, n_col_values, col_values, is_inclusive);
  }

  auto dockey = VERIFY_RESULT(EncodeRowKeyForBound(handle, n_col_values, col_values, true));
  if (read_req_->has_lower_bound()) {
      dockv::DocKey current_lower_bound_key;
      RETURN_NOT_OK(current_lower_bound_key.DecodeFrom(
                    read_req_->lower_bound().key(),
                    dockv::DocKeyPart::kWholeDocKey,
                    dockv::AllowSpecial::kTrue));

      if (current_lower_bound_key > dockey) {
        return Status::OK();
      }

      if (current_lower_bound_key == dockey) {
          is_inclusive = is_inclusive & read_req_->lower_bound().is_inclusive();
          read_req_->mutable_lower_bound()->set_is_inclusive(is_inclusive);
          return Status::OK();
      }

      // current_lower_bound_key > dockey
  }
  read_req_->mutable_lower_bound()->dup_key(dockey.Encode().AsSlice());
  read_req_->mutable_lower_bound()->set_is_inclusive(is_inclusive);

  return Status::OK();
}

Status PgDmlRead::SubstitutePrimaryBindsWithYbctids(const PgExecParameters* params) {
  const auto ybctids = VERIFY_RESULT(BuildYbctidsFromPrimaryBinds());
  for (auto& col : bind_.columns()) {
    col.UnbindValue();
  }
  read_req_->mutable_partition_column_values()->clear();
  read_req_->mutable_range_column_values()->clear();
  RETURN_NOT_OK(InitDocOp(params, /* is_concrete_row_read = */ true));
  return UpdateRequestWithYbctids(ybctids);
}

// Function builds vector of ybctids from primary key binds.
// Required precondition that not more than one range key component has the IN operator and all
// other key components are set must be checked by caller code.
Result<std::vector<Slice>> PgDmlRead::BuildYbctidsFromPrimaryBinds() {
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
  std::vector<Slice> ybctids;
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
    ybctids.reserve(provider.size());
    InOperatorYbctidsGenerator generator(&arena(), &doc_key, in_operator_info->placeholder_idx,
                                         &ybctids);
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
    ybctids.push_back(arena().DupSlice(doc_key.Encode().AsSlice()));
  }
  return ybctids;
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
  ApplyBound(read_req_.get(), start, true /* is_lower */);
  ApplyBound(read_req_.get(), end, false /* is_lower */);
}

Status PgDmlRead::BindRange(
    Slice lower_bound, bool lower_bound_inclusive, Slice upper_bound, bool upper_bound_inclusive) {
  // Clean up operations remaining from the previous range's scan
  if (doc_op_) {
    RETURN_NOT_OK(down_cast<PgDocReadOp*>(doc_op_.get())->ResetPgsqlOps());
  }
  if (auto* secondary_index = SecondaryIndex(); secondary_index) {
    secondary_index->RequireReExecution();
    return secondary_index->query().BindRange(
        lower_bound, lower_bound_inclusive, upper_bound, upper_bound_inclusive);
  }
  // Set lower bound
  if (lower_bound.empty()) {
    read_req_->clear_lower_bound();
  } else {
    auto* mutable_bound = read_req_->mutable_lower_bound();
    mutable_bound->dup_key(lower_bound);
    mutable_bound->set_is_inclusive(lower_bound_inclusive);
  }
  // Set upper bound
  if (upper_bound.empty()) {
    read_req_->clear_upper_bound();
  } else {
    auto* mutable_bound = read_req_->mutable_upper_bound();
    mutable_bound->dup_key(upper_bound);
    mutable_bound->set_is_inclusive(upper_bound_inclusive);
  }
  return Status::OK();
}

void PgDmlRead::UpgradeDocOp(PgDocOp::SharedPtr doc_op) {
  CHECK(!original_doc_op_) << "DocOp can be upgraded only once";
  CHECK(doc_op_) << "No DocOp object for upgrade";
  original_doc_op_.swap(doc_op_);
  doc_op_.swap(doc_op);
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
