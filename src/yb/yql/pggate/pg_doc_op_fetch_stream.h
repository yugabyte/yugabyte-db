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
#include <vector>

#include "yb/gutil/macros.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/concepts.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"

#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/util/pg_tuple.h"

namespace yb::pggate {

template <class T>
concept PgDocResultSysEntryProcessor = InvocableAs<T, Status(Slice&)>;

using FetchedTargets = boost::container::small_vector<PgFetchedTarget*, 8>;
using FetchedTargetsPtr = std::shared_ptr<FetchedTargets>;
using MergeSortKeys = boost::container::small_vector<YbcSortKey, 8>;
using MergeSortKeysPtr = std::shared_ptr<MergeSortKeys>;
using Buffers = boost::container::small_vector<RefCntBuffer, 8>;
using BuffersPtr = std::shared_ptr<Buffers>;
using YbctidProcessor = std::function<void(Slice)>;

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
  virtual Status WritePgTuple(const FetchedTargets& targets, PgTuple* pg_tuple);
  Status NextPgTuple();

  int CompareTo(DocResult& other);

  virtual Status ProcessYbctids(const YbctidProcessor& processor) {
    return ProcessSysEntries([&processor](Slice& data) -> Status {
      processor(VERIFY_RESULT(ReadYbctid(data)));
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

  const RefCntBuffer& DataBuffer() { return data_.first; }

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
  virtual Status WritePgTuple(const FetchedTargets& targets, PgTuple* pg_tuple) override;
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
  ~DocResultStream();

  bool operator==(const PgsqlOpPtr& op) const { return op_ == op; }

  StreamFetchStatus FetchStatus() const;

  void Detach();

  // Do not discard empty DocResults. The held DocResults are discarded when HoldResults() is
  // called again or when the DocResultStream instance is destroyed.
  void HoldResults(BuffersPtr holder = nullptr);

  // Returns nullptr if nothing is available. May invalidate previously returned data.
  R* GetNextDocResult();

 private:
  // Append another batch of the fetched data to the queue
  template <typename... Args>
  uint64_t EmplaceDocResult(Args&&... args);

  friend class PgDocOpFetchStream;

  PgsqlOpPtr op_;
  std::list<R> results_queue_;
  BuffersPtr old_results_holder_;

  DISALLOW_COPY_AND_ASSIGN(DocResultStream);
};

using PgDocFetchCallback = std::function<Status()>;
// Base class to control fetch from multiple streams of data from the DocDB sources, and their read
// order.
// Fetch is controlled by calling the fetch_func when and where appropriate.
// The fetch_func is expected to return fetched data to the PgDocOpFetchStream by calling
// the EmplaceOpDocResult method.
// The class provides single data stream to the caller.
// Supports work in batches, caller can reset the PgsqlOps to set up the new batch.
class PgDocOpFetchStream {
 public:
  explicit PgDocOpFetchStream(PgDocFetchCallback fetch_func) : fetch_func_(fetch_func) {}

  virtual ~PgDocOpFetchStream() = default;

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

  BuffersPtr HoldResults(BuffersPtr holder = nullptr);

 protected:
  template<class R, typename... Args>
  uint64_t EmplaceDocResult(DocResultStream<R>& stream, Args&&... args);
  virtual void DoHoldResults() = 0;
  PgDocFetchCallback fetch_func_;
  FetchedTargetsPtr targets_;
  MergeSortKeysPtr merge_sort_keys_;
  BuffersPtr old_results_holder_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PgDocOpFetchStream);
};

// PgDocOpFetchStream with lazy fetch.
// It select for reading the first operation that has locally buffered data.
// Only if there's no operation with data readily available, the fetch is performed.
class ParallelPgDocOpFetchStream : public PgDocOpFetchStream {
 public:
  ParallelPgDocOpFetchStream(PgDocFetchCallback fetch_func, const std::vector<PgsqlOpPtr> &ops);

  virtual ~ParallelPgDocOpFetchStream() = default;

  virtual void ResetOps() override;

  virtual void ResetOps(const std::vector<PgsqlOpPtr> &ops) override;

  virtual Result<uint64_t> EmplaceOpDocResult(
      const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) override;

  virtual Result<DocResult*> NextDocResult() override;

  virtual void DoHoldResults() override;

 private:
  Result<DocResultStream<DocResult>&> FindReadStream(const PgsqlOpPtr& op);

  std::list<DocResultStream<DocResult>> read_streams_;
};

// CachedPgDocOpFetchStream provides access to static (cached) data.
class CachedPgDocOpFetchStream : public PgDocOpFetchStream {
 public:
  explicit CachedPgDocOpFetchStream(std::list<DocResult>&& results);

  virtual ~CachedPgDocOpFetchStream() = default;

  virtual void ResetOps() override {}

  virtual void ResetOps(const std::vector<PgsqlOpPtr> &ops) override;

  virtual Result<uint64_t> EmplaceOpDocResult(
      const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) override;

  virtual Result<DocResult*> NextDocResult() override;

  virtual void DoHoldResults() override {}

 private:
  DocResultStream<DocResult> read_stream_;
};

// Implementation of PgDocOpFetchStream which merge sorts rows in its streams.
// Each stream is expected to be pre-sorted in the same order.
// The MergingPgDocOpFetchStream takes a function, which retrieves order from the order of the first
// row in the DocResultStream as a value of type T. The MergingPgDocOpFetchStream puts
// DocResultStreams into priority queue and uses values returned by the row order function as the
// priorities. It is assumed that order values are retrieved from locally buffered data, so only
// DocResultStreams with locally buffered data are put to the queue. The merge sort algorithm
// require all the streams to provide the order of their first element. Therefore
// MergingPgDocOpFetchStream keeps fetching until all participant have some locally buffered data.
// Like other PgDocOpFetchStream subclasses, the MergingPgDocOpFetchStream can be reset. Obviously,
// MergingPgDocOpFetchStream guarantees order only within the batch, so ResetOps should be used with
// care.
template <class R>
class MergingPgDocOpFetchStream : public PgDocOpFetchStream {
 public:
  MergingPgDocOpFetchStream(
      PgDocFetchCallback fetch_func, const std::vector<PgsqlOpPtr>& ops, size_t nattrs);

  virtual ~MergingPgDocOpFetchStream() = default;

  virtual void ResetOps() override;

  virtual void ResetOps(const std::vector<PgsqlOpPtr> &ops) override;

  virtual Result<uint64_t> EmplaceOpDocResult(
      const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) override;

  virtual Result<DocResult*> NextDocResult() override;

  virtual Result<bool> ProcessNextYbctids(const YbctidProcessor& processor) override {
    VLOG_WITH_FUNC(4) << "Start";
    size_t found = 0;
    DoHoldResults();
    while (true) {
      auto* result = VERIFY_RESULT(NextDocResult());
      if (!result) {
        VLOG_WITH_FUNC(4) << "All done after " << found << " ybctids processed";
        return found > 0;
      }
      ++found;
      RETURN_NOT_OK(result->ProcessYbctids(processor));
      if (current_stream_->FetchStatus() == StreamFetchStatus::kNeedsFetch) {
        VLOG_WITH_FUNC(4) << "Need to fetch after " << found << " ybctids processed";
        return true;
      }
    }
  }

  virtual void DoHoldResults() override;

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
  // the MergingPgDocOpFetchStream may not work correctly.
  // Next time when NextDocResult will be called, current_stream_ will be added to the read_queue_
  // with new priority, unless exhausted.
  DocResultStream<R>* current_stream_ = nullptr;
  // The MergingPgDocOpFetchStream starts and ends the batch with empty read queue.
  // We need this flag to distinguish not yet initialized batch from exhausted.
  bool started_ = false;
};

}  // namespace yb::pggate
