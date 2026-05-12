/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/io/pgbsonelement.c
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

#include "io/bson_core.h"
#include "utils/documentdb_errors.h"
#include "io/pgbsonelement.h"

extern bool EnableCollation;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static bool FillPgbsonElementUnsafe(uint8_t *data, uint32_t data_len,
									pgbsonelement *element, bool skipLengthOffset);

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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("invalid input BSON: Should not have empty document")));
	}

	BsonIterToPgbsonElement(iterator, element);

	/* The Enable collation is safety net to make sure something not expecting collation in operator spec
	 * breaks unexpctedly */
	if (bson_iter_next(iterator) &&
		!(EnableCollation && strcmp(bson_iter_key(iterator), "collation") == 0))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
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
 * Converts a pgbson that has one or two entries into a pgbson element,
 * and optionally sets the collationString if the second entry has key: "collation".
 * Throws error in all other cases.
 */
const char *
PgbsonToSinglePgbsonElementWithCollation(const pgbson *filter, pgbsonelement *element)
{
	bson_iter_t iter;
	PgbsonInitIterator(filter, &iter);
	const char *collationString = NULL;

	if (!bson_iter_next(&iter))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("invalid input BSON: Should not have empty document")));
	}

	BsonIterToPgbsonElement(&iter, element);

	if (bson_iter_next(&iter))
	{
		if (strcmp(bson_iter_key(&iter), "collation") == 0)
		{
			collationString = bson_iter_utf8(&iter, NULL);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"invalid input BSON: 2nd entry in the bson document must have key \"collation\"")));
		}

		if (bson_iter_next(&iter))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"invalid input BSON: Should have only 2 entries in the bson document")));
		}
	}

	return collationString;
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
		/* No fields are currently available */
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


void
BsonValueToPgbsonElementUnsafe(const bson_value_t *bsonValue,
							   pgbsonelement *element)
{
	Assert(bsonValue != NULL);

	if (bsonValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("invalid input BSON: Should be a document")));
	}

	bool skipLengthOffset = false;
	if (!FillPgbsonElementUnsafe(bsonValue->value.v_doc.data,
								 bsonValue->value.v_doc.data_len, element,
								 skipLengthOffset))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("invalid input BSON: Invalid single value document.")));
	}
}


void
BsonDocumentBytesToPgbsonElementUnsafe(const uint8_t *bytes, uint32_t bytesLen,
									   pgbsonelement *element)
{
	bool skipLengthOffset = false;
	BsonDocumentBytesToPgbsonElementWithOptionsUnsafe(bytes, bytesLen, element,
													  skipLengthOffset);
}


void
BsonDocumentBytesToPgbsonElementWithOptionsUnsafe(const uint8_t *bytes, int32_t bytesLen,
												  pgbsonelement *element, bool
												  skipLengthOffset)
{
	if (!FillPgbsonElementUnsafe((uint8_t *) bytes, bytesLen, element, skipLengthOffset))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("invalid input BSON: Invalid single value document.")));
	}
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

	Assert(bsonValue != NULL);

	if (!bson_iter_init_from_data(&iterator,
								  bsonValue->value.v_doc.data,
								  bsonValue->value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
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

static bool
FillPgbsonElementUnsafe(uint8_t *data, uint32_t data_len, pgbsonelement *element, bool
						skipLengthOffset)
{
#if BSON_BYTE_ORDER == BSON_BIG_ENDIAN
	if (!bson_iter_init_from_data(&iterator,
								  data,
								  data_len))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("Could not initialize bson iterator.")));
	}

	if (!bson_iter_next(&iterator))
	{
		ereport(ERROR, errmsg("invalid input BSON: Should not be empty document"));
	}

	BsonIterToPgbsonElement(&iterator, element);
	return true;
