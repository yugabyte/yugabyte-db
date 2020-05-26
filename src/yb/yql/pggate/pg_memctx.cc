//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_memctx.h"

namespace yb {
namespace pggate {

PgMemctx::PgMemctx() {
}

PgMemctx::~PgMemctx() {
}

void PgMemctx::Reset() {
  // The safest option is to retain all YugaByte statement objects.
  // - Clear the table descriptors from cache. We can just reload them when requested.
  // - Clear the "stmts_" for now. However, if this causes issue, keep "stmts_" vector around.
  //
  // PgGate and its contexts are between Postgres and YugaByte lower layers, and because these
  // layers might be still operating on the raw pointer or reference to "stmts_" after Postgres's
  // cancellation, there's a chance we might have an unexpected issue.
  tabledesc_map_.clear();
  stmts_.clear();
}

void PgMemctx::Cache(const PgStatement::ScopedRefPtr &stmt) {
  // Hold the stmt until the context is released.
  stmts_.push_back(stmt);
}

void PgMemctx::Cache(size_t hash_id, const PgTableDesc::ScopedRefPtr &table_desc) {
  // Add table descriptor to table.
  tabledesc_map_[hash_id] = table_desc;
}

void PgMemctx::GetCache(size_t hash_id, PgTableDesc **handle) {
  // Read table descriptor to table.
  const auto iter = tabledesc_map_.find(hash_id);
  if (iter == tabledesc_map_.end()) {
    *handle = nullptr;
  } else {
    *handle = iter->second.get();
  }
}

}  // namespace pggate
}  // namespace yb
