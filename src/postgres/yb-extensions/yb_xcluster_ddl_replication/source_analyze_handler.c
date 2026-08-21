/*-----------------------------------------------------------------------------
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *
 *-----------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/catalog.h"
#include "catalog/pg_class.h"
#include "catalog/pg_type_d.h"
#include "executor/spi.h"
#include "extension_util.h"
#include "lib/stringinfo.h"
#include "source_analyze_handler.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

/* Prefix of the dollar quoting tag used to wrap the generated statements. */
#define ANALYZE_STATS_QUOTE_TAG_PREFIX "$yb_xcluster_analyze"

/* Columns of kRelationsQuery. */
#define RELATIONS_NSPNAME_COLUMN_ID		  1
#define RELATIONS_RELNAME_COLUMN_ID		  2
#define RELATIONS_RELPAGES_COLUMN_ID	  3
#define RELATIONS_RELTUPLES_COLUMN_ID	  4
#define RELATIONS_RELALLVISIBLE_COLUMN_ID 5

/*
 * The analyzed relation plus its indexes; ANALYZE also computes statistics for
 * the columns of expression indexes.
 *
 * Invalid / not-live indexes are skipped: they may not exist on the target
 * (see yb_xcluster_handle_phantom_indexes), and applying statistics to a
 * relation that is missing there would halt replication.
 *
 * The analyzed relation is ordered first purely to make the generated
 * statement easier to read.
 */
static const char *kRelationsQuery =
	"SELECT n.nspname, c.relname, c.relpages, c.reltuples, c.relallvisible "
	"FROM pg_catalog.pg_class c "
	"JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace "
	"WHERE c.oid = $1 OR c.oid IN ("
	"  SELECT i.indexrelid FROM pg_catalog.pg_index i "
	"  WHERE i.indrelid = $1 AND i.indisvalid AND i.indislive) "
	"ORDER BY (c.oid <> $1), c.relname";

/*
 * Statistics columns of pg_stats, in the order they are selected by
 * kAttributeStatsQueryPrefix, along with the argument name and type used to
 * pass them back to pg_restore_attribute_stats. The types must match
 * attarginfo in attribute_stats.c exactly; a mismatch is only reported as a
 * warning by the target and the statistics are silently dropped.
 */
typedef struct YbAttributeStatsColumn
{
	const char *name;
	const char *type;
} YbAttributeStatsColumn;

static const YbAttributeStatsColumn kAttributeStatsColumns[] = {
	{"inherited", "boolean"},
	{"null_frac", "real"},
	{"avg_width", "integer"},
	{"n_distinct", "real"},
	{"most_common_vals", "text"},
	{"most_common_freqs", "real[]"},
	{"histogram_bounds", "text"},
	{"correlation", "real"},
	{"most_common_elems", "text"},
	{"most_common_elem_freqs", "real[]"},
	{"elem_count_histogram", "real[]"},
	{"range_length_histogram", "text"},
	{"range_empty_frac", "real"},
	{"range_bounds_histogram", "text"},
};

#define NUM_ATTRIBUTE_STATS_COLUMNS ((int) lengthof(kAttributeStatsColumns))

/* attname is selected first, the statistics columns follow. */
#define ATTRIBUTE_STATS_ATTNAME_COLUMN_ID 1
#define ATTRIBUTE_STATS_FIRST_COLUMN_ID	  2

/* Relation level statistics, as returned by kRelationsQuery. */
typedef struct YbRelationStats
{
	char	   *nspname;
	char	   *relname;
	char	   *relpages;
	char	   *reltuples;
	char	   *relallvisible;
} YbRelationStats;

