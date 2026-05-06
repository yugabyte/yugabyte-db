#pragma once

#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb {
extern int64_t executor_nest_level;
bool IsAllowedStatement(Query *query, bool throw_error = false);
bool IsCatalogTable(Relation rel);
bool ContainsPostgresTable(Node *node, void *context);
bool NeedsDuckdbExecution(Query *query);
bool ShouldTryToUseDuckdbExecution(Query *query);
} // namespace pgduckdb
