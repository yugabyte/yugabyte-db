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

#pragma once

#include <functional>
#include <utility>

#include <boost/container/small_vector.hpp>

#include "yb/common/pgsql_protocol.pb.h"

#include "yb/docdb/doc_expr.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/util/operation_counter.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/write_buffer.h"

namespace yb::docdb {

YB_STRONGLY_TYPED_BOOL(IsUpsert);

bool ShouldYsqlPackRow(bool has_cotable_id);

class PgsqlWriteOperation :
    public DocOperationBase<DocOperationType::PGSQL_WRITE_OPERATION, PgsqlWriteRequestPB>,
    public DocExprExecutor {
 public:
  PgsqlWriteOperation(std::reference_wrapper<const PgsqlWriteRequestPB> request,
                      DocReadContextPtr doc_read_context,
                      const TransactionOperationContext& txn_op_context,
                      rpc::Sidecars* sidecars);

  // Initialize PgsqlWriteOperation. Content of request will be swapped out by the constructor.
  Status Init(PgsqlResponsePB* response);
  bool RequireReadSnapshot() const override {
    // For YSQL the the standard operations (INSERT/UPDATE/DELETE) will read/check the primary key.
    // We use UPSERT stmt type for specific requests when we can guarantee we can skip the read.
    return request_.stmt_type() != PgsqlWriteRequestPB::PGSQL_UPSERT;
  }

  const PgsqlWriteRequestPB& request() const { return request_; }
  PgsqlResponsePB* response() const { return response_; }

  Result<bool> HasDuplicateUniqueIndexValue(const DocOperationApplyData& data);
  Result<bool> HasDuplicateUniqueIndexValueBackward(const DocOperationApplyData& data);
  Result<bool> HasDuplicateUniqueIndexValue(
      const DocOperationApplyData& data,
      const ReadHybridTime& read_time);
  Result<HybridTime> FindOldestOverwrittenTimestamp(
      IntentAwareIterator* iter,
      const dockv::SubDocKey& sub_doc_key,
      HybridTime min_hybrid_time);

  // Execute write.
  Status Apply(const DocOperationApplyData& data) override;

 private:
  void ClearResponse() override {
    if (response_) {
      response_->Clear();
    }
  }

  // Insert, update, delete, and colocated truncate operations.
  Status ApplyInsert(
      const DocOperationApplyData& data, IsUpsert is_upsert = IsUpsert::kFalse);
  Status ApplyUpdate(const DocOperationApplyData& data);
  Status ApplyDelete(const DocOperationApplyData& data, const bool is_persist_needed);
  Status ApplyTruncateColocated(const DocOperationApplyData& data);
  Status ApplyFetchSequence(const DocOperationApplyData& data);

  Status DeleteRow(const dockv::DocPath& row_path, DocWriteBatch* doc_write_batch,
                   const ReadOperationData& read_operation_data);

  // Reading current row before operating on it.
  // Returns true if row was present.
  Result<bool> ReadRow(const DocOperationApplyData& data, dockv::PgTableRow* table_row);
  Result<bool> ReadRow(
      const DocOperationApplyData& data, const dockv::DocKey& doc_key,
      dockv::PgTableRow* table_row);

  Status PopulateResultSet(const dockv::PgTableRow* table_row);

  // Reading path to operate on.
  Status GetDocPaths(GetDocPathsMode mode,
                     DocPathsToLock *paths,
                     IsolationLevel *level) const override;

  class RowPackContext;

  template <typename Value>
  Status DoInsertColumn(
      const DocOperationApplyData& data, ColumnId column_id, const ColumnSchema& column,
      Value&& column_value, RowPackContext* pack_context);

  Status InsertColumn(
      const DocOperationApplyData& data, const PgsqlColumnValuePB& column_value,
      RowPackContext* pack_context);

  template <typename Value>
  Status DoUpdateColumn(
      const DocOperationApplyData& data, ColumnId column_id, const ColumnSchema& column,
      Value&& value, RowPackContext* pack_context);

  Status UpdateColumn(
      const DocOperationApplyData& data, const dockv::PgTableRow& table_row,
      const PgsqlColumnValuePB& column_value, dockv::PgTableRow* returning_table_row,
      qlexpr::QLExprResult* result, RowPackContext* pack_context);

  const dockv::ReaderProjection& projection() const;

  //------------------------------------------------------------------------------------------------
  // Context.
  DocReadContextPtr doc_read_context_;
  mutable std::optional<dockv::ReaderProjection> projection_;
  const TransactionOperationContext txn_op_context_;

  // Input arguments.
  PgsqlResponsePB* response_ = nullptr;

  // TODO(neil) Output arguments.
  // UPDATE, DELETE, INSERT operations should return total number of new or changed rows.

  // Doc key and encoded doc key for the primary key.
  dockv::DocKey doc_key_;
  RefCntPrefix encoded_doc_key_;

  // Rows result requested.
  rpc::Sidecars* const sidecars_;

  int64_t result_rows_ = 0;
  WriteBufferPos row_num_pos_;
  WriteBuffer* write_buffer_ = nullptr;
  const bool ysql_skip_row_lock_for_update_;
};

struct PgsqlReadOperationData {
  const ReadOperationData& read_operation_data;
  bool is_explicit_request_read_time;
  const PgsqlReadRequestPB& request;
  const DocReadContext& doc_read_context;
  const DocReadContext* index_doc_read_context;
  const TransactionOperationContext& txn_op_context;
  const YQLStorageIf& ql_storage;
  const ScopedRWOperation& pending_op;
  VectorIndexPtr vector_index;
};

class PgsqlReadOperation : public DocExprExecutor {
 public:
  // Construct and access methods.
  PgsqlReadOperation(std::reference_wrapper<const PgsqlReadOperationData> data,
                     WriteBuffer* result_buffer,
                     HybridTime* restart_read_ht)
      : data_(data), request_(data_.request), result_buffer_(result_buffer),
        restart_read_ht_(restart_read_ht) {
  }

