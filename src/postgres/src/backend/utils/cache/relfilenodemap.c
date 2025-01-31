/*-------------------------------------------------------------------------
 *
 * relfilenodemap.c
 *	  relfilenode to oid mapping cache.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/cache/relfilenodemap.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "catalog/pg_class.h"
#include "catalog/pg_tablespace.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/inval.h"
#include "utils/rel.h"
#include "utils/relfilenodemap.h"
#include "utils/relmapper.h"

/* Hash table for information about each relfilenode <-> oid pair */
static HTAB *RelfilenodeMapHash = NULL;

/* built first time through in InitializeRelfilenodeMap */
static ScanKeyData relfilenode_skey[2];

typedef struct
{
	Oid			reltablespace;
	Oid			relfilenode;
} RelfilenodeMapKey;

typedef struct
{
	RelfilenodeMapKey key;		/* lookup key - must be first */
	Oid			relid;			/* pg_class.oid */
} RelfilenodeMapEntry;

/*
 * RelfilenodeMapInvalidateCallback
 *		Flush mapping entries when pg_class is updated in a relevant fashion.
 */
static void
RelfilenodeMapInvalidateCallback(Datum arg, Oid relid)
{
	HASH_SEQ_STATUS status;
	RelfilenodeMapEntry *entry;

	/* callback only gets registered after creating the hash */
	Assert(RelfilenodeMapHash != NULL);

	hash_seq_init(&status, RelfilenodeMapHash);
	while ((entry = (RelfilenodeMapEntry *) hash_seq_search(&status)) != NULL)
	{
		/*
		 * If relid is InvalidOid, signaling a complete reset, we must remove
		 * all entries, otherwise just remove the specific relation's entry.
		 * Always remove negative cache entries.
		 */
		if (relid == InvalidOid ||	/* complete reset */
			entry->relid == InvalidOid ||	/* negative cache entry */
			entry->relid == relid)	/* individual flushed relation */
		{
			if (hash_search(RelfilenodeMapHash,
							(void *) &entry->key,
							HASH_REMOVE,
							NULL) == NULL)
				elog(ERROR, "hash table corrupted");
		}
	}
}

/*
 * InitializeRelfilenodeMap
 *		Initialize cache, either on first use or after a reset.
 */
static void
InitializeRelfilenodeMap(void)
{
	HASHCTL		ctl;
	int			i;

	/* Make sure we've initialized CacheMemoryContext. */
	if (CacheMemoryContext == NULL)
		CreateCacheMemoryContext();

	/* build skey */
	MemSet(&relfilenode_skey, 0, sizeof(relfilenode_skey));

	for (i = 0; i < 2; i++)
	{
		fmgr_info_cxt(F_OIDEQ,
					  &relfilenode_skey[i].sk_func,
					  CacheMemoryContext);
		relfilenode_skey[i].sk_strategy = BTEqualStrategyNumber;
		relfilenode_skey[i].sk_subtype = InvalidOid;
		relfilenode_skey[i].sk_collation = InvalidOid;
	}

	relfilenode_skey[0].sk_attno = Anum_pg_class_reltablespace;
	relfilenode_skey[1].sk_attno = Anum_pg_class_relfilenode;

	/*
	 * Only create the RelfilenodeMapHash now, so we don't end up partially
	 * initialized when fmgr_info_cxt() above ERRORs out with an out of memory
	 * error.
	 */
	ctl.keysize = sizeof(RelfilenodeMapKey);
	ctl.entrysize = sizeof(RelfilenodeMapEntry);
	ctl.hcxt = CacheMemoryContext;

	RelfilenodeMapHash =
		hash_create("RelfilenodeMap cache", 64, &ctl,
					HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

	/* Watch for invalidation events. */
	CacheRegisterRelcacheCallback(RelfilenodeMapInvalidateCallback,
								  (Datum) 0);
}

/*
 * Map a relation's (tablespace, filenode) to a relation's oid and cache the
 * result.
 *
 * Returns InvalidOid if no relation matching the criteria could be found.
 */
Oid
RelidByRelfilenode(Oid reltablespace, Oid relfilenode)
{
	RelfilenodeMapKey key;
	RelfilenodeMapEntry *entry;
	bool		found;
	SysScanDesc scandesc;
	Relation	relation;
	HeapTuple	ntp;
	ScanKeyData skey[2];
	Oid			relid;

	if (RelfilenodeMapHash == NULL)
		InitializeRelfilenodeMap();

	/* pg_class will show 0 when the value is actually MyDatabaseTableSpace */
	if (reltablespace == MyDatabaseTableSpace)
		reltablespace = 0;

	MemSet(&key, 0, sizeof(key));
	key.reltablespace = reltablespace;
	key.relfilenode = relfilenode;

	/*
	 * Check cache and return entry if one is found. Even if no target
	 * relation can be found later on we store the negative match and return a
	 * InvalidOid from cache. That's not really necessary for performance
	 * since querying invalid values isn't supposed to be a frequent thing,
	 * but it's basically free.
	 */
	entry = hash_search(RelfilenodeMapHash, (void *) &key, HASH_FIND, &found);

	if (found)
		return entry->relid;

	/* ok, no previous cache entry, do it the hard way */

	/* initialize empty/negative cache entry before doing the actual lookups */
	relid = InvalidOid;

	if (reltablespace == GLOBALTABLESPACE_OID)
	{
		/*
		 * Ok, shared table, check relmapper.
		 */
		relid = RelationMapFilenodeToOid(relfilenode, true);
	}
	else
	{
		/*
		 * Not a shared table, could either be a plain relation or a
		 * non-shared, nailed one, like e.g. pg_class.
		 */

		/* check for plain relations by looking in pg_class */
		relation = table_open(RelationRelationId, AccessShareLock);

		/* copy scankey to local copy, it will be modified during the scan */
		memcpy(skey, relfilenode_skey, sizeof(skey));

		/* set scan arguments */
		skey[0].sk_argument = ObjectIdGetDatum(reltablespace);
		skey[1].sk_argument = ObjectIdGetDatum(relfilenode);

		scandesc = systable_beginscan(relation,
									  ClassTblspcRelfilenodeIndexId,
									  true,
									  NULL,
									  2,
									  skey);

		found = false;

		while (HeapTupleIsValid(ntp = systable_getnext(scandesc)))
		{
			Form_pg_class classform = (Form_pg_class) GETSTRUCT(ntp);

			if (found)
				elog(ERROR,
					 "unexpected duplicate for tablespace %u, relfilenode %u",
					 reltablespace, relfilenode);
			found = true;

			Assert(classform->reltablespace == reltablespace);
			Assert(classform->relfilenode == relfilenode);
			relid = classform->oid;
		}

		systable_endscan(scandesc);
		table_close(relation, AccessShareLock);

		/* check for tables that are mapped but not shared */
		if (!found)
			relid = RelationMapFilenodeToOid(relfilenode, false);
	}

	/*
	 * Only enter entry into cache now, our opening of pg_class could have
	 * caused cache invalidations to be executed which would have deleted a
	 * new entry if we had entered it above.
	 */
	entry = hash_search(RelfilenodeMapHash, (void *) &key, HASH_ENTER, &found);
	if (found)
		elog(ERROR, "corrupted hashtable");
	entry->relid = relid;

	return relid;
}
