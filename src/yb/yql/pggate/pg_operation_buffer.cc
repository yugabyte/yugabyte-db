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

#include "yb/yql/pggate/pg_operation_buffer.h"

#include <string>
#include <ostream>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/common/constants.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/port.h"

#include "yb/util/lw_function.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pggate_flags.h"

namespace yb {
namespace pggate {
namespace {

docdb::PrimitiveValue NullValue(SortingType sorting) {
  using SortingType = SortingType;

  return docdb::PrimitiveValue(
      sorting == SortingType::kAscendingNullsLast || sorting == SortingType::kDescendingNullsLast
          ? docdb::ValueType::kNullHigh
          : docdb::ValueType::kNullLow);
}

std::vector<docdb::PrimitiveValue> InitKeyColumnPrimitiveValues(
    const ArenaList<LWPgsqlExpressionPB> &column_values,
    const Schema &schema,
    size_t start_idx) {
  std::vector<docdb::PrimitiveValue> result;
  size_t column_idx = start_idx;
  for (const auto& column_value : column_values) {
    const auto sorting_type = schema.column(column_idx).sorting_type();
    if (column_value.has_value()) {
      const auto& value = column_value.value();
      result.push_back(
          IsNull(value)
          ? NullValue(sorting_type)
          : docdb::PrimitiveValue::FromQLValuePB(value, sorting_type));
    } else {
      // TODO(neil) The current setup only works for CQL as it assumes primary key value must not
      // be dependent on any column values. This needs to be fixed as PostgreSQL expression might
      // require a read from a table.
      //
      // Use regular executor for now.
      QLExprExecutor executor;
      QLExprResult expr_result;
      auto expr = column_value.ToGoogleProtobuf(); // TODO(LW_PERFORM)
      auto s = executor.EvalExpr(expr, nullptr, expr_result.Writer());

      result.push_back(docdb::PrimitiveValue::FromQLValuePB(expr_result.Value(), sorting_type));
    }
    ++column_idx;
  }
  return result;
}

// Represents row id (ybctid) from the DocDB's point of view.
class RowIdentifier {
 public:
  RowIdentifier(
      const PgObjectId& table_id, const Schema& schema, const LWPgsqlWriteRequestPB& request)
      : table_id_(table_id) {
    if (request.has_ybctid_column_value()) {
      ybctid_ = request.ybctid_column_value().value().binary_value();
    } else {
      auto hashed_components = InitKeyColumnPrimitiveValues(
          request.partition_column_values(), schema, 0 /* start_idx */);
      auto range_components = InitKeyColumnPrimitiveValues(
          request.range_column_values(), schema, schema.num_hash_key_columns());
      if (hashed_components.empty()) {
        ybctid_holder_ = docdb::DocKey(std::move(range_components)).Encode().ToStringBuffer();
      } else {
        ybctid_holder_ = docdb::DocKey(request.hash_code(),
                                       std::move(hashed_components),
                                       std::move(range_components)).Encode().ToStringBuffer();
      }
      ybctid_ = Slice(static_cast<const char*>(nullptr), static_cast<size_t>(0));
    }
  }

  Slice ybctid() const {
    return ybctid_.data() ? ybctid_ : ybctid_holder_;
  }

  const PgObjectId& table_id() const {
    return table_id_;
  }

 private:
  friend bool operator==(const RowIdentifier& k1, const RowIdentifier& k2);

  PgObjectId table_id_;
  Slice ybctid_;
  std::string ybctid_holder_;
};


bool operator==(const RowIdentifier& k1, const RowIdentifier& k2) {
  return k1.table_id() == k2.table_id() && k1.ybctid() == k2.ybctid();
}

size_t hash_value(const RowIdentifier& key) {
  size_t hash = 0;
  boost::hash_combine(hash, key.table_id());
  boost::hash_combine(hash, key.ybctid());
  return hash;
}

inline bool IsTableUsedByRequest(const LWPgsqlReadRequestPB& request, const Slice& table_id) {
  return request.table_id() == table_id ||
      (request.has_index_request() && IsTableUsedByRequest(request.index_request(), table_id));
}

} // namespace

void BufferableOperations::Add(PgsqlOpPtr op, const PgObjectId& relation) {
  operations.push_back(std::move(op));
  relations.push_back(relation);
}

void BufferableOperations::Swap(BufferableOperations* rhs) {
  operations.swap(rhs->operations);
  relations.swap(rhs->relations);
}

void BufferableOperations::Clear() {
  operations.clear();
  relations.clear();
}

void BufferableOperations::Reserve(size_t capacity) {
  operations.reserve(capacity);
  relations.reserve(capacity);
}

bool BufferableOperations::empty() const {
  return operations.empty();
}

size_t BufferableOperations::size() const {
  return operations.size();
}

class PgOperationBuffer::Impl {
 public:
  explicit Impl(const Flusher& flusher)
      : flusher_(flusher) {
  }

