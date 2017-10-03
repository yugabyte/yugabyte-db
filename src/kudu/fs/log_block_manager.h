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

#ifndef KUDU_FS_LOG_BLOCK_MANAGER_H
#define KUDU_FS_LOG_BLOCK_MANAGER_H

#include <deque>
#include <gtest/gtest_prod.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "kudu/fs/block_id.h"
#include "kudu/fs/block_manager.h"
#include "kudu/fs/fs.pb.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/atomic.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/oid_generator.h"
#include "kudu/util/random.h"

namespace kudu {
class Env;
class MetricEntity;
class ThreadPool;

namespace fs {
class PathInstanceMetadataFile;

namespace internal {
class LogBlock;
class LogBlockContainer;

struct LogBlockManagerMetrics;
} // namespace internal

// A log-backed (i.e. sequentially allocated file) block storage
// implementation.
//
// This is a block storage implementation that attempts to reduce the
// number of files used by clustering blocks into large files known
// henceforth as containers. A container begins empty and is written to
// sequentially, block by block. When a container becomes full, it is set
// aside and a new container is created.
//
// Implementation details
// ----------------------
//
// A container is comprised of two files, one for metadata and one for
// data. Both are written to sequentially. During a write, the block's data
// is written as-is to the data file. After the block has been
// synchronized, a small record is written to the metadata file containing
// the block's ID and its location within the data file.
//
// Block deletions are handled similarly. When a block is deleted, a record
// is written describing the deletion, orphaning the old block data. The
// orphaned data can be reclaimed instantaneously via hole punching, or
// later via garbage collection. The latter is used when hole punching is
// not supported on the filesystem, or on next boot if there's a crash
// after deletion but before hole punching. The metadata file itself is not
// compacted, as it is expected to remain quite small even after a great
// many create/delete cycles.
//
// Data and metadata operations are carefully ordered to ensure the
// correctness of the persistent representation at all times. During the
// writable block lifecycle (i.e. when a block is being created), data
// operations come before metadata operations. In the event that a metadata
// operation fails, the result is an orphaned block that is detected and
// pruned in the next garbage collection cycle. Conversely, metadata
// operations precede the data operations when deleting a block. In the
// worst case, a failure in the latter yields more garbage data that can be
// deleted in a GC.
//
// Care is taken to keep the in-memory representation of the block manager
// in synch with its persistent representation. To wit, a block is only
// made available in memory if _all_ on-disk operations (including any
// necessary synchronization calls) are successful.
//
// When a new block is created, a container is selected using a round-robin
// policy (i.e. the least recently used container). If no containers are
// available, a new one is created. Only when the block is fully written is
// the container returned to the pool of available containers.
//
// All log block manager metadata requests are served from memory. When an
// existing block manager is opened, all on-disk container metadata is
// parsed to build a single in-memory map describing the existence and
// locations of various blocks. Each entry in the map consumes ~64 bytes,
// putting the memory overhead at ~610 MB for 10 million blocks.
//
// New blocks are placed on a filesystem block boundary, and the size of
// hole punch requests is rounded up to the nearest filesystem block size.
// Taken together, this guarantees that hole punching can actually reclaim
// disk space (instead of just zeroing the block's bytes on disk).
//
// Design trade-offs
// -----------------
// In general, log-backed block storage is optimized for sustained reads
// and writes. The idea is that all blocks in a given container contain
// related data and are generally read at once, reducing seeks for
// sustained scans. This comes at a cost: the containers need to be garbage
// collected every now and then, though newer systems can take advantage of
// filesystem hole punching (as described above) to reclaim space.
//
// The on-disk container metadata design favors simplicity and contiguous
// access over space consumption and scalability to a very large number of
// blocks. To be more specific, the separation of metadata from data allows
// for high performance sustained reads at block manager open time at a
// manageability cost: a container is not a single file, and needs multiple
// open fds to be of use. Moreover, the log-structured nature of the
// metadata is simple and performant at open time.
//
// Likewise, the default container placement policy favors simplicity over
// performance. In the future, locality hints will ensure that blocks
// pertaining to similar data are colocated, improving scan performance.
//
// The choice to serve all metadata requests from memory favors simplicity
// over memory consumption. With a very large number of blocks, the
// in-memory map may balloon in size and some sort of "spilling" behavior
// may be beneficial.

// TODO
// ----
// - Implement garbage collection fallback for hole punching.
// - Implement locality hints so that specific containers can be used for
//   groups of blocks (i.e. an entire column).
// - Unlock containers on FlushDataAsync() so that the workflow in
//   BlockManagerTest::CloseManyBlocksTest can use just one container.
// - Implement failure recovery (i.e. metadata truncation and other
//   similarly recoverable errors).
// - Evaluate and implement a solution for data integrity (e.g. per-block
//   checksum).

// The log-backed block manager.
class LogBlockManager : public BlockManager {
 public:
  LogBlockManager(Env* env, const BlockManagerOptions& opts);

