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

#include "yb/fs/fs_manager.h"

#include <deque>
#include <iostream>
#include <map>
#include <unordered_set>

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <google/protobuf/message.h>

#include "yb/fs/block_id.h"
#include "yb/fs/file_block_manager.h"
#include "yb/fs/fs.pb.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/strip.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/strtoint.h"
#include "yb/gutil/walltime.h"
#include "yb/util/env_util.h"
#include "yb/util/flag_tags.h"
#include "yb/util/net/net_util.h"
#include "yb/util/oid_generator.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"

DEFINE_bool(enable_data_block_fsync, true,
            "Whether to enable fsync() of data blocks, metadata, and their parent directories. "
            "Disabling this flag may cause data loss in the event of a system crash.");
TAG_FLAG(enable_data_block_fsync, unsafe);

DEFINE_string(block_manager, "file", "Which block manager to use for storage. "
              "Only the file block manager is supported for non-Linux systems.");
TAG_FLAG(block_manager, advanced);

DECLARE_string(fs_data_dirs);

DEFINE_string(fs_wal_dirs, "",
              "Comma-separated list of directories for write-ahead logs. This is an optional "
              "argument. If this is not specified, fs_data_dirs is used for write-ahead logs "
              "also and that's a reasonable default for most use cases.");
TAG_FLAG(fs_wal_dirs, stable);

using google::protobuf::Message;
using yb::env_util::ScopedFileDeleter;
using yb::fs::BlockManagerOptions;
using yb::fs::CreateBlockOptions;
using yb::fs::FileBlockManager;
using yb::fs::ReadableBlock;
using yb::fs::WritableBlock;
using std::map;
using std::unordered_set;
using strings::Substitute;

