/*-------------------------------------------------------------------------
 *
 * ybInsertOnConflictBatchingMap.c
 *	  Hashmap used during INSERT ON CONFLICT batching.
 *
 * Copyright (c) YugabyteDB, Inc.
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/hash.h"
#include "executor/tuptable.h"
#include "utils/datum.h"
#include "utils/hashutils.h"

/*
 * YbInsertOnConflictBatchingKey
 * The hash table key
 */
typedef struct YbInsertOnConflictBatchingKey
{
	int			nkeys;
	Datum		values[INDEX_MAX_KEYS];
	bool		isnull[INDEX_MAX_KEYS];
} YbInsertOnConflictBatchingKey;

/*
 * YbInsertOnConflictBatchingEntry
 *		The data struct that the hash table stores
 */
typedef struct YbInsertOnConflictBatchingEntry
{
	YbInsertOnConflictBatchingKey key;	/* Hash key for hash table lookups */
	TupleTableSlot *slot;		/* Heap slot for the existing row related to
								   this key.  NULL for just-inserted rows. */
	uint32		hash;			/* Hash value (cached) */
	char		status;			/* Hash status */
} YbInsertOnConflictBatchingEntry;

#define SH_PREFIX yb_insert_on_conflict_batching
#define SH_ELEMENT_TYPE YbInsertOnConflictBatchingEntry
#define SH_KEY_TYPE YbInsertOnConflictBatchingKey
#define SH_SCOPE static inline
#define SH_DECLARE
#include "lib/simplehash.h"

static uint32 YbInsertOnConflictBatching_hash(struct yb_insert_on_conflict_batching_hash *tb,
											  const YbInsertOnConflictBatchingKey key);
static bool YbInsertOnConflictBatching_equal(struct yb_insert_on_conflict_batching_hash *tb,
											 const YbInsertOnConflictBatchingKey params1,
											 const YbInsertOnConflictBatchingKey params2);

#define SH_PREFIX yb_insert_on_conflict_batching
#define SH_ELEMENT_TYPE YbInsertOnConflictBatchingEntry
#define SH_KEY_TYPE YbInsertOnConflictBatchingKey
#define SH_KEY key
#define SH_HASH_KEY(tb, key) YbInsertOnConflictBatching_hash(tb, key)
#define SH_EQUAL(tb, a, b) YbInsertOnConflictBatching_equal(tb, a, b)
#define SH_SCOPE static inline
/* TODO(jason): store hash may be unnecessary */
#define SH_STORE_HASH
#define SH_GET_HASH(tb, a) a->hash
#define SH_DEFINE
#include "lib/simplehash.h"

/* Taken from pg_bitutils.c of upstream PG 15.2 */
static inline uint32
pg_rotate_left32(uint32 word, int n)
{
	return (word << n) | (word >> (32 - n));
}

/*
 * YbInsertOnConflictBatching_hash
 *		Hash function for simplehash hashtable.
 */
static uint32
YbInsertOnConflictBatching_hash(struct yb_insert_on_conflict_batching_hash *tb,
								const YbInsertOnConflictBatchingKey key)
{
	uint32		hashkey = 0;

	for (int i = 0; i < key.nkeys; ++i)
	{
		/* combine successive hashkeys by rotating */
		hashkey = pg_rotate_left32(hashkey, 1);

		if (!key.isnull[i])	/* treat nulls as having hash key 0 */
		{
			FormData_pg_attribute *attr;
			uint32		hkey;

			attr = &((TupleDesc) tb->private_data)->attrs[i];

			hkey = datum_image_hash(key.values[i],
									attr->attbyval,
									attr->attlen);

			hashkey ^= hkey;
		}
	}

	return murmurhash32(hashkey);
}

/*
 * YbInsertOnConflictBatching_equal
 *		Equality function for confirming hash value matches during a hash
 *		table lookup.
 */
static bool
YbInsertOnConflictBatching_equal(struct yb_insert_on_conflict_batching_hash *tb,
								 const YbInsertOnConflictBatchingKey key1,
								 const YbInsertOnConflictBatchingKey key2)
{
	Assert(key1.nkeys == key2.nkeys);
	TupleDesc tupdesc = (TupleDesc) tb->private_data;
	for (int i = 0; i < key1.nkeys; ++i)
	{
		FormData_pg_attribute *attr;

		if (key1.isnull[i] != key2.isnull[i])
			return false;

		/* both NULL? they're equal */
		/* TODO(jason): don't assume nulls distinct */
		if (key1.isnull[i])
			continue;

		/* perform binary comparison on the two datums */
		attr = &tupdesc->attrs[i];
		/*
		 * YB: use simpler (faster?) datumIsEqual over datum_image_eq since
		 * TOAST and the like are not supported yet.
		 */
		if (!datumIsEqual(key1.values[i], key2.values[i],
						  attr->attbyval, attr->attlen))
			return false;
	}
	return true;
}