  virtual ~LogBlockManager();

  virtual Status Create() OVERRIDE;

  virtual Status Open() OVERRIDE;

  virtual Status CreateBlock(const CreateBlockOptions& opts,
                             gscoped_ptr<WritableBlock>* block) OVERRIDE;

  virtual Status CreateBlock(gscoped_ptr<WritableBlock>* block) OVERRIDE;

  virtual Status OpenBlock(const BlockId& block_id,
                           gscoped_ptr<ReadableBlock>* block) OVERRIDE;

  virtual Status DeleteBlock(const BlockId& block_id) OVERRIDE;

  virtual Status CloseBlocks(const std::vector<WritableBlock*>& blocks) OVERRIDE;

  // Return the number of blocks stored in the block manager.
  int64_t CountBlocksForTests() const;

 private:
  FRIEND_TEST(LogBlockManagerTest, TestReuseBlockIds);
  friend class internal::LogBlockContainer;

  // Simpler typedef for a block map which isn't tracked in the memory tracker.
  // Used during startup.
  typedef std::unordered_map<
      const BlockId,
      scoped_refptr<internal::LogBlock>,
      BlockIdHash,
      BlockIdEqual> UntrackedBlockMap;

  typedef MemTrackerAllocator<
      std::pair<const BlockId, scoped_refptr<internal::LogBlock> > > BlockAllocator;
  typedef std::unordered_map<
      const BlockId,
      scoped_refptr<internal::LogBlock>,
      BlockIdHash,
      BlockIdEqual,
      BlockAllocator> BlockMap;

  // Adds an as of yet unseen container to this block manager.
  void AddNewContainerUnlocked(internal::LogBlockContainer* container);

  // Returns the next container available for writing using a round-robin
  // selection policy, or null if no suitable container was found.
  //
  // After returning, the container is considered to be in use. When
  // writing is finished, call MakeContainerAvailable() to make it
  // available to other writers.
  internal::LogBlockContainer* GetAvailableContainer();

  // Indicate that this container is no longer in use and can be handed out
  // to other writers.
  void MakeContainerAvailable(internal::LogBlockContainer* container);
  void MakeContainerAvailableUnlocked(internal::LogBlockContainer* container);

  // Synchronizes a container's dirty metadata to disk, taking care not to
  // sync more than is necessary (using 'dirty_dirs_').
  Status SyncContainer(const internal::LogBlockContainer& container);

  // Attempts to claim 'block_id' for use in a new WritableBlock.
  //
  // Returns true if the given block ID was not in use (and marks it as in
  // use), false otherwise.
  bool TryUseBlockId(const BlockId& block_id);

  // Adds a LogBlock to in-memory data structures.
  //
  // Returns success if the LogBlock was successfully added, failure if it
  // was already present.
  bool AddLogBlock(internal::LogBlockContainer* container,
                   const BlockId& block_id,
                   int64_t offset,
                   int64_t length);

