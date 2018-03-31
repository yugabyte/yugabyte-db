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

#ifndef YB_DOCDB_DOC_OPERATION_H_
#define YB_DOCDB_DOC_OPERATION_H_

#include <list>
#include <boost/optional.hpp>

#include "yb/rocksdb/db.h"

#include "yb/common/index.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_resultset.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/ql_storage_interface.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/redis_protocol.pb.h"

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/pgsql_resultset.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/doc_expr.h"
#include "yb/docdb/intent_aware_iterator.h"

namespace yb {
namespace docdb {

class DocWriteBatch;

struct DocOperationApplyData {
  DocWriteBatch* doc_write_batch;
  ReadHybridTime read_time;
  HybridTime* restart_read_ht;
};

// When specifiying the parent key, the constant -1 is used for the subkey index.
const int kNilSubkeyIndex = -1;

class DocOperation {
 public:
  virtual ~DocOperation() {}

  // Does the operation require a read snapshot to be taken before being applied? If so, a
  // clean snapshot hybrid_time will be supplied when Apply() is called. For example,
  // QLWriteOperation for a DML with a "... IF <condition> ..." clause needs to read the row to
  // evaluate the condition before the write and needs a read snapshot for a consistent read.
  virtual bool RequireReadSnapshot() const = 0;
  virtual void GetDocPathsToLock(std::list<DocPath> *paths, IsolationLevel *level) const = 0;
  virtual CHECKED_STATUS Apply(const DocOperationApplyData& data) = 0;
};

typedef std::vector<std::unique_ptr<DocOperation>> DocOperations;

//--------------------------------------------------------------------------------------------------
// Redis support.
//--------------------------------------------------------------------------------------------------
// Redis value data with attached type of this value.
// Used internally by RedisWriteOperation.
struct RedisValue {
  RedisDataType type;
  std::string value;
};

class RedisWriteOperation : public DocOperation {
 public:
  // Construct a RedisWriteOperation. Content of request will be swapped out by the constructor.
  explicit RedisWriteOperation(RedisWriteRequestPB* request) {
    request_.Swap(request);
  }

  bool RequireReadSnapshot() const override { return false; }

  CHECKED_STATUS Apply(const DocOperationApplyData& data) override;

  void GetDocPathsToLock(std::list<DocPath> *paths, IsolationLevel *level) const override;

  RedisResponsePB &response() { return response_; }

 private:
  void InitializeIterator(const DocOperationApplyData& data);
  Result<RedisDataType> GetValueType(const DocOperationApplyData& data,
      int subkey_index = kNilSubkeyIndex);
  Result<RedisValue> GetValue(const DocOperationApplyData& data,
      int subkey_index = kNilSubkeyIndex);

