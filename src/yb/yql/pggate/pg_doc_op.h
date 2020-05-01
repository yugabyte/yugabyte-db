//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_DOC_OP_H_
#define YB_YQL_PGGATE_PG_DOC_OP_H_

#include <deque>

#include "yb/util/locks.h"
#include "yb/client/yb_op.h"
#include "yb/yql/pggate/pg_session.h"

namespace yb {
namespace pggate {

YB_STRONGLY_TYPED_BOOL(RequestSent);

//--------------------------------------------------------------------------------------------------
// PgDocResult represents a batch of rows in ONE reply from tablet servers.
class PgDocResult {
 public:
  explicit PgDocResult(string&& data);
  PgDocResult(string&& data, std::list<int64_t>&& row_orders);
  ~PgDocResult();

  PgDocResult(const PgDocResult&) = delete;
  PgDocResult& operator=(const PgDocResult&) = delete;

  // Get the order of the next row in this batch.
  int64_t NextRowOrder();

  // End of this batch.
  bool is_eof() const {
    return row_count_ == 0 || row_iterator_.empty();
  }

  // Get the postgres tuple from this batch.
  CHECKED_STATUS WritePgTuple(const std::vector<PgExpr*>& targets, PgTuple *pg_tuple,
                              int64_t *row_order);

  // Get system columns' values from this batch.
  // Currently, we only have ybctids, but there could be more.
  CHECKED_STATUS ProcessSystemColumns();

  // Access function to ybctids value in this batch.
  // Sys columns must be processed before this function is called.
  const vector<Slice>& ybctids() const {
    DCHECK(syscol_processed_) << "System columns are not yet setup";
    return ybctids_;
  }

  // Row count in this batch.
  int64_t row_count() const {
    return row_count_;
  }

 private:
  // Data selected from DocDB.
  string data_;

  // Iterator on "data_" from row to row.
  Slice row_iterator_;

  // The row number of only this batch.
  int64_t row_count_ = 0;

  // The indexing order of the row in this batch.
  // These order values help to identify the row order across all batches.
  std::list<int64_t> row_orders_;

  // System columns.
  // - ybctids_ contains pointers to the buffers "data_".
  // - System columns must be processed before these fields have any meaning.
  vector<Slice> ybctids_;
  bool syscol_processed_ = false;
};

//--------------------------------------------------------------------------------------------------

class PgDocOp : public std::enable_shared_from_this<PgDocOp> {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocOp> SharedPtr;
  typedef std::shared_ptr<const PgDocOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocOp> UniPtr;
  typedef std::unique_ptr<const PgDocOp> UniPtrConst;

  // Constructors & Destructors.
  explicit PgDocOp(const PgSession::ScopedRefPtr& pg_session);
  virtual ~PgDocOp();

  // Initialize doc operator.
  virtual void Initialize(const PgExecParameters *exec_params);

  // Currently, only ybctid can be batched.
  virtual CHECKED_STATUS SetBatchArgYbctid(const vector<Slice> *ybctids,
                                           const PgTableDesc *table_desc) = 0;

  // Execute the op. Return true if the request has been sent and is awaiting the result.
  virtual Result<RequestSent> Execute(bool force_non_bufferable = false);

  // Get the result of the op. No rows will be added to rowsets in case end of data reached.
  CHECKED_STATUS GetResult(std::list<PgDocResult> *rowsets);

  Result<int32_t> GetRowsAffectedCount() const;

  // Instruct this doc_op to abandon execution and querying data by setting end_of_data_ to 'true'.
  // - This op will not send request to tablet server.
  // - This op will return empty result-set when being requested for data.
  void AbandonExecution() {
    end_of_data_ = true;
  }

 protected:
  // Session control.
  PgSession::ScopedRefPtr pg_session_;

  // Operation time. This time is set at the start and must stay the same for the lifetime of the
  // operation to ensure that it is operating on one snapshot.
  uint64_t read_time_ = 0;

  // Future object to fetch a response from DocDB after sending a request.
  // Object's valid() method returns false in case no request is sent
  // or sent request was buffered by the session.
  // Only one request can be sent to DocDB at a time.
  PgSessionAsyncRunResult response_;

  // Whether all requested data by the statement has been received or there's a run-time error.
  bool end_of_data_ = false;

  // Exec control parameters.
  PgExecParameters exec_params_;

  // Suppress sending new request after processing response.
  // Next request will be sent in case upper level will ask for additional data.
  bool suppress_next_result_prefetching_ = false;

 private:
  CHECKED_STATUS SendRequest(bool force_non_bufferable);

  Result<std::list<PgDocResult>> ProcessResponse(const Status& exec_status);

  virtual CHECKED_STATUS SendRequestImpl(bool force_non_bufferable) = 0;

  virtual Result<std::list<PgDocResult>> ProcessResponseImpl() = 0;

  virtual int32_t GetRowsAffectedCountImpl() const { return 0; }

  // Result set either from selected or returned targets is cached in a list of strings.
  // Querying state variables.
  Status exec_status_ = Status::OK();
};

//--------------------------------------------------------------------------------------------------

class PgDocReadOp : public PgDocOp {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocReadOp> SharedPtr;
  typedef std::shared_ptr<const PgDocReadOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocReadOp> UniPtr;
  typedef std::unique_ptr<const PgDocReadOp> UniPtrConst;

  // Constructors & Destructors.
  PgDocReadOp(const PgSession::ScopedRefPtr& pg_session,
              const PgTableDesc::ScopedRefPtr& table_desc,
              size_t num_hash_key_columns,
              std::unique_ptr<client::YBPgsqlReadOp> read_op);

