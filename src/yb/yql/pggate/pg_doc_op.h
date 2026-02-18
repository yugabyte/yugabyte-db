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
#include <list>
#include <memory>
#include <queue>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "yb/common/hybrid_time.h"

#include "yb/dockv/doc_key.h"

#include "yb/gutil/macros.h"

#include "yb/util/concepts.h"
#include "yb/util/locks.h"
#include "yb/util/lw_function.h"
#include "yb/util/ref_cnt_buffer.h"

#include "yb/yql/pggate/pg_doc_metrics.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_sys_table_prefetcher.h"
#include "yb/yql/pggate/util/pg_tuple.h"

namespace yb::pggate {

YB_STRONGLY_TYPED_BOOL(RequestSent);
YB_STRONGLY_TYPED_BOOL(KeepOrder);

template <class T>
concept PgDocResultSysEntryProcessor = InvocableAs<T, Status(Slice&)>;

using FetchedTargets = std::vector<PgFetchedTarget*>;
using FetchedTargetsPtr = std::shared_ptr<FetchedTargets>;
using MergeSortKeys = std::vector<YbcSortKey>;
using MergeSortKeysPtr = std::shared_ptr<MergeSortKeys>;
using YbctidProcessor = std::function<void(Slice, const RefCntBuffer&)>;

//--------------------------------------------------------------------------------------------------
// DocResult represents data in ONE reply from tablet servers.
class DocResult {
 public:
  explicit DocResult(rpc::SidecarHolder data);
  DocResult(rpc::SidecarHolder data, const LWPgsqlResponsePB& response);
  virtual ~DocResult() = default;

  // End of this batch.
  virtual bool is_eof() const {
    return row_count_ == 0 || row_iterator_.empty();
  }

  // Get the postgres tuple from this batch.
  virtual Status WritePgTuple(const std::vector<PgFetchedTarget*>& targets, PgTuple* pg_tuple);
  Status NextPgTuple();

  int CompareTo(DocResult& other);

  virtual Status ProcessYbctids(const YbctidProcessor& processor) {
    return ProcessSysEntries([this, &processor](Slice& data) -> Status {
      processor(VERIFY_RESULT(ReadYbctid(data)), data_.first);
      return Status::OK();
    });
  }

  // Processes rows containing data other than PG rows. (currently ybctids or sampling reservoir)
  template <PgDocResultSysEntryProcessor Processor>
  Status ProcessSysEntries(const Processor& processor) {
    for ([[maybe_unused]] auto _ : std::views::iota(0, row_count_)) {
      RETURN_NOT_OK(processor(row_iterator_));
    }
    SCHECK(row_iterator_.empty(), IllegalState, "Unread row data");
    return Status::OK();
  }

  // Row count in this batch.
  int64_t row_count() const {
    return row_count_;
  }

 protected:
  static Result<Slice> ReadYbctid(Slice& data);
  // Data selected from DocDB.
  rpc::SidecarHolder data_;

  // Iterator on "data_" from row to row.
  Slice row_iterator_;
  int64_t row_count_ = 0;
  size_t current_row_idx_;

 private:
  std::vector<int64_t> row_orders_;
  DISALLOW_COPY_AND_ASSIGN(DocResult);
};

// DocResult subclass to handle specifically Postgres data.
// It allocates built-in PgTuple instance to parse current row data, store the values and access
// them. Currently the values are accessed for the merge sort purposes.
// The class overrides the WritePgTuple(const std::vector<PgFetchedTarget*>&, PgTuple*) method of
// the DocResult class to copy data from the built-in PgTuple if it already has been read.
class PgDocResult : public DocResult {
 public:
  PgDocResult(rpc::SidecarHolder data, FetchedTargetsPtr targets,
              MergeSortKeysPtr merge_sort_keys, size_t nattrs);
  virtual ~PgDocResult() = default;

  int CompareTo(PgDocResult& other);

  // Get the postgres tuple from this batch.
  virtual Status WritePgTuple(
      const std::vector<PgFetchedTarget*>& targets, PgTuple* pg_tuple) override;
  Status WritePgTuple(PgTuple* pg_tuple);
  Status NextPgTuple();
  virtual bool is_eof() const override {
    return row_count_ == 0 || (!pg_tuple_is_valid_ && row_iterator_.empty());
  }
  virtual Status ProcessYbctids(const YbctidProcessor& processor) override;
 private:
  struct PgMemoryContextWrapper {
    explicit PgMemoryContextWrapper(void* ptr_) : ptr(ptr_) {}
    ~PgMemoryContextWrapper();
    void* ptr;
  };
  PgTuple MakePgTuple();
  PgMemoryContextWrapper mem_context_;
  FetchedTargetsPtr targets_;
  MergeSortKeysPtr merge_sort_keys_;
  size_t nattrs_;
  uint64_t* datums_;
  bool* isnulls_;
  YbcPgSysColumns syscols_;
  PgTuple pg_tuple_;
  bool pg_tuple_is_valid_ = false;

