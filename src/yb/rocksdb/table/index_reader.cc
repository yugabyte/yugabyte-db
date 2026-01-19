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

#include "yb/rocksdb/table/index_reader.h"

#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/block_based_table_internal.h"
#include "yb/rocksdb/table/iterator_wrapper.h"
#include "yb/rocksdb/table/meta_blocks.h"
#include "yb/util/slice.h"

namespace rocksdb {

using namespace std::placeholders;

namespace {

using BlockIterWrapper = IteratorWrapperBase<BlockIter, /* kSkipLastEntry = */ false>;

class DataBlockAwareIndexInternalIteratorBase : public DataBlockAwareIndexInternalIterator {
 public:
  virtual std::unique_ptr<InternalIterator> GetCurrentDataBlockIterator() const = 0;

  yb::Result<std::pair<std::string, std::string>> GetCurrentDataBlockBounds() const override {
    auto block_iter = GetCurrentDataBlockIterator();
    RETURN_NOT_OK(block_iter->status());

    std::pair<std::string, std::string> result;
    bool first = true;
    for (auto& entry : {block_iter->SeekToFirst(), block_iter->SeekToLast()}) {
      if (!entry.Valid()) {
        BlockHandle block_handle;
        Slice block_handle_slice = value();
        RETURN_NOT_OK(block_handle.DecodeFrom(&block_handle_slice));
        return STATUS_FORMAT(IllegalState, "Block is empty for $0", block_handle.ToDebugString());
      }
      if (first) {
        result.first = entry.key;
      } else {
        result.second = entry.key;
      }
      first = false;
    }
    return result;
  };
};

class DataBlockAwareIndexInternalIteratorImpl : public DataBlockAwareIndexInternalIteratorBase {
 public:
  DataBlockAwareIndexInternalIteratorImpl(
      std::unique_ptr<InternalIterator> index_internal_iterator,
      std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state)
      : index_internal_iterator_(std::move(index_internal_iterator)),
        data_iterator_state_(std::move(data_iterator_state)) {}

  const KeyValueEntry& Entry() const override { return index_internal_iterator_->Entry(); }
  const KeyValueEntry& Next() override { return index_internal_iterator_->Next(); }
  const KeyValueEntry& Prev() override { return index_internal_iterator_->Prev(); }
  const KeyValueEntry& SeekToFirst() override { return index_internal_iterator_->SeekToFirst(); }
  const KeyValueEntry& SeekToLast() override { return index_internal_iterator_->SeekToLast(); }

  const KeyValueEntry& Seek(Slice target) override {
    return index_internal_iterator_->Seek(target);
  }

  Status status() const override { return index_internal_iterator_->status(); }

 protected:
  std::unique_ptr<InternalIterator> GetCurrentDataBlockIterator() const override {
    if (!index_internal_iterator_->Valid()) {
      return std::unique_ptr<InternalIterator>(
          NewErrorInternalIterator(STATUS(IllegalState, "Index iterator is not valid")));
    }
    return std::unique_ptr<InternalIterator>(
        data_iterator_state_->NewSecondaryIterator(index_internal_iterator_->value()));
  }

