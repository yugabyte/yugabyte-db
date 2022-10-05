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

#ifndef YB_DOCDB_PGSQL_OPERATION_H
#define YB_DOCDB_PGSQL_OPERATION_H

#include "yb/common/pgsql_protocol.pb.h"

#include "yb/docdb/doc_expr.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

namespace yb {

class IndexInfo;

namespace docdb {

YB_STRONGLY_TYPED_BOOL(IsUpsert);

class PgsqlWriteOperation :
    public DocOperationBase<DocOperationType::PGSQL_WRITE_OPERATION, PgsqlWriteRequestPB>,
    public DocExprExecutor {
 public:
  PgsqlWriteOperation(std::reference_wrapper<const PgsqlWriteRequestPB> request,
                      DocReadContextPtr doc_read_context,
                      const TransactionOperationContext& txn_op_context)
      : DocOperationBase(request),
        doc_read_context_(std::move(doc_read_context)),
        txn_op_context_(txn_op_context) {
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

  const faststring& result_buffer() const { return result_buffer_; }

  bool result_is_single_empty_row() const {
    return result_rows_ == 1 && result_buffer_.size() == sizeof(int64_t);
  }

  Result<bool> HasDuplicateUniqueIndexValue(const DocOperationApplyData& data);
  Result<bool> HasDuplicateUniqueIndexValue(
      const DocOperationApplyData& data,
      yb::docdb::Direction direction);
  Result<bool> HasDuplicateUniqueIndexValue(
      const DocOperationApplyData& data,
      ReadHybridTime read_time);
  Result<HybridTime> FindOldestOverwrittenTimestamp(
      IntentAwareIterator* iter,
      const SubDocKey& sub_doc_key,
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

  Status DeleteRow(const DocPath& row_path, DocWriteBatch* doc_write_batch,
                   const ReadHybridTime& read_ht, CoarseTimePoint deadline);

  // Reading current row before operating on it.
  Status ReadColumns(const DocOperationApplyData& data, QLTableRow* table_row);

  Status PopulateResultSet(const QLTableRow& table_row);

  // Reading path to operate on.
  Status GetDocPaths(GetDocPathsMode mode,
                     DocPathsToLock *paths,
                     IsolationLevel *level) const override;

  class RowPackContext;

  Status InsertColumn(
      const DocOperationApplyData& data, const QLTableRow& table_row,
      const PgsqlColumnValuePB& column_value, RowPackContext* pack_context);

  Status UpdateColumn(
      const DocOperationApplyData& data, const QLTableRow& table_row,
      const PgsqlColumnValuePB& column_value, QLTableRow* returning_table_row,
      QLExprResult* result, RowPackContext* pack_context);

  //------------------------------------------------------------------------------------------------
  // Context.
  DocReadContextPtr doc_read_context_;
  const TransactionOperationContext txn_op_context_;

  // Input arguments.
  PgsqlResponsePB* response_ = nullptr;

  // TODO(neil) Output arguments.
  // UPDATE, DELETE, INSERT operations should return total number of new or changed rows.

  // Doc key and encoded doc key for the primary key.
  boost::optional<DocKey> doc_key_;
  RefCntPrefix encoded_doc_key_;

  // Rows result requested.
  int64_t result_rows_ = 0;
  faststring result_buffer_;
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
                         CoarseTimePoint deadline,
                         const ReadHybridTime& read_time,
                         bool is_explicit_request_read_time,
                         const DocReadContext& doc_read_context,
                         const DocReadContext* index_doc_read_context,
                         faststring *result_buffer,
                         HybridTime *restart_read_ht);

  Status GetTupleId(QLValuePB *result) const override;

  Status GetIntents(const Schema& schema, KeyValueWriteBatchPB* out);

 private:
  // Execute a READ operator for a given scalar argument.
  Result<size_t> ExecuteScalar(const YQLStorageIf& ql_storage,
                               CoarseTimePoint deadline,
                               const ReadHybridTime& read_time,
                               bool is_explicit_request_read_time,
                               const DocReadContext& doc_read_context,
                               const DocReadContext *index_doc_read_context,
                               faststring *result_buffer,
                               HybridTime *restart_read_ht,
                               bool *has_paging_state);

  // Execute a READ operator for a given batch of ybctids.
  Result<size_t> ExecuteBatchYbctid(const YQLStorageIf& ql_storage,
                                    CoarseTimePoint deadline,
                                    const ReadHybridTime& read_time,
                                    const DocReadContext& doc_read_context,
                                    faststring *result_buffer,
                                    HybridTime *restart_read_ht);

  Result<size_t> ExecuteSample(const YQLStorageIf& ql_storage,
                               CoarseTimePoint deadline,
                               const ReadHybridTime& read_time,
                               bool is_explicit_request_read_time,
                               const DocReadContext& doc_read_context,
                               faststring *result_buffer,
                               HybridTime *restart_read_ht,
                               bool *has_paging_state);

  Status PopulateResultSet(const QLTableRow& table_row,
                                   faststring *result_buffer);

  Status EvalAggregate(const QLTableRow& table_row);

  Status PopulateAggregate(const QLTableRow& table_row,
                                   faststring *result_buffer);

  // Checks whether we have processed enough rows for a page and sets the appropriate paging
  // state in the response object.
  Status SetPagingStateIfNecessary(const YQLRowwiseIteratorIf* iter,
                                           size_t fetched_rows,
                                           const size_t row_count_limit,
                                           const bool scan_time_exceeded,
                                           const Schema& schema,
                                           const ReadHybridTime& read_time,
                                           bool *has_paging_state);

  //------------------------------------------------------------------------------------------------
  const PgsqlReadRequestPB& request_;
  const TransactionOperationContext txn_op_context_;
  PgsqlResponsePB response_;
  YQLRowwiseIteratorIf::UniPtr table_iter_;
  YQLRowwiseIteratorIf::UniPtr index_iter_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_PGSQL_OPERATION_H
