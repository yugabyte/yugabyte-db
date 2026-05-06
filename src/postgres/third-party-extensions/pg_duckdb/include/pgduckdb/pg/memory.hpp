#pragma once

#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb::pg {

MemoryContext MemoryContextCreate(MemoryContext parent, const char *name);
MemoryContext MemoryContextSwitchTo(MemoryContext target);
void MemoryContextReset(MemoryContext context);
void MemoryContextDelete(MemoryContext context);

} // namespace pgduckdb::pg