  std::unique_ptr<InternalIterator> const index_internal_iterator_;
  std::unique_ptr<TwoLevelBlockIteratorState> const data_iterator_state_;
};

} // namespace

Result<std::unique_ptr<BinarySearchIndexReader>> BinarySearchIndexReader::Create(
    RandomAccessFileReader* file, const Footer& footer,
    const BlockHandle& index_handle, Env* env,
    const ComparatorPtr& comparator,
    const std::shared_ptr<yb::MemTracker>& mem_tracker) {
  std::unique_ptr<Block> index_block;
  RETURN_NOT_OK(block_based_table::ReadBlockFromFile(
      file, footer, ReadOptions::kDefault, index_handle, &index_block, env, mem_tracker));

  return std::unique_ptr<BinarySearchIndexReader>(
      new BinarySearchIndexReader(comparator, std::move(index_block)));
}

Result<std::string> BinarySearchIndexReader::GetMiddleKey() const {
  return index_block_->GetMiddleKey(kIndexBlockKeyValueEncodingFormat);
}

DataBlockAwareIndexInternalIterator* BinarySearchIndexReader::NewDataBlockAwareIterator(
    BlockIter* iter, std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
    std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state, bool) {
  return new DataBlockAwareIndexInternalIteratorImpl(
      std::unique_ptr<InternalIterator>(
          NewIterator(iter, std::move(index_iterator_state), /* total_order_seek = */ true)),
      std::move(data_iterator_state));
}


Result<std::unique_ptr<IndexReader>> HashIndexReader::Create(
    const SliceTransform* hash_key_extractor,
    const Footer& footer, RandomAccessFileReader* file,
    Env* env, const ComparatorPtr& comparator,
    const BlockHandle& index_handle,
    InternalIterator* meta_index_iter,
    bool hash_index_allow_collision,
    const std::shared_ptr<yb::MemTracker>& mem_tracker) {
  std::unique_ptr<Block> index_block;
  auto s = block_based_table::ReadBlockFromFile(file, footer, ReadOptions::kDefault, index_handle,
                             &index_block, env, mem_tracker);

  if (!s.ok()) {
    return s;
  }

  // Note, failure to create prefix hash index does not need to be a hard error. We can still fall
  // back to the original binary search index.
  // So, Create will succeed regardless, from this point on.
  std::unique_ptr<HashIndexReader> index_reader(
      new HashIndexReader(comparator, std::move(index_block)));

  // Get prefixes block
  BlockHandle prefixes_handle;
  s = FindMetaBlock(meta_index_iter, kHashIndexPrefixesBlock,
                    &prefixes_handle);
  if (!s.ok()) {
    LOG(DFATAL) << "Failed to find hash index prefixes block: " << s;
    return index_reader;
  }

  // Get index metadata block
  BlockHandle prefixes_meta_handle;
  s = FindMetaBlock(meta_index_iter, kHashIndexPrefixesMetadataBlock,
                    &prefixes_meta_handle);
  if (!s.ok()) {
    LOG(DFATAL) << "Failed to find hash index prefixes metadata block: " << s;
    return index_reader;
  }

  // Read contents for the blocks
  BlockContents prefixes_contents;
  s = ReadBlockContents(file, footer, ReadOptions::kDefault, prefixes_handle,
                        &prefixes_contents, env, mem_tracker, true /* do decompression */);
  if (!s.ok()) {
    return s;
  }
  BlockContents prefixes_meta_contents;
  s = ReadBlockContents(file, footer, ReadOptions::kDefault, prefixes_meta_handle,
                        &prefixes_meta_contents, env, mem_tracker,
                        true /* do decompression */);
  if (!s.ok()) {
    LOG(DFATAL) << "Failed to read hash index prefixes metadata block: " << s;
    return index_reader;
  }

  if (!hash_index_allow_collision) {
    // TODO: deprecate once hash_index_allow_collision proves to be stable.
    BlockHashIndex* hash_index = nullptr;
    s = CreateBlockHashIndex(hash_key_extractor,
                             prefixes_contents.data,
                             prefixes_meta_contents.data,
                             &hash_index);
    if (s.ok()) {
      index_reader->index_block_->SetBlockHashIndex(hash_index);
      index_reader->OwnPrefixesContents(std::move(prefixes_contents));
    } else {
      LOG(DFATAL) << "Failed to create block hash index: " << s;
    }
  } else {
    BlockPrefixIndex* prefix_index = nullptr;
    s = BlockPrefixIndex::Create(hash_key_extractor,
                                 prefixes_contents.data,
                                 prefixes_meta_contents.data,
                                 &prefix_index);
    if (s.ok()) {
      index_reader->index_block_->SetBlockPrefixIndex(prefix_index);
    } else {
      LOG(DFATAL) << "Failed to create block prefix index: " << s;
    }
  }

  return index_reader;
}

Result<std::string> HashIndexReader::GetMiddleKey() const {
  return index_block_->GetMiddleKey(kIndexBlockKeyValueEncodingFormat);
}

DataBlockAwareIndexInternalIterator* HashIndexReader::NewDataBlockAwareIterator(
    BlockIter* iter, std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
    std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state, bool) {
  return new DataBlockAwareIndexInternalIteratorImpl(
      std::unique_ptr<InternalIterator>(
          NewIterator(iter, std::move(index_iterator_state), /* total_order_seek = */ true)),
      std::move(data_iterator_state));
}

// Helpers for MultiLevelIterator::GetMiddleKey()
namespace {

using BlockIteratorWrapper = IteratorWrapperBase<BlockIter, /* kSkipLastEntry = */ false>;
using BlockBoundaryRestarts = std::pair<uint32_t, uint32_t>;

// Get the bounding restarts of an index or data block.
Result<BlockBoundaryRestarts> GetBlockBoundaryRestarts(
    BlockIter* block_iter, Slice lower_bound_key) {
  if (DCHECK_NOTNULL(block_iter)->GetNumRestarts() == 0) {
    // Not possible to have less than 1 restart at all, refer to the BlockBuilder's constructor.
    return STATUS(Corruption, "Restarts number cannot be zero, this might be a data corruption!");
  }

  if (VLOG_IS_ON(3)) {
    for (uint i = 0; i < block_iter->GetNumRestarts(); ++i) {
      block_iter->SeekToRestart(i);
      VLOG_WITH_FUNC(3) << "Restart " << i << ": " << block_iter->key().ToDebugHexString();
    }
  }

  // Seek to lowest key restart.
  if (!lower_bound_key.empty()) {
    VLOG_WITH_FUNC(3) << "Input low key: " << lower_bound_key.ToDebugHexString();
    block_iter->Seek(lower_bound_key);
  } else {
    block_iter->SeekToFirst();
  }

  RETURN_NOT_OK_PREPEND(block_iter->status(), "Failed to seek to lower bound entry");
  if (!block_iter->Valid()) {
    return STATUS(InternalError, "Failed to seek to lower bound entry");
  }

  BlockBoundaryRestarts restarts;
  restarts.first = block_iter->GetCurrentRestart();
  restarts.second = block_iter->GetNumRestarts() - 1;
  VLOG_WITH_FUNC(3) << yb::Format(
      "Bound: [$0, $1], qualifying entries: $2, total entries: $3",
      restarts.first, restarts.second, 1 + restarts.second - restarts.first, 1 + restarts.second);
  return restarts;
}

Result<Slice> GetValueAtRestartIndex(BlockIter* block_iter, uint32_t restart_idx) {
  DCHECK_NOTNULL(block_iter)->SeekToRestart(restart_idx);
  RETURN_NOT_OK_PREPEND(block_iter->status(), "Failed to seek");
  if (!block_iter->Valid()) {
    return STATUS(InternalError, "Failed to seek");
  }
  return block_iter->value();
}

} // namespace

class MultiLevelIterator final : public DataBlockAwareIndexInternalIteratorBase {
 public:
  static constexpr auto kIterChainInitialCapacity = 4;

