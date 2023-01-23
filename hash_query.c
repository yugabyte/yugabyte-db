/*-------------------------------------------------------------------------
 *
 * hash_query.c
 *		Track statement execution times across a whole database cluster.
 *
 * Portions Copyright © 2018-2020, Percona LLC and/or its affiliates
 *
 * Portions Copyright (c) 1996-2020, PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * IDENTIFICATION
 *	  contrib/pg_stat_monitor/hash_query.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "nodes/pg_list.h"
#include "pg_stat_monitor.h"

static pgsmLocalState	pgsmStateLocal;
static PGSM_HASH_TABLE_HANDLE pgsm_create_bucket_hash(pgssSharedState *pgss, dsa_area *dsa);
static Size pgsm_get_shared_area_size(void);

#if USE_DYNAMIC_HASH
/* parameter for the shared hash */
static dshash_parameters dsh_params = {
     sizeof(pgssHashKey),
     sizeof(pgssEntry),
     dshash_memcmp,
     dshash_memhash
};
#endif

/*
 * Returns the shared memory area size for storing the query texts.
 * USE_DYNAMIC_HASH also creates the hash table in the same memory space,
 * so add the required bucket memory size to the query text area size
 */

static Size
pgsm_query_area_size(void)
{
	Size	sz = MAX_QUERY_BUF;
	#if USE_DYNAMIC_HASH
	/* Dynamic hash also lives DSA area */
	sz = add_size(sz, MAX_BUCKETS_MEM);
	#endif
	return MAXALIGN(sz);
}
/*
 * Total shared memory area required by pgsm
 */
Size
pgsm_ShmemSize(void)
{
    Size	sz = MAXALIGN(sizeof(pgssSharedState));
    sz = add_size(sz, MAX_QUERY_BUF);
	#if USE_DYNAMIC_HASH
	sz = add_size(sz, MAX_BUCKETS_MEM);
	#else
	sz = add_size(sz, hash_estimate_size(MAX_BUCKET_ENTRIES, sizeof(pgssEntry)));
	#endif
    return MAXALIGN(sz);
}

/*
 * Returns the shared memory area size for storing the query texts and pgsm
 * shared state structure,
 * Moreover, for USE_DYNAMIC_HASH, both the hash table and raw query text area
 * get allocated as a single shared memory chunk.
 */
static Size
pgsm_get_shared_area_size(void)
{
	Size	sz;
	#if USE_DYNAMIC_HASH
		sz = pgsm_ShmemSize();
	#else
		sz = MAXALIGN(sizeof(pgssSharedState));
		sz = add_size(sz, pgsm_query_area_size());
	#endif
	return sz;
}

void
pgss_startup(void)
{
	bool		found = false;
	pgssSharedState *pgss;
	/* reset in case this is a restart within the postmaster */
	pgsmStateLocal.dsa = NULL;
	pgsmStateLocal.shared_hash = NULL;
	pgsmStateLocal.shared_pgssState = NULL;

	/*
	 * Create or attach to the shared memory state, including hash table
	 */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	pgss = ShmemInitStruct("pg_stat_monitor", pgsm_get_shared_area_size(), &found);
	if (!found)
	{
		/* First time through ... */
		dsa_area   *dsa;
		char	*p = (char *) pgss;

		pgss->lock = &(GetNamedLWLockTranche("pg_stat_monitor"))->lock;
		SpinLockInit(&pgss->mutex);
		ResetSharedState(pgss);
		/* the allocation of pgssSharedState itself */
		p += MAXALIGN(sizeof(pgssSharedState));
		pgss->raw_dsa_area = p;
		dsa = dsa_create_in_place(pgss->raw_dsa_area,
                                   pgsm_query_area_size(),
                                   LWLockNewTrancheId(), 0);
		dsa_pin(dsa);
		dsa_set_size_limit(dsa, pgsm_query_area_size());

		pgss->hash_handle = pgsm_create_bucket_hash(pgss,dsa);

		/* If overflow is enabled, set the DSA size to unlimited,
		 * and allow the DSA to grow beyond the shared memory space
		 * into the swap area*/
		if (PGSM_OVERFLOW_TARGET == OVERFLOW_TARGET_DISK)
			dsa_set_size_limit(dsa, -1);

		pgsmStateLocal.shared_pgssState = pgss;
		/*
		* Postmaster will never access the dsa again, thus free it's local
		* references.
		*/
        dsa_detach(dsa);
	}

#ifdef BENCHMARK
	init_hook_stats();
#endif

	LWLockRelease(AddinShmemInitLock);

	/*
	 * If we're in the postmaster (or a standalone backend...), set up a shmem
	 * exit hook to dump the statistics to disk.
	 */
	on_shmem_exit(pgss_shmem_shutdown, (Datum) 0);
}

