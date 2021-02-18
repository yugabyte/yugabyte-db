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

// TTL helper methods are used in the DocDB code.

#ifndef YB_DOCDB_DOC_TTL_UTIL_H_
#define YB_DOCDB_DOC_TTL_UTIL_H_

#include <string>

#include "yb/util/slice.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

// Determines whether or not the TTL for a key has expired, given the ttl for the key, its hybrid
// time and the hybrid_time we're reading at.
bool HasExpiredTTL(const HybridTime& key_hybrid_time, const MonoDelta& ttl,
                   const HybridTime& read_hybrid_time);

// Computes the table level TTL, given a schema.
const MonoDelta TableTTL(const Schema& schema);

// Computes the effective TTL by combining the column level TTL with the default table level TTL.
const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const MonoDelta& default_ttl);

// Utility function that computes the effective TTL directly given a schema
const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const Schema& schema);

// Cassandra considers a TTL of zero as resetting the TTL.
static const uint64_t kResetTTL = 0;

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_TTL_UTIL_H_
