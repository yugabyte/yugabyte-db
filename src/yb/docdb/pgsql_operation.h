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
                      rpc::Sidecars* sidecars)
      : DocOperationBase(request),
        doc_read_context_(std::move(doc_read_context)),
        txn_op_context_(txn_op_context),
        sidecars_(sidecars) {
  }

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
  Result<bool> ReadColumns(const DocOperationApplyData& data, dockv::PgTableRow* table_row);

  Status PopulateResultSet(const dockv::PgTableRow* table_row);

  // Reading path to operate on.
  Status GetDocPaths(GetDocPathsMode mode,
                     DocPathsToLock *paths,
                     IsolationLevel *level) const override;

  class RowPackContext;

  Status InsertColumn(
      const DocOperationApplyData& data, const PgsqlColumnValuePB& column_value,
      RowPackContext* pack_context);

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
};

class PgsqlReadOperation : public DocExprExecutor {
 public:
  // Construct and access methods.
  PgsqlReadOperation(const PgsqlReadRequestPB& request,
                     const TransactionOperationContext& txn_op_context)
      : request_(request), txn_op_context_(txn_op_context) {
  }

  const PgsqlReadRequestPB& request() const { return request_; }
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
  Result<size_t> Execute(const YQLStorageIf& ql_storage,
                         const ReadOperationData& read_operation_data,
                         bool is_explicit_request_read_time,
                         const DocReadContext& doc_read_context,
                         const DocReadContext* index_doc_read_context,
                         std::reference_wrapper<const ScopedRWOperation> pending_op,
                         WriteBuffer* result_buffer,
                         HybridTime* restart_read_ht,
                         const DocDBStatistics* statistics = nullptr);

  Status GetSpecialColumn(ColumnIdRep column_id, QLValuePB* result);

  Status GetIntents(const Schema& schema, IsolationLevel level, LWKeyValueWriteBatchPB* out);

 private:
  // Execute a READ operator for a given scalar argument.
  Result<size_t> ExecuteScalar(const YQLStorageIf& ql_storage,
                               const ReadOperationData& read_operation_data,
                               bool is_explicit_request_read_time,
                               const DocReadContext& doc_read_context,
                               const DocReadContext* index_doc_read_context,
                               std::reference_wrapper<const ScopedRWOperation> pending_op,
                               WriteBuffer* result_buffer,
                               HybridTime* restart_read_ht,
                               bool* has_paging_state,
                               const DocDBStatistics* statistics);

  // Execute a READ operator for a given batch of ybctids.
  Result<size_t> ExecuteBatchYbctid(const YQLStorageIf& ql_storage,
                                    const ReadOperationData& read_operation_data,
                                    const DocReadContext& doc_read_context,
                                    std::reference_wrapper<const ScopedRWOperation> pending_op,
                                    WriteBuffer* result_buffer,
                                    HybridTime* restart_read_ht,
                                    const DocDBStatistics* statistics);

  Result<size_t> ExecuteSample(const YQLStorageIf& ql_storage,
                               const ReadOperationData& read_operation_data,
                               bool is_explicit_request_read_time,
                               const DocReadContext& doc_read_context,
                               std::reference_wrapper<const ScopedRWOperation> pending_op,
                               WriteBuffer* result_buffer,
                               HybridTime* restart_read_ht,
                               bool* has_paging_state,
                               const DocDBStatistics* statistics);

  Status PopulateResultSet(const dockv::PgTableRow& table_row,
                           WriteBuffer *result_buffer);

  Status EvalAggregate(const dockv::PgTableRow& table_row);

  Status PopulateAggregate(WriteBuffer *result_buffer);

  // Checks whether we have processed enough rows for a page and sets the appropriate paging
  // state in the response object.
  Result<bool> SetPagingState(
      YQLRowwiseIteratorIf* iter, const Schema& schema, const ReadHybridTime& read_time);

  //------------------------------------------------------------------------------------------------
  const PgsqlReadRequestPB& request_;
  const TransactionOperationContext txn_op_context_;
  boost::container::small_vector<dockv::PgWireEncoderEntry, 0x10> target_encoders_;
  PgsqlResponsePB response_;
  YQLRowwiseIteratorIf::UniPtr table_iter_;
  YQLRowwiseIteratorIf::UniPtr index_iter_;
  uint64_t scanned_table_rows_ = 0;
  uint64_t scanned_index_rows_ = 0;
};

}  // namespace yb::docdb
