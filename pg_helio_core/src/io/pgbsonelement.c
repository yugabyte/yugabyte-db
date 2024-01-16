/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/io/pgbsonelement.c
 *
 * The BSON Element type implementation.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <executor/executor.h>
#include <utils/builtins.h>
#include <utils/typcache.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <utils/timestamp.h>
#include <utils/array.h>
#include <parser/parse_coerce.h>
#include <catalog/pg_type.h>
#include <funcapi.h>
#include <lib/stringinfo.h>

#include "io/helio_bson_core.h"
#include "io/pgbsonelement.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */


/* --------------------------------------------------------- */
/* pgbsonelement functions */
/* --------------------------------------------------------- */

/*
 * Converts the current value at the iterator into a pgbsonelement.
 */
void
BsonIterToPgbsonElement(bson_iter_t *iterator, pgbsonelement *element)
{
	element->path = bson_iter_key(iterator);
	element->pathLength = bson_iter_key_len(iterator);
	element->bsonValue = *bson_iter_value(iterator);
}


/*
 * Converts a bson iterator that has exactly 1 value in it to a pgbsonelement.
 * iterator must be an uninitialized iterator.
 */
void
BsonIterToSinglePgbsonElement(bson_iter_t *iterator, pgbsonelement *element)
{
	if (!bson_iter_next(iterator))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input BSON: Should not have empty document")));
	}

	BsonIterToPgbsonElement(iterator, element);
	if (bson_iter_next(iterator))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"invalid input BSON: Should have only 1 entry in the bson document")));
	}
}


/*
 * Converts a pgbson that has exactly 1 value in it to a pgbsonelement.
 */
void
PgbsonToSinglePgbsonElement(const pgbson *bson, pgbsonelement *element)
{
	bson_iter_t iterator;
	PgbsonInitIterator(bson, &iterator);
	BsonIterToSinglePgbsonElement(&iterator, element);
}


/*
 * Converts a pgbson that has exactly 1 value in it to a pgbsonelement.
 * returns true if it's a single pgbson element, false otherwise.
 */
bool
TryGetSinglePgbsonElementFromPgbson(pgbson *bson, pgbsonelement *element)
{
	bson_iter_t iterator;
	PgbsonInitIterator(bson, &iterator);
	return TryGetSinglePgbsonElementFromBsonIterator(&iterator, element);
}


/*
 * Converts a bson_iter_t that has exactly 1 value in it to a pgbsonelement.
 * returns true if it's a single pgbson element, false otherwise.
 */
bool
TryGetSinglePgbsonElementFromBsonIterator(bson_iter_t *iterator, pgbsonelement *element)
{
	if (!bson_iter_next(iterator))
	{
		/* there's 0 fields */
		return false;
	}

	BsonIterToPgbsonElement(iterator, element);
	if (bson_iter_next(iterator))
	{
		/* there's more fields. */
		return false;
	}

	return true;
}


/*
 * For a given bson value of document type, converts to a pgbsonelement,
 * which contains the path and value at that path.
 */
void
BsonValueToPgbsonElement(const bson_value_t *bsonValue,
						 pgbsonelement *element)
{
	bson_iter_t iterator;

	/* bsonValue should not be null */
	Assert(bsonValue != NULL);

	if (!bson_iter_init_from_data(&iterator,
								  bsonValue->value.v_doc.data,
								  bsonValue->value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("Could not initialize bson iterator.")));
	}

	if (!bson_iter_next(&iterator))
	{
		ereport(ERROR, errmsg("invalid input BSON: Should not be empty document"));
	}
	BsonIterToPgbsonElement(&iterator, element);
}


/*
 * For a given bson value of document type, tries to converts to a pgbsonelement,
 * which contains the path and value at that path.
 * If there is an empty object or invalid value (not an object/array) then return false.
 */
bool
TryGetBsonValueToPgbsonElement(const bson_value_t *bsonValue, pgbsonelement *element)
{
	bson_iter_t iterator;

	/* bsonValue should not be null */
	Assert(bsonValue != NULL);

	if (!bson_iter_init_from_data(&iterator,
								  bsonValue->value.v_doc.data,
								  bsonValue->value.v_doc.data_len))
	{
		return false;
	}

	if (!bson_iter_next(&iterator))
	{
		return false;
	}

	BsonIterToPgbsonElement(&iterator, element);
	return true;
}


/*
 * Converts a pgbsonelement to a serialized pgbson for returning from a C function.
 */
pgbson *
PgbsonElementToPgbson(pgbsonelement *element)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendValue(&writer, element->path, element->pathLength,
							&element->bsonValue);
	return PgbsonWriterGetPgbson(&writer);
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */
