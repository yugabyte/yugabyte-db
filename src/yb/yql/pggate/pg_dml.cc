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

#include "yb/yql/pggate/pg_dml.h"

#include "yb/client/yb_op.h"

#include "yb/common/pg_system_attr.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_select.h"
#include "yb/yql/pggate/pg_select_index.h"
#include "yb/yql/pggate/util/pg_doc_data.h"

namespace yb::pggate {
namespace {

class IndexYbctidProvider : public YbctidProvider {
 public:
  explicit IndexYbctidProvider(PgSelectIndex& index)
      : index_(index) {}

 private:
  Result<std::optional<YbctidBatch>> Fetch() override {
    return index_.FetchYbctidBatch();
  }

  void Reset() override {}

  PgSelectIndex& index_;
};

} // namespace

PgDml::SecondaryIndexQueryWrapper::SecondaryIndexQueryWrapper(
    std::unique_ptr<PgDmlRead>&& query, std::reference_wrapper<const YbcPgExecParameters*> params)
    : query_(std::move(query)), params_(params), is_executed_(false) {
  DCHECK(query_);
}

Status PgDml::SecondaryIndexQueryWrapper::Execute() {
  if (is_executed_) {
    return Status::OK();
  }
  is_executed_ = true;
  return query_->Exec(params_);
}

PgDml::PgDml(const PgSession::ScopedRefPtr& pg_session)
    : PgStatement(pg_session) {}

PgDml::~PgDml() = default;

Status PgDml::AppendTarget(PgExpr* target, bool is_for_secondary_index) {
  if (is_for_secondary_index) {
    return DCHECK_NOTNULL(SecondaryIndexQuery())->AppendTargetPB(target);
  }
  return AppendTargetPB(target);
}

Status PgDml::AddBaseYbctidTarget() {
  auto& col = VERIFY_RESULT_REF(target_.ColumnForAttr(
      std::to_underlying(PgSystemAttrNum::kYBIdxBaseTupleId)));
  if (!col.read_requested()) {
    auto* target_pb = AllocTargetPB();
    target_pb->set_column_id(col.id());
    col.set_read_requested(true);
  }
  return Status::OK();
}

Status PgDml::AppendTargetPB(PgExpr* target) {
  // Append to targets_.
  bool is_aggregate = target->is_aggregate();
  if (!targets_) {
    targets_ = std::make_shared<FetchedTargets>();
    has_aggregate_targets_ = is_aggregate;
  } else {
    RSTATUS_DCHECK_EQ(has_aggregate_targets_, is_aggregate,
                      IllegalState, "Combining aggregate and non aggregate targets");
  }

  if (is_aggregate) {
    auto aggregate = down_cast<PgAggregateOperator*>(target);
    aggregate->set_index(narrow_cast<int>(targets_->size()));
    targets_->push_back(aggregate);
  } else {
    targets_->push_back(down_cast<PgColumnRef*>(target));
  }

  // Prepare expression. Except for constants and place_holders, all other expressions can be
  // evaluated just one time during prepare.
  return target->PrepareForRead(this, AllocTargetPB());
}

Status PgDml::AppendColumnRef(PgColumnRef* colref, bool is_for_secondary_index) {
  if (is_for_secondary_index) {
    return DCHECK_NOTNULL(SecondaryIndexQuery())->AppendColumnRef(
        colref, /* is_for_secondary_index= */ false);
  }

  // Postgres attribute number, this is column id to refer the column from Postgres code
  int attr_num = colref->attr_num();
  // Retrieve column metadata from the target relation metadata
  auto& col = VERIFY_RESULT_REF(target_.ColumnForAttr(attr_num));
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

Result<const PgColumn&> PgDml::PrepareColumnForRead(int attr_num, LWPgsqlExpressionPB* target_pb) {
  // Find column from targeted table.
  auto& col = VERIFY_RESULT_REF(target_.ColumnForAttr(attr_num));

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

Result<const PgColumn&> PgDml::PrepareColumnForRead(int attr_num, LWQLExpressionPB* target_pb) {
  // Find column from targeted table.
  auto& col = VERIFY_RESULT_REF(target_.ColumnForAttr(attr_num));

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

Status PgDml::PrepareColumnForWrite(PgColumn* pg_col, LWPgsqlExpressionPB* assign_pb) {
  // Prepare protobuf to send to DocDB.
  assign_pb->set_column_id(pg_col->id());

  // Mark non-virtual column reference for DocDB.
  if (!pg_col->is_virtual_column()) {
    pg_col->set_write_requested(true);
  }

  return Status::OK();
}

void PgDml::ColumnRefsToPB(LWPgsqlColumnRefsPB* column_refs) {
  column_refs->Clear();
  for (const PgColumn& col : target_.columns()) {
    if (col.read_requested() || col.write_requested()) {
      column_refs->mutable_ids()->push_back(col.id());
    }
  }
}

void PgDml::ColRefsToPB() {
  // Remove previously set column references in case if the statement is being reexecuted
  auto& col_refs = ColRefPBs();
  col_refs.clear();
  for (const auto& col : target_.columns()) {
    // Only used columns are added to the request
    if (col.read_requested() || col.write_requested()) {
      // Allocate a protobuf entry
      auto& col_ref = col_refs.emplace_back();
      // Add DocDB identifier
      col_ref.set_column_id(col.id());
      // Add Postgres identifier
      col_ref.set_attno(col.attr_num());
      // Add Postgres type information, if defined
      if (col.has_pg_type_info()) {
        col_ref.set_typid(col.pg_typid());
        col_ref.set_typmod(col.pg_typmod());
        col_ref.set_collid(col.pg_collid());
      }
    }
  }
}

Status PgDml::BindColumn(int attr_num, PgExpr* attr_value) {
  // Convert to wire protocol. See explanation in pg_system_attr.h.
  if (attr_num == static_cast<int>(PgSystemAttrNum::kPGInternalYBTupleId)) {
    attr_num = static_cast<int>(PgSystemAttrNum::kYBTupleId);
  }

  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->BindColumn(attr_num, attr_value);
  }

  // Find column to bind.
  auto& column = VERIFY_RESULT_REF(bind_.ColumnForAttr(attr_num));

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

Status PgDml::ANNBindVector(PgExpr *query_vec) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->ANNBindVector(query_vec);
  }

  return down_cast<PgDmlRead*>(this)->ANNBindVector(query_vec);
}

Status PgDml::ANNSetPrefetchSize(int32_t prefetch_size) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->ANNSetPrefetchSize(prefetch_size);
  }

