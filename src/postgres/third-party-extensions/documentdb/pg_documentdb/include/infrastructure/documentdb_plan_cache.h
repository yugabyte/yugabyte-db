/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/planner/documentdb_plan_cache.h
 *
 * Common declarations for the pg_documentdb plan cache.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_PLAN_CACHE_H
#define BSON_PLAN_CACHE_H

#include <executor/spi.h>

/*
 * ID prefixes for different types of query.
 *
 * The first 32 bits of a query ID are used to identity the operation,
 * and the latter 32 bits to identify different query variants.
 */
#define QUERY_ID_INSERT (1L << 32)
#define QUERY_ID_UPDATE_BY_TID (2L << 32)
#define QUERY_ID_DELETE_BY_TID (3L << 32)

#define QUERY_DELETE_WITH_FILTER (4L << 32);
#define QUERY_DELETE_WITH_FILTER_SHARDKEY (5L << 32)
#define QUERY_DELETE_WITH_FILTER_ID (6L << 32)
#define QUERY_DELETE_WITH_FILTER_SHARDKEY_ID (7L << 32)

#define QUERY_CALL_UPDATE_ONE (8L << 32)

#define QUERY_DELETE_ONE (9L << 32)
#define QUERY_DELETE_ONE_ID (10L << 32)
#define QUERY_DELETE_ONE_ID_RETURN_DOCUMENT (11L << 32)

#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE (12L << 32)
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_NON_OBJECT_ID (13L << 32)
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_ONLY_OBJECT_ID (14L << 32)
#define QUERY_UPDATE_SELECT_UPDATE_CANDIDATE_BOTH_FILTER (15L << 32)


#define QUERY_ID_RETRY_RECORD_INSERT (20L << 32)
#define QUERY_ID_RETRY_RECORD_DELETE (21L << 32)
#define QUERY_ID_RETRY_RECORD_SELECT (22L << 32)

#define QUERY_ID_INSERT_OR_REPLACE (23L << 32)


/* GUC that controls the query plan cache size */
extern int QueryPlanCacheSizeLimit;


void InitializeQueryPlanCache(void);
SPIPlanPtr GetSPIQueryPlan(uint64 collectionId, uint64 queryId,
						   const char *query, Oid *argTypes, int argCount);

SPIPlanPtr GetSPIQueryPlanWithLocalShard(uint64 collectionId, const char *shardTableName,
										 uint64 queryId,
										 const char *query, Oid *argTypes, int argCount);

#endif