/*
 * Create the classic or dshahs hash table for storing the query statistics.
 */
static PGSM_HASH_TABLE_HANDLE
pgsm_create_bucket_hash(pgssSharedState *pgss, dsa_area *dsa)
{
	PGSM_HASH_TABLE_HANDLE	bucket_hash;

#if USE_DYNAMIC_HASH
	dshash_table *dsh;
	pgss->hash_tranche_id = LWLockNewTrancheId();
	dsh_params.tranche_id = pgss->hash_tranche_id;
	dsh = dshash_create(dsa, &dsh_params, 0);
	bucket_hash = dshash_get_hash_table_handle(dsh);
	dshash_detach(dsh);
#else
	HASHCTL	info;
	memset(&info, 0, sizeof(info));
	info.keysize = sizeof(pgssHashKey);
	info.entrysize = sizeof(pgssEntry);
	bucket_hash = ShmemInitHash("pg_stat_monitor: bucket hashtable", MAX_BUCKET_ENTRIES, MAX_BUCKET_ENTRIES, &info, HASH_ELEM | HASH_BLOBS);
#endif
	return bucket_hash;
}

/*
 * Attach to a DSA area created by the postmaster, in the case of 
 * USE_DYNAMIC_HASH, also attach the local dshash handle to
 * the dshash created by the postmaster.
 *
 * Note: The dsa area and dshash for the process may be mapped at a
 * different virtual address in this process.
 *
 */
void
pgsm_attach_shmem(void)
{
	MemoryContext oldcontext;
	if (pgsmStateLocal.dsa)
		return;

	/*
	 * We want the dsa to remain valid throughout the lifecycle of this process.
	 * so switch to TopMemoryContext before attaching
	 */
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);

	pgsmStateLocal.dsa = dsa_attach_in_place(pgsmStateLocal.shared_pgssState->raw_dsa_area,
                                           NULL);
	/* pin the attached area to keep the area attached until end of
	 * session or explicit detach.
	 */
	dsa_pin_mapping(pgsmStateLocal.dsa);

#if USE_DYNAMIC_HASH
	dsh_params.tranche_id = pgsmStateLocal.shared_pgssState->hash_tranche_id;
	pgsmStateLocal.shared_hash = dshash_attach(pgsmStateLocal.dsa, &dsh_params,
                                             pgsmStateLocal.shared_pgssState->hash_handle, 0);
#else
	pgsmStateLocal.shared_hash = pgsmStateLocal.shared_pgssState->hash_handle;
#endif

	MemoryContextSwitchTo(oldcontext);
}

dsa_area*
get_dsa_area_for_query_text(void)
{
	pgsm_attach_shmem();
	return pgsmStateLocal.dsa;
}

PGSM_HASH_TABLE*
get_pgssHash(void)
{
	pgsm_attach_shmem();
	return pgsmStateLocal.shared_hash;
}

pgssSharedState *
pgsm_get_ss(void)
{
	pgsm_attach_shmem();
	return pgsmStateLocal.shared_pgssState;
}


/*
 * shmem_shutdown hook: Dump statistics into file.
 *
 * Note: we don't bother with acquiring lock, because there should be no
 * other processes running when this is called.
 */
void
pgss_shmem_shutdown(int code, Datum arg)
{
	/* Don't try to dump during a crash. */
	elog(LOG,"pgss_shmem_shutdown");
	if (code)
		return;

	pgsmStateLocal.shared_pgssState = NULL;
	/* Safety check ... shouldn't get here unless shmem is set up. */
	if (!IsHashInitialize())
		return;
}

pgssEntry *
hash_entry_alloc(pgssSharedState *pgss, pgssHashKey *key, int encoding)
{
	pgssEntry  *entry = NULL;
	bool		found = false;
	/* Find or create an entry with desired hash code */
	entry = (pgssEntry*) pgsm_hash_find_or_insert(pgsmStateLocal.shared_hash, key, &found);
	if (entry == NULL)
		elog(DEBUG1, "hash_entry_alloc: OUT OF MEMORY");
	else if (!found)
	{
		pgss->bucket_entry[pg_atomic_read_u64(&pgss->current_wbucket)]++;
		/* New entry, initialize it */
		/* reset the statistics */
		memset(&entry->counters, 0, sizeof(Counters));
		entry->query_pos = InvalidDsaPointer;
		entry->counters.info.parent_query = InvalidDsaPointer;

		/* set the appropriate initial usage count */
		/* re-initialize the mutex each time ... we assume no one using it */
		SpinLockInit(&entry->mutex);
		/* ... and don't forget the query text metadata */
		entry->encoding = encoding;
	}
	#if USE_DYNAMIC_HASH
	if(entry)
		dshash_release_lock(pgsmStateLocal.shared_hash, entry);
	#endif

	return entry;
}

