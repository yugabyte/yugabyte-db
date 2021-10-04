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


static pgssSharedState *pgss;
static HTAB *pgss_hash;
static HTAB *pgss_query_hash;

static HTAB* hash_init(const char *hash_name, int key_size, int entry_size, int hash_size);
/*
 * Copy all queries from query_buffer[old_bucket_id] to query_buffer[new_bucket_id]
 * whose query ids are found in the array 'query_ids', of length 'n_queries'.
 */
static void copy_queries(unsigned char *query_buffer[],
						uint64 new_bucket_id,
						uint64 old_bucket_id,
						uint64 *query_ids,
						size_t n_queries);

static HTAB*
hash_init(const char *hash_name, int key_size, int entry_size, int hash_size)
{
	HASHCTL info;
	memset(&info, 0, sizeof(info));
	info.keysize = key_size;
	info.entrysize = entry_size;
	return ShmemInitHash(hash_name, hash_size, hash_size, &info, HASH_ELEM | HASH_BLOBS);
}

void
pgss_startup(void)
{
	bool		found = false;
	int32		i;

	/* reset in case this is a restart within the postmaster */

	pgss = NULL;
	pgss_hash = NULL;
	pgss_query_hash = NULL;

	/*
	* Create or attach to the shared memory state, including hash table
	*/
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	pgss = ShmemInitStruct("pg_stat_monitor", sizeof(pgssSharedState), &found);
	if (!found)
	{
		/* First time through ... */
		pgss->lock = &(GetNamedLWLockTranche("pg_stat_monitor"))->lock;
		SpinLockInit(&pgss->mutex);
		ResetSharedState(pgss);
	}

#ifdef BENCHMARK
	init_hook_stats();
#endif

	pgss->query_buf_size_bucket = MAX_QUERY_BUF / PGSM_MAX_BUCKETS;

	for (i = 0; i < PGSM_MAX_BUCKETS; i++)
	{
		unsigned char *buf = (unsigned char *)ShmemAlloc(pgss->query_buf_size_bucket);
		set_qbuf(i, buf);
		memset(buf, 0, sizeof (uint64));
	}

	pgss_hash = hash_init("pg_stat_monitor: bucket hashtable", sizeof(pgssHashKey), sizeof(pgssEntry), MAX_BUCKET_ENTRIES);
	pgss_query_hash = hash_init("pg_stat_monitor: query hashtable", sizeof(pgssQueryHashKey), sizeof(pgssQueryEntry),MAX_BUCKET_ENTRIES);

	LWLockRelease(AddinShmemInitLock);

	/*
	 * If we're in the postmaster (or a standalone backend...), set up a shmem
	 * exit hook to dump the statistics to disk.
	 */
	on_shmem_exit(pgss_shmem_shutdown, (Datum) 0);
}

pgssSharedState*
pgsm_get_ss(void)
{
	return pgss;
}

HTAB*
pgsm_get_hash(void)
{
	return pgss_hash;
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
	if (code)
		return;

	pgss = NULL;
	/* Safety check ... shouldn't get here unless shmem is set up. */
	if (!IsHashInitialize())
		return;
}

Size
hash_memsize(void)
{
	Size	size;

	size = MAXALIGN(sizeof(pgssSharedState));
	size += MAXALIGN(MAX_QUERY_BUF);
	size = add_size(size, hash_estimate_size(MAX_BUCKET_ENTRIES, sizeof(pgssEntry)));
	size = add_size(size, hash_estimate_size(MAX_BUCKET_ENTRIES, sizeof(pgssQueryEntry)));

	return size;
}

