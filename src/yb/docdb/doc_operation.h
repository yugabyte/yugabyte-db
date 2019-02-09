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
#include "yb/common/typedefs.h"

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/pgsql_resultset.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value.h"
#include "yb/docdb/doc_expr.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/deadline_info.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/ref_cnt_buffer.h"

namespace yb {
namespace docdb {

class DocWriteBatch;
class KeyValueWriteBatchPB;

// Useful for calculating expiration.
struct Expiration {
  Expiration() :
    ttl(Value::kMaxTtl) {}

  explicit Expiration(MonoDelta default_ttl) :
    ttl(default_ttl) {}

  explicit Expiration(HybridTime new_write_ht) :
    ttl(Value::kMaxTtl),
    write_ht(new_write_ht) {}

  explicit Expiration(HybridTime new_write_ht, MonoDelta new_ttl) :
    ttl(new_ttl),
    write_ht(new_write_ht) {}

  MonoDelta ttl;
  HybridTime write_ht = HybridTime::kMin;

  // A boolean which dictates whether the TTL of kMaxValue
  // should override the existing TTL. Not compatible with
  // the concept of default TTL when set to true.
  bool always_override = false;

  Result<MonoDelta> ComputeRelativeTtl(const HybridTime& input_time) {
    if (input_time < write_ht)
      return STATUS(Corruption, "Read time earlier than record write time.");
    if (ttl == Value::kMaxTtl || ttl.IsNegative())
      return ttl;
    MonoDelta elapsed_time = MonoDelta::FromNanoseconds(
        server::HybridClock::GetPhysicalValueNanos(input_time) -
        server::HybridClock::GetPhysicalValueNanos(write_ht));
    // This way, we keep the default TTL, and all negative TTLs are expired.
    MonoDelta new_ttl(ttl);
    return new_ttl -= elapsed_time;
  }
};

struct DocOperationApplyData {
  DocWriteBatch* doc_write_batch;
  MonoTime deadline;
  ReadHybridTime read_time;
  HybridTime* restart_read_ht;
};

// When specifiying the parent key, the constant -1 is used for the subkey index.
const int kNilSubkeyIndex = -1;

typedef boost::container::small_vector_base<RefCntPrefix> DocPathsToLock;

YB_DEFINE_ENUM(GetDocPathsMode, (kLock)(kIntents));
YB_DEFINE_ENUM(DocOperationType,
               (PGSQL_WRITE_OPERATION)(PGSQL_READ_OPERATION)(QL_WRITE_OPERATION)
                   (REDIS_WRITE_OPERATION));

class DocOperation {
 public:
  typedef DocOperationType Type;

  virtual ~DocOperation() {}

  // Does the operation require a read snapshot to be taken before being applied? If so, a
  // clean snapshot hybrid_time will be supplied when Apply() is called. For example,
  // QLWriteOperation for a DML with a "... IF <condition> ..." clause needs to read the row to
  // evaluate the condition before the write and needs a read snapshot for a consistent read.
  virtual bool RequireReadSnapshot() const = 0;

  // Returns doc paths for this operation and isolation level this operation.
  // Doc paths are added to the end of paths, i.e. paths content is not cleared before it.
  //
  // Returned doc paths are controlled by mode argument:
  //   kLock - paths should be locked for this operation.
  //   kIntents - paths that should be used when writing intents, i.e. for conflict resolution.
  virtual CHECKED_STATUS GetDocPaths(
      GetDocPathsMode mode, DocPathsToLock *paths, IsolationLevel *level) const = 0;

  virtual CHECKED_STATUS Apply(const DocOperationApplyData& data) = 0;
  virtual Type OpType() = 0;
  virtual void ClearResponse() = 0;

  virtual std::string ToString() const = 0;
};

template <DocOperationType OperationType, class RequestPB>
class DocOperationBase : public DocOperation {
 public:
  Type OpType() override {
    return OperationType;
  }

  std::string ToString() const override {
    return Format("$0 { request: $1 }", OperationType, request_);
  }

