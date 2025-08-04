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

#include "yb/rocksdb/table/index_reader.h"

#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/block_based_table_internal.h"
#include "yb/rocksdb/table/iterator_wrapper.h"
#include "yb/rocksdb/table/meta_blocks.h"
#include "yb/util/slice.h"

namespace rocksdb {

using namespace std::placeholders;

namespace {

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
      std::unique_ptr<TwoLevelIteratorState> data_iterator_state)
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
  std::unique_ptr<TwoLevelIteratorState> const data_iterator_state_;
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
    BlockIter* iter, std::unique_ptr<TwoLevelIteratorState> index_iterator_state,
    std::unique_ptr<TwoLevelIteratorState> data_iterator_state, bool) {
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
    BlockIter* iter, std::unique_ptr<TwoLevelIteratorState> index_iterator_state,
    std::unique_ptr<TwoLevelIteratorState> data_iterator_state, bool) {
  return new DataBlockAwareIndexInternalIteratorImpl(
      std::unique_ptr<InternalIterator>(
          NewIterator(iter, std::move(index_iterator_state), /* total_order_seek = */ true)),
      std::move(data_iterator_state));
}


class MultiLevelIterator final : public DataBlockAwareIndexInternalIteratorBase {
 public:
  static constexpr auto kIterChainInitialCapacity = 4;

  MultiLevelIterator(
      std::unique_ptr<TwoLevelIteratorState> index_iterator_state,
      std::unique_ptr<TwoLevelIteratorState> data_iterator_state, InternalIterator* top_level_iter,
      uint32_t num_levels, bool need_free_top_level_iter)
      : index_iterator_state_(std::move(index_iterator_state)),
        data_iterator_state_(std::move(data_iterator_state)),
        iter_(num_levels),
        index_block_handle_(num_levels - 1),
        bottom_level_iter_(iter_.data() + (num_levels - 1)),
        need_free_top_level_iter_(need_free_top_level_iter) {
    iter_[0].Set(top_level_iter);
  }

  ~MultiLevelIterator() {
    IteratorWrapper* iter = iter_.data() + (need_free_top_level_iter_ ? 0 : 1);
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

    return DoSeek(std::bind(&IteratorWrapper::Seek, std::placeholders::_1, target));
  }

  const KeyValueEntry& SeekToFirst() override {
    return DoSeek(std::bind(&IteratorWrapper::SeekToFirst, std::placeholders::_1));
  }

  const KeyValueEntry& SeekToLast() override {
    return DoSeek(std::bind(&IteratorWrapper::SeekToLast, std::placeholders::_1));
  }

  const KeyValueEntry& Next() override {
    return DoMove(
        std::bind(&IteratorWrapper::Next, std::placeholders::_1),
        std::bind(&IteratorWrapper::SeekToFirst, std::placeholders::_1)
    );
  }

  const KeyValueEntry& Prev() override {
    return DoMove(
        std::bind(&IteratorWrapper::Prev, std::placeholders::_1),
        std::bind(&IteratorWrapper::SeekToLast, std::placeholders::_1)
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
      const IteratorWrapper* iter = iter_.data();
      while (iter <= bottommost_positioned_iter_ && iter->iter()) {
        if (!iter->status().ok()) {
          return iter->status();
        }
        ++iter;
      }
    }
    return status_;
  }

  void SetSubIterator(IteratorWrapper* iter_wrapper, InternalIterator* iter) {
    if (iter_wrapper->iter() != nullptr) {
      SaveError(iter_wrapper->status());
    }
    iter_wrapper->Set(iter);
  }

 private:
  void SaveError(const Status& s) {
    if (status_.ok() && !s.ok()) status_ = s;
  }

  template <typename F>
  const KeyValueEntry& DoSeek(F seek_function) {
    IteratorWrapper* iter = iter_.data();
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
    IteratorWrapper* iter = bottom_level_iter_;
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

  void InitSubIterator(IteratorWrapper* parent_iter) {
    DCHECK(parent_iter->Valid());
    IteratorWrapper* sub_iter = parent_iter + 1;
    std::string* child_index_block_handle =
        index_block_handle_.data() + (parent_iter - iter_.data());
    Slice handle = parent_iter->value();
    if (sub_iter->iter() && !sub_iter->status().IsIncomplete()
        && handle.compare(*child_index_block_handle) == 0) {
      // wrapper is already set to iterator for this handle, no need to change.
    } else {
      // TODO(index_iter): consider updating existing iterator rather than recreating, measure
      // potential perf impact.
      InternalIterator* iter = index_iterator_state_->NewSecondaryIterator(handle);
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

  // We maintain separate states for data and index iterator in order to prevent data sequential
  // reads and index sequential reads resetting each other readahead state.
  // Such scenario happens when we sequentially read data and using Seek, for example during
  // sampling data for YSQL ANALYZE query execution.
  std::unique_ptr<TwoLevelIteratorState> const index_iterator_state_;
  std::unique_ptr<TwoLevelIteratorState> const data_iterator_state_;
  boost::container::small_vector<IteratorWrapper, kIterChainInitialCapacity> iter_;
  // If iter_[level] holds non-nullptr, then "index_block_handle_[level-1]" holds the
  // handle passed to index_iterator_state_->NewSecondaryIterator to create iter_[level].
  boost::container::small_vector<std::string, kIterChainInitialCapacity - 1> index_block_handle_;
  IteratorWrapper* const bottom_level_iter_;
  bool need_free_top_level_iter_;
  Status status_ = Status::OK();
  IteratorWrapper* bottommost_positioned_iter_ = nullptr;
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
    BlockIter* iter, std::unique_ptr<TwoLevelIteratorState> index_iterator_state,
    const bool) {
  return NewDataBlockAwareIterator(
      iter, std::move(index_iterator_state), /* data_iterator_state = */ nullptr,
      /* total_order_seek = */ true);
}

DataBlockAwareIndexInternalIterator* MultiLevelIndexReader::NewDataBlockAwareIterator(
    BlockIter* iter, std::unique_ptr<TwoLevelIteratorState> index_iterator_state,
    std::unique_ptr<TwoLevelIteratorState> data_iterator_state, bool) {
  InternalIterator* top_level_iter = top_level_index_block_->NewIndexBlockIterator(
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