struct yb_insert_on_conflict_batching_hash *
YbInsertOnConflictBatchingMapCreate(MemoryContext metacxt,
									uint32 size,
									TupleDesc tupdesc)
{
	MemoryContext tbcxt;

	/* Make a guess at a good size when we're not given a valid size. */
	if (size == 0)
		size = 1024;

	tbcxt = AllocSetContextCreate(metacxt,
								  "InsertOnConflictBatchingMap context",
								  ALLOCSET_DEFAULT_SIZES);
	/* TODO(jason): copy tupdesc? */
	return yb_insert_on_conflict_batching_create(tbcxt, size, tupdesc);
}

void
YbInsertOnConflictBatchingMapInsert(struct yb_insert_on_conflict_batching_hash *tb,
									int nkeys,
									Datum *values,
									bool *isnull,
									TupleTableSlot *slot)
{
	bool found;
	YbInsertOnConflictBatchingEntry *entry;
	TupleDesc tupdesc = (TupleDesc) tb->private_data;
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(tb->ctx);

	YbInsertOnConflictBatchingKey key;
	key.nkeys = nkeys;
	Assert(nkeys <= INDEX_MAX_KEYS);
	for (int i = 0; i < nkeys; ++i)
	{
		if (!isnull[i])
			key.values[i] = datumCopy(values[i], tupdesc->attrs[i].attbyval,
									  tupdesc->attrs[i].attlen);
	}
	memcpy(key.isnull, isnull, nkeys * sizeof(Datum));

	MemoryContextSwitchTo(oldcontext);

	entry = yb_insert_on_conflict_batching_insert(tb, key, &found);
	/*
	 * Entry could already exist in case of DO UPDATE.  This insert attempt
	 * will end up causing duplicate key error.  For now, free the old slot if
	 * it exists.
	 */
	if (found && entry->slot)
		ExecDropSingleTupleTableSlot(entry->slot);
	/* TODO(jason): check memory allocation */
	entry->slot = slot;
}

bool
YbInsertOnConflictBatchingMapLookup(struct yb_insert_on_conflict_batching_hash *tb,
									int nkeys,
									Datum *values,
									bool *isnull,
									TupleTableSlot **slot)
{
	YbInsertOnConflictBatchingEntry *entry;
	YbInsertOnConflictBatchingKey key;

	key.nkeys = nkeys;
	Assert(nkeys <= INDEX_MAX_KEYS);
	/* Shallow copy is fine since this is for temporary lookup. */
	memcpy(key.values, values, nkeys * sizeof(Datum));
	memcpy(key.isnull, isnull, nkeys * sizeof(Datum));

	entry = yb_insert_on_conflict_batching_lookup(tb, key);
	if (entry)
	{
		*slot = entry->slot;
		return true;
	}
	return false;
}

void
YbInsertOnConflictBatchingMapDelete(struct yb_insert_on_conflict_batching_hash *tb,
									int nkeys,
									Datum *values,
									bool *isnull)
{
	YbInsertOnConflictBatchingEntry *entry;
	YbInsertOnConflictBatchingKey key;

	key.nkeys = nkeys;
	Assert(nkeys <= INDEX_MAX_KEYS);
	/* Shallow copy is fine since this is for temporary lookup. */
	memcpy(key.values, values, nkeys * sizeof(Datum));
	memcpy(key.isnull, isnull, nkeys * sizeof(Datum));

	entry = yb_insert_on_conflict_batching_lookup(tb, key);
	/*
	 * Only existing entries should be attempted to be deleted.  Just-inserted
	 * entries will give error instead so this is unreachable in that case.
	 */
	Assert(entry && entry->slot);
	/*
	 * Save pointer to slot before calling delete because delete can invalidate
	 * entry.
	 */
	TupleTableSlot *slot = entry->slot;
	/*
	 * Don't bother freeing any memory allocated for key.values datumCopy.
	 * It's nontrivial to find each of those.  The memory context holding that
	 * will be freed upon map destroy.
	 * TODO(jason): in case we want to keep the map across batches for the sake
	 * of detecting inserting a value multiple times, this ought to be
	 * revisited to avoid growing memory.
	 */

	if (!yb_insert_on_conflict_batching_delete(tb, key))
	{
		/* Can't happen: we just looked it up. */
		Assert(false);
	}
	ExecDropSingleTupleTableSlot(slot);
}

void
YbInsertOnConflictBatchingMapDestroy(struct yb_insert_on_conflict_batching_hash *tb)
{
	yb_insert_on_conflict_batching_iterator i;
	YbInsertOnConflictBatchingEntry *entry;

	yb_insert_on_conflict_batching_start_iterate(tb, &i);

	while ((entry = yb_insert_on_conflict_batching_iterate(tb, &i)) != NULL)
	{
		if (entry->slot)
			ExecDropSingleTupleTableSlot(entry->slot);
	}

	MemoryContext tbctx = tb->ctx;
	yb_insert_on_conflict_batching_destroy(tb);
	MemoryContextDelete(tbctx);
}
