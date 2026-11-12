// Copyright (c) YugabyteDB, Inc.
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

#include <functional>

#include "yb/storage/storage_fwd.h"

#include "yb/util/env.h"
#include "yb/util/result.h"

#include "yb/vector_index/vector_lsm.pb.h"

namespace yb::vector_index {

struct VectorLSMMetadataLoadResult {
  size_t next_free_file_no = 0;

  // Current set of chunks in the manifest after replaying all add/remove updates.
  std::vector<VectorLSMChunkPB> chunks;

  std::string ToString() const;
};

Result<VectorLSMMetadataLoadResult> VectorLSMMetadataLoad(Env* env, const std::string& dir);

// Files of a vector index found in its storage directory.
struct VectorLSMFiles {
  // Manifest and chunk files, in the manifest load order.
  std::vector<std::string> manifest_and_chunk_files;

  // Whether the directory has at least one vector payload file, see VectorPayloadMap. Payload
  // files are verified to accompany chunk files and are not listed, so expected file lists in
  // tests are backend independent.
  bool has_payload_files = false;
};

Result<VectorLSMFiles> ListVectorLSMFiles(Env* env, const std::string& dir);

Result<std::unique_ptr<WritableFile>> VectorLSMMetadataOpenFile(
    Env* env, const std::string& dir, size_t file_index);

// The files update references must be in the directory of file, which is synced before the append.
Status VectorLSMMetadataAppendUpdate(
    Env& env, WritableFile& file, const VectorLSMUpdatePB& update);

// The counterpart of VectorLSM::ModifyChunkFrontiers for a vector LSM that is not open: calls
// modify on the frontiers of every chunk of the LSM stored in dir, and adds a chunk without a file
// carrying added_chunk_frontiers, as a single reset update in a new manifest file. The existing
// manifest files are left as they are, so they can be hard links, e.g. to the files of a snapshot.
// The new file is synced together with dir. Does nothing if dir does not exist.
// Returns false, after logging why, if the manifest could not be loaded. Nothing is written then.
Result<bool> VectorLSMMetadataModifyChunkFrontiers(
    Env* env, const std::string& dir,
    const std::function<storage::UserFrontiersPtr()>& frontiers_factory,
    const std::function<void(storage::UserFrontiers&)>& modify,
    const storage::UserFrontiers& added_chunk_frontiers);

}  // namespace yb::vector_index
