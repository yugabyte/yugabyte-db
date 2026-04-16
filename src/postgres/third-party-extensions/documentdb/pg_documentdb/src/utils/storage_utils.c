/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/storage_utils.c
 *
 * Utilities that provide physical storage related metrics and information.
 *
 *-------------------------------------------------------------------------
 */


#include "utils/storage_utils.h"

#include "api_hooks.h"
#include "commands/diagnostic_commands_common.h"
#include "io/bson_core.h"
#include "io/bsonvalue_utils.h"
#include "utils/documentdb_errors.h"
#include "utils/guc_utils.h"
#include "utils/query_utils.h"
#include "metadata/metadata_cache.h"

static CollectionBloatStats MergeBloatStatsBsons(List *workerBsons);

PG_FUNCTION_INFO_V1(get_bloat_stats_worker);


/*
 * Quickly estimates the bloat of a collection table.
 * This can be +/- 20% off, but provides a good enough estimate for the collection.
 * For more details refer: https://github.com/pgexperts/pgx_scripts/blob/master/bloat/table_bloat_check.sql
 */
Datum
get_bloat_stats_worker(PG_FUNCTION_ARGS)
{
	uint64 collectionId = PG_GETARG_INT64(0);

	MongoCollection *collection = GetMongoCollectionByColId(collectionId,
															AccessShareLock);
	ArrayType *shardNames = NULL;
	ArrayType *shardOids = NULL;
	GetMongoCollectionShardOidsAndNames(collection, &shardOids, &shardNames);

	if (shardNames == NULL)
	{
		return PointerGetDatum(PgbsonInitEmpty());
	}

	StringInfo bloatEstimateQuery = makeStringInfo();
	appendStringInfo(bloatEstimateQuery,
					 "WITH constants AS ("
					 "   SELECT %d::numeric AS bs, 23::numeric AS hdr, 8::numeric AS ma"
					 "),",
					 BLCKSZ);

	appendStringInfo(bloatEstimateQuery,
					 "null_headers AS ("
					 "   SELECT "
					 "   hdr+1+(sum(case when null_frac <> 0 THEN 1 else 0 END)/8) as nullhdr, "
					 "   SUM((1-null_frac)*avg_width) as datawidth, "
					 "   MAX(null_frac) as maxfracsum,"
					 "   schemaname, tablename, hdr, ma, bs "
					 "   FROM pg_stats CROSS JOIN constants "
					 "   WHERE schemaname = %s"
					 "   AND tablename = ANY ($1)"
					 "   GROUP BY schemaname, tablename, hdr, ma, bs ), ",
					 quote_literal_cstr(ApiDataSchemaName));

	appendStringInfo(bloatEstimateQuery,
					 " data_headers AS ( "
					 "   SELECT "
					 "   ma, bs, hdr, schemaname, tablename, "
					 "   (datawidth+(hdr+ma-(case when hdr%%ma=0 THEN ma ELSE hdr%%ma END)))::numeric AS datahdr, "
					 "   (maxfracsum*(nullhdr+ma-(case when nullhdr%%ma=0 THEN ma ELSE nullhdr%%ma END))) AS nullhdr2 "
					 "   FROM null_headers "
					 "),"
					 "table_estimates AS ( "
					 "   SELECT schemaname, tablename, bs, "
					 "   reltuples::numeric as est_rows, relpages * bs as table_bytes, "
					 "   CEIL((reltuples* "
					 "       (datahdr + nullhdr2 + 4 + ma - "
					 "        (CASE WHEN datahdr%%ma=0 "
					 "            THEN ma ELSE datahdr%%ma END)"
					 "        )/(bs-20))) * bs AS expected_bytes, "
					 "   reltoastrelid "
					 "   FROM data_headers "
					 "   JOIN pg_class ON tablename = relname "
					 "   JOIN pg_namespace ON relnamespace = pg_namespace.oid "
					 "   AND schemaname = nspname "
					 "   WHERE pg_class.relkind = 'r' "
					 "),"
					 "estimates_with_toast AS ( "
					 "   SELECT schemaname, tablename, "
					 "        TRUE as can_estimate,"
					 "        est_rows,"
					 "        table_bytes + ( coalesce(toast.relpages, 0) * bs ) as table_bytes,"
					 "        expected_bytes + ( ceil( coalesce(toast.reltuples, 0) / 4 ) * bs ) as expected_bytes"
					 "    FROM table_estimates LEFT OUTER JOIN pg_class as toast"
					 "        ON table_estimates.reltoastrelid = toast.oid"
					 "            AND toast.relkind = 't'"
					 "),"
					 "table_estimates_plus AS ("
					 "    SELECT current_database() as databasename,"
					 "            schemaname, tablename, can_estimate, "
					 "            est_rows,"
					 "            CASE WHEN table_bytes > 0"
					 "                THEN table_bytes::NUMERIC"
					 "                ELSE NULL::NUMERIC END"
					 "                AS table_bytes,"
					 "            CASE WHEN expected_bytes > 0 "
					 "                THEN expected_bytes::NUMERIC"
					 "                ELSE NULL::NUMERIC END"
					 "                    AS expected_bytes,"
					 "            CASE WHEN expected_bytes > 0 AND table_bytes > 0"
					 "                AND expected_bytes <= table_bytes"
					 "                THEN (table_bytes - expected_bytes)::NUMERIC"
					 "                ELSE 0::NUMERIC END AS bloat_bytes"
					 "    FROM estimates_with_toast"
					 "),"
					 "bloat_data AS ("
					 "    select current_database() as databasename,"
					 "        schemaname, tablename, can_estimate, "
					 "        round(bloat_bytes*100/table_bytes) as pct_bloat,"
					 "        bloat_bytes,"
					 "        table_bytes, expected_bytes, est_rows"
					 "    FROM table_estimates_plus"
					 "),"
					 "projectBloat AS ("
					 "   SELECT "
					 "   SUM(bloat_bytes::int8) as bloat_bytes,"
					 "   SUM(table_bytes::int8) as table_bytes "
					 "   FROM bloat_data"
					 ")"
					 " SELECT %s.row_get_bson(projectBloat) FROM projectBloat",
					 CoreSchemaNameV2);

	int argCount = 1;
	char argNulls[1] = { ' ' };
	Oid argTypes[1] = { TEXTARRAYOID };
	Datum argValues[1] = { PointerGetDatum(shardNames) };
	bool readOnly = true;

	Datum resultDatum[1] = { 0 };
	bool isNulls[1] = { false };
	int numResults = 1;
	RunMultiValueQueryWithNestedDistribution(bloatEstimateQuery->data, argCount, argTypes,
											 argValues,
											 argNulls,
											 readOnly, SPI_OK_SELECT, resultDatum,
											 isNulls, numResults);

	if (isNulls[0])
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Unable to retrieve bloat statistics for the specified collection %lu",
							collectionId)));
	}

	PG_RETURN_POINTER(DatumGetPgBson(resultDatum[0]));
}


