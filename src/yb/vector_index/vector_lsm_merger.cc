// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//
#include <functional>
#include <optional>
#include <thread>

#include "yb/storage/frontier.h"

#include "yb/util/flags.h"
#include "yb/util/priority_thread_pool.h"
#include "yb/util/sync_point.h"

#include "yb/vector_index/vector_lsm_merger.h"
#include "yb/vector_index/vector_lsm_registry.h"

DECLARE_uint64(vector_index_task_size);

namespace yb::vector_index {

using namespace std::literals;

extern MonoDelta TEST_sleep_on_merged_chunk_populated;

namespace {

void PopulateMergeTasks(
    auto& tasks, size_t num_vectors_per_task, size_t& num_remaining, auto& source_iterator) {
  DCHECK(source_iterator.Valid());
  for (auto tasks_it = tasks.begin(); tasks_it != tasks.end(); ++tasks_it) {
    size_t num_vectors_added_in_task = 0;
    while (num_vectors_added_in_task < num_vectors_per_task) {
      if ((num_remaining == 0) || !source_iterator.Next()) {
        return;
      }
      tasks_it->Add(source_iterator->first, std::move(source_iterator->second));
      ++num_vectors_added_in_task;
      --num_remaining;
    }
  }
}

} // namespace

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSM<Vector, DistanceResult>::MergingIterator::Impl {
 public:
  using LSM = VectorLSM<Vector, DistanceResult>;
  using VectorIndex = typename LSM::VectorIndex;
  using VectorIndexPtr = typename LSM::VectorIndexPtr;
  using InnerIterator = typename VectorIndex::Iterator;
  using Iterator = typename ImmutableChunkPtrs::const_iterator;
  using ValueType = typename VectorIndex::IteratorValue;

  Impl(LSM& lsm, const ImmutableChunkPtrs& chunks, VectorLSMMergeFilter& filter)
      : lsm_(lsm), chunks_(chunks), filter_(filter), outer_it_(chunks_.begin()) {
    // Seed frontiers from the first chunk so the first Merger::ResetFrontiers() call
    // returns them as part of the first output chunk's frontiers.
    ResetFrontiers();
  }

  // Returns true if Next() can be called.
  bool Valid() const {
    return outer_it_ != chunks_.end();
  }

  ValueType& operator*() {
    DCHECK(value_.has_value());
    return *value_;
  }

  ValueType* operator->() {
    DCHECK(value_.has_value());
    return &(*value_);
  }

  bool Next() {
    bool reset_inner_iterator = !inner_it_.Valid();
    while (Valid()) {
      if (InnerNext(reset_inner_iterator)) {
        return true;
      }

      ++outer_it_;
      if (!Valid()) {
        break;
      }
      UpdateFrontiers();
      reset_inner_iterator = true;
    }

    value_.reset();
    return false;
  }

  bool FrontiersUpdated() const {
    return frontiers_updated_;
  }

  // Returns current frontiers merged by iterating over chunks and resets frontiers to
  // the current chunk frontiers.
  storage::UserFrontiersPtr ResetFrontiers() {
    storage::UserFrontiersPtr result;
    std::swap(result, frontiers_);
    if (Valid()) {
      UpdateFrontiers();

      // Resetting because we need to track the frontiers update state after this call.
      frontiers_updated_ = false;
    }
    return result;
  }

  size_t GetOrderNo() const {
    return Valid() ? lsm_.GetImmutableChunkOrderNo(*outer_it_) :
        chunks_.empty() ? 0 : lsm_.GetImmutableChunkOrderNo(chunks_.back());
  }

  size_t GetNumVectors() const {
    if (!cached_num_vectors_.has_value()) {
      size_t num_vectors = 0;
      for (const auto& chunk : chunks_) {
        auto index = lsm_.GetImmutableChunkIndex(chunk);
        num_vectors += index ? index->Size() : 0;
      }
      cached_num_vectors_ = num_vectors;
    }
    return *cached_num_vectors_;
  }

 private:
  bool InnerValid() const {
    return inner_it_ != inner_end_;
  }

  // Outer iterator must be valid and advanced to the next chunk if necessary. To prevent
  // the infinite loop of the inner iterator, the method accepts a flag to explicitly indicate
  // if the inner iterator should be reset (it is expected to be true on the outer iterator move).
  // Returns true if the inner iterator advanced to the next vector which passes the filter.
  bool InnerNext(bool reset_inner_iterator) {
    // 1. Setup/advance inner iterator.
    if (reset_inner_iterator) {
      // Sanity check.
      DCHECK(Valid());

      // Outer iterator may have no index (for example, for frontiers only update).
      auto index = lsm_.GetImmutableChunkIndex(*outer_it_);
      if (!index) {
        return false;
      }
      inner_it_  = index->begin();
      inner_end_ = index->end();
    } else if (InnerValid()) {
      ++inner_it_;
    } else {
      return false;
    }

    // 2. Find a vector which passes the filter and update the value_ only in case of success.
    ValueType current_value;
    while (inner_it_ != inner_end_) {
      current_value = *inner_it_;
      if (filter_.Filter(current_value.first) == storage::FilterDecision::kKeep) {
        value_ = std::move(current_value);
        return true;
      }
      ++inner_it_;
    }

    return false;
  }

  // The caller must guarantee that the current outer iterator is valid.
  void UpdateFrontiers() {
    DCHECK(Valid());

    // Sanity check for the invariant that user_frontiers are non-null: only manifested chunks
    // are compaction inputs, and flush and manifest load always attach user frontiers to the
    // immutable chunks (refer to `ImmutableChunk::AddToUpdate`).
    const auto& user_frontiers = lsm_.GetImmutableChunkFrontiers(*outer_it_);
    DCHECK(user_frontiers);

    storage::UpdateFrontiers(frontiers_, *user_frontiers);
    frontiers_updated_ = true;
  }

  LSM& lsm_;
  const ImmutableChunkPtrs& chunks_;
  VectorLSMMergeFilter& filter_;
  Iterator outer_it_;
  InnerIterator inner_it_ { nullptr };
  InnerIterator inner_end_ { nullptr };
  std::optional<ValueType> value_ { std::nullopt };
  storage::UserFrontiersPtr frontiers_;
  bool frontiers_updated_ = false;
  mutable std::optional<size_t> cached_num_vectors_ { std::nullopt };
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSM<Vector, DistanceResult>::Merger::Impl {
 public:
  using LSM = VectorLSM<Vector, DistanceResult>;
  using VectorIndexPtr = typename LSM::VectorIndexPtr;
  using Iterator = typename LSM::MergingIterator;
  using MergeRegistry = VectorLSMMergeRegistry<Vector, DistanceResult>;

  Impl(LSM& lsm, MergeRegistry& merge_registry, PriorityThreadPoolSuspender* suspender)
      : lsm_(lsm), merge_registry_(merge_registry), suspender_(suspender) {
  }

  Result<ImmutableChunkPtrs> Merge(Iterator& input_it, size_t max_vectors_per_output_chunk) {
    ImmutableChunkPtrs merged_chunks;

    // Calculate number of vectors per chunk.
    const auto input_size = input_it.GetNumVectors();
    const auto num_vectors_per_chunk = max_vectors_per_output_chunk == 0 ?
        input_size : std::min(input_size, max_vectors_per_output_chunk);

    auto do_merge = merge_registry_.MaxCapacity() == 0
        ? &Impl::DoMerge : &Impl::DoMergeWithThreadPool;

    // While there's something to read. All the underlying calls must hold the invariant that
    // input iterator is already positioned to the next vector.
    while (input_it.Valid()) {
      VectorIndexPtr merged_index;
      if (num_vectors_per_chunk == 0) {
        // Frontiers-only input: walk all chunks to accumulate frontiers, no index to merge.
        while (input_it.Next()) {}
      } else {
        merged_index = VERIFY_RESULT(lsm_.CreateVectorIndex(
            num_vectors_per_chunk, rocksdb::Cache::ReservationMode::kAlways));
        RETURN_NOT_OK(std::invoke(do_merge, this, input_it, num_vectors_per_chunk, merged_index));

        if (TEST_sleep_on_merged_chunk_populated) {
          SleepFor(TEST_sleep_on_merged_chunk_populated);
        }
      }

      const auto num_vectors_merged = merged_index ? merged_index->Size() : 0;
      LOG_WITH_PREFIX(INFO) << "Chunks merge done [vectors: " << num_vectors_merged << "]";

      // If nothing got merged into the index, probably all vectors are read or are outdated.
      // But the chunk may still be required to not lose the merged frontiers.
      VectorLSMFileMetaDataPtr merged_index_file;
      if (!merged_index || merged_index->Size() == 0) {
        // The expectation here is that input iterator is already at the end.
        RSTATUS_DCHECK(!input_it.Valid(), IllegalState, "Input iterator must be at the end");
      } else {
        // Check shutting down in progress before saving new vector index on disk.
        RETURN_NOT_OK(lsm_.DoCheckRunning(__FILE__, __LINE__));

        // Save index to disk, the index may be updated to a different structure.
        VectorIndexPtr new_index;
        std::tie(merged_index_file, new_index) = VERIFY_RESULT(
            lsm_.SaveIndexToFile(*merged_index, lsm_.NextSerialNo()));
        if (new_index) {
          merged_index = new_index;
        }
      }

      if (merged_index_file || merged_chunks.empty() || input_it.FrontiersUpdated()) {
        // Get current merged frontiers and make sure they are not empty.
        auto merged_frontiers = input_it.ResetFrontiers();
        DCHECK_ONLY_NOTNULL(merged_frontiers.get());

        // Create new immutable chunk and add it to the list of new chunks.
        merged_chunks.push_back(lsm_.MakeCompactedChunk(
            input_it.GetOrderNo(), std::move(merged_index_file), std::move(merged_index),
            std::move(merged_frontiers)));
      }
    }

    return merged_chunks;
  }

 private:
  const std::string& LogPrefix() const {
    return lsm_.LogPrefix();
  }

  Status DoMerge(
      Iterator& source_iterator, size_t num_vectors_to_merge, VectorIndexPtr target_index) {
    // Let's be more conservative and don't check shutdown status on every inserted vector.
    const size_t min_iterations_to_check_shutdown =
        std::min<size_t>(2, 200000 / target_index->Dimensions());
    size_t num_iterations_to_check_shutdown = min_iterations_to_check_shutdown;

    // Adding all input vectors to the target index, filtering outdated vectors out.
    size_t num_vectors_added = 0;
    while ((num_vectors_added < num_vectors_to_merge) && source_iterator.Next()) {
      RETURN_NOT_OK(target_index->Insert(source_iterator->first, source_iterator->second));
      ++num_vectors_added;

      if (--num_iterations_to_check_shutdown == 0) {
        RETURN_NOT_OK(lsm_.DoCheckRunning(__FILE__, __LINE__));
        MaybeYield();
        TEST_SYNC_POINT("VectorLSM::DoMerge:Checkpoint");
        num_iterations_to_check_shutdown = min_iterations_to_check_shutdown;
      }
    }

    return Status::OK();
  }

  Status DoMergeWithThreadPool(
      Iterator& source_iterator, size_t num_vectors_to_merge, VectorIndexPtr target_index) {
    size_t num_total_tasks = ceil_div<size_t>(num_vectors_to_merge, FLAGS_vector_index_task_size);
    const size_t num_vectors_per_task = ceil_div(num_vectors_to_merge, num_total_tasks);

    size_t num_scheduled_tasks = 0;
    std::atomic<size_t> num_completed_tasks = 0;

    size_t num_remaining_vectors = num_vectors_to_merge;
    while (num_remaining_vectors > 0 && source_iterator.Valid()) {
      // The actual vector inserts run on a separate (insert) thread pool; this loop only schedules
      // them and otherwise sleeps waiting for registry capacity, so yield the priority pool worker
      // to higher priority tasks (e.g. flushes) instead of holding it for the whole merge.
      MaybeYield();

      // On shutdown stop scheduling, but fall through to the wait loop below so all already
      // scheduled tasks are drained before this frame (and `num_completed_tasks`) goes away.
      if (lsm_.IsShuttingDown()) {
        break;
      }

      auto tasks = VERIFY_RESULT(merge_registry_.AllocateTasks(
          num_total_tasks, target_index,
          [&num_completed_tasks](const Status&) {
            // TODO: Handle failure
            num_completed_tasks.fetch_add(1, std::memory_order::relaxed);
          }));

      VLOG_WITH_PREFIX(3) << "Allocated " << tasks.size() << " merge tasks";

      if (tasks.empty()) {
        std::this_thread::sleep_for(200ms);
        continue;
      }
      num_total_tasks -= tasks.size();

      PopulateMergeTasks(tasks, num_vectors_per_task, num_remaining_vectors, source_iterator);

      // `tasks` is now counted in the merge registry. Once allocated they must always be executed:
      // the registry only tracks executed tasks via active_tasks_, so dropping an allocated batch
      // before executing it would leak its reserved capacity and hang the registry's Shutdown(). A
      // shutdown check here would be racy (shutdown could start right after it), so we do not bail
      // mid-batch -- the loop checks IsShuttingDown() at the top before allocating the next batch.
      TEST_SYNC_POINT("VectorLSM::DoMergeWithThreadPool:AfterAllocate");

      num_scheduled_tasks += tasks.size();
      merge_registry_.ExecuteTasks(tasks);
    }

    // Wait for everything got merged.
    while (num_scheduled_tasks != num_completed_tasks.load(std::memory_order::relaxed)) {
      MaybeYield();
      std::this_thread::sleep_for(200ms);
    }

    // All scheduled tasks have completed, so it is now safe to propagate a shutdown that may have
    // broken the loop above.
    RETURN_NOT_OK(lsm_.DoCheckRunning(__FILE__, __LINE__));

    LOG_WITH_PREFIX(INFO) << "Chunks merge done via " << num_scheduled_tasks << " tasks";
    return Status::OK();
  }

  // Yields the priority thread pool worker to higher priority tasks if any are waiting. Called at
  // the existing per-step checkpoints of both merge paths so a long compaction does not hold its
  // worker (and starve flushes) for the whole merge. No-op when there is no suspender (the task is
  // not running on a priority pool worker).
  void MaybeYield() {
    if (suspender_) {
      suspender_->PauseIfNecessary();
    }
  }

  LSM& lsm_;
  MergeRegistry& merge_registry_;
  PriorityThreadPoolSuspender* suspender_;
};


template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::MergingIterator::MergingIterator(
    VectorLSM& lsm, const ImmutableChunkPtrs& chunks, VectorLSMMergeFilter& filter)
    : impl_(std::make_unique<Impl>(lsm, chunks, filter)) {
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::MergingIterator::~MergingIterator() = default;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::MergingIterator::Valid() const {
  return impl_->Valid();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
typename VectorLSM<Vector, DistanceResult>::VectorIndex::IteratorValue&
VectorLSM<Vector, DistanceResult>::MergingIterator::operator*() {
  return impl_->operator*();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
typename VectorLSM<Vector, DistanceResult>::VectorIndex::IteratorValue*
VectorLSM<Vector, DistanceResult>::MergingIterator::operator->() {
  return impl_->operator->();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::MergingIterator::Next() {
  return impl_->Next();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
bool VectorLSM<Vector, DistanceResult>::MergingIterator::FrontiersUpdated() const {
  return impl_->FrontiersUpdated();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
storage::UserFrontiersPtr VectorLSM<Vector, DistanceResult>::MergingIterator::ResetFrontiers() {
  return impl_->ResetFrontiers();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
size_t VectorLSM<Vector, DistanceResult>::MergingIterator::GetOrderNo() const {
  return impl_->GetOrderNo();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
size_t VectorLSM<Vector, DistanceResult>::MergingIterator::GetNumVectors() const {
  return impl_->GetNumVectors();
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::Merger::Merger(
    VectorLSM& lsm, VectorLSMMergeRegistry<Vector, DistanceResult>& merge_registry,
    PriorityThreadPoolSuspender* suspender)
    : impl_(std::make_unique<Impl>(lsm, merge_registry, suspender)) {
}

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
VectorLSM<Vector, DistanceResult>::Merger::~Merger() = default;

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
Result<typename VectorLSM<Vector, DistanceResult>::ImmutableChunkPtrs>
VectorLSM<Vector, DistanceResult>::Merger::Merge(
    MergingIterator& input_it, size_t max_vectors_per_output_chunk) {
  return impl_->Merge(input_it, max_vectors_per_output_chunk);
}

template class VectorLSM<std::vector<float>, float>::MergingIterator;
template class VectorLSM<std::vector<uint8_t>, float>::MergingIterator;
template class VectorLSM<std::vector<uint8_t>, uint32_t>::MergingIterator;

template class VectorLSM<std::vector<float>, float>::Merger;
template class VectorLSM<std::vector<uint8_t>, float>::Merger;
template class VectorLSM<std::vector<uint8_t>, uint32_t>::Merger;

}  // namespace yb::vector_index