  MultiLevelIterator(
      std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
      std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state, BlockIter* top_level_iter,
      uint32_t num_levels, bool need_free_top_level_iter)
      : num_levels_(num_levels),
        index_iterator_state_(std::move(index_iterator_state)),
        data_iterator_state_(std::move(data_iterator_state)),
        iter_(num_levels),
        index_block_handle_(num_levels - 1),
        bottom_level_iter_(iter_.data() + (num_levels - 1)),
        need_free_top_level_iter_(need_free_top_level_iter) {
    iter_[0].Set(top_level_iter);
  }

  ~MultiLevelIterator() {
    auto* iter = iter_.data() + (need_free_top_level_iter_ ? 0 : 1);
    while (iter <= bottom_level_iter_) {
      iter->DeleteIter(false /* arena_mode */);
      ++iter;
    }
  }

  const KeyValueEntry& Seek(Slice target) override {
    if (index_iterator_state_->check_prefix_may_match &&
        !index_iterator_state_->PrefixMayMatch(target)) {
      bottommost_positioned_iter_ = &iter_[0];
      return Entry();
    }

    return DoSeek(std::bind(&BlockIterWrapper::Seek, std::placeholders::_1, target));
  }

  const KeyValueEntry& SeekToFirst() override {
    return DoSeek(std::bind(&BlockIterWrapper::SeekToFirst, std::placeholders::_1));
  }

  const KeyValueEntry& SeekToLast() override {
    return DoSeek(std::bind(&BlockIterWrapper::SeekToLast, std::placeholders::_1));
  }

  const KeyValueEntry& Next() override {
    return DoMove(
        std::bind(&BlockIterWrapper::Next, std::placeholders::_1),
        std::bind(&BlockIterWrapper::SeekToFirst, std::placeholders::_1)
    );
  }

  const KeyValueEntry& Prev() override {
    return DoMove(
        std::bind(&BlockIterWrapper::Prev, std::placeholders::_1),
        std::bind(&BlockIterWrapper::SeekToLast, std::placeholders::_1)
    );
  }

