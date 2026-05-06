#include "duckdb.hpp"

#include "pgduckdb/pgduckdb_planner.hpp"
#include "pgduckdb/pg/transactions.hpp"
#include "pgduckdb/pg/explain.hpp"
#include "pgduckdb/pgduckdb_xact.hpp"
#include "pgduckdb/pgduckdb_hooks.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"

extern "C" {
#include "postgres.h"

#include "catalog/pg_namespace.h"
#include "commands/extension.h"
#include "nodes/nodes.h"
#include "nodes/nodeFuncs.h"
#include "nodes/print.h"
#include "nodes/primnodes.h"
#include "tcop/utility.h"
#include "tcop/pquery.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "optimizer/optimizer.h"
#include "optimizer/planner.h"
}

#include "pgduckdb/pgduckdb.h"
#include "pgduckdb/pgduckdb_guc.hpp"
#include "pgduckdb/pgduckdb_metadata_cache.hpp"
#include "pgduckdb/pgduckdb_ddl.hpp"
#include "pgduckdb/pgduckdb_table_am.hpp"
#include "pgduckdb/pgduckdb_background_worker.hpp"
#include "pgduckdb/utility/copy.hpp"
#include "pgduckdb/vendor/pg_explain.hpp"
#include "pgduckdb/vendor/pg_list.hpp"
#include "pgduckdb/pgduckdb_node.hpp"
#include "pgduckdb/utility/cpp_wrapper.hpp"

static planner_hook_type prev_planner_hook = NULL;
static ExecutorStart_hook_type prev_executor_start_hook = NULL;
static ExecutorFinish_hook_type prev_executor_finish_hook = NULL;
static ExplainOneQuery_hook_type prev_explain_one_query_hook = NULL;
static emit_log_hook_type prev_emit_log_hook = NULL;

static bool
ContainsCatalogTable(List *rtes) {
	foreach_node(RangeTblEntry, rte, rtes) {
		if (rte->rtekind == RTE_SUBQUERY) {
			/* Check Subquery rtable list if any table is from PG catalog */
			if (ContainsCatalogTable(rte->subquery->rtable)) {
				return true;
			}
		}

		if (rte->relid) {
			Relation rel = RelationIdGetRelation(rte->relid);
			bool is_catalog_table = pgduckdb::IsCatalogTable(rel);
			RelationClose(rel);
			if (is_catalog_table) {
				return true;
			}
		}
	}
	return false;
}

static bool
IsDuckdbTable(Oid relid) {
	return pgduckdb::DuckdbTableAmGetName(relid) != nullptr;
}

static bool
ContainsDuckdbTables(List *rte_list) {
	foreach_node(RangeTblEntry, rte, rte_list) {
		if (IsDuckdbTable(rte->relid)) {
			return true;
		}
	}
	return false;
}

static bool
ContainsDuckdbItems(Node *node, void *context) {
	if (node == NULL)
		return false;

	if (IsA(node, Query)) {
		Query *query = (Query *)node;
		if (ContainsDuckdbTables(query->rtable)) {
			return true;
		}
#if PG_VERSION_NUM >= 160000
		return query_tree_walker(query, ContainsDuckdbItems, context, 0);
#else
		return query_tree_walker(query, (bool (*)())((void *)ContainsDuckdbItems), context, 0);
#endif
	}

	if (IsA(node, FuncExpr)) {
		FuncExpr *func = castNode(FuncExpr, node);
		if (pgduckdb::IsDuckdbOnlyFunction(func->funcid)) {
			return true;
		}
	}

	if (IsA(node, Aggref)) {
		Aggref *func = castNode(Aggref, node);
		if (pgduckdb::IsDuckdbOnlyFunction(func->aggfnoid)) {
			return true;
		}
	}

#if PG_VERSION_NUM >= 160000
	return expression_tree_walker(node, ContainsDuckdbItems, context);
#else
	return expression_tree_walker(node, (bool (*)())((void *)ContainsDuckdbItems), context);
#endif
}

/*
 * We only check ContainsFromClause if duckdb.force_execution is set to true.
 *
 * If there's no FROM clause, we're only selecting constants. From a
 * performance perspective there's not really a point in using DuckDB. If we
 * forward all of such queries to DuckDB anyway, then many queries that are
 * used to inspect postgres will throw a warning or return incorrect results.
 * For example:
 *
 *    SELECT current_setting('work_mem');
 *
 * So even if a user sets duckdb.force_execution = true, we still won't
 * forward such queries to DuckDB. With one exception: If the query
 * requires duckdb e.g. due to a duckdb-only function being used, we'll
 * still executing this in DuckDB.
 */
static bool
ContainsFromClause(Query *query) {
	return query->rtable;
}