bool
ShouldReplicateAnalyzedRelation(Oid relid)
{
	HeapTuple	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));

	if (!HeapTupleIsValid(tuple))
		return false;

	Form_pg_class relform = (Form_pg_class) GETSTRUCT(tuple);
	Oid			relnamespace = relform->relnamespace;
	bool		is_temp = (relform->relpersistence == RELPERSISTENCE_TEMP);

	ReleaseSysCache(tuple);

	/* Temporary relations are session local and are never replicated. */
	if (is_temp)
		return false;

	/*
	 * System catalogs (which includes information_schema) are maintained
	 * independently by each universe.
	 */
	if (IsCatalogRelationOid(relid))
		return false;

	/*
	 * The extension's own tables are replicated as ordinary data; there is no
	 * point in replicating statistics for them.
	 */
	char	   *nspname = get_namespace_name(relnamespace);

	if (nspname && strcmp(nspname, EXTENSION_NAME) == 0)
		return false;

	return true;
}

/*
 * Appends ", 'name', 'value'::type" to buf, mirroring how pg_dump emits
 * statistics import arguments.
 */
static void
AppendNamedArgument(StringInfo buf, const char *name, const char *type,
					const char *value)
{
	appendStringInfo(buf, ", '%s', %s::%s", name, quote_literal_cstr(value),
					 type);
}

/*
 * Appends the pg_restore_relation_stats call for a single relation.
 */
static void
AppendRelationStats(StringInfo buf, const YbRelationStats *rel)
{
	appendStringInfo(buf,
					 "PERFORM pg_catalog.pg_restore_relation_stats('version', %d::integer",
					 PG_VERSION_NUM);
	AppendNamedArgument(buf, "schemaname", "text", rel->nspname);
	AppendNamedArgument(buf, "relname", "text", rel->relname);

	/* A NULL here means "leave the target's current value alone". */
	if (rel->relpages)
		AppendNamedArgument(buf, "relpages", "integer", rel->relpages);
	if (rel->reltuples)
		AppendNamedArgument(buf, "reltuples", "real", rel->reltuples);
	if (rel->relallvisible)
		AppendNamedArgument(buf, "relallvisible", "integer",
							rel->relallvisible);

	appendStringInfoString(buf, ");\n");
}

/*
 * Appends one pg_restore_attribute_stats call per row of pg_stats belonging to
 * the given relation.
 */
static void
AppendAttributeStats(StringInfo buf, const YbRelationStats *rel)
{
	StringInfoData query_buf;

	initStringInfo(&query_buf);
	appendStringInfoString(&query_buf, "SELECT attname");
	for (int i = 0; i < NUM_ATTRIBUTE_STATS_COLUMNS; i++)
	{
		const YbAttributeStatsColumn *column = &kAttributeStatsColumns[i];

		/*
		 * The array valued columns of pg_stats are declared as anyarray, so
		 * cast them to the type that pg_restore_attribute_stats expects.
		 */
		appendStringInfo(&query_buf, ", %s::%s", column->name, column->type);
	}
	appendStringInfoString(&query_buf,
						   " FROM pg_catalog.pg_stats "
						   "WHERE schemaname = $1 AND tablename = $2 "
						   "ORDER BY attname, inherited");

	Oid			arg_types[2] = {TEXTOID, TEXTOID};
	Datum		arg_vals[2] = {CStringGetTextDatum(rel->nspname),
		CStringGetTextDatum(rel->relname)};

	/*
	 * Not read only: we need SPI to bump the command counter so that the
	 * pg_statistic rows that ANALYZE just wrote are visible to us.
	 */
	int			exec_res = SPI_execute_with_args(query_buf.data, 2, arg_types,
												 arg_vals, /* Nulls */ NULL,
												  /* readonly */ false,
												  /* tuple-count limit */ 0);

	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, query_buf.data);

	int			num_attrs = SPI_processed;
	SPITupleTable *tuptable = SPI_tuptable;

	for (int row = 0; row < num_attrs; row++)
	{
		HeapTuple	spi_tuple = tuptable->vals[row];
		char	   *attname = SPI_getvalue(spi_tuple, tuptable->tupdesc,
										   ATTRIBUTE_STATS_ATTNAME_COLUMN_ID);

		if (!attname)
			continue;			/* Should not happen, attname is NOT NULL. */

		appendStringInfo(buf,
						 "PERFORM pg_catalog.pg_restore_attribute_stats('version', %d::integer",
						 PG_VERSION_NUM);
		AppendNamedArgument(buf, "schemaname", "text", rel->nspname);
		AppendNamedArgument(buf, "relname", "text", rel->relname);
		AppendNamedArgument(buf, "attname", "text", attname);

		for (int i = 0; i < NUM_ATTRIBUTE_STATS_COLUMNS; i++)
		{
			const YbAttributeStatsColumn *column = &kAttributeStatsColumns[i];
			char	   *value = SPI_getvalue(spi_tuple, tuptable->tupdesc,
											 ATTRIBUTE_STATS_FIRST_COLUMN_ID + i);

			/* Omit the argument entirely rather than passing a NULL. */
			if (value)
				AppendNamedArgument(buf, column->name, column->type, value);
		}

		appendStringInfoString(buf, ");\n");
	}

	pfree(query_buf.data);
}

