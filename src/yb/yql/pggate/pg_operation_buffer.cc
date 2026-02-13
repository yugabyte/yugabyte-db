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

#include "yb/yql/pggate/pg_operation_buffer.h"

#include <string>
#include <ostream>
#include <unordered_map>
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

#include "yb/yql/pggate/pg_flush_debug_context.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pggate_flags.h"

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

using RowKeys = std::unordered_map<RowIdentifier, uint64_t, boost::hash<RowIdentifier>>;

struct InFlightOperation {
  RowKeys keys;
  FlushFuture future;
  size_t op_count;

  explicit InFlightOperation(RowKeys&& keys, FlushFuture&& future_, size_t op_count_)
      : keys(std::move(keys)), future(std::move(future_)), op_count(op_count_) {}
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
  Impl(Flusher&& flusher, const BufferingSettings& buffering_settings)
      : flusher_(std::move(flusher)),
        buffering_settings_(buffering_settings) {}

  Status Add(const PgTableDesc& table, PgsqlWriteOpPtr op, bool transactional) {
    return ClearOnError(DoAdd(table, std::move(op), transactional));
  }

  Status Flush(const PgFlushDebugContext& dbg_ctx) {
    return ClearOnError(DoFlush(dbg_ctx));
  }

  Result<BufferableOperations> Take(bool transactional, const PgFlushDebugContext& dbg_ctx) {
    return ClearOnError(DoTake(transactional, dbg_ctx));
  }

  size_t PendingOpsCount() const {
    return ops_.Size() + txn_ops_.Size();
  }

  size_t Size() const {
    return PendingOpsCount() + InFlightOpsCount();
  }

  bool IsEmpty() const {
    return keys_.empty() && in_flight_ops_.empty();
  }

