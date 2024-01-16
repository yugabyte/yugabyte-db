/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/hashset_utils.c
 *
 * Utilities for HTAB objects.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <utils/hsearch.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "utils/hashset_utils.h"

/*
 * Callbacks used to deal with bson element keys within a hash table.
 */
static uint32 PgbsonElementHashEntryHashFunc(const void *obj, size_t objsize);
static int PgbsonElementHashEntryCompareFunc(const void *obj1, const void *obj2,
											 Size objsize);
static uint32 StringViewHashEntryHashFunc(const void *obj, size_t objsize);
static int StringViewHashEntryCompareFunc(const void *obj1, const void *obj2,
										  Size objsize);
static uint32 BsonValueHashFunc(const void *obj, size_t objsize);
static int BsonValueHashEntryCompareFunc(const void *obj1, const void *obj2, Size
										 objsize);

/*
 * Creates a hash table that stores pgbsonelement entries using
 * a hash and search based on the element path.
 */
HTAB *
CreatePgbsonElementHashSet()
{
	HASHCTL hashInfo = CreateExtensionHashCTL(
		sizeof(PgbsonElementHashEntry),
		sizeof(PgbsonElementHashEntry),
		PgbsonElementHashEntryCompareFunc,
		PgbsonElementHashEntryHashFunc
		);
	HTAB *bsonElementHashSet =
		hash_create("Bson Element Hash Table", 32, &hashInfo, DefaultExtensionHashFlags);

	return bsonElementHashSet;
}


/*
 * PgbsonElementHashEntryHashFunc is the (HASHCTL.hash) callback (based on
 * string_hash()) used to hash a PgbsonElementHashEntry object based on key
 * of the bson element that it holds.
 */
static uint32
PgbsonElementHashEntryHashFunc(const void *obj, size_t objsize)
{
	const PgbsonElementHashEntry *hashEntry = obj;
	return hash_bytes((const unsigned char *) hashEntry->element.path,
					  (int) hashEntry->element.pathLength);
}


/*
 * PgbsonElementHashEntryCompareFunc is the (HASHCTL.match) callback (based
 * on string_compare()) used to determine if keys of the bson elements hold
 * by given two PgbsonElementHashEntry objects are the same.
 *
 * Returns 0 if those two bson element keys are same, 1 otherwise.
 */
static int
PgbsonElementHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize)
{
	const PgbsonElementHashEntry *hashEntry1 = obj1;
	const PgbsonElementHashEntry *hashEntry2 = obj2;

	if (hashEntry1->element.pathLength != hashEntry2->element.pathLength)
	{
		return 1;
	}

	if (strcmp(hashEntry1->element.path, hashEntry2->element.path) != 0)
	{
		return 1;
	}

	return 0;
}


/*
 * Creates a hash table that stores StringView using
 * a hash and search based on the StringView.
 */
HTAB *
CreateStringViewHashSet()
{
	HASHCTL hashInfo = CreateExtensionHashCTL(
		sizeof(StringView),
		sizeof(StringView),
		StringViewHashEntryCompareFunc,
		StringViewHashEntryHashFunc
		);
	return hash_create("StringView Hash Value", 32, &hashInfo, DefaultExtensionHashFlags);
}


/*
 * StringViewHashEntryHashFunc is the (HASHCTL.hash) callback used to hash a StringView
 */
static uint32
StringViewHashEntryHashFunc(const void *obj, size_t objsize)
{
	const StringView *hashEntry = obj;
	return hash_bytes((const unsigned char *) hashEntry->string, hashEntry->length);
}


/*
 * StringViewHashEntryCompareFunc is the (HASHCTL.match) callback used to determine if two StringView are same.
 * Returns 0 if those two StringView are same, 1 otherwise.
 */
static int
StringViewHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize)
{
	const StringView *hashEntry1 = obj1;
	const StringView *hashEntry2 = obj2;

	return CompareStringView(hashEntry1, hashEntry2);
}


/*
 * Creates a hash table that stores bson_value_t entries using
 * a hash and search based on the bson_value_t.
 */
HTAB *
CreateBsonValueHashSet()
{
	HASHCTL hashInfo = CreateExtensionHashCTL(
		sizeof(bson_value_t),
		sizeof(bson_value_t),
		BsonValueHashEntryCompareFunc,
		BsonValueHashFunc);
	static const int numElements = 32;
	HTAB *bsonValueHashSet =
		hash_create("Bson Value Hash Table", numElements, &hashInfo,
					DefaultExtensionHashFlags);

	return bsonValueHashSet;
}


static uint32
BsonValueHashFunc(const void *obj, size_t objsize)
{
	return BsonValueHashUint32((const bson_value_t *) obj);
}


static int
BsonValueHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize)
{
	bool isComparisonValidIgnore;
	return CompareBsonValueAndType((const bson_value_t *) obj1,
								   (const bson_value_t *) obj2,
								   &isComparisonValidIgnore);
}
