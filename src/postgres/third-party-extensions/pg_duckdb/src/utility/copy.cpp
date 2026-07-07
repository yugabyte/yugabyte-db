#include "pgduckdb/utility/copy.hpp"

#include <inttypes.h>

#include "pgduckdb/pgduckdb_guc.hpp"
#include "pgduckdb/pgduckdb_metadata_cache.hpp"
#include "pgduckdb/pgduckdb_hooks.hpp"

extern "C" {
#include "postgres.h"

#include "access/table.h"
#include "catalog/namespace.h"
#include "commands/defrem.h"
#include "common/string.h"
#include "executor/executor.h"
#include "nodes/parsenodes.h"
#include "parser/parser.h"
#include "parser/parse_node.h"
#include "parser/parse_relation.h"
#include "storage/lockdefs.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/rls.h"

#include "pgduckdb/vendor/pg_list.hpp"
#include "pgduckdb/pgduckdb_ruleutils.h"
}

/*
 * Returns the relation of the copy_stmt as a fully qualified DuckDB table reference. This is done
 * including the column names if provided in the original copy_stmt, e.g. my_table(column1, column2).
 */
static void
AppendCreateRelationCopyString(StringInfo info, ParseState *pstate, CopyStmt *copy_stmt) {
	/* Open and lock the relation, using the appropriate lock type. */
	Relation rel = table_openrv(copy_stmt->relation, AccessShareLock);
	Oid relid = RelationGetRelid(rel);
	ParseNamespaceItem *nsitem = addRangeTableEntryForRelation(pstate, rel, AccessShareLock, NULL, false, false);

#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *perminfo = nsitem->p_perminfo;
	perminfo->requiredPerms = ACL_SELECT;
#else
	RangeTblEntry *rte = nsitem->p_rte;
	rte->requiredPerms = ACL_SELECT;
#endif

#if PG_VERSION_NUM >= 160000
	ExecCheckPermissions(pstate->p_rtable, list_make1(perminfo), true);
#else
	ExecCheckRTPerms(pstate->p_rtable, true);
#endif

	table_close(rel, AccessShareLock);

	appendStringInfoString(info, pgduckdb_relation_name(relid));
	if (!copy_stmt->attlist) {
		return;
	}

	appendStringInfo(info, "(");
	bool first = true;

#if PG_VERSION_NUM >= 150000
	foreach_node(String, attr, copy_stmt->attlist) {
#else
	foreach_ptr(Value, attr, copy_stmt->attlist) {
#endif
		if (first) {
			first = false;
		} else {
			appendStringInfo(info, ", ");
		}

		appendStringInfoString(info, quote_identifier(strVal(attr)));
	}

	appendStringInfo(info, ") ");
}

/*
 * Checks if postgres permissions permit us to execute this query as the
 * current user.
 */
void
CheckQueryPermissions(Query *query, const char *query_string) {
	Query *copied_query = (Query *)copyObjectImpl(query);

	/* First we let postgres plan the query */
	PlannedStmt *postgres_plan = pg_plan_query(copied_query, query_string, CURSOR_OPT_PARALLEL_OK, NULL);

	if (postgres_plan == nullptr) {
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		                errmsg("(PGDuckDB/CheckQueryPermissions) Used query in COPY that could not be planned")));
	}

#if PG_VERSION_NUM >= 160000
	ExecCheckPermissions(postgres_plan->rtable, postgres_plan->permInfos, true);
#else
	ExecCheckRTPerms(postgres_plan->rtable, true);
#endif

	foreach_node(RangeTblEntry, rte, postgres_plan->rtable) {
		if (check_enable_rls(rte->relid, InvalidOid, false) == RLS_ENABLED) {
			ereport(ERROR,
			        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			         errmsg("(PGDuckDB/CheckQueryPermissions) RLS enabled on \"%s\", cannot use DuckDB based COPY",
			                get_rel_name(rte->relid))));
		}
	}
}

