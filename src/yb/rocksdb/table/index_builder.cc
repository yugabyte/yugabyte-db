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

#include "yb/rocksdb/table/index_builder.h"

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/port/likely.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/util/coding.h"

#include "yb/util/logging.h"

namespace rocksdb {

Result<bool> IndexBuilder::FlushNextBlock(
    IndexBlocks* index_blocks, const BlockHandle& last_partition_block_handle) {
  RETURN_NOT_OK(Finish(index_blocks));
  return true;
}

IndexBuilder* IndexBuilder::CreateIndexBuilder(
    IndexType type,
    const Comparator* comparator,
    const SliceTransform* prefix_extractor,
    const BlockBasedTableOptions& table_opt) {
  switch (type) {
    case IndexType::kBinarySearch: {
      return new ShortenedIndexBuilder(comparator,
                                       table_opt.index_block_restart_interval);
    }
    case IndexType::kHashSearch: {
      return new HashIndexBuilder(comparator, prefix_extractor,
                                  table_opt.index_block_restart_interval);
    }
    case IndexType::kMultiLevelBinarySearch: {
      return new MultiLevelIndexBuilder(comparator, table_opt);
    }
    default: {
      assert(!"Do not recognize the index type ");
      return nullptr;
    }
  }
  // impossible.
  assert(false);
  return nullptr;
}

void ShortenedIndexBuilder::AddIndexEntry(
    std::string* last_key_in_current_block,
    const Slice* first_key_in_next_block,
    const BlockHandle& block_handle) {
  std::string handle_encoding;
  block_handle.AppendEncodedTo(&handle_encoding);
  AddIndexEntry(
      last_key_in_current_block, first_key_in_next_block, handle_encoding, ShortenKeys::kTrue);
}

void ShortenedIndexBuilder::AddIndexEntry(
    std::string* last_key_in_current_block,
    const Slice* first_key_in_next_block,
    const std::string& block_handle_encoded,
    const ShortenKeys shorten_keys) {
  if (shorten_keys) {
    if (UNLIKELY(first_key_in_next_block == nullptr)) {
      comparator_->FindShortSuccessor(last_key_in_current_block);
    } else {
      comparator_->FindShortestSeparator(last_key_in_current_block, *first_key_in_next_block);
    }
  }

  index_block_builder_.Add(*last_key_in_current_block, block_handle_encoded);
}

Status ShortenedIndexBuilder::Finish(IndexBlocks* index_blocks) {
  index_blocks->index_block_contents = index_block_builder_.Finish();
  return Status::OK();
}

void HashIndexBuilder::AddIndexEntry(
    std::string* last_key_in_current_block,
    const Slice* first_key_in_next_block,
    const BlockHandle& block_handle) {
  ++current_restart_index_;
  primary_index_builder_.AddIndexEntry(
      last_key_in_current_block, first_key_in_next_block, block_handle);
}

void HashIndexBuilder::OnKeyAdded(const Slice& key) {
  auto key_prefix = hash_key_extractor_->Transform(key);
  bool is_first_entry = pending_block_num_ == 0;

  // Keys may share the prefix
  if (is_first_entry || pending_entry_prefix_ != key_prefix) {
    if (!is_first_entry) {
      FlushPendingPrefix();
    }

    // need a hard copy otherwise the underlying data changes all the time.
    // TODO(kailiu) ToString() is expensive. We may speed up can avoid data
    // copy.
    pending_entry_prefix_ = key_prefix.ToString();
    pending_block_num_ = 1;
    pending_entry_index_ = static_cast<uint32_t>(current_restart_index_);
  } else {
    // entry number increments when keys share the prefix reside in
    // different data blocks.
    auto last_restart_index = pending_entry_index_ + pending_block_num_ - 1;
    assert(last_restart_index <= current_restart_index_);
    if (last_restart_index != current_restart_index_) {
      ++pending_block_num_;
    }
  }
}

Status HashIndexBuilder::Finish(IndexBlocks* index_blocks) {
  FlushPendingPrefix();
  RETURN_NOT_OK(primary_index_builder_.Finish(index_blocks));
  index_blocks->meta_blocks.emplace(kHashIndexPrefixesBlock, prefix_block_);
  index_blocks->meta_blocks.emplace(kHashIndexPrefixesMetadataBlock, prefix_meta_block_);
  return Status::OK();
}

void HashIndexBuilder::FlushPendingPrefix() {
  prefix_block_.append(pending_entry_prefix_.data(), pending_entry_prefix_.size());
  PutVarint32(&prefix_meta_block_, static_cast<uint32_t>(pending_entry_prefix_.size()));
  PutVarint32(&prefix_meta_block_, pending_entry_index_);
  PutVarint32(&prefix_meta_block_, pending_block_num_);
}

MultiLevelIndexBuilder::MultiLevelIndexBuilder(
    const Comparator* comparator, const BlockBasedTableOptions& table_opt)
    : IndexBuilder(comparator),
      table_opt_(table_opt) {}

void MultiLevelIndexBuilder::EnsureCurrentLevelIndexBuilderCreated() {
  if (!current_level_index_block_builder_) {
    DCHECK(!flush_policy_);
    current_level_index_block_builder_.reset(
        new ShortenedIndexBuilder(comparator_, table_opt_.index_block_restart_interval));
    flush_policy_ = FlushBlockBySizePolicyFactory::NewFlushBlockPolicy(
        table_opt_.index_block_size, table_opt_.block_size_deviation,
        table_opt_.min_keys_per_index_block,
        current_level_index_block_builder_->GetIndexBlockBuilder());
  }
}

void MultiLevelIndexBuilder::AddIndexEntry(
    std::string* last_key_in_current_block, const Slice* first_key_in_next_block,
    const BlockHandle& block_handle, const ShortenKeys shorten_keys) {
  DCHECK(!current_level_block_.is_ready)
      << "Expected to first flush already complete index block";
  EnsureCurrentLevelIndexBuilderCreated();

  block_handle_encoding_.clear();
  block_handle.AppendEncodedTo(&block_handle_encoding_);

  current_level_index_block_builder_->AddIndexEntry(
      last_key_in_current_block, first_key_in_next_block, block_handle_encoding_, shorten_keys);

  if (flush_policy_->Update(*last_key_in_current_block, block_handle_encoding_)
      || first_key_in_next_block == nullptr) {
    current_level_block_.is_ready = true;
    yb::CopyToBuffer(*last_key_in_current_block, &current_level_block_.last_key);
    current_level_block_.has_next_block = first_key_in_next_block != nullptr;
    if (current_level_block_.has_next_block) {
      first_key_in_next_block->CopyToBuffer(&current_level_block_.next_block_first_key);
    }
  }
}

bool MultiLevelIndexBuilder::ShouldFlush() const {
  return current_level_block_.is_ready ||
         next_level_.block_to_add.IsReadyAndLast() ||
         (next_level_index_builder_ && next_level_index_builder_->ShouldFlush());
}

Status MultiLevelIndexBuilder::FlushCurrentLevel(IndexBlocks* index_blocks) {
  RETURN_NOT_OK(current_level_index_block_builder_->Finish(index_blocks));
  finished_index_block_builder_ = std::move(current_level_index_block_builder_);
  flush_policy_.reset();
  current_level_size_ += index_blocks->index_block_contents.size() + kBlockTrailerSize;
  return Status::OK();
}

void MultiLevelIndexBuilder::AddToNextLevelIfReady(
    IndexBlockInfo* block_info, const BlockHandle& block_handle) {
  DCHECK_ONLY_NOTNULL(block_info);
  if (block_info->is_ready) {
    DCHECK_ONLY_NOTNULL(next_level_index_builder_.get());
    // We don't need to shorten keys in next level index builder, since we already did that when
    // added this entry at previous levels.
    if (block_info->has_next_block) {
      Slice key_slice(block_info->next_block_first_key);
      next_level_index_builder_->AddIndexEntry(
          &block_info->last_key, &key_slice, block_handle, ShortenKeys::kFalse);
    } else {
      next_level_index_builder_->AddIndexEntry(
          &block_info->last_key, nullptr, block_handle, ShortenKeys::kFalse);
      DCHECK(next_level_index_builder_->ShouldFlush())
          << "Expected to request flushing of last block at next index level";
    }
    block_info->is_ready = false;
  }
}

Result<bool> MultiLevelIndexBuilder::FlushNextBlock(
    IndexBlocks* index_blocks, const BlockHandle& last_index_block_handle) {
  if (next_level_.just_flushed) {
    // We should only have current level index block to add for next level if we have just
    // flushed it, but not next level index block.
    if (next_level_.block_to_add.is_ready) {
      return STATUS(IllegalState, "MultiLevelIndexBuilder just flushed next level index block, but"
          " current level index block is ready to add for next level.");
    }
    next_level_.last_flushed_block_ = last_index_block_handle;
    next_level_.just_flushed = false;
  }
  if (flushing_indexes_) {
    // If ready - add last flushed current level index block to next level index.
    AddToNextLevelIfReady(&next_level_.block_to_add, last_index_block_handle);
    if (next_level_index_builder_ && next_level_index_builder_->ShouldFlush()) {
      // First flush next level index block for all previously flushed index blocks at this
      // level and only after that we will flush pending current level index block on subsequent
      // FlushNextBlock() call at this level.
      auto result = next_level_index_builder_->FlushNextBlock(
          index_blocks, next_level_.last_flushed_block_);
      RETURN_NOT_OK(result);
      next_level_.just_flushed = true;
      return result;
    }
  }
  flushing_indexes_ = true;
  if (current_level_block_.is_ready) {
    RETURN_NOT_OK(FlushCurrentLevel(index_blocks));
    if (!next_level_index_builder_ && current_level_block_.has_next_block) {
      // We only need next level index builder if we plan to have more than one index entry at next
      // level, i.e. if we have next block at current level. Otherwise current level will be the top
      // level with single index block.
      next_level_index_builder_ = std::make_unique<MultiLevelIndexBuilder>(
          comparator_, table_opt_);
    }
    if (next_level_index_builder_) {
      // Postpone adding index entry for just flushed block for next FlushNextBlock() call, since
      // we need to have block handle to add it to next level index.
      next_level_.block_to_add = std::move(current_level_block_);
    }
    current_level_block_.is_ready = false;
    return true;
  } else if (!last_index_block_handle.IsSet()) {
    // It means this is the first call of FlushNextBlock() and no keys have been added to index,
    // which can only happen when we call FlushNextBlock() unconditionally during finishing of empty
    // SST file, so we need to produce an empty index.
    EnsureCurrentLevelIndexBuilderCreated();
    RETURN_NOT_OK(FlushCurrentLevel(index_blocks));
    return true;
  } else {
    return false;
  }
}

size_t MultiLevelIndexBuilder::EstimatedSize() const {
  // We subtract kBlockTrailerSize at the top level, because it will be added by
  // BlockBasedTableBuilder.
  return current_level_size_ +
      (next_level_index_builder_ ? next_level_index_builder_->EstimatedSize() : -kBlockTrailerSize);
}

int MultiLevelIndexBuilder::NumLevels() const {
  return 1 + (next_level_index_builder_ ? next_level_index_builder_->NumLevels() : 0);
}

} // namespace rocksdb