/*
 * Prepare resources for using the new bucket:
 *    - Deallocate finished hash table entries in new_bucket_id (entries whose
 *      state is PGSS_FINISHED or PGSS_FINISHED).
 *    - Clear query buffer for new_bucket_id.
 *    - If old_bucket_id != -1, move all pending hash table entries in
 *      old_bucket_id to the new bucket id, also move pending queries from the
 *      previous query buffer (query_buffer[old_bucket_id]) to the new one
 *      (query_buffer[new_bucket_id]).
 *
 * Caller must hold an exclusive lock on pgss->lock.
 */
void
hash_entry_dealloc(int new_bucket_id, int old_bucket_id, unsigned char *query_buffer)
{
	PGSM_HASH_SEQ_STATUS hstat;
	pgssEntry  *entry = NULL;
	/* Store pending query ids from the previous bucket. */
	List	   *pending_entries = NIL;
	ListCell   *pending_entry;

	if (!pgsmStateLocal.shared_hash)
		return;

	/* Iterate over the hash table. */
	pgsm_hash_seq_init(&hstat, pgsmStateLocal.shared_hash, true);

	while ((entry = pgsm_hash_seq_next(&hstat)) != NULL)
	{
		dsa_pointer pdsa;

		/*
		 * Remove all entries if new_bucket_id == -1. Otherwise remove entry
		 * in new_bucket_id if it has finished already.
		 */
		if (new_bucket_id < 0 ||
			(entry->key.bucket_id == new_bucket_id &&
			 (entry->counters.state == PGSS_FINISHED || entry->counters.state == PGSS_ERROR)))
		{
			dsa_pointer parent_qdsa = entry->counters.info.parent_query;
			pdsa = entry->query_pos;

			pgsm_hash_delete_current(&hstat, pgsmStateLocal.shared_hash, &entry->key);

			if (DsaPointerIsValid(pdsa))
				dsa_free(pgsmStateLocal.dsa, pdsa);

			if (DsaPointerIsValid(parent_qdsa))
				dsa_free(pgsmStateLocal.dsa, parent_qdsa);
			continue;
		}

		/*
		 * If we detect a pending query residing in the previous bucket id, we
		 * add it to a list of pending elements to be moved to the new bucket
		 * id. Can't update the hash table while iterating it inside this
		 * loop, as this may introduce all sort of problems.
		 */
		if (old_bucket_id != -1 && entry->key.bucket_id == old_bucket_id)
		{
			if (entry->counters.state == PGSS_PARSE ||
				entry->counters.state == PGSS_PLAN ||
				entry->counters.state == PGSS_EXEC)
			{
				pgssEntry  *bkp_entry = malloc(sizeof(pgssEntry));

				if (!bkp_entry)
				{
					elog(DEBUG1, "hash_entry_dealloc: out of memory");

					/*
					 * No memory, If the entry has calls > 1 then we change
					 * the state to finished, as the pending query will likely
					 * finish execution during the new bucket time window. The
					 * pending query will vanish in this case, can't list it
					 * until it completes.
					 *
					 * If there is only one call to the query and it's
					 * pending, remove the entry from the previous bucket and
					 * allow it to finish in the new bucket, in order to avoid
					 * the query living in the old bucket forever.
					 */
					if (entry->counters.calls.calls > 1)
						entry->counters.state = PGSS_FINISHED;
					else
					{
						pdsa = entry->query_pos;
						pgsm_hash_delete_current(&hstat, pgsmStateLocal.shared_hash, &entry->key);
						if (DsaPointerIsValid(pdsa))
							dsa_free(pgsmStateLocal.dsa, pdsa);
					}
					continue;
				}

				/* Save key/data from the previous entry. */
				memcpy(bkp_entry, entry, sizeof(pgssEntry));

				/* Update key to use the new bucket id. */
				bkp_entry->key.bucket_id = new_bucket_id;

				/* Add the entry to a list of nodes to be processed later. */
				pending_entries = lappend(pending_entries, bkp_entry);

				/*
				 * If the entry has calls > 1 then we change the state to
				 * finished in the previous bucket, as the pending query will
				 * likely finish execution during the new bucket time window.
				 * Can't remove it from the previous bucket as it may have
				 * many calls and we would lose the query statistics.
				 *
				 * If there is only one call to the query and it's pending,
				 * remove the entry from the previous bucket and allow it to
				 * finish in the new bucket, in order to avoid the query
				 * living in the old bucket forever.
				 */
				if (entry->counters.calls.calls > 1)
					entry->counters.state = PGSS_FINISHED;
				else
				{
					pdsa = entry->query_pos;
					pgsm_hash_delete_current(&hstat, pgsmStateLocal.shared_hash, &entry->key);
					/* We should not delete the Query in DSA here
					 * as the same will get reused when the entry gets inserted into new bucket
					 */
				}
			}
		}
	}
	pgsm_hash_seq_term(&hstat);
	/*
	 * Iterate over the list of pending queries in order to add them back to
	 * the hash table with the updated bucket id.
	 */
	foreach(pending_entry, pending_entries)
	{
		bool		found = false;
		pgssEntry  *new_entry;
		pgssEntry  *old_entry = (pgssEntry *) lfirst(pending_entry);


		PGSM_DISABLE_ERROR_CAPUTRE();
		{
			new_entry = (pgssEntry*) pgsm_hash_find_or_insert(pgsmStateLocal.shared_hash, &old_entry->key, &found);
		}PGSM_END_DISABLE_ERROR_CAPTURE();

		if (new_entry == NULL)
			elog(DEBUG1, "%s", "pg_stat_monitor: out of memory");
		else if (!found)
		{
			/* Restore counters and other data. */
			new_entry->counters = old_entry->counters;
			SpinLockInit(&new_entry->mutex);
			new_entry->encoding = old_entry->encoding;
			new_entry->query_pos = old_entry->query_pos;
		}
		#if USE_DYNAMIC_HASH
		if(new_entry)
			dshash_release_lock(pgsmStateLocal.shared_hash, new_entry);
		#endif
		free(old_entry);
	}
	list_free(pending_entries);
}

/*
 * Release all entries.
 */
void
hash_entry_reset()
{
	pgssSharedState *pgss = pgsm_get_ss();
	PGSM_HASH_SEQ_STATUS hstat;
	pgssEntry  *entry;

	LWLockAcquire(pgss->lock, LW_EXCLUSIVE);

	pgsm_hash_seq_init(&hstat, pgsmStateLocal.shared_hash, true);

	while ((entry = pgsm_hash_seq_next(&hstat)) != NULL)
	{
		dsa_pointer pdsa = entry->query_pos;
		pgsm_hash_delete_current(&hstat, pgsmStateLocal.shared_hash, &entry->key);
		if (DsaPointerIsValid(pdsa))
			dsa_free(pgsmStateLocal.dsa, pdsa);
	}

	pgsm_hash_seq_term(&hstat);

	pg_atomic_write_u64(&pgss->current_wbucket, 0);
	LWLockRelease(pgss->lock);
}

bool
IsHashInitialize(void)
{
	return (pgsmStateLocal.shared_pgssState != NULL);
}

/*
 * pgsm_* functions are just wrapper functions over the hash table standard
 * API and call the appropriate hash table function based on USE_DYNAMIC_HASH
 */

void *
pgsm_hash_find_or_insert(PGSM_HASH_TABLE *shared_hash, pgssHashKey *key, bool* found)
{
	#if USE_DYNAMIC_HASH
	void *entry;
	entry = dshash_find_or_insert(shared_hash, key, found);
	return entry;
	#else
	return hash_search(shared_hash, key, HASH_ENTER_NULL, found);
	#endif
}

void *
pgsm_hash_find(PGSM_HASH_TABLE *shared_hash, pgssHashKey *key, bool* found)
{
	#if USE_DYNAMIC_HASH
	return dshash_find(shared_hash, key, false);
	#else
	return hash_search(shared_hash, key, HASH_FIND, found);
	#endif
}

void
pgsm_hash_seq_init(PGSM_HASH_SEQ_STATUS *hstat, PGSM_HASH_TABLE *shared_hash, bool lock)
{
#if USE_DYNAMIC_HASH
	dshash_seq_init(hstat, shared_hash, lock);
#else
	hash_seq_init(hstat, shared_hash);
#endif
}

void*
pgsm_hash_seq_next(PGSM_HASH_SEQ_STATUS *hstat)
{
#if USE_DYNAMIC_HASH
	return dshash_seq_next(hstat);
#else
	return hash_seq_search(hstat);
#endif
}

void
pgsm_hash_seq_term(PGSM_HASH_SEQ_STATUS *hstat)
{
#if USE_DYNAMIC_HASH
	dshash_seq_term(hstat);
#endif
}

void
pgsm_hash_delete_current(PGSM_HASH_SEQ_STATUS *hstat, PGSM_HASH_TABLE *shared_hash, void *key)
{
	#if USE_DYNAMIC_HASH
	dshash_delete_current(hstat);
	#else
	hash_search(shared_hash, key, HASH_REMOVE, NULL);
	#endif
}