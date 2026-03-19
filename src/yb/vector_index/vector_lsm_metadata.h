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

#include "yb/util/env.h"
#include "yb/util/result.h"

#include "yb/vector_index/vector_lsm.pb.h"

namespace yb::vector_index {

struct VectorLSMMetadataLoadResult {
  size_t next_free_file_no = 0;
  std::vector<VectorLSMUpdatePB> updates;

  std::string ToString() const;
};

Result<VectorLSMMetadataLoadResult> VectorLSMMetadataLoad(
    Env* env, const std::string& dir);
Result<std::unique_ptr<WritableFile>> VectorLSMMetadataOpenFile(
    Env* env, const std::string& dir, size_t file_index);
Status VectorLSMMetadataAppendUpdate(WritableFile& file, const VectorLSMUpdatePB& update);

}  // namespace yb::vector_index
