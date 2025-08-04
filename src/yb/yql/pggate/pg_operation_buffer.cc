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

#include <boost/circular_buffer.hpp>
#include <boost/container/small_vector.hpp>

#include "yb/common/constants.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/qlexpr/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/primitive_value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/port.h"

#include "yb/util/atomic.h"
#include "yb/util/lw_function.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_tabledesc.h"

DECLARE_uint64(rpc_max_message_size);
DECLARE_double(max_buffer_size_to_rpc_limit_ratio);

namespace yb::pggate {
namespace {

dockv::KeyEntryValue NullValue(SortingType sorting) {
  using SortingType = SortingType;

  return dockv::KeyEntryValue(
      sorting == SortingType::kAscendingNullsLast || sorting == SortingType::kDescendingNullsLast
          ? dockv::KeyEntryType::kNullHigh
          : dockv::KeyEntryType::kNullLow);
}

dockv::KeyEntryValues InitKeyColumnPrimitiveValues(
    const ArenaList<LWPgsqlExpressionPB> &column_values,
    const Schema &schema,
    size_t start_idx) {
  dockv::KeyEntryValues result;
  size_t column_idx = start_idx;
  for (const auto& column_value : column_values) {
    const auto sorting_type = schema.column(column_idx).sorting_type();
    if (column_value.has_value()) {
      const auto& value = column_value.value();
      result.push_back(
          IsNull(value)
          ? NullValue(sorting_type)
          : dockv::KeyEntryValue::FromQLValuePB(value, sorting_type));
    } else {
      // TODO(neil) The current setup only works for CQL as it assumes primary key value must not
      // be dependent on any column values. This needs to be fixed as PostgreSQL expression might
      // require a read from a table.
      //
      // Use regular executor for now.
      LOG(FATAL) << "Expression instead of value";
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
        ybctid_holder_ = dockv::DocKey(std::move(range_components)).Encode().ToStringBuffer();
      } else {
        ybctid_holder_ = dockv::DocKey(request.hash_code(),
                                       std::move(hashed_components),
                                       std::move(range_components)).Encode().ToStringBuffer();
      }
      ybctid_ = Slice(static_cast<const char*>(nullptr), static_cast<size_t>(0));
    }
  }

  RowIdentifier(const PgObjectId& table_id, Slice ybctid) : table_id_(table_id), ybctid_(ybctid) {
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

using RowKeys = std::unordered_set<RowIdentifier, boost::hash<RowIdentifier>>;

struct InFlightOperation {
  RowKeys keys;
  FlushFuture future;

  explicit InFlightOperation(FlushFuture&& future_)
      : future(std::move(future_)) {}
};

using InFlightOps = boost::circular_buffer_space_optimized<InFlightOperation,
                                                           std::allocator<InFlightOperation>>;

void EnsureCapacity(InFlightOps* in_flight_ops, BufferingSettings buffering_settings) {
  size_t capacity = in_flight_ops->capacity();
  size_t num_buffers_needed =
    (buffering_settings.max_in_flight_operations / buffering_settings.max_batch_size) + 1;
  // Change the capacity of the buffer if needed. This will only be different when
  // buffering_settings_ is changed in StartOperationsBuffering(), or right after construction
  // of the buffer. As such, we don't have to worry about set_capacity() dropping any
  // InFlightOperations, since there are guaranteed to be none.
  if (capacity < num_buffers_needed) {
    in_flight_ops->set_capacity(num_buffers_needed);
  }
}

} // namespace

void BufferableOperations::Add(PgsqlOpPtr&& op, const PgTableDesc& table) {
  operations_.push_back(std::move(op));
  relations_.push_back(table.pg_table_id());
}

void BufferableOperations::Swap(BufferableOperations* rhs) {
  operations_.swap(rhs->operations_);
  relations_.swap(rhs->relations_);
}

void BufferableOperations::Clear() {
  operations_.clear();
  relations_.clear();
}

void BufferableOperations::Reserve(size_t capacity) {
  operations_.reserve(capacity);
  relations_.reserve(capacity);
}

bool BufferableOperations::Empty() const {
  return operations_.empty();
}

size_t BufferableOperations::Size() const {
  return operations_.size();
}

void BufferableOperations::MoveTo(PgsqlOps& operations, PgObjectIds& relations) && {
  operations = std::move(operations_);
  relations = std::move(relations_);
}

std::pair<BufferableOperations, BufferableOperations> Split(
    BufferableOperations&& ops, size_t index) {
  if (index >= ops.Size()) {
    return {std::move(ops), {}};
  }
  if (!index) {
    return {{}, std::move(ops)};
  }
  BufferableOperations tail;
  tail.Reserve(ops.Size() - index);
  auto& operations = ops.operations_;
  auto& relations = ops.relations_;
  auto rel_it = relations.begin() + index;
  for (auto op_it = operations.begin() + index; op_it != ops.operations_.end(); ++op_it, ++rel_it) {
    tail.operations_.push_back(std::move(*op_it));
    tail.relations_.push_back(*rel_it);
  }
  operations.resize(index);
  relations.resize(index);
  return {std::move(ops), std::move(tail)};
}

class PgOperationBuffer::Impl {
 public:
  Impl( Flusher&& flusher, const BufferingSettings& buffering_settings)
      : flusher_(std::move(flusher)),
        buffering_settings_(buffering_settings) {}

