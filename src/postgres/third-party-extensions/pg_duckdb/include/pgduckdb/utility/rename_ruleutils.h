#pragma once

#include "postgres.h"
#include "fmgr.h"

/*
 * This file contains the renaming of the functions exposed by
 * vendor/pg_ruleutils.h functions to avoid conflicts with the PostgreSQL
 * functions.
 */
#define pg_get_indexdef_string           pgduckdb_pg_get_indexdef_string
#define pg_get_indexdef_columns          pgduckdb_pg_get_indexdef_columns
#define pg_get_indexdef_columns_extended pgduckdb_pg_get_indexdef_columns_extended
#define pg_get_querydef                  pgduckdb_pg_get_querydef_internal
#define pg_get_partkeydef_columns        pgduckdb_pg_get_partkeydef_columns
#define pg_get_partconstrdef_string      pgduckdb_pg_get_partconstrdef_string
#define pg_get_constraintdef_command     pgduckdb_pg_get_constraintdef_command
#define deparse_expression               pgduckdb_deparse_expression
#define deparse_context_for              pgduckdb_deparse_context_for
#define deparse_context_for_plan_tree    pgduckdb_deparse_context_for_plan_tree
#define set_deparse_context_plan         pgduckdb_set_deparse_context_plan
#define select_rtable_names_for_explain  pgduckdb_select_rtable_names_for_explain
#define generate_collation_name          pgduckdb_generate_collation_name
#define generate_opclass_name            pgduckdb_generate_opclass_name
#define get_range_partbound_string       pgduckdb_get_range_partbound_string
#define pg_get_statisticsobjdef_string   pgduckdb_pg_get_statisticsobjdef_string
#define get_list_partvalue_string        pgduckdb_get_list_partvalue_string

/*
 * The following replaces all usages of generate_qualified_relation_name and
 * generate_relation_name with calls to the pgduckdb_relation_name function
 */
#define generate_qualified_relation_name          pgduckdb_relation_name
#define generate_relation_name(relid, namespaces) pgduckdb_relation_name(relid)

#define declare_pgduckdb_ruleutils_function(original_name) extern Datum pgduckdb_##original_name(PG_FUNCTION_ARGS);

#define pg_get_viewdef           pgduckdb_pg_get_viewdef
#define pg_get_viewdef_ext       pgduckdb_pg_get_viewdef_ext
#define pg_get_viewdef_wrap      pgduckdb_pg_get_viewdef_wrap
#define pg_get_viewdef_name      pgduckdb_pg_get_viewdef_name
#define pg_get_viewdef_name_ext  pgduckdb_pg_get_viewdef_name_ext
#define pg_get_triggerdef        pgduckdb_pg_get_triggerdef
#define pg_get_triggerdef_ext    pgduckdb_pg_get_triggerdef_ext
#define pg_get_indexdef_name     pgduckdb_pg_get_indexdef_name
#define pg_get_indexdef_name_ext pgduckdb_pg_get_indexdef_name_ext
declare_pgduckdb_ruleutils_function(pg_get_viewdef);
declare_pgduckdb_ruleutils_function(pg_get_viewdef_ext);
declare_pgduckdb_ruleutils_function(pg_get_viewdef_wrap);
declare_pgduckdb_ruleutils_function(pg_get_viewdef_name);
declare_pgduckdb_ruleutils_function(pg_get_viewdef_name_ext);
declare_pgduckdb_ruleutils_function(pg_get_triggerdef);
declare_pgduckdb_ruleutils_function(pg_get_triggerdef_ext);
declare_pgduckdb_ruleutils_function(pg_get_indexdef);
declare_pgduckdb_ruleutils_function(pg_get_indexdef_ext);
