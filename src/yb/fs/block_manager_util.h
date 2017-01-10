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
#ifndef YB_FS_BLOCK_MANAGER_UTIL_H
#define YB_FS_BLOCK_MANAGER_UTIL_H

#include <string>
#include <vector>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/util/path_util.h"
#include "yb/util/status.h"

namespace yb {

class Env;
class FileLock;
class PathInstanceMetadataPB;

namespace fs {

// Reads and writes block manager instance metadata files.
//
// Thread-unsafe; access to this object must be externally synchronized.
class PathInstanceMetadataFile {
 public:
  // 'env' must remain valid for the lifetime of this class.
  PathInstanceMetadataFile(Env* env, std::string block_manager_type,
                           std::string filename);

  ~PathInstanceMetadataFile();

  // Creates, writes, synchronizes, and closes a new instance metadata file.
  //
  // 'uuid' is this instance's UUID, and 'all_uuids' is all of the UUIDs in
  // this instance's path set.
  CHECKED_STATUS Create(const std::string& uuid,
                const std::vector<std::string>& all_uuids);

  // Opens, reads, verifies, and closes an existing instance metadata file.
  //
  // On success, 'metadata_' is overwritten with the contents of the file.
  CHECKED_STATUS LoadFromDisk();

  // Locks the instance metadata file, which must exist on-disk. Returns an
  // error if it's already locked. The lock is released when Unlock() is
  // called, when this object is destroyed, or when the process exits.
  //
  // Note: the lock is also released if any fd of the instance metadata file
  // in this process is closed. Thus, it is an error to call Create() or
  // LoadFromDisk() on a locked file.
  CHECKED_STATUS Lock();

  // Unlocks the instance metadata file. Must have been locked to begin with.
  CHECKED_STATUS Unlock();

  void SetMetadataForTests(gscoped_ptr<PathInstanceMetadataPB> metadata) {
    metadata_ = metadata.Pass();
  }

  std::string path() const { return DirName(filename_); }
  PathInstanceMetadataPB* const metadata() const { return metadata_.get(); }

  // Check the integrity of the provided instances' path sets.
  static CHECKED_STATUS CheckIntegrity(const std::vector<PathInstanceMetadataFile*>& instances);

 private:
  Env* env_;
  const std::string block_manager_type_;
  const std::string filename_;
  gscoped_ptr<PathInstanceMetadataPB> metadata_;
  gscoped_ptr<FileLock> lock_;
};

} // namespace fs
} // namespace yb
#endif