  const KeyValueEntry& Entry() const override {
    if (bottommost_positioned_iter_ == bottom_level_iter_) {
      return bottom_level_iter_->Entry();
    }

    return KeyValueEntry::Invalid();
  }

  Status status() const override {
    if (bottommost_positioned_iter_) {
      const auto* iter = iter_.data();
      while (iter <= bottommost_positioned_iter_ && iter->iter()) {
        if (!iter->status().ok()) {
          return iter->status();
        }
        ++iter;
      }
    }
    return status_;
  }

  void SetSubIterator(BlockIterWrapper* iter_wrapper, BlockIter* iter) {
    if (iter_wrapper->iter() != nullptr) {
      SaveError(iter_wrapper->status());
    }
    iter_wrapper->Set(iter);
  }

  // Find the approximate middle key starting from the given lower bound key.
  // Algorithm:
  //   1. Start with the sequence of entries from the top-level index block.
  //   2. Repeat until a midpoint is found or no more levels remain:
  //      - If the sequence has 3 or more entries, return the midpoint entry.
  //      - If the sequence has exactly 2 entries, descend to their blocks (main and aux), replace
  //        the sequence with entries from those blocks in order, and continue.
  //      - If the sequence has exactly 1 entry, descend to its block, replace the sequence with
  //        entries from that block, and continue.
  //   3. If no midpoint was found, return a failed status.
  yb::Result<std::string> GetMiddleKey(Slice lower_bound_key) const override {
    VLOG_WITH_FUNC(3) << "Total levels: " << num_levels_;

    // Owners for child block iterators that we may descend to.
    std::unique_ptr<BlockIter> main_block_iter_holder;
    std::unique_ptr<BlockIter> aux_block_iter_holder;

    // This top-level block iterator should not be deallocated and hence is not referenced by the
    // unique pointers above.
    BlockIter* top_level_iter = iter_[0].iter();

    // Iterate from index top-level (0) up to and including the data blocks level (num_levels_).
    for (int64_t cur_level = 0; cur_level <= num_levels_; ++cur_level) {
      VLOG_WITH_FUNC(3) << "Level: " << cur_level;
      auto* main_block_iter = cur_level == 0 ? top_level_iter : main_block_iter_holder.get();
      auto* aux_block_iter = aux_block_iter_holder.get();

      // TODO(tsplit): This algorithm determines the middle key at the granularity of block
      // restarts and not records. A restart may contain several records (restart interval). This
      // may lead to a bad approximation of the middle record key when:
      //   1. The index block restart interval is greater than the default of 1
      //   2. The middle key is obtained from a data block, where the default interval is 16
      // We should add checks for the index block restart interval and handle data blocks better.

      // Find the bounding restarts for the main block.
      auto main_bounds = VERIFY_RESULT(GetBlockBoundaryRestarts(main_block_iter, lower_bound_key));
      // Number of entries in the main block.
      auto num_main_entries = main_bounds.second - main_bounds.first + 1;
      // Number of entries in the main block + aux block.
      auto num_total_entries = num_main_entries;

      // Find the bounding restarts for the aux block, if an aux block exists.
      BlockBoundaryRestarts aux_bounds;
      if (aux_block_iter) {
        aux_bounds = VERIFY_RESULT(GetBlockBoundaryRestarts(aux_block_iter, Slice{}));
        DCHECK_EQ(aux_bounds.first, 0);
        num_total_entries += aux_bounds.second - aux_bounds.first + 1;
      }

      // If more than 2 restarts exist across the main and aux block, find the restart in the
      // middle.
      if (num_total_entries > 2) {
        const auto policy = cur_level == num_levels_ ?
            MiddlePointPolicy::kMiddleHigh : MiddlePointPolicy::kMiddleLow;
        auto middle_idx = Block::GetMiddlePointIndex(num_total_entries, policy);
        bool main_has_middle = middle_idx < num_main_entries;
        BlockIter* block_iter;
        uint32_t restart_idx;
        Slice middle_key;
        if (main_has_middle) {
          block_iter = main_block_iter;
          restart_idx = main_bounds.first + middle_idx;
        } else {
          block_iter = DCHECK_NOTNULL(aux_block_iter);
          restart_idx = aux_bounds.first + middle_idx - num_main_entries;
        }
        block_iter->SeekToRestart(restart_idx);
        middle_key = block_iter->key();
        VLOG_WITH_FUNC(3) << yb::Format(
            "Middle key found in $0 block: restart: $1, key: $2",
            main_has_middle ? "main" : "aux", restart_idx, middle_key.ToDebugHexString());
        return middle_key.ToBuffer();
      }

      // If at last level, we cannot descend further.
      if (cur_level == num_levels_) {
        break;
      }

      // Descend to the next level.
      if (num_total_entries == 2) {
        // If exactly two restarts exist across the main and aux block, descend to the two
        // corresponding child blocks and load them as main and aux.
        std::string main_handle;
        std::string aux_handle;
        if (num_main_entries == 2) {
          main_handle = VERIFY_RESULT(GetValueAtRestartIndex(main_block_iter, main_bounds.first));
          aux_handle = VERIFY_RESULT(GetValueAtRestartIndex(main_block_iter, main_bounds.second));
        } else if (num_main_entries == 1) {
          main_handle = VERIFY_RESULT(GetValueAtRestartIndex(main_block_iter, main_bounds.first));
          aux_handle = VERIFY_RESULT(GetValueAtRestartIndex(aux_block_iter, aux_bounds.first));
        } else {
          main_handle = VERIFY_RESULT(GetValueAtRestartIndex(aux_block_iter, aux_bounds.first));
          aux_handle = VERIFY_RESULT(GetValueAtRestartIndex(aux_block_iter, aux_bounds.second));
        }
        auto& iterator_state =
            cur_level == num_levels_ - 1 ? data_iterator_state_ : index_iterator_state_;
        main_block_iter_holder.reset(iterator_state->NewSecondaryIterator(main_handle));
        aux_block_iter_holder.reset(iterator_state->NewSecondaryIterator(aux_handle));
      } else if (num_total_entries == 1) {
        // If exactly one restart exists, descend to the corresponding child block.
        auto& iterator_state =
            cur_level == num_levels_ - 1 ? data_iterator_state_ : index_iterator_state_;
        std::string main_handle;
        if (num_main_entries == 1) {
          main_handle = VERIFY_RESULT(GetValueAtRestartIndex(main_block_iter, main_bounds.first));
        } else {
          main_handle = VERIFY_RESULT(GetValueAtRestartIndex(aux_block_iter, aux_bounds.first));
        }
        main_block_iter_holder.reset(iterator_state->NewSecondaryIterator(main_handle));
        aux_block_iter_holder.reset();
      } else {
        break;
      }
    }

    // Return Incomplete when there are less than 3 data block restarts in the SST with user keys.
    return STATUS(Incomplete, "Insufficient entries in SST.");
  }

