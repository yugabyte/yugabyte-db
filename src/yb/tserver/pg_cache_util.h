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

#include <cstdint>
#include <span>
#include <string>

#include "yb/common/pg_types.h"

namespace yb::tserver {

struct DbCatalogVersionUpdate {
  PgOid db_oid;
  uint64_t new_version;

  std::string ToString() const;
};

struct PgTableCacheInvalidationFilter {
  using UpdatedDbs = std::span<const DbCatalogVersionUpdate>;
  using DroppedDbs = std::span<const PgOid>;

  UpdatedDbs updated_dbs;
  DroppedDbs dropped_dbs{};

  std::string ToString() const;
};

} // namespace yb::tserver
