#pragma once

#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb {
void InitUserDataCache();
bool IsMotherDuckEnabled();
Oid MotherDuckPostgresUserOid();
char *MotherDuckPostgresUserName();
void InvalidateUserDataCache();
Oid GetMotherduckForeignServerOid();
Oid GetMotherDuckUserMappingOid();
} // namespace pgduckdb