 private:
  void SaveError(const Status& s) {
    if (status_.ok() && !s.ok()) status_ = s;
  }

  template <typename F>
  const KeyValueEntry& DoSeek(F seek_function) {
    auto* iter = iter_.data();
    seek_function(iter);
    bottommost_positioned_iter_ = iter;
    while (iter < bottom_level_iter_ && iter->Valid()) {
      InitSubIterator(iter);
      ++iter;
      seek_function(iter);
    }
    bottommost_positioned_iter_ = iter;
    return Entry();
  }

  template <typename F1, typename F2>
  const KeyValueEntry& DoMove(F1 move_function, F2 lower_levels_init_function) {
    DCHECK(Valid());
    // First try to move iterator starting with bottom level.
    auto* iter = bottom_level_iter_;
    move_function(iter);
    while (!iter->Valid() && iter > iter_.data()) {
      --iter;
      move_function(iter);
    }
    if (!iter->Valid()) {
      bottommost_positioned_iter_ = iter;
      return Entry();
    }
    // Once we've moved iterator at some level, we need to reset iterators at levels below.
    while (iter < bottom_level_iter_) {
      InitSubIterator(iter);
      ++iter;
      lower_levels_init_function(iter);
    }
    bottommost_positioned_iter_ = bottom_level_iter_;
    return Entry();
  }

  void InitSubIterator(BlockIterWrapper* parent_iter) {
    DCHECK(parent_iter->Valid());
    auto* sub_iter = parent_iter + 1;
    std::string* child_index_block_handle =
        index_block_handle_.data() + (parent_iter - iter_.data());
    Slice handle = parent_iter->value();
    if (sub_iter->iter() && !sub_iter->status().IsIncomplete()
        && handle.compare(*child_index_block_handle) == 0) {
      // wrapper is already set to iterator for this handle, no need to change.
    } else {
      // TODO(index_iter): consider updating existing iterator rather than recreating, measure
      // potential perf impact.
      auto* iter = index_iterator_state_->NewSecondaryIterator(handle);
      handle.CopyToBuffer(child_index_block_handle);
      SetSubIterator(sub_iter, iter);
    }
  }

