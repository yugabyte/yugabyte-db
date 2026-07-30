#pragma once

#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb {
enum class DDLType { NONE, CREATE_TABLE, ALTER_TABLE, REFRESH_MATERIALIZED_VIEW, RENAME_VIEW };
/* Tracks the type of DDL statement that is currently being executed */
extern DDLType top_level_duckdb_ddl_type;
bool IsMotherDuckView(Form_pg_class relation);
bool IsMotherDuckView(Relation relation);
FuncExpr *GetDuckdbViewExprFromQuery(Query *query);
} // namespace pgduckdb

void DuckdbTruncateTable(Oid relation_oid);
void DuckdbInitUtilityHook();
