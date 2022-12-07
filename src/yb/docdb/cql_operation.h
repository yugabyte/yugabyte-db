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

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/typedefs.h"

#include "yb/docdb/doc_expr.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_operation.h"
#include "yb/docdb/intent_aware_iterator.h"

namespace yb {
namespace docdb {

YB_STRONGLY_TYPED_BOOL(IsInsert);

class QLWriteOperation :
    public DocOperationBase<DocOperationType::QL_WRITE_OPERATION, QLWriteRequestPB>,
    public DocExprExecutor {
 public:
  QLWriteOperation(std::reference_wrapper<const QLWriteRequestPB> request,
                   DocReadContextPtr doc_read_context,
                   std::reference_wrapper<const IndexMap> index_map,
                   const Schema* unique_index_key_schema,
                   const TransactionOperationContext& txn_op_context);
  ~QLWriteOperation();

  // Construct a QLWriteOperation. Content of request will be swapped out by the constructor.
  Status Init(QLResponsePB* response);

  bool RequireReadSnapshot() const override { return require_read_; }

  Status GetDocPaths(
      GetDocPathsMode mode, DocPathsToLock *paths, IsolationLevel *level) const override;

  Status Apply(const DocOperationApplyData& data) override;

  const QLWriteRequestPB& request() const { return request_; }
  QLResponsePB* response() const { return response_; }

  IndexRequests& index_requests() {
    return index_requests_;
  }

  // Rowblock to return the "[applied]" status for conditional DML.
  const QLRowBlock* rowblock() const { return rowblock_.get(); }

  MonoDelta request_ttl() const;

 private:
  using JsonColumnMap = std::unordered_map<ColumnId, std::vector<int>>;

  Status ApplyForJsonOperators(
    const ColumnSchema& column_schema,
    const ColumnId col_id,
    const JsonColumnMap& col_map,
    const DocOperationApplyData& data,
    const ValueControlFields& control_fields,
    IsInsert is_insert,
    QLTableRow* current_row,
    RowPacker* row_packer);

  Status ApplyForSubscriptArgs(const QLColumnValuePB& column_value,
                               const QLTableRow& current_row,
                               const DocOperationApplyData& data,
                               const ValueControlFields& control_fields,
                               const ColumnSchema& column,
                               ColumnId column_id);

  Status ApplyForRegularColumns(const QLColumnValuePB& column_value,
                                const QLTableRow& current_row,
                                const DocOperationApplyData& data,
                                const ValueControlFields& control_fields,
                                const ColumnSchema& column,
                                ColumnId column_id,
                                QLTableRow* new_row,
                                RowPacker* row_packer);

  void ClearResponse() override {
    if (response_) {
      response_->Clear();
    }
  }

  // Initialize hashed_doc_key_ and/or pk_doc_key_.
  Status InitializeKeys(bool hashed_key, bool primary_key);

  Status ReadColumns(const DocOperationApplyData& data,
                     Schema *static_projection,
                     Schema *non_static_projection,
                     QLTableRow* table_row);

  Status PopulateConditionalDmlRow(const DocOperationApplyData& data,
                                   bool should_apply,
                                   const QLTableRow& table_row,
                                   Schema static_projection,
                                   Schema non_static_projection,
                                   std::unique_ptr<QLRowBlock>* rowblock);

  Status PopulateStatusRow(const DocOperationApplyData& data,
                           bool should_apply,
                           const QLTableRow& table_row,
                           std::unique_ptr<QLRowBlock>* rowblock);

  Result<bool> HasDuplicateUniqueIndexValue(const DocOperationApplyData& data);
  Result<bool> HasDuplicateUniqueIndexValue(
      const DocOperationApplyData& data, yb::docdb::Direction direction);
  Result<bool> HasDuplicateUniqueIndexValue(
      const DocOperationApplyData& data, ReadHybridTime read_time);
  Result<HybridTime> FindOldestOverwrittenTimestamp(
      IntentAwareIterator* iter, const SubDocKey& sub_doc_key,
      HybridTime min_hybrid_time);

  Status DeleteRow(const DocPath& row_path, DocWriteBatch* doc_write_batch,
                   const ReadHybridTime& read_ht, CoarseTimePoint deadline);

  Result<bool> IsRowDeleted(const QLTableRow& current_row, const QLTableRow& new_row) const;
  UserTimeMicros user_timestamp() const;

  Status UpdateIndexes(const QLTableRow& current_row, const QLTableRow& new_row);

  Status ApplyUpsert(
      const DocOperationApplyData& data, const QLTableRow& existing_row, QLTableRow* new_row);
  Status ApplyDelete(
      const DocOperationApplyData& data, QLTableRow* existing_row, QLTableRow* new_row);
  DocPath MakeSubPath(const ColumnSchema& column_schema, ColumnId column_id);
  Status InsertScalar(
      const DocOperationApplyData& data,
      const ColumnSchema& column_schema,
      ColumnId column_id,
      const ValueControlFields& control_fields,
      const QLValuePB& value,
      bfql::TSOpcode op_code,
      RowPacker* row_packer);

  docdb::DocReadContextPtr doc_read_context_;
  const IndexMap& index_map_;
  const Schema* unique_index_key_schema_ = nullptr;

  // Doc key and encoded Doc key for hashed key (i.e. without range columns). Present when there is
  // a static column being written.
  boost::optional<DocKey> hashed_doc_key_;
  RefCntPrefix encoded_hashed_doc_key_;

  // Doc key and encoded Doc key for primary key (i.e. with range columns). Present when there is a
  // non-static column being written or when writing the primary key alone (i.e. range columns are
  // present or table does not have range columns).
  boost::optional<DocKey> pk_doc_key_;
  RefCntPrefix encoded_pk_doc_key_;

  QLResponsePB* response_ = nullptr;

  IndexRequests index_requests_;

  const TransactionOperationContext txn_op_context_;

  // The row that is returned to the CQL client for an INSERT/UPDATE/DELETE that has a
  // "... IF <condition> ..." clause. The row contains the "[applied]" status column
  // plus the values of all columns referenced in the if-clause if the condition is not satisfied.
  std::unique_ptr<QLRowBlock> rowblock_;

  // Does this write operation require a read?
  bool require_read_ = false;

  // Any indexes that may need update?
  bool update_indexes_ = false;

  // Is this an insert into a unique index?
  bool insert_into_unique_index_ = false;

  // Does the liveness column exist before the write operation?
  bool liveness_column_exists_ = false;
};

Result<QLWriteRequestPB*> CreateAndSetupIndexInsertRequest(
    QLExprExecutor* expr_executor,
    bool index_has_write_permission,
    const QLTableRow& existing_row,
    const QLTableRow& new_row,
    const IndexInfo* index,
    IndexRequests* index_requests,
    bool* has_index_key_changed = nullptr,
    bool* index_pred_new_row = nullptr,
    bool index_pred_existing_row = true);

class QLReadOperation : public DocExprExecutor {
 public:
  QLReadOperation(
      const QLReadRequestPB& request,
      const TransactionOperationContext& txn_op_context)
      : request_(request), txn_op_context_(txn_op_context) {}

  Status Execute(const YQLStorageIf& ql_storage,
                 CoarseTimePoint deadline,
                 const ReadHybridTime& read_time,
                 const DocReadContext& doc_read_context,
                 const Schema& projection,
                 QLResultSet* result_set,
                 HybridTime* restart_read_ht);

  Status PopulateResultSet(const std::unique_ptr<QLScanSpec>& spec,
                           const QLTableRow& table_row,
                           QLResultSet *result_set);

  Status EvalAggregate(const QLTableRow& table_row);
  Status PopulateAggregate(const QLTableRow& table_row, QLResultSet *resultset);

  Status AddRowToResult(const std::unique_ptr<QLScanSpec>& spec,
                        const QLTableRow& row,
                        const size_t row_count_limit,
                        const size_t offset,
                        QLResultSet* resultset,
                        int* match_count,
                        size_t* num_rows_skipped);

  Status GetIntents(const Schema& schema, LWKeyValueWriteBatchPB* out);

  QLResponsePB& response() { return response_; }

 private:
  // Checks whether we have processed enough rows for a page and sets the appropriate paging
  // state in the response object.
  Status SetPagingStateIfNecessary(YQLRowwiseIteratorIf* iter,
                                   const QLResultSet* resultset,
                                   const size_t row_count_limit,
                                   const size_t num_rows_skipped,
                                   const ReadHybridTime& read_time);

  const QLReadRequestPB& request_;
  const TransactionOperationContext txn_op_context_;
  QLResponsePB response_;
};

}  // namespace docdb
}  // namespace yb
