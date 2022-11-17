// Copyright (c) YugaByte, Inc.
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

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <iosfwd>
#include <limits>
#include <string>

#include "yb/common/doc_hybrid_time.h"

#include "yb/docdb/docdb_types.h"

#include "yb/gutil/endian.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/strings/substitute.h" // For Substitute

#include "yb/util/status_fwd.h"

// This file should only be included in .cc files of the docdb subsystem. Defines some macros for
// debugging DocDB functionality.

// Enable this during debugging only. This enables very verbose logging. Should always be undefined
// when code is checked in.
#undef DOCDB_DEBUG

constexpr bool IsDocDbDebug() {
#ifdef DOCDB_DEBUG
  return true;
#else
  return false;
#endif
}

#define DOCDB_DEBUG_LOG(...) \
  do { \
    if (IsDocDbDebug()) { \
      LOG(INFO) << "DocDB DEBUG [" << __func__  << "]: " \
                << strings::Substitute(__VA_ARGS__); \
    } \
  } while (false)

#ifdef DOCDB_DEBUG
#define DOCDB_DEBUG_SCOPE_LOG(msg, on_scope_bounds) \
    ScopeLogger sl(std::string("DocDB DEBUG [") + __func__ + "] " + (msg), on_scope_bounds)
#else
// Still compile the debug logging code to make sure it does not get broken silently.
#define DOCDB_DEBUG_SCOPE_LOG(msg, on_scope_bounds) \
    if (false) \
      ScopeLogger sl(std::string("DocDB DEBUG [") + __func__ + "] " + (msg), (on_scope_bounds))
#endif

namespace yb {
namespace docdb {

// Infer the key type from the given slice, given whether this is regular or intents RocksDB.
KeyType GetKeyType(const Slice& slice, StorageDbType db_type);

} // namespace docdb
} // namespace yb
