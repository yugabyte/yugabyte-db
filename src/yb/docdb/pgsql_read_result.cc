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

#include "yb/docdb/pgsql_read_result.h"

#include <algorithm>
#include <limits>
#include <optional>

#include "yb/bfpg/tserver_opcodes.h"

#include "yb/common/pg_system_attr.h"
#include "yb/common/pgsql_protocol.messages.h"

#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/write_buffer.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

DECLARE_uint64(rpc_max_message_size);
DECLARE_double(max_buffer_size_to_rpc_limit_ratio);

namespace yb::docdb {

namespace {

// Batch-key reads do not apply an emitted-row prefetch limit: the client already bounded the
// batch. A ybctid batch pages on the size limit through its key provider. Vector search reads
// keys from a provider that cannot page, so ResolveResponseSizeLimit gives it no size limit.
//
// Matches the two shapes PgsqlReadOperation::Execute routes to ExecuteBatchKeys: an explicit
// ybctid batch, and vector search (keys from the index, empty batch_arguments).
size_t ResolveEmittedRowCountLimit(const PgsqlReadRequestMsg& request) {
  const bool is_batch_read =
      !request.batch_arguments().empty() || request.index_request().has_vector_idx_options();
  return !is_batch_read && request.has_limit() && request.limit() > 0
             ? request.limit()
             : std::numeric_limits<size_t>::max();
}

// The response size should not exceed the rpc_max_message_size, so use it as a default size
// limit. Reduce it to the number requested by the client, if it is stricter.
uint64_t ResolveResponseSizeLimit(const PgsqlReadRequestMsg& request) {
  if (request.index_request().has_vector_idx_options()) {
    return std::numeric_limits<uint64_t>::max();
  }
  uint64_t response_size_limit = FLAGS_rpc_max_message_size *
                                FLAGS_max_buffer_size_to_rpc_limit_ratio;
  if (request.has_size_limit() && request.size_limit() > 0) {
    response_size_limit = std::min(response_size_limit, request.size_limit());
  }
  return response_size_limit;
}

template <bool kLast>
void NullEncoder(const dockv::PgTableRow& row, WriteBuffer* buffer,
                 const dockv::PgWireEncoderEntry* chain);

template <bool kLast>
void TupleIdEncoder(
    const dockv::PgTableRow& row, WriteBuffer* buffer, const dockv::PgWireEncoderEntry* chain);

dockv::PgWireEncoderEntry MakeNullEncoder(bool last);

dockv::PgWireEncoderEntry GetEncoder(
    YQLRowwiseIteratorIf::UniPtr* table_iter, const dockv::PgTableRow& table_row,
    const PgsqlExpressionMsg& expr, bool last) {
  if (expr.expr_case() == PgsqlExpressionPB::kColumnId) {
    if (expr.column_id() != static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
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

void NopEncoder(
    const dockv::PgTableRow& row, WriteBuffer* buffer, const dockv::PgWireEncoderEntry* chain) {}

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

template <bool kLast>
void NullEncoder(const dockv::PgTableRow& row, WriteBuffer* buffer,
                 const dockv::PgWireEncoderEntry* chain) {
  buffer->PushBack(1);
  dockv::CallNextEncoder<kLast>(row, buffer, chain);
}

bool IsAggregateTarget(const PgsqlExpressionMsg& expr) {
  return expr.expr_case() == PgsqlExpressionPB::kTscall &&
         bfpg::IsAggregateOpcode(static_cast<bfpg::TSOpcode>(expr.tscall().opcode()));
}

size_t Int64ColumnWireSizeFloor() {
  static const size_t kFloor = [] {
    QLValuePB value;
    value.set_int64_value(0);
    return CHECK_RESULT(pggate::ColumnWireSize(value));
  }();
  return kFloor;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// PgsqlReadResultProcessor
// ------------------------------------------------------------------------------------------------

PgsqlReadResultProcessor::PgsqlReadResultProcessor(
    const PgsqlReadRequestMsg& request, WriteBuffer* result_buffer)
    : request_(request),
      result_buffer_(result_buffer),
      emitted_row_count_limit_(ResolveEmittedRowCountLimit(request)),
      response_size_limit_(ResolveResponseSizeLimit(request)) {}

// ------------------------------------------------------------------------------------------------
// PgsqlReadRowsProcessor
// ------------------------------------------------------------------------------------------------

PgsqlReadRowsProcessor::PgsqlReadRowsProcessor(
    const PgsqlReadRequestMsg& request, WriteBuffer* result_buffer,
    YQLRowwiseIteratorIf::UniPtr* table_iter_holder)
    : PgsqlReadResultProcessor(request, result_buffer), table_iter_holder_(table_iter_holder) {}

Result<FetchLimit> PgsqlReadRowsProcessor::ProcessMatchedRow(const dockv::PgTableRow& row) {
  auto row_start = result_buffer_->Position();
  RETURN_NOT_OK(WriteRow(row));

  // First emitted row may exceed the size limit; later rows must not.
  if (emitted_rows_ > 0 && result_buffer_->size() > response_size_limit_) {
    RETURN_NOT_OK(result_buffer_->Truncate(row_start));
    return FetchLimit::kExceeded;
  }

  ++emitted_rows_;
  if (emitted_rows_ >= emitted_row_count_limit_ ||
      result_buffer_->size() >= response_size_limit_) {
    return FetchLimit::kReached;
  }

  return FetchLimit::kNotReached;
}

Result<size_t> PgsqlReadRowsProcessor::Finish() {
  return emitted_rows_;
}

Status PgsqlReadRowsProcessor::WriteRow(const dockv::PgTableRow& table_row) {
  if (target_encoders_.empty()) {
    InitTargetEncoders(table_row);
  }
  target_encoders_.front().Invoke(table_row, result_buffer_);
  return Status::OK();
}

void PgsqlReadRowsProcessor::InitTargetEncoders(const dockv::PgTableRow& table_row) {
  const auto& targets = request_.targets();
  const auto size = targets.size();
  if (size) {
    target_encoders_.reserve(size);
    for (auto it = targets.begin(), end = targets.end();;) {
      const auto& expr = *it;
      bool last = ++it == end;
      target_encoders_.push_back(GetEncoder(table_iter_holder_, table_row, expr, last));
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

// ------------------------------------------------------------------------------------------------
// PgsqlReadAggregateProcessor
// ------------------------------------------------------------------------------------------------

PgsqlReadAggregateProcessor::PgsqlReadAggregateProcessor(
    const PgsqlReadRequestMsg& request, WriteBuffer* result_buffer,
    YQLRowwiseIteratorIf::UniPtr* table_iter_holder)
    : PgsqlReadResultProcessor(request, result_buffer), table_iter_holder_(table_iter_holder) {
  group_key_targets_.reserve(request.targets().size());
  aggregate_targets_.reserve(request.targets().size());
  for (const auto& target : request.targets()) {
    (IsAggregateTarget(target) ? aggregate_targets_ : group_key_targets_).push_back(&target);
  }
}

Status PgsqlReadAggregateProcessor::Init(const dockv::ReaderProjection& projection) {
  return ClassifyFixedWireSizes(projection);
}

Result<FetchLimit> PgsqlReadAggregateProcessor::ProcessMatchedRow(const dockv::PgTableRow& row) {
  const bool has_group_changed = has_active_group_ && GroupKeyChanged(row);
  if (has_group_changed) {
    RETURN_NOT_OK(EmitActiveGroup());
    if (emitted_rows_ >= emitted_row_count_limit_ ||
        result_buffer_->size() >= response_size_limit_) {
      // It is NOT possible that we exceed the response_size_limit_ or emitted_row_count_limit_
      // here except for the first emitted group, which skips the projected-size check.
      // i.e. result_buffer_->size() > response_size_limit_ OR
      //       emitted_rows_ > emitted_row_count_limit_.
      // This is because we already ensured before beginning to process the previous row that we are
      // not going to exceed the response_size_limit_ after processing the previous row. Similarly,
      // we check emitted_rows_ after incrementing by 1 whether it has reached the
      // emitted_row_count_limit_. Through these checks, we ensure that after processing the
      // previous row, we are not going to exceed the limits by processing it. If for any reason the
      // limits are exceeded, it is too late to undo the processing now and we have to include the
      // previous row in the aggregate response.
      return FetchLimit::kExceeded;
    }
  }

  PrepareAggregateResults(aggregate_targets_.size());

  if (!has_active_group_) {
    RETURN_NOT_OK(BuildGroupKeyValues(row));
    has_active_group_ = true;
  }

  if (emitted_rows_ > 0 &&
      VERIFY_RESULT(MeasureProjectedUpperBound(row)) > response_size_limit_) {
    if (has_group_changed) {
      // Clear the just-started empty group; aggregates are empty and must not be emitted.
      ClearActiveGroup();
    }
    // Finish is going to emit the currently open group if any.
    return FetchLimit::kExceeded;
  }

  RETURN_NOT_OK(EvalTargets(row));
  return FetchLimit::kNotReached;
}

Result<size_t> PgsqlReadAggregateProcessor::Finish() {
  // Emit any open group so folded work is not discarded (e.g. after scan deadline).
  if (has_active_group_) {
    RETURN_NOT_OK(EmitActiveGroup());
  }
  return emitted_rows_;
}

Status PgsqlReadAggregateProcessor::ClassifyFixedWireSizes(
    const dockv::ReaderProjection& projection) {
  RETURN_NOT_OK(CacheGroupKeyColumnIndexes(projection));
  RETURN_NOT_OK(CacheAggregateOperandColumnIndexes(projection));

  variable_key_idxs_.reserve(group_key_targets_.size());
  for (size_t i = 0; i < group_key_targets_.size(); ++i) {
    if (auto size = VERIFY_RESULT(FixedKeyWireSize(projection, group_key_column_idxs_[i]))) {
      fixed_keys_wire_size_ += *size;
    } else {
      variable_key_idxs_.push_back(i);
    }
  }

  variable_aggregate_idxs_.reserve(aggregate_targets_.size());
  size_t fixed_aggregate_count = 0;
  for (size_t i = 0; i < aggregate_targets_.size(); ++i) {
    if (VERIFY_RESULT(IsFixedWidthAggregateTarget(
            projection, *aggregate_targets_[i], aggregate_operand_idxs_[i]))) {
      ++fixed_aggregate_count;
    } else {
      variable_aggregate_idxs_.push_back(i);
    }
  }
  fixed_aggregates_wire_size_ = fixed_aggregate_count * Int64ColumnWireSizeFloor();
  return Status::OK();
}

bool PgsqlReadAggregateProcessor::GroupKeyChanged(const dockv::PgTableRow& row) const {
  DCHECK_EQ(group_key_column_idxs_.size(), active_group_keys_.size());
  for (size_t i = 0; i < active_group_keys_.size(); ++i) {
    if (!row.EqualsQLValuePB(group_key_column_idxs_[i], active_group_keys_[i])) {
      return true;
    }
  }
  return false;
}

Status PgsqlReadAggregateProcessor::EmitActiveGroup() {
  size_t key_index = 0;
  size_t aggregate_index = 0;
  // Emit in original targets[] order (keys and aggregates may be interleaved).
  for (const auto& target : request_.targets()) {
    if (IsAggregateTarget(target)) {
      RETURN_NOT_OK(
          pggate::WriteColumn(aggr_result_[aggregate_index++].Value(), result_buffer_));
    } else {
      RETURN_NOT_OK(pggate::WriteColumn(active_group_keys_[key_index++], result_buffer_));
    }
  }
  ++emitted_rows_;

  ClearActiveGroup();
  ResetAggregateResults();

  return Status::OK();
}

Status PgsqlReadAggregateProcessor::BuildGroupKeyValues(const dockv::PgTableRow& row) {
  active_group_keys_.reserve(group_key_targets_.size());
  for (const auto* target : group_key_targets_) {
    active_group_keys_.push_back(row.GetQLValuePB(target->column_id()));
  }

  active_keys_wire_size_ = fixed_keys_wire_size_;
  for (const auto idx : variable_key_idxs_) {
    active_keys_wire_size_ += VERIFY_RESULT(pggate::ColumnWireSize(active_group_keys_[idx]));
  }
  return Status::OK();
}

Result<size_t> PgsqlReadAggregateProcessor::MeasureProjectedUpperBound(
    const dockv::PgTableRow& row) {
  size_t total = result_buffer_->size() + active_keys_wire_size_ + fixed_aggregates_wire_size_;
  const size_t fixed_floor = Int64ColumnWireSizeFloor();
  for (const auto idx : variable_aggregate_idxs_) {
    const size_t aggr_size = VERIFY_RESULT(pggate::ColumnWireSize(aggr_result_[idx].Value()));
    const size_t row_size = VERIFY_RESULT(MeasureAggregateRowInputWireSize(
        *aggregate_targets_[idx], row, aggregate_operand_idxs_[idx]));
    total += std::max({aggr_size, row_size, fixed_floor});
  }
  return total;
}

void PgsqlReadAggregateProcessor::ClearActiveGroup() {
  active_group_keys_.clear();
  active_keys_wire_size_ = 0;
  has_active_group_ = false;
}

Status PgsqlReadAggregateProcessor::EvalTargets(const dockv::PgTableRow& row) {
  for (size_t i = 0; i < aggregate_targets_.size(); ++i) {
    RETURN_NOT_OK(EvalExpr(*aggregate_targets_[i], row, aggr_result_[i].Writer()));
  }
  return Status::OK();
}

Status PgsqlReadAggregateProcessor::CacheGroupKeyColumnIndexes(
    const dockv::ReaderProjection& projection) {
  group_key_column_idxs_.resize(group_key_targets_.size());
  for (size_t i = 0; i < group_key_targets_.size(); ++i) {
    const auto& target = *group_key_targets_[i];
    SCHECK(
        target.expr_case() == PgsqlExpressionPB::kColumnId, InvalidArgument,
        "Group key target $0 is not a column reference", i);
    // Negative ids are system columns. ColumnId aborts on them in debug
    // before the projection lookup below can reject them.
    SCHECK_GE(target.column_id(), 0, InvalidArgument, "Group key is a system column");
    group_key_column_idxs_[i] = VERIFY_RESULT(
        ProjectionColumnIndex(projection, target.column_id()));
    const auto type = projection.columns[group_key_column_idxs_[i]].data_type;
    SCHECK(
        pggate::IsSupportedGroupKeyType(type), InvalidArgument,
        Format("Group key $0 has unsupported type $1", i, type));
  }
  return Status::OK();
}

Status PgsqlReadAggregateProcessor::CacheAggregateOperandColumnIndexes(
    const dockv::ReaderProjection& projection) {
  aggregate_operand_idxs_.assign(
      aggregate_targets_.size(), dockv::ReaderProjection::kNotFoundIndex);
  for (size_t i = 0; i < aggregate_targets_.size(); ++i) {
    const auto& target = *aggregate_targets_[i];
    if (!target.has_tscall() || target.tscall().operands().empty()) {
      continue;
    }
    const auto& operand = *target.tscall().operands().begin();
    // COUNT(*)'s dummy constant and ybctid/other system columns are not projected columns.
    if (!operand.has_column_id() || operand.column_id() < 0) {
      continue;
    }
    aggregate_operand_idxs_[i] = VERIFY_RESULT(
        ProjectionColumnIndex(projection, operand.column_id()));
  }
  return Status::OK();
}

Result<std::optional<size_t>> PgsqlReadAggregateProcessor::FixedKeyWireSize(
    const dockv::ReaderProjection& projection, size_t idx) {
  const auto type = projection.columns[idx].data_type;
  if (!pggate::IsFixedWidthDataType(type)) {
    return std::nullopt;
  }
  // IsFixedWidthDataType guarantees ColumnWireSize(type, false, 0) succeeds.
  return CHECK_RESULT(pggate::ColumnWireSize(type, /*is_null=*/false, /*payload_size=*/0));
}

Result<bool> PgsqlReadAggregateProcessor::IsFixedWidthAggregateTarget(
    const dockv::ReaderProjection& projection, const PgsqlExpressionMsg& target, size_t idx) {
  if (!target.has_tscall()) {
    return false;
  }

  const auto opcode = static_cast<bfpg::TSOpcode>(target.tscall().opcode());
  // Operand type only bounds MIN/MAX output; COUNT/SUM* are fixed-width regardless of it.
  // idx is kNotFoundIndex for a non-column operand (COUNT(*)'s dummy constant) or a system
  // column, which leaves the type unknown and keeps MIN/MAX on the measured path.
  DataType arg_type = DataType::UNKNOWN_DATA;
  if ((opcode == bfpg::TSOpcode::kMin || opcode == bfpg::TSOpcode::kMax) &&
      idx != dockv::ReaderProjection::kNotFoundIndex) {
    arg_type = projection.columns[idx].data_type;
  }

  return pggate::IsFixedWidthAggregate(opcode, arg_type);
}

Result<size_t> PgsqlReadAggregateProcessor::MeasureAggregateRowInputWireSize(
    const PgsqlExpressionMsg& expr, const dockv::PgTableRow& row, size_t operand_idx) {
  if (expr.expr_case() != PgsqlExpressionPB::kTscall || expr.tscall().operands().empty()) {
    return 0;
  }
  const auto& operand = *expr.tscall().operands().begin();
  if (operand.has_column_id() &&
      operand.column_id() == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
    const auto tuple_id = (*table_iter_holder_)->GetTupleId();
    return pggate::ColumnWireSize(
        DataType::BINARY, /* is_null= */ false, tuple_id.size());
  }
  if (operand_idx == dockv::ReaderProjection::kNotFoundIndex) {
    return 0;
  }
  return row.ColumnWireSize(operand_idx);
}

Result<size_t> PgsqlReadAggregateProcessor::ProjectionColumnIndex(
    const dockv::ReaderProjection& projection, ColumnIdRep column_id) {
  const auto idx = projection.ColumnIdxById(ColumnId(column_id));
  SCHECK_NE(
      idx, dockv::ReaderProjection::kNotFoundIndex, InvalidArgument,
      Format("Column $0 is not present in row projection", column_id));
  return idx;
}

Result<Slice> PgsqlReadAggregateProcessor::GetSpecialColumn(ColumnIdRep column_id) {
  // Get row key and save to QLValue.
  // TODO(neil) Check if we need to append a table_id and other info to TupleID. For example, we
  // might need info to make sure the TupleId by itself is a valid reference to a specific row of
  // a valid table.
  if (column_id == static_cast<int>(PgSystemAttrNum::kYBTupleId)) {
    return (*table_iter_holder_)->GetTupleId();
  }

  return STATUS_FORMAT(InvalidArgument, "Invalid column ID: $0", column_id);
}

}  // namespace yb::docdb
