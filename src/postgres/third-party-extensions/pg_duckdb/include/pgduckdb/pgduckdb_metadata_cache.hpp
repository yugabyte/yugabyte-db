#pragma once

#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb {
bool IsExtensionRegistered();
bool IsDuckdbOnlyFunction(Oid function_oid);
uint64_t CacheVersion();
Oid ExtensionOid();
Oid SchemaOid();
Oid DuckdbRowOid();
Oid DuckdbStructOid();
Oid DuckdbUnresolvedTypeOid();
Oid DuckdbUnionOid();
Oid DuckdbMapOid();
Oid DuckdbStructArrayOid();
Oid DuckdbUnionArrayOid();
Oid DuckdbMapArrayOid();
Oid DuckdbJsonOid();
Oid DuckdbTableAmOid();
bool IsDuckdbTable(Form_pg_class relation);
bool IsDuckdbTable(Relation relation);
bool IsMotherDuckTable(Form_pg_class relation);
bool IsMotherDuckTable(Relation relation);
bool IsDuckdbExecutionAllowed();
void RequireDuckdbExecution();
} // namespace pgduckdb
