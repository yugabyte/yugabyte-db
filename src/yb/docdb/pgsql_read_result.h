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

#pragma once

#include <optional>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "yb/docdb/doc_expr.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/util/enums.h"

namespace yb {

class WriteBuffer;

namespace docdb {

// What a processor's ProcessMatchedRow returns. ExecuteScalar and ExecuteBatchKeys stop or page
// on it, and SetPagingState uses the result to decide where the next request starts.
// NotReached - iteration finished at the upper bound or the end of the tablet
// Reached     - a limit was reached and current record made it into result
// Exceeded    - a limit was reached and current record did not make it into result (no room)
YB_DEFINE_ENUM(FetchLimit, (kNotReached)(kReached)(kExceeded));

// ------------------------------------------------------------------------------------------------
// PgsqlReadResultProcessor
// ------------------------------------------------------------------------------------------------

// Shared request/buffer/limit state. Constructor (and limit gflags) stay in the .cc.
class PgsqlReadResultProcessor {
 public:
  size_t emitted_row_count_limit() const { return emitted_row_count_limit_; }
  uint64_t response_size_limit() const { return response_size_limit_; }

 protected:
  PgsqlReadResultProcessor(const PgsqlReadRequestMsg& request, WriteBuffer* result_buffer);

  const PgsqlReadRequestMsg& request_;
  WriteBuffer* const result_buffer_;
  const size_t emitted_row_count_limit_;
  const uint64_t response_size_limit_;
  size_t emitted_rows_ = 0;
};

// ------------------------------------------------------------------------------------------------
// PgsqlReadRowsProcessor
// ------------------------------------------------------------------------------------------------

// Writes each matched row to the result buffer, in request targets[] order.
class PgsqlReadRowsProcessor : public PgsqlReadResultProcessor {
 public:
  PgsqlReadRowsProcessor(
      const PgsqlReadRequestMsg& request, WriteBuffer* result_buffer,
      YQLRowwiseIteratorIf::UniPtr* table_iter_holder);

  Status Init(const dockv::ReaderProjection&) { return Status::OK(); }
  Result<FetchLimit> ProcessMatchedRow(const dockv::PgTableRow& row);
  // Returns the number of result rows written to the buffer.
  Result<size_t> Finish();

 private:
  Status WriteRow(const dockv::PgTableRow& table_row);
  void InitTargetEncoders(const dockv::PgTableRow& table_row);

  YQLRowwiseIteratorIf::UniPtr* const table_iter_holder_;
  boost::container::small_vector<dockv::PgWireEncoderEntry, 0x10> target_encoders_;
};

// ------------------------------------------------------------------------------------------------
// PgsqlReadAggregateProcessor
// ------------------------------------------------------------------------------------------------

// Handles both plain and grouped aggregate pushdown. Empty group-key list means plain aggregate
// (key never changes); non-empty means grouped. has_active_group_ distinguishes "folding started"
// from empty active_group_keys_ in plain mode.
class PgsqlReadAggregateProcessor : public PgsqlReadResultProcessor, public DocExprExecutor {
 public:
  PgsqlReadAggregateProcessor(
      const PgsqlReadRequestMsg& request, WriteBuffer* result_buffer,
      YQLRowwiseIteratorIf::UniPtr* table_iter_holder);

  Status Init(const dockv::ReaderProjection& projection);
  Result<FetchLimit> ProcessMatchedRow(const dockv::PgTableRow& row);
  // Returns the number of result rows written to the buffer.
  Result<size_t> Finish();

 private:
  Status ClassifyFixedWireSizes(const dockv::ReaderProjection& projection);
  bool GroupKeyChanged(const dockv::PgTableRow& row) const;
  Status EmitActiveGroup();
  Status BuildGroupKeyValues(const dockv::PgTableRow& row);
  // buffer + fixed key/aggregate sizes + measured variable keys (already in
  // active_keys_wire_size_) + max(aggr, row input, int64 floor) per variable aggregate.
  // max (not sum) keeps the bound tight for the first fold while remaining opcode-agnostic.
  Result<size_t> MeasureProjectedUpperBound(const dockv::PgTableRow& row);
  void ClearActiveGroup();
  Status EvalTargets(const dockv::PgTableRow& row);

  Status CacheGroupKeyColumnIndexes(const dockv::ReaderProjection& projection);
  // Non-null wire size of one group key when its type is fixed-width; nullopt otherwise.
  // Nulls encode smaller, so the non-null size is a safe overestimate.
  Result<std::optional<size_t>> FixedKeyWireSize(
      const dockv::ReaderProjection& projection, size_t idx);
  Status CacheAggregateOperandColumnIndexes(const dockv::ReaderProjection& projection);
  // COUNT/SUM are fixed-width regardless of operand. MIN/MAX are fixed only for a
  // fixed-width column operand; a system column or non-column operand is variable.
  Result<bool> IsFixedWidthAggregateTarget(
      const dockv::ReaderProjection& projection, const PgsqlExpressionMsg& target, size_t idx);
  // Wire size of the aggregate's row operand when it is a column reference; otherwise 0.
  // The int64 floor in MeasureProjectedUpperBound covers fixed-width COUNT/SUM cases.
  Result<size_t> MeasureAggregateRowInputWireSize(
      const PgsqlExpressionMsg& expr, const dockv::PgTableRow& row, size_t operand_idx);
  static Result<size_t> ProjectionColumnIndex(
      const dockv::ReaderProjection& projection, ColumnIdRep column_id);

  Result<Slice> GetSpecialColumn(ColumnIdRep column_id) override;

  YQLRowwiseIteratorIf::UniPtr* const table_iter_holder_;
  std::vector<const PgsqlExpressionMsg*> group_key_targets_;
  std::vector<const PgsqlExpressionMsg*> aggregate_targets_;
  std::vector<size_t> group_key_column_idxs_;
  // Sum of non-null sizes of the fixed-width group keys. variable_key_idxs_ indexes
  // group_key_targets_ / active_group_keys_ for the rest; empty means every key is fixed.
  size_t fixed_keys_wire_size_ = 0;
  std::vector<size_t> variable_key_idxs_;
  // Indexed the same as aggregate_targets_. Projection slot of a plain-column operand;
  // kNotFoundIndex when the operand isn't a projected column (COUNT(*)'s dummy constant,
  // ybctid, or another system column).
  std::vector<size_t> aggregate_operand_idxs_;
  // fixed_count * int64 wire floor. variable_aggregate_idxs_ indexes aggregate_targets_
  // and aggr_result_; empty means every aggregate is fixed.
  size_t fixed_aggregates_wire_size_ = 0;
  std::vector<size_t> variable_aggregate_idxs_;
  bool has_active_group_ = false;
  // Capacity is retained across groups for reuse.
  std::vector<QLValuePB> active_group_keys_;
  size_t active_keys_wire_size_ = 0;
};

}  // namespace docdb
}  // namespace yb