namespace pgduckdb {

int64_t executor_nest_level = 0;

bool
ContainsPostgresTable(Node *node, void *context) {
	if (node == NULL)
		return false;

	if (IsA(node, Query)) {
		Query *query = (Query *)node;
		List *rtable = query->rtable;
		foreach_node(RangeTblEntry, rte, rtable) {
			if (rte->relid == InvalidOid) {
				continue;
			}
			if (!::IsDuckdbTable(rte->relid)) {
				return true;
			}
		}

#if PG_VERSION_NUM >= 160000
		return query_tree_walker(query, ContainsPostgresTable, context, 0);
#else
		return query_tree_walker(query, (bool (*)())((void *)ContainsPostgresTable), context, 0);
#endif
	}

#if PG_VERSION_NUM >= 160000
	return expression_tree_walker(node, ContainsPostgresTable, context);
#else
	return expression_tree_walker(node, (bool (*)())((void *)ContainsPostgresTable), context);
#endif
}

bool
ShouldTryToUseDuckdbExecution(Query *query) {
	if (top_level_duckdb_ddl_type == DDLType::REFRESH_MATERIALIZED_VIEW) {
		/* When refreshing materialized views, we only want to use DuckDB
		 * execution when needed by the query, not when duckdb.force_execution
		 * is set to true. This is because DuckDB execution and Postgres
		 * execution might return different types for the same query and when
		 * that happens for a materialized view refresh it results in data
		 * corruption. So we avoid this by ignoring duckdb.force_execution
		 * during a REFRESH, so that the refresh results in the same types as
		 * when the materialized view was created.
		 */
		return false;
	}

	if (pgduckdb::is_background_worker) {
		/* If we're the background worker, we don't want to force duckdb
		 * execution. Some of the queries that we're doing depend on Postgres
		 * execution, particularly the ones that use the regclsas type to
		 * understand tablenames. */
		return false;
	}

	return duckdb_force_execution && pgduckdb::IsAllowedStatement(query) && ContainsFromClause(query);
}

bool
NeedsDuckdbExecution(Query *query) {
	return ContainsDuckdbItems((Node *)query, NULL);
}

bool
IsCatalogTable(Relation rel) {
	auto namespace_oid = RelationGetNamespace(rel);
	return namespace_oid == PG_CATALOG_NAMESPACE || namespace_oid == PG_TOAST_NAMESPACE;
}

bool
IsAllowedStatement(Query *query, bool throw_error) {
	int elevel = throw_error ? ERROR : DEBUG4;
	/* DuckDB does not support modifying CTEs INSERT/UPDATE/DELETE */
	if (query->hasModifyingCTE) {
		elog(elevel, "DuckDB does not support modifying CTEs");
		return false;
	}

	/* We don't support modifying statements on Postgres tables yet */
	if (query->commandType != CMD_SELECT) {
		if (query->rtable != NULL) {
			RangeTblEntry *resultRte = list_nth_node(RangeTblEntry, query->rtable, query->resultRelation - 1);
			if (!::IsDuckdbTable(resultRte->relid)) {
				elog(elevel, "DuckDB does not support modififying Postgres tables");
				return false;
			}
		}

		if (pgduckdb::DidDisallowedMixedWrites()) {
			elog(elevel, "Writing to DuckDB and Postgres tables in the same transaction block is not supported");
			return false;
		}
	}

	if (pgduckdb::executor_nest_level > 0 && !duckdb_unsafe_allow_execution_inside_functions) {
		elog(elevel, "DuckDB execution is not supported inside functions");
		return false;
	}

	/*
	 * If any table is from pg_catalog, we don't want to use DuckDB. This is
	 * because DuckDB has its own pg_catalog tables that contain different data
	 * then Postgres its pg_catalog tables.
	 */
	if (ContainsCatalogTable(query->rtable)) {
		elog(elevel, "DuckDB does not support querying PG catalog tables");
		return false;
	}

	/* Anything else is hopefully fine... */
	return true;
}
} // namespace pgduckdb

