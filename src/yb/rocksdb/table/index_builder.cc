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
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/format.h"

namespace rocksdb {

IndexBuilder* IndexBuilder::CreateIndexBuilder(
    BlockBasedTableOptions::IndexType type,
    const Comparator* comparator,
    const SliceTransform* prefix_extractor,
    int index_block_restart_interval) {
  switch (type) {
    case BlockBasedTableOptions::kBinarySearch: {
      return new ShortenedIndexBuilder(comparator,
                                       index_block_restart_interval);
    }
    case BlockBasedTableOptions::kHashSearch: {
      return new HashIndexBuilder(comparator, prefix_extractor,
                                  index_block_restart_interval);
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
  if (first_key_in_next_block != nullptr) {
    comparator_->FindShortestSeparator(last_key_in_current_block, *first_key_in_next_block);
  } else {
    comparator_->FindShortSuccessor(last_key_in_current_block);
  }

  std::string handle_encoding;
  block_handle.EncodeTo(&handle_encoding);
  index_block_builder_.Add(*last_key_in_current_block, handle_encoding);
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
  index_blocks->meta_blocks.insert({kHashIndexPrefixesBlock, prefix_block_});
  index_blocks->meta_blocks.insert({kHashIndexPrefixesMetadataBlock, prefix_meta_block_});
  return Status::OK();
}

void HashIndexBuilder::FlushPendingPrefix() {
  prefix_block_.append(pending_entry_prefix_.data(), pending_entry_prefix_.size());
  PutVarint32(&prefix_meta_block_, static_cast<uint32_t>(pending_entry_prefix_.size()));
  PutVarint32(&prefix_meta_block_, pending_entry_index_);
  PutVarint32(&prefix_meta_block_, pending_block_num_);
}

} // namespace rocksdb
