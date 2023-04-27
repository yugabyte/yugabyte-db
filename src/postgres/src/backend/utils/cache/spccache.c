/*-------------------------------------------------------------------------
 *
 * spccache.c
 *	  Tablespace cache management.
 *
 * We cache the parsed version of spcoptions for each tablespace to avoid
 * needing to reparse on every lookup.  Right now, there doesn't appear to
 * be a measurable performance gain from doing this, but that might change
 * in the future as we add more options.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/cache/spccache.c
 *
 *-------------------------------------------------------------------------
 */
#include <float.h>

#include "postgres.h"

#include "access/reloptions.h"
#include "catalog/pg_tablespace.h"
#include "commands/tablespace.h"
#include "common/pg_yb_common.h"

#include "pg_yb_utils.h"

#include "miscadmin.h"
#include "optimizer/cost.h"
#include "storage/bufmgr.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/hsearch.h"
#include "utils/inval.h"
#include "utils/jsonfuncs.h"
#include "utils/memutils.h"
#include "utils/spccache.h"
#include "utils/syscache.h"


/* Hash table for information about each tablespace */
static HTAB *TableSpaceCacheHash = NULL;

typedef struct
{
	Oid			oid;			/* lookup key - must be first */
	union Opts_t
	{
		TableSpaceOpts *pg_opts;
		YBTableSpaceOpts *yb_opts;
	} opts; 					/* options, or NULL if none */
} TableSpaceCacheEntry;

/*
 * InvalidateTableSpaceCacheCallback
 *		Flush all cache entries when pg_tablespace is updated.
 *
 * When pg_tablespace is updated, we must flush the cache entry at least
 * for that tablespace.  Currently, we just flush them all.  This is quick
 * and easy and doesn't cost much, since there shouldn't be terribly many
 * tablespaces, nor do we expect them to be frequently modified.
 */
static void
InvalidateTableSpaceCacheCallback(Datum arg, int cacheid, uint32 hashvalue)
{
	HASH_SEQ_STATUS status;
	TableSpaceCacheEntry *spc;

	hash_seq_init(&status, TableSpaceCacheHash);
	while ((spc = (TableSpaceCacheEntry *) hash_seq_search(&status)) != NULL)
	{
		if (spc->opts.pg_opts)
			pfree(spc->opts.pg_opts);
		if (hash_search(TableSpaceCacheHash,
						(void *) &spc->oid,
						HASH_REMOVE,
						NULL) == NULL)
			elog(ERROR, "hash table corrupted");
	}
}

/*
 * InitializeTableSpaceCache
 *		Initialize the tablespace cache.
 */
static void
InitializeTableSpaceCache(void)
{
	HASHCTL		ctl;

	/* Initialize the hash table. */
	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(TableSpaceCacheEntry);
	TableSpaceCacheHash =
		hash_create("TableSpace cache", 16, &ctl,
					HASH_ELEM | HASH_BLOBS);

	/* Make sure we've initialized CacheMemoryContext. */
	if (!CacheMemoryContext)
		CreateCacheMemoryContext();

	/* Watch for invalidation events. */
	CacheRegisterSyscacheCallback(TABLESPACEOID,
								  InvalidateTableSpaceCacheCallback,
								  (Datum) 0);
}

/*
 * get_tablespace
 *		Fetch TableSpaceCacheEntry structure for a specified table OID.
 *
 * Pointers returned by this function should not be stored, since a cache
 * flush will invalidate them.
 */
static TableSpaceCacheEntry *
get_tablespace(Oid spcid)
{
	TableSpaceCacheEntry *spc;
	HeapTuple	tp;
	TableSpaceOpts *opts;

	/*
	 * Since spcid is always from a pg_class tuple, InvalidOid implies the
	 * default.
	 */
	if (spcid == InvalidOid)
		spcid = MyDatabaseTableSpace;

	/* Find existing cache entry, if any. */
	if (!TableSpaceCacheHash)
		InitializeTableSpaceCache();
	spc = (TableSpaceCacheEntry *) hash_search(TableSpaceCacheHash,
											   (void *) &spcid,
											   HASH_FIND,
											   NULL);
	if (spc)
		return spc;

	/*
	 * Not found in TableSpace cache.  Check catcache.  If we don't find a
	 * valid HeapTuple, it must mean someone has managed to request tablespace
	 * details for a non-existent tablespace.  We'll just treat that case as
	 * if no options were specified.
	 */
	tp = SearchSysCache1(TABLESPACEOID, ObjectIdGetDatum(spcid));
	if (!HeapTupleIsValid(tp))
		opts = NULL;
	else
	{
		Datum		datum;
		bool		isNull;

		datum = SysCacheGetAttr(TABLESPACEOID,
								tp,
								Anum_pg_tablespace_spcoptions,
								&isNull);
		if (isNull)
			opts = NULL;
		else
		{
			bytea *bytea_opts;
			if (IsYugaByteEnabled())
				bytea_opts = yb_tablespace_reloptions(datum, false);
			else
				bytea_opts = tablespace_reloptions(datum, false);

			opts = MemoryContextAlloc(CacheMemoryContext, VARSIZE(bytea_opts));
			memcpy(opts, bytea_opts, VARSIZE(bytea_opts));
		}
		ReleaseSysCache(tp);
	}

	/*
	 * Now create the cache entry.  It's important to do this only after
	 * reading the pg_tablespace entry, since doing so could cause a cache
	 * flush.
	 */
	spc = (TableSpaceCacheEntry *) hash_search(TableSpaceCacheHash,
											   (void *) &spcid,
											   HASH_ENTER,
											   NULL);

	/*
	 * Equivalent to spc->opts.yb_opts = opts as spc->opts is a union between
	 * yb_opts and pg_opts.
	 */
	spc->opts.pg_opts = opts;
	return spc;
}


/*
 * get_tablespace_distance
 *
 *		Returns a GeolocationDistance indicating how far away a given
 *		tablespace is from the current node.
 */
GeolocationDistance get_tablespace_distance(Oid spcid)
{
	Assert(IsYugaByteEnabled());
    if (spcid == InvalidOid)
       return UNKNOWN_DISTANCE;

	TableSpaceCacheEntry *spc = get_tablespace(spcid);
	if (spc->opts.yb_opts == NULL)
	{
		return UNKNOWN_DISTANCE;
	}

	const char *current_cloud = YBGetCurrentCloud();
	const char *current_region = YBGetCurrentRegion();
	const char *current_zone = YBGetCurrentZone();

	if (current_cloud == NULL || current_region == NULL || current_zone == NULL)
	{
		/* no placement info specified, so nothing to do */
		return UNKNOWN_DISTANCE;
	}

	MemoryContext tablespaceDistanceContext = AllocSetContextCreate(GetCurrentMemoryContext(),
														   "tablespace distance calculation",
														   ALLOCSET_SMALL_SIZES);

	MemoryContext oldContext = MemoryContextSwitchTo(tablespaceDistanceContext);

	/*
	 * The tablespace options json is stored as a payload after the header
	 * information in memory address pointed to by spc->opts.yb_opts. In other
	 * words, the json is stored sizeof(YBTableSpaceOpts) bytes after the
	 * memory adddress in spc->opts.yb_opts
	 */
	text *tsp_options_json = cstring_to_text((const char *)
								(spc->opts.yb_opts + 1));

	text *placement_array = json_get_value(tsp_options_json,
											"placement_blocks");
	const int length = get_json_array_length(placement_array);

	static char *cloudKey = "cloud";
	static char *regionKey = "region";
	static char *zoneKey = "zone";
	static char *leaderPrefKey = "leader_preference";

	GeolocationDistance farthest = ZONE_LOCAL;
	bool leader_pref_exists = false;

	for (size_t i = 0; i < length; i++)
	{
		text *json_element = get_json_array_element(placement_array, i);
		text *pref = json_get_denormalized_value(json_element, leaderPrefKey);
		bool preferred = (pref != NULL) && (atoi(text_to_cstring(pref)) == 1);

		/*
		 * YB: If we've seen a preferred placement,
		 * skip all non-preferred ones.
		 */
		if (!preferred && leader_pref_exists)
			continue;

		GeolocationDistance current_dist;
		const char *tsp_cloud = text_to_cstring(
			json_get_denormalized_value(json_element, cloudKey));
		const char *tsp_region = text_to_cstring(
			json_get_denormalized_value(json_element, regionKey));
		const char *tsp_zone = text_to_cstring(
			json_get_denormalized_value(json_element, zoneKey));


		/* are the current cloud and the given cloud the same */
		if (strcmp(tsp_cloud, current_cloud) == 0)
		{
			/* are the current region and the given region the same */
			if (strcmp(tsp_region, current_region) == 0)
			{
				/* are the current cloud and the given zone the same */
				if (strcmp(tsp_zone, current_zone) == 0)
				{
					current_dist = ZONE_LOCAL;
				}
				else
				{
					current_dist = REGION_LOCAL;
				}
			}
			else
			{
				current_dist = CLOUD_LOCAL;
			}
		}
		else
		{
			current_dist = INTER_CLOUD;
		}

		/*
		 * YB: If this is the first preferred placement we find,
		 * disregard all previous placements.
		 */
		if (preferred && !leader_pref_exists)
		{
			leader_pref_exists = true;
			farthest = current_dist;
		}
		else
		{
			farthest = current_dist > farthest ? current_dist : farthest;
		}
	}
	MemoryContextSwitchTo(oldContext);
	MemoryContextDelete(tablespaceDistanceContext);

	return farthest;
}

/*
 * get_yb_tablespace_cost
 *
 *		Costs per-tuple access on a given tablespace. Currently we score a
 *		placement option in a tablespace by assigning a cost based on its
 *		distance that is denoted by a GeolocationDistance. The computed cost
 *		is stored in yb_tsp_cost. Returns false iff geolocation costing is
 *		disabled or a NULL pointer was passed in for yb_tsp_cost.
 */
bool get_yb_tablespace_cost(Oid spcid, double *yb_tsp_cost)
{
	if (!yb_enable_geolocation_costing)
	{
		return false;
	}

	Assert(IsYugaByteEnabled());

	if (!yb_tsp_cost)
	{
		return false;
	}

	GeolocationDistance distance = get_tablespace_distance(spcid);
	double cost;
	switch (distance)
	{
		case UNKNOWN_DISTANCE:
			switch_fallthrough();
		case INTER_CLOUD:
			cost = yb_intercloud_cost;
			break;
		case CLOUD_LOCAL:
			cost = yb_interregion_cost;
			break;
		case REGION_LOCAL:
			cost = yb_interzone_cost;
			break;
		case ZONE_LOCAL:
			cost = yb_local_cost;
			break;
	}

	*yb_tsp_cost = cost;
	return true;
}

/*
 * get_tablespace_page_costs
 *		Return random and/or sequential page costs for a given tablespace.
 *
 *		This value is not locked by the transaction, so this value may
 *		be changed while a SELECT that has used these values for planning
 *		is still executing.
 */
void
get_tablespace_page_costs(Oid spcid,
						  double *spc_random_page_cost,
						  double *spc_seq_page_cost)
{
	TableSpaceCacheEntry *spc = get_tablespace(spcid);

	if (spc_random_page_cost)
	{
		if (!spc->opts.pg_opts || spc->opts.pg_opts->random_page_cost < 0
			|| IsYugaByteEnabled())
			*spc_random_page_cost = random_page_cost;
		else
			*spc_random_page_cost = spc->opts.pg_opts->random_page_cost;
	}

	if (spc_seq_page_cost)
	{
		if (!spc->opts.pg_opts || spc->opts.pg_opts->seq_page_cost < 0
			|| IsYugaByteEnabled())
			*spc_seq_page_cost = seq_page_cost;
		else
			*spc_seq_page_cost = spc->opts.pg_opts->seq_page_cost;
	}
}

/*
 * get_tablespace_io_concurrency
 *
 *		This value is not locked by the transaction, so this value may
 *		be changed while a SELECT that has used these values for planning
 *		is still executing.
 */
int
get_tablespace_io_concurrency(Oid spcid)
{
	TableSpaceCacheEntry *spc = get_tablespace(spcid);

	if (!spc->opts.pg_opts || spc->opts.pg_opts->effective_io_concurrency < 0)
		return effective_io_concurrency;
	else
		return spc->opts.pg_opts->effective_io_concurrency;
}
