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

#include "yb/common/ql_storage_interface.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/ql_resultset.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/doc_expr.h"

namespace yb {
namespace docdb {

class DocWriteBatch;

struct DocOperationApplyData {
  DocWriteBatch* doc_write_batch;
  ReadHybridTime read_time;
  HybridTime* restart_read_ht;
};

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
  Result<RedisDataType> GetValueType(const DocOperationApplyData& data, int subkey_index = -1);
  Result<RedisValue> GetValue(const DocOperationApplyData& data, int subkey_index = -1);

  CHECKED_STATUS ApplySet(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyGetSet(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyAppend(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyDel(const DocOperationApplyData& data);
  CHECKED_STATUS ApplySetRange(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyIncr(const DocOperationApplyData& data, int64_t incr = 1);
  CHECKED_STATUS ApplyPush(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyInsert(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyPop(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyAdd(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyRemove(const DocOperationApplyData& data);

  RedisWriteRequestPB request_;
  RedisResponsePB response_;

  rocksdb::QueryId redis_query_id() { return reinterpret_cast<rocksdb::QueryId > (&request_); }
};

class RedisReadOperation {
 public:
  explicit RedisReadOperation(const yb::RedisReadRequestPB& request,
                              rocksdb::DB* db,
                              const ReadHybridTime& read_time)
      : request_(request), db_(db), read_time_(read_time) {}

  CHECKED_STATUS Execute();

  const RedisResponsePB &response();

 private:
  Result<RedisDataType> GetValueType(int subkey_index = -1);
  Result<RedisValue> GetValue(int subkey_index = -1);

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
  CHECKED_STATUS ExecuteGetCard(rocksdb::DB *rocksdb, HybridTime hybrid_time);

  rocksdb::QueryId redis_query_id() { return reinterpret_cast<rocksdb::QueryId> (&request_); }

  const RedisReadRequestPB& request_;
  RedisResponsePB response_;
  rocksdb::DB* db_;
  ReadHybridTime read_time_;
};

class QLWriteOperation : public DocOperation, public DocExprExecutor {
 public:
  QLWriteOperation(const Schema& schema,
                   const TransactionOperationContextOpt& txn_op_context)
      : schema_(schema),
        txn_op_context_(txn_op_context)
  {}

  // Construct a QLWriteOperation. Content of request will be swapped out by the constructor.
  CHECKED_STATUS Init(QLWriteRequestPB* request, QLResponsePB* response);

  bool RequireReadSnapshot() const override { return require_read_; }

  void GetDocPathsToLock(std::list<DocPath> *paths, IsolationLevel *level) const override;

  CHECKED_STATUS Apply(const DocOperationApplyData& data) override;

  const QLWriteRequestPB& request() const { return request_; }
  QLResponsePB* response() const { return response_; }

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

  CHECKED_STATUS DeleteRow(DocWriteBatch* doc_write_batch,
                           const DocPath row_path);

  const Schema& schema_;

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
  const TransactionOperationContextOpt txn_op_context_;

  // The row that is returned to the CQL client for an INSERT/UPDATE/DELETE that has a
  // "... IF <condition> ..." clause. The row contains the "[applied]" status column
  // plus the values of all columns referenced in the if-clause if the condition is not satisfied.
  std::unique_ptr<QLRowBlock> rowblock_;

  // Does this write operation require a read?
  bool require_read_ = false;
};

class QLReadOperation : public DocExprExecutor {
 public:
  QLReadOperation(
      const QLReadRequestPB& request,
      const TransactionOperationContextOpt& txn_op_context)
      : request_(request), txn_op_context_(txn_op_context) {}

  CHECKED_STATUS Execute(const common::QLStorageIf& ql_storage,
                         const ReadHybridTime& read_time,
                         const Schema& schema,
                         const Schema& query_schema,
                         QLResultSet* result_set,
                         HybridTime* restart_read_ht);

  CHECKED_STATUS PopulateResultSet(const QLTableRow& table_row, QLResultSet *result_set);

  CHECKED_STATUS EvalAggregate(const QLTableRow& table_row);
  CHECKED_STATUS PopulateAggregate(const QLTableRow& table_row, QLResultSet *resultset);

  QLResponsePB& response() { return response_; }

 private:
  const QLReadRequestPB& request_;
  const TransactionOperationContextOpt txn_op_context_;
  QLResponsePB response_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_OPERATION_H_
