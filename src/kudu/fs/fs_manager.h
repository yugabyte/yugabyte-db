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

#ifndef KUDU_FS_FS_MANAGER_H
#define KUDU_FS_FS_MANAGER_H

#include <gtest/gtest_prod.h>
#include <iosfwd>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/env.h"
#include "kudu/util/path_util.h"

DECLARE_bool(enable_data_block_fsync);

namespace google {
namespace protobuf {
class Message;
} // namespace protobuf
} // namespace google

namespace kudu {

class MemTracker;
class MetricEntity;

namespace fs {
class BlockManager;
class ReadableBlock;
class WritableBlock;
} // namespace fs

namespace itest {
class ExternalMiniClusterFsInspector;
}

class BlockId;
class InstanceMetadataPB;

struct FsManagerOpts {
  FsManagerOpts();
  ~FsManagerOpts();

  // The entity under which all metrics should be grouped. If NULL, metrics
  // will not be produced.
  //
  // Defaults to NULL.
  scoped_refptr<MetricEntity> metric_entity;

  // The memory tracker under which all new memory trackers will be parented.
  // If NULL, new memory trackers will be parented to the root tracker.
  std::shared_ptr<MemTracker> parent_mem_tracker;

  // The path where WALs will be stored. Cannot be empty.
  std::string wal_path;

  // The paths where data blocks will be stored. Cannot be empty.
  std::vector<std::string> data_paths;

  // Whether or not read-write operations should be allowed. Defaults to false.
  bool read_only;
};

// FsManager provides helpers to read data and metadata files,
// and it's responsible for abstracting the file-system layout.
//
// The user should not be aware of where files are placed,
// but instead should interact with the storage in terms of "open the block xyz"
// or "write a new schema metadata file for table kwz".
//
// The current layout is:
//    <kudu.root.dir>/data/
//    <kudu.root.dir>/data/<prefix-0>/<prefix-2>/<prefix-4>/<name>
class FsManager {
 public:
  static const char *kWalFileNamePrefix;
  static const char *kWalsRecoveryDirSuffix;

  // Only for unit tests.
  FsManager(Env* env, const std::string& root_path);

  FsManager(Env* env, const FsManagerOpts& opts);
  ~FsManager();

  // Initialize and load the basic filesystem metadata.
  // If the file system has not been initialized, returns NotFound.
  // In that case, CreateInitialFileSystemLayout may be used to initialize
  // the on-disk structures.
  Status Open();

  // Create the initial filesystem layout.
  //
  // Returns an error if the file system is already initialized.
  Status CreateInitialFileSystemLayout();

  void DumpFileSystemTree(std::ostream& out);

  // Return the UUID persisted in the local filesystem. If Open()
  // has not been called, this will crash.
  const std::string& uuid() const;

  // ==========================================================================
  //  Data read/write interfaces
  // ==========================================================================

  // Creates a new anonymous block.
  //
  // Block will be synced on close.
  Status CreateNewBlock(gscoped_ptr<fs::WritableBlock>* block);

  Status OpenBlock(const BlockId& block_id,
                   gscoped_ptr<fs::ReadableBlock>* block);

  Status DeleteBlock(const BlockId& block_id);

  bool BlockExists(const BlockId& block_id) const;

  // ==========================================================================
  //  on-disk path
  // ==========================================================================
  std::vector<std::string> GetDataRootDirs() const;

  std::string GetWalsRootDir() const {
    DCHECK(initted_);
    return JoinPathSegments(canonicalized_wal_fs_root_, kWalDirName);
  }

  std::string GetTabletWalDir(const std::string& tablet_id) const {
    return JoinPathSegments(GetWalsRootDir(), tablet_id);
  }

  std::string GetTabletWalRecoveryDir(const std::string& tablet_id) const;

  std::string GetWalSegmentFileName(const std::string& tablet_id,
                                    uint64_t sequence_number) const;

  // Return the directory where tablet superblocks should be stored.
  std::string GetTabletMetadataDir() const;

