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
#include "yb/rocksdb/table/meta_blocks.h"

namespace rocksdb {

Status BinarySearchIndexReader::Create(
    RandomAccessFileReader* file, const Footer& footer,
    const BlockHandle& index_handle, Env* env,
    const Comparator* comparator,
    std::unique_ptr<IndexReader>* index_reader) {
  std::unique_ptr<Block> index_block;
  auto s = block_based_table::ReadBlockFromFile(
      file, footer, ReadOptions::kDefault, index_handle, &index_block, env);

  if (s.ok()) {
    index_reader->reset(new BinarySearchIndexReader(comparator, std::move(index_block)));
  }

  return s;
}

Status HashIndexReader::Create(const SliceTransform* hash_key_extractor,
                       const Footer& footer, RandomAccessFileReader* file,
                       Env* env, const Comparator* comparator,
                       const BlockHandle& index_handle,
                       InternalIterator* meta_index_iter,
                       std::unique_ptr<IndexReader>* index_reader,
                       bool hash_index_allow_collision) {
  std::unique_ptr<Block> index_block;
  auto s = block_based_table::ReadBlockFromFile(file, footer, ReadOptions::kDefault, index_handle,
                             &index_block, env);

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
                        &prefixes_contents, env, true /* do decompression */);
  if (!s.ok()) {
    return s;
  }
  BlockContents prefixes_meta_contents;
  s = ReadBlockContents(file, footer, ReadOptions::kDefault, prefixes_meta_handle,
                        &prefixes_meta_contents, env,
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

} // namespace rocksdb
