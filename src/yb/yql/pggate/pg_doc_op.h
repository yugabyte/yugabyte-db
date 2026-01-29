//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "yb/common/hybrid_time.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"

#include "yb/gutil/macros.h"

#include "yb/util/concepts.h"
#include "yb/util/locks.h"
#include "yb/util/lw_function.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"

#include "yb/yql/pggate/pg_doc_metrics.h"
#include "yb/yql/pggate/pg_doc_op_fetch_stream.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_sys_table_prefetcher.h"
#include "yb/yql/pggate/util/pg_tuple.h"

namespace yb::pggate {

YB_STRONGLY_TYPED_BOOL(RequestSent);
YB_STRONGLY_TYPED_BOOL(KeepOrder);

//--------------------------------------------------------------------------------------------------
// Doc operation API
// Classes
// - PgDocOp: Shared functionalities among all ops, mostly just RPC calls to tablet servers.
// - PgDocReadOp: Definition for data & method members to be used in READ operation.
// - PgDocWriteOp: Definition for data & method members to be used in WRITE operation.
// - DocResult: Definition data holder before they are passed to Postgres layer.
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
//     pgsql_ops_ is sorted to place active ops first, and all inactive ops are place at the end.
//
// (4) ReadResponse:
//     Response are written to a local cache DocResult.
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
//  * Methods
//    - ExecuteInit()
//    - Execute() Driver for all RPC related effort.
//    - SendRequest() Send request for active operators to tablet server using YBPgsqlOp.
//    - ProcessResponse() Get response from tablet server using YBPgsqlOp.
//    - MoveInactiveOpsOutside() Sort pgsql_ops_ to move inactive operators outside of exec range.
//
// (4) Return result
//  This section return result via PgGate API to postgres.
//  * Attributes
//    - Objects of class DocResult
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
YB_STRONGLY_TYPED_BOOL(IsOpBuffered);

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
    MetricInfo(TableType table_type_,
               IsForWritePgDoc is_write_,
               IsOpBuffered is_buffered_)
        : table_type(table_type_), is_write(is_write_), is_buffered(is_buffered_) {}

