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

#include "yb/client/yb_op.h"

#include "yb/common/partition.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"

#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_select_index.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/util/pg_doc_data.h"

namespace yb {
namespace pggate {

namespace {

template<class Key, class Value, class CompatibleKey>
auto Find(const boost::unordered_map<Key, Value>& map, const CompatibleKey& key) {
  return map.find(key, boost::hash<CompatibleKey>(), std::equal_to<CompatibleKey>());
}

using DocKeyBuilder = std::function<docdb::DocKey(const vector<docdb::PrimitiveValue>&)>;

Result<DocKeyBuilder> CreateDocKeyBuilder(
    const vector<docdb::PrimitiveValue>& hashed_components,
    const google::protobuf::RepeatedPtrField<PgsqlExpressionPB>& hashed_values,
    const PartitionSchema& partition_schema) {

  if (hashed_values.empty()) {
    return [](const auto& range_components) {
      return docdb::DocKey(range_components);
    };
  }

  string partition_key;
  RETURN_NOT_OK(partition_schema.EncodeKey(hashed_values, &partition_key));
  const auto hash = PartitionSchema::DecodeMultiColumnHashValue(partition_key);

  return [hash, &hashed_components](const auto& range_components) {
    return docdb::DocKey(hash, hashed_components, range_components);
  };
}

} // namespace

using std::make_shared;

//--------------------------------------------------------------------------------------------------
// PgDmlRead
//--------------------------------------------------------------------------------------------------

PgDmlRead::PgDmlRead(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id,
                     const PgObjectId& index_id, const PgPrepareParameters *prepare_params)
    : PgDml(std::move(pg_session), table_id, index_id, prepare_params) {
}

PgDmlRead::~PgDmlRead() {
}

void PgDmlRead::PrepareBinds() {
  if (!bind_) {
    // This statement doesn't have bindings.
    return;
  }

  for (auto& col : bind_.columns()) {
    col.AllocPrimaryBindPB(read_req_);
  }
}

void PgDmlRead::SetForwardScan(const bool is_forward_scan) {
  if (secondary_index_query_) {
    return secondary_index_query_->SetForwardScan(is_forward_scan);
  }
  read_req_->set_is_forward_scan(is_forward_scan);
}

//--------------------------------------------------------------------------------------------------
// DML support.
// TODO(neil) WHERE clause is not yet supported. Revisit this function when it is.

PgsqlExpressionPB *PgDmlRead::AllocColumnBindPB(PgColumn *col) {
  return col->AllocBindPB(read_req_);
}

PgsqlExpressionPB *PgDmlRead::AllocColumnBindConditionExprPB(PgColumn *col) {
  return col->AllocBindConditionExprPB(read_req_);
}

PgsqlExpressionPB *PgDmlRead::AllocColumnAssignPB(PgColumn *col) {
  // SELECT statement should not have an assign expression (SET clause).
  LOG(FATAL) << "Pure virtual function is being call";
  return nullptr;
}

PgsqlExpressionPB *PgDmlRead::AllocTargetPB() {
  return read_req_->add_targets();
}

//--------------------------------------------------------------------------------------------------
// RESULT SET SUPPORT.
// For now, selected expressions are just a list of column names (ref).
//   SELECT column_l, column_m, column_n FROM ...

void PgDmlRead::SetColumnRefs() {
  if (secondary_index_query_) {
    DCHECK(!has_aggregate_targets()) << "Aggregate pushdown should not happen with index";
  }
  read_req_->set_is_aggregate(has_aggregate_targets());
  ColumnRefsToPB(read_req_->mutable_column_refs());
}

// Method removes empty primary binds and moves tailing non empty range primary binds
// which are following after empty binds into the 'condition_expr' field.
Status PgDmlRead::ProcessEmptyPrimaryBinds() {
  if (!bind_) {
    // This query does not have any binds.
    read_req_->clear_partition_column_values();
    read_req_->clear_range_column_values();
    return Status::OK();
  }

  // NOTE: ybctid is a system column and not processed as bind.
  bool miss_partition_columns = false;
  bool has_partition_columns = false;

  const auto hash_columns_begin = bind_.columns().begin();
  const auto hash_columns_end = hash_columns_begin + bind_->num_hash_key_columns();
  for (auto it = hash_columns_begin; it != hash_columns_end; ++it) {
    auto expr = it->bind_pb();
    // For IN clause expr->has_condition() returns 'true'.
    if (!expr || (!expr->has_condition() && (expr_binds_.find(expr) == expr_binds_.end()))) {
      miss_partition_columns = true;
    } else {
      has_partition_columns = true;
    }
  }

  SCHECK(!has_partition_columns || !miss_partition_columns, InvalidArgument,
      "Partition key must be fully specified");

  bool preceding_key_column_missed = false;

  if (miss_partition_columns) {
    VLOG(1) << "Full scan is needed";
    read_req_->clear_partition_column_values();
    // Reset binding of columns whose values has been deleted.
    std::for_each(
        hash_columns_begin,
        hash_columns_end,
        [](auto& column) { column.ResetBindPB(); });

    // Move all range column binds (if any) into the 'condition_expr' field.
    preceding_key_column_missed = true;
  }

  int num_bound_range_columns = 0;

  const auto range_columns_end = bind_.columns().begin() + bind_->num_columns();
  const auto range_columns_begin = bind_.columns().begin() + bind_->num_hash_key_columns();
  for (auto it = range_columns_begin; it < range_columns_end; ++it) {
    auto& col = *it;
    auto expr = col.bind_pb();
    const auto expr_bind = expr ? expr_binds_.find(expr) : expr_binds_.end();
    // For IN clause expr->has_condition() returns 'true'.
    if (expr && expr->has_condition()) {
      preceding_key_column_missed = true;
      RETURN_NOT_OK(MoveBoundKeyInOperator(&col, expr->condition()));
    } else if (expr_bind == expr_binds_.end()) {
      preceding_key_column_missed = true;
    } else {
      if (preceding_key_column_missed) {
        // Move current bind into the 'condition_expr' field.
        PgsqlExpressionPB* condition_expr_pb = AllocColumnBindConditionExprPB(&col);
        condition_expr_pb->mutable_condition()->set_op(QL_OP_EQUAL);

        auto op1_pb = condition_expr_pb->mutable_condition()->add_operands();
        auto op2_pb = condition_expr_pb->mutable_condition()->add_operands();

        op1_pb->set_column_id(col.id());

        auto attr_value = expr_bind->second;
        RETURN_NOT_OK(attr_value->Eval(op2_pb->mutable_value()));
        expr_binds_.erase(expr_bind);
      } else {
        ++num_bound_range_columns;
      }
    }
  }

  auto& range_column_values = *read_req_->mutable_range_column_values();
  range_column_values.DeleteSubrange(
      num_bound_range_columns, range_column_values.size() - num_bound_range_columns);
  // Reset binding of columns whose values has been deleted.
  std::for_each(
      range_columns_begin + num_bound_range_columns,
      range_columns_end,
      [](auto& column) { column.ResetBindPB(); });
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

bool PgDmlRead::IsConcreteRowRead() const {
  // Operation reads a concrete row at least one of the following conditions is met:
  // - ybctid is explicitly bound
  // - ybctid is used implicitly by using secondary index
  // - all hash and range key components are bound (Note: each key component can be bound only once)
  return doc_op_ && bind_ &&
         (ybctid_bind_ ||
          (secondary_index_query_ && secondary_index_query_->has_doc_op()) ||
          (bind_->num_key_columns() ==
              (read_req_->partition_column_values_size() + read_req_->range_column_values_size())));
}

Status PgDmlRead::Exec(const PgExecParameters *exec_params) {
  // Save IN/OUT parameters from Postgres.
  pg_exec_params_ = exec_params;

  // Set column references in protobuf and whether query is aggregate.
  SetColumnRefs();

  const auto row_mark_type = GetRowMarkType(exec_params);
  if (doc_op_ &&
      !secondary_index_query_ &&
      IsValidRowMarkType(row_mark_type) &&
      CanBuildYbctidsFromPrimaryBinds()) {
    RETURN_NOT_OK(SubstitutePrimaryBindsWithYbctids(exec_params));
  } else {
    RETURN_NOT_OK(ProcessEmptyPrimaryBinds());
    if (doc_op_) {
      if (row_mark_type == RowMarkType::ROW_MARK_KEYSHARE && !IsConcreteRowRead()) {
        // ROW_MARK_KEYSHARE creates a weak read intent on DocDB side. As a result it is only
        // applicable when the read operation reads a concrete row (by using ybctid or by specifying
        // all primary key columns). In case some columns of the primary key are not specified,
        // a strong read intent is required to prevent rows from being deleted by another
        // transaction. For this purpose ROW_MARK_KEYSHARE must be replaced with ROW_MARK_SHARE.
        auto actual_exec_params = *exec_params;
        actual_exec_params.rowmark = RowMarkType::ROW_MARK_SHARE;
        RETURN_NOT_OK(doc_op_->ExecuteInit(&actual_exec_params));
      } else {
        RETURN_NOT_OK(doc_op_->ExecuteInit(exec_params));
      }
    }
  }

  // First, process the secondary index request.
  bool has_ybctid = VERIFY_RESULT(ProcessSecondaryIndexRequest(exec_params));

  if (!has_ybctid && secondary_index_query_ && secondary_index_query_->has_doc_op()) {
    // No ybctid is found from the IndexScan. Instruct "doc_op_" to abandon the execution and not
    // querying any data from tablet server.
    //
    // Note: For system catalog (colocated table), the secondary_index_query_ won't send a separate
    // scan read request to DocDB.  For this case, the index request is embedded inside the SELECT
    // request (PgsqlReadRequestPB::index_request).
    doc_op_->AbandonExecution();

  } else {
    // Update bind values for constants and placeholders.
    RETURN_NOT_OK(UpdateBindPBs());

    // Execute select statement and prefetching data from DocDB.
    // Note: For SysTable, doc_op_ === null, IndexScan doesn't send separate request.
    if (doc_op_) {
      SCHECK_EQ(VERIFY_RESULT(doc_op_->Execute()), RequestSent::kTrue, IllegalState,
                "YSQL read operation was not sent");
    }
  }

  return Status::OK();
}

Status PgDmlRead::BindColumnCondBetween(int attr_num, PgExpr *attr_value, PgExpr *attr_value_end) {
  if (secondary_index_query_) {
    // Bind by secondary key.
    return secondary_index_query_->BindColumnCondBetween(attr_num, attr_value, attr_value_end);
  }

  DCHECK(attr_num != static_cast<int>(PgSystemAttrNum::kYBTupleId))
    << "Operator BETWEEN cannot be applied to ROWID";

  // Find column.
  PgColumn& col = VERIFY_RESULT(bind_.ColumnForAttr(attr_num));

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
  PgsqlExpressionPB *condition_expr_pb = AllocColumnBindConditionExprPB(&col);

  if (attr_value != nullptr) {
    if (attr_value_end != nullptr) {
      condition_expr_pb->mutable_condition()->set_op(QL_OP_BETWEEN);

      auto op1_pb = condition_expr_pb->mutable_condition()->add_operands();
      auto op2_pb = condition_expr_pb->mutable_condition()->add_operands();
      auto op3_pb = condition_expr_pb->mutable_condition()->add_operands();

      op1_pb->set_column_id(col.id());

      RETURN_NOT_OK(attr_value->Eval(op2_pb->mutable_value()));
      RETURN_NOT_OK(attr_value_end->Eval(op3_pb->mutable_value()));
    } else {
      condition_expr_pb->mutable_condition()->set_op(QL_OP_GREATER_THAN_EQUAL);

      auto op1_pb = condition_expr_pb->mutable_condition()->add_operands();
      auto op2_pb = condition_expr_pb->mutable_condition()->add_operands();

      op1_pb->set_column_id(col.id());

      RETURN_NOT_OK(attr_value->Eval(op2_pb->mutable_value()));
    }
  } else {
    if (attr_value_end != nullptr) {
      condition_expr_pb->mutable_condition()->set_op(QL_OP_LESS_THAN_EQUAL);

      auto op1_pb = condition_expr_pb->mutable_condition()->add_operands();
      auto op2_pb = condition_expr_pb->mutable_condition()->add_operands();

      op1_pb->set_column_id(col.id());

      RETURN_NOT_OK(attr_value_end->Eval(op2_pb->mutable_value()));
    } else {
      // Unreachable.
    }
  }

  return Status::OK();
}

Status PgDmlRead::BindColumnCondIn(int attr_num, int n_attr_values, PgExpr **attr_values) {
  if (secondary_index_query_) {
    // Bind by secondary key.
    return secondary_index_query_->BindColumnCondIn(attr_num, n_attr_values, attr_values);
  }

  SCHECK(attr_num != static_cast<int>(PgSystemAttrNum::kYBTupleId),
         InvalidArgument,
         "Operator IN cannot be applied to ROWID");

  // Find column.
  PgColumn& col = VERIFY_RESULT(bind_.ColumnForAttr(attr_num));

  // Check datatype.
  // TODO(neil) Current code combine TEXT and BINARY datatypes into ONE representation.  Once that
  // is fixed, we can remove the special if() check for BINARY type.
  if (col.internal_type() != InternalType::kBinaryValue) {
    for (int i = 0; i < n_attr_values; i++) {
      if (attr_values[i]) {
        SCHECK_EQ(col.internal_type(), attr_values[i]->internal_type(), Corruption,
            "Attribute value type does not match column type");
      }
    }
  }

  if (col.is_primary()) {
    // Alloc the protobuf.
    auto *bind_pb = col.bind_pb();
    if (bind_pb == nullptr) {
      bind_pb = AllocColumnBindPB(&col);
    } else {
      if (expr_binds_.find(bind_pb) != expr_binds_.end()) {
        LOG(WARNING) << strings::Substitute("Column $0 is already bound to another value.",
                                            attr_num);
      }
    }

    bind_pb->mutable_condition()->set_op(QL_OP_IN);
    bind_pb->mutable_condition()->add_operands()->set_column_id(col.id());

    // There's no "list of expressions" field so we simulate it with an artificial nested OR
    // with repeated operands, one per bind expression.
    // This is only used for operation unrolling in pg_doc_op and is not understood by DocDB.
    auto op2_pb = bind_pb->mutable_condition()->add_operands();
    op2_pb->mutable_condition()->set_op(QL_OP_OR);

    for (int i = 0; i < n_attr_values; i++) {
      auto *attr_pb = op2_pb->mutable_condition()->add_operands();
      // Link the expression and protobuf. During execution, expr will write result to the pb.
      RETURN_NOT_OK(attr_values[i]->PrepareForRead(this, attr_pb));

      expr_binds_[attr_pb] = attr_values[i];
    }
  } else {
    // Alloc the protobuf.
    PgsqlExpressionPB *condition_expr_pb = AllocColumnBindConditionExprPB(&col);

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
        RETURN_NOT_OK(attr_values[i]->Eval(
            op2_pb->mutable_value()->mutable_list_value()->add_elems()));
      }
    }
  }