namespace yb {

// ==========================================================================
//  FS Paths
// ==========================================================================
const char *FsManager::kWalDirName = "wals";
const char *FsManager::kWalFileNamePrefix = "wal";
const char *FsManager::kWalsRecoveryDirSuffix = ".recovery";
const char *FsManager::kRocksDBDirName = "rocksdb";
const char *FsManager::kTabletMetadataDirName = "tablet-meta";
const char *FsManager::kDataDirName = "data";
const char *FsManager::kCorruptedSuffix = ".corrupted";
const char *FsManager::kInstanceMetadataFileName = "instance";
const char *FsManager::kConsensusMetadataDirName = "consensus-meta";

static const char* const kTmpInfix = ".tmp";

FsManagerOpts::FsManagerOpts()
  : read_only(false) {
  if (FLAGS_fs_wal_dirs.empty() && !FLAGS_fs_data_dirs.empty()) {
    // It is sufficient if user sets the data dirs. By default we use the same
    // directories for WALs as well.
    FLAGS_fs_wal_dirs = FLAGS_fs_data_dirs;
  }
  wal_paths = strings::Split(FLAGS_fs_wal_dirs, ",", strings::SkipEmpty());
  data_paths = strings::Split(FLAGS_fs_data_dirs, ",", strings::SkipEmpty());
}

FsManagerOpts::~FsManagerOpts() {
}

FsManager::FsManager(Env* env, const string& root_path, const std::string& server_type)
  : env_(DCHECK_NOTNULL(env)),
    read_only_(false),
    wal_fs_roots_({ root_path }),
    data_fs_roots_({ root_path }),
    server_type_(server_type),
    metric_entity_(nullptr),
    initted_(false) {
}

FsManager::FsManager(Env* env,
                     const FsManagerOpts& opts)
  : env_(DCHECK_NOTNULL(env)),
    read_only_(opts.read_only),
    wal_fs_roots_(opts.wal_paths),
    data_fs_roots_(opts.data_paths),
    server_type_(opts.server_type),
    metric_entity_(opts.metric_entity),
    parent_mem_tracker_(opts.parent_mem_tracker),
    initted_(false) {
}

FsManager::~FsManager() {
}

Status FsManager::Init() {
  if (initted_) {
    return Status::OK();
  }

  // The wal root must be set.
  if (data_fs_roots_.empty()) {
    return STATUS(IOError, "List of data directories (fs_data_dirs) not provided");
  }

  // Deduplicate all of the roots.
  set<string> all_roots;
  for (const string& wal_fs_root : wal_fs_roots_) {
    all_roots.insert(wal_fs_root);
  }
  for (const string& data_fs_root : data_fs_roots_) {
    all_roots.insert(data_fs_root);
  }

  // Build a map of original root --> canonicalized root, sanitizing each
  // root a bit as we go.
  typedef map<string, string> RootMap;
  RootMap canonicalized_roots;
  for (const string& root : all_roots) {
    if (root.empty()) {
      return STATUS(IOError, "Empty string provided for filesystem root");
    }
    if (root[0] != '/') {
      return STATUS(IOError,
          Substitute("Relative path $0 provided for filesystem root", root));
    }
    {
      string root_copy = root;
      StripWhiteSpace(&root_copy);
      if (root != root_copy) {
        return STATUS(IOError,
                  Substitute("Filesystem root $0 contains illegal whitespace", root));
      }
    }

    // Strip the basename when canonicalizing, as it may not exist. The
    // dirname, however, must exist.
    string canonicalized;
    RETURN_NOT_OK(env_->Canonicalize(DirName(root), &canonicalized));
    canonicalized = JoinPathSegments(canonicalized, BaseName(root));
    InsertOrDie(&canonicalized_roots, root, canonicalized);
  }

  // All done, use the map to set the canonicalized state.
  for (const auto& wal_fs_root : wal_fs_roots_) {
    canonicalized_wal_fs_roots_.insert(FindOrDie(canonicalized_roots, wal_fs_root));
  }
  if (!data_fs_roots_.empty()) {
    canonicalized_metadata_fs_root_ = FindOrDie(canonicalized_roots, data_fs_roots_[0]);
    for (const string& data_fs_root : data_fs_roots_) {
      canonicalized_data_fs_roots_.insert(FindOrDie(canonicalized_roots, data_fs_root));
    }
  } else {
    LOG(FATAL) << "Data directories (fs_data_dirs) must be specified";
  }

  for (const RootMap::value_type& e : canonicalized_roots) {
    canonicalized_all_fs_roots_.insert(e.second);
  }

  if (VLOG_IS_ON(1)) {
    VLOG(1) << "WAL roots: " << canonicalized_wal_fs_roots_;
    VLOG(1) << "Metadata root: " << canonicalized_metadata_fs_root_;
    VLOG(1) << "Data roots: " << canonicalized_data_fs_roots_;
    VLOG(1) << "All roots: " << canonicalized_all_fs_roots_;
  }

  // With the data roots canonicalized, we can initialize the block manager.
  InitBlockManager();

  initted_ = true;
  return Status::OK();
}

void FsManager::InitBlockManager() {
  BlockManagerOptions opts;
  opts.metric_entity = metric_entity_;
  opts.parent_mem_tracker = parent_mem_tracker_;
  opts.root_paths = GetDataRootDirs();
  opts.read_only = read_only_;
  if (FLAGS_block_manager == "file") {
    block_manager_.reset(new FileBlockManager(env_, opts));
  } else {
    LOG(FATAL) << "Invalid block manager: " << FLAGS_block_manager;
  }
}

Status FsManager::Open() {
  RETURN_NOT_OK(Init());
  for (const string& root : canonicalized_all_fs_roots_) {
    gscoped_ptr<InstanceMetadataPB> pb(new InstanceMetadataPB);
    RETURN_NOT_OK(pb_util::ReadPBContainerFromPath(env_, GetInstanceMetadataPath(root), pb.get()));
    if (!metadata_) {
      metadata_.reset(pb.release());
    } else if (pb->uuid() != metadata_->uuid()) {
      return STATUS(Corruption, Substitute(
          "Mismatched UUIDs across filesystem roots: $0 vs. $1",
          metadata_->uuid(), pb->uuid()));
    }
  }

  RETURN_NOT_OK(block_manager_->Open());
  LOG(INFO) << "Opened local filesystem: " << JoinStrings(canonicalized_all_fs_roots_, ",")
            << std::endl << metadata_->DebugString();
  return Status::OK();
}

Status FsManager::CreateInitialFileSystemLayout() {
  CHECK(!read_only_);

  RETURN_NOT_OK(Init());

  // It's OK if a root already exists as long as there's nothing in it.
  for (const string& root : canonicalized_all_fs_roots_) {
    if (!env_->FileExists(GetServerTypeDataPath(root, server_type_))) {
      // We'll create the directory below.
      continue;
    }
    bool is_empty;
    RETURN_NOT_OK_PREPEND(IsDirectoryEmpty(GetServerTypeDataPath(root, server_type_), &is_empty),
                          "Unable to check if FSManager root is empty");
    if (!is_empty) {
      return STATUS(AlreadyPresent, "FSManager root is not empty", root);
    }
  }

  // All roots are either empty or non-existent. Create missing roots and all
  // subdirectories.
  //
  // In the event of failure, delete everything we created.
  deque<ScopedFileDeleter*> delete_on_failure;
  ElementDeleter d(&delete_on_failure);

  InstanceMetadataPB metadata;
  CreateInstanceMetadata(&metadata);
  unordered_set<string> to_sync;
  for (const string& root : canonicalized_all_fs_roots_) {
    bool created;
    std::string out_dir;
    RETURN_NOT_OK(SetupRootDir(env_, root, server_type_, &out_dir, &created));
    if (created) {
      delete_on_failure.push_front(new ScopedFileDeleter(env_, out_dir));
      to_sync.insert(DirName(out_dir));
    }
    const string instance_metadata_path = GetInstanceMetadataPath(root);
    RETURN_NOT_OK_PREPEND(WriteInstanceMetadata(metadata, instance_metadata_path),
                          "Unable to write instance metadata");
    delete_on_failure.push_front(new ScopedFileDeleter(env_, instance_metadata_path));
  }

  // Initialize ancillary directories.
  auto ancillary_dirs = GetWalRootDirs();
  ancillary_dirs.push_back(GetTabletMetadataDir());
  ancillary_dirs.push_back(GetConsensusMetadataDir());

  for (const string& dir : ancillary_dirs) {
    bool created;
    RETURN_NOT_OK_PREPEND(CreateDirIfMissing(dir, &created),
                          Substitute("Unable to create directory $0", dir));
    if (created) {
      delete_on_failure.push_front(new ScopedFileDeleter(env_, dir));
      to_sync.insert(DirName(dir));
    }
  }

  // Ensure newly created directories are synchronized to disk.
  if (FLAGS_enable_data_block_fsync) {
    for (const string& dir : to_sync) {
      RETURN_NOT_OK_PREPEND(env_->SyncDir(dir),
                            Substitute("Unable to synchronize directory $0", dir));
    }
  }

  // Create the block manager.
  RETURN_NOT_OK_PREPEND(block_manager_->Create(), "Unable to create block manager");

  // Create the RocksDB directory under each data directory.
  for (const string& data_root : GetDataRootDirs()) {
    const string dir = JoinPathSegments(data_root, kRocksDBDirName);
    bool created = false;
    RETURN_NOT_OK_PREPEND(CreateDirIfMissing(dir, &created),
                          Substitute("Unable to create directory $0", dir));
    if (created) {
      delete_on_failure.push_front(new ScopedFileDeleter(env_, dir));
      to_sync.insert(DirName(dir));
    }
  }

  // Success: don't delete any files.
  for (ScopedFileDeleter* deleter : delete_on_failure) {
    deleter->Cancel();
  }
  return Status::OK();
}

void FsManager::CreateInstanceMetadata(InstanceMetadataPB* metadata) {
  ObjectIdGenerator oid_generator;
  metadata->set_uuid(oid_generator.Next());

  string time_str;
  StringAppendStrftime(&time_str, "%Y-%m-%d %H:%M:%S", time(nullptr), false);
  string hostname;
  if (!GetHostname(&hostname).ok()) {
    hostname = "<unknown host>";
  }
  metadata->set_format_stamp(Substitute("Formatted at $0 on $1", time_str, hostname));
}

Status FsManager::WriteInstanceMetadata(const InstanceMetadataPB& metadata,
                                        const string& path) {
  // The instance metadata is written effectively once per TS, so the
  // durability cost is negligible.
  RETURN_NOT_OK(pb_util::WritePBContainerToPath(env_, path,
                                                metadata,
                                                pb_util::NO_OVERWRITE,
                                                pb_util::SYNC));
  LOG(INFO) << "Generated new instance metadata in path " << path << ":\n"
            << metadata.DebugString();
  return Status::OK();
}

Status FsManager::IsDirectoryEmpty(const string& path, bool* is_empty) {
  vector<string> children;
  RETURN_NOT_OK(env_->GetChildren(path, &children));
  for (const string& child : children) {
    // Excluding logs directory from the list of things to check for.
    if (child == "." || child == ".." || child == "logs") {
      continue;
    } else {
      LOG(INFO) << "Found data " << child;
      *is_empty = false;
      return Status::OK();
    }
  }
  *is_empty = true;
  return Status::OK();
}

Status FsManager::CreateDirIfMissing(const string& path, bool* created) {
  return env_util::CreateDirIfMissing(env_, path, created);
}

Status FsManager::CreateDirIfMissingAndSync(const std::string& path, bool* created) {
  RETURN_NOT_OK_PREPEND(CreateDirIfMissing(path, created),
                        Substitute("Failed to create directory $0", path));
  RETURN_NOT_OK_PREPEND(env_->SyncDir(DirName(path)),
                        Substitute("Failed to sync root directory $0", DirName(path)));
  return Status::OK();
}

const string& FsManager::uuid() const {
  return CHECK_NOTNULL(metadata_.get())->uuid();
}

vector<string> FsManager::GetDataRootDirs() const {
  // Add the data subdirectory to each data root.
  vector<string> data_paths;
  for (const string& data_fs_root : canonicalized_data_fs_roots_) {
    data_paths.push_back(
        JoinPathSegments(GetServerTypeDataPath(data_fs_root, server_type_), kDataDirName));
  }
  return data_paths;
}

vector<string> FsManager::GetWalRootDirs() const {
  DCHECK(initted_);
  vector<string> wal_dirs;
  for (const auto& canonicalized_wal_fs_root : canonicalized_wal_fs_roots_) {
    wal_dirs.push_back(JoinPathSegments(
          GetServerTypeDataPath(canonicalized_wal_fs_root, server_type_), kWalDirName));
  }
  return wal_dirs;
}

string FsManager::GetTabletMetadataDir() const {
  DCHECK(initted_);
  return JoinPathSegments(
      GetServerTypeDataPath(canonicalized_metadata_fs_root_, server_type_), kTabletMetadataDirName);
}

string FsManager::GetTabletMetadataPath(const string& tablet_id) const {
  return JoinPathSegments(GetTabletMetadataDir(), tablet_id);
}

namespace {
// Return true if 'fname' is a valid tablet ID.
bool IsValidTabletId(const std::string& fname) {
  if (fname.find(kTmpInfix) != string::npos) {
    LOG(WARNING) << "Ignoring tmp file in tablet metadata dir: " << fname;
    return false;
  }

  if (HasPrefixString(fname, ".")) {
    // Hidden file or ./..
    VLOG(1) << "Ignoring hidden file in tablet metadata dir: " << fname;
    return false;
  }

  return true;
}
} // anonymous namespace

Status FsManager::ListTabletIds(vector<string>* tablet_ids) {
  string dir = GetTabletMetadataDir();
  vector<string> children;
  RETURN_NOT_OK_PREPEND(ListDir(dir, &children),
                        Substitute("Couldn't list tablets in metadata directory $0", dir));

  vector<string> tablets;
  for (const string& child : children) {
    if (!IsValidTabletId(child)) {
      continue;
    }
    tablet_ids->push_back(child);
  }
  return Status::OK();
}

std::string FsManager::GetInstanceMetadataPath(const string& root) const {
  return JoinPathSegments(GetServerTypeDataPath(root, server_type_), kInstanceMetadataFileName);
}

std::string FsManager::GetConsensusMetadataDir() const {
  DCHECK(initted_);
  return JoinPathSegments(
      GetServerTypeDataPath(canonicalized_metadata_fs_root_, server_type_),
      kConsensusMetadataDirName);
}

std::string FsManager::GetFirstTabletWalDirOrDie(const std::string& table_id,
                                                 const std::string& tablet_id) const {
  auto wal_root_dirs = GetWalRootDirs();
  CHECK(!wal_root_dirs.empty()) << "No WAL directories specified";
  auto table_wal_dir = JoinPathSegments(wal_root_dirs[0], Substitute("table-$0", table_id));
  return JoinPathSegments(table_wal_dir, Substitute("tablet-$0", tablet_id));
}

std::string FsManager::GetTabletWalRecoveryDir(const string& tablet_wal_path) const {
  return tablet_wal_path + kWalsRecoveryDirSuffix;
}

std::string FsManager::GetWalSegmentFileName(const string& tablet_wal_path,
                                             uint64_t sequence_number) const {
  return JoinPathSegments(tablet_wal_path,
                          strings::Substitute("$0-$1",
                                              kWalFileNamePrefix,
                                              StringPrintf("%09" PRIu64, sequence_number)));
}

// ==========================================================================
//  Dump/Debug utils
// ==========================================================================

void FsManager::DumpFileSystemTree(ostream& out) {
  DCHECK(initted_);

  for (const string& root : canonicalized_all_fs_roots_) {
    out << "File-System Root: " << root << std::endl;

    std::vector<string> objects;
    Status s = env_->GetChildren(root, &objects);
    if (!s.ok()) {
      LOG(ERROR) << "Unable to list the fs-tree: " << s.ToString();
      return;
    }

    DumpFileSystemTree(out, "|-", root, objects);
  }
}

void FsManager::DumpFileSystemTree(ostream& out, const string& prefix,
                                   const string& path, const vector<string>& objects) {
  for (const string& name : objects) {
    if (name == "." || name == "..") continue;

    std::vector<string> sub_objects;
    string sub_path = JoinPathSegments(path, name);
    Status s = env_->GetChildren(sub_path, &sub_objects);
    if (s.ok()) {
      out << prefix << name << "/" << std::endl;
      DumpFileSystemTree(out, prefix + "---", sub_path, sub_objects);
    } else {
      out << prefix << name << std::endl;
    }
  }
}

// ==========================================================================
//  Data read/write interfaces
// ==========================================================================

Status FsManager::CreateNewBlock(gscoped_ptr<WritableBlock>* block) {
  CHECK(!read_only_);

  return block_manager_->CreateBlock(block);
}

Status FsManager::OpenBlock(const BlockId& block_id, gscoped_ptr<ReadableBlock>* block) {
  return block_manager_->OpenBlock(block_id, block);
}

Status FsManager::DeleteBlock(const BlockId& block_id) {
  CHECK(!read_only_);

  return block_manager_->DeleteBlock(block_id);
}

bool FsManager::BlockExists(const BlockId& block_id) const {
  gscoped_ptr<ReadableBlock> block;
  return block_manager_->OpenBlock(block_id, &block).ok();
}

std::ostream& operator<<(std::ostream& o, const BlockId& block_id) {
  return o << block_id.ToString();
}

} // namespace yb
