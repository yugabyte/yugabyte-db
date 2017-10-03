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
#include "kudu/fs/block_manager_util.h"

#include <set>
#include <unordered_map>
#include <utility>

#include <gflags/gflags.h>

#include "kudu/fs/fs.pb.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/env.h"
#include "kudu/util/path_util.h"
#include "kudu/util/pb_util.h"

DECLARE_bool(enable_data_block_fsync);

namespace kudu {
namespace fs {

using std::set;
using std::string;
using std::unordered_map;
using std::vector;
using strings::Substitute;

PathInstanceMetadataFile::PathInstanceMetadataFile(Env* env,
                                                   string block_manager_type,
                                                   string filename)
    : env_(env),
      block_manager_type_(std::move(block_manager_type)),
      filename_(std::move(filename)) {}

PathInstanceMetadataFile::~PathInstanceMetadataFile() {
  if (lock_) {
    WARN_NOT_OK(Unlock(), Substitute("Failed to unlock file $0", filename_));
  }
}

Status PathInstanceMetadataFile::Create(const string& uuid, const vector<string>& all_uuids) {
  DCHECK(!lock_) <<
      "Creating a metadata file that's already locked would release the lock";
  DCHECK(ContainsKey(set<string>(all_uuids.begin(), all_uuids.end()), uuid));

  uint64_t block_size;
  RETURN_NOT_OK(env_->GetBlockSize(DirName(filename_), &block_size));

  PathInstanceMetadataPB new_instance;

  // Set up the path set.
  PathSetPB* new_path_set = new_instance.mutable_path_set();
  new_path_set->set_uuid(uuid);
  new_path_set->mutable_all_uuids()->Reserve(all_uuids.size());
  for (const string& u : all_uuids) {
    new_path_set->add_all_uuids(u);
  }

  // And the rest of the metadata.
  new_instance.set_block_manager_type(block_manager_type_);
  new_instance.set_filesystem_block_size_bytes(block_size);

  return pb_util::WritePBContainerToPath(
      env_, filename_, new_instance,
      pb_util::NO_OVERWRITE,
      FLAGS_enable_data_block_fsync ? pb_util::SYNC : pb_util::NO_SYNC);
}

Status PathInstanceMetadataFile::LoadFromDisk() {
  DCHECK(!lock_) <<
      "Opening a metadata file that's already locked would release the lock";

  gscoped_ptr<PathInstanceMetadataPB> pb(new PathInstanceMetadataPB());
  RETURN_NOT_OK(pb_util::ReadPBContainerFromPath(env_, filename_, pb.get()));

  if (pb->block_manager_type() != block_manager_type_) {
    return Status::IOError("Wrong block manager type", pb->block_manager_type());
  }

  uint64_t block_size;
  RETURN_NOT_OK(env_->GetBlockSize(filename_, &block_size));
  if (pb->filesystem_block_size_bytes() != block_size) {
    return Status::IOError("Wrong filesystem block size", Substitute(
        "Expected $0 but was $1", pb->filesystem_block_size_bytes(), block_size));
  }

  metadata_.swap(pb);
  return Status::OK();
}

Status PathInstanceMetadataFile::Lock() {
  DCHECK(!lock_);

  FileLock* lock;
  RETURN_NOT_OK_PREPEND(env_->LockFile(filename_, &lock),
                        Substitute("Could not lock $0", filename_));
  lock_.reset(lock);
  return Status::OK();
}

Status PathInstanceMetadataFile::Unlock() {
  DCHECK(lock_);

  RETURN_NOT_OK_PREPEND(env_->UnlockFile(lock_.release()),
                        Substitute("Could not unlock $0", filename_));
  return Status::OK();
}

Status PathInstanceMetadataFile::CheckIntegrity(
    const vector<PathInstanceMetadataFile*>& instances) {
  unordered_map<string, PathInstanceMetadataFile*> uuids;
  pair<string, PathInstanceMetadataFile*> first_all_uuids;

  for (PathInstanceMetadataFile* instance : instances) {
    const PathSetPB& path_set = instance->metadata()->path_set();

    // Check that this instance's UUID wasn't already claimed.
    PathInstanceMetadataFile** other =
        InsertOrReturnExisting(&uuids, path_set.uuid(), instance);
    if (other) {
      return Status::IOError(Substitute(
          "File $0 claimed uuid $1 already claimed by file $2",
          instance->filename_, path_set.uuid(), (*other)->filename_));
    }

    // Check that there are no duplicate UUIDs in all_uuids.
    set<string> deduplicated_uuids(path_set.all_uuids().begin(),
                                   path_set.all_uuids().end());
    string all_uuids_str = JoinStrings(path_set.all_uuids(), ",");
    if (deduplicated_uuids.size() != path_set.all_uuids_size()) {
      return Status::IOError(Substitute(
          "File $0 has duplicate uuids: $1",
          instance->filename_, all_uuids_str));
    }

    // Check that this instance's UUID is a member of all_uuids.
    if (!ContainsKey(deduplicated_uuids, path_set.uuid())) {
      return Status::IOError(Substitute(
          "File $0 claimed uuid $1 which is not in all_uuids ($2)",
          instance->filename_, path_set.uuid(), all_uuids_str));
    }

    // Check that every all_uuids is the same.
    if (first_all_uuids.first.empty()) {
      first_all_uuids.first = all_uuids_str;
      first_all_uuids.second = instance;
    } else if (first_all_uuids.first != all_uuids_str) {
      return Status::IOError(Substitute(
          "File $0 claimed all_uuids $1 but file $2 claimed all_uuids $3",
          instance->filename_, all_uuids_str,
          first_all_uuids.second->filename_, first_all_uuids.first));
    }
  }

  return Status::OK();
}

} // namespace fs
} // namespace kudu