pgssEntry *
hash_entry_alloc(pgssSharedState *pgss, pgssHashKey *key, int encoding)
{
	pgssEntry	*entry = NULL;
	bool		found = false;

	if (hash_get_num_entries(pgss_hash) >= MAX_BUCKET_ENTRIES)
	{
		elog(DEBUG1, "%s", "pg_stat_monitor: out of memory");
		return NULL;
	}
	/* Find or create an entry with desired hash code */
	entry = (pgssEntry *) hash_search(pgss_hash, key, HASH_ENTER_NULL, &found);
	if (!found)
	{
		pgss->bucket_entry[pg_atomic_read_u64(&pgss->current_wbucket)]++;
		/* New entry, initialize it */
		/* reset the statistics */
		memset(&entry->counters, 0, sizeof(Counters));
		/* set the appropriate initial usage count */
		/* re-initialize the mutex each time ... we assume no one using it */
		SpinLockInit(&entry->mutex);
		/* ... and don't forget the query text metadata */
		entry->encoding = encoding;
	}
	if (entry == NULL)
		elog(DEBUG1, "%s", "pg_stat_monitor: out of memory");
	return entry;
}
/*
 * Reset all the entries.
 *
 * Caller must hold an exclusive lock on pgss->lock.
 */
void
hash_query_entryies_reset()
{
	HASH_SEQ_STATUS 	hash_seq;
	pgssQueryEntry      *entry;

	hash_seq_init(&hash_seq, pgss_query_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
		entry = hash_search(pgss_query_hash, &entry->key, HASH_REMOVE, NULL);
}


/*
 * Deallocate finished entries in new_bucket_id.
 *
 * Move all pending queries in query_buffer[old_bucket_id] to
 * query_buffer[new_bucket_id].
 *
 * Caller must hold an exclusive lock on pgss->lock.
 */
void
hash_query_entry_dealloc(int new_bucket_id, int old_bucket_id, unsigned char *query_buffer[])
{
	HASH_SEQ_STATUS 	hash_seq;
	pgssQueryEntry      *entry;
	pgssSharedState     *pgss = pgsm_get_ss();
	/*
	 * Store pending query ids from the previous bucket.
	 * If there are more pending queries than MAX_PENDING_QUERIES then
	 * we try to dynamically allocate memory for them.
	 */
#define MAX_PENDING_QUERIES 128
	uint64				pending_query_ids[MAX_PENDING_QUERIES];
	uint64				*pending_query_ids_buf = NULL;
	size_t				n_pending_queries = 0;
	bool				out_of_memory = false;

	/* Clear all queries in the query buffer for the new bucket. */
	memset(query_buffer[new_bucket_id], 0, pgss->query_buf_size_bucket);

	hash_seq_init(&hash_seq, pgss_query_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		/* Remove previous finished query entries matching new bucket id. */
		if (entry->key.bucket_id == new_bucket_id)
		{
			if (entry->state == PGSS_FINISHED || entry->state == PGSS_ERROR)
			{
				entry = hash_search(pgss_query_hash, &entry->key, HASH_REMOVE, NULL);
			}
		}
		/* Set up a list of pending query ids from the previous bucket. */
		else if (entry->key.bucket_id == old_bucket_id &&
					(entry->state == PGSS_PARSE ||
					entry->state == PGSS_PLAN ||
					entry->state == PGSS_EXEC))
		{
			if (n_pending_queries < MAX_PENDING_QUERIES)
			{
				pending_query_ids[n_pending_queries] = entry->key.queryid;
				++n_pending_queries;
			}
			else
			{
				/*
				 * No. of pending queries exceeds MAX_PENDING_QUERIES.
				 * Try to allocate memory from heap to keep track of pending query ids.
				 * If allocation fails we manually copy pending query to the next query buffer.
				 */
				if (!out_of_memory && !pending_query_ids_buf)
				{
					/* Allocate enough room for query ids. */
					pending_query_ids_buf = malloc(sizeof(uint64) * hash_get_num_entries(pgss_query_hash));
					if (pending_query_ids_buf != NULL)
						memcpy(pending_query_ids_buf, pending_query_ids, n_pending_queries * sizeof(uint64));
					else
						out_of_memory = true;
				}

				if (!out_of_memory)
				{
					/* Store pending query id in the dynamic buffer. */
					pending_query_ids_buf[n_pending_queries] = entry->key.queryid;
					++n_pending_queries;
				}
				else
				{
					/* No memory, manually copy query from previous buffer. */
					char query_txt[1024];

					if (read_query(query_buffer[old_bucket_id], old_bucket_id, entry->key.queryid, query_txt) != 0
						|| read_query_buffer(old_bucket_id, entry->key.queryid, query_txt) == MAX_QUERY_BUFFER_BUCKET)
					{
						SaveQueryText(new_bucket_id, entry->key.queryid, query_buffer[new_bucket_id], query_txt, strlen(query_txt));
					}
					else
						/* There was no space available to store the pending query text. */
						elog(WARNING, "hash_query_entry_dealloc: Failed to move pending query %lX, %s",
								entry->key.queryid,
								(PGSM_OVERFLOW_TARGET == OVERFLOW_TARGET_NONE) ?
									"insufficient shared space for query" :
									"I/O error reading query from disk");
				}
			}
		}
	}

	/* Copy all detected pending queries from previous bucket id to the new one. */
	if (n_pending_queries > 0) {
		if (n_pending_queries < MAX_PENDING_QUERIES)
			pending_query_ids_buf = pending_query_ids;

		copy_queries(query_buffer, new_bucket_id, old_bucket_id, pending_query_ids_buf, n_pending_queries);
	}
}

/*
 * Deallocate least-used entries.
 *
 * If old_bucket_id != -1, move all pending queries in old_bucket_id
 * to the new bucket id.
 *
 * Caller must hold an exclusive lock on pgss->lock.
 */
bool
hash_entry_dealloc(int new_bucket_id, int old_bucket_id)
{
	HASH_SEQ_STATUS hash_seq;
	pgssEntry		*entry = NULL;
	List 			*pending_entries = NIL;
	ListCell		*pending_entry;

	hash_seq_init(&hash_seq, pgss_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		if (new_bucket_id < 0 ||
			(entry->key.bucket_id == new_bucket_id &&
				 (entry->counters.state == PGSS_FINISHED || entry->counters.state == PGSS_ERROR)))
		{
			entry = hash_search(pgss_hash, &entry->key, HASH_REMOVE, NULL);
		}

		/*
		 * If we detect a pending query residing in the previous bucket id,
		 * we add it to a list of pending elements to be moved to the new
		 * bucket id.
		 * Can't update the hash table while iterating it inside this loop,
		 * as this may introduce all sort of problems.
		 */
		if (old_bucket_id != -1 && entry->key.bucket_id == old_bucket_id)
		{
			if (entry->counters.state == PGSS_PARSE ||
				entry->counters.state == PGSS_PLAN ||
				entry->counters.state == PGSS_EXEC)
			{
				pgssEntry *bkp_entry = malloc(sizeof(pgssEntry));
				if (!bkp_entry)
				{
					/* No memory, remove pending query entry from the previous bucket. */
					entry = hash_search(pgss_hash, &entry->key, HASH_REMOVE, NULL);
					continue;
				}

				/* Save key/data from the previous entry. */
				memcpy(bkp_entry, entry, sizeof(pgssEntry));

				/* Update key to use the new bucket id. */
				bkp_entry->key.bucket_id = new_bucket_id;

				/* Add the entry to a list of nodes to be processed later. */
				pending_entries = lappend(pending_entries, bkp_entry);

				/* Finally remove the pending query from the expired bucket id. */
				entry = hash_search(pgss_hash, &entry->key, HASH_REMOVE, NULL);
			}
		}
	}

	/*
	 * Iterate over the list of pending queries in order
	 * to add them back to the hash table with the updated bucket id.
	 */
	foreach (pending_entry, pending_entries) {
		bool found = false;
		pgssEntry	*new_entry;
		pgssEntry	*old_entry = (pgssEntry *) lfirst(pending_entry);

		new_entry = (pgssEntry *) hash_search(pgss_hash, &old_entry->key, HASH_ENTER_NULL, &found);
		if (new_entry == NULL)
			elog(DEBUG1, "%s", "pg_stat_monitor: out of memory");
		else if (!found)
		{
			/* Restore counters and other data. */
			new_entry->counters = old_entry->counters;
			SpinLockInit(&new_entry->mutex);
			new_entry->encoding = old_entry->encoding;
		}

		free(old_entry);
	}

	list_free(pending_entries);

	return true;
}

/*
 * Release all entries.
 */
void
hash_entry_reset()
{
	pgssSharedState *pgss   = pgsm_get_ss();
	HASH_SEQ_STATUS		hash_seq;
	pgssEntry			*entry;

	LWLockAcquire(pgss->lock, LW_EXCLUSIVE);

	hash_seq_init(&hash_seq, pgss_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		hash_search(pgss_hash, &entry->key, HASH_REMOVE, NULL);
	}
	pg_atomic_write_u64(&pgss->current_wbucket, 0);
	LWLockRelease(pgss->lock);
}

/* Caller must acquire a lock */
pgssQueryEntry*
hash_create_query_entry(uint64 bucket_id, uint64 queryid, uint64 dbid, uint64 userid, uint64 ip, uint64 appid)
{
    pgssQueryHashKey    key;
	pgssQueryEntry      *entry;
	bool                found;

    key.queryid = queryid;
	key.bucket_id = bucket_id;
	key.dbid = dbid;
	key.userid = userid;
	key.ip = ip;
	key.appid = appid;

	entry = (pgssQueryEntry *) hash_search(pgss_query_hash, &key, HASH_ENTER_NULL, &found);
	return entry;
}

/* Caller must acquire a lock */
pgssQueryEntry*
hash_find_query_entry(uint64 bucket_id, uint64 queryid, uint64 dbid, uint64 userid, uint64 ip, uint64 appid)
{
    pgssQueryHashKey    key;
	pgssQueryEntry      *entry;
	bool                found;

    key.queryid = queryid;
	key.bucket_id = bucket_id;
	key.dbid = dbid;
	key.userid = userid;
	key.ip = ip;
	key.appid = appid;

    /* Lookup the hash table entry with shared lock. */
	entry = (pgssQueryEntry *) hash_search(pgss_query_hash, &key, HASH_FIND, &found);
    return entry;
}

bool
IsHashInitialize(void)
{
	return (pgss != NULL &&
			pgss_hash != NULL);
}

static void copy_queries(unsigned char *query_buffer[],
						uint64 new_bucket_id,
						uint64 old_bucket_id,
						uint64 *query_ids,
						size_t n_queries)
{
	bool found;
	uint64 query_id           = 0;
	uint64 query_len          = 0;
	uint64 rlen               = 0;
	uint64 buf_len            = 0;
	unsigned char *src_buffer = query_buffer[old_bucket_id];
	size_t i;

	memcpy(&buf_len, src_buffer, sizeof (uint64));
	if (buf_len <= 0)
		return;

	rlen = sizeof (uint64); /* Move forwad to skip length bytes */
	while (rlen < buf_len)
	{
		found = false;
		memcpy(&query_id, &src_buffer[rlen], sizeof (uint64)); /* query id */
		for (i = 0; i < n_queries; ++i)
		{
			if (query_id == query_ids[i])
			{
				found = true;
				break;
			}
		}

		rlen += sizeof (uint64);
		if (buf_len <= rlen)
			break;

		memcpy(&query_len, &src_buffer[rlen], sizeof (uint64)); /* query len */
		rlen += sizeof (uint64);
		if (buf_len < rlen + query_len)
			break;

		if (found) {
			SaveQueryText(new_bucket_id, query_id, query_buffer[new_bucket_id],
							(const char *)&src_buffer[rlen], query_len);
		}

		rlen += query_len;
	}
}