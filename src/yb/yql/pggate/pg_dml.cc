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

#include "yb/yql/pggate/pg_dml.h"

#include "yb/client/yb_op.h"

#include "yb/common/pg_system_attr.h"

#include "yb/util/atomic.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_select_index.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/ybc_pggate.h"

using std::vector;

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// PgDml
//--------------------------------------------------------------------------------------------------

PgDml::PgDml(PgSession::ScopedRefPtr pg_session,
             const PgObjectId& table_id,
             bool is_region_local)
    : PgStatement(std::move(pg_session)), table_id_(table_id), is_region_local_(is_region_local) {
}

PgDml::PgDml(PgSession::ScopedRefPtr pg_session,
             const PgObjectId& table_id,
             const PgObjectId& index_id,
             const PgPrepareParameters *prepare_params,
             bool is_region_local)
    : PgDml(pg_session, table_id, is_region_local) {

  if (prepare_params) {
    prepare_params_ = *prepare_params;
    // Primary index does not have its own data table.
    if (prepare_params_.use_secondary_index) {
      index_id_ = index_id;
    }
  }
}

PgDml::~PgDml() = default;

//--------------------------------------------------------------------------------------------------

Status PgDml::AppendTarget(PgExpr *target) {
  // Except for base_ctid, all targets should be appended to this DML.
  if (target_ && (prepare_params_.index_only_scan || !target->is_ybbasetid())) {
    RETURN_NOT_OK(AppendTargetPB(target));
  } else {
    // Append base_ctid to the index_query.
    RETURN_NOT_OK(secondary_index_query_->AppendTargetPB(target));
  }

  return Status::OK();
}

Status PgDml::AppendTargetPB(PgExpr *target) {
  // Append to targets_.
  bool is_aggregate = target->is_aggregate();
  if (targets_.empty()) {
    has_aggregate_targets_ = is_aggregate;
  } else {
    RSTATUS_DCHECK_EQ(has_aggregate_targets_, is_aggregate,
                      IllegalState, "Combining aggregate and non aggregate targets");
  }

  if (target->is_system()) {
    has_system_targets_ = true;
  }

  if (is_aggregate) {
    auto aggregate = down_cast<PgAggregateOperator*>(target);
    aggregate->set_index(narrow_cast<int>(targets_.size()));
    targets_.push_back(aggregate);
  } else {
    targets_.push_back(down_cast<PgColumnRef*>(target));
  }

  // Prepare expression. Except for constants and place_holders, all other expressions can be
  // evaluated just one time during prepare.
  return target->PrepareForRead(this, AllocTargetPB());
}

Status PgDml::AppendQual(PgExpr *qual, bool is_primary) {
  if (!is_primary) {
    DCHECK(secondary_index_query_) << "The secondary index query is expected";
    return secondary_index_query_->AppendQual(qual, true);
  }

  // Allocate associated protobuf.
  auto* expr_pb = AllocQualPB();

  // Populate the expr_pb with data from the qual expression.
  // Side effect of PrepareForRead is to call PrepareColumnForRead on "this" being passed in
  // for any column reference found in the expression. However, the serialized Postgres expressions,
  // the only kind of Postgres expressions supported as quals, can not be searched.
  // Their column references should be explicitly appended with AppendColumnRef()
  return qual->PrepareForRead(this, expr_pb);
}

Status PgDml::AppendColumnRef(PgColumnRef* colref, bool is_primary) {
  if (!is_primary) {
    DCHECK(secondary_index_query_) << "The secondary index query is expected";
    return secondary_index_query_->AppendColumnRef(colref, true);
  }

  // Postgres attribute number, this is column id to refer the column from Postgres code
  int attr_num = colref->attr_num();
  // Retrieve column metadata from the target relation metadata
  PgColumn& col = VERIFY_RESULT(target_.ColumnForAttr(attr_num));
  if (!col.is_virtual_column()) {
    // Do not overwrite Postgres
    if (!col.has_pg_type_info()) {
      // Postgres type information is required to get column value to evaluate serialized Postgres
      // expressions. For other purposes it is OK to use InvalidOids (zeroes). That would not make
      // the column to appear like it has Postgres type information.
      // Note, that for expression kinds other than serialized Postgres expressions column
      // references are set automatically: when the expressions are being appended they call either
      // PrepareColumnForRead or PrepareColumnForWrite for each column reference expression they
      // contain.
      col.set_pg_type_info(colref->get_pg_typid(),
                           colref->get_pg_typmod(),
                           colref->get_pg_collid());
    }
    // Flag column as used, so it is added to the request
    col.set_read_requested(true);
  }
  return Status::OK();
}

