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

#ifndef KUDU_FS_FILE_BLOCK_MANAGER_H
#define KUDU_FS_FILE_BLOCK_MANAGER_H

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "kudu/fs/block_id.h"
#include "kudu/fs/block_manager.h"
#include "kudu/util/locks.h"
#include "kudu/util/random.h"

namespace kudu {

class Env;
class MemTracker;
class MetricEntity;
class WritableFile;

namespace fs {
class PathInstanceMetadataFile;

namespace internal {
class FileBlockLocation;
class FileReadableBlock;
class FileWritableBlock;

struct BlockManagerMetrics;
} // namespace internal

// A file-backed block storage implementation.
//
// This is a naive block implementation which maps each block to its own
// file on disk. To prevent the block directory from becoming too large,
// blocks are aggregated into a 3-level directory hierarchy.
//
// The block manager can take advantage of multiple filesystem paths. A block
// written to a given path will be assigned an ID that includes enough
// information to uniquely identify the path's underlying disk. The ID is
// resolved back into a filesystem path when the block is opened for reading.
// The structure of this ID limits the block manager to at most 65,536 disks.
//
// When creating blocks, the block manager will round robin through the
// available filesystem paths.
//
// TODO: Support path-based block placement hints.

// The file-backed block manager.
class FileBlockManager : public BlockManager {
 public:

  // Creates a new in-memory instance of a FileBlockManager.
  //
  // 'env' should remain alive for the lifetime of the block manager.
  FileBlockManager(Env* env, const BlockManagerOptions& opts);

  virtual ~FileBlockManager();

  virtual Status Create() OVERRIDE;

  virtual Status Open() OVERRIDE;

  virtual Status CreateBlock(const CreateBlockOptions& opts,
                             gscoped_ptr<WritableBlock>* block) OVERRIDE;

  virtual Status CreateBlock(gscoped_ptr<WritableBlock>* block) OVERRIDE;

  virtual Status OpenBlock(const BlockId& block_id,
                           gscoped_ptr<ReadableBlock>* block) OVERRIDE;

  virtual Status DeleteBlock(const BlockId& block_id) OVERRIDE;

  virtual Status CloseBlocks(const std::vector<WritableBlock*>& blocks) OVERRIDE;

 private:
  friend class internal::FileBlockLocation;
  friend class internal::FileReadableBlock;
  friend class internal::FileWritableBlock;

  // Synchronizes the metadata for a block with the given id.
  Status SyncMetadata(const internal::FileBlockLocation& block_id);

  // Looks up the path of the file backing a particular block ID.
  //
  // On success, overwrites 'path' with the file's path.
  bool FindBlockPath(const BlockId& block_id,
                     std::string* root_path) const;

  Env* env() const { return env_; }

  // For manipulating files.
  Env* env_;

  // If true, only read operations are allowed.
  const bool read_only_;

  // Filesystem paths where all block directories are found.
  const std::vector<std::string> root_paths_;

  // Maps path indices their instance files.
  //
  // There's no need to synchronize access to the map as it is only written
  // to during Create() and Open(); all subsequent accesses are reads.
  typedef std::map<uint16_t, PathInstanceMetadataFile*> PathMap;
  PathMap root_paths_by_idx_;

  // For generating block IDs.
  ThreadSafeRandom rand_;

  // Protects 'dirty_dirs_' and 'next_root_path_'.
  mutable simple_spinlock lock_;

  // Tracks the block directories which are dirty from block creation. This
  // lets us perform some simple coalescing when synchronizing metadata.
  std::unordered_set<std::string> dirty_dirs_;

  // Points to the filesystem path to be used when creating the next block.
  PathMap::iterator next_root_path_;

  // Metric container for the block manager.
  // May be null if instantiated without metrics.
  gscoped_ptr<internal::BlockManagerMetrics> metrics_;

  // Tracks memory consumption of any allocations numerous enough to be
  // interesting.
  std::shared_ptr<MemTracker> mem_tracker_;

  DISALLOW_COPY_AND_ASSIGN(FileBlockManager);
};

} // namespace fs
} // namespace kudu

#endif
