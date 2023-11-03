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

Status BinarySearchIndexReader::Create(
    RandomAccessFileReader* file, const Footer& footer,
    const BlockHandle& index_handle, Env* env,
    const ComparatorPtr& comparator,
    std::unique_ptr<IndexReader>* index_reader,
    const std::shared_ptr<yb::MemTracker>& mem_tracker) {
  std::unique_ptr<Block> index_block;
  auto s = block_based_table::ReadBlockFromFile(
      file, footer, ReadOptions::kDefault, index_handle, &index_block, env, mem_tracker);

  if (s.ok()) {
    index_reader->reset(new BinarySearchIndexReader(comparator, std::move(index_block)));
  }

  return s;
}

Result<std::string> BinarySearchIndexReader::GetMiddleKey() const {
  return index_block_->GetMiddleKey(kIndexBlockKeyValueEncodingFormat);
}

Status HashIndexReader::Create(const SliceTransform* hash_key_extractor,
                       const Footer& footer, RandomAccessFileReader* file,
                       Env* env, const ComparatorPtr& comparator,
                       const BlockHandle& index_handle,
                       InternalIterator* meta_index_iter,
                       std::unique_ptr<IndexReader>* index_reader,
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
  HashIndexReader* new_index_reader;
  index_reader->reset(new_index_reader = new HashIndexReader(comparator, std::move(index_block)));

  // Get prefixes block
  BlockHandle prefixes_handle;
  s = FindMetaBlock(meta_index_iter, kHashIndexPrefixesBlock,
                    &prefixes_handle);
  if (!s.ok()) {
    LOG(ERROR) << "Failed to find hash index prefixes block: " << s;
    return Status::OK();
  }

  // Get index metadata block
  BlockHandle prefixes_meta_handle;
  s = FindMetaBlock(meta_index_iter, kHashIndexPrefixesMetadataBlock,
                    &prefixes_meta_handle);
  if (!s.ok()) {
    LOG(ERROR) << "Failed to find hash index prefixes metadata block: " << s;
    return Status::OK();
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
    LOG(ERROR) << "Failed to read hash index prefixes metadata block: " << s;
    return Status::OK();
  }

  if (!hash_index_allow_collision) {
    // TODO: deprecate once hash_index_allow_collision proves to be stable.
    BlockHashIndex* hash_index = nullptr;
    s = CreateBlockHashIndex(hash_key_extractor,
                             prefixes_contents.data,
                             prefixes_meta_contents.data,
                             &hash_index);
    if (s.ok()) {
      new_index_reader->index_block_->SetBlockHashIndex(hash_index);
      new_index_reader->OwnPrefixesContents(std::move(prefixes_contents));
    } else {
      LOG(ERROR) << "Failed to create block hash index: " << s;
    }
  } else {
    BlockPrefixIndex* prefix_index = nullptr;
    s = BlockPrefixIndex::Create(hash_key_extractor,
                                 prefixes_contents.data,
                                 prefixes_meta_contents.data,
                                 &prefix_index);
    if (s.ok()) {
      new_index_reader->index_block_->SetBlockPrefixIndex(prefix_index);
    } else {
      LOG(ERROR) << "Failed to create block prefix index: " << s;
    }
  }

  return Status::OK();
}

Result<std::string> HashIndexReader::GetMiddleKey() const {
  return index_block_->GetMiddleKey(kIndexBlockKeyValueEncodingFormat);
}

class MultiLevelIterator final : public InternalIterator {
 public:
  static constexpr auto kIterChainInitialCapacity = 4;

  MultiLevelIterator(
      TwoLevelIteratorState* state, InternalIterator* top_level_iter, uint32_t num_levels,
      bool need_free_top_level_iter)
    : state_(state), iter_(num_levels), index_block_handle_(num_levels - 1),
      bottom_level_iter_(iter_.data() + (num_levels - 1)),
      need_free_top_level_iter_(need_free_top_level_iter)  {
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
    if (state_->check_prefix_may_match && !state_->PrefixMayMatch(target)) {
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
      InternalIterator* iter = state_->NewSecondaryIterator(handle);
      handle.CopyToBuffer(child_index_block_handle);
      SetSubIterator(sub_iter, iter);
    }
  }

  TwoLevelIteratorState* const state_;
  boost::container::small_vector<IteratorWrapper, kIterChainInitialCapacity> iter_;
  // If iter_[level] holds non-nullptr, then "index_block_handle_[level-1]" holds the
  // handle passed to state_->NewSecondaryIterator to create iter_[level].
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
    BlockIter* iter, TwoLevelIteratorState* index_iterator_state, bool) {
  InternalIterator* top_level_iter = top_level_index_block_->NewIndexIterator(
      comparator_.get(), iter, /* total_order_seek = */ true);
  return new MultiLevelIterator(
      index_iterator_state, top_level_iter, num_levels_, top_level_iter != iter);
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
