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
#ifndef KUDU_TABLET_TABLET_METADATA_H
#define KUDU_TABLET_TABLET_METADATA_H

#include <boost/optional/optional_fwd.hpp>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "kudu/common/partition.h"
#include "kudu/common/schema.h"
#include "kudu/consensus/opid.pb.h"
#include "kudu/fs/block_id.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/callback.h"
#include "kudu/gutil/dynamic_annotations.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/tablet/metadata.pb.h"
#include "kudu/util/mutex.h"
#include "kudu/util/status.h"
#include "kudu/util/status_callback.h"

namespace kudu {
namespace tablet {

class RowSetMetadata;
class RowSetMetadataUpdate;

typedef std::vector<std::shared_ptr<RowSetMetadata> > RowSetMetadataVector;
typedef std::unordered_set<int64_t> RowSetMetadataIds;

extern const int64 kNoDurableMemStore;

// Manages the "blocks tracking" for the specified tablet.
//
// TabletMetadata is owned by the Tablet. As new blocks are written to store
// the tablet's data, the Tablet calls Flush() to persist the block list
// on disk.
//
// At startup, the TSTabletManager will load a TabletMetadata for each
// super block found in the tablets/ directory, and then instantiate
// tablets from this data.
class TabletMetadata : public RefCountedThreadSafe<TabletMetadata> {
 public:
  // Create metadata for a new tablet. This assumes that the given superblock
  // has not been written before, and writes out the initial superblock with
  // the provided parameters.
  static Status CreateNew(FsManager* fs_manager,
                          const std::string& tablet_id,
                          const std::string& table_name,
                          const Schema& schema,
                          const PartitionSchema& partition_schema,
                          const Partition& partition,
                          const TabletDataState& initial_tablet_data_state,
                          scoped_refptr<TabletMetadata>* metadata);

  // Load existing metadata from disk.
  static Status Load(FsManager* fs_manager,
                     const std::string& tablet_id,
                     scoped_refptr<TabletMetadata>* metadata);

  // Try to load an existing tablet. If it does not exist, create it.
  // If it already existed, verifies that the schema of the tablet matches the
  // provided 'schema'.
  //
  // This is mostly useful for tests which instantiate tablets directly.
  static Status LoadOrCreate(FsManager* fs_manager,
                             const std::string& tablet_id,
                             const std::string& table_name,
                             const Schema& schema,
                             const PartitionSchema& partition_schema,
                             const Partition& partition,
                             const TabletDataState& initial_tablet_data_state,
                             scoped_refptr<TabletMetadata>* metadata);

  static void CollectBlockIdPBs(const TabletSuperBlockPB& superblock,
                                std::vector<BlockIdPB>* block_ids);

  const std::string& tablet_id() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return tablet_id_;
  }

  // Returns the partition of the tablet.
  const Partition& partition() const {
    return partition_;
  }

  std::string table_id() const {
    DCHECK_NE(state_, kNotLoadedYet);
    return table_id_;
  }

  std::string table_name() const;

  uint32_t schema_version() const;

  void SetSchema(const Schema& schema, uint32_t version);

  void SetTableName(const std::string& table_name);

  // Return a reference to the current schema.
  // This pointer will be valid until the TabletMetadata is destructed,
  // even if the schema is changed.
  const Schema& schema() const {
    const Schema* s = reinterpret_cast<const Schema*>(
        base::subtle::Acquire_Load(reinterpret_cast<const AtomicWord*>(&schema_)));
    return *s;
  }

  // Returns the partition schema of the tablet's table.
  const PartitionSchema& partition_schema() const {
    return partition_schema_;
  }

  // Set / get the remote bootstrap / tablet data state.
  void set_tablet_data_state(TabletDataState state);
  TabletDataState tablet_data_state() const;

  // Increments flush pin count by one: if flush pin count > 0,
  // metadata will _not_ be flushed to disk during Flush().
  void PinFlush();