  void Clear() {
    const auto op_count = PendingOpsCount();
    VLOG_IF(1, op_count) << "Dropping " << op_count << " pending operations";
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

  // Inserts a (key=row-key, value=op) pair into an in-memory map that tracks
  // pending write operations. This function returns true if the operation can
  // be enqueued immediately, or false if the buffer is required to be flushed
  // before enqueuing the operation.
  bool InsertKey(const RowIdentifier& key, LWPgsqlWriteRequestPB& write_request) {
    const auto stmt_type = write_request.stmt_type();
    uint64_t stmt_type_mask = (1u << stmt_type);

    bool is_index_row_rewrite_optimization_allowed = false;
    if (PREDICT_TRUE(FLAGS_ysql_optimize_index_row_rewrites)) {
      switch (stmt_type) {
        case PgsqlWriteRequestPB::PGSQL_INSERT:
        case PgsqlWriteRequestPB::PGSQL_UPDATE:
        case PgsqlWriteRequestPB::PGSQL_UPSERT:
          is_index_row_rewrite_optimization_allowed = true;
          break;
        case PgsqlWriteRequestPB::PGSQL_DELETE:
        case PgsqlWriteRequestPB::PGSQL_TRUNCATE_COLOCATED:
        case PgsqlWriteRequestPB::PGSQL_FETCH_SEQUENCE:
          is_index_row_rewrite_optimization_allowed = false;
          break;
        default:
          LOG(DFATAL) << "Unexpected stmt_type: " << write_request.ShortDebugString();
          return false;
      }
    }

    const auto it = keys_.find(key);
    if (!is_index_row_rewrite_optimization_allowed || it == keys_.end()) {
      return keys_.emplace(key, stmt_type_mask).second;
    }

    constexpr auto kDeleteMask = (1u << PgsqlWriteRequestPB::PGSQL_DELETE);
    constexpr auto kUpsertMask = (1u << PgsqlWriteRequestPB::PGSQL_UPSERT);

    // An operation corresponding to the same key is already enqueued in the buffer.
    // If the enqueued operation is anything other than a DELETE, then we cannot enqueue
    // the incoming operation as it would insert a new row corresponding to the same key.
    if (it->second != kDeleteMask) {
      return false;
    }

    // If the operation is an INSERT and a DELETE has already been enqueued for the same key,
    // there is no need to perform the uniqueness check associated with the INSERT. Therefore,
    // the INSERT can be downgraded to an UPSERT.
    if (stmt_type == PgsqlWriteRequestPB::PGSQL_INSERT) {
      write_request.set_stmt_type(PgsqlWriteRequestPB::PGSQL_UPSERT);
      stmt_type_mask = kUpsertMask;
    }

    it->second |= stmt_type_mask;
    return true;
  }

  void EraseKey(const RowIdentifier& key, PgsqlWriteRequestPB::PgsqlStmtType stmt_type) {
    if (auto it = keys_.find(key); it == keys_.end()) {
      it->second &= ~(1u << stmt_type);

      if (PREDICT_TRUE(!it->second)) {
        keys_.erase(it);
      }
    }
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

    auto& write_request = op->write_request();
    const auto stmt_type = write_request.stmt_type();
    const bool need_transaction = op->need_transaction();
    const auto& packed_rows = write_request.packed_rows();
    const auto& table_relfilenode_id = table.relfilenode_id();

    const size_t payload = write_request.SerializedSize();
    const size_t max_size = FLAGS_rpc_max_message_size *
                            FLAGS_max_buffer_size_to_rpc_limit_ratio;

    if (PendingOpsCount() % buffering_settings_.multiple == 0 &&
        total_bytes_in_buffer_ + payload >= max_size) {
      // The data size in buffer exceeds the limit, need to flush buffer.
      // For copy on colocated tables that use fast-path transaction, we perform the check only
      // when the op comes from a row rather than an index. This makes the check very simple,
      // but it should be sufficient to catch common cases where the average row size is large,
      // causing the buffer size to exceed the RPC limit with the default batch size.
      RETURN_NOT_OK(SendBuffer(PgFlushDebugContext::BufferFull(total_bytes_in_buffer_ + payload)));
    }

    if (!packed_rows.empty()) {
      // Optimistically assume that we don't have conflicts with existing operations.
      bool has_conflict = false;
      for (auto it = packed_rows.begin(); it != packed_rows.end(); it += 2) {
        if (PREDICT_FALSE(!InsertKey(RowIdentifier(table_relfilenode_id, *it), write_request))) {
          RETURN_NOT_OK(CheckDuplicateInsertForFastPathCopy(table, stmt_type, need_transaction));
          while (it != packed_rows.begin()) {
            it -= 2;
            EraseKey(RowIdentifier(table_relfilenode_id, *it), stmt_type);
          }
          // Have to flush because already have operations for the same key.
          has_conflict = true;
          break;
        }
      }
      if (has_conflict) {
        RETURN_NOT_OK(Flush(PgFlushDebugContext::ConflictingKeyWrite(
            table.pg_table_id().object_oid, table.table_name().table_name())));
        for (auto it = packed_rows.begin(); it != packed_rows.end(); it += 2) {
          SCHECK(InsertKey(RowIdentifier(table_relfilenode_id, *it), write_request),
                 IllegalState, "Unable to insert key: $0", packed_rows);
        }
      }
    } else {
      RowIdentifier row_id(table_relfilenode_id, table.schema(), write_request);
      if (PREDICT_FALSE(!InsertKey(row_id, write_request))) {
        RETURN_NOT_OK(CheckDuplicateInsertForFastPathCopy(table, stmt_type, need_transaction));
        RETURN_NOT_OK(Flush(PgFlushDebugContext::ConflictingKeyWrite(
            table.pg_table_id().object_oid, table.table_name().table_name(), row_id.ybctid())));
        InsertKey(row_id, write_request);
      } else {
        // Prevent conflicts on in-flight operations which use current row_id.
        for (auto i = in_flight_ops_.begin(); i != in_flight_ops_.end(); ++i) {
          if (i->keys.contains(row_id)) {
            RETURN_NOT_OK(EnsureCompleted(i - in_flight_ops_.begin() + 1));
            break;
          }
        }
      }
    }
    target.Add(std::move(op), table);
    total_bytes_in_buffer_ += write_request.SerializedSize();

    if (PendingOpsCount() >= buffering_settings_.max_batch_size) {
      return SendBuffer(PgFlushDebugContext::BufferFull(total_bytes_in_buffer_));
    }

    return Status::OK();
  }

  Status DoFlush(const PgFlushDebugContext& dbg_ctx) {
    RETURN_NOT_OK(SendBuffer(dbg_ctx));
    return EnsureAllCompleted();
  }

  Result<BufferableOperations> DoTake(bool transactional, const PgFlushDebugContext& dbg_ctx) {
    BufferableOperations result;
    RETURN_NOT_OK(SendBuffer(dbg_ctx, make_lw_function(
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
      ops_count += op.op_count;
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

  Status SendBuffer(const PgFlushDebugContext& dbg_ctx) {
    return SendBufferImpl(nullptr /* interceptor */, dbg_ctx);
  }

  Status SendBuffer(const PgFlushDebugContext& dbg_ctx, const SendInterceptor& interceptor) {
    return SendBufferImpl(&interceptor, dbg_ctx);
  }

  Status SendBufferImpl(const SendInterceptor* interceptor, const PgFlushDebugContext& dbg_ctx) {
    if (keys_.empty()) {
      return Status::OK();
    }

    RSTATUS_DCHECK(
        ops_.Empty() || txn_ops_.Empty(), IllegalState,
        "Operations buffer cannot have both transactional and non-transactional ops enqueued");

    auto* ops_source = &txn_ops_;
    bool is_transactional = true;
    if (ops_source->Empty()) {
      ops_source = &ops_;
      is_transactional = false;
    }

    BufferableOperations ops = std::exchange(*ops_source, {});
    RowKeys keys = std::exchange(keys_, {});
    total_bytes_in_buffer_ = 0;

    return SendOperations(
        interceptor, std::move(ops), std::move(keys), is_transactional, dbg_ctx);
  }

  Status SendOperations(
      const SendInterceptor* interceptor, BufferableOperations ops, RowKeys&& keys,
      bool transactional, const PgFlushDebugContext& dbg_ctx) {
    const auto ops_count = ops.Size();
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
        space_required -= in_flight_ops_.front().op_count;
        RETURN_NOT_OK(EnsureCompleted(1));
      }
      in_flight_ops_.push_back(InFlightOperation{
          std::move(keys),
          VERIFY_RESULT(flusher_(std::move(ops), transactional, dbg_ctx)), ops_count});
    }

    return Status::OK();
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

Status PgOperationBuffer::Flush(const PgFlushDebugContext& dbg_ctx) {
    return impl_->Flush(dbg_ctx);
}

Result<BufferableOperations> PgOperationBuffer::Take(
    bool transactional, const PgFlushDebugContext& dbg_ctx) {
  return impl_->Take(transactional, dbg_ctx);
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
