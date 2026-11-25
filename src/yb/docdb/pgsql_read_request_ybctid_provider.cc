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
//

#include "yb/docdb/pgsql_read_request_ybctid_provider.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <span>

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"

DECLARE_bool(legacy_ybctid_batch_pagination);

namespace yb::docdb {

// Iterates over the batch elements.
// Implements two modes of operations: first pass, iterates over full batch, with pagination
// support by resuming iteration from the given position; subsequent passes, retry only the
// keys failed on the previous passes. List of indexes to retry is provided, iteration is always
// from the first index in the list, to paginate caller has to update the list of indexes.
class BatchArgIterator {
 public:
  BatchArgIterator(const PgsqlBatchArgumentMsgs& batch, size_t start_position)
      : batch_it_(batch.begin()), batch_size_(batch.size()), indexes_(std::nullopt) {
    MoveTo(start_position);
  }
  BatchArgIterator(const PgsqlBatchArgumentMsgs& batch, std::span<const uint64_t> indexes)
      : batch_it_(batch.begin()), batch_size_(batch.size()), indexes_(indexes) {
    MoveTo(indexes_->front());
  }

  size_t CurrentPosition() const {
    DCHECK(!Done());
    return batch_position_;
  }

  const PgsqlBatchArgumentMsg& CurrentElement() const {
    DCHECK(!Done());
    return *batch_it_;
  }

  bool Advance() {
    DCHECK(!Done());
    if (!indexes_) {
      MoveTo(batch_position_ + 1);
      return !Done();
    }
    *indexes_ = indexes_->subspan(1);
    if (indexes_->empty()) {
      return false;
    }
    MoveTo(indexes_->front());
    return true;
  }

  bool Done() const {
    return indexes_ ? indexes_->empty() : batch_position_ == batch_size_;
  }

  bool Retrying() const {
    return indexes_.has_value();
  }

  std::span<const uint64_t> RemainingRetries() const {
    return indexes_ ? *indexes_ : std::span<const uint64_t>();
  }

 private:
  void MoveTo(size_t position) {
    if (position == batch_position_) {
      return;
    }
    DCHECK(batch_position_ < position && position <= batch_size_)
        << "Invalid batch position: " << position
        << ", previous position: " << batch_position_
        << ", batch size: " << batch_size_;
    std::advance(batch_it_, position - batch_position_);
    batch_position_ = position;
  }