  // Decrements flush pin count by one: if flush pin count is zero,
  // metadata will be flushed to disk during the next call to Flush()
  // or -- if Flush() had been called after a call to PinFlush() but
  // before this method was called -- Flush() will be called inside
  // this method.
  Status UnPinFlush();

  Status Flush();

  // Updates the metadata in the following ways:
  // 1. Adds rowsets from 'to_add'.
  // 2. Removes rowsets from 'to_remove'.
  // 3. Adds orphaned blocks from 'to_remove'.
  // 4. Updates the last durable MRS ID from 'last_durable_mrs_id',
  //    assuming it's not kNoMrsFlushed.
  static const int64_t kNoMrsFlushed = -1;
  Status UpdateAndFlush(const RowSetMetadataIds& to_remove,
                        const RowSetMetadataVector& to_add,
                        int64_t last_durable_mrs_id);

  // Adds the blocks referenced by 'block_ids' to 'orphaned_blocks_'.
  //
  // This set will be written to the on-disk metadata in any subsequent
  // flushes.
  //
  // Blocks are removed from this set after they are successfully deleted
  // in a call to DeleteOrphanedBlocks().
  void AddOrphanedBlocks(const std::vector<BlockId>& block_ids);

  // Mark the superblock to be in state 'delete_type', sync it to disk, and
  // then delete all of the rowsets in this tablet.
  // The metadata (superblock) is not deleted. For that, call DeleteSuperBlock().
  //
  // 'delete_type' must be one of TABLET_DATA_DELETED or TABLET_DATA_TOMBSTONED.
  // 'last_logged_opid' should be set to the last opid in the log, if any is known.
  // If 'last_logged_opid' is not set, then the current value of
  // last_logged_opid is not modified. This is important for roll-forward of
  // partially-tombstoned tablets during crash recovery.
  //
  // Returns only once all data has been removed.
  Status DeleteTabletData(TabletDataState delete_type,
                          const boost::optional<consensus::OpId>& last_logged_opid);

  // Permanently deletes the superblock from the disk.
  // DeleteTabletData() must first be called and the tablet data state must be
  // TABLET_DATA_DELETED.
  // Returns Status::InvalidArgument if the list of orphaned blocks is not empty.
  // Returns Status::IllegalState if the tablet data state is not TABLET_DATA_DELETED.
  Status DeleteSuperBlock();

  // Create a new RowSetMetadata for this tablet.
  // Does not add the new rowset to the list of rowsets. Use one of the Update()
  // calls to do so.
  Status CreateRowSet(std::shared_ptr<RowSetMetadata> *rowset, const Schema& schema);

  const RowSetMetadataVector& rowsets() const { return rowsets_; }

  FsManager *fs_manager() const { return fs_manager_; }

  int64_t last_durable_mrs_id() const { return last_durable_mrs_id_; }

  void SetLastDurableMrsIdForTests(int64_t mrs_id) { last_durable_mrs_id_ = mrs_id; }

  void SetPreFlushCallback(StatusClosure callback) { pre_flush_callback_ = callback; }

  consensus::OpId tombstone_last_logged_opid() const { return tombstone_last_logged_opid_; }

  // Loads the currently-flushed superblock from disk into the given protobuf.
  Status ReadSuperBlockFromDisk(TabletSuperBlockPB* superblock) const;

  // Sets *super_block to the serialized form of the current metadata.
  Status ToSuperBlock(TabletSuperBlockPB* super_block) const;

  // Fully replace a superblock (used for bootstrap).
  Status ReplaceSuperBlock(const TabletSuperBlockPB &pb);

  // ==========================================================================
  // Stuff used by the tests
  // ==========================================================================
  const RowSetMetadata *GetRowSetForTests(int64_t id) const;

  RowSetMetadata *GetRowSetForTests(int64_t id);

 private:
  friend class RefCountedThreadSafe<TabletMetadata>;
  friend class MetadataTest;

  // Compile time assert that no one deletes TabletMetadata objects.
  ~TabletMetadata();