  // Unlocked variant of AddLogBlock() for an already-constructed LogBlock object.
  // Must hold 'lock_'.
  bool AddLogBlockUnlocked(const scoped_refptr<internal::LogBlock>& lb);

  // Removes a LogBlock from in-memory data structures.
  //
  // Returns the LogBlock if it was successfully removed, NULL if it was
  // already gone.
  scoped_refptr<internal::LogBlock> RemoveLogBlock(const BlockId& block_id);

  // Unlocked variant of RemoveLogBlock(); must hold 'lock_'.
  scoped_refptr<internal::LogBlock> RemoveLogBlockUnlocked(const BlockId& block_id);

  // Parse a block record, adding or removing it in 'block_map', and
  // accounting for it in the metadata for 'container'.
  void ProcessBlockRecord(const BlockRecordPB& record,
                          internal::LogBlockContainer* container,
                          UntrackedBlockMap* block_map);

  // Open a particular root path belonging to the block manager.
  //
  // Success or failure is set in 'result_status'. On success, also sets
  // 'result_metadata' with an allocated metadata file.
  void OpenRootPath(const std::string& root_path,
                    Status* result_status,
                    PathInstanceMetadataFile** result_metadata);

  // Test for hole punching support at 'path'.
  Status CheckHolePunch(const std::string& path);

  // Perform basic initialization.
  Status Init();

  ObjectIdGenerator* oid_generator() { return &oid_generator_; }

  Env* env() const { return env_; }

  const internal::LogBlockManagerMetrics* metrics() const { return metrics_.get(); }

  // Tracks memory consumption of any allocations numerous enough to be
  // interesting (e.g. LogBlocks).
  std::shared_ptr<MemTracker> mem_tracker_;

  // Protects the block map, container structures, and 'dirty_dirs'.
  mutable simple_spinlock lock_;

  // Maps block IDs to blocks that are now readable, either because they
  // already existed on disk when the block manager was opened, or because
  // they're WritableBlocks that were closed.
  BlockMap blocks_by_block_id_;

  // Contains block IDs for WritableBlocks that are still open for writing.
  // When a WritableBlock is closed, its ID is moved to blocks_by_block_id.
  //
  // Together with blocks_by_block_id's keys, used to prevent collisions
  // when creating new anonymous blocks.
  std::unordered_set<BlockId, BlockIdHash> open_block_ids_;

  // Holds (and owns) all containers loaded from disk.
  std::vector<internal::LogBlockContainer*> all_containers_;

  // Holds only those containers that are currently available for writing,
  // excluding containers that are either in use or full.
  //
  // Does not own the containers.
  std::deque<internal::LogBlockContainer*> available_containers_;

  // Tracks dirty container directories.
  //
  // Synced and cleared by SyncMetadata().
  std::unordered_set<std::string> dirty_dirs_;

  // For manipulating files.
  Env* env_;

  // If true, only read operations are allowed.
  const bool read_only_;

  // Filesystem paths where all block directories are found.
  const std::vector<std::string> root_paths_;

  // Index of 'root_paths_' for the next created block.
  AtomicInt<int32> root_paths_idx_;

  // Maps root paths to instance metadata files found in each root path.
  typedef std::unordered_map<std::string, PathInstanceMetadataFile*> InstanceMap;
  InstanceMap instances_by_root_path_;

  // Maps root paths to thread pools. Each pool runs at most one thread, and
  // so serves as a "work queue" for that particular disk.
  typedef std::unordered_map<std::string, ThreadPool*> ThreadPoolMap;
  ThreadPoolMap thread_pools_by_root_path_;

  // For generating container names.
  ObjectIdGenerator oid_generator_;

  // For generating block IDs.
  ThreadSafeRandom rand_;

  // Metrics for the block manager.
  //
  // May be null if instantiated without metrics.
  gscoped_ptr<internal::LogBlockManagerMetrics> metrics_;

  DISALLOW_COPY_AND_ASSIGN(LogBlockManager);
};

} // namespace fs
} // namespace kudu

#endif
