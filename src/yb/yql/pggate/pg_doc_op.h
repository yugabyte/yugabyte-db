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

#pragma once

#include <list>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "yb/common/hybrid_time.h"

#include "yb/gutil/macros.h"

#include "yb/util/locks.h"
#include "yb/util/lw_function.h"
#include "yb/util/ref_cnt_buffer.h"

#include "yb/yql/pggate/pg_doc_metrics.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_sys_table_prefetcher.h"

namespace yb::pggate {

YB_STRONGLY_TYPED_BOOL(RequestSent);
YB_STRONGLY_TYPED_BOOL(KeepOrder);

//--------------------------------------------------------------------------------------------------
// PgDocResult represents a batch of rows in ONE reply from tablet servers.
class PgDocResult {
 public:
  explicit PgDocResult(rpc::SidecarHolder data, std::vector<int64_t>&& row_orders = {});

  PgDocResult(const PgDocResult&) = delete;
  PgDocResult& operator=(const PgDocResult&) = delete;

  // Get the order of the next row in this batch.
  int64_t NextRowOrder();

  // End of this batch.
  bool is_eof() const {
    return row_count_ == 0 || row_iterator_.empty();
  }

  // Get the postgres tuple from this batch.
  Status WritePgTuple(
      const std::vector<PgFetchedTarget*>& targets, PgTuple* pg_tuple, int64_t* row_order);

  // Get system columns' values from this batch.
  // Currently, we only have ybctids, but there could be more.
  Status ProcessSystemColumns();

  // Update the reservoir with ybctids from this batch.
  // The update is expected to be sparse, so ybctids come as index/value pairs.
  Status ProcessSparseSystemColumns(std::string* reservoir);

  // Access function to ybctids value in this batch.
  // Sys columns must be processed before this function is called.
  const std::vector<Slice>& ybctids() const {
    DCHECK(syscol_processed_) << "System columns are not yet setup";
    return ybctids_;
  }

  // Row count in this batch.
  int64_t row_count() const {
    return row_count_;
  }

 private:
  // Data selected from DocDB.
  rpc::SidecarHolder data_;

  // Iterator on "data_" from row to row.
  Slice row_iterator_;

  // The row number of only this batch.
  int64_t row_count_ = 0;

  // The indexing order of the row in this batch.
  // These order values help to identify the row order across all batches.
  using RowOrders = std::vector<int64_t>;
  RowOrders row_orders_;
  RowOrders::const_iterator current_row_order_;