  // Constructor for creating a new tablet.
  //
  // TODO: get rid of this many-arg constructor in favor of just passing in a
  // SuperBlock, which already contains all of these fields.
  TabletMetadata(FsManager* fs_manager, std::string tablet_id,
                 std::string table_name, const Schema& schema,
                 PartitionSchema partition_schema, Partition partition,
                 const TabletDataState& tablet_data_state);

  // Constructor for loading an existing tablet.
  TabletMetadata(FsManager* fs_manager, std::string tablet_id);

  void SetSchemaUnlocked(gscoped_ptr<Schema> schema, uint32_t version);

  Status LoadFromDisk();

  // Update state of metadata to that of the given superblock PB.
  Status LoadFromSuperBlock(const TabletSuperBlockPB& superblock);

  Status ReadSuperBlock(TabletSuperBlockPB *pb);

  // Fully replace superblock.
  // Requires 'flush_lock_'.
  Status ReplaceSuperBlockUnlocked(const TabletSuperBlockPB &pb);

  // Requires 'data_lock_'.
  Status UpdateUnlocked(const RowSetMetadataIds& to_remove,
                        const RowSetMetadataVector& to_add,
                        int64_t last_durable_mrs_id);

  // Requires 'data_lock_'.
  Status ToSuperBlockUnlocked(TabletSuperBlockPB* super_block,
                              const RowSetMetadataVector& rowsets) const;

  // Requires 'data_lock_'.
  void AddOrphanedBlocksUnlocked(const std::vector<BlockId>& block_ids);

  // Deletes the provided 'blocks' on disk.
  //
  // All blocks that are successfully deleted are removed from the
  // 'orphaned_blocks_' set.
  //
  // Failures are logged, but are not fatal.
  void DeleteOrphanedBlocks(const std::vector<BlockId>& blocks);

  // Return standard "T xxx P yyy" log prefix.
  std::string LogPrefix() const;

  enum State {
    kNotLoadedYet,
    kNotWrittenYet,
    kInitialized
  };
  State state_;

  // Lock protecting the underlying data.
  typedef simple_spinlock LockType;
  mutable LockType data_lock_;

  // Lock protecting flushing the data to disk.
  // If taken together with 'data_lock_', must be acquired first.
  mutable Mutex flush_lock_;

  const std::string tablet_id_;
  std::string table_id_;

  Partition partition_;

  FsManager* const fs_manager_;
  RowSetMetadataVector rowsets_;

  base::subtle::Atomic64 next_rowset_idx_;

  int64_t last_durable_mrs_id_;

  // The current schema version. This is owned by this class.
  // We don't use gscoped_ptr so that we can do an atomic swap.
  Schema* schema_;
  uint32_t schema_version_;
  std::string table_name_;
  PartitionSchema partition_schema_;

  // Previous values of 'schema_'.
  // These are currently kept alive forever, under the assumption that
  // a given tablet won't have thousands of "alter table" calls.
  // They are kept alive so that callers of schema() don't need to
  // worry about reference counting or locking.
  std::vector<Schema*> old_schemas_;

  // Protected by 'data_lock_'.
  std::unordered_set<BlockId, BlockIdHash, BlockIdEqual> orphaned_blocks_;

  // The current state of remote bootstrap for the tablet.
  TabletDataState tablet_data_state_;

  // Record of the last opid logged by the tablet before it was last
  // tombstoned. Has no meaning for non-tombstoned tablets.
  consensus::OpId tombstone_last_logged_opid_;

  // If this counter is > 0 then Flush() will not write any data to
  // disk.
  int32_t num_flush_pins_;

  // Set if Flush() is called when num_flush_pins_ is > 0; if true,
  // then next UnPinFlush will call Flush() again to ensure the
  // metadata is persisted.
  bool needs_flush_;

  // A callback that, if set, is called before this metadata is flushed
  // to disk.
  StatusClosure pre_flush_callback_;

  DISALLOW_COPY_AND_ASSIGN(TabletMetadata);
};

} // namespace tablet
} // namespace kudu

#endif /* KUDU_TABLET_TABLET_METADATA_H */
