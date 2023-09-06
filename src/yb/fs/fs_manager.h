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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#pragma once

#include <iosfwd>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "yb/util/flags.h"
#include <gtest/gtest_prod.h>

#include "yb/gutil/ref_counted.h"
#include "yb/util/env.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/strongly_typed_uuid.h"

DECLARE_bool(enable_data_block_fsync);

namespace google {
namespace protobuf {
class Message;
} // namespace protobuf
} // namespace google

namespace yb {

class MemTracker;
class MetricEntity;

namespace itest {
class ExternalMiniClusterFsInspector;
}

class InstanceMetadataPB;

YB_STRONGLY_TYPED_BOOL(ShouldDeleteLogs);
YB_STRONGLY_TYPED_UUID_DECL(UniverseUuid);


struct FsManagerOpts {
  FsManagerOpts();
  ~FsManagerOpts();

  FsManagerOpts(const FsManagerOpts&);
  FsManagerOpts& operator=(const FsManagerOpts&);

  // The aggregated registry associated with the server.
  MetricRegistry* metric_registry;

  // The memory tracker under which all new memory trackers will be parented.
  // If NULL, new memory trackers will be parented to the root tracker.
  std::shared_ptr<MemTracker> parent_mem_tracker;

  // The paths where WALs will be stored. Cannot be empty.
  std::vector<std::string> wal_paths;

  // The paths where data blocks will be stored. Cannot be empty.
  std::vector<std::string> data_paths;

  // Whether or not read-write operations should be allowed. Defaults to false.
  bool read_only;

  // This field is to be used as a path component for all the fs roots. For now, we expect it to be
  // either master or tserver.
  std::string server_type;
};

// FsManager provides helpers to read data and metadata files,
// and it's responsible for abstracting the file-system layout.
//
// The user should not be aware of where files are placed,
// but instead should interact with the storage in terms of "open the file xyz"
// or "write a new schema metadata file for table kwz".
//
// The current top-level dir layout is <yb.root.dir>/yb-data/<server>/. Subdirs under it are:
//     logs/
//     instance
//     wals/<table>/<tablet>
//     tablet-meta/<tablet>
//     data/rocksdb/<table>/<tablet>/
//     consensus-meta/<tablet>

class FsManager {
 public:
  static const char *kWalDirName;
  static const char *kWalFileNamePrefix;
  static const char *kWalsRecoveryDirSuffix;
  static const char *kRocksDBDirName;
  static const char *kDataDirName;

  // Only for unit tests.
  FsManager(Env* env, const std::string& root_path, const std::string& server_type);

  FsManager(Env* env, const FsManagerOpts& opts);
  ~FsManager();

  Status ReadAutoFlagsConfig(google::protobuf::Message* msg) EXCLUDES(auto_flag_mutex_);
  Status WriteAutoFlagsConfig(const google::protobuf::Message* msg) EXCLUDES(auto_flag_mutex_);

  // Initialize and load the basic filesystem metadata.
  // If the file system has not been initialized, returns NotFound.
  // In that case, CreateInitialFileSystemLayout may be used to initialize
  // the on-disk structures.
  Status CheckAndOpenFileSystemRoots();

  //
  // Returns an error if the file system is already initialized.
  Status CreateInitialFileSystemLayout(bool delete_fs_if_lock_found = false);

  // Deletes the yb-data directory contents for data/wal. "logs" subdirectory deletion is skipped
  // when 'also_delete_logs' is set to false.
  // Needed for a master shell process to be stoppable and restartable correctly in shell mode.
  Status DeleteFileSystemLayout(
      ShouldDeleteLogs also_delete_logs = ShouldDeleteLogs::kFalse);

  // Check if a lock file is present.
  bool HasAnyLockFiles();
  // Delete the lock files. Used once the caller deems fs creation was succcessful.
  Status DeleteLockFiles();

  void DumpFileSystemTree(std::ostream& out);

  // Return the UUID persisted in the local filesystem. If Open()
  // has not been called, this will crash.
  const std::string& uuid() const;

  bool initdb_done_set_after_sys_catalog_restore() const;

  // ==========================================================================
  //  on-disk path
  // ==========================================================================
  std::set<std::string> GetFsRootDirs() const;

  std::vector<std::string> GetDataRootDirs() const;

  std::vector<std::string> GetWalRootDirs() const;

  // Used for tests only. If GetWalRootDirs returns an empty vector, we will crash the process.
  std::string GetFirstTabletWalDirOrDie(const std::string& table_id,
                                        const std::string& tablet_id) const;

  static std::string GetTabletWalRecoveryDir(const std::string& tablet_wal_path);

  static std::string GetWalSegmentFileName(uint64_t sequence_number);

  static bool IsWalSegmentFileName(const std::string& file_name);

  static std::string GetWalSegmentFilePath(
      const std::string& wal_path, uint64_t sequence_number) {
    return JoinPathSegments(wal_path, GetWalSegmentFileName(sequence_number));
  }

  std::vector<std::string> GetRaftGroupMetadataDirs() const;
  // Return the directory where Raft group superblocks should be stored.
  static std::string GetRaftGroupMetadataDir(const std::string& data_dir);

  void SetTabletPathByDataPath(const std::string& tablet_id, const std::string& path);
  Result<std::string> GetTabletPath(const std::string& tablet_id) const;
  bool LookupTablet(const std::string& tablet_id);