  void Initialize(const PgExecParameters *exec_params) override;

  CHECKED_STATUS SetBatchArgYbctid(const vector<Slice> *ybctids,
                                   const PgTableDesc *table_desc) override;

 private:
  // Process response from DocDB.
  CHECKED_STATUS SendRequestImpl(bool force_non_bufferable) override;
  Result<std::list<PgDocResult>> ProcessResponseImpl() override;

  // Analyze options and pick the appropriate prefetch limit.
  void SetRequestPrefetchLimit();

  // Set the row_mark_type field of our read request based on our exec control parameter.
  void SetRowMark();

  // Create batch operators, one per partition.
  CHECKED_STATUS CreateBatchOps(int partition_count);

  const size_t num_hash_key_columns_;

  // Template operation, used to fill in read_ops_ by copying it with custom partition_column_values
  std::unique_ptr<client::YBPgsqlReadOp> template_op_;

  // Initialize up to N new operations from template_op_ and add them to read_ops_.
  //
  // If partition column binds are defined, partition_column_values on each opearation
  // is set to be the next permutation. Otherwise, template_op_ is copied as-is just once.
  //
  // Also updates the value of can_produce_more_ops_.
  void InitializeNextOps(int num_ops);

  void InitializeParallelSelectCountOps(int select_parallelism);

  // Used internally for InitializeNextOps to keep track of which permutation should be used
  // to construct the next read_op.
  // Is valid as long as can_produce_more_ops_ is true.
  //
  // Example:
  // For a query clause "h1 = 1 AND h2 IN (2,3) AND h3 IN (4,5,6) AND h4 = 7",
  // there are 1*2*3*1 = 6 possible permutation.
  // As such, this field will take on values 0 through 5.
  int next_op_idx_ = 0;

  // Used internally for InitializeNextOps to holds all partition expressions of expressions.
  // Elements correspond to a hash columns, in the same order as they were defined
  // in CREATE TABLE statement.
  // This is somewhat similar to what hash_values_options_ in CQL is used for.
  //
  // Example:
  // For a query clause "h1 = 1 AND h2 IN (2,3) AND h3 IN (4,5,6) AND h4 = 7",
  // this will be initialized to [[1], [2, 3], [4, 5, 6], [7]]
  std::vector<std::vector<const PgsqlExpressionPB*>> partition_exprs_;

  // True when either:
  // 1) Partition columns are not bound but the request hasn't been sent yet.
  // 2) Partition columns are bound and some permutations remain unprocessed.
  bool can_produce_more_ops_ = true;

  PgTableDesc::ScopedRefPtr table_desc_;

  // Offset to use for next element in partition_keys.
  int partition_keys_next_to_use_ = 0;

  // Operation(s).
  //
  // If there's more than one, partition_column_values will be fully specified on all of them.
  //
  // This list is initialized only in SendRequestUnlocked - i.e. just before operations are sent.
  // In ReceiveResponse this list is pruned from exhausted operations, remaining are set up to
  // retrieve the next batch of data.
  std::vector<std::shared_ptr<client::YBPgsqlReadOp>> read_ops_;

  // TODO(neil) batch_ops_ vs read_ops_
  // - batch_ops_ and read_ops_ should be just one list. Instead of erasing item from read_ops_,
  //   just mark them as inactive. Currently, read_op is erased when done without being reused.
  // - The field read_ops_ was introduced to support IN operator on hash values where we batch
  //   by statement, and the read_ops are recreated for each hash permutation. We can change this
  //   to batch by arguments and therefore merge read_ops_ and batch_ops into one list.
  //
  // Array of doc operators, one operator per tablet server. The arguments of the same hash-code
  // are grouped or batched together in one operator, which is called batch op.
  std::vector<std::shared_ptr<client::YBPgsqlReadOp>> batch_ops_;

  // The order number of each request when batching arguments.
  std::vector<std::list<int64_t>> batch_row_orders_;

  // If true, all data for each batch must be collected before processing and returning to users.
  bool wait_for_batch_completion_ = false;

  // The order number of each argument when the operator sends request in batch fashion.
  int64_t batch_row_ordering_counter_ = 0;
};

//--------------------------------------------------------------------------------------------------

class PgDocWriteOp : public PgDocOp {
 public:
  // Public types.
  typedef std::shared_ptr<PgDocWriteOp> SharedPtr;
  typedef std::shared_ptr<const PgDocWriteOp> SharedPtrConst;

  typedef std::unique_ptr<PgDocWriteOp> UniPtr;
  typedef std::unique_ptr<const PgDocWriteOp> UniPtrConst;

  // Constructors & Destructors.
  PgDocWriteOp(const PgSession::ScopedRefPtr& pg_session,
               const PgObjectId& relation_id,
               std::unique_ptr<client::YBPgsqlWriteOp> write_op);

  // For write ops, we are not yet batching ybctid from index query.
  CHECKED_STATUS SetBatchArgYbctid(const vector<Slice> *ybctids,
                                   const PgTableDesc *table_desc) override {
    return Status::OK();
  }

 private:
  CHECKED_STATUS SendRequestImpl(bool force_non_bufferable) override;
  Result<std::list<PgDocResult>> ProcessResponseImpl() override;
  int32_t GetRowsAffectedCountImpl() const override { return rows_affected_count_; }

  std::shared_ptr<client::YBPgsqlWriteOp> write_op_;
  PgObjectId relation_id_;
  int32_t rows_affected_count_ = 0;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DOC_OP_H_
