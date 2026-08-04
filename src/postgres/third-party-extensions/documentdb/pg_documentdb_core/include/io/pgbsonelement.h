/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/io/pgbsonelement.h
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

/* Unsafe version of the BsonValueToPgbsonElement that assumes value is a single field doc */
void BsonValueToPgbsonElementUnsafe(const bson_value_t *bsonValue,
									pgbsonelement *element);
void BsonDocumentBytesToPgbsonElementUnsafe(const uint8_t *bytes, uint32_t bytesLen,
											pgbsonelement *element);
void BsonDocumentBytesToPgbsonElementWithOptionsUnsafe(const uint8_t *bytes, int32_t
													   bytesLen, pgbsonelement *element,
													   bool skipLengthOffset);
pgbson * PgbsonElementToPgbson(pgbsonelement *element);

#endif
