//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
#include <string>

#include "yb/rocksdb/status.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/options.h"
#include "yb/util/strongly_typed_bool.h"

namespace rocksdb {

extern Status CopyFile(Env* env, const std::string& source,
                       const std::string& destination, uint64_t size = 0);

extern Status DeleteSSTFile(const DBOptions* db_options,
                            const std::string& fname, uint32_t path_id);

YB_STRONGLY_TYPED_BOOL(CreateIfMissing);
YB_STRONGLY_TYPED_BOOL(UseHardLinks);

extern Status CopyDirectory(
    Env* env, const std::string& src_dir, const std::string& dest_dir,
    CreateIfMissing create_if_missing = CreateIfMissing::kTrue,
    UseHardLinks use_hard_links = UseHardLinks::kTrue);

}  // namespace rocksdb