static char *
CommaSeparatedQuotedList(const List *names) {
	StringInfoData string;
	ListCell *l;

	initStringInfo(&string);

	foreach (l, names) {
		if (l != list_head(names))
			appendStringInfoChar(&string, ',');
		appendStringInfoString(&string, quote_identifier(strVal(lfirst(l))));
	}

	return string.data;
}

static void
AppendCreateCopyOptions(StringInfo info, CopyStmt *copy_stmt) {
	if (list_length(copy_stmt->options) == 0) {
		appendStringInfo(info, ";");
		return;
	}

	appendStringInfo(info, "(");

	bool first = true;
	foreach_node(DefElem, defel, copy_stmt->options) {
		if (first) {
			first = false;
		} else {
			appendStringInfo(info, ", ");
		}

		appendStringInfoString(info, defel->defname);
		if (defel->arg) {
			appendStringInfo(info, " ");
			switch (nodeTag(defel->arg)) {
			case T_Integer:
			case T_Float:
#if PG_VERSION_NUM >= 150000
			case T_Boolean:
#endif
				appendStringInfoString(info, defGetString(defel));
				break;
			case T_String:
			case T_TypeName:
				appendStringInfoString(info, quote_literal_cstr(defGetString(defel)));
				break;
			case T_List:
				appendStringInfo(info, "(");
				appendStringInfoString(info, CommaSeparatedQuotedList((List *)defel->arg));
				appendStringInfo(info, ")");
				break;
			case T_A_Star:
				appendStringInfo(info, "*");
				break;
			default:
				elog(ERROR, "Unexpected node type in COPY: %" PRIu64, (uint64_t)nodeTag(defel->arg));
			}
		}
	}

	appendStringInfo(info, ");");
}

/*
 * Throws an error if a rewritten raw statement returns an unexpected number of
 * queries (i.e. not just a single query). This is taken from Postgres its
 * BeginCopyTo function.
 */
static void
CheckRewritten(List *rewritten) {
	/* check that we got back something we can work with */
	if (rewritten == NIL) {
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		                errmsg("DO INSTEAD NOTHING rules are not supported for COPY")));
	} else if (list_length(rewritten) > 1) {
		/* examine queries to determine which error message to issue */
		foreach_node(Query, q, rewritten) {
			if (q->querySource == QSRC_QUAL_INSTEAD_RULE)
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				                errmsg("conditional DO INSTEAD rules are not supported for COPY")));
			if (q->querySource == QSRC_NON_INSTEAD_RULE)
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				                errmsg("DO ALSO rules are not supported for the COPY")));
		}

		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
		                errmsg("multi-statement DO INSTEAD rules are not supported for COPY")));
	}
}

