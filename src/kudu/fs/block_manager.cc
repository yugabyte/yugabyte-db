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

#include "kudu/fs/block_manager.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/metrics.h"

// The default value is optimized for the case where:
// 1. the cfile blocks are colocated with the WALs.
// 2. The underlying hardware is a spinning disk.
// 3. The underlying filesystem is either XFS or EXT4.
// 4. cfile_do_on_finish is 'close' (see cfile/cfile_writer.cc).
//
// When all conditions hold, this value ensures low latency for WAL writes.
DEFINE_bool(block_coalesce_close, false,
            "Coalesce synchronization of data during CloseBlocks()");
TAG_FLAG(block_coalesce_close, experimental);

DEFINE_bool(block_manager_lock_dirs, true,
            "Lock the data block directories to prevent concurrent usage. "
            "Note that read-only concurrent usage is still allowed.");
TAG_FLAG(block_manager_lock_dirs, unsafe);

namespace kudu {
namespace fs {

const char* BlockManager::kInstanceMetadataFileName = "block_manager_instance";

BlockManagerOptions::BlockManagerOptions()
  : read_only(false) {
}

BlockManagerOptions::~BlockManagerOptions() {
}

} // namespace fs
} // namespace kudu