static PlannedStmt *
DuckdbPlannerHook_Cpp(Query *parse, const char *query_string, int cursor_options, ParamListInfo bound_params) {
	if (pgduckdb::IsExtensionRegistered()) {
		if (pgduckdb::NeedsDuckdbExecution(parse)) {
			pgduckdb::TriggerActivity();
			pgduckdb::IsAllowedStatement(parse, true);

			return DuckdbPlanNode(parse, cursor_options, true);
		} else if (pgduckdb::ShouldTryToUseDuckdbExecution(parse)) {
			pgduckdb::TriggerActivity();
			PlannedStmt *duckdbPlan = DuckdbPlanNode(parse, cursor_options, false);
			if (duckdbPlan) {
				return duckdbPlan;
			}
			/* If we can't create a plan, we'll fall back to Postgres */
		}
		if (parse->commandType != CMD_SELECT && !pgduckdb::pg::AllowWrites()) {
			elog(ERROR, "Writing to DuckDB and Postgres tables in the same transaction block is not supported");
		}
	}

	/*
	 * If we're executing a PG query, then if we'll execute a DuckDB
	 * later in the same transaction that means that DuckDB query was
	 * not executed at the top level, but internally by that PG query.
	 * A common case where this happens is a plpgsql function that
	 * executes a DuckDB query.
	 */

	pgduckdb::MarkStatementNotTopLevel();

	return prev_planner_hook(parse, query_string, cursor_options, bound_params);
}

static PlannedStmt *
DuckdbPlannerHook(Query *parse, const char *query_string, int cursor_options, ParamListInfo bound_params) {
	return InvokeCPPFunc(DuckdbPlannerHook_Cpp, parse, query_string, cursor_options, bound_params);
}

bool
IsDuckdbPlan(PlannedStmt *stmt) {
	Plan *plan = stmt->planTree;
	if (!plan) {
		return false;
	}

	/* If the plan is a Material node, we need to extract the actual plan to
	 * see if it is our CustomScan node. A Matarial containing our CustomScan
	 * node gets created for backward scanning cursors. See our usage of.
	 * materialize_finished_plan. */
	if (IsA(plan, Material)) {
		Material *material = castNode(Material, plan);
		plan = material->plan.lefttree;
		if (!plan) {
			return false;
		}
	}

	if (!IsA(plan, CustomScan)) {
		return false;
	}

	CustomScan *custom_scan = castNode(CustomScan, plan);
	return custom_scan->methods == &duckdb_scan_scan_methods;
}

/*
 * Claim the current command id for obvious DuckDB writes.
 *
 * If we're not in a transaction, this triggers the command to be executed
 * outside of any implicit transaction.
 *
 * This claims the command ID if we're doing a INSERT/UPDATE/DELETE on a DuckDB
 * table. This isn't strictly necessary for safety, as the ExecutorFinishHook
 * would catch it anyway, but this allows us to fail early, i.e. before doing
 * the potentially time-consuming write operation.
 */
static void
DuckdbExecutorStartHook_Cpp(QueryDesc *queryDesc) {
	if (!IsDuckdbPlan(queryDesc->plannedstmt)) {
		/*
		 * If we're executing a PG query, then if we'll execute a DuckDB
		 * later in the same transaction that means that DuckDB query was
		 * not executed at the top level, but internally by that PG query.
		 * A common case where this happens is a plpgsql function that
		 * executes a DuckDB query.
		 */

		pgduckdb::MarkStatementNotTopLevel();
		return;
	}

	pgduckdb::AutocommitSingleStatementQueries();

	if (queryDesc->operation == CMD_SELECT) {
		return;
	}
	pgduckdb::ClaimCurrentCommandId();
}

static void
DuckdbExecutorStartHook(QueryDesc *queryDesc, int eflags) {
	pgduckdb::executor_nest_level++;
	if (!pgduckdb::IsExtensionRegistered()) {
		pgduckdb::MarkStatementNotTopLevel();
		prev_executor_start_hook(queryDesc, eflags);
		return;
	}

	prev_executor_start_hook(queryDesc, eflags);

	InvokeCPPFunc(DuckdbExecutorStartHook_Cpp, queryDesc);
}

/*
 * Claim the current command id for non-obvious DuckDB writes.
 *
 * It's possible that a Postgres SELECT query writes to DuckDB, for example
 * when using one of our UDFs that that internally writes to DuckDB. This
 * function claims the command ID in those cases.
 */
static void
DuckdbExecutorFinishHook_Cpp(QueryDesc *queryDesc) {
	if (!IsDuckdbPlan(queryDesc->plannedstmt)) {
		return;
	}

	if (!pgduckdb::ddb::DidWrites()) {
		return;
	}

	pgduckdb::ClaimCurrentCommandId();
}

static void
DuckdbExecutorFinishHook(QueryDesc *queryDesc) {
	Assert(pgduckdb::executor_nest_level > 0);
	pgduckdb::executor_nest_level--;
	if (!pgduckdb::IsExtensionRegistered()) {
		prev_executor_finish_hook(queryDesc);
		return;
	}

	prev_executor_finish_hook(queryDesc);
	InvokeCPPFunc(DuckdbExecutorFinishHook_Cpp, queryDesc);
}