  std::unique_ptr<InternalIterator> GetCurrentDataBlockIterator() const override {
    if (!Valid()) {
      return std::unique_ptr<InternalIterator>(
          NewErrorInternalIterator(STATUS(IllegalState, "Index iterator is not valid")));
    }
    if (!data_iterator_state_) {
      return std::unique_ptr<InternalIterator>(
          NewErrorInternalIterator(STATUS(IllegalState, "Data iterator state is not set")));
    }
    return std::unique_ptr<InternalIterator>(data_iterator_state_->NewSecondaryIterator(value()));
  }

  uint32_t num_levels_;
  // We maintain separate states for data and index iterator in order to prevent data sequential
  // reads and index sequential reads resetting each other readahead state.
  // Such scenario happens when we sequentially read data and using Seek, for example during
  // sampling data for YSQL ANALYZE query execution.
  std::unique_ptr<TwoLevelBlockIteratorState> const index_iterator_state_;
  std::unique_ptr<TwoLevelBlockIteratorState> const data_iterator_state_;
  boost::container::small_vector<BlockIterWrapper, kIterChainInitialCapacity> iter_;
  // If iter_[level] holds non-nullptr, then "index_block_handle_[level-1]" holds the
  // handle passed to index_iterator_state_->NewSecondaryIterator to create iter_[level].
  boost::container::small_vector<std::string, kIterChainInitialCapacity - 1> index_block_handle_;
  BlockIterWrapper* const bottom_level_iter_;
  bool need_free_top_level_iter_;
  Status status_ = Status::OK();
  BlockIterWrapper* bottommost_positioned_iter_ = nullptr;
};

Result<std::unique_ptr<MultiLevelIndexReader>> MultiLevelIndexReader::Create(
    RandomAccessFileReader* file, const Footer& footer, const uint32_t num_levels,
    const BlockHandle& top_level_index_handle, Env* env, const ComparatorPtr& comparator,
    const std::shared_ptr<yb::MemTracker>& mem_tracker) {
  std::unique_ptr<Block> index_block;
  RETURN_NOT_OK(block_based_table::ReadBlockFromFile(
      file, footer, ReadOptions::kDefault, top_level_index_handle, &index_block, env,
      mem_tracker));

  return std::make_unique<MultiLevelIndexReader>(comparator, num_levels, std::move(index_block));
}

InternalIterator* MultiLevelIndexReader::NewIterator(
    BlockIter* iter, std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
    const bool) {
  return NewDataBlockAwareIterator(
      iter, std::move(index_iterator_state), /* data_iterator_state = */ nullptr,
      /* total_order_seek = */ true);
}

DataBlockAwareIndexInternalIterator* MultiLevelIndexReader::NewDataBlockAwareIterator(
    BlockIter* iter, std::unique_ptr<TwoLevelBlockIteratorState> index_iterator_state,
    std::unique_ptr<TwoLevelBlockIteratorState> data_iterator_state, bool) {
  auto* top_level_iter = top_level_index_block_->NewIndexBlockIterator(
      comparator_.get(), iter, /* total_order_seek = */ true);
  return new MultiLevelIterator(
      std::move(index_iterator_state), std::move(data_iterator_state), top_level_iter, num_levels_,
      top_level_iter != iter);
}

Result<std::string> MultiLevelIndexReader::GetMiddleKey() const {
  const auto middle_key =
      top_level_index_block_->GetMiddleKey(kIndexBlockKeyValueEncodingFormat, comparator_.get());
  if (!middle_key.ok() && middle_key.status().IsIncomplete() && (num_levels_ > 1)) {
    // Incomplete status means block has less than 2 entries and this shouldn't happen if there are
    // more than 1 level in the block, see MultiLevelIndexBuilder::FlushNextBlock().
    return STATUS_FORMAT(
        InternalError,
        "It is expected to have more than 1 entry in top-level block in case of more than 1 level,"
        " num_levels: $0",
        num_levels_);
  }
  return middle_key;
}

} // namespace rocksdb
