/*-------------------------------------------------------------------------
 *
 * hash_query.c
 *		Track statement execution times across a whole database cluster.
 *
 * Copyright (c) 2008-2020, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  contrib/pg_stat_monitor/hash_query.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pg_stat_monitor.h"

static pgssSharedState *pgss;
static HTAB *pgss_hash;
static HTAB *pgss_object_hash;
static HTAB *pgss_buckethash = NULL;
static HTAB *pgss_waiteventshash = NULL;

static pgssWaitEventEntry **pgssWaitEventEntries = NULL;
static HTAB* hash_init(const char *hash_name, int key_size, int entry_size, int hash_size);

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
	pgss_object_hash = NULL;
	pgss_buckethash = NULL;
	pgss_waiteventshash = NULL;

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

	pgss->query_buf_size_bucket = PGSM_QUERY_BUF_SIZE / PGSM_MAX_BUCKETS;

	for (i = 0; i < PGSM_MAX_BUCKETS; i++)
	{
		unsigned char *buf = (unsigned char *)ShmemAlloc(pgss->query_buf_size_bucket);
		set_qbuf(i, buf);
		memset(buf, 0, sizeof (uint64));
	}

	pgss_hash = hash_init("pg_stat_monitor: Queries hashtable", sizeof(pgssHashKey), sizeof(pgssEntry),PGSM_MAX);

	pgss_waiteventshash = hash_init("pg_stat_monitor: Wait Event hashtable", sizeof(pgssWaitEventKey), sizeof(pgssWaitEventEntry), 100);

	pgss_object_hash = hash_init("pg_stat_monitor: Object hashtable", sizeof(pgssObjectHashKey), sizeof(pgssObjectEntry), PGSM_OBJECT_CACHE);

	Assert(IsHashInitialize());

	pgssWaitEventEntries = malloc(sizeof (pgssWaitEventEntry) * MAX_BACKEND_PROCESES);
	for (i = 0; i < MAX_BACKEND_PROCESES; i++)
	{
		pgssWaitEventKey	key;
		pgssWaitEventEntry	*entry = NULL;
		bool				found = false;

		key.processid = i;
		entry = (pgssWaitEventEntry *) hash_search(pgss_waiteventshash, &key, HASH_ENTER, &found);
		if (!found)
		{
			SpinLockInit(&entry->mutex);
			pgssWaitEventEntries[i] = entry;
		}
	}

	LWLockRelease(AddinShmemInitLock);

	/*
	* If we're in the postmaster (or a standalone backend...), set up a shmem
	* exit hook to dump the statistics to disk.
	*/
	if (!IsUnderPostmaster)
		on_shmem_exit(pgss_shmem_shutdown, (Datum) 0);
}

int
pgsm_get_bucket_size(void)
{
	return pgss->query_buf_size_bucket;
}

pgssSharedState* pgsm_get_ss(void)
{
	Assert(pgss);
	return pgss;
}

HTAB* pgsm_get_hash(void)
{
	Assert(pgss_hash);
	return pgss_hash;
}

HTAB* pgsm_get_wait_event_hash(void)
{
	Assert(pgss_waiteventshash);
	return pgss_waiteventshash;
}

pgssWaitEventEntry** pgsm_get_wait_event_entry(void)
{
	Assert(pgssWaitEventEntries);
	return pgssWaitEventEntries;
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
	elog(DEBUG2, "pg_stat_monitor: %s()", __FUNCTION__);
	/* Don't try to dump during a crash. */
	if (code)
		return;

	/* Safety check ... shouldn't get here unless shmem is set up. */
	if (IsHashInitialize())
		return;
}

Size
hash_memsize(void)
{
	Size	size;

	size = MAXALIGN(sizeof(pgssSharedState));
	size = add_size(size, hash_estimate_size(PGSM_MAX, sizeof(pgssEntry)));

	return size;
}

pgssEntry *
hash_entry_alloc(pgssSharedState *pgss, pgssHashKey *key,int encoding)
{
	pgssEntry	*entry = NULL;
	bool		found = false;

	if (pgss->bucket_entry[pgss->current_wbucket] >= (PGSM_MAX / PGSM_MAX_BUCKETS))
	{
		pgss->bucket_overflow[pgss->current_wbucket]++;
		return NULL;
	}

	if (hash_get_num_entries(pgss_hash)  >= PGSM_MAX)
		return NULL;

	/* Find or create an entry with desired hash code */
	entry = (pgssEntry *) hash_search(pgss_hash, key, HASH_ENTER, &found);
	if (!found)
	{
		pgss->bucket_entry[pgss->current_wbucket]++;
		/* New entry, initialize it */

		/* reset the statistics */
		memset(&entry->counters, 0, sizeof(Counters));
		/* set the appropriate initial usage count */
		/* re-initialize the mutex each time ... we assume no one using it */
		SpinLockInit(&entry->mutex);
		/* ... and don't forget the query text metadata */
		entry->encoding = encoding;
	}
	return entry;
}