  DISALLOW_COPY_AND_ASSIGN(PgDocResult);
};

// DocResultStream's fetch status.
// kHasLocalData - there are buffered data in the queue.
// kNeedsFetch - no buffered data in the queue, but the operation is active.
// kDone - no buffered data in the queue, and operation is not defined or inactive.
YB_DEFINE_ENUM(StreamFetchStatus, (kHasLocalData)(kNeedsFetch)(kDone));

// Stream of data from a DocDB source, usually a tablet.
// Since DocDB sources are ordered, designed to maintain the order.
// In fact, implements a wrapper for a queue of DocResult instances.
template <class R>
class DocResultStream {
 public:
  // DocResultStream provides access to either fetchable or static data.
  // Fetchable data require PgsqlOpPtr to check if it is active, so there's something to fetch.
  // Static data is a predefined list of DocResult.
  explicit DocResultStream(PgsqlOpPtr op) : op_(op) {}
  explicit DocResultStream(std::list<R>&& results) : results_queue_(std::move(results)) {}

  bool operator==(const PgsqlOpPtr& op) const { return op_ == op; }

  StreamFetchStatus FetchStatus() const;

  void Detach();

  // Do not discard empty DocResults. The held DocResults are discarded when HoldResults() is
  // called again or when the DocResultStream instance is destroyed.
  void HoldResults() { old_results_holder_.emplace(); }

  // Returns nullptr if nothing is available. May invalidate previously returned data.
  R* GetNextDocResult();

 private:
  // Append another batch of the fetched data to the queue
  template <typename... Args>
  uint64_t EmplaceDocResult(Args&&... args);

  friend class PgDocResultStream;

  PgsqlOpPtr op_;
  std::list<R> results_queue_;
  std::optional<std::list<R>> old_results_holder_;

  DISALLOW_COPY_AND_ASSIGN(DocResultStream);
};

using PgDocFetchCallback = std::function<Status()>;
// Base class to control fetch from multiple streams of data from the DocDB sources, and their read
// order.
// Fetch is controlled by calling the fetch_func when and where appropriate.
// The fetch_func is expected to return fetched data to the PgDocResultStream by calling
// the EmplaceOpDocResult method.
// The class provides single data stream to the caller.
// Supports work in batches, caller can reset the PgsqlOps to set up the new batch.
class PgDocResultStream {
 public:
  explicit PgDocResultStream(PgDocFetchCallback fetch_func) : fetch_func_(fetch_func) {}

  virtual ~PgDocResultStream() = default;

  // Parsers for the column values
  void SetFetchedTargets(FetchedTargetsPtr targets) { targets_ = targets; }
  // Merge sort support data for each key (comparator, etc.)
  void SetMergeSortKeys(MergeSortKeysPtr merge_sort_keys) { merge_sort_keys_ = merge_sort_keys; }

  // Reset the stream, but keep the accumulated responses to serve data from.
  virtual void ResetOps() = 0;

  // Reset the stream and set up new batch of PgsqlOps to read results from.
  virtual void ResetOps(const std::vector<PgsqlOpPtr> &ops) = 0;

  // Accessors, each of these returns false once the batch is out of results (EOF).
  // Executing any of these may invalidate previously returned data, so caller should copy what
  // is needed.

  // Read next one row into the tuple. It is caller responsibility to provide matching targets
  Result<bool> GetNextRow(PgTuple* pg_tuple);

  template <PgDocResultSysEntryProcessor Processor>
  Result<bool> ProcessNextSysEntries(const Processor& processor) {
    auto* result = VERIFY_RESULT(NextDocResult());
    if (result) {
      RETURN_NOT_OK(result->ProcessSysEntries(processor));
      return true;
    }
    return false;
  }

  virtual Result<bool> ProcessNextYbctids(const YbctidProcessor& processor) {
    auto* result = VERIFY_RESULT(NextDocResult());
    if (result) {
      RETURN_NOT_OK(result->ProcessYbctids(processor));
      return true;
    }
    return false;
  }

  // Returns next DocResult to read from or nullptr if EOF.
  virtual Result<DocResult*> NextDocResult() = 0;

  // To be used by the fetcher function.
  // Find DocResultStream for the op and put the fetched data into the queue.
  virtual Result<uint64_t> EmplaceOpDocResult(
      const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) = 0;