  // System columns.
  // - ybctids_ contains pointers to the buffers "data_".
  // - System columns must be processed before these fields have any meaning.
  std::vector<Slice> ybctids_;
  bool syscol_processed_ = false;
};

//--------------------------------------------------------------------------------------------------
// Doc operation API
// Classes
// - PgDocOp: Shared functionalities among all ops, mostly just RPC calls to tablet servers.
// - PgDocReadOp: Definition for data & method members to be used in READ operation.
// - PgDocWriteOp: Definition for data & method members to be used in WRITE operation.
// - PgDocResult: Definition data holder before they are passed to Postgres layer.
//
// Processing Steps
// (1) Collecting Data:
//     PgGate collects data from Posgres and write to a "PgDocOp::Template".
//
// (2) Create operators:
//     When no optimization is applied, the "template_op" is executed as is. When an optimization
//     is chosen, PgDocOp will clone the template to populate operators and kept them in vector
//     "pgsql_ops_". When an op executes arguments, it sends request and reads replies from servers.
//
//     * Vector "pgsql_ops_" is of fixed size for the entire execution, and its contents (YBPgsqlOp
//       shared_ptrs) also remain for the entire execution.
//     * There is a LIMIT on how many pgsql-op can be cloned. If the number of requests / arguments
//       are higher than the LIMIT, some requests will have to wait in queue until the execution
//       of precedent arguments are completed.
//     * After an argument input is executed, its associated YBPgsqlOp will be reused to execute
//       a new set of arguments. We don't clone new ones for new arguments.
//     * When a YBPgsqlOp is reused, its YBPgsqlOp::ProtobufRequest will be updated appropriately
//       with new arguments.
//     * NOTE: Some operators in "pgsql_ops_" might not be active (no arguments) at a given time
//       of execution. For example, some ops might complete their execution while others have
//       paging state and are sent again to table server.
//
// (3) SendRequest:
//     PgSession API requires contiguous array of operators. For this reason, before sending the
//     pgsql_ops_ is soreted to place active ops first, and all inactive ops are place at the end.
//     For example,
//        PgSession::RunAsync(pgsql_ops_.data(), active_op_count)
//
// (4) ReadResponse:
//     Response are written to a local cache PgDocResult.
//
// This API has several sets of methods and attributes for different purposes.
// (1) Build request.
//  This section collect information and data from PgGate API.
//  * Attributes
//    - relation_id_: Table to be operated on.
//    - template_op_ of type YBPgsqlReadOp and YBPgsqlWriteOp.
//      This object contains statement descriptions and expression values from users.
//      All user-provided arguments are kept in this attributes.
//  * Methods
//    - Class constructors.
//
// (2) Constructing protobuf request.
//  This section populates protobuf requests using the collected information in "template_op_".
//  - Without optimization, the protobuf request in "template_op_" will be used .
//  - With parallel optimization, multiple protobufs are constructed by cloning template into many
//    operators. How the execution are subdivided is depending on the parallelism method.
//  NOTE Whenever we support PREPARE(stmt), we'd stop processing at after this step for PREPARE.
//
//  * Attributes
//    - YBPgsqlOp pgsql_ops_: Contains all protobuf requests to be sent to tablet servers.
//  * Methods
//    - When there isn't any optimization, template_op_ is used.
//        pgsql_ops_[0] = template_op_
//    - CreateRequests()
//    - ClonePgsqlOps() Clone template_op_ into one or more ops.
//    - PopulateParallelSelectOps() Parallel processing of aggregate requests or requests with
//      WHERE expressions filtering rows in DocDB.
//      The same requests are constructed for each tablet server.
//    - PopulateNextHashPermutationOps() Parallel processing SELECT by hash conditions.
//      Hash permutations will be group into different request based on their hash_codes.
//    - PopulateByYbctidOps() Parallel processing SELECT by ybctid values.
//      Ybctid values will be group into different request based on their hash_codes.
//      This function is a bit different from other formulating function because it is used for an
//      internal request within PgGate. Other populate functions are used for external requests
//      from Postgres layer via PgGate API.
//
// (3) Execution
//  This section exchanges RPC calls with tablet servers.
//  * Attributes
//    - active_op_counts_: Number of active operators in vector "pgsql_ops_".
//        Exec/active op range = pgsql_ops_[0, active_op_count_)
//        Inactive op range = pgsql_ops_[active_op_count_, total_count)
//      The vector pgsql_ops_ is fixed sized, can have inactive operators as operators are not
//      completing execution at the same time.
//  * Methods
//    - ExecuteInit()
//    - Execute() Driver for all RPC related effort.
//    - SendRequest() Send request for active operators to tablet server using YBPgsqlOp.
//        RunAsync(pgsql_ops_.data(), active_op_count_)
//    - ProcessResponse() Get response from tablet server using YBPgsqlOp.
//    - MoveInactiveOpsOutside() Sort pgsql_ops_ to move inactive operators outside of exec range.
//
// (4) Return result
//  This section return result via PgGate API to postgres.
//  * Attributes
//    - Objects of class PgDocResult
//    - rows_affected_count_: Number of rows that was operated by this doc_op.
//  * Methods
//    - GetResult()
//    - GetRowsAffectedCount()
//
// TODO(dmitry / neil) Allow sending active requests and receive their response one at a time.
//
// To process data in parallel, the operators must be able to run independently from one another.
// However, currently operators are executed in batches and together even though they belong to
// different partitions and interact with different tablet servers.
//--------------------------------------------------------------------------------------------------

YB_STRONGLY_TYPED_BOOL(IsForWritePgDoc);

// Helper class to wrap PerformFuture and custom response provider.
// No memory allocations is required in case of using PerformFuture.
class PgDocResponse {
 public:
  using Data = PerformFuture::Data;

  class Provider {
   public:
    virtual ~Provider() = default;
    virtual Result<Data> Get() = 0;
  };

  struct MetricInfo {
    MetricInfo(TableType table_type_, IsForWritePgDoc is_write_)
        : table_type(table_type_), is_write(is_write_) {}

    TableType table_type;
    IsForWritePgDoc is_write;
  };

  struct FutureInfo {
    FutureInfo() : metrics(TableType::USER, IsForWritePgDoc::kFalse) {}
    FutureInfo(PerformFuture&& future_, const MetricInfo& metrics_)
        : future(std::move(future_)), metrics(metrics_) {}

    PerformFuture future;
    MetricInfo metrics;
  };

  using ProviderPtr = std::unique_ptr<Provider>;

  PgDocResponse() = default;
  PgDocResponse(PerformFuture&& future, const MetricInfo& info);
  explicit PgDocResponse(ProviderPtr provider);

  bool Valid() const;
  Result<Data> Get(PgSession& session);

 private:
  std::variant<FutureInfo, ProviderPtr> holder_;
};

class PgDocOp : public std::enable_shared_from_this<PgDocOp> {
 public:
  using SharedPtr = std::shared_ptr<PgDocOp>;

  using Sender = std::function<Result<PgDocResponse>(
      PgSession*, const PgsqlOpPtr*, size_t, const PgTableDesc&, HybridTime,
      ForceNonBufferable, IsForWritePgDoc)>;

  struct OperationRowOrder {
    OperationRowOrder(const PgsqlOpPtr& operation_, int64_t order_)
        : operation(operation_), order(order_) {}

    std::weak_ptr<const PgsqlOp> operation;
    int64_t order;
  };

  using OperationRowOrders = std::vector<OperationRowOrder>;

  virtual ~PgDocOp() = default;

  // Initialize doc operator.
  virtual Status ExecuteInit(const PgExecParameters *exec_params);

  const PgExecParameters& ExecParameters() const;

  // Execute the op. Return true if the request has been sent and is awaiting the result.
  virtual Result<RequestSent> Execute(
      ForceNonBufferable force_non_bufferable = ForceNonBufferable::kFalse);

  // Instruct this doc_op to abandon execution and querying data by setting end_of_data_ to 'true'.
  // - This op will not send request to tablet server.
  // - This op will return empty result-set when being requested for data.
  void AbandonExecution() {
    end_of_data_ = true;
  }

  // Get the result of the op. No rows will be added to rowsets in case end of data reached.
  virtual Result<std::list<PgDocResult>> GetResult();
  Result<int32_t> GetRowsAffectedCount() const;

  struct YbctidGenerator {
    using Next = LWFunction<Slice()>;

    YbctidGenerator(const Next& next_, size_t capacity_)
        : next(next_), capacity(capacity_) {}

    const Next& next;
    const size_t capacity;
  };

  // This operation is requested internally within PgGate, and that request does not go through
  // all the steps as other operation from Postgres thru PgDocOp. This is used to create requests
  // for the following select.
  //   SELECT ... FROM <table> WHERE ybctid IN (SELECT base_ybctids from INDEX)
  // After ybctids are queried from INDEX, PgGate will call "PopulateByYbctidOps" to create
  // operators to fetch rows whose rowids equal queried ybctids.
  // Response will have same order of ybctids as request in case of using KeepOrder::kTrue.
  Status PopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder = KeepOrder::kFalse);

  bool has_out_param_backfill_spec() {
    return !out_param_backfill_spec_.empty();
  }

  const char* out_param_backfill_spec() {
    return out_param_backfill_spec_.c_str();
  }

  bool end_of_data() const {
    return end_of_data_;
  }

  virtual bool IsWrite() const = 0;

  Status CreateRequests();

  const PgTable& table() const { return table_; }

  static Result<PgDocResponse> DefaultSender(
      PgSession* session, const PgsqlOpPtr* ops, size_t ops_count, const PgTableDesc& table,
      HybridTime in_txn_limit, ForceNonBufferable force_non_bufferable, IsForWritePgDoc is_write);

 protected:
  PgDocOp(
    const PgSession::ScopedRefPtr& pg_session, PgTable* table,
    const Sender& = Sender(&PgDocOp::DefaultSender));