Result<const PgColumn&> PgDml::PrepareColumnForRead(int attr_num, LWPgsqlExpressionPB *target_pb) {
  // Find column from targeted table.
  PgColumn& col = VERIFY_RESULT(target_.ColumnForAttr(attr_num));

  // Prepare protobuf to send to DocDB.
  if (target_pb) {
    target_pb->set_column_id(col.id());
  }

  // Mark non-virtual column reference for DocDB.
  if (!col.is_virtual_column()) {
    col.set_read_requested(true);
  }

  return const_cast<const PgColumn&>(col);
}

Result<const PgColumn&> PgDml::PrepareColumnForRead(int attr_num, LWQLExpressionPB *target_pb) {
  // Find column from targeted table.
  PgColumn& col = VERIFY_RESULT(target_.ColumnForAttr(attr_num));

  // Prepare protobuf to send to DocDB.
  if (target_pb) {
    target_pb->set_column_id(col.id());
  }

  // Mark non-virtual column reference for DocDB.
  if (!col.is_virtual_column()) {
    col.set_read_requested(true);
  }

  return const_cast<const PgColumn&>(col);
}

Status PgDml::PrepareColumnForWrite(PgColumn *pg_col, LWPgsqlExpressionPB *assign_pb) {
  // Prepare protobuf to send to DocDB.
  assign_pb->set_column_id(pg_col->id());

  // Mark non-virtual column reference for DocDB.
  if (!pg_col->is_virtual_column()) {
    pg_col->set_write_requested(true);
  }

  return Status::OK();
}

void PgDml::ColumnRefsToPB(LWPgsqlColumnRefsPB *column_refs) {
  column_refs->Clear();
  for (const PgColumn& col : target_.columns()) {
    if (col.read_requested() || col.write_requested()) {
      column_refs->mutable_ids()->push_back(col.id());
    }
  }
}

void PgDml::ColRefsToPB() {
  // Remove previously set column references in case if the statement is being reexecuted
  ClearColRefPBs();
  for (const PgColumn& col : target_.columns()) {
    // Only used columns are added to the request
    if (col.read_requested() || col.write_requested()) {
      // Allocate a protobuf entry
      auto* col_ref = AllocColRefPB();
      // Add DocDB identifier
      col_ref->set_column_id(col.id());
      // Add Postgres identifier
      col_ref->set_attno(col.attr_num());
      // Add Postgres type information, if defined
      if (col.has_pg_type_info()) {
        col_ref->set_typid(col.pg_typid());
        col_ref->set_typmod(col.pg_typmod());
        col_ref->set_collid(col.pg_collid());
      }
    }
  }
}

//--------------------------------------------------------------------------------------------------