  Status Add(const PgTableDesc& table, PgsqlWriteOpPtr op, bool transactional) {
    return ClearOnError(DoAdd(table, std::move(op), transactional));
  }

  Status Flush() {
    return ClearOnError(DoFlush());
  }

  Result<BufferableOperations> Take(bool transactional) {
    return ClearOnError(DoTake(transactional));
  }

  size_t Size() const {
    return keys_.size() + InFlightOpsCount();
  }

  bool IsEmpty() const {
    return keys_.empty() && in_flight_ops_.empty();
  }

  void Clear() {
    VLOG_IF(1, !keys_.empty()) << "Dropping " << keys_.size() << " pending operations";
    ops_.Clear();
    txn_ops_.Clear();
    keys_.clear();
    // Clearing of in_flight_ops_ might get blocked on future::get()
    // (see PerformFuture::~PerformFuture() for details). And due to the #12884 issue
    // in_flight_ops_'s destructor might get called. In this case it is safer to keep
    // in_flight_ops_ empty before blocking on future::get().
    // Remove this code after fixing #12884.
    boost::container::small_vector<InFlightOperation, 16> in_flight_ops;
    in_flight_ops.reserve(in_flight_ops_.size());
    for (auto& i : in_flight_ops_) {
      in_flight_ops.push_back(std::move(i));
    }
    in_flight_ops_.clear();
  }

 private:
  template<class Res>
  Res ClearOnError(Res res) {
    if (!res.ok()) {
      Clear();
    }
    return res;
  }

  Status CheckDuplicateInsertForFastPathCopy(const PgTableDesc& table,
                                             PgsqlWriteRequestPB::PgsqlStmtType stmt_type,
                                             bool need_transaction) {
    /**
     * In case of fast-path COPY, if there is a duplicate INSERT to a unique index, instead of
     * adding the operation to the next buffer (say B2), we want to error out the current
     * buffer (say B1) too. This is because if we don't do so, the main table row with this
     * duplicate unique index value would be committed as part of B1 without the unique index row.
     * This would lead to index inconsistency. To avoid this, we error out early for INSERT
     * operations that are not transactional. We use !need_transaction as a proxy for checking if
     * we are in a fast-path COPY. This condition is not full-proof because in future we might have
     * fast path operations that do a DELETE followed by an INSERT on the same key (think of an
     * update of the index key columns) and throwing a duplicate key error would be incorrect in
     * that case. However, currently, only fast-path COPY fits this condition, so it is okay to do
     * so for now.
     */
    if (!need_transaction && stmt_type == PgsqlWriteRequestPB::PGSQL_INSERT) {
      static const auto dk_status =
        STATUS(AlreadyPresent, PgsqlError(YBPgErrorCode::YB_PG_UNIQUE_VIOLATION));
      auto status = dk_status.CloneAndAddErrorCode(RelationOid(table.pg_table_id().object_oid));
      LOG(INFO) << "duplicate key error:=" << status;
      return status;
    }
    return Status::OK();
  }

