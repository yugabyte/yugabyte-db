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

#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pggate.h"

namespace yb {
namespace pggate {

PgMemctx::~PgMemctx() {
  Clear();
}

void PgMemctx::Register(Registrable *obj) {
  registered_objects_.push_back(*obj);
  obj->memctx_ = this;
}

void PgMemctx::Destroy(Registrable *obj) {
  auto& objs = obj->memctx_->registered_objects_;
  objs.erase_and_dispose(objs.iterator_to(*obj), std::default_delete<Registrable>());
}

void PgMemctx::Clear() {
  tabledesc_map_.clear();
  registered_objects_.clear_and_dispose(std::default_delete<Registrable>());
}

void PgMemctx::Cache(size_t hash_id, const PgTableDescPtr &table_desc) {
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