  // Return the path for a specific tablet's superblock.
  std::string GetTabletMetadataPath(const std::string& tablet_id) const;

  // List the tablet IDs in the metadata directory.
  Status ListTabletIds(std::vector<std::string>* tablet_ids);

  // Return the path where InstanceMetadataPB is stored.
  std::string GetInstanceMetadataPath(const std::string& root) const;

  // Return the directory where the consensus metadata is stored.
  std::string GetConsensusMetadataDir() const {
    DCHECK(initted_);
    return JoinPathSegments(canonicalized_metadata_fs_root_, kConsensusMetadataDirName);
  }

  // Return the path where ConsensusMetadataPB is stored.
  std::string GetConsensusMetadataPath(const std::string& tablet_id) const {
    return JoinPathSegments(GetConsensusMetadataDir(), tablet_id);
  }

  Env *env() { return env_; }

  bool read_only() const {
    return read_only_;
  }

  // ==========================================================================
  //  file-system helpers
  // ==========================================================================
  bool Exists(const std::string& path) const {
    return env_->FileExists(path);
  }

  Status ListDir(const std::string& path, std::vector<std::string> *objects) const {
    return env_->GetChildren(path, objects);
  }

  Status CreateDirIfMissing(const std::string& path, bool* created = NULL);

  fs::BlockManager* block_manager() {
    return block_manager_.get();
  }

 private:
  FRIEND_TEST(FsManagerTestBase, TestDuplicatePaths);
  friend class itest::ExternalMiniClusterFsInspector; // for access to directory names

  // Initializes, sanitizes, and canonicalizes the filesystem roots.
  Status Init();

  // Select and create an instance of the appropriate block manager.
  //
  // Does not actually perform any on-disk operations.
  void InitBlockManager();

  // Create a new InstanceMetadataPB.
  void CreateInstanceMetadata(InstanceMetadataPB* metadata);

  // Save a InstanceMetadataPB to the filesystem.
  // Does not mutate the current state of the fsmanager.
  Status WriteInstanceMetadata(const InstanceMetadataPB& metadata,
                               const std::string& root);

  // Checks if 'path' is an empty directory.
  //
  // Returns an error if it's not a directory. Otherwise, sets 'is_empty'
  // accordingly.
  Status IsDirectoryEmpty(const std::string& path, bool* is_empty);

  // ==========================================================================
  //  file-system helpers
  // ==========================================================================
  void DumpFileSystemTree(std::ostream& out,
                          const std::string& prefix,
                          const std::string& path,
                          const std::vector<std::string>& objects);

  static const char *kDataDirName;
  static const char *kTabletMetadataDirName;
  static const char *kWalDirName;
  static const char *kCorruptedSuffix;
  static const char *kInstanceMetadataFileName;
  static const char *kInstanceMetadataMagicNumber;
  static const char *kTabletSuperBlockMagicNumber;
  static const char *kConsensusMetadataDirName;

  Env *env_;

  // If false, operations that mutate on-disk state are prohibited.
  const bool read_only_;

  // These roots are the constructor input verbatim. None of them are used
  // as-is; they are first canonicalized during Init().
  const std::string wal_fs_root_;
  const std::vector<std::string> data_fs_roots_;

  scoped_refptr<MetricEntity> metric_entity_;

  std::shared_ptr<MemTracker> parent_mem_tracker_;

  // Canonicalized forms of 'wal_fs_root_ and 'data_fs_roots_'. Constructed
  // during Init().
  //
  // - The first data root is used as the metadata root.
  // - Common roots in the collections have been deduplicated.
  std::string canonicalized_wal_fs_root_;
  std::string canonicalized_metadata_fs_root_;
  std::set<std::string> canonicalized_data_fs_roots_;
  std::set<std::string> canonicalized_all_fs_roots_;

  gscoped_ptr<InstanceMetadataPB> metadata_;

  gscoped_ptr<fs::BlockManager> block_manager_;

  bool initted_;

  DISALLOW_COPY_AND_ASSIGN(FsManager);
};

} // namespace kudu

#endif