  const PgsqlReadRequestPB& request() const { return data_.request; }
  PgsqlResponsePB& response() { return response_; }

  // Driver of the execution for READ operators for the given conditions in Protobuf request.
  // The protobuf request carries two different types of arguments.
  // - Scalar argument: The query condition is represented by one set of values. For example, each
  //   of the following scalar protobuf requests will carry one "ybctid" (ROWID).
  //     SELECT ... WHERE ybctid = y1;
  //     SELECT ... WHERE ybctid = y2;
  //     SELECT ... WHERE ybctid = y3;
  //
  // - Batch argument: The query condition is represented by many sets of values. For example, a
  //   batch protobuf will carry many ybctids.
  //     SELECT ... WHERE ybctid IN (y1, y2, y3)
  Result<size_t> Execute();

  Status GetSpecialColumn(ColumnIdRep column_id, QLValuePB* result);

 private:
  // Execute a READ operator for a given scalar argument.
  Result<std::tuple<size_t, bool>> ExecuteScalar();

  // Execute a READ operator for a given vector search.
  Result<std::tuple<size_t, bool>> ExecuteVectorSearch(
      const DocReadContext& doc_read_context, const PgVectorReadOptionsPB& options);

  // Execute a READ operator for a given batch of keys.
  template <class KeyProvider>
  Result<size_t> ExecuteBatchKeys(KeyProvider& key_provider);

  Result<std::tuple<size_t, bool>> ExecuteSample();

  Result<std::tuple<size_t, bool>> ExecuteSampleBlockBased();

  void BindReadTimeToPagingState(const ReadHybridTime& read_time);

  Status PopulateResultSet(const dockv::PgTableRow& table_row,
                           WriteBuffer *result_buffer);

  Status EvalAggregate(const dockv::PgTableRow& table_row);

  Status PopulateAggregate(WriteBuffer *result_buffer);

  // Checks whether we have processed enough rows for a page and sets the appropriate paging
  // state in the response object.
  Result<bool> SetPagingState(
      YQLRowwiseIteratorIf* iter, const Schema& schema, const ReadHybridTime& read_time);

  Result<size_t> ExecuteVectorLSMSearch(const PgVectorReadOptionsPB& options);

  void InitTargetEncoders(
      const google::protobuf::RepeatedPtrField<PgsqlExpressionPB>& targets,
      const dockv::PgTableRow& table_row);

  //------------------------------------------------------------------------------------------------
  const PgsqlReadOperationData& data_;
  const PgsqlReadRequestPB& request_;
  WriteBuffer* const result_buffer_;
  HybridTime* const restart_read_ht_;

  boost::container::small_vector<dockv::PgWireEncoderEntry, 0x10> target_encoders_;
  PgsqlResponsePB response_;
  YQLRowwiseIteratorIf::UniPtr table_iter_;
  YQLRowwiseIteratorIf::UniPtr index_iter_;
  uint64_t scanned_table_rows_ = 0;
  uint64_t scanned_index_rows_ = 0;
  Status delayed_failure_;
};

Status GetIntents(
    const PgsqlReadRequestPB& request, const Schema& schema, IsolationLevel level,
    LWKeyValueWriteBatchPB* out);

class PgsqlLockOperation :
    public DocOperationBase<DocOperationType::PGSQL_LOCK_OPERATION, PgsqlLockRequestPB> {
 public:
  PgsqlLockOperation(std::reference_wrapper<const PgsqlLockRequestPB> request,
                     const TransactionOperationContext& txn_op_context);

  bool RequireReadSnapshot() const override {
    return false;
  }

  const PgsqlLockRequestPB& request() const { return request_; }
  PgsqlResponsePB* response() const { return response_; }

  // Init doc_key_ and encoded_doc_key_.
  Status Init(PgsqlResponsePB* response, const DocReadContextPtr& doc_read_context);

  Status Apply(const DocOperationApplyData& data) override;

  // Reading path to operate on.
  Status GetDocPaths(GetDocPathsMode mode,
                     DocPathsToLock *paths,
                     IsolationLevel *level) const override;

  std::string ToString() const override;

  dockv::IntentTypeSet GetIntentTypes(IsolationLevel isolation_level) const override;

 private:
  void ClearResponse() override;

  Result<bool> LockExists(const DocOperationApplyData& data);

  const TransactionOperationContext txn_op_context_;

  // Input arguments.
  PgsqlResponsePB* response_ = nullptr;

  // The key of the advisory lock to be locked.
  dockv::DocKey doc_key_;
  RefCntPrefix encoded_doc_key_;
};

}  // namespace yb::docdb