    TableType table_type;
    IsForWritePgDoc is_write;
    IsOpBuffered is_buffered;
  };

  struct FutureInfo {
    FutureInfo() : metrics(TableType::USER, IsForWritePgDoc::kFalse, IsOpBuffered::kFalse) {}
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

class PgDocOp {
 public:
  using SharedPtr = std::shared_ptr<PgDocOp>;

  using Sender = std::function<Result<PgDocResponse>(
      PgSession*, std::span<const PgsqlOpPtr>, const PgTableDesc&, const PgSession::RunOptions&,
      IsForWritePgDoc)>;

  virtual ~PgDocOp() = default;

  // Initialize doc operator.
  virtual Status ExecuteInit(const YbcPgExecParameters* exec_params);

  const YbcPgExecParameters& ExecParameters() const;

  // Execute the op. Return true if the request has been sent and is awaiting the result.
  virtual Result<RequestSent> Execute(
      ForceNonBufferable force_non_bufferable = ForceNonBufferable::kFalse);

  // Instruct this doc_op to abandon execution and querying data by setting end_of_data_ to 'true'.
  // - This op will not send request to tablet server.
  // - This op will return empty result-set when being requested for data.
  void AbandonExecution() {
    DCHECK(!HasActiveOps());
    end_of_data_ = true;
  }

  Result<int32_t> GetRowsAffectedCount() const;

  // Get the results and hand them over to the result stream.
  // Send requests for new pages if necessary.
  virtual Status FetchMoreResults();
  PgDocOpFetchStream& ResultStream();
  void SetFetchedTargets(FetchedTargetsPtr targets);

  // This operation is requested internally within PgGate, and that request does not go through
  // all the steps as other operation from Postgres thru PgDocOp.
  // Ybctids from the generator may be skipped if they conflict with other conditions placed on the
  // request. Function returns true result if it ended up with any requests to execute.
  // Response will have same order of ybctids as request in case of using KeepOrder::kTrue.
  Result<bool> PopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder = KeepOrder::kFalse);

  // Create PgsqlOp instances and adjust their requests for the merge streams defined by provided
  // sort keys and conditions on the template requests.
  // Check requests boundaries and discard those that are out of bounds.
  // Returns true if requests are successfully created, false if all are out of bounds.
  Result<bool> PopulateMergeStreams(MergeSortKeysPtr merge_sort_keys);

  bool has_out_param_backfill_spec() {
    return !out_param_backfill_spec_.empty();
  }

  const char* out_param_backfill_spec() {
    return out_param_backfill_spec_.c_str();
  }

  virtual bool IsWrite() const = 0;

  Result<size_t> CreateRequests();

  const PgTable& table() const { return table_; }

  static Result<PgDocResponse> DefaultSender(
      PgSession* session, std::span<const PgsqlOpPtr> ops, const PgTableDesc& table,
      const PgSession::RunOptions& options, IsForWritePgDoc is_write);

 protected:
  PgDocOp(
    const PgSession::ScopedRefPtr& pg_session, PgTable* table,
    const Sender& = Sender(&PgDocOp::DefaultSender));

  // Populate Protobuf requests using the collected information for this DocDB operator.
  virtual Result<bool> DoCreateRequests() = 0;

  virtual Status DoPopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder keep_order) = 0;

  virtual Status DoPopulateMergeStreams(MergeSortKeysPtr merge_sort_keys) = 0;

  // Sorts the operators in "pgsql_ops_" to move "inactive" operators to the end of the list.
  size_t MoveInactiveOpsOutside();

  // If there is a result stream, reset it.
  void ResetResultStream();

  // If there is a result stream, add current operations to it.
  void AddOpsToResultStream();

  auto ActiveOps() const;
  bool HasActiveOps() const;

  // Session control.
  PgSession::ScopedRefPtr pg_session_;

  // Target table.
  PgTable& table_;

  // Exec control parameters.
  YbcPgExecParameters exec_params_;

  // Suppress sending new request after processing response.
  // Next request will be sent in case upper level will ask for additional data.
  bool suppress_next_result_prefetching_ = false;

  // Populated protobuf request.
  std::vector<PgsqlOpPtr> pgsql_ops_;

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

  std::unique_ptr<PgDocOpFetchStream> result_stream_;
  FetchedTargetsPtr targets_;

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

  Status ProcessResponse(const Result<PgDocResponse::Data>& data);

  Status ProcessResponseImpl(const Result<PgDocResponse::Data>& data);

  Status ProcessCallResponse(const rpc::CallResponse& response);

  virtual Status CompleteProcessResponse() = 0;

  Result<size_t> CompleteRequests();

  // Returns a reference to the in_txn_limit_ht to be used.
  //
  // For read ops: usually one in txn limit is chosen for all for read ops of a SQL statement. And
  // the hybrid time in such a situation references the statement level integer that is passed down
  // to all PgDocOp instances via YbcPgExecParameters.
  //
  // In case the reference to the statement level in_txn_limit_ht isn't passed in
  // YbcPgExecParameters, the local in_txn_limit_ht_ is used which is 0 at the start of the PgDocOp.
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
// Classes to facilitate IN clause permutations.
// The input is one or more expressions of following supported types:
// 1. Single value representing an equality condition
// 2. A tuple of values representing IN clause condition
// 3. A tuple of tuples, where the first inner tuple contains column references, and remaining
//    tuples contain values, representing ROW(<columns>) IN (ROW(values1), ... ROW(valuesN))
// The facility is initialized by the InPermutationBuilder class. The participating expressions are
// added to the builder using AddExpression method. Since expressions of type 1. and 2. do not
// contain column information, the caller must provide the index of the associated column. If the
// expression is of type 3., the indexes are retrieved from the expression and the index passed in
// is ignored. The participating expressions may only refer table's key columns, must refer all the
// hash column and must not refer any column more than once.
// After all participating expressions are added to the builder, the Build() method initializes and
// returns the permutations state. Call to Build() invalidates the builder.
// The InPermutationGenerator class is the core of the permutations state. It tracks the current
// permutation and provides it as a vector of pointers to the values in the respective expressions.
// Not referenced indexes in the permutation contain null pointers.
// The InExpressionWrapper keeps current position in one participating expression and configured to
// efficiently retrieve current value(s) into the permutation.
class InExpressionWrapper {
 public:
  static InExpressionWrapper Make(
      const PgTable& table, size_t idx, const LWPgsqlExpressionPB* expr);

  const std::vector<size_t>& Targets() const { return targets_; }
  size_t Size() const { return values_.size(); }

  bool Next();
  void GetValues(std::vector<const LWQLValuePB*>* permutation);
  void ResetPos() { pos_ = 0; }

 private:
  InExpressionWrapper(
      std::vector<size_t>&& targets, std::vector<const LWPgsqlExpressionPB*>&& values,
      void (InExpressionWrapper::*fn)(std::vector<const LWQLValuePB*>*))
      : pos_(0),
        targets_(std::move(targets)),
        values_(std::move(values)),
        GetValuesFn_(fn) {}

  // GetValues implementations for supported expression types
  void GetSingleValue(std::vector<const LWQLValuePB*>* permutation);
  void GetInValue(std::vector<const LWQLValuePB*>* permutation);
  void GetRowInValues(std::vector<const LWQLValuePB*>* permutation);

  size_t pos_;
  const std::vector<size_t> targets_;
  const std::vector<const LWPgsqlExpressionPB*> values_;
  void (InExpressionWrapper::*GetValuesFn_)(std::vector<const LWQLValuePB*>*);
};

class InPermutationGenerator {
 public:
  InPermutationGenerator(InPermutationGenerator&& other) = default;

  const std::vector<size_t>& Targets() const { return all_targets_; }
  size_t Size();

  bool HasPermutation() { return !done_; }
  const std::vector<const LWQLValuePB*>& NextPermutation();
  void Reset();

 private:
  friend class InPermutationBuilder;
  InPermutationGenerator(
      PgTable& table,
      std::vector<InExpressionWrapper>&& source_exprs,
      std::vector<size_t>&& targets);
  void Next();

  const std::vector<size_t> all_targets_;
  std::vector<InExpressionWrapper> source_exprs_;
  std::vector<const LWQLValuePB*> current_permutation_;
  bool done_ = false;
  DISALLOW_COPY_AND_ASSIGN(InPermutationGenerator);
};

class InPermutationBuilder {
 public:
  explicit InPermutationBuilder(PgTable& table) : table_(table) {}

  void AddExpression(size_t idx, const LWPgsqlExpressionPB* expr);
  InPermutationGenerator Build();

 private:
  bool TargetsAreValid(const std::vector<size_t>& all_targets);
  PgTable& table_;
  std::vector<InExpressionWrapper> source_exprs_;
};

//--------------------------------------------------------------------------------------------------
class PgDocReadOp : public PgDocOp {
 public:
  PgDocReadOp(const PgSession::ScopedRefPtr& pg_session, PgTable* table, PgsqlReadOpPtr read_op);
  PgDocReadOp(
      const PgSession::ScopedRefPtr& pg_session, PgTable* table,
      PgsqlReadOpPtr read_op, const Sender& sender);

  Status ExecuteInit(const YbcPgExecParameters *exec_params) override;

  bool IsWrite() const override {
    return false;
  }

  Status DoPopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder keep_order) override;

  Status DoPopulateMergeStreams(MergeSortKeysPtr merge_sort_keys) override;

  Status ResetPgsqlOps();

 protected:
  Status CompleteProcessResponse() override;

  // Set bounds on the request so it only affect specified partition
  // Returns false if current bounds fully exclude the partition
  Result<bool> SetLowerUpperBound(LWPgsqlReadRequestPB* request, size_t partition);

  // Get the read_op for a specific operation index from pgsql_ops_.
  PgsqlReadOp& GetReadOp(size_t op_index);

  // Get the read_req for a specific operation index from pgsql_ops_.
  LWPgsqlReadRequestPB& GetReadReq(size_t op_index);

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

  const LWPgsqlReadRequestPB& GetTemplateReadReq() const { return read_op_->read_request(); }

 private:
  using QLValuePBs = std::vector<const LWQLValuePB*>;
  using PartitionBatches = std::vector<std::pair<bool, LWPgsqlExpressionPB*>>;

  // Check request conditions if they allow to limit the scan range
  // Returns true if resulting range is not empty, false otherwise
  Result<bool> SetScanBounds(LWPgsqlReadRequestPB& request);

  // Create protobuf requests using template_op_.
  Result<bool> DoCreateRequests() override;

  // Create operators by partition.
  // - Optimization for statement
  //     SELECT xxx FROM <table> WHERE ybctid IN (SELECT ybctid FROM INDEX)
  // - After being queried from inner select, ybctids are used for populate request for outer query.
  void InitializeYbctidOperators();

  bool IsHashBatchingEnabled();

  bool IsBatchFlushRequired() const;

  // Create operators by partition arguments.
  // - Optimization for statement:
  //     SELECT ... WHERE <hash-columns> IN <value-lists>
  // - If partition column binds are defined, partition_column_values field of each operation
  //   is set to be the next permutation.
  // - When an operator is assigned a hash permutation, it is marked as active to be executed.
  // - When an operator completes the execution, it is marked as inactive and available for the
  //   exection of the next hash permutation.
  Result<bool> PopulateNextHashPermutationOps();
  InPermutationGenerator InitializeHashPermutationStates();

  // Binds the given values to the given read operation.
  // Hash column values are bound to the request's partition_column_values, range column values are
  // added as equality conditions to the condition_expr.
  // Performs boundary check, if the request range is empty, returns false
  Result<bool> BindExprsRegular(LWPgsqlReadRequestPB& read_req, const QLValuePBs& values);

  // Binds the given values to the partition defined by hash column values.
  // The partition_batches vector for each partition stores the flag indicating the partition
  // has been initialized, and the references to initialized RHSs of the batched IN expression,
  // or null, if the partition batch is inactive.
  // Batch may be inactive if it has not been yet initialized, or its request range is empty.
  using ReadOpProvider = LWFunction<Result<PgsqlReadOp&>()>;
  Result<bool> BindExprsToBatch(
      PartitionBatches& partition_batches, const QLValuePBs& values,
      const ReadOpProvider& read_op_provider);
  LWPgsqlExpressionPB* InitHashPermutationBatch(LWPgsqlReadRequestPB& read_req);
  // Create operators by partitions.
  // - Optimization for aggregating or filtering requests.
  Result<bool> PopulateParallelSelectOps();

  // Set partition boundaries to a given partition. Return true if new boundaries combined with
  // old boundaries, if any, are non-empty range. Obviously, there's no need to send request with
  // empty range boundaries, because the result will be empty.
  Result<bool> SetScanPartitionBoundary();

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

  // Re-format the request when connecting to older server during rolling upgrade.
  void FormulateRequestForRollingUpgrade(LWPgsqlReadRequestPB *read_req);

  //----------------------------------- Data Members -----------------------------------------------

  // Template operation, used to fill in pgsql_ops_ by either assigning or cloning.
  PgsqlReadOpPtr read_op_;

  // Arena for pgsql operations. In some cases PgGate may need to create too many pgsql operations
  // to keep everything in memory. In such cases pgsql_op_arena_ is created and pgsql operations
  // are allocated on that arena. When all pgsql operations are out of active state the arena is
  // reset and operations are recreated to start the next batch from the scratch.
  std::shared_ptr<ThreadSafeArena> pgsql_op_arena_;

  // Support for hash permutations. Allows IN conditions on hash columns.
  // For example, if the query clause is "h1 = 1 AND h2 IN (2,3) AND h3 IN (4,5,6) AND h4 = 7",
  // it is transformed into the following set of values for ROW(h1, h2, h3, h4):
  //   (1, 2, 4, 7)
  //   (1, 3, 4, 7)
  //   (1, 2, 5, 7)
  //   (1, 3, 5, 7)
  //   (1, 2, 6, 7)
  //   (1, 3, 6, 7)
  // Each element is a complete set of hash column values, so DocDB can calculate the hash code and
  // locate keys by the prefix.
  //
  // hash_permutations_ contains the permutation state, which allows to generate desired number of
  // permutations at a time and work in batches.
  // is_hash_batched_ indicates if multiple permutations can be batched into one request as part of
  // the condition_expr condition instead of making one request per permutation with updated
  // partition_column_values. PgGate makes one batch per tablet.
  std::optional<InPermutationGenerator> hash_permutations_;
  std::optional<bool> is_hash_batched_;
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

  Status DoPopulateMergeStreams(MergeSortKeysPtr merge_sort_keys) override {
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


bool ApplyBounds(
    LWPgsqlReadRequestPB& req,
    Slice lower_bound, bool lower_bound_is_inclusive,
    Slice upper_bound, bool upper_bound_is_inclusive);
void ApplyLowerBound(LWPgsqlReadRequestPB& req, Slice lower_bound, bool is_inclusive);
void ApplyUpperBound(LWPgsqlReadRequestPB& req, Slice upper_bound, bool is_inclusive);
dockv::KeyBytes HashCodeToDocKeyBound(
    const Schema& schema, uint16_t hash, bool is_inclusive, bool is_lower);

}  // namespace yb::pggate