  return Status::OK();
}

Status PgDmlRead::SubstitutePrimaryBindsWithYbctids(const PgExecParameters* exec_params) {
  const auto ybctids = VERIFY_RESULT(BuildYbctidsFromPrimaryBinds());
  std::vector<Slice> ybctidsAsSlice;
  for (const auto& ybctid : ybctids) {
    ybctidsAsSlice.emplace_back(ybctid);
  }
  expr_binds_.clear();
  read_req_->clear_partition_column_values();
  read_req_->clear_range_column_values();
  RETURN_NOT_OK(doc_op_->ExecuteInit(exec_params));
  return doc_op_->PopulateDmlByYbctidOps(&ybctidsAsSlice);
}

// Function builds vector of ybctids from primary key binds.
// Required precondition that one and only one range key component has IN clause and all
// other key components are set must be checked by caller code.
Result<std::vector<std::string>> PgDmlRead::BuildYbctidsFromPrimaryBinds() {
  google::protobuf::RepeatedPtrField<PgsqlExpressionPB> hashed_values;
  vector<docdb::PrimitiveValue> hashed_components, range_components;
  hashed_components.reserve(bind_->num_hash_key_columns());
  range_components.reserve(bind_->num_key_columns() - bind_->num_hash_key_columns());
  for (size_t i = 0; i < bind_->num_hash_key_columns(); ++i) {
    auto& col = bind_.columns()[i];
    hashed_components.push_back(VERIFY_RESULT(
        BuildKeyColumnValue(col, *col.bind_pb(), hashed_values.Add())));
  }

  auto dockey_builder = VERIFY_RESULT(CreateDocKeyBuilder(
      hashed_components, hashed_values, bind_->partition_schema()));

  for (size_t i = bind_->num_hash_key_columns(); i < bind_->num_key_columns(); ++i) {
    auto& col = bind_.columns()[i];
    auto& expr = *col.bind_pb();
    // For IN clause expr->has_condition() returns 'true'.
    if (expr.has_condition()) {
      const auto prefix_len = range_components.size();
      // Form ybctid for each value in IN clause.
      std::vector<std::string> ybctids;
      for (const auto& in_exp : expr.condition().operands(1).condition().operands()) {
        range_components.push_back(VERIFY_RESULT(BuildKeyColumnValue(col, in_exp)));
        // Range key component has one and only one IN clause,
        // all remains components has explicit values. Add them as is.
        for (size_t j = i + 1; j < bind_->num_key_columns(); ++j) {
          auto& suffix_col = bind_.columns()[j];
          range_components.push_back(VERIFY_RESULT(
              BuildKeyColumnValue(suffix_col, *suffix_col.bind_pb())));
        }
        const auto doc_key = dockey_builder(range_components);
        ybctids.push_back(doc_key.Encode().ToStringBuffer());
        range_components.resize(prefix_len);
      }
      return ybctids;
    } else {
      range_components.push_back(VERIFY_RESULT(BuildKeyColumnValue(col, expr)));
    }
  }
  return STATUS(IllegalState, "Can't build ybctids, bad preconditions");
}

// Function checks that one and only one range key component has IN clause
// and all other key components are set.
bool PgDmlRead::CanBuildYbctidsFromPrimaryBinds() {
  if (!bind_) {
    return false;
  }

  size_t range_components_in_clause_count = 0;

  for (size_t i = 0; i < bind_->num_key_columns(); ++i) {
    auto& col = bind_.ColumnForIndex(i);
    auto* expr = col.bind_pb();
    // For IN clause expr->has_condition() returns 'true'.
    if (expr->has_condition()) {
      if ((i < bind_->num_hash_key_columns()) || (++range_components_in_clause_count > 1)) {
        // unsupported IN clause
        return false;
      }
    } else if (expr_binds_.find(expr) == expr_binds_.end()) {
      // missing key component found
      return false;
    }
  }
  return range_components_in_clause_count == 1;
}

// Moves IN operator bound for range key component into 'condition_expr' field
Status PgDmlRead::MoveBoundKeyInOperator(PgColumn* col, const PgsqlConditionPB& in_operator) {
  auto* condition_expr_pb = AllocColumnBindConditionExprPB(col);
  condition_expr_pb->mutable_condition()->set_op(QL_OP_IN);

  auto op1_pb = condition_expr_pb->mutable_condition()->add_operands();
  op1_pb->set_column_id(col->id());

  auto op2_pb = condition_expr_pb->mutable_condition()->add_operands();
  for (const auto& expr : in_operator.operands(1).condition().operands()) {
    RETURN_NOT_OK(CopyBoundValue(
        *col, expr, op2_pb->mutable_value()->mutable_list_value()->add_elems()));
    expr_binds_.erase(Find(expr_binds_, &expr));
  }
  return Status::OK();
}

Status PgDmlRead::CopyBoundValue(
    const PgColumn& col, const PgsqlExpressionPB& src, QLValuePB* dest) const {
  // 'src' expression has no value yet,
  // it is used as the key to find actual source in 'expr_binds_'.
  const auto it = Find(expr_binds_, &src);
  if (it == expr_binds_.end()) {
    return STATUS_FORMAT(IllegalState, "Bind value not found for $0", col.id());
  }
  return it->second->Eval(dest);
}

Result<docdb::PrimitiveValue> PgDmlRead::BuildKeyColumnValue(
    const PgColumn& col, const PgsqlExpressionPB& src, PgsqlExpressionPB* dest) {
  RETURN_NOT_OK(CopyBoundValue(col, src, dest->mutable_value()));
  return docdb::PrimitiveValue::FromQLValuePB(dest->value(), col.desc().sorting_type());
}

Result<docdb::PrimitiveValue> PgDmlRead::BuildKeyColumnValue(
    const PgColumn& col, const PgsqlExpressionPB& src) {
  PgsqlExpressionPB temp_expr;
  return BuildKeyColumnValue(col, src, &temp_expr);
}

Status PgDmlRead::BindHashCode(bool start_valid, bool start_inclusive,
                                uint64_t start_hash_val, bool end_valid,
                                bool end_inclusive, uint64_t end_hash_val) {
  if (secondary_index_query_) {
    return secondary_index_query_->BindHashCode(start_valid, start_inclusive,
                                                  start_hash_val, end_valid,
                                                  end_inclusive, end_hash_val);
  }
  if (start_valid) {
    read_req_->mutable_lower_bound()
            ->set_key(PartitionSchema::EncodeMultiColumnHashValue
                      (start_hash_val));
    read_req_->mutable_lower_bound()->set_is_inclusive(start_inclusive);
  }

  if (end_valid) {
    read_req_->mutable_upper_bound()
              ->set_key(PartitionSchema::EncodeMultiColumnHashValue
                        (end_hash_val));
    read_req_->mutable_upper_bound()->set_is_inclusive(end_inclusive);
  }
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb
