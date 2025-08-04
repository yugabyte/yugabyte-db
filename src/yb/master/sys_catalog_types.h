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
#include <string>
#include <unordered_map>

#include "yb/common/pg_types.h"

namespace yb::master {

struct PgTypeInfo {
  char typtype;
  uint32_t typbasetype;
  PgTypeInfo(char typtype_, uint32_t typbasetype_) : typtype(typtype_), typbasetype(typbasetype_) {}
};

struct PgTableAllOids {
  bool initiated() const { return database_oid != kPgInvalidOid; }

  PgOid database_oid = kPgInvalidOid;
  PgOid relfilenode_oid = kPgInvalidOid;
  PgOid pg_table_oid = kPgInvalidOid;
};

// Generic PG OID -> PG OID map.
// Used internally to store collections like: [table oid -> relnamespace oid].
using PgOidToOidMap = std::unordered_map<PgOid, PgOid>;
// Generic PG OID -> String map.
// Used internally to store collections like: [relnamespace oid -> relnamespace name].
using PgOidToStringMap = std::unordered_map<PgOid, std::string>;

struct PgRelNamespaceData {
  PgOidToStringMap rel_nsp_name_map; // [relnamespace oid -> relnamespace name]
  PgOidToOidMap rel_nsp_oid_map;     // [table oid -> relnamespace oid]
};
// Cached data: db oid -> ( [nsp oid -> nsp name], [tbl oid -> nsp oid] ).
using PgDbRelNamespaceMap = std::unordered_map<PgOid, PgRelNamespaceData>;

} // namespace yb::master