char *
BuildRestoreStatsQuery(Oid relid)
{
	Oid			arg_types[1] = {OIDOID};
	Datum		arg_vals[1] = {ObjectIdGetDatum(relid)};

	/*
	 * Not read only: we need SPI to bump the command counter so that the
	 * pg_class rows that ANALYZE just updated are visible to us.
	 */
	int			exec_res = SPI_execute_with_args(kRelationsQuery, 1, arg_types,
												 arg_vals, /* Nulls */ NULL,
												  /* readonly */ false,
												  /* tuple-count limit */ 0);

	if (exec_res != SPI_OK_SELECT)
		elog(ERROR, "SPI_exec failed (error %d): %s", exec_res, kRelationsQuery);

	int			num_rels = SPI_processed;

	if (num_rels == 0)
		return NULL;

	/*
	 * Copy the relation level statistics out before running any further
	 * queries, as those overwrite SPI_tuptable.
	 */
	YbRelationStats *rels = (YbRelationStats *) palloc(num_rels *
													   sizeof(YbRelationStats));

	for (int row = 0; row < num_rels; row++)
	{
		HeapTuple	spi_tuple = SPI_tuptable->vals[row];
		YbRelationStats *rel = &rels[row];

		rel->nspname = SPI_GetText(spi_tuple, RELATIONS_NSPNAME_COLUMN_ID);
		rel->relname = SPI_GetText(spi_tuple, RELATIONS_RELNAME_COLUMN_ID);
		rel->relpages = SPI_GetText(spi_tuple, RELATIONS_RELPAGES_COLUMN_ID);
		rel->reltuples = SPI_GetText(spi_tuple, RELATIONS_RELTUPLES_COLUMN_ID);
		rel->relallvisible = SPI_GetText(spi_tuple,
										 RELATIONS_RELALLVISIBLE_COLUMN_ID);
	}

	StringInfoData body;

	initStringInfo(&body);

	for (int row = 0; row < num_rels; row++)
	{
		if (!rels[row].nspname || !rels[row].relname)
			continue;

		AppendRelationStats(&body, &rels[row]);
		AppendAttributeStats(&body, &rels[row]);
	}

	pfree(rels);

	/*
	 * Wrap everything in a DO block: the ddl_queue handler on the target
	 * executes the query with a client that rejects result rows, and the
	 * pg_restore_*_stats functions return a boolean.
	 *
	 * Dollar quoting is scanned lexically, so the single quotes around the
	 * values embedded in the body do not hide anything from it. The body
	 * contains column values, which can be any text at all, including something
	 * that looks like our tag, so pick a tag that does not occur in the body,
	 * the same way pg_get_functiondef() does for function bodies. Checking for
	 * the tag prefix rather than the whole tag is conservative but keeps this
	 * obviously correct.
	 */
	StringInfoData tag;

	initStringInfo(&tag);
	appendStringInfoString(&tag, ANALYZE_STATS_QUOTE_TAG_PREFIX);
	while (strstr(body.data, tag.data) != NULL)
		appendStringInfoChar(&tag, 'x');
	appendStringInfoChar(&tag, '$');

	StringInfoData buf;

	initStringInfo(&buf);
	appendStringInfo(&buf, "DO %s BEGIN\n%sEND %s;", tag.data, body.data,
					 tag.data);

	pfree(tag.data);
	pfree(body.data);
	return buf.data;
}