  // Return the path for a specific Raft group's superblock.
  Result<std::string> GetRaftGroupMetadataPath(const std::string& tablet_id) const;

  // Read all RaftGroupMetadataDirs and fill tablet_id_to_path_.
  // Return the tablet IDs in the metadata directory.
  Result<std::vector<std::string>> ListTabletIds();

  Result<std::string> GetUniverseUuidFromTserverInstanceMetadata() const;

  Status SetUniverseUuidOnTserverInstanceMetadata(const UniverseUuid& universe_uuid);

  // Return the path where InstanceMetadataPB is stored.
  std::string GetInstanceMetadataPath(const std::string& root) const;

  // Return the path where the fs lock file is stored.
  std::string GetFsLockFilePath(const std::string& root) const;

  // Return the directory where the certs are stored.
  std::string GetDefaultRootDir() const;
  static std::string GetCertsDir(const std::string& root_dir);

  std::vector<std::string> GetConsensusMetadataDirs() const;
  // Return the directory where the consensus metadata is stored.
  static std::string GetConsensusMetadataDir(const std::string& data_dir);

  // Return the path where ConsensusMetadataPB is stored.
  Result<std::string> GetConsensusMetadataPath(const std::string& tablet_id) const;

  std::string GetAutoFlagsConfigPath() const EXCLUDES(auto_flag_mutex_);

  Env *env() { return env_; }

  void SetEncryptedEnv(std::unique_ptr<Env> encrypted_env) {
    encrypted_env_ = std::move(encrypted_env);
  }

  Env *encrypted_env() {
    return encrypted_env_ ? encrypted_env_.get() : env();
  }

  bool read_only() const {
    return read_only_;
  }

  bool has_faulty_drive() const {
    return has_faulty_drive_;
  }

  // ==========================================================================
  //  file-system helpers
  // ==========================================================================
  bool Exists(const std::string& path) const {
    return env_->FileExists(path);
  }

  Status ListDir(const std::string& path, std::vector<std::string> *objects) const;

  Result<std::vector<std::string>> ListDir(const std::string& path) const;

  Status CreateDirIfMissing(const std::string& path, bool* created = NULL);

  Status CreateDirIfMissingAndSync(const std::string& path, bool* created = NULL);

 private:
  FRIEND_TEST(FsManagerTestBase, TestDuplicatePaths);
  FRIEND_TEST(FsManagerTestBase, AutoFlagsTest);

  // Initializes, sanitizes, and canonicalizes the filesystem roots.
  Status Init();

  // Creates filesystem roots, writing new on-disk instances using 'metadata'.
  Status CreateFileSystemRoots(const InstanceMetadataPB& metadata,
                               bool create_lock = false);

  std::set<std::string> GetAncillaryDirs() const;

  // Create a new InstanceMetadataPB.
  void CreateInstanceMetadata(InstanceMetadataPB* metadata);

  // Save a InstanceMetadataPB to the filesystem.
  // Does not mutate the current state of the fsmanager.
  Status WriteInstanceMetadata(
      const InstanceMetadataPB& metadata,
      const std::string& path);

  // Checks if 'path' is an empty directory.
  //
  // Returns an error if it's not a directory. Otherwise, sets 'is_empty'
  // accordingly.
  Status IsDirectoryEmpty(const std::string& path, bool* is_empty);

  // Checks write to temporary file on root.
  Status CheckWrite(const std::string& path);

  void CreateAndSetFaultDriveMetric(const std::string& path);

  // ==========================================================================
  //  file-system helpers
  // ==========================================================================
  void DumpFileSystemTree(std::ostream& out,
                          const std::string& prefix,
                          const std::string& path,
                          const std::vector<std::string>& objects);

  Result<std::string> GetExistingInstanceMetadataPath() const;

  Env *env_;

  // Set on the TabletServer::Init path.
  std::unique_ptr<Env> encrypted_env_;

  // If false, operations that mutate on-disk state are prohibited.
  const bool read_only_;

  // These roots are the constructor input verbatim. None of them are used
  // as-is; they are first canonicalized during Init().
  const std::vector<std::string> wal_fs_roots_;
  const std::vector<std::string> data_fs_roots_;
  const std::string server_type_;

  MetricRegistry* metric_registry_;

  std::shared_ptr<MemTracker> parent_mem_tracker_;

  // Canonicalized forms of 'wal_fs_roots_ and 'data_fs_roots_'. Constructed
  // during Init().
  //
  // - The first data root is used as the metadata root.
  // - Common roots in the collections have been deduplicated.
  std::set<std::string> canonicalized_wal_fs_roots_;
  std::string canonicalized_default_fs_root_;
  std::set<std::string> canonicalized_data_fs_roots_;
  std::set<std::string> canonicalized_all_fs_roots_;

  std::unordered_map<std::string, std::string> tablet_id_to_path_ GUARDED_BY(data_mutex_);
  mutable std::mutex data_mutex_;
  mutable std::mutex auto_flag_mutex_;
  std::string auto_flags_config_path_ GUARDED_BY(auto_flag_mutex_);

  std::unique_ptr<InstanceMetadataPB> metadata_ GUARDED_BY(metadata_mutex_);
  mutable std::mutex metadata_mutex_;

  // Keep references to counters, counters without reference will be retired.
  std::vector<scoped_refptr<Counter>> counters_;

  bool initted_ = false;

  bool has_faulty_drive_ = false;

  DISALLOW_COPY_AND_ASSIGN(FsManager);
};

} // namespace yb
