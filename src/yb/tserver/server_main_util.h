// Copyright (c) Yugabyte, Inc.
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

// Utilities used in main functions of server processes.

#pragma once

#include <string>

#include "yb/util/status.h"

namespace yb {

// If a memory limit gflag has this value after applying any command line flags then we replace its
// value at process start with a computed value based on the available memory among other things.
#define USE_RECOMMENDED_MEMORY_VALUE -1000

// Special values for the db_block_cache_size_bytes and db_block_cache_size_percentage flags.
// See the flag descriptions for their meaning.
#define DB_CACHE_SIZE_USE_PERCENTAGE -1
#define DB_CACHE_SIZE_CACHE_DISABLED -2
#define DB_CACHE_SIZE_USE_DEFAULT -3

Status MasterTServerParseFlagsAndInit(
    const std::string& server_type, bool is_master, int* argc, char*** argv);

}  // namespace yb