static bool
CheckPrefix(const char *str, const char *prefix) {
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

static bool
StringEquals(const char *str1, const char *str2) {
	return strcmp(str1, str2) == 0;
}

static bool
StringOneOfInternal(const char *str, const char *compare_to[], int length_of_compare_to) {
	for (int i = 0; i < length_of_compare_to; i++) {
		if (StringEquals(str, compare_to[i])) {
			return true;
		}
	}
	return false;
}

bool
MatchesURIScheme(const char *str) {
	if (str == NULL) {
		return false;
	}

	const char *p = str;

	// First character must be a letter
	if (!isalpha(*p))
		return false;
	p++;

	// Continue with alphanumeric
	while (*p && (isalnum(*p)))
		p++;

	// Must be followed by ://
	return (strncmp(p, "://", 3) == 0);
}

#define StringOneOf(str, compare_to) StringOneOfInternal(str, compare_to, lengthof(compare_to))

static bool
IsAllowedStatement(CopyStmt *stmt, bool throw_error = false) {
	int elevel = throw_error ? ERROR : DEBUG4;

	if (stmt->relation) {
		Relation rel = table_openrv(stmt->relation, AccessShareLock);
		bool is_duckdb_table = pgduckdb::IsDuckdbTable(rel);
		bool is_catalog_table = pgduckdb::IsCatalogTable(rel);
		table_close(rel, NoLock);
		if (is_catalog_table) {
			elog(elevel, "DuckDB does not support querying PG catalog tables");
			return false;
		}

		if (stmt->is_from && !is_duckdb_table) {
			elog(elevel, "pg_duckdb does not support COPY ... FROM ... yet for Postgres tables");
			return false;
		}
	}

	if (stmt->filename == NULL) {
		elog(elevel, "COPY ... TO STDOUT/FROM STDIN is not supported by DuckDB");
		return false;
	}

	if (!stmt->is_from && !is_absolute_path(stmt->filename) && !MatchesURIScheme(stmt->filename)) {
		ereport(elevel, (errcode(ERRCODE_INVALID_NAME), errmsg("relative path not allowed for COPY to file")));
		return false;
	}

	return true;
}

static bool
ContainsDuckdbCopyOption(CopyStmt *stmt) {
	static const char *duckdb_only_formats[] = {"parquet", "json"};
	static const char *duckdb_only_options[] = {"partition_by",
	                                            "use_tmp_file",
	                                            "overwrite_or_ignore",
	                                            "overwrite",
	                                            "append",
	                                            "filename_pattern",
	                                            "file_extension",
	                                            "per_thread_output",
	                                            "file_size_bytes",
	                                            "write_partition_columns",
	                                            "array",
	                                            "compression",
	                                            "dateformat",
	                                            "timestampformat",
	                                            "nullstr",
	                                            "prefix",
	                                            "suffix",
	                                            "compression_level",
	                                            "field_ids",
	                                            "row_group_size_bytes",
	                                            "row_group_size",
	                                            "row_groups_per_file"};

	foreach_node(DefElem, defel, stmt->options) {
		if (strcmp(defel->defname, "format") == 0) {
			char *fmt = defGetString(defel);
			if (StringOneOf(fmt, duckdb_only_formats)) {
				return true;
			}
		} else if (StringOneOf(defel->defname, duckdb_only_options)) {
			return true;
		}
	}
	return false;
}

static bool
NeedsDuckdbExecution(CopyStmt *stmt) {
	/* Copy `filename` should start with S3/GS/R2 prefix */
	if (stmt->filename != NULL) {
		if (CheckPrefix(stmt->filename, "s3://") || CheckPrefix(stmt->filename, "r2://") ||
		    CheckPrefix(stmt->filename, "gcs://") || CheckPrefix(stmt->filename, "gs://") ||
		    CheckPrefix(stmt->filename, "http://") || CheckPrefix(stmt->filename, "https://") ||
		    CheckPrefix(stmt->filename, "az://") || CheckPrefix(stmt->filename, "azure://") ||
		    CheckPrefix(stmt->filename, "abfs://") || CheckPrefix(stmt->filename, "abfss://")) {

			return true;
		}

		if (pg_str_endswith(stmt->filename, ".parquet") || pg_str_endswith(stmt->filename, ".json") ||
		    pg_str_endswith(stmt->filename, ".ndjson") || pg_str_endswith(stmt->filename, ".jsonl") ||
		    pg_str_endswith(stmt->filename, ".gz") || pg_str_endswith(stmt->filename, ".zst")) {
			return true;
		}
	}

	if (ContainsDuckdbCopyOption(stmt)) {
		return true;
	}

	if (!stmt->relation) {
		return false;
	}

	if (stmt->is_from) {
		/*
		 * For COPY ... FROM we require duckdb execution if it's a duckdb
		 * table. For COPY ... TO this is not the case, because we can use
		 * Postgres its COPY implementation on duckdb tables.
		 */
		Relation rel = table_openrv(stmt->relation, AccessShareLock);
		bool is_duckdb_table = pgduckdb::IsDuckdbTable(rel);
		table_close(rel, NoLock);
		/* We do support duckdb tables */
		return is_duckdb_table;
	}
	return false;
}

const char *
MakeDuckdbCopyQuery(PlannedStmt *pstmt, const char *query_string, struct QueryEnvironment *query_env) {
	CopyStmt *copy_stmt = (CopyStmt *)pstmt->utilityStmt;
	bool needs_duckdb_execution = NeedsDuckdbExecution(copy_stmt);

	if (needs_duckdb_execution) {
		IsAllowedStatement(copy_stmt, true);
	} else if (!pgduckdb::duckdb_force_execution || !IsAllowedStatement(copy_stmt)) {
		if (copy_stmt->relation && !copy_stmt->is_from) {
			/*
			 * We don't support enough of the table access method API to allow
			 * Postgres to read from the table directly when users use:
			 * COPY duckdb_table TO ...
			 *
			 * Luckily we can easily work around that lack of support by
			 * creating a SELECT * query on the duckdb table, because COPY from
			 * a duckdb query works fine.
			 */
			Relation rel = table_openrv(copy_stmt->relation, AccessShareLock);
			bool is_duckdb_table = pgduckdb::IsDuckdbTable(rel);
			table_close(rel, NoLock);
			if (is_duckdb_table) {
				char *select_query;
				select_query = psprintf("SELECT * FROM %s", quote_identifier(get_rel_name(RelationGetRelid(rel))));
				RawStmt *raw_stmt = linitial_node(RawStmt, raw_parser(select_query, RAW_PARSE_DEFAULT));
				copy_stmt->query = raw_stmt->stmt;
				copy_stmt->relation = nullptr;
			}
		}

		return nullptr;
	}

	StringInfo rewritten_query_info = makeStringInfo();
	appendStringInfo(rewritten_query_info, "COPY ");
	if (copy_stmt->query) {
		RawStmt *raw_stmt = makeNode(RawStmt);
		raw_stmt->stmt = (Node *)copyObjectImpl(copy_stmt->query);
		raw_stmt->stmt_location = pstmt->stmt_location;
		raw_stmt->stmt_len = pstmt->stmt_len;

#if PG_VERSION_NUM >= 150000
		List *rewritten = pg_analyze_and_rewrite_fixedparams(raw_stmt, query_string, NULL, 0, NULL);
#else
		List *rewritten = pg_analyze_and_rewrite(raw_stmt, query_string, NULL, 0, NULL);
#endif
		CheckRewritten(rewritten);

		Query *query = linitial_node(Query, rewritten);

		if (needs_duckdb_execution) {
			pgduckdb::IsAllowedStatement(query, true);
		} else if (!pgduckdb::IsAllowedStatement(query, false) || query->commandType != CMD_SELECT) {
			/* We don't need to do anything */
			return nullptr;
		}

		if (query->commandType != CMD_SELECT) {
			ereport(ERROR,
			        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("DuckDB COPY only supports SELECT statements")));
		}

		CheckQueryPermissions(query, query_string);

		appendStringInfo(rewritten_query_info, "(");
		appendStringInfoString(rewritten_query_info, pgduckdb_get_querydef(query));
		appendStringInfo(rewritten_query_info, ")");
	} else {
		ParseState *pstate = make_parsestate(NULL);
		pstate->p_sourcetext = query_string;
		pstate->p_queryEnv = query_env;
		AppendCreateRelationCopyString(rewritten_query_info, pstate, copy_stmt);
	}

	if (copy_stmt->is_from) {
		appendStringInfo(rewritten_query_info, " FROM ");
	} else {
		appendStringInfo(rewritten_query_info, " TO ");
	}
	appendStringInfoString(rewritten_query_info, quote_literal_cstr(copy_stmt->filename));
	appendStringInfo(rewritten_query_info, " ");
	AppendCreateCopyOptions(rewritten_query_info, copy_stmt);

	elog(DEBUG2, "(PGDuckDB/CreateRelationCopyString) Rewritten query: \'%s\'", rewritten_query_info->data);

	return rewritten_query_info->data;
}