  return down_cast<PgDmlRead*>(this)->ANNSetPrefetchSize(prefetch_size);
}

Status PgDml::HnswSetReadOptions(int ef_search) {
  if (auto* secondary_index = SecondaryIndexQuery(); secondary_index) {
    return secondary_index->HnswSetReadOptions(ef_search);
  }

  return down_cast<PgDmlRead*>(this)->HnswSetReadOptions(ef_search);
}

Status PgDml::BindTable() {
  bind_table_ = true;
  return Status::OK();
}

Status PgDml::AssignColumn(int attr_num, PgExpr* attr_value) {
  // Find column from targeted table.
  auto& column = VERIFY_RESULT_REF(target_.ColumnForAttr(attr_num));

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

Result<bool> PgDml::ProcessProvidedYbctids() {
  if (secondary_index_) {
    RETURN_NOT_OK(secondary_index_->Execute());
  }

  auto* provider = ybctid_provider();
  const auto data =  provider ? VERIFY_RESULT(provider->Fetch()) : std::nullopt;
  if (!data) {
    return false;
  }

  // Update request with the new batch of ybctids to fetch the next batch of rows.
  return UpdateRequestWithYbctids(data->ybctids, KeepOrder(data->keep_order));
}

Result<bool> PgDml::UpdateRequestWithYbctids(std::span<const Slice> ybctids, KeepOrder keep_order) {
  auto i = ybctids.begin();
  return doc_op_->PopulateByYbctidOps({make_lw_function([&i, end = ybctids.end()] {
    return i != end ? *i++ : Slice();
  }), ybctids.size()}, keep_order);
}

Status PgDml::Fetch(
    int32_t natts, uint64_t* values, bool* isnulls, YbcPgSysColumns* syscols, bool* has_data) {
  // Each isnulls and values correspond (in order) to columns from the table schema.
  // Initialize to nulls for any columns not present in result.
  if (isnulls) {
    memset(isnulls, true, natts * sizeof(bool));
  }
  if (syscols) {
    memset(syscols, 0, sizeof(YbcPgSysColumns));
  }

  // Keep reading until we either reach the end or get some rows.
  *has_data = true;
#ifdef PGTUPLE_DEBUG
  PgTuple pg_tuple(natts, values, isnulls, syscols);
#else
  PgTuple pg_tuple(values, isnulls, syscols);
#endif
  while (!VERIFY_RESULT(doc_op_->ResultStream().GetNextRow(&pg_tuple))) {
    // Find out if there are more ybctids to fetch
    if (!VERIFY_RESULT(ProcessProvidedYbctids())) {
      *has_data = false;
      return Status::OK();
    }
    // Execute doc_op_ again for the new set of ybctids
    SCHECK_EQ(
      VERIFY_RESULT(doc_op_->Execute()), RequestSent::kTrue, IllegalState,
      "YSQL read operation was not sent");
  }

  return Status::OK();
}

Result<YbcPgColumnInfo> PgDml::GetColumnInfo(int attr_num) const {
  auto* secondary_index = SecondaryIndexQuery();
  return secondary_index
      ? secondary_index->GetColumnInfo(attr_num) : bind_->GetColumnInfo(attr_num);
}

void PgDml::SetYbctidProvider(std::unique_ptr<YbctidProvider>&& provider) {
  DCHECK(!secondary_index_);
  ybctid_provider_ = std::move(provider);
}

void PgDml::SetSecondaryIndex(std::unique_ptr<PgSelectIndex>&& index_query) {
  DCHECK(!secondary_index_ && !ybctid_provider_);
  if (index_query->doc_op_) {
    ybctid_provider_ = std::make_unique<IndexYbctidProvider>(*index_query);
  }
  secondary_index_.emplace(std::move(index_query), pg_exec_params_);
}

}  // namespace yb::pggate