  CHECKED_STATUS ApplySet(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyGetSet(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyAppend(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyDel(const DocOperationApplyData& data);
  CHECKED_STATUS ApplySetRange(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyIncr(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyPush(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyInsert(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyPop(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyAdd(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyRemove(const DocOperationApplyData& data);

  RedisWriteRequestPB request_;
  RedisResponsePB response_;
  // TODO: Currently we have a separate iterator per operation, but in future, we leave the option
  // open for operations to share iterators.
  std::unique_ptr<IntentAwareIterator> iterator_;

  rocksdb::QueryId redis_query_id() { return reinterpret_cast<rocksdb::QueryId > (&request_); }
};

class RedisReadOperation {
 public:
  explicit RedisReadOperation(const yb::RedisReadRequestPB& request,
                              rocksdb::DB* db,
      const ReadHybridTime& read_time) : request_(request), db_(db), read_time_(read_time) {}

  CHECKED_STATUS Execute();

  const RedisResponsePB &response();

 private:
  Result<RedisDataType> GetValueType(int subkey_index = kNilSubkeyIndex);

  Result<RedisValue> GetValue(int subkey_index = kNilSubkeyIndex);

  int ApplyIndex(int32_t index, const int32_t len);
  CHECKED_STATUS ExecuteGet();
  // Used to implement HGETALL, HKEYS, HVALS, SMEMBERS, HLEN, SCARD
  CHECKED_STATUS ExecuteHGetAllLikeCommands(
                                    ValueType value_type,
                                    bool add_keys,
                                    bool add_values);
  CHECKED_STATUS ExecuteStrLen();
  CHECKED_STATUS ExecuteExists();
  CHECKED_STATUS ExecuteGetRange();
  CHECKED_STATUS ExecuteCollectionGetRange();

  rocksdb::QueryId redis_query_id() { return reinterpret_cast<rocksdb::QueryId> (&request_); }

  const RedisReadRequestPB& request_;
  RedisResponsePB response_;
  rocksdb::DB* db_;
  ReadHybridTime read_time_;
  // TODO: Move iterator_ to a superclass of RedisWriteOperation RedisReadOperation
  // Make these two classes similar in terms of how rocksdb state is passed to them.
  // Currently ReadOperations get the state during construction, but Write operations get them when
  // calling Apply(). Apply() and Execute() should be more similar() in definition.
  std::unique_ptr<IntentAwareIterator> iterator_;
};

//--------------------------------------------------------------------------------------------------
// CQL support.
//--------------------------------------------------------------------------------------------------
class QLWriteOperation : public DocOperation, public DocExprExecutor {
 public:
  QLWriteOperation(const Schema& schema,
                   const IndexMap& index_map,
                   const TransactionOperationContextOpt& txn_op_context)
      : schema_(schema),
        index_map_(index_map),
        txn_op_context_(txn_op_context)
  {}

  // Construct a QLWriteOperation. Content of request will be swapped out by the constructor.
  CHECKED_STATUS Init(QLWriteRequestPB* request, QLResponsePB* response);

  bool RequireReadSnapshot() const override { return require_read_; }

  void GetDocPathsToLock(std::list<DocPath> *paths, IsolationLevel *level) const override;

  CHECKED_STATUS Apply(const DocOperationApplyData& data) override;

  const QLWriteRequestPB& request() const { return request_; }
  QLResponsePB* response() const { return response_; }

  std::vector<std::pair<const IndexInfo*, QLWriteRequestPB>>* index_requests() {
    return &index_requests_;
  }

  // Rowblock to return the "[applied]" status for conditional DML.
  const QLRowBlock* rowblock() const { return rowblock_.get(); }

 private:
  // Initialize hashed_doc_key_ and/or pk_doc_key_.
  CHECKED_STATUS InitializeKeys(bool hashed_key, bool primary_key);

  CHECKED_STATUS ReadColumns(const DocOperationApplyData& data,
                             Schema *static_projection,
                             Schema *non_static_projection,
                             QLTableRow* table_row);

  CHECKED_STATUS IsConditionSatisfied(const QLConditionPB& condition,
                                      const DocOperationApplyData& data,
                                      bool* should_apply,
                                      std::unique_ptr<QLRowBlock>* rowblock,
                                      QLTableRow* table_row);

  CHECKED_STATUS DeleteRow(const DocPath& row_path, DocWriteBatch* doc_write_batch);

  bool IsRowDeleted(const QLTableRow& current_row, const QLTableRow& new_row) const;

  CHECKED_STATUS UpdateIndexes(const QLTableRow& current_row, const QLTableRow& new_row);

  QLWriteRequestPB* NewIndexRequest(const IndexInfo* index, QLWriteRequestPB::QLStmtType type);

  const Schema& schema_;
  const IndexMap& index_map_;

  // Doc key and doc path for hashed key (i.e. without range columns). Present when there is a
  // static column being written.
  std::unique_ptr<DocKey> hashed_doc_key_;
  std::unique_ptr<DocPath> hashed_doc_path_;

  // Doc key and doc path for primary key (i.e. with range columns). Present when there is a
  // non-static column being written or when writing the primary key alone (i.e. range columns are
  // present or table does not have range columns).
  std::unique_ptr<DocKey> pk_doc_key_;
  std::unique_ptr<DocPath> pk_doc_path_;

  QLWriteRequestPB request_;
  QLResponsePB* response_ = nullptr;

  std::vector<std::pair<const IndexInfo*, QLWriteRequestPB>> index_requests_;

  const TransactionOperationContextOpt txn_op_context_;

  // The row that is returned to the CQL client for an INSERT/UPDATE/DELETE that has a
  // "... IF <condition> ..." clause. The row contains the "[applied]" status column
  // plus the values of all columns referenced in the if-clause if the condition is not satisfied.
  std::unique_ptr<QLRowBlock> rowblock_;

  // Does this write operation require a read?
  bool require_read_ = false;

  // Any indexes that may need update?
  bool update_indexes_ = false;

  // Does the liveness column exist before the write operation?
  bool liveness_column_exists_ = false;
};

class QLReadOperation : public DocExprExecutor {
 public:
  QLReadOperation(
      const QLReadRequestPB& request,
      const TransactionOperationContextOpt& txn_op_context)
      : request_(request), txn_op_context_(txn_op_context) {}

  CHECKED_STATUS Execute(const common::YQLStorageIf& ql_storage,
                         const ReadHybridTime& read_time,
                         const Schema& schema,
                         const Schema& query_schema,
                         QLResultSet* result_set,
                         HybridTime* restart_read_ht);

  CHECKED_STATUS PopulateResultSet(const QLTableRow& table_row, QLResultSet *result_set);

  CHECKED_STATUS EvalAggregate(const QLTableRow& table_row);
  CHECKED_STATUS PopulateAggregate(const QLTableRow& table_row, QLResultSet *resultset);

  CHECKED_STATUS AddRowToResult(const std::unique_ptr<common::QLScanSpec>& spec,
                                const QLTableRow& row,
                                const size_t row_count_limit,
                                QLResultSet* resultset,
                                int* match_count);

  QLResponsePB& response() { return response_; }

 private:
  const QLReadRequestPB& request_;
  const TransactionOperationContextOpt txn_op_context_;
  QLResponsePB response_;
};

//--------------------------------------------------------------------------------------------------
// PGSQL support.
//--------------------------------------------------------------------------------------------------
class PgsqlDocOperation : public DocOperation, public DocExprExecutor {
 public:
  CHECKED_STATUS CreateProjections(const Schema& schema,
                                   const PgsqlColumnRefsPB& column_refs,
                                   Schema* column_projection);
};

class PgsqlWriteOperation : public PgsqlDocOperation {
 public:
  PgsqlWriteOperation(const Schema& schema,
                      const TransactionOperationContextOpt& txn_op_context)
      : schema_(schema),
        txn_op_context_(txn_op_context) {
  }

  // Initialize PgsqlWriteOperation. Content of request will be swapped out by the constructor.
  CHECKED_STATUS Init(PgsqlWriteRequestPB* request, PgsqlResponsePB* response);
  bool RequireReadSnapshot() const override { return request_.has_column_refs(); }
  const PgsqlWriteRequestPB& request() const { return request_; }
  PgsqlResponsePB* response() const { return response_; }

  // Execute write.
  CHECKED_STATUS Apply(const DocOperationApplyData& data) override;

 private:
  // Insert, update, and delete operations.
  CHECKED_STATUS ApplyInsert(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyUpdate(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyDelete(const DocOperationApplyData& data);

  // Reading current row before operating on it.
  CHECKED_STATUS ReadColumns(const DocOperationApplyData& data,
                             const QLTableRow::SharedPtr& table_row);

  // Reading path to operate on.
  void GetDocPathsToLock(std::list<DocPath> *paths, IsolationLevel *level) const override;

  //------------------------------------------------------------------------------------------------
  // Context.
  const Schema& schema_;
  const TransactionOperationContextOpt txn_op_context_;

  // Input arguments.
  PgsqlWriteRequestPB request_;
  PgsqlResponsePB* response_ = nullptr;

  // TODO(neil) Output arguments.
  // UPDATE, DELETE, INSERT operations should return total number of new or changed rows.

  // State variables.
  // Doc key and doc path for hashed key (i.e. without range columns). Present when there is a
  // static column being written.
  std::shared_ptr<DocKey> hashed_doc_key_;
  std::shared_ptr<DocPath> hashed_doc_path_;

  // Doc key and doc path for primary key (i.e. with range columns). Present when there is a
  // non-static column being written or when writing the primary key alone (i.e. range columns are
  // present or table does not have range columns).
  std::shared_ptr<DocKey> range_doc_key_;
  std::shared_ptr<DocPath> range_doc_path_;
};

class PgsqlReadOperation : public PgsqlDocOperation {
 public:
  // Construct and access methods.
  PgsqlReadOperation(const PgsqlReadRequestPB& request,
                     const TransactionOperationContextOpt& txn_op_context)
      : request_(request), txn_op_context_(txn_op_context) {
  }

  bool RequireReadSnapshot() const override { return request_.has_column_refs(); }
  const PgsqlReadRequestPB& request() const { return request_; }
  PgsqlResponsePB& response() { return response_; }

  // Execute read operations.
  CHECKED_STATUS Apply(const DocOperationApplyData& data) override {
    LOG(FATAL) << "This should not be callled for read operations";
    return Status::OK();
  }

  void GetDocPathsToLock(std::list<DocPath> *paths, IsolationLevel *level) const override {
    LOG(FATAL) << "This lock should not be applied as writing while reading is not yet allowed";
  }

  CHECKED_STATUS Execute(const common::YQLStorageIf& ql_storage,
                         const Schema& schema,
                         const Schema& query_schema,
                         const ReadHybridTime& read_time,
                         PgsqlResultSet *result_set,
                         HybridTime *restart_read_ht);

 private:
  CHECKED_STATUS PopulateResultSet(const QLTableRow::SharedPtr& table_row,
                                   PgsqlResultSet *result_set);

  CHECKED_STATUS EvalAggregate(const QLTableRow::SharedPtr& table_row);

  CHECKED_STATUS PopulateAggregate(const QLTableRow::SharedPtr& table_row,
                                   PgsqlResultSet *resultset);

  //------------------------------------------------------------------------------------------------
  const PgsqlReadRequestPB& request_;
  const TransactionOperationContextOpt txn_op_context_;
  PgsqlResponsePB response_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_OPERATION_H_