void
DuckdbExplainOneQueryHook(Query *query, int cursorOptions, IntoClause *into, ExplainState *es, const char *queryString,
                          ParamListInfo params, QueryEnvironment *queryEnv) {
	/*
	 * It might seem sensible to store this data in the custom_private
	 * field of the CustomScan node, but that's not a trivial change to make.
	 * Storing this in a global variable works fine, as long as we only use
	 * this variable during planning when we're actually executing an explain
	 * QUERY (this can be checked by checking the commandTag of the
	 * ActivePortal). This even works when plans would normally be cached,
	 * because EXPLAIN always execute this hook whenever they are executed.
	 * EXPLAIN queries are also always re-planned (see
	 * standard_ExplainOneQuery).
	 */
	duckdb_explain_analyze = pgduckdb::pg::IsExplainAnalyze(es);
	duckdb_explain_format = pgduckdb::pg::DuckdbExplainFormat(es);
	duckdb_explain_ctas = into != NULL;
	prev_explain_one_query_hook(query, cursorOptions, into, es, queryString, params, queryEnv);
}

static bool
IsOutdatedMotherduckCatalogErrcode(int error_code) {
	switch (error_code) {
	case ERRCODE_UNDEFINED_COLUMN:
	case ERRCODE_UNDEFINED_SCHEMA:
	case ERRCODE_UNDEFINED_TABLE:
		return true;
	default:
		return false;
	}
}

static bool
ContainsDuckdbRowReturningFunction(const char *query_string) {
	return strstr(query_string, "read_parquet") || strstr(query_string, "read_csv") ||
	       strstr(query_string, "read_json") || strstr(query_string, "delta_scan") ||
	       strstr(query_string, "iceberg_scan") || strstr(query_string, "duckdb.query");
}

static void
DuckdbEmitLogHook(ErrorData *edata) {
	if (prev_emit_log_hook) {
		prev_emit_log_hook(edata);
	}

	if (edata->elevel == ERROR && edata->sqlerrcode == ERRCODE_UNDEFINED_COLUMN && pgduckdb::IsExtensionRegistered()) {
		if (ContainsDuckdbRowReturningFunction(debug_query_string)) {
			edata->hint = pstrdup("If you use DuckDB functions like read_parquet, you need to use the r['colname'] "
			                      "syntax to use columns. If "
			                      "you're already doing that, maybe you forgot to give the function the r alias.");
		}
	} else if (edata->elevel == ERROR && edata->sqlerrcode == ERRCODE_SYNTAX_ERROR &&
	           pgduckdb::IsExtensionRegistered() &&
	           strcmp(edata->message_id,
	                  "a column definition list is only allowed for functions returning \"record\"") == 0) {
		if (ContainsDuckdbRowReturningFunction(debug_query_string)) {
			/*
			 * NOTE: We can probably remove this hint after a few releases once
			 * we've updated all known blogposts that still used the old syntax.
			 */
			edata->hint = pstrdup(
			    "If you use DuckDB functions like read_parquet, you need to use the r['colname'] syntax introduced "
			    "in pg_duckdb 0.3.0. It seems like you might be using the outdated \"AS (colname coltype, ...)\" "
			    "syntax");
		}
	}

	/*
	 * The background worker stops syncing the catalogs after the
	 * motherduck_background_catalog_refresh_inactivity_timeout has been
	 * reached. This means that the table metadata that Postgres knows about
	 * could be out of date, which could then easily result in errors about
	 * missing from the Postgres parser because it cannot understand the query.
	 *
	 * This mitigates the impact of that by triggering a restart of the catalog
	 * syncing when one of those errors occurs AND the current user can
	 * actually use DuckDB.
	 */
	if (IsOutdatedMotherduckCatalogErrcode(edata->sqlerrcode) && pgduckdb::IsExtensionRegistered() &&
	    pgduckdb::IsDuckdbExecutionAllowed()) {
		pgduckdb::TriggerActivity();
	}
}

void
DuckdbInitHooks(void) {
	prev_planner_hook = planner_hook ? planner_hook : standard_planner;
	planner_hook = DuckdbPlannerHook;

	prev_executor_start_hook = ExecutorStart_hook ? ExecutorStart_hook : standard_ExecutorStart;
	ExecutorStart_hook = DuckdbExecutorStartHook;

	prev_executor_finish_hook = ExecutorFinish_hook ? ExecutorFinish_hook : standard_ExecutorFinish;
	ExecutorFinish_hook = DuckdbExecutorFinishHook;

	prev_explain_one_query_hook = ExplainOneQuery_hook ? ExplainOneQuery_hook : standard_ExplainOneQuery;
	ExplainOneQuery_hook = DuckdbExplainOneQueryHook;

	prev_emit_log_hook = emit_log_hook ? emit_log_hook : NULL;
	emit_log_hook = DuckdbEmitLogHook;

	DuckdbInitUtilityHook();
}
