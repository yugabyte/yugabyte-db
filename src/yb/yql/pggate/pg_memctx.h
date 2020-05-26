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
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_MEMCTX_H_
#define YB_YQL_PGGATE_PG_MEMCTX_H_

#include <vector>
#include <unordered_map>
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/pg_tabledesc.h"

namespace yb {
namespace pggate {

// This is the YB counterpart of Postgres's MemoryContext.
// - Each YB Memctx will be associated with a Postgres MemoryContext.
// - YB Memctx will be initialized to NULL and later created on its first use.
// - When Postgres MemoryContext is destroyed, YB Memctx will be destroyed.
// - When Postgres MemoryContext allocates YugaByte object, that YB object will belong to the
//   associated YB Memctx. The object is automatically destroyed when YB Memctx is destroyed.
class PgMemctx : public RefCountedThreadSafe<PgMemctx> {
 public:
  PgMemctx();
  virtual ~PgMemctx();

  // Postgres has this option where it clears the allocated memory for the current context but
  // keeps all the allocated memory for the child contexts.
  // - Not sure if we should destroy the yugabyte objects in the current context also because the
  //   objects in nested context might still have referenced to the objects of the outer memctx.
  // - For now, to be safe, we keep them around and free them when all contexts are destroyed.
  //
  // NOTE:
  // - In Postgres, the objects in the outer context can references to the objects of the nested
  //   context but not vice versa.
  // - In YugaByte, the above abstraction must be followed, but I am not yet sure that we did.
  void Reset();

  // Cache the statement in the memory context to be destroyed later on.
  void Cache(const PgStatement::ScopedRefPtr &stmt);

  // Cache the table descriptor in the memory context to be destroyed later on.
  void Cache(size_t hash_id, const PgTableDesc::ScopedRefPtr &table_desc);

  // Read the table descriptor from cache.
  void GetCache(size_t hash_id, PgTableDesc **handle);

 private:
  // All statements that are allocated with this memory context.
  std::vector<PgStatement::ScopedRefPtr> stmts_;

  // All talbe descriptors that are allocated with this memory context.
  std::unordered_map<size_t, PgTableDesc::ScopedRefPtr> tabledesc_map_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_MEMCTX_H_
