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

#include "yb/yql/pggate/pg_doc_op_fetch_stream.h"

#include <algorithm>

#include "yb/common/pgsql_protocol.messages.h"

#include "yb/util/format.h"
#include "yb/util/logging.h"

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/util/ybc-internal.h"

namespace yb::pggate {

DocResult::DocResult(rpc::SidecarHolder data)
    : data_(std::move(data)), current_row_idx_(0) {
  PgDocData::LoadCache(data_.second, &row_count_, &row_iterator_);
}

DocResult::DocResult(rpc::SidecarHolder data, const LWPgsqlResponsePB& response)
    : DocResult(std::move(data)) {
  if (row_count_ > 0 && !response.batch_orders().empty()) {
    const auto& orders = response.batch_orders();
    row_orders_.assign(orders.begin(), orders.end());
    DCHECK(row_orders_.size() == static_cast<size_t>(row_count_))
        << "Number of the row orders does not match the number of rows";
  }
}

Status DocResult::WritePgTuple(const FetchedTargets& targets, PgTuple *pg_tuple) {
  for (auto* target : targets) {
    if (PgDocData::ReadHeaderIsNull(&row_iterator_)) {
      target->SetNull(pg_tuple);
    } else {
      target->SetValue(&row_iterator_, pg_tuple);
    }
  }
  ++current_row_idx_;
  return Status::OK();
}

Status DocResult::NextPgTuple() {
  DCHECK(current_row_idx_ < static_cast<size_t>(row_count_));
  return Status::OK();
}

int DocResult::CompareTo(DocResult& other) {
  DCHECK(current_row_idx_ < static_cast<size_t>(row_count_));
  DCHECK(!row_orders_.empty());
  DCHECK(other.current_row_idx_ < static_cast<size_t>(other.row_count_));
  DCHECK(!other.row_orders_.empty());
  auto lhs = row_orders_[current_row_idx_];
  auto rhs = other.row_orders_[other.current_row_idx_];
  if (lhs < rhs) return -1;
  if (lhs > rhs) return 1;
  return 0;
}

Result<Slice> DocResult::ReadYbctid(Slice& data) {
  SCHECK(!PgDocData::ReadHeaderIsNull(&data), InternalError, "System column ybctid cannot be NULL");
  const auto data_size = PgDocData::ReadNumber<int64_t>(&data);
  const auto* data_ptr = data.data();
  Slice ybctid{data_ptr, data_ptr + data_size};
  data.remove_prefix(data_size);
  return ybctid;
}

PgDocResult::PgDocResult(rpc::SidecarHolder data, FetchedTargetsPtr targets,
                         MergeSortKeysPtr merge_sort_keys, size_t nattrs)
    : DocResult(data),
      mem_context_(YBCCreateMemoryContext(nullptr, "PgDocResultData")),
      targets_(targets),
      merge_sort_keys_(merge_sort_keys),
      nattrs_(nattrs),
      pg_tuple_(MakePgTuple()) {}

PgTuple PgDocResult::MakePgTuple() {
  VLOG_WITH_FUNC(4) << "targets: " << targets_->size() << " nattrs: " << nattrs_
                    << " sort keys: " << merge_sort_keys_->size();
  auto save = YBCSwitchMemoryContext(mem_context_.ptr);
  datums_ = static_cast<uint64_t*>(YBCPAlloc(nattrs_ * sizeof(uint64_t)));
  isnulls_ = static_cast<bool*>(YBCPAlloc(nattrs_ * sizeof(bool)));
  memset(isnulls_, true, nattrs_ * sizeof(bool));
  memset(&syscols_, 0, sizeof(YbcPgSysColumns));
  YBCSwitchMemoryContext(save);
  return PgTuple(datums_, isnulls_, &syscols_, nattrs_);
}

PgDocResult::PgMemoryContextWrapper::~PgMemoryContextWrapper() {
  if (ptr) {
    YBCDeleteMemoryContext(ptr);
  }
}

int PgDocResult::CompareTo(PgDocResult& other) {
  DCHECK(merge_sort_keys_);
  DCHECK(other.merge_sort_keys_->size() == merge_sort_keys_->size());
  DCHECK(pg_tuple_is_valid_);
  DCHECK(other.pg_tuple_is_valid_);
  for (const auto& sort_key : *merge_sort_keys_) {
    const auto idx = sort_key.value_idx;
    auto datum1 = datums_[idx];
    auto isnull1 = isnulls_[idx];
    auto datum2 = other.datums_[idx];
    auto isnull2 = other.isnulls_[idx];
    auto cmp_result = sort_key.comparator(datum1, isnull1, datum2, isnull2, sort_key.sortstate);
    if (cmp_result != 0) return cmp_result;
  }
  return 0;
}

Status PgDocResult::WritePgTuple(const FetchedTargets& targets, PgTuple* pg_tuple) {
  RSTATUS_DCHECK_EQ(targets.size(), targets_->size(), InvalidArgument, "Unmatching fetch targets");
  return WritePgTuple(pg_tuple);
}

Status PgDocResult::WritePgTuple(PgTuple* pg_tuple) {
  if (pg_tuple_is_valid_) {
    pg_tuple->CopyFrom(pg_tuple_, nattrs_);
    pg_tuple_is_valid_ = false;
    return Status::OK();
  }
  auto save = YBCSwitchMemoryContext(mem_context_.ptr);
  auto status = DocResult::WritePgTuple(*targets_, pg_tuple);
  YBCSwitchMemoryContext(save);
  return status;
}

Status PgDocResult::NextPgTuple() {
  if (!pg_tuple_is_valid_) {
    DCHECK(current_row_idx_ < static_cast<size_t>(row_count_));
    auto save = YBCSwitchMemoryContext(mem_context_.ptr);
    auto status = DocResult::WritePgTuple(*targets_, &pg_tuple_);
    YBCSwitchMemoryContext(save);
    RETURN_NOT_OK(status);
    pg_tuple_is_valid_ = true;
  }
  return Status::OK();
}

Status PgDocResult::ProcessYbctids(const YbctidProcessor& processor) {
  RETURN_NOT_OK(NextPgTuple());
  processor(VERIFY_RESULT(ReadYbctid(row_iterator_)));
  pg_tuple_is_valid_ = false;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
template <class R>
DocResultStream<R>::~DocResultStream() {
  if (old_results_holder_) {
    for (auto& result : results_queue_) {
      old_results_holder_->push_back(result.DataBuffer());
    }
  }
}

template <class R>
StreamFetchStatus DocResultStream<R>::FetchStatus() const {
  for (const auto& result : results_queue_) {
    if (!result.is_eof()) {
      return StreamFetchStatus::kHasLocalData;
    }
  }
  return (op_ && op_->is_active()) ? StreamFetchStatus::kNeedsFetch : StreamFetchStatus::kDone;
}

template <class R>
void DocResultStream<R>::Detach() {
  if (op_) {
    DCHECK(!op_->is_active());
    op_ = nullptr;
  }
}

template <class R>
void DocResultStream<R>::HoldResults(BuffersPtr holder) {
  old_results_holder_ = holder ? holder : std::make_shared<Buffers>();
}

template <class R>
R* DocResultStream<R>::GetNextDocResult() {
  while (!results_queue_.empty() && results_queue_.front().is_eof()) {
    if (old_results_holder_) {
      old_results_holder_->push_back(results_queue_.front().DataBuffer());
    }
    results_queue_.pop_front();
  }
  return results_queue_.empty() ? nullptr : &results_queue_.front();
}

Result<bool> PgDocOpFetchStream::GetNextRow(PgTuple* pg_tuple) {
  auto* result = VERIFY_RESULT(NextDocResult());
  if (result) {
    RSTATUS_DCHECK(targets_, InternalError, "Fetch targets are not provided");
    RETURN_NOT_OK(result->WritePgTuple(*targets_, pg_tuple));
    return true;
  }
  return false;
}

// Append another batch of the fetched data to the queue
template<class R>
template<typename... Args>
uint64_t DocResultStream<R>::EmplaceDocResult(Args&&... args) {
  results_queue_.emplace_back(std::forward<Args>(args)...);
  uint64_t rows_affected = results_queue_.back().row_count();
  VLOG_WITH_FUNC(3) << "Operation " << (op_ ? op_->ToString() : "<empty>")
                    << " received new response with " << rows_affected
                    << " rows. Now has " << results_queue_.size() << " responses in the queue";
  // immediately discard empty response
  if (results_queue_.back().is_eof()) {
    results_queue_.pop_back();
    return 0;
  }
  return rows_affected;
}

template <class R, typename... Args>
uint64_t PgDocOpFetchStream::EmplaceDocResult(DocResultStream<R>& stream, Args&&... args) {
  return stream.EmplaceDocResult(std::forward<Args>(args)...);
}

BuffersPtr PgDocOpFetchStream::HoldResults(BuffersPtr holder) {
  auto previous = old_results_holder_;
  old_results_holder_ = holder ? holder : std::make_shared<Buffers>();
  DoHoldResults();
  return previous;
}

ParallelPgDocOpFetchStream::ParallelPgDocOpFetchStream(
    PgDocFetchCallback fetch_func, const std::vector<PgsqlOpPtr>& ops)
    : PgDocOpFetchStream(fetch_func) {
  ResetOps(ops);
}

void ParallelPgDocOpFetchStream::ResetOps() {
  for (auto& stream : read_streams_) {
    stream.Detach();
  }
}

void ParallelPgDocOpFetchStream::ResetOps(const std::vector<PgsqlOpPtr>& ops) {
  for (const auto& op : ops) {
    read_streams_.emplace_back(op);
  }
  if (old_results_holder_) {
    DoHoldResults();
  }
}

Result<uint64_t> ParallelPgDocOpFetchStream::EmplaceOpDocResult(
    const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) {
  return EmplaceDocResult(VERIFY_RESULT_REF(FindReadStream(op)), std::move(data));
}

Result<DocResult*> ParallelPgDocOpFetchStream::NextDocResult() {
  for (;;) {
    for (auto it = read_streams_.begin(); it != read_streams_.end();) {
      switch (it->FetchStatus()) {
        case StreamFetchStatus::kHasLocalData:
          return it->GetNextDocResult();
        case StreamFetchStatus::kNeedsFetch:
          ++it;
          break;
        case StreamFetchStatus::kDone:
          it = read_streams_.erase(it);  // erase returns the next valid iterator
          break;
        default:
          LOG(FATAL) << "Invalid stream fetch status: " << it->FetchStatus();
      }
    }

    if (read_streams_.empty()) {
      return nullptr;
    }

    RETURN_NOT_OK(fetch_func_());
  }

  return STATUS(RuntimeError, "Unreachable statement");
}

void ParallelPgDocOpFetchStream::DoHoldResults() {
  for (auto& read_stream : read_streams_) {
    read_stream.HoldResults(old_results_holder_);
  }
}

Result<DocResultStream<DocResult>&> ParallelPgDocOpFetchStream::FindReadStream(
    const PgsqlOpPtr& op) {
  auto it = std::find(read_streams_.begin(), read_streams_.end(), op);
  if (it == read_streams_.end()) {
    return STATUS(RuntimeError, Format("Operation $0 not found", op));
  }
  return *it;
}

CachedPgDocOpFetchStream::CachedPgDocOpFetchStream(std::list<DocResult>&& results)
    : PgDocOpFetchStream([]() {
          return STATUS(RuntimeError, "CachedPgDocOpFetchStream does not fetch");
      }),
      read_stream_(std::move(results)) {}

void CachedPgDocOpFetchStream::ResetOps(const std::vector<PgsqlOpPtr>& ops) {
  LOG(FATAL) << "Can't reset CachedPgDocOpFetchStream";
}

Result<uint64_t> CachedPgDocOpFetchStream::EmplaceOpDocResult(
    const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) {
  return STATUS(RuntimeError, Format("Operation $0 not found", op));
}

Result<DocResult*> CachedPgDocOpFetchStream::NextDocResult() {
  if (read_stream_.FetchStatus() != StreamFetchStatus::kDone) {
    return read_stream_.GetNextDocResult();
  }
  return nullptr;
}

template <class R>
MergingPgDocOpFetchStream<R>::MergingPgDocOpFetchStream(
    PgDocFetchCallback fetch_func, const std::vector<PgsqlOpPtr>& ops, size_t nattrs)
    : PgDocOpFetchStream(fetch_func),
      nattrs_(nattrs) {
  ResetOps(ops);
}

template <class R>
void MergingPgDocOpFetchStream<R>::ResetOps() {
  for (auto& stream : read_streams_) {
    stream.Detach();
  }
}

template <class R>
void MergingPgDocOpFetchStream<R>::ResetOps(const std::vector<PgsqlOpPtr>& ops) {
  current_stream_ = nullptr;
  if (!read_queue_.empty()) {
    MergeSortPQ pq;
    read_queue_.swap(pq);
  }
  for (auto& op : ops) {
    read_streams_.emplace_back(op);
  }
  if (old_results_holder_) {
    DoHoldResults();
  }
  started_ = false;
}

template <class R>
Result<uint64_t> MergingPgDocOpFetchStream<R>::EmplaceOpDocResult(
    const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) {
  return EmplaceDocResult(
      VERIFY_RESULT_REF(FindReadStream(op, response)), std::move(data), response);
}

template <>
Result<uint64_t> MergingPgDocOpFetchStream<PgDocResult>::EmplaceOpDocResult(
    const PgsqlOpPtr& op, rpc::SidecarHolder&& data, const LWPgsqlResponsePB& response) {
  VLOG_WITH_FUNC(4) << "targets: " << targets_->size() << " nattrs: " << nattrs_
                    << " merge sort keys: " << merge_sort_keys_->size();
  return EmplaceDocResult(
      VERIFY_RESULT_REF(FindReadStream(op, response)),
      std::move(data),
      targets_,
      merge_sort_keys_,
      nattrs_);
}

template <class R>
Result<DocResultStream<R>&> MergingPgDocOpFetchStream<R>::FindReadStream(
    const PgsqlOpPtr& op, const LWPgsqlResponsePB& response) {
  if (!op->is_merge_stream() && response.has_paging_state()) {
    // If request paginates, we don't know what it exactly means, has DocDB hit some limit and
    // stopped, but requested rows are still being fetched in order, or tablet has split and some
    // rows are to be fetched from other tablet. Here we assume the worst, that is, some requested
    // rows are missing and will come in the next response, ordered, but probably not in the order
    // continuing this response.
    // Since rows within single response are properly ordered, data from this response would be
    // valid participant of the merge sort. So we add it to the read list. And we separate them out
    // from this operation's stream, as we don't know if next page will be valid continuation.
    // Therefore this operation's stream continues to require fetch, and we won't start the merge
    // sort as of yet.
    // TODO: have DocDB to hint us. If DocDB is fetching specific rows with predefined order, it
    // is OK to enqueue the request at the operation's stream as long as DocDB has found all
    // requested rows so far in the list.
    // When we fetching from the tablet in some order that is not predefined, and is not matching
    // natural row order, (i.e. following vector index), if target tablet is split, the operation
    // should actively split, too. Original operation's range should be truncated to tablet
    // boundaries and new active operation should be created covering remaining range.
    // Large part of such split is beyond the scope of the result stream management structures.
    VLOG_WITH_FUNC(2) << "Adding split stream for operation " << op;
    read_streams_.emplace_back(nullptr);
    auto& new_stream = read_streams_.back();
    if (old_results_holder_) {
      new_stream.HoldResults(old_results_holder_);
    }
    return new_stream;
  }
  auto it = std::find(read_streams_.begin(), read_streams_.end(), op);
  if (it == read_streams_.end()) {
    return STATUS(RuntimeError, Format("Operation $0 not found", op));
  }
  return *it;
}

template <class R>
Result<DocResult*> MergingPgDocOpFetchStream<R>::NextDocResult() {
  if (!started_) {
    VLOG_WITH_FUNC(2) << "Initialize merge sort of " << read_streams_.size() << " streams";
    DCHECK(current_stream_ == nullptr);
    while (true) {
      size_t num_not_ready = 0;
      for (const auto& stream : read_streams_) {
        if (stream.FetchStatus() == StreamFetchStatus::kNeedsFetch) {
          ++num_not_ready;
        }
      }
      if (num_not_ready == 0) {
        VLOG_WITH_FUNC(3) << "All streams are ready";
        break;
      }
      VLOG_WITH_FUNC(3) << num_not_ready << " streams need data, continue fetching";
      RETURN_NOT_OK(fetch_func_());
    }
    for (auto it = read_streams_.begin(); it != read_streams_.end(); ++it) {
      if (it->FetchStatus() == StreamFetchStatus::kHasLocalData) {
        RETURN_NOT_OK(it->GetNextDocResult()->NextPgTuple());
        read_queue_.push(&*it);
      }
    }
    started_ = true;
    VLOG_WITH_FUNC(2) << "Start merging " << read_queue_.size() << " streams";
  } else if (current_stream_) {
    // If the last read stream has more data to read, return it to the read queue.
    while (current_stream_->FetchStatus() == StreamFetchStatus::kNeedsFetch) {
      VLOG_WITH_FUNC(2) << "Current stream needs data to be returned to the read queue";
      RETURN_NOT_OK(fetch_func_());
    }
    if (current_stream_->FetchStatus() == StreamFetchStatus::kHasLocalData) {
      RETURN_NOT_OK(current_stream_->GetNextDocResult()->NextPgTuple());
      read_queue_.push(current_stream_);
    }
    current_stream_ = nullptr;
  }

  if (read_queue_.empty()) {
    VLOG_WITH_FUNC(2) << "Merging is done";
    return nullptr;
  }

  // Take first element out of the queue. After reading it may have different priority when
  // entering the queue again
  current_stream_ = read_queue_.top();
  read_queue_.pop();
  return current_stream_->GetNextDocResult();
}

template <class R>
void MergingPgDocOpFetchStream<R>::DoHoldResults() {
  for (auto& read_stream : read_streams_) {
    read_stream.HoldResults(old_results_holder_);
  }
}

// Explicit template instantiations
template class DocResultStream<DocResult>;
template class DocResultStream<PgDocResult>;
template class MergingPgDocOpFetchStream<DocResult>;
template class MergingPgDocOpFetchStream<PgDocResult>;

}  // namespace yb::pggate