  Status DoAdd(const PgTableDesc& table, PgsqlWriteOpPtr op, bool transactional) {
    // Check for buffered operation related to same row.
    // If multiple operations are performed in context of single RPC second operation will not
    // see the results of first operation on DocDB side.
    // Multiple operations on same row must be performed in context of different RPC.
    // Flush is required in this case.
    auto& target = transactional ? txn_ops_ : ops_;
    if (target.Empty()) {
      target.Reserve(buffering_settings_.max_batch_size);
    }
    DCHECK_EQ(buffering_settings_.max_batch_size % buffering_settings_.multiple, 0);

    const auto& write_request = op->write_request();
    const auto stmt_type = write_request.stmt_type();
    const bool need_transaction = op->need_transaction();
    const auto& packed_rows = write_request.packed_rows();
    const auto& table_relfilenode_id = table.relfilenode_id();

    const size_t payload = write_request.SerializedSize();
    const size_t max_size = GetAtomicFlag(&FLAGS_rpc_max_message_size) *
                            FLAGS_max_buffer_size_to_rpc_limit_ratio;
    if (keys_.size() % buffering_settings_.multiple == 0 &&
        total_bytes_in_buffer_ + payload >= max_size) {
      // The data size in buffer exceeds the limit, need to flush buffer.
      // For copy on colocated tables that use fast-path transaction, we perform the check only
      // when the op comes from a row rather than an index. This makes the check very simple,
      // but it should be sufficient to catch common cases where the average row size is large,
      // causing the buffer size to exceed the RPC limit with the default batch size.
      RETURN_NOT_OK(SendBuffer());
    }

    if (!packed_rows.empty()) {
      // Optimistically assume that we don't have conflicts with existing operations.
      bool has_conflict = false;
      for (auto it = packed_rows.begin(); it != packed_rows.end(); it += 2) {
        if (PREDICT_FALSE(!keys_.insert(RowIdentifier(table_relfilenode_id, *it)).second)) {
          RETURN_NOT_OK(CheckDuplicateInsertForFastPathCopy(table, stmt_type, need_transaction));
          while (it != packed_rows.begin()) {
            it -= 2;
            keys_.erase(RowIdentifier(table_relfilenode_id, *it));
          }
          // Have to flush because already have operations for the same key.
          has_conflict = true;
          break;
        }
      }
      if (has_conflict) {
        RETURN_NOT_OK(Flush());
        for (auto it = packed_rows.begin(); it != packed_rows.end(); it += 2) {
          SCHECK(keys_.insert(RowIdentifier(table_relfilenode_id, *it)).second, IllegalState,
                 "Unable to insert key: $0", packed_rows);
        }
      }
    } else {
      RowIdentifier row_id(table_relfilenode_id, table.schema(), write_request);
      if (PREDICT_FALSE(!keys_.insert(row_id).second)) {
        RETURN_NOT_OK(CheckDuplicateInsertForFastPathCopy(table, stmt_type, need_transaction));
        RETURN_NOT_OK(Flush());
        keys_.insert(row_id);
      } else {
        // Prevent conflicts on in-flight operations which use current row_id.
        for (auto i = in_flight_ops_.begin(); i != in_flight_ops_.end(); ++i) {
          if (i->keys.find(row_id) != i->keys.end()) {
            RETURN_NOT_OK(EnsureCompleted(i - in_flight_ops_.begin() + 1));
            break;
          }
        }
      }
    }
    target.Add(std::move(op), table);
    total_bytes_in_buffer_ += write_request.SerializedSize();
    return keys_.size() >= buffering_settings_.max_batch_size ? SendBuffer() : Status::OK();
  }

  Status DoFlush() {
    RETURN_NOT_OK(SendBuffer());
    return EnsureAllCompleted();
  }

  Result<BufferableOperations> DoTake(bool transactional) {
    BufferableOperations result;
    RETURN_NOT_OK(SendBuffer(make_lw_function(
        [transactional, &result](BufferableOperations* ops, bool txn) {
          if (txn == transactional) {
            ops->Swap(&result);
            return true;
          }
          return false;
        })));
    RETURN_NOT_OK(EnsureAllCompleted());
    return result;
  }