Status PgDml::BindColumn(int attr_num, PgExpr *attr_value) {
  if (secondary_index_query_) {
    // Bind by secondary key.
    return secondary_index_query_->BindColumn(attr_num, attr_value);
  }

  // Find column to bind.
  PgColumn& column = VERIFY_RESULT(bind_.ColumnForAttr(attr_num));

  // Check datatype.
  const auto attr_internal_type = attr_value->internal_type();
  if (attr_internal_type != InternalType::kGinNullValue) {
    SCHECK_EQ(column.internal_type(), attr_internal_type, Corruption,
              "Attribute value type does not match column type");
  }

  RETURN_NOT_OK(AllocColumnBindPB(&column, attr_value));

  if (attr_num == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
    CHECK(attr_value->is_constant()) << "Column ybctid must be bound to constant";
    ybctid_bind_ = true;
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgDml::BindTable() {
  bind_table_ = true;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PgDml::AssignColumn(int attr_num, PgExpr *attr_value) {
  // Find column from targeted table.
  PgColumn& column = VERIFY_RESULT(target_.ColumnForAttr(attr_num));

  // Check datatype.
  SCHECK_EQ(column.internal_type(), attr_value->internal_type(), Corruption,
            "Attribute value type does not match column type");

  // Alloc the protobuf.
  auto* assign_pb = column.assign_pb();
  if (assign_pb == nullptr) {
    assign_pb = AllocColumnAssignPB(&column);
  } else {
    if (expr_assigns_.count(assign_pb)) {
      return STATUS_SUBSTITUTE(InvalidArgument,
                               "Column $0 is already assigned to another value", attr_num);
    }
  }

  // Link the expression and protobuf. During execution, expr will write result to the pb.
  // - Prepare the left hand side for write.
  // - Prepare the right hand side for read. Currently, the right hand side is always constant.
  RETURN_NOT_OK(PrepareColumnForWrite(&column, assign_pb));
  RETURN_NOT_OK(attr_value->PrepareForRead(this, assign_pb));

  // Link the given expression "attr_value" with the allocated protobuf. Note that except for
  // constants and place_holders, all other expressions can be setup just one time during prepare.
  // Examples:
  // - Setup rhs values for SET column = assign_pb in UPDATE statement.
  //     UPDATE a_table SET col = assign_expr;
  expr_assigns_[assign_pb] = attr_value;

  return Status::OK();
}

Status PgDml::UpdateAssignPBs() {
  // Process the column binds for two cases.
  // For performance reasons, we might evaluate these expressions together with bind values in YB.
  for (const auto &entry : expr_assigns_) {
    auto* expr_pb = entry.first;
    PgExpr *attr_value = entry.second;
    RETURN_NOT_OK(attr_value->EvalTo(expr_pb));
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Result<bool> PgDml::ProcessSecondaryIndexRequest(const PgExecParameters *exec_params) {
  if (!secondary_index_query_) {
    // Secondary INDEX is not used in this request.
    return false;
  }

  // Execute query in PgGate.
  // If index query is not yet executed, run it.
  if (!secondary_index_query_->is_executed()) {
    secondary_index_query_->set_is_executed(true);
    RETURN_NOT_OK(secondary_index_query_->Exec(exec_params));
  }

  // Not processing index request if it does not require its own doc operator.
  //
  // When INDEX is used for system catalog (colocated table), the index subquery does not have its
  // own operator. The request is combined with 'this' outer SELECT using 'index_request' attribute.
  //   (PgDocOp)doc_op_->(YBPgsqlReadOp)read_op_->(PgsqlReadRequestPB)read_request_::index_request
  if (!secondary_index_query_->has_doc_op()) {
    return false;
  }

  // When INDEX has its own doc_op, execute it to fetch next batch of ybctids which is then used
  // to read data from the main table.
  if (!VERIFY_RESULT(secondary_index_query_->FetchYbctidBatch(&retrieved_ybctids_))) {
    // No more rows of ybctids.
    return false;
  }

  if (prepare_params_.fetch_ybctids_only)
    return true;

  // Update request with the new batch of ybctids to fetch the next batch of rows.
  RETURN_NOT_OK(UpdateRequestWithYbctids(*retrieved_ybctids_,
                                         KeepOrder(secondary_index_query_->KeepOrder())));

  AtomicFlagSleepMs(&FLAGS_TEST_inject_delay_between_prepare_ybctid_execute_batch_ybctid_ms);
  return true;
}

Status PgDml::UpdateRequestWithYbctids(const std::vector<Slice>& ybctids, KeepOrder keep_order) {
  auto i = ybctids.begin();
  return doc_op_->PopulateByYbctidOps({make_lw_function([&i, end = ybctids.end()] {
    return i != end ? *i++ : Slice();
  }), ybctids.size()}, keep_order);
}

Status PgDml::Fetch(int32_t natts,
                    uint64_t *values,
                    bool *isnulls,
                    PgSysColumns *syscols,
                    bool *has_data) {
  // Each isnulls and values correspond (in order) to columns from the table schema.
  // Initialize to nulls for any columns not present in result.
  if (isnulls) {
    memset(isnulls, true, natts * sizeof(bool));
  }
  if (syscols) {
    memset(syscols, 0, sizeof(PgSysColumns));
  }

  // Keep reading until we either reach the end or get some rows.
  *has_data = true;
  PgTuple pg_tuple(values, isnulls, syscols);
  while (!VERIFY_RESULT(GetNextRow(&pg_tuple))) {
    if (!VERIFY_RESULT(FetchDataFromServer())) {
      // Stop processing as server returns no more rows.
      *has_data = false;
      return Status::OK();
    }
  }

  return Status::OK();
}

Result<bool> PgDml::FetchDataFromServer() {
  // Get the rowsets from doc-operator.
  rowsets_.splice(rowsets_.end(), VERIFY_RESULT(doc_op_->GetResult()));

  // Check if EOF is reached.
  if (rowsets_.empty()) {
    // Process the secondary index to find the next WHERE condition.
    //   DML(Table) WHERE ybctid IN (SELECT base_ybctid FROM IndexTable),
    //   The nested query would return many rows each of which yields different result-set.
    if (!VERIFY_RESULT(ProcessSecondaryIndexRequest(nullptr))) {
      // Return EOF as the nested subquery does not have any more data.
      return false;
    }

    // Execute doc_op_ again for the new set of WHERE condition from the nested query.
    SCHECK_EQ(VERIFY_RESULT(doc_op_->Execute()), RequestSent::kTrue, IllegalState,
              "YSQL read operation was not sent");

    // Get the rowsets from doc-operator.
    rowsets_.splice(rowsets_.end(), VERIFY_RESULT(doc_op_->GetResult()));
  }

  // Return the output parameter back to Postgres if server wants.
  if (doc_op_->has_out_param_backfill_spec() && pg_exec_params_) {
    PgExecOutParamValue value;
    value.bfoutput = doc_op_->out_param_backfill_spec();
    YBCGetPgCallbacks()->WriteExecOutParam(pg_exec_params_->out_param, &value);
  }

  return true;
}

Result<bool> PgDml::GetNextRow(PgTuple *pg_tuple) {
  for (;;) {
    for (auto rowset_iter = rowsets_.begin(); rowset_iter != rowsets_.end();) {
      // Check if the rowset has any data.
      auto& rowset = *rowset_iter;
      if (rowset.is_eof()) {
        rowset_iter = rowsets_.erase(rowset_iter);
        continue;
      }

      // If this rowset has the next row of the index order, load it. Otherwise, continue looking
      // for the next row in the order.
      //
      // NOTE:
      //   DML <Table> WHERE ybctid IN (SELECT base_ybctid FROM <Index> ORDER BY <Index Range>)
      // The nested subquery should return rows in indexing order, but the ybctids are then grouped
      // by hash-code for BATCH-DML-REQUEST, so the response here are out-of-order.
      if (rowset.NextRowOrder() <= current_row_order_) {
        // Write row to postgres tuple.
        int64_t row_order = -1;
        RETURN_NOT_OK(rowset.WritePgTuple(targets_, pg_tuple, &row_order));
        SCHECK(row_order == -1 || row_order == current_row_order_, InternalError,
               "The resulting row are not arranged in indexing order");

        // Found the current row. Move cursor to next row.
        current_row_order_++;
        return true;
      }

      rowset_iter++;
    }

    if (!rowsets_.empty() && doc_op_->end_of_data()) {
      // If the current desired row is missing, skip it and continue to look for the next
      // desired row in order. A row is deemed missing if it is not found and the doc op
      // has no more rows to return.
      current_row_order_++;
    } else {
      break;
    }
  }

  return false;
}

bool PgDml::has_aggregate_targets() const {
  return has_aggregate_targets_;
}

bool PgDml::has_system_targets() const {
  return has_system_targets_;
}

bool PgDml::has_secondary_index_with_doc_op() const {
  return secondary_index_query_ && secondary_index_query_->has_doc_op();
}

Result<YBCPgColumnInfo> PgDml::GetColumnInfo(int attr_num) const {
  if (secondary_index_query_) {
    return secondary_index_query_->GetColumnInfo(attr_num);
  }
  return bind_->GetColumnInfo(attr_num);
}

}  // namespace pggate
}  // namespace yb
