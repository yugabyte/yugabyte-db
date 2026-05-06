/*-------------------------------------------------------------------------
 *
 * pg_ruleutils.h
 *		Declarations for ruleutils.c
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef RULEUTILS_H
#define RULEUTILS_H

#include "postgres.h"

#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"

struct Plan;					/* avoid including plannodes.h here */
struct PlannedStmt;

/* Flags for pg_get_indexdef_columns_extended() */
#define RULE_INDEXDEF_PRETTY		0x01
#define RULE_INDEXDEF_KEYS_ONLY		0x02	/* ignore included attributes */

extern char *pgduckdb_pg_get_indexdef_string(Oid indexrelid);
extern char *pgduckdb_pg_get_indexdef_columns(Oid indexrelid, bool pretty);
extern char *pgduckdb_pg_get_indexdef_columns_extended(Oid indexrelid,
											  bits16 flags);
extern char *pgduckdb_pg_get_querydef_internal(Query *query, bool pretty);

extern char *pgduckdb_pg_get_partkeydef_columns(Oid relid, bool pretty);
extern char *pgduckdb_pg_get_partconstrdef_string(Oid partitionId, char *aliasname);

extern char *pgduckdb_pg_get_constraintdef_command(Oid constraintId);
extern char *pgduckdb_deparse_expression(Node *expr, List *dpcontext,
								bool forceprefix, bool showimplicit);
extern List *pgduckdb_deparse_context_for(const char *aliasname, Oid relid);
extern List *pgduckdb_deparse_context_for_plan_tree(struct PlannedStmt *pstmt,
										   List *rtable_names);
extern List *pgduckdb_set_deparse_context_plan(List *dpcontext,
									  struct Plan *plan, List *ancestors);
extern List *pgduckdb_select_rtable_names_for_explain(List *rtable,
											 Bitmapset *rels_used);
#if PG_VERSION_NUM >= 180000
extern char *get_window_frame_options_for_explain(int frameOptions,
												  Node *startOffset,
												  Node *endOffset,
												  List *dpcontext,
												  bool forceprefix);
#endif
extern char *pgduckdb_generate_collation_name(Oid collid);
extern char *pgduckdb_generate_opclass_name(Oid opclass);
extern char *pgduckdb_get_range_partbound_string(List *bound_datums);

extern char *pgduckdb_pg_get_statisticsobjdef_string(Oid statextid);

extern char *pgduckdb_get_list_partvalue_string(Const *val);

extern void* pg_duckdb_get_oper_expr_make_ctx(const char*, Node**, Node**);
extern void pg_duckdb_get_oper_expr_prefix(StringInfo buf, void* ctx);
extern void pg_duckdb_get_oper_expr_middle(StringInfo buf, void* ctx);
extern void pg_duckdb_get_oper_expr_suffix(StringInfo buf, void* ctx);

#endif							/* RULEUTILS_H */