 protected:
  template<class R, typename... Args>
  uint64_t EmplaceDocResult(DocResultStream<R>& stream, Args&&... args);
  PgDocFetchCallback fetch_func_;
  FetchedTargetsPtr targets_;
  MergeSortKeysPtr merge_sort_keys_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PgDocResultStream);
};

// PgDocResultStream with lazy fetch.
// It select for reading the first operation that has locally buffered data.
// Only if there's no operation with data readily available, the fetch is performed.
class ParallelPgDocResultStream : public PgDocResultStream {
 public:
  ParallelPgDocResultStream(PgDocFetchCallback fetch_func, const std::vector<PgsqlOpPtr> &ops);

  virtual ~ParallelPgDocResultStream() = default;

  virtual void ResetOps() override;

  virtual void ResetOps(const std::vector<PgsqlOpPtr> &ops) override;

  virtual Result<uint64_t> EmplaceOpDocResult(
      const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) override;

  virtual Result<DocResult*> NextDocResult() override;

 private:
  Result<DocResultStream<DocResult>&> FindReadStream(const PgsqlOpPtr& op);

  std::list<DocResultStream<DocResult>> read_streams_;
};

// CachedPgDocResultStream provides access to static (cached) data.
class CachedPgDocResultStream : public PgDocResultStream {
 public:
  explicit CachedPgDocResultStream(std::list<DocResult>&& results);

  virtual ~CachedPgDocResultStream() = default;

  virtual void ResetOps() override {}

  virtual void ResetOps(const std::vector<PgsqlOpPtr> &ops) override;

  virtual Result<uint64_t> EmplaceOpDocResult(
      const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) override;

  virtual Result<DocResult*> NextDocResult() override;

 private:
  DocResultStream<DocResult> read_stream_;
};

// Implementation of PgDocResultStream which merge sorts rows in its streams.
// Each stream is expected to be pre-sorted in the same order.
// The MergingPgDocResultStream takes a function, which retrieves order from the order of the first
// row in the DocResultStream as a value of type T. The MergingPgDocResultStream puts
// DocResultStreams into priority queue and uses values returned by the row order function as the
// priorities. It is assumed that order values are retrieved from locally buffered data, so only
// DocResultStreams with locally buffered data are put to the queue. The merge sort algorithm
// require all the streams to provide the order of their first element. Therefore
// MergingPgDocResultStream keeps fetching until all participant have some locally buffered data.
// Like other PgDocResultStream subclasses, the MergingPgDocResultStream can be reset. Obviously,
// MergingPgDocResultStream guarantees order only within the batch, so ResetOps should be used with
// care.
template <class R>
class MergingPgDocResultStream : public PgDocResultStream {
 public:
  MergingPgDocResultStream(
      PgDocFetchCallback fetch_func, const std::vector<PgsqlOpPtr>& ops, size_t nattrs);

  virtual ~MergingPgDocResultStream() = default;

  virtual void ResetOps() override;

  virtual void ResetOps(const std::vector<PgsqlOpPtr> &ops) override;

  virtual Result<uint64_t> EmplaceOpDocResult(
      const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) override;

  virtual Result<DocResult*> NextDocResult() override;

  virtual Result<bool> ProcessNextYbctids(const YbctidProcessor& processor) override {
    VLOG(4) << "MergingPgDocResultStream::ProcessNextYbctids start";
    size_t found = 0;
    for (auto& stream : read_streams_) {
      stream.HoldResults();
    }
    while (true) {
      auto* result = VERIFY_RESULT(NextDocResult());
      if (!result) {
        VLOG(4) << "MergingPgDocResultStream::ProcessNextYbctids is all done after "
                << found << " ybctids processed";
        return found > 0;
      }
      ++found;
      RETURN_NOT_OK(result->ProcessYbctids(processor));
      if (current_stream_->FetchStatus() == StreamFetchStatus::kNeedsFetch) {
        VLOG(4) << "MergingPgDocResultStream::ProcessNextYbctids would fetch after "
                << found << " ybctids processed";
        return true;
      }
    }
  }

 private:
  Result<DocResultStream<R>&> FindReadStream(
      const PgsqlOpPtr& op, const LWPgsqlResponsePB& response);

  // The list of streams participating in merge sort.
  std::list<DocResultStream<R>> read_streams_;
  size_t nattrs_;