#else
	uint32_t minLength = 5;
	uint32_t typeOffset = 4;
	if (skipLengthOffset)
	{
		typeOffset = 0;
		minLength = 1;
	}

	if (data == NULL || data_len < minLength)
	{
		ereport(ERROR, errmsg("invalid input BSON: Should not be empty document"));
	}

	/* First 4 bytes are the length */
	uint32_t length = data_len;

	if (!skipLengthOffset)
	{
		memcpy(&length, data, sizeof(uint32_t));
	}

	/* Fifth byte is the value */
	element->bsonValue.value_type = (bson_type_t) data[typeOffset];
	data += minLength;

	/* Then the path that is null terminated */
	element->path = (char *) data;
	element->pathLength = strlen(element->path);
	data += element->pathLength + 1;

	int lengthLeft = length - element->pathLength - 1 - typeOffset;
	switch (element->bsonValue.value_type)
	{
		case BSON_TYPE_DATE_TIME:
		{
			if (lengthLeft < 8)
			{
				return false;
			}

			memcpy(&element->bsonValue.value.v_datetime, data, sizeof(int64_t));
			return true;
		}

		case BSON_TYPE_DOUBLE:
		{
			if (lengthLeft < 8)
			{
				return false;
			}

			memcpy(&element->bsonValue.value.v_double, data, sizeof(double));
			return true;
		}

		case BSON_TYPE_INT64:
		{
			if (lengthLeft < 8)
			{
				return false;
			}

			memcpy(&element->bsonValue.value.v_int64, data, sizeof(int64_t));
			return true;
		}

		case BSON_TYPE_TIMESTAMP:
		{
			if (lengthLeft < 8)
			{
				return false;
			}

			memcpy(&element->bsonValue.value.v_timestamp.timestamp, data,
				   sizeof(uint32_t));
			data += 4;
			memcpy(&element->bsonValue.value.v_timestamp.increment, data,
				   sizeof(uint32_t));
			return true;
		}

		case BSON_TYPE_CODE:
		{
			if (lengthLeft < 4)
			{
				return false;
			}

			int32_t codeLength;
			memcpy(&codeLength, data, sizeof(int32_t));
			data += 4;

			if (lengthLeft < codeLength + 4)
			{
				return false;
			}

			element->bsonValue.value.v_code.code = (char *) data;
			element->bsonValue.value.v_code.code_len = codeLength - 1;
			return true;
		}

		case BSON_TYPE_SYMBOL:
		{
			if (lengthLeft < 4)
			{
				return false;
			}

			int32_t symbolLength;
			memcpy(&symbolLength, data, sizeof(int32_t));
			data += 4;
			if (lengthLeft < symbolLength + 4)
			{
				return false;
			}

			element->bsonValue.value.v_symbol.symbol = (char *) data;
			element->bsonValue.value.v_symbol.len = symbolLength - 1;
			return true;
		}

		case BSON_TYPE_UTF8:
		{
			if (lengthLeft < 4)
			{
				return false;
			}

			int32_t utf8Length;
			memcpy(&utf8Length, data, sizeof(int32_t));
			data += 4;
			if (lengthLeft < utf8Length + 4)
			{
				return false;
			}

			element->bsonValue.value.v_utf8.str = (char *) data;
			element->bsonValue.value.v_utf8.len = utf8Length - 1;
			return true;
		}

		case BSON_TYPE_BINARY:
		{
			if (lengthLeft < 4)
			{
				return false;
			}

			int32_t binaryLength;
			memcpy(&binaryLength, data, sizeof(int32_t));
			data += 4;
			if (lengthLeft < 4 + binaryLength + 1)
			{
				return false;
			}

			element->bsonValue.value.v_binary.subtype = (bson_subtype_t) data[0];
			data++;

			if (element->bsonValue.value.v_binary.subtype ==
				BSON_SUBTYPE_BINARY_DEPRECATED)
			{
				binaryLength -= 4;
				data += 4;
			}

			element->bsonValue.value.v_binary.data = data;
			element->bsonValue.value.v_binary.data_len = binaryLength;
			return true;
		}

		case BSON_TYPE_ARRAY:
		case BSON_TYPE_DOCUMENT:
		{
			if (lengthLeft < 4)
			{
				return false;
			}

			int32_t docLength;
			memcpy(&docLength, data, sizeof(int32_t));

			if (lengthLeft < docLength)
			{
				return false;
			}

			element->bsonValue.value.v_doc.data = data;
			element->bsonValue.value.v_doc.data_len = docLength;
			return true;
		}

		case BSON_TYPE_OID:
		{
			if (lengthLeft < 12)
			{
				return false;
			}

			memcpy(&element->bsonValue.value.v_oid, data, 12);
			return true;
		}

		case BSON_TYPE_BOOL:
		{
			if (lengthLeft < 1)
			{
				return false;
			}

			element->bsonValue.value.v_bool = data[0];
			return true;
		}

		case BSON_TYPE_REGEX:
		{
			if (lengthLeft < 2)
			{
				return false;
			}

			char *regex = (char *) data;
			int32_t regexLength = strlen(regex);

			if (lengthLeft < regexLength + 2)
			{
				return false;
			}

			element->bsonValue.value.v_regex.regex = regex;
			element->bsonValue.value.v_regex.options = (char *) (data + regexLength + 1);
			return true;
		}

		case BSON_TYPE_DBPOINTER:
		{
			if (lengthLeft < 4)
			{
				return false;
			}

			int32_t utf8Length;
			memcpy(&utf8Length, data, sizeof(int32_t));
			data += 4;
			if (lengthLeft < utf8Length + 4)
			{
				return false;
			}

			element->bsonValue.value.v_dbpointer.collection = (char *) data;
			element->bsonValue.value.v_dbpointer.collection_len = utf8Length - 1;

			/* Next is object id */
			if (lengthLeft < utf8Length + 4 + 12)
			{
				return false;
			}

			memcpy(&element->bsonValue.value.v_dbpointer.oid, data, 12);
			return true;
		}

		case BSON_TYPE_CODEWSCOPE:
		{
			if (lengthLeft < 4)
			{
				return false;
			}

			int32_t totalcodeLength;
			memcpy(&totalcodeLength, data, sizeof(int32_t));
			data += 4;
			if (lengthLeft < totalcodeLength)
			{
				return false;
			}

			/* First is a string for code */
			int32_t codeLength = *((int32_t *) data);
			data += 4;

			if (totalcodeLength < codeLength)
			{
				return false;
			}

			element->bsonValue.value.v_codewscope.code = (char *) data;
			element->bsonValue.value.v_codewscope.code_len = codeLength;
			data += codeLength + 1;

			/* next is the scope doc */
			int32_t docLength = *((int32_t *) data);

			if (totalcodeLength < docLength)
			{
				return false;
			}

			element->bsonValue.value.v_codewscope.scope_data = data;
			element->bsonValue.value.v_codewscope.scope_len = docLength;
			return true;
		}

		case BSON_TYPE_INT32:
		{
			if (lengthLeft < 4)
			{
				return false;
			}

			memcpy(&element->bsonValue.value.v_int32, data, sizeof(int32_t));
			return true;
		}

		case BSON_TYPE_DECIMAL128:
		{
			if (lengthLeft < 16)
			{
				return false;
			}

			memcpy(&element->bsonValue.value.v_decimal128, data, 16);
			return true;
		}

		case BSON_TYPE_MAXKEY:
		case BSON_TYPE_MINKEY:
		case BSON_TYPE_NULL:
		case BSON_TYPE_UNDEFINED:
		{
			return true;
		}

		default:
		{
			return false;
		}
	}
#endif
}