/*
 * Deallocate least-used entries.
 *
 * Caller must hold an exclusive lock on pgss->lock.
 */
void
hash_entry_dealloc(int bucket)
{
	HASH_SEQ_STATUS hash_seq;
	pgssEntry		*entry;

	pgss->bucket_entry[bucket] = 0;

	hash_seq_init(&hash_seq, pgss_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		if (entry->key.bucket_id == bucket || bucket < 0)
			entry = hash_search(pgss_hash, &entry->key, HASH_REMOVE, NULL);
	}
}

/*
 * Release all entries.
 */
void
hash_entry_reset()
{
	HASH_SEQ_STATUS		hash_seq;
	pgssEntry			*entry;
	pgssObjectEntry		*objentry;
	pgssWaitEventEntry	*weentry;

	LWLockAcquire(pgss->lock, LW_EXCLUSIVE);

	hash_seq_init(&hash_seq, pgss_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		hash_search(pgss_hash, &entry->key, HASH_REMOVE, NULL);
	}

	hash_seq_init(&hash_seq, pgss_buckethash);
    while ((objentry = hash_seq_search(&hash_seq)) != NULL)
    {
		hash_search(pgss_buckethash, &objentry->key, HASH_REMOVE, NULL);
    }

	hash_seq_init(&hash_seq, pgss_waiteventshash);
	while ((weentry = hash_seq_search(&hash_seq)) != NULL)
	{
		hash_search(pgss_waiteventshash, &weentry->key, HASH_REMOVE, NULL);
    }
	pgss->current_wbucket = 0;
	free(pgssWaitEventEntries);
	LWLockRelease(pgss->lock);
}

void
hash_alloc_object_entry(uint64 queryid, char *objects)
{
    pgssObjectEntry *entry = NULL;
    bool            found;
    pgssObjectHashKey key;

	LWLockAcquire(pgss->lock, LW_EXCLUSIVE);
    key.queryid = queryid;
    entry = (pgssObjectEntry *) hash_search(pgss_object_hash, &key, HASH_ENTER, &found);
    if (!found)
    {
        SpinLockAcquire(&entry->mutex);
        snprintf(entry->tables_name, MAX_REL_LEN, "%s", objects);
        SpinLockRelease(&entry->mutex);
    }
	LWLockRelease(pgss->lock);
}

/* De-alocate memory */
void
hash_dealloc_object_entry(uint64 queryid, char *objects)
{
    pgssObjectHashKey       key;
    pgssObjectEntry         *entry;

    key.queryid = queryid;

	LWLockAcquire(pgss->lock, LW_EXCLUSIVE);
    entry = (pgssObjectEntry *) hash_search(pgss_object_hash, &key, HASH_FIND, NULL);
    if (entry != NULL)
    {
        snprintf(objects, MAX_REL_LEN, "%s", entry->tables_name);
        hash_search(pgss_object_hash, &entry->key, HASH_REMOVE, NULL);
    }
    LWLockRelease(pgss->lock);
}

pgssEntry*
hash_create_query_entry(unsigned int queryid,
                        unsigned int userid,
                        unsigned int dbid,
                        unsigned int bucket_id,
                        unsigned int ip)
{
    pgssHashKey     key;
    pgssEntry       *entry =  NULL;
    int             encoding = GetDatabaseEncoding();

    key.queryid = queryid;
    key.userid = userid;
    key.dbid = dbid;
    key.bucket_id = bucket_id;
	key.ip = ip;

    /* Lookup the hash table entry with shared lock. */
    LWLockAcquire(pgss->lock, LW_SHARED);
    entry = (pgssEntry *) hash_search(pgss_hash, &key, HASH_FIND, NULL);
    if(!entry)
    {
        LWLockRelease(pgss->lock);
        LWLockAcquire(pgss->lock, LW_EXCLUSIVE);

        /* OK to create a new hashtable entry */
        entry = hash_entry_alloc(pgss, &key, encoding);
    }
    return entry;
}

bool
IsHashInitialize(void)
{
	return (pgss || pgss_hash || pgss_object_hash || pgss_buckethash || pgss_waiteventshash);
}

