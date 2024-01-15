/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/plan_cache.c
 *
 * Implementation of an SPI query plan cache for helioapi.
 *
 * Each collection has different query strings, and each query can have
 * have several variations. Hence the cached is keyed by collection ID
 * and a set of query flags. A least recently used (LRU) queue is kept
 * to limit the size of the cache.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"

#include "utils/memutils.h"

#include "infrastructure/plan_cache.h"

/* QueryKey is used as the key of a query in the cache */
typedef struct QueryKey
{
	/* collection ID which we are querying */
	uint64 collectionId;

	/* identifier defining different query variants */
	uint64 queryId;
} QueryKey;

typedef struct QueryPlanCacheEntry
{
	/* key of the query plan in the hash */
	QueryKey queryKey;

	/* pointer to the query plan */
	SPIPlanPtr plan;

	/* node in the LRU queue */
	dlist_node lruNode;

	/* whether this cache entry was fully built */
	bool isValid;
} QueryPlanCacheEntry;

/* internal function declarations */
static void RemoveOldestQueryPlan(void);

/* memory context in which the cache is allocated */
static MemoryContext QueryPlanCacheContext = NULL;

/* hash table containing cached query plans */
static HTAB *QueryPlanHash = NULL;

/* linked list for keeping track of LRU */
static dlist_head QueryPlanLRUQueue;

/* number of entries in the query plan cache */
static int CachedPlansCount = 0;

/* number of entries allowed in the query plan cache */
extern int QueryPlanCacheSizeLimit;


/*
 * InitializeQueryPlanCache initalized the session-level query plan
 * cache.
 */
void
InitializeQueryPlanCache(void)
{
	if (QueryPlanHash != NULL)
	{
		return;
	}

	QueryPlanCacheContext = AllocSetContextCreate(CacheMemoryContext,
												  "Pgmongo query cache context",
												  ALLOCSET_DEFAULT_SIZES);


	/* create the (database name, collection name) -> collection hash */
	HASHCTL info;
	memset(&info, 0, sizeof(info));
	info.keysize = sizeof(QueryKey);
	info.entrysize = sizeof(QueryPlanCacheEntry);
	info.hcxt = QueryPlanCacheContext;
	int hashFlags = HASH_ELEM | HASH_BLOBS | HASH_CONTEXT;

	QueryPlanHash = hash_create("Pgmongo query cache hash", 32, &info, hashFlags);

	dlist_init(&QueryPlanLRUQueue);
}


/*
 * GetSPIQueryPlan gets the query plan for a given query and purges the
 * oldest entry from the LRU queue if the cache exceeds the size limit.
 */
SPIPlanPtr
GetSPIQueryPlan(uint64 collectionId, uint64 queryId,
				const char *query, Oid *argTypes, int argCount)
{
	InitializeQueryPlanCache();

	bool foundInCache = false;
	QueryKey queryKey = {
		.collectionId = collectionId,
		.queryId = queryId
	};

	QueryPlanCacheEntry *entry = hash_search(QueryPlanHash,
											 &queryKey,
											 HASH_ENTER,
											 &foundInCache);
	if (!foundInCache || !entry->isValid)
	{
		/*
		 * Since HASH_ENTER doesn't zero-initialize cache-entry, we first set
		 * isValid to false before performing any other operations. That way,
		 * if we now fail to fully-initialize the cache entry for some reason,
		 * then the next caller wouldn't mistakenly assume the otherwise due to
		 * isValid being set to a garbage value different than "false".
		 */
		entry->isValid = false;

		if (CachedPlansCount >= QueryPlanCacheSizeLimit)
		{
			RemoveOldestQueryPlan();
		}

		SPIPlanPtr plan = SPI_prepare(query, argCount, argTypes);
		if (plan == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
							errmsg("SPI_prepare failed for: %s", query)));
		}

		SPI_keepplan(plan);
		entry->plan = plan;

		/*
		 * Now that we initialized all the fields without any errors, i) append
		 * the cache entry at the tail of the queue and ii) mark the cache entry
		 * as valid.
		 *
		 * We must do those at the very end and atomically to make sure that
		 * QueryPlanHash and QueryPlanLRUQueue are consistent. For atomicity, we
		 * must _not_ perform any operations that could result in an ereport()
		 * call between i) and ii).
		 */
		dlist_push_tail(&QueryPlanLRUQueue, &entry->lruNode);
		CachedPlansCount++;
		entry->isValid = true;
	}
	else
	{
		/* move entry to the tail of the queue */
		dlist_delete(&entry->lruNode);
		dlist_push_tail(&QueryPlanLRUQueue, &entry->lruNode);
	}

	return entry->plan;
}


/*
 * RemoveOldestQueryPlan removes the oldest query plan, which is
 * at the head of the LRU queue.
 */
static void
RemoveOldestQueryPlan(void)
{
	QueryPlanCacheEntry *entry = dlist_container(QueryPlanCacheEntry,
												 lruNode,
												 dlist_pop_head_node(&QueryPlanLRUQueue));

	bool foundInCache = false;
	hash_search(QueryPlanHash, &entry->queryKey, HASH_REMOVE, &foundInCache);
	Assert(foundInCache);

	SPI_freeplan(entry->plan);

	CachedPlansCount--;
}
