// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <vector>

#include "kudu/cfile/block_cache.h"
#include "kudu/cfile/cfile_reader.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/cfile/index_btree.h"
#include "kudu/common/key_encoder.h"
#include "kudu/util/debug-util.h"

namespace kudu {
namespace cfile {

IndexTreeBuilder::IndexTreeBuilder(
  const WriterOptions *options,
  CFileWriter *writer) :
  options_(options),
  writer_(writer) {
  idx_blocks_.push_back(CreateBlockBuilder(true));
}


IndexBlockBuilder *IndexTreeBuilder::CreateBlockBuilder(bool is_leaf) {
  return new IndexBlockBuilder(options_, is_leaf);
}

Status IndexTreeBuilder::Append(const Slice &key,
                                const BlockPointer &block) {
  return Append(key, block, 0);
}

Status IndexTreeBuilder::Append(
  const Slice &key, const BlockPointer &block_ptr,
  size_t level) {
  if (level >= idx_blocks_.size()) {
    // Need to create a new level
    CHECK(level == idx_blocks_.size()) <<
      "trying to create level " << level << " but size is only "
                                << idx_blocks_.size();
    VLOG(1) << "Creating level-" << level << " in index b-tree";
    idx_blocks_.push_back(CreateBlockBuilder(false));
  }

  IndexBlockBuilder &idx_block = idx_blocks_[level];
  idx_block.Add(key, block_ptr);

  size_t est_size = idx_block.EstimateEncodedSize();
  if (est_size > options_->index_block_size) {
    DCHECK(idx_block.Count() > 1)
      << "Index block full with only one entry - this would create "
      << "an infinite loop";
    // This index block is full, flush it.
    BlockPointer index_block_ptr;
    RETURN_NOT_OK(FinishBlockAndPropagate(level));
  }

  return Status::OK();
}


Status IndexTreeBuilder::Finish(BTreeInfoPB *info) {
  // Now do the same for the positional index blocks, starting
  // with leaf
  VLOG(1) << "flushing tree, b-tree has " <<
    idx_blocks_.size() << " levels";

  // Flush all but the root of the index.
  for (size_t i = 0; i < idx_blocks_.size() - 1; i++) {
    RETURN_NOT_OK(FinishBlockAndPropagate(i));
  }

  // Flush the root
  int root_level = idx_blocks_.size() - 1;
  BlockPointer ptr;
  Status s = FinishAndWriteBlock(root_level, &ptr);
  if (!s.ok()) {
    LOG(ERROR) << "Unable to flush root index block";
    return s;
  }

  VLOG(1) << "Flushed root index block: " << ptr.ToString();

  ptr.CopyToPB(info->mutable_root_block());
  return Status::OK();
}

Status IndexTreeBuilder::FinishBlockAndPropagate(size_t level) {
  IndexBlockBuilder &idx_block = idx_blocks_[level];

  // If the block doesn't have any data in it, we don't need to
  // write it out.
  // This happens if a lower-level block fills up exactly,
  // and then the file completes.
  //
  // TODO: add a test case which exercises this explicitly.
  if (idx_block.Count() == 0) {
    return Status::OK();
  }

  // Write to file.
  BlockPointer idx_block_ptr;
  RETURN_NOT_OK(FinishAndWriteBlock(level, &idx_block_ptr));

  // Get the first key of the finished block.
  Slice first_in_idx_block;
  Status s = idx_block.GetFirstKey(&first_in_idx_block);

  if (!s.ok()) {
    LOG(ERROR) << "Unable to get first key of level-" << level
               << " index block: " << s.ToString() << std::endl
               << GetStackTrace();
    return s;
  }

  // Add to higher-level index.
  RETURN_NOT_OK(Append(first_in_idx_block, idx_block_ptr,
                       level + 1));

  // Finally, reset the block we just wrote. It's important to wait until
  // here to do this, since the first_in_idx_block data may point to internal
  // storage of the index block.
  idx_block.Reset();

  return Status::OK();
}

// Finish the current block at the given level, writing it
// to the file. Return the location of the written block
// in 'written'.
Status IndexTreeBuilder::FinishAndWriteBlock(size_t level, BlockPointer *written) {
  IndexBlockBuilder &idx_block = idx_blocks_[level];
  Slice data = idx_block.Finish();

  vector<Slice> v;
  v.push_back(data);
  Status s = writer_->AddBlock(v, written, "index block");
  if (!s.ok()) {
    LOG(ERROR) << "Unable to append level-" << level << " index "
               << "block to file";
    return s;
  }

  return Status::OK();
}

////////////////////////////////////////////////////////////


IndexTreeIterator::IndexTreeIterator(const CFileReader *reader,
                                              const BlockPointer &root_blockptr)
    : reader_(reader),
      root_block_(root_blockptr) {
}

Status IndexTreeIterator::SeekAtOrBefore(const Slice &search_key) {
  return SeekDownward(search_key, root_block_, 0);
}

Status IndexTreeIterator::SeekToFirst() {
  return SeekToFirstDownward(root_block_, 0);
}

bool IndexTreeIterator::HasNext() {
  for (int i = seeked_indexes_.size() - 1; i >= 0; i--) {
    if (seeked_indexes_[i].iter.HasNext())
      return true;
  }
  return false;
}

Status IndexTreeIterator::Next() {
  CHECK(!seeked_indexes_.empty()) << "not seeked";

  // Start at the bottom level of the BTree, calling Next(),
  // until one succeeds. If any does not succeed, then
  // that block is exhausted, and gets removed.
  while (!seeked_indexes_.empty()) {
    Status s = BottomIter()->Next();
    if (s.IsNotFound()) {
      seeked_indexes_.pop_back();
    } else if (s.ok()) {
      break;
    } else {
      // error
      return s;
    }
  }

  // If we're now empty, then the root block was exhausted,
  // so we're entirely out of data.
  if (seeked_indexes_.empty()) {
    return Status::NotFound("end of iterator");
  }

  // Otherwise, the last layer points to the valid
  // next block. Propagate downward if it is not a leaf.
  while (!BottomReader()->IsLeaf()) {
    RETURN_NOT_OK(
        LoadBlock(BottomIter()->GetCurrentBlockPointer(),
                  seeked_indexes_.size()));
    RETURN_NOT_OK(BottomIter()->SeekToIndex(0));
  }

  return Status::OK();
}

const Slice IndexTreeIterator::GetCurrentKey() const {
  return seeked_indexes_.back().iter.GetCurrentKey();
}

const BlockPointer &IndexTreeIterator::GetCurrentBlockPointer() const {
  return seeked_indexes_.back().iter.GetCurrentBlockPointer();
}

IndexBlockIterator *IndexTreeIterator::BottomIter() {
  return &seeked_indexes_.back().iter;
}

IndexBlockReader *IndexTreeIterator::BottomReader() {
  return &seeked_indexes_.back().reader;
}

IndexBlockIterator *IndexTreeIterator::seeked_iter(int depth) {
  return &seeked_indexes_[depth].iter;
}

IndexBlockReader *IndexTreeIterator::seeked_reader(int depth) {
  return &seeked_indexes_[depth].reader;
}

Status IndexTreeIterator::LoadBlock(const BlockPointer &block, int depth) {

  SeekedIndex *seeked;
  if (depth < seeked_indexes_.size()) {
    // We have a cached instance from previous seek.
    seeked = &seeked_indexes_[depth];

    if (seeked->block_ptr.offset() == block.offset()) {
      // We're already seeked to this block - no need to re-parse it.
      // This is handy on the root block as well as for the case
      // when a lot of requests are traversing down the same part of
      // the tree.
      return Status::OK();
    }

    // Seeked to a different block: reset the reader
    seeked->reader.Reset();
    seeked->iter.Reset();
  } else {
    // No cached instance, make a new one.
    seeked_indexes_.push_back(new SeekedIndex());
    seeked = &seeked_indexes_.back();
  }

  RETURN_NOT_OK(reader_->ReadBlock(block, CFileReader::CACHE_BLOCK, &seeked->data));
  seeked->block_ptr = block;

  // Parse the new block.
  RETURN_NOT_OK(seeked->reader.Parse(seeked->data.data()));

  return Status::OK();
}

Status IndexTreeIterator::SeekDownward(const Slice &search_key, const BlockPointer &in_block,
                    int cur_depth) {

  // Read the block.
  RETURN_NOT_OK(LoadBlock(in_block, cur_depth));
  IndexBlockIterator *iter = seeked_iter(cur_depth);

  RETURN_NOT_OK(iter->SeekAtOrBefore(search_key));

  // If the block is a leaf block, we're done,
  // otherwise recurse downward into next layer
  // of B-Tree
  if (seeked_reader(cur_depth)->IsLeaf()) {
    seeked_indexes_.resize(cur_depth + 1);
    return Status::OK();
  } else {
    return SeekDownward(search_key, iter->GetCurrentBlockPointer(),
                        cur_depth + 1);
  }
}

Status IndexTreeIterator::SeekToFirstDownward(const BlockPointer &in_block, int cur_depth) {
  // Read the block.
  RETURN_NOT_OK(LoadBlock(in_block, cur_depth));
  IndexBlockIterator *iter = seeked_iter(cur_depth);

  RETURN_NOT_OK(iter->SeekToIndex(0));

  // If the block is a leaf block, we're done,
  // otherwise recurse downward into next layer
  // of B-Tree
  if (seeked_reader(cur_depth)->IsLeaf()) {
    seeked_indexes_.resize(cur_depth + 1);
    return Status::OK();
  } else {
    return SeekToFirstDownward(iter->GetCurrentBlockPointer(), cur_depth + 1);
  }
}

IndexTreeIterator *IndexTreeIterator::IndexTreeIterator::Create(
    const CFileReader *reader,
    const BlockPointer &root_blockptr) {
  return new IndexTreeIterator(reader, root_blockptr);
}


} // namespace cfile
} // namespace kudu
