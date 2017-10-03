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

#ifndef KUDU_CFILE_INDEX_BTREE_H
#define KUDU_CFILE_INDEX_BTREE_H

#include <boost/ptr_container/ptr_vector.hpp>
#include <memory>

#include "kudu/cfile/block_handle.h"
#include "kudu/cfile/cfile.pb.h"
#include "kudu/cfile/index_block.h"
#include "kudu/gutil/macros.h"

namespace kudu {
namespace cfile {

using boost::ptr_vector;

class CFileReader;
class CFileWriter;

class IndexTreeBuilder {
 public:
  explicit IndexTreeBuilder(
    const WriterOptions *options,
    CFileWriter *writer);

  // Append the given key into the index.
  // The key is copied into the builder's internal
  // memory.
  Status Append(const Slice &key, const BlockPointer &block);
  Status Finish(BTreeInfoPB *info);
 private:
  IndexBlockBuilder *CreateBlockBuilder(bool is_leaf);
  Status Append(const Slice &key, const BlockPointer &block_ptr,
                size_t level);

  // Finish the current block at the given index level, and then
  // propagate by inserting this block into the next higher-up
  // level index.
  Status FinishBlockAndPropagate(size_t level);

  // Finish the current block at the given level, writing it
  // to the file. Return the location of the written block
  // in 'written'.
  Status FinishAndWriteBlock(size_t level, BlockPointer *written);

  const WriterOptions *options_;
  CFileWriter *writer_;

  ptr_vector<IndexBlockBuilder> idx_blocks_;

  DISALLOW_COPY_AND_ASSIGN(IndexTreeBuilder);
};

class IndexTreeIterator {
 public:
  explicit IndexTreeIterator(
      const CFileReader *reader,
      const BlockPointer &root_blockptr);

  Status SeekToFirst();
  Status SeekAtOrBefore(const Slice &search_key);
  bool HasNext();
  Status Next();

  // The slice key at which the iterator
  // is currently seeked to.
  const Slice GetCurrentKey() const;
  const BlockPointer &GetCurrentBlockPointer() const;

  static IndexTreeIterator *Create(
    const CFileReader *reader,
    const BlockPointer &idx_root);

 private:
  IndexBlockIterator *BottomIter();
  IndexBlockReader *BottomReader();
  IndexBlockIterator *seeked_iter(int depth);
  IndexBlockReader *seeked_reader(int depth);
  Status LoadBlock(const BlockPointer &block, int dept);
  Status SeekDownward(const Slice &search_key, const BlockPointer &in_block,
                      int cur_depth);
  Status SeekToFirstDownward(const BlockPointer &in_block, int cur_depth);

  struct SeekedIndex {
    SeekedIndex() :
      iter(&reader)
    {}

    // Hold a copy of the underlying block data, which would
    // otherwise go out of scope. The reader and iter
    // do not themselves retain the data.
    BlockPointer block_ptr;
    BlockHandle data;
    IndexBlockReader reader;
    IndexBlockIterator iter;
  };

  const CFileReader *reader_;

  BlockPointer root_block_;

  ptr_vector<SeekedIndex> seeked_indexes_;

  DISALLOW_COPY_AND_ASSIGN(IndexTreeIterator);
};

} // namespace cfile
} // namespace kudu
#endif
