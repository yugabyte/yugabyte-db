/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/pgbsonelement.h
 *
 * The BSON element type declaration.
 *
 *-------------------------------------------------------------------------
 */

#ifndef PG_BSONELEMENT_H
#define PG_BSONELEMENT_H

/*
 * Represents a single field value in a bson document
 * contains the path, and the value at that path
 * Note that this is a stack object so the backing document
 * buffer has to be kept alive while using the pgbsonelement
 */
typedef struct pgbsonelement
{
	const char *path;
	uint32_t pathLength;
	bson_value_t bsonValue;
} pgbsonelement;

void BsonIterToPgbsonElement(bson_iter_t *iterator, pgbsonelement *element);
void PgbsonToSinglePgbsonElement(const pgbson *bson, pgbsonelement *element);
const char * PgbsonToSinglePgbsonElementWithCollation(const pgbson *bson,
													  pgbsonelement *element);
void BsonIterToSinglePgbsonElement(bson_iter_t *iterator, pgbsonelement *element);
bool TryGetSinglePgbsonElementFromPgbson(pgbson *bson, pgbsonelement *element);
bool TryGetSinglePgbsonElementFromBsonIterator(bson_iter_t *iterator,
											   pgbsonelement *element);
void BsonValueToPgbsonElement(const bson_value_t *value, pgbsonelement *element);
bool TryGetBsonValueToPgbsonElement(const bson_value_t *value, pgbsonelement *element);

pgbson * PgbsonElementToPgbson(pgbsonelement *element);

#endif