  // Populate Protobuf requests using the collected information for this DocDB operator.
  virtual Result<bool> DoCreateRequests() = 0;

  virtual Status DoPopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder keep_order) = 0;

  // Only active operators are kept in the active range [0, active_op_count_)
  // - Not execute operators that are outside of range [0, active_op_count_).
  // - Sort the operators in "pgsql_ops_" to move "inactive" operators to the end of the list.
  void MoveInactiveOpsOutside();

  // Session control.
  PgSession::ScopedRefPtr pg_session_;

  // Target table.
  PgTable& table_;

  // Exec control parameters.
  PgExecParameters exec_params_;

  // Suppress sending new request after processing response.
  // Next request will be sent in case upper level will ask for additional data.
  bool suppress_next_result_prefetching_ = false;

  // Populated protobuf request.
  std::vector<PgsqlOpPtr> pgsql_ops_;

  // Number of active operators in the pgsql_ops_ list.
  size_t active_op_count_ = 0;

  // Indicator for completing all request populations.
  bool request_population_completed_ = false;

  // Object to fetch a response from DocDB after sending a request.
  // Object's Valid() method returns false in case no request is sent
  // or sent request was buffered by the session.
  // Only one RunAsync() can be called to sent to DocDB at a time.
  PgDocResponse response_;

  // Executed row count.
  int32_t rows_affected_count_ = 0;

  // Whether all requested data by the statement has been received or there's a run-time error.
  bool end_of_data_ = false;

  // The order number of each request when batching arguments.
  // Currently, this is used for query by YBCTID.
  // - Each pgsql_op has a batch of ybctids selected from INDEX.
  // - The order of resulting rows should match with the order of queried ybctids.
  // - Example:
  //   Suppose we got from INDEX table
  //     { ybctid_1, ybctid_2, ybctid_3, ybctid_4, ybctid_5, ybctid_6, ybctid_7 }
  //
  //   Now pgsql_op are constructed as the following, one op per partition.
  //     pgsql_op <partition 1> (ybctid_1, ybctid_3, ybctid_4)
  //     pgsql_op <partition 2> (ybctid_2, ybctid_6)
  //     pgsql_op <partition 2> (ybctid_5, ybctid_7)
  //
  //  These respective ybctids are stored in batch_ybctid_ also.
  //  In other words,
  //     batch_ybctid_[partition 1] contains (ybctid_1, ybctid_3, ybctid_4)
  //     batch_ybctid_[partition 2] contains (ybctid_2, ybctid_6)
  //     batch_ybctid_[partition 3] contains (ybctid_5, ybctid_7)
  //
  //   After getting the rows of data from pgsql, the rows must be then ordered from 1 thru 7.
  //   To do so, for each pgsql_op we kept an array of orders, batch_row_orders_.
  //  Caution: batch_row_orders_ might have irrelevant data in case of dynamic tablet splitting
  //           In the vast majority of cases ordering information will came with response.
  //           Local ordering info is only required for upgrade scenario when new YSQL communicates
  //           With quite old t-server. In future this field must be removed.
  OperationRowOrders batch_row_orders_;

  // This counter is used to maintain the row order when the operator sends requests in parallel
  // by partition. Currently only query by YBCTID uses this variable.
  int64_t batch_row_ordering_counter_ = 0;

  // Parallelism level.
  // - This is the maximum number of read/write requests being sent to servers at one time.
  // - When it is 1, there's no optimization. Available requests is executed one at a time.
  size_t parallelism_level_ = 1;

  // Output parameter of the execution.
  std::string out_param_backfill_spec_;

 private:
  Status SendRequest(ForceNonBufferable force_non_bufferable = ForceNonBufferable::kFalse);

  Status SendRequestImpl(ForceNonBufferable force_non_bufferable);

  void RecordRequestMetrics();

  Result<std::list<PgDocResult>> ProcessResponse(const Result<PgDocResponse::Data>& data);

  Result<std::list<PgDocResult>> ProcessResponseImpl(const Result<PgDocResponse::Data>& data);

  Result<std::list<PgDocResult>> ProcessCallResponse(const rpc::CallResponse& response);

  virtual Status CompleteProcessResponse() = 0;

  Status CompleteRequests();

  // Returns a reference to the in_txn_limit_ht to be used.
  //
  // For read ops: usually one in txn limit is chosen for all for read ops of a SQL statement. And
  // the hybrid time in such a situation references the statement level integer that is passed down
  // to all PgDocOp instances via PgExecParameters.
  //
  // In case the reference to the statement level in_txn_limit_ht isn't passed in PgExecParameters,
  // the local in_txn_limit_ht_ is used which is 0 at the start of the PgDocOp.
  //
  // For writes: the local in_txn_limit_ht_ is used if available.
  //
  // See ReadHybridTimePB for more details about in_txn_limit.
  uint64_t& GetInTxnLimitHt();

  // Result set either from selected or returned targets is cached in a list of strings.
  // Querying state variables.
  Status exec_status_ = Status::OK();

  Sender sender_;

  // See ReadHybridTimePB for more details about in_txn_limit.
  uint64_t in_txn_limit_ht_ = 0;

  DISALLOW_COPY_AND_ASSIGN(PgDocOp);
};

//--------------------------------------------------------------------------------------------------

class PgDocReadOp : public PgDocOp {
 public:
  PgDocReadOp(const PgSession::ScopedRefPtr& pg_session, PgTable* table, PgsqlReadOpPtr read_op);
  PgDocReadOp(
      const PgSession::ScopedRefPtr& pg_session, PgTable* table,
      PgsqlReadOpPtr read_op, const Sender& sender);

  Status ExecuteInit(const PgExecParameters *exec_params) override;

  // Row sampler collects number of live and dead rows it sees.
  Status GetEstimatedRowCount(double *liverows, double *deadrows);

  bool IsWrite() const override {
    return false;
  }

  Status DoPopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder keep_order) override;

  Status ResetPgsqlOps();

 private:
  // Create protobuf requests using template_op_.
  Result<bool> DoCreateRequests() override;

  // Create operators by partition.
  // - Optimization for statement
  //     SELECT xxx FROM <table> WHERE ybctid IN (SELECT ybctid FROM INDEX)
  // - After being queried from inner select, ybctids are used for populate request for outer query.
  void InitializeYbctidOperators();

  bool IsHashBatchingEnabled();

  bool IsBatchFlushRequired() const;

  void ResetHashBatches();

  // Create operators by partition arguments.
  // - Optimization for statement:
  //     SELECT ... WHERE <hash-columns> IN <value-lists>
  // - If partition column binds are defined, partition_column_values field of each operation
  //   is set to be the next permutation.
  // - When an operator is assigned a hash permutation, it is marked as active to be executed.
  // - When an operator completes the execution, it is marked as inactive and available for the
  //   exection of the next hash permutation.
  Result<bool> PopulateNextHashPermutationOps();
  void InitializeHashPermutationStates();

  // Binds the given hash and range values to the given read request.
  // hashed_values and range_values have the same descriptions as in BindExprsToBatch.
  void BindExprsRegular(
      LWPgsqlReadRequestPB* read_req,
      const std::vector<const LWPgsqlExpressionPB*>& hashed_values,
      const std::vector<const LWPgsqlExpressionPB*>& range_values);

  // Helper functions for when we are batching hash permutations where
  // we are creating an IN condition of the form
  // (yb_hash_code(hashkeys), h1, h2, ..., hn) IN (tuple_1, tuple_2, tuple_3, ...)

  // This operates by creating one such IN condition for each partition we are sending
  // our query to and buidling each of them up in hash_in_conds_[partition_idx]. We make sure
  // that each permutation value goes into the correct partition's condition. We then
  // make one read op clone per partition and for each one we bind their respective condition
  // from hash_in_conds_[partition_idx].

  // This prepares the LHS of the hash IN condition for a particular partition.
  void PrepareInitialHashConditionList(size_t partition);

  // Binds the given hash and range values to whatever partition in hash_in_conds_
  // the hashed values suggest. The range_values vector is expected to be a
  // vector of size num_range_keys where a range_values[i] == nullptr iff
  // the ith range column is not relevant to the IN condition we are building
  // up.
  void BindExprsToBatch(
      const std::vector<const LWPgsqlExpressionPB*>& hashed_values,
      const std::vector<const LWPgsqlExpressionPB*>& range_values);

  // These functions are used to iterate over each partition batch and bind them to a request.

  Result<bool> BindBatchesToRequests();

  // Returns false if we are done iterating over our partition batches.
  bool HasNextBatch();

  // Binds the next partition batch available to the given request's condition expression.
  Result<bool> BindNextBatchToRequest(LWPgsqlReadRequestPB* read_req);

  // Helper functions for PopulateNextHashPermutationOps
  // Prepares a new read request from the pool of inactive operators.
  LWPgsqlReadRequestPB* PrepareReadReq();
  // True if the next call to GetNextPermutation will not fail.
  bool HasNextPermutation() const;
  // Gets the next possible permutation of partition_exprs.
  bool GetNextPermutation(std::vector<const LWPgsqlExpressionPB*>* exprs);
  // Binds a given permutation of partition expressions to the given read request.
  void BindPermutation(const std::vector<const LWPgsqlExpressionPB*>& exprs,
                       LWPgsqlReadRequestPB* read_op);

  // Create operators by partitions.
  // - Optimization for aggregating or filtering requests.
  Result<bool> PopulateParallelSelectOps();

  // Create one sampling operator per partition and arrange their execution in random order
  Result<bool> PopulateSamplingOps();

  // Set partition boundaries to a given partition. Return true if new boundaries combined with
  // old boundaries, if any, are non-empty range. Obviously, there's no need to send request with
  // empty range boundaries, because the result will be empty.
  Result<bool> SetScanPartitionBoundary();

  Status CompleteProcessResponse() override;

  // Process response read state from DocDB.
  Status ProcessResponseReadStates();

  // Reset pgsql operators before reusing them with new arguments / inputs from Postgres.
  void ResetInactivePgsqlOps();

  // Analyze options and pick the appropriate prefetch limit.
  Status SetRequestPrefetchLimit();

  // Set the backfill_spec field of our read request.
  void SetBackfillSpec();

  // Set the row_mark_type field of our read request based on our exec control parameter.
  void SetRowMark();

  // Set the read_time for our backfill's read request based on our exec control parameter.
  void SetReadTimeForBackfill();

  // Set bounds on the request so it only affect specified partition
  // Returns false if current bounds fully exclude the partition
  Result<bool> SetLowerUpperBound(LWPgsqlReadRequestPB* request, size_t partition);

  // Get the read_op for a specific operation index from pgsql_ops_.
  PgsqlReadOp& GetReadOp(size_t op_index);

  // Get the read_req for a specific operation index from pgsql_ops_.
  LWPgsqlReadRequestPB& GetReadReq(size_t op_index);

  // Re-format the request when connecting to older server during rolling upgrade.
  void FormulateRequestForRollingUpgrade(LWPgsqlReadRequestPB *read_req);

  // Create operators.
  // - Each operator is used for one request.
  // - When parallelism by partition is applied, each operator is associated with one partition,
  //   and each operator has a batch of arguments that belong to that partition.
  //   * The higher the number of partition_count, the higher the parallelism level.
  //   * If (partition_count == 1), only one operator is needed for the entire partition range.
  //   * If (partition_count > 1), each operator is used for a specific partition range.
  //   * This optimization is used by
  //       PopulateByYbctidOps()
  //       PopulateParallelSelectOps()
  // - When parallelism by arguments is applied, each operator has only one argument.
  //   When tablet server will run the requests in parallel as it assigned one thread per request.
  //       PopulateNextHashPermutationOps()
  void ClonePgsqlOps(size_t op_count);

  //----------------------------------- Data Members -----------------------------------------------

  // Whether or not we are using hash permutation batching for this op.
  boost::optional<bool> is_hash_batched_;

  // Pointer to the per tablet hash component condition expression. For each hash key
  // combination, once we identify the partition at which it should be executed, we enqueue
  // it into this vector among the other hash keys that are to be executed in that tablet.
  // This vector contains a reference to the vector that contains the list of hash keys
  // that are supposed to be executed in each tablet.

  // This is a vector of IN condition expressions for each partition. In each partition's
  // condition expression the LHS is a tuple of the hash code and hash keys and the RHS
  // is built up to eventually be a list of all the hash key permutations
  // that belong to that partition. These conditions are eventually bound
  // to a read op's condition expression.
  std::vector<LWPgsqlExpressionPB*> hash_in_conds_;

  // Sometimes in the final hash IN condition's LHS, range columns may be involved.
  // For example, if we get a filter of the form (h1, h2, r2) IN (t2, t2, t3), commonly found
  // in the case of batched nested loop joins. These should be sorted.
  std::vector<size_t> permutation_range_column_indexes_;

  // Used when iterating over the partition batches in hash_in_conds_.
  size_t next_batch_partition_ = 0;

  // Used to store temporary objects formed during op creation. Relevant
  // when cloning ops for hash permutations.
  std::unique_ptr<ThreadSafeArena> hash_key_arena_;

  // Arena for operations fetching from the main table by keys retrieved from the secondary index.
  // Those can take billions of keys to fetch, and we want to be able to periodically release
  // their memory, without harming the template operation and stuff, hence separate arena.
  std::shared_ptr<ThreadSafeArena> pgsql_op_arena_;

  // Template operation, used to fill in pgsql_ops_ by either assigning or cloning.
  PgsqlReadOpPtr read_op_;

  // While sampling is in progress, number of scanned row is accumulated in this variable.
  // After completion the value is extrapolated to account for not scanned partitions and estimate
  // total number of rows in the table.
  double sample_rows_ = 0;

  // Used internally for PopulateNextHashPermutationOps to keep track of which permutation should
  // be used to construct the next read_op.
  // Is valid as long as request_population_completed_ is false.
  //
  // Example:
  // For a query clause "h1 = 1 AND h2 IN (2,3) AND h3 IN (4,5,6) AND h4 = 7",
  // there are 1*2*3*1 = 6 possible permutation.
  // As such, this field will take on values 0 through 5.
  size_t total_permutation_count_ = 0;
  size_t next_permutation_idx_ = 0;

  // Used internally for PopulateNextHashPermutationOps to holds all partition expressions.
  // Elements correspond to a hash columns, in the same order as they were defined
  // in CREATE TABLE statement.
  // This is somewhat similar to what hash_values_options_ in CQL is used for.
  //
  // Example:
  // For a query clause "h1 = 1 AND h2 IN (2,3) AND h3 IN (4,5,6) AND h4 = 7",
  // this will be initialized to [[1], [2, 3], [4, 5, 6], [7]]
  // For a query clause "(h1,h3) IN ((1,5),(2,3)) AND h2 IN (2,4)"
  // the will be initialized to [[(1,5), (2,3)], [(2,4)], []]
  std::vector<std::vector<const LWPgsqlExpressionPB*>> partition_exprs_;
};

//--------------------------------------------------------------------------------------------------

class PgDocWriteOp : public PgDocOp {
 public:
  PgDocWriteOp(const PgSession::ScopedRefPtr& pg_session,
               PgTable* table,
               PgsqlWriteOpPtr write_op);

  // Set write time.
  void SetWriteTime(const HybridTime& write_time);

  bool IsWrite() const override {
    return true;
  }

 private:
  Status CompleteProcessResponse() override;

  // Create protobuf requests using template_op (write_op).
  Result<bool> DoCreateRequests() override;

  // For write ops, we are not yet batching ybctid from index query.
  // TODO(neil) This function will be implemented when we push down sub-query inside WRITE ops to
  // the proxy layer. There's many scenarios where this optimization can be done.
  Status DoPopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder keep_order) override {
    LOG(FATAL) << "Not yet implemented";
    return Status::OK();
  }

  // Get WRITE operator for a specific operator index in pgsql_ops_.
  LWPgsqlWriteRequestPB& GetWriteOp(int op_index);

  //----------------------------------- Data Members -----------------------------------------------
  // Template operation all write ops.
  PgsqlWriteOpPtr write_op_;
};

PgDocOp::SharedPtr MakeDocReadOpWithData(
    const PgSession::ScopedRefPtr& pg_session, PrefetchedDataHolder data);

}  // namespace yb::pggate