 protected:
  RequestPB request_;
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
  Expiration exp;
  int64_t internal_index = 0;
};

class RedisWriteOperation :
    public DocOperationBase<DocOperationType::REDIS_WRITE_OPERATION, RedisWriteRequestPB> {
 public:
  // Construct a RedisWriteOperation. Content of request will be swapped out by the constructor.
  explicit RedisWriteOperation(RedisWriteRequestPB* request) {
    request_.Swap(request);
  }

  bool RequireReadSnapshot() const override { return false; }

  CHECKED_STATUS Apply(const DocOperationApplyData& data) override;

  CHECKED_STATUS GetDocPaths(
      GetDocPathsMode mode, DocPathsToLock *paths, IsolationLevel *level) const override;

  RedisResponsePB &response() { return response_; }

 private:
  void ClearResponse() override {
    response_.Clear();
  }

  void InitializeIterator(const DocOperationApplyData& data);
  Result<RedisDataType> GetValueType(const DocOperationApplyData& data,
      int subkey_index = kNilSubkeyIndex);
  Result<RedisValue> GetValue(const DocOperationApplyData& data,
      int subkey_index = kNilSubkeyIndex, Expiration* exp = nullptr);

  CHECKED_STATUS ApplySetTtl(const DocOperationApplyData& data);
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

  RedisResponsePB response_;
  // TODO: Currently we have a separate iterator per operation, but in future, we leave the option
  // open for operations to share iterators.
  std::unique_ptr<IntentAwareIterator> iterator_;

  rocksdb::QueryId redis_query_id() { return reinterpret_cast<rocksdb::QueryId > (&request_); }
};

class RedisReadOperation {
 public:
  explicit RedisReadOperation(const yb::RedisReadRequestPB& request,
                              const DocDB& doc_db,
                              MonoTime deadline,
                              const ReadHybridTime& read_time)
      : request_(request), doc_db_(doc_db), deadline_(deadline), read_time_(read_time) {}

  CHECKED_STATUS Execute();

  const RedisResponsePB &response();

 private:
  Result<RedisDataType> GetValueType(int subkey_index = kNilSubkeyIndex);

  // GetValue when always_override should be true.
  // This is particularly relevant for the Timeseries datatype, for which
  // child TTLs override parent TTLs.
  // TODO: Once the timeseries bug is fixed, this function as well as the
  // corresponding field can be safely removed.
  Result<RedisValue>GetOverrideValue(int subkey_index = kNilSubkeyIndex);

  Result<RedisValue> GetValue(int subkey_index = kNilSubkeyIndex);

  int ApplyIndex(int32_t index, const int32_t len);
  CHECKED_STATUS ExecuteGet();
  CHECKED_STATUS ExecuteGet(const RedisGetRequestPB& get_request);
  CHECKED_STATUS ExecuteGet(RedisGetRequestPB::GetRequestType type);
  CHECKED_STATUS ExecuteGetForRename();
  CHECKED_STATUS ExecuteGetTtl();
  // Used to implement HGETALL, HKEYS, HVALS, SMEMBERS, HLEN, SCARD
  CHECKED_STATUS ExecuteHGetAllLikeCommands(
                                    ValueType value_type,
                                    bool add_keys,
                                    bool add_values);
  CHECKED_STATUS ExecuteStrLen();
  CHECKED_STATUS ExecuteExists();
  CHECKED_STATUS ExecuteGetRange();
  CHECKED_STATUS ExecuteCollectionGetRange();
  CHECKED_STATUS ExecuteCollectionGetRangeByBounds(
      RedisCollectionGetRangeRequestPB::GetRangeRequestType request_type, bool add_keys);
  CHECKED_STATUS ExecuteCollectionGetRangeByBounds(
      RedisCollectionGetRangeRequestPB::GetRangeRequestType request_type,
      const RedisSubKeyBoundPB& lower_bound, const RedisSubKeyBoundPB& upper_bound, bool add_keys);
  CHECKED_STATUS ExecuteKeys();

  rocksdb::QueryId redis_query_id() { return reinterpret_cast<rocksdb::QueryId> (&request_); }

  const RedisReadRequestPB& request_;
  RedisResponsePB response_;
  const DocDB doc_db_;
  MonoTime deadline_;
  ReadHybridTime read_time_;
  // TODO: Move iterator_ to a superclass of RedisWriteOperation RedisReadOperation
  // Make these two classes similar in terms of how rocksdb state is passed to them.
  // Currently ReadOperations get the state during construction, but Write operations get them when
  // calling Apply(). Apply() and Execute() should be more similar() in definition.
  std::unique_ptr<IntentAwareIterator> iterator_;