  PgsqlBatchArgumentMsgs::const_iterator batch_it_;
  size_t batch_position_ = 0;
  size_t batch_size_;
  // Remaining indexes, starting from the current one.
  std::optional<std::span<const uint64_t>> indexes_;
};

PgsqlReadRequestYbctidProvider::PgsqlReadRequestYbctidProvider(
    const DocReadContext& doc_read_context, const PgsqlReadRequestMsg& request,
    PgsqlResponseMsg& response)
    : request_(request), response_(response) {
  const auto& batch_args = request_.batch_arguments();
  VLOG_WITH_FUNC(3) << "Batch size: " << batch_args.size();

  // There are two different cases when the paging state has missing keys:
  // 1. The first pass over the batch arguments. In this case the position to restore
  // (batch_keys_processed) is strictly greater than all the missing keys, or the last missing key,
  // since they are sorted. In this case we restore the missing keys and continue iterating
  // the batch normally, and continue populating the missing keys. The first pass can't span over
  // multiple tablets, we continue to paginate to the first tablet in the range until the end of
  // the batch is reached. Even if the first tablet splits, pagination will go to the new first
  // tablet.
  // 2. The subsequent passes retry the missing keys. They are only possible if there are multiple
  // tablets in the range and pagination has happened to a subsequent batch. In this case
  // the position to restore may be absent (first page of the tablet), so we retry all the missing
  // keys. Keys may be missing on that tablet too, so we will collect them again, if there more
  // tablets in the range. If the position to restore is specified, that means the pagination has
  // happened in the middle of the tablet. The position to restore has to be one of the missing
  // keys, the missing keys lower, if any, are accumulated missing keys from the current pass, and
  // remaining missing keys are to be retried, so the missing keys are split accordingly.
  // Without missing keys we just restore the position, if specified.
  if (request_.has_paging_state() && request_.paging_state().batch_keys_missing_size() > 0) {
    const auto& missing_keys = request_.paging_state().batch_keys_missing();
    if (request_.paging_state().has_batch_keys_processed()) {
      auto position = request_.paging_state().batch_keys_processed();
      if (position > missing_keys.back()) {
        batch_arg_it_ = std::make_unique<BatchArgIterator>(
            batch_args, request_.paging_state().batch_keys_processed());
        retry_keys_.assign(missing_keys.begin(), missing_keys.end());
      } else {
        auto it = std::find(missing_keys.begin(), missing_keys.end(),
                            request_.paging_state().batch_keys_processed());
        DCHECK(it != missing_keys.end()) << "Paging state is inconsistent: "
                                        << request_.paging_state().ShortDebugString();
        batch_arg_it_ = std::make_unique<BatchArgIterator>(
            batch_args, std::span<const uint64_t>(it, missing_keys.end()));
        retry_keys_.assign(missing_keys.begin(), it);
      }
    } else {
      batch_arg_it_ = std::make_unique<BatchArgIterator>(
          batch_args, std::span<const uint64_t>(missing_keys.begin(), missing_keys.end()));
    }
  } else {
    batch_arg_it_ = std::make_unique<BatchArgIterator>(
        batch_args, request_.paging_state().batch_keys_processed());
  }

  // Make a copy of the batch iterator and do one pass over to determine the bounds of the batch.
  if (!batch_arg_it_->Done()) {
    auto it = *batch_arg_it_;
    auto min_arg = it.CurrentElement().ybctid().value().binary_value();
    auto max_arg = min_arg;
    while (it.Advance()) {
      auto val = it.CurrentElement().ybctid().value().binary_value();
      if (min_arg > val) {
        min_arg = val;
      } else if (max_arg < val) {
        max_arg = val;
      }
    }
    bounds_ = {min_arg, max_arg};
  }
}

PgsqlReadRequestYbctidProvider::~PgsqlReadRequestYbctidProvider() = default;

Result<Slice> PgsqlReadRequestYbctidProvider::FetchKey() {
  if (!valid_ || batch_arg_it_->Done()) {
    RETURN_NOT_OK(UpdateResponseForTheEndOfBatch());
    return Slice();
  }

  // Store element's data locally and advance the batch iterator to the next position.
  const auto& element = batch_arg_it_->CurrentElement();
  Slice ret = element.ybctid().value().binary_value();
  if (element.has_order()) {
    current_order_ = element.order();
  }
  current_position_ = batch_arg_it_->CurrentPosition();

  batch_arg_it_->Advance();
  return ret;
}

void PgsqlReadRequestYbctidProvider::AddedKeyToResultSet() {
  if (current_order_.has_value()) {
    response_.add_batch_orders(*current_order_);
  }
}

void PgsqlReadRequestYbctidProvider::KeyNotFound() {
  // We do not track unordered keys.
  // Because the keys in an unordered batch are sorted, and keys on tablets are in the same order,
  // if a key is not found, there are 3 possibilities.
  // 1. Such key does not exist.
  // 2. The key is on a tablet before the current one.
  // 3. The key is on a tablet after the current one.
  // We can ignore 1., it does not make sense to retry such key. If 2., we have fetched the key
  // when we scanned the previous tablet. If 3, all subsequent keys in the batch are on the tablets
  // after the current one, or do not exist. We going to reach the end of the batch without
  // pagination and current batch will be forwarded to the next tablet without trimming. Hence,
  // we will find these keys when processing next pages.
  if (current_order_.has_value()) {
    retry_keys_.push_back(*current_position_);
  }
}

Status PgsqlReadRequestYbctidProvider::UpdatePagingState() {
  DCHECK(valid_) << "Exhausted provider shouldn't attempt to paginate";
  // No more keys from this provider.
  valid_ = false;

  DCHECK(current_position_);

  // Current position in the batch refers the element last returned by the FetchKey.
  // It is considered "found", but it could not make it into the result, so the next page should
  // start from it.
  // It can't be the first element in the batch, otherwise there's a risk of batch never making
  // progress. It can be the last, caller does not know when the batch ends, but not higher:
  // when batch is exhausted, the provider is finalized and invalid.
  const auto current = *current_position_;
  RSTATUS_DCHECK(current > 0 && current < request_.batch_arguments_size(),
                 IllegalState, "Invalid batch position: $0", current);

  if (batch_arg_it_->Retrying()) {
    // Whatever is currently in the retry list is for the next tablet, now append to the same list
    // items to retry on this tablet, starting from the current.
    retry_keys_.push_back(current);
    // Dump remaining retries to the retry_keys_ list.
    auto remaining_keys = batch_arg_it_->RemainingRetries();
    retry_keys_.insert(retry_keys_.end(), remaining_keys.begin(), remaining_keys.end());
  }

  // Make sure the next page request lands on the same tablet, or on the first child tablet, if
  // this tablet will split afterwards.
  auto* mutable_paging_state = response_.mutable_paging_state();
  mutable_paging_state->dup_next_partition_key(request_.partition_key());

  // Without missing keys it is safe to pop keys from the front of the batch.
  if (retry_keys_.empty()) {
    response_.set_batch_arg_count(*current_position_);
    return Status::OK();
  }

  // At this point paging state is going to take critical information, that may go to an older
  // node which may not be able to read it. Error out to be safe.
  if (FLAGS_legacy_ybctid_batch_pagination) {
    return STATUS(IllegalState,
        "Some keys were not found, it is unsafe to paginate. "
        "Consider disabling legacy_ybctid_batch_pagination");
  }

  auto first_not_found_key = retry_keys_.front();
  // Pop found keys from the front of the batch. Need to adjust the indexes after that.
  response_.set_batch_arg_count(first_not_found_key);
  // Save retry keys into the paging state, shift the indexes by the number of keys popped from
  // the front of the batch. Shift current position as well.
  mutable_paging_state->set_batch_keys_processed(current - first_not_found_key);
  for (auto key_index : retry_keys_) {
    mutable_paging_state->add_batch_keys_missing(key_index - first_not_found_key);
  }

  VLOG_WITH_FUNC(3) << "batch paging state: " << mutable_paging_state->ShortDebugString();
  return Status::OK();
}

Status PgsqlReadRequestYbctidProvider::UpdateResponseForTheEndOfBatch() {
  DCHECK(valid_) << "This key provider is already finalized";
  // No more keys from this provider.
  valid_ = false;

  response_.set_batch_arg_count(request_.batch_arguments_size());
  // Batch is successfully completed.
  if (retry_keys_.empty()) {
    return Status::OK();
  }

  // In the case if upper level finds more tablets in the scan range (tablet has split), the
  // request will be forwarded to the next tablet, and the missing keys will be used to retry
  // only the missing keys. In legacy mode the destination node may be running an older version
  // and may be unable to read the retry keys. Such node will retry entire batch. That impacts
  // performance, but not correctness, as subsequent tablet won't have the keys found on this
  // tablet. Hence we don't error out, or even check.
  auto* mutable_paging_state = response_.mutable_paging_state();
  for (auto key_index : retry_keys_) {
    mutable_paging_state->add_batch_keys_missing(key_index);
  }

  VLOG_WITH_FUNC(3) << "batch paging state: " << mutable_paging_state->ShortDebugString();
  return Status::OK();
}

}  // namespace yb::docdb