  CHECKED_STATUS Add(const PgTableDesc& table, PgsqlWriteOpPtr op, bool transactional) {
    // Check for buffered operation related to same row.
    // If multiple operations are performed in context of single RPC second operation will not
    // see the results of first operation on DocDB side.
    // Multiple operations on same row must be performed in context of different RPC.
    // Flush is required in this case.
    RowIdentifier row_id(table.id(), table.schema(), op->write_request());
    if (PREDICT_FALSE(!keys_.insert(row_id).second)) {
      RETURN_NOT_OK(Flush());
      keys_.insert(row_id);
    }
    auto& target = (transactional ? txn_ops_ : ops_);
    if (target.empty()) {
      target.Reserve(FLAGS_ysql_session_max_batch_size);
    }
    target.Add(std::move(op), table.id());
    return keys_.size() >= FLAGS_ysql_session_max_batch_size ? Flush() : Status::OK();
  }

  CHECKED_STATUS Flush() {
    return DoFlush(make_lw_function([this](BufferableOperations ops, bool txn) {
      return ResultToStatus(VERIFY_RESULT(flusher_(std::move(ops), txn)).Get());
    }));
  }

  Result<BufferableOperations> FlushTake(
      const PgTableDesc& table, const PgsqlOp& op, bool transactional) {
    BufferableOperations result;
    if (IsFullFlushRequired(table, op)) {
      RETURN_NOT_OK(Flush());
    } else {
      RETURN_NOT_OK(DoFlush(make_lw_function(
          [this, transactional, &result](BufferableOperations ops, bool txn) -> Status {
            if (txn == transactional) {
              ops.Swap(&result);
              return Status::OK();
            }
            return ResultToStatus(VERIFY_RESULT(flusher_(std::move(ops), txn)).Get());
          })));
    }
    return result;
  }

  size_t Size() const {
    return keys_.size();
  }

  void Clear() {
    VLOG_IF(1, !keys_.empty()) << "Dropping " << keys_.size() << " pending operations";
    ops_.Clear();
    txn_ops_.Clear();
    keys_.clear();
  }

 private:
  using SyncFlusher = LWFunction<Status(BufferableOperations, bool)>;

  CHECKED_STATUS DoFlush(const SyncFlusher& flusher) {
    BufferableOperations ops;
    BufferableOperations txn_ops;
    ops_.Swap(&ops);
    txn_ops_.Swap(&txn_ops);
    keys_.clear();

    if (!ops.empty()) {
      RETURN_NOT_OK(flusher(std::move(ops), false /* transactional */));
    }
    if (!txn_ops.empty()) {
      RETURN_NOT_OK(flusher(std::move(txn_ops), true /* transactional */));
    }
    return Status::OK();
  }

  bool IsFullFlushRequired(const PgTableDesc& table, const PgsqlOp& op) const {
    return op.is_read()
        ? IsSameTableUsedByBufferedOperations(down_cast<const PgsqlReadOp&>(op).read_request())
        : keys_.find(RowIdentifier(
              table.id(), table.schema(),
              down_cast<const PgsqlWriteOp&>(op).write_request())) != keys_.end();
  }

  bool IsSameTableUsedByBufferedOperations(const LWPgsqlReadRequestPB& request) const {
    for (const auto& k : keys_) {
      if (IsTableUsedByRequest(request, k.table_id().GetYbTableId())) {
        return true;
      }
    }
    return false;
  }

  const Flusher flusher_;
  BufferableOperations ops_;
  BufferableOperations txn_ops_;
  std::unordered_set<RowIdentifier, boost::hash<RowIdentifier>> keys_;
};

PgOperationBuffer::PgOperationBuffer(const Flusher& flusher)
    : impl_(new Impl(flusher)) {
}

PgOperationBuffer::~PgOperationBuffer() = default;

Status PgOperationBuffer::Add(const PgTableDesc& table, PgsqlWriteOpPtr op, bool transactional) {
    return impl_->Add(table, std::move(op), transactional);
}

CHECKED_STATUS PgOperationBuffer::Flush() {
    return impl_->Flush();
}

Result<BufferableOperations> PgOperationBuffer::FlushTake(
    const PgTableDesc& table, const PgsqlOp& op, bool transactional) {
  return impl_->FlushTake(table, op, transactional);
}

size_t PgOperationBuffer::Size() const {
    return impl_->Size();
}

void PgOperationBuffer::Clear() {
    impl_->Clear();
}

} // namespace pggate
} // namespace yb
