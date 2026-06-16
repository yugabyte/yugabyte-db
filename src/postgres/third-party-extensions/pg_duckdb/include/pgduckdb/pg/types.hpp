#pragma once

#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb::pg {
bool IsArrayType(Oid type_oid);
bool IsDomainType(Oid type_oid);
bool IsArrayDomainType(Oid type_oid);
Oid GetBaseTypeAndTypmod(Oid attribute_type_oid, int32_t *type_modifier);
Datum StringToNumeric(const char *str);
Datum StringToVarbit(const char *str);
const char *VarbitToString(Datum pg_varbit);
} // namespace pgduckdb::pg