  size_t InFlightOpsCount() const {
    size_t ops_count = 0;
    for (const auto& op : in_flight_ops_) {
      ops_count += op.keys.size();
    }
    return ops_count;
  }

  Status EnsureAllCompleted() {
    return EnsureCompleted(in_flight_ops_.size());
  }

  Status EnsureCompleted(size_t count) {
    for (; count && !in_flight_ops_.empty(); --count) {
      RETURN_NOT_OK(in_flight_ops_.front().future.Get());
      in_flight_ops_.pop_front();
    }
    return Status::OK();
  }

  using SendInterceptor = LWFunction<bool(BufferableOperations*, bool)>;

  Status SendBuffer() {
    return SendBufferImpl(nullptr /* interceptor */);
  }

  Status SendBuffer(const SendInterceptor& interceptor) {
    return SendBufferImpl(&interceptor);
  }

  Status SendBufferImpl(const SendInterceptor* interceptor) {
    if (keys_.empty()) {
      return Status::OK();
    }
    BufferableOperations ops;
    BufferableOperations txn_ops;
    RowKeys keys;
    ops_.Swap(&ops);
    txn_ops_.Swap(&txn_ops);
    keys_.swap(keys);
    total_bytes_in_buffer_ = 0;

    const auto ops_count = keys.size();
    bool ops_sent = VERIFY_RESULT(SendOperations(
      interceptor, std::move(txn_ops), true /* transactional */, ops_count));
    ops_sent = VERIFY_RESULT(SendOperations(
      interceptor, std::move(ops),
      false /* transactional */, ops_sent ? 0 : ops_count)) || ops_sent;
    if (ops_sent) {
      in_flight_ops_.back().keys = std::move(keys);
    }
    return Status::OK();
  }

  Result<bool> SendOperations(const SendInterceptor* interceptor,
                              BufferableOperations ops,
                              bool transactional,
                              size_t ops_count) {
    if (!ops.Empty() && !(interceptor && (*interceptor)(&ops, transactional))) {
      EnsureCapacity(&in_flight_ops_, buffering_settings_);
      // In case max_in_flight_operations < max_batch_size, the number of in-flight operations will
      // be equal to max_batch_size after sending single buffer. So use max of these values for
      // actual_max_in_flight_operations.
      const auto actual_max_in_flight_operations = std::max(
          buffering_settings_.max_in_flight_operations,
          buffering_settings_.max_batch_size);
      int64_t space_required = (InFlightOpsCount() + ops_count) - actual_max_in_flight_operations;
      while (!in_flight_ops_.empty() &&
             (space_required > 0 || in_flight_ops_.front().future.Ready())) {
        space_required -= in_flight_ops_.front().keys.size();
        RETURN_NOT_OK(EnsureCompleted(1));
      }
      in_flight_ops_.push_back(
          InFlightOperation(VERIFY_RESULT(flusher_(std::move(ops), transactional))));
      return true;
    }
    return false;
  }

  Flusher flusher_;
  const BufferingSettings& buffering_settings_;
  BufferableOperations ops_;
  BufferableOperations txn_ops_;
  RowKeys keys_;
  uint32_t total_bytes_in_buffer_ = 0;
  InFlightOps in_flight_ops_;
};

PgOperationBuffer::PgOperationBuffer(
    Flusher&& flusher, const BufferingSettings& buffering_settings)
    : impl_(new Impl(std::move(flusher), buffering_settings)) {}

PgOperationBuffer::~PgOperationBuffer() = default;

Status PgOperationBuffer::Add(const PgTableDesc& table, PgsqlWriteOpPtr op, bool transactional) {
    return impl_->Add(table, std::move(op), transactional);
}

Status PgOperationBuffer::Flush() {
    return impl_->Flush();
}

Result<BufferableOperations> PgOperationBuffer::Take(bool transactional) {
  return impl_->Take(transactional);
}

size_t PgOperationBuffer::Size() const {
  return impl_->Size();
}

void PgOperationBuffer::Clear() {
  impl_->Clear();
}

bool PgOperationBuffer::IsEmpty() const {
  return impl_->IsEmpty();
}

} // namespace yb::pggate
