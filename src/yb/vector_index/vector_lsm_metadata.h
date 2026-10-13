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

Status VectorLSMMetadataAppendUpdate(WritableFile& file, const VectorLSMUpdatePB& update);

// Applies modify_user_frontier to the user frontiers of every chunk in the manifest of dir, and
// saves the result as a single reset update in a new manifest file. The files of the previous
// manifest are left in place, to be removed on the next load, so a manifest whose files are hard
// links to another directory's, e.g. a restored checkpoint, does not change there.
Status VectorLSMMetadataModifyFrontiers(
    Env* env, const std::string& dir,
    const std::function<Status(google::protobuf::Any&)>& modify_user_frontier);

}  // namespace yb::vector_index