  struct StreamComparator {
    bool operator()(DocResultStream<R>* a, DocResultStream<R>* b) const {
      return a->GetNextDocResult()->CompareTo(*b->GetNextDocResult()) > 0;
    }
  };
  using MergeSortPQ = std::priority_queue<DocResultStream<R>*,
                                          std::vector<DocResultStream<R>*>,
                                          StreamComparator>;
  // Pointers to read_streams_ elements ordered.
  MergeSortPQ read_queue_;
  // The stream from where last NextDocResult has returned the DocResult.
  // The pointer is not in the read_queue_.
  // The caller is expected to read one and only one row from the stream, otherwise
  // the MergingPgDocResultStream may not work correctly.
  // Next time when NextDocResult will be called, current_stream_ will be added to the read_queue_
  // with new priority, unless exhausted.
  DocResultStream<R>* current_stream_ = nullptr;
  // The MergingPgDocResultStream starts and ends the batch with empty read queue.
  // We need this flag to distinguish not yet initialized batch from exhausted.
  bool started_ = false;
};

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
//     pgsql_ops_ is soreted to place active ops first, and all inactive ops are place at the end.
//     For example,
//        PgSession::RunAsync(pgsql_ops_.data(), active_op_count)
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

class PgDocOp : public std::enable_shared_from_this<PgDocOp> {
 public:
  using SharedPtr = std::shared_ptr<PgDocOp>;

  using Sender = std::function<Result<PgDocResponse>(
      PgSession*, const PgsqlOpPtr*, size_t, const PgTableDesc&, HybridTime,
      ForceNonBufferable, IsForWritePgDoc)>;

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
    DCHECK_EQ(active_op_count_, 0);
    end_of_data_ = true;
  }

  Result<int32_t> GetRowsAffectedCount() const;

  // Get the results and hand them over to the result stream.
  // Send requests for new pages if necessary.
  virtual Status FetchMoreResults();
  PgDocResultStream& ResultStream();
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
  Result<bool> PopulateMergeStreamRequests(
      MergeSortKeysPtr merge_sort_keys, PgTable& bind, InPermutationGenerator&& merge_streams);

  bool has_out_param_backfill_spec() {
    return !out_param_backfill_spec_.empty();
  }

  const char* out_param_backfill_spec() {
    return out_param_backfill_spec_.c_str();
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

  virtual Status DoPopulateMergeStreamRequests(
      MergeSortKeysPtr merge_sort_keys, PgTable& bind, InPermutationGenerator&& merge_streams) = 0;

  // Sorts the operators in "pgsql_ops_" to move "inactive" operators to the end of the list.
  void MoveInactiveOpsOutside();

  // If there is a result stream, reset it.
  void ResetResultStream();

  // If there is a result stream, add current operations to it.
  void AddOpsToResultStream();

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

  std::unique_ptr<PgDocResultStream> result_stream_;
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

  Status CompleteRequests();

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

  Status DoPopulateMergeStreamRequests(
      MergeSortKeysPtr merge_sort_keys, PgTable& bind,
      InPermutationGenerator&& merge_streams) override;

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

  const PgsqlReadOp& GetTemplateReadOp() { return *read_op_; }

 private:
  // Check request conditions if they allow to limit the scan range
  // Returns true if resulting range is not empty, false otherwise
  Result<bool> SetScanBounds(PgTable& table, LWPgsqlReadRequestPB& request);

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
  Result<bool> BindExprsRegular(
    PgTable& table, LWPgsqlReadRequestPB& read_req, const std::vector<const LWQLValuePB*>& values);

  // Binds the given values to the partition defined by hash column values.
  // The partition_batches vector for each partition stores the flag indicating the partition
  // has been initialized, and the references to initialized RHSs of the batched IN expression,
  // or null, if the partition batch is inactive.
  // Batch may be inactive if it has not been yet initialized, or its request range is empty.
  Result<bool> BindExprsToBatch(
      std::vector<std::pair<bool, LWPgsqlExpressionPB*>>& partition_batches,
      const std::vector<const LWQLValuePB*>& values);

  Result<LWPgsqlExpressionPB*> InitHashPermutationBatch(LWPgsqlReadRequestPB& read_req);

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

  Status DoPopulateMergeStreamRequests(
      MergeSortKeysPtr merge_sort_keys, PgTable& bind,
      InPermutationGenerator&& merge_streams) override {
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


bool ApplyBounds(LWPgsqlReadRequestPB& req,
                 const Slice lower_bound,
                 bool lower_bound_is_inclusive,
                 const Slice upper_bound,
                 bool upper_bound_is_inclusive);
void ApplyLowerBound(LWPgsqlReadRequestPB& req, const Slice lower_bound, bool is_inclusive);
void ApplyUpperBound(LWPgsqlReadRequestPB& req, const Slice upper_bound, bool is_inclusive);
dockv::DocKey HashCodeToDocKeyBound(
    const Schema& schema, uint16_t hash, bool is_inclusive, bool is_lower);

}  // namespace yb::pggate