  boost::optional<DeadlineInfo> deadline_info_;
};

//--------------------------------------------------------------------------------------------------
// CQL support.
//--------------------------------------------------------------------------------------------------
class QLWriteOperation :
    public DocOperationBase<DocOperationType::QL_WRITE_OPERATION, QLWriteRequestPB>,
    public DocExprExecutor {
 public:
  QLWriteOperation(const Schema& schema,
                   const IndexMap& index_map,
                   const Schema* unique_index_key_schema,
                   const TransactionOperationContextOpt& txn_op_context)
      : schema_(schema),
        index_map_(index_map),
        unique_index_key_schema_(unique_index_key_schema),
        txn_op_context_(txn_op_context)
  {}

  // Construct a QLWriteOperation. Content of request will be swapped out by the constructor.
  CHECKED_STATUS Init(QLWriteRequestPB* request, QLResponsePB* response);

  bool RequireReadSnapshot() const override { return require_read_; }

  CHECKED_STATUS GetDocPaths(
      GetDocPathsMode mode, DocPathsToLock *paths, IsolationLevel *level) const override;

  CHECKED_STATUS Apply(const DocOperationApplyData& data) override;

  CHECKED_STATUS ApplyForJsonOperators(const QLColumnValuePB& column_value,
                                       const DocOperationApplyData& data,
                                       const DocPath& sub_path, const MonoDelta& ttl,
                                       const UserTimeMicros& user_timestamp,
                                       const ColumnSchema& column,
                                       QLTableRow* current_row);

  CHECKED_STATUS ApplyForSubscriptArgs(const QLColumnValuePB& column_value,
                                       const QLTableRow& current_row,
                                       const DocOperationApplyData& data,
                                       const MonoDelta& ttl,
                                       const UserTimeMicros& user_timestamp,
                                       const ColumnSchema& column,
                                       DocPath* sub_path);

  CHECKED_STATUS ApplyForRegularColumns(const QLColumnValuePB& column_value,
                                        const QLTableRow& current_row,
                                        const DocOperationApplyData& data,
                                        const DocPath& sub_path, const MonoDelta& ttl,
                                        const UserTimeMicros& user_timestamp,
                                        const ColumnSchema& column,
                                        const ColumnId& column_id,
                                        QLTableRow* new_row);

  const QLWriteRequestPB& request() const { return request_; }
  QLResponsePB* response() const { return response_; }

  std::vector<std::pair<const IndexInfo*, QLWriteRequestPB>>* index_requests() {
    return &index_requests_;
  }

  // Rowblock to return the "[applied]" status for conditional DML.
  const QLRowBlock* rowblock() const { return rowblock_.get(); }

 private:
  void ClearResponse() override {
    if (response_) {
      response_->Clear();
    }
  }

  // Initialize hashed_doc_key_ and/or pk_doc_key_.
  CHECKED_STATUS InitializeKeys(bool hashed_key, bool primary_key);

  CHECKED_STATUS ReadColumns(const DocOperationApplyData& data,
                             Schema *static_projection,
                             Schema *non_static_projection,
                             QLTableRow* table_row);

  CHECKED_STATUS PopulateConditionalDmlRow(const DocOperationApplyData& data,
                                           bool should_apply,
                                           const QLTableRow& table_row,
                                           Schema static_projection,
                                           Schema non_static_projection,
                                           std::unique_ptr<QLRowBlock>* rowblock);

  CHECKED_STATUS PopulateStatusRow(const DocOperationApplyData& data,
                                   bool should_apply,
                                   const QLTableRow& table_row,
                                   std::unique_ptr<QLRowBlock>* rowblock);

  Result<bool> DuplicateUniqueIndexValue(const DocOperationApplyData& data);

  CHECKED_STATUS DeleteRow(const DocPath& row_path, DocWriteBatch* doc_write_batch,
                           const ReadHybridTime& read_ht, const MonoTime deadline);

  bool IsRowDeleted(const QLTableRow& current_row, const QLTableRow& new_row) const;

  CHECKED_STATUS UpdateIndexes(const QLTableRow& current_row, const QLTableRow& new_row);

  QLWriteRequestPB* NewIndexRequest(const IndexInfo* index,
                                    QLWriteRequestPB::QLStmtType type,
                                    const QLTableRow& new_row);

  const Schema& schema_;
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

  // Is this an insert into a unique index?
  bool insert_into_unique_index_ = false;

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
                         MonoTime deadline,
                         const ReadHybridTime& read_time,
                         const Schema& schema,
                         const Schema& projection,
                         QLResultSet* result_set,
                         HybridTime* restart_read_ht);