/*
 * Gets the bloat stats for a given collectionId across all shards.
 */
CollectionBloatStats
GetCollectionBloatEstimate(uint64 collectionId)
{
	int numValues = 1;
	Datum values[1] = { UInt64GetDatum(collectionId) };
	Oid types[1] = { INT8OID };
	List *workerBsons = RunQueryOnAllServerNodes("BloatStats", values, types, numValues,
												 get_bloat_stats_worker,
												 ApiInternalSchemaNameV2,
												 "get_bloat_stats_worker");
	return MergeBloatStatsBsons(workerBsons);
}


/*
 * Merges the bloat stats from all the workers into a single
 * CollectionBloatStats object.
 */
static CollectionBloatStats
MergeBloatStatsBsons(List *workerBsons)
{
	CollectionBloatStats bloatStats;
	memset(&bloatStats, 0, sizeof(CollectionBloatStats));
	bloatStats.nullStats = true;

	ListCell *workerCell;
	foreach(workerCell, workerBsons)
	{
		pgbson *workerBson = lfirst(workerCell);
		bson_iter_t iter;
		PgbsonInitIterator(workerBson, &iter);
		while (bson_iter_next(&iter))
		{
			/* Non-Empty stats, reset nullStats */
			bloatStats.nullStats = false;

			const char *key = bson_iter_key(&iter);
			const bson_value_t *value = bson_iter_value(&iter);
			if (key[0] == 'b')
			{
				bloatStats.estimatedBloatStorage += BsonValueAsInt64(value);
			}
			else if (key[0] == 't')
			{
				bloatStats.estimatedTableStorage += BsonValueAsInt64(value);
			}
		}
	}

	return bloatStats;
}
