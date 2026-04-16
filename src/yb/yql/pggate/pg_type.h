//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include <span>
#include <unordered_map>

#include "yb/util/logging.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

class PgTypeInfo {
 public:
  explicit PgTypeInfo(YbcPgTypeEntities entities) {
    for (const auto& entity : std::span(entities.data, entities.count)) {
      map_[entity.type_oid] = &entity;
    }
    ybctid_ = Find(kByteArrayOid);
    CHECK(ybctid_) << "Failed to find type for ybctid";
  }

  const YbcPgTypeEntity* Find(PgOid type_oid) const {
    auto it = map_.find(type_oid);
    return it == map_.end() ? nullptr : it->second;
  }

  const YbcPgTypeEntity& GetYbctid() const {
    return *ybctid_;
  }

 private:
  // Mapping table of YugaByte and PostgreSQL datatypes.
  std::unordered_map<int, const YbcPgTypeEntity*> map_;
  const YbcPgTypeEntity* ybctid_;
};

}  // namespace yb::pggate