  CHECKED_STATUS PopulateResultSet(const QLTableRow& table_row, QLResultSet *result_set);

  CHECKED_STATUS EvalAggregate(const QLTableRow& table_row);
  CHECKED_STATUS PopulateAggregate(const QLTableRow& table_row, QLResultSet *resultset);

  CHECKED_STATUS AddRowToResult(const std::unique_ptr<common::QLScanSpec>& spec,
                                const QLTableRow& row,
                                const size_t row_count_limit,
                                const size_t offset,
                                QLResultSet* resultset,
                                int* match_count,
                                size_t* num_rows_skipped);

  CHECKED_STATUS GetIntents(const Schema& schema, KeyValueWriteBatchPB* out);

  QLResponsePB& response() { return response_; }

 private:
  const QLReadRequestPB& request_;
  const TransactionOperationContextOpt txn_op_context_;
  QLResponsePB response_;
};

//--------------------------------------------------------------------------------------------------
// PGSQL support.
//--------------------------------------------------------------------------------------------------

class PgsqlWriteOperation :
    public DocOperationBase<DocOperationType::PGSQL_WRITE_OPERATION, PgsqlWriteRequestPB>,
    public DocExprExecutor {
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

  const PgsqlResultSet& resultset() const { return resultset_; }

  // Execute write.
  CHECKED_STATUS Apply(const DocOperationApplyData& data) override;

 private:
  void ClearResponse() override {
    if (response_) {
      response_->Clear();
    }
  }

  // Insert, update, and delete operations.
  CHECKED_STATUS ApplyInsert(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyUpdate(const DocOperationApplyData& data);
  CHECKED_STATUS ApplyDelete(const DocOperationApplyData& data);

  CHECKED_STATUS DeleteRow(const DocPath& row_path, DocWriteBatch* doc_write_batch,
                           const ReadHybridTime& read_ht, const MonoTime deadline);

  // Reading current row before operating on it.
  CHECKED_STATUS ReadColumns(const DocOperationApplyData& data,
                             const QLTableRow::SharedPtr& table_row);

  CHECKED_STATUS PopulateResultSet();

  // Reading path to operate on.
  CHECKED_STATUS GetDocPaths(
      GetDocPathsMode mode, DocPathsToLock *paths, IsolationLevel *level) const override;

  //------------------------------------------------------------------------------------------------
  // Context.
  const Schema& schema_;
  const TransactionOperationContextOpt txn_op_context_;

  // Input arguments.
  PgsqlResponsePB* response_ = nullptr;

  // TODO(neil) Output arguments.
  // UPDATE, DELETE, INSERT operations should return total number of new or changed rows.

  // State variables.
  // Doc key and encoded doc key for hashed key (i.e. without range columns). Present when there is
  // a static column being written.
  boost::optional<DocKey> hashed_doc_key_;
  RefCntPrefix encoded_hashed_doc_key_;

  // Doc key and encoded doc key for primary key (i.e. with range columns). Present when there is a
  // non-static column being written or when writing the primary key alone (i.e. range columns are
  // present or table does not have range columns).
  boost::optional<DocKey> range_doc_key_;
  RefCntPrefix encoded_range_doc_key_;

  // Rows result requested.
  PgsqlResultSet resultset_;
};

class PgsqlReadOperation : public DocExprExecutor {
 public:
  // Construct and access methods.
  PgsqlReadOperation(const PgsqlReadRequestPB& request,
                     const TransactionOperationContextOpt& txn_op_context)
      : request_(request), txn_op_context_(txn_op_context) {
  }

  const PgsqlReadRequestPB& request() const { return request_; }
  PgsqlResponsePB& response() { return response_; }

  CHECKED_STATUS Execute(const common::YQLStorageIf& ql_storage,
                         MonoTime deadline,
                         const ReadHybridTime& read_time,
                         const Schema& schema,
                         const Schema *index_schema,
                         PgsqlResultSet *result_set,
                         HybridTime *restart_read_ht);

  virtual CHECKED_STATUS GetTupleId(QLValue *result) const override;

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
  common::YQLRowwiseIteratorIf::UniPtr table_iter_;
  common::YQLRowwiseIteratorIf::UniPtr index_iter_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_OPERATION_H_
