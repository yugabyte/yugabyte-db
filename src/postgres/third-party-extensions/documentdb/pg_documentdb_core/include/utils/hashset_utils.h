/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/hashset_utils.h
 *
 * Utilities for HTAB objects.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <common/hashfn.h>
#include <utils/hsearch.h>

#ifndef PG_BSONELEMENT_HASHSET_H
#define PG_BSONELEMENT_HASHSET_H

/*
 * Struct used to track bson elements by their keys in a hash table.
 */
typedef struct
{
	pgbsonelement element;
} PgbsonElementHashEntry;

/*
 * Struct used to store key, value pair in PG hash table.
 */
typedef struct PgbsonElementHashEntryOrdered
{
	/* pgbsonelement to store key and value in the hash map */
	pgbsonelement element;

	/* To maintain insertion order, we store the address of the next hash entry here. We need to update the tail every time we insert a new element. */
	struct PgbsonElementHashEntryOrdered *next;
} PgbsonElementHashEntryOrdered;

/*
 * Struct used to store a bson value and accompanying collation string in a hash table.
 */
typedef struct BsonValueHashEntry
{
	/* key for hash entry; should be the first field */
	bson_value_t bsonValue;

	/* Collation string; should be the second field */
	const char *collationString;
} BsonValueHashEntry;

/*
 * Defines the flags to be used in standard HTAB creations in the DocumentDB scenario.
 * We use the hash of the element, with a custom comparison function, and create it in
 * the current query's memory context.
 */
static const int DefaultExtensionHashFlags = (HASH_ELEM | HASH_COMPARE | HASH_FUNCTION |
											  HASH_CONTEXT);


/*
 * Helper function that creates a HashCTL that matches the
 * Default ExtensionHashFlags
 */
pg_attribute_always_inline
static HASHCTL
CreateExtensionHashCTL(Size keySize, Size entrySize,
					   HashCompareFunc compareFunc,
					   HashValueFunc hashFunc)
{
	HASHCTL hashInfo;
	memset(&hashInfo, 0, sizeof(HASHCTL));

	hashInfo.keysize = keySize;
	hashInfo.entrysize = entrySize;
	hashInfo.match = compareFunc;
	hashInfo.hash = hashFunc;
	hashInfo.hcxt = CurrentMemoryContext;
	return hashInfo;
}


HTAB * CreatePgbsonElementHashSet(void);
HTAB * CreatePgbsonElementPathAndValueHashSet(void);
HTAB * CreateStringViewHashSet(void);
HTAB * CreateBsonValueHashSet(void);
HTAB * CreatePgbsonElementOrderedHashSet(void);
HTAB * CreateBsonValueWithCollationHashSet(int extraDataSize);

bool InsertInToPgbsonElementOrderedHash(HTAB *hashTable,
										PgbsonElementHashEntryOrdered *hashEntry,
										PgbsonElementHashEntryOrdered *head,
										PgbsonElementHashEntryOrdered *tail);
#endif
