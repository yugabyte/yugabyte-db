/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/io/pgbson.c
 *
 * The BSON type serialization.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <utils/builtins.h>

#define PRIVATE_PGBSON_H
#include "io/pgbson.h"
#include "io/pgbson_writer.h"
#undef PRIVATE_PGBSON_H

#include "io/bsonvalue_utils.h"
#include "utils/mongo_errors.h"
#include "utils/string_view.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static pgbson * CreatePgbsonfromBson_t(bson_t *document, bool destroyDocument);

static pgbson * CreatePgbsonfromBsonBytes(const uint8_t *rawbytes, uint32_t length);

static const char *BsonHexPrefix = "BSONHEX";
static const uint32_t BsonHexPrefixLength = 7;


/* --------------------------------------------------------- */
/* pgbson functions */
/* --------------------------------------------------------- */

/*
 * Compares to pgbson structures for strict equality:
 * returns true if they are byte-wise equal.
 */
bool
PgbsonEquals(const pgbson *left, const pgbson *right)
{
	if (VARSIZE_ANY_EXHDR(left) != VARSIZE_ANY_EXHDR(right))
	{
		return false;
	}

	return memcmp(VARDATA_ANY(left), VARDATA_ANY(right), VARSIZE_ANY_EXHDR(left)) == 0;
}


/*
 * PgbsonCountKeys returns the number of keys in a given bson document.
 */
int
PgbsonCountKeys(const pgbson *bsonDocument)
{
	bson_t bson;
	if (!bson_init_static(&bson, (const uint8_t *) VARDATA_ANY(bsonDocument),
						  VARSIZE_ANY_EXHDR(bsonDocument)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}

	return bson_count_keys(&bson);
}


/*
 * BsonDocumentValueCountKeys returns the number of keys in a given bson document value.
 * "value" should be of type BSON_TYPE_ARRAY or BSON_TYPE_DOCUMENT
 */
int
BsonDocumentValueCountKeys(const bson_value_t *value)
{
	if (value->value_type != BSON_TYPE_ARRAY && value->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("Expected value of type array or document")));
	}
	bson_t bson;
	if (!bson_init_static(&bson, value->value.v_doc.data,
						  value->value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}

	return bson_count_keys(&bson);
}


/*
 * Gets the size of the underlying bson represented by
 * the pgbson
 */
uint32_t
PgbsonGetBsonSize(const pgbson *bson)
{
	return VARSIZE_ANY_EXHDR(bson);
}


/*
 * Whether or not the string could be a bson hex string.
 */
bool
IsBsonHexadecimalString(const char *string)
{
	return string[0] == BsonHexPrefix[0];
}


/*
 * Initializes a pgbson structure from a hexadecimal String.
 */
pgbson *
PgbsonInitFromHexadecimalString(const char *hexadecimalString)
{
	uint32_t strLength = strlen(hexadecimalString);

	uint32_t hexStringLength = (strLength - BsonHexPrefixLength);
	if (hexStringLength <= 0 || (hexStringLength % 2 != 0))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"Invalid Hex string for pgbson input")));
	}

	if (strncmp(hexadecimalString, BsonHexPrefix, BsonHexPrefixLength) != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("Bson Hex string does not have valid prefix %s",
							   BsonHexPrefix)));
	}

	uint32_t binaryLength = hexStringLength / 2;

	int allocSize = binaryLength + VARHDRSZ;
	pgbson *pgbsonVal = (pgbson *) palloc(allocSize);

	uint64 actualBinarySize = hex_decode(&hexadecimalString[BsonHexPrefixLength],
										 hexStringLength, VARDATA(pgbsonVal));
	Assert(actualBinarySize == binaryLength);
	SET_VARSIZE(pgbsonVal, (uint32_t) actualBinarySize + VARHDRSZ);
	return pgbsonVal;
}


/*
 * Converts a pgbson to a hexadecimal string representation.
 */
const char *
PgbsonToHexadecimalString(const pgbson *bsonDocument)
{
	size_t binarySize = VARSIZE_ANY_EXHDR(bsonDocument);
	size_t hexEncodedSize = binarySize * 2;
	size_t hexStringSize = hexEncodedSize + 1 + BsonHexPrefixLength; /* add \0 and prefix "BSONHEX"; */
	char *hexString = palloc(hexStringSize);

	memcpy(hexString, BsonHexPrefix, BsonHexPrefixLength);

	const char *pgbsonData = VARDATA_ANY(bsonDocument);

	uint64 hexStringActualSize = hex_encode(pgbsonData, binarySize,
											&hexString[BsonHexPrefixLength]);
	Assert(hexStringActualSize == hexEncodedSize);

	hexString[hexStringActualSize + BsonHexPrefixLength] = 0;
	return hexString;
}


/*
 * Initializes a pgbson structure from a mongodb extended json
 * syntax string.
 */
pgbson *
PgbsonInitFromJson(const char *jsonString)
{
	bson_t bson;
	bson_error_t error;
	bool parseResult = bson_init_from_json(&bson, jsonString, -1, &error);
	if (!parseResult)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"invalid input syntax JSON for BSON: Code: '%d', Message '%s'",
							error.code, error.message)));
	}

	return CreatePgbsonfromBson_t(&bson, true /* destroyDocument */);
}


/*
 * PgbsonToJsonForLogging converts a pgbson structure to a mongodb
 * extended json syntax string.
 */
const char *
PgbsonToJsonForLogging(const pgbson *bsonDocument)
{
	bson_t bson;
	if (!bson_init_static(&bson, (uint8_t *) VARDATA_ANY(bsonDocument),
						  (uint32_t) VARSIZE_ANY_EXHDR(bsonDocument)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}

	return bson_as_relaxed_extended_json(&bson, NULL);
}


/*
 * BsonValueToJsonForLogging converts a bson_value structure to a mongodb
 * extended json syntax string.
 */
const char *
BsonValueToJsonForLogging(const bson_value_t *value)
{
	bson_t bson;
	pgbson *bsonDocument;
	const uint8_t *documentData;
	uint32_t documentLength;
	const char *returnValue;
	char numBuffer[30];

	/* For common types optimize the conversion to logging strings */
	switch (value->value_type)
	{
		case BSON_TYPE_DOCUMENT:
		{
			documentData = value->value.v_doc.data;
			documentLength = value->value.v_doc.data_len;

			if (!bson_init_static(&bson, documentData, documentLength))
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								errmsg("invalid input syntax for BSON")));
			}

			/* since bson strings are palloced - we can simply return the string created. */
			returnValue = bson_as_relaxed_extended_json(&bson, NULL);
			break;
		}

		case BSON_TYPE_UTF8:
		{
			/* create a string that has the original string, \0, and the quotes. */
			char *finalString = palloc(value->value.v_utf8.len + 1 + 2);
			finalString[0] = '"';
			memcpy(&finalString[1], value->value.v_utf8.str, value->value.v_utf8.len);
			finalString[value->value.v_utf8.len + 1] = '"';
			finalString[value->value.v_utf8.len + 2] = 0;

			returnValue = finalString;
			break;
		}

		case BSON_TYPE_INT32:
		{
			int strLength = pg_ltoa(value->value.v_int32, numBuffer);
			returnValue = pnstrdup(numBuffer, strLength);
			break;
		}

		case BSON_TYPE_INT64:
		{
			int strLength = pg_lltoa(value->value.v_int64, numBuffer);
			returnValue = pnstrdup(numBuffer, strLength);
			break;
		}

		case BSON_TYPE_DOUBLE:
		{
			int strLength = pg_snprintf(numBuffer, 30, "%G", value->value.v_double);
			returnValue = pnstrdup(numBuffer, strLength);
			break;
		}

		case BSON_TYPE_BOOL:
		{
			returnValue = value->value.v_bool ? "true" : "false";
			break;
		}

		case BSON_TYPE_DECIMAL128:
		{
			char finalString[BSON_DECIMAL128_STRING];
			bson_decimal128_to_string(&(value->value.v_decimal128), finalString);
			returnValue = pnstrdup(finalString, strlen(finalString));
			break;
		}

		default:
		{
			bsonDocument = BsonValueToDocumentPgbson(value);
			documentData = (const uint8_t *) VARDATA_ANY(bsonDocument);
			documentLength = VARSIZE_ANY_EXHDR(bsonDocument);
			if (!bson_init_static(&bson, documentData, documentLength))
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								errmsg("invalid input syntax for BSON")));
			}

			/*
			 * Ignore surronding curly braces and the empty field name:
			 *   7 for '{ "" : ' prefix, and
			 *   2 for ' }' suffix.
			 */
			char *repr = bson_as_relaxed_extended_json(&bson, NULL);
			returnValue = pnstrdup(repr + 7, strlen(repr) - 9);
			break;
		}
	}

	return returnValue;
}


/**
 * Gets the prefix for bson values of BSON for constructing messages
 *
 */
const char *
FormatBsonValueForShellLogging(const bson_value_t *bson)
{
	StringInfo str = makeStringInfo();
	switch (bson->value_type)
	{
		case BSON_TYPE_INT32:
		{
			appendStringInfo(str, "(%s)%d", "NumberInt", bson->value.v_int32);
			break;
		}

		case BSON_TYPE_INT64:
		{
			/*
			 * Cast value to "long long int" and use "%lld" to handle different word
			 * sizes in different architectures.
			 */
			appendStringInfo(str, "(%s)%lld", "NumberLong",
							 (long long int) bson->value.v_int64);
			break;
		}

		case BSON_TYPE_DOUBLE:
		{
			appendStringInfo(str, "%.1lf", bson->value.v_double);
			break;
		}

		case BSON_TYPE_DECIMAL128:
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("Decimal 128 operation is not supported yet")));
		}

		case BSON_TYPE_DOCUMENT:
		{
			bson_iter_t iter;
			BsonValueInitIterator(bson, &iter);
			appendStringInfoString(str, "{ ");
			const char *separator = NULL;
			while (bson_iter_next(&iter))
			{
				const char *key = bson_iter_key(&iter);
				const bson_value_t *bsonValue = bson_iter_value(&iter);

				if (separator != NULL)
				{
					appendStringInfoString(str, separator);
				}

				const char *bsonValStr = FormatBsonValueForShellLogging(bsonValue);
				appendStringInfo(str, "%s: %s", key, bsonValStr);
				separator = ", ";
			}

			appendStringInfoString(str, " }");
			break;
		}

		case BSON_TYPE_UTF8:
		{
			appendStringInfo(str, "\"%s\"", bson->value.v_utf8.str);
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"Expected Numeric, document or UTF8 bson value type")));
		}
	}
	return str->data;
}


/*
 * Converts a pgbson structure to a mongodb extended json
 * syntax string.
 */
const char *
PgbsonToCanonicalExtendedJson(const pgbson *bsonDocument)
{
	bson_t bson;
	if (!bson_init_static(&bson, (const uint8_t *) VARDATA_ANY(bsonDocument),
						  VARSIZE_ANY_EXHDR(bsonDocument)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}

	/* since bson strings are palloced - we can simply return the string created. */
	return bson_as_canonical_extended_json(&bson, NULL);
}


/*
 * Converts a pgbson structure to a simple json syntax string.
 */
const char *
PgbsonToLegacyJson(const pgbson *bsonDocument)
{
	bson_t bson;
	if (!bson_init_static(&bson, (const uint8_t *) VARDATA_ANY(bsonDocument),
						  VARSIZE_ANY_EXHDR(bsonDocument)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}

	/* since bson strings are palloced - we can simply return the string created. */
	return bson_as_json(&bson, NULL);
}


/*
 * Validates a user input bson for the specified flags, to ensure it's compatible with the database.
 */
void
PgbsonValidateInputBson(const pgbson *bsonDocument, bson_validate_flags_t validateFlag)
{
	ValidateInputBsonBytes((const uint8_t *) VARDATA_ANY(bsonDocument),
						   VARSIZE_ANY_EXHDR(bsonDocument), validateFlag);
}


/*
 * Validates a user input bson as bytes to ensure it's compatible with the database.
 * This includes validating field names for the specified flags.
 */
void
ValidateInputBsonBytes(const uint8_t *documentBytes,
					   uint32_t documentBytesLength,
					   bson_validate_flags_t validateFlag)
{
	bson_t bson;
	if (!bson_init_static(&bson, documentBytes, documentBytesLength))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"invalid input syntax for BSON: Unable to initialize for validate")));
	}

	bson_error_t error;
	if (!bson_validate_with_error(&bson, validateFlag, &error))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON. Code: %u, Message %s",
							   error.code, error.message)));
	}
}


/*
 * Initializes a pgbson structure from a data buffer
 */
pgbson *
PgbsonInitFromBuffer(const char *buffer, uint32_t bufferLength)
{
	bson_iter_t bson;
	if (!bson_iter_init_from_data(&bson, (const uint8_t *) buffer, bufferLength))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}

	return CreatePgbsonfromBsonBytes((const uint8_t *) buffer, bufferLength);
}


/*
 * PgbsonInitEmpty initializes an empty pgbson document.
 */
pgbson *
PgbsonInitEmpty(void)
{
	bool destroyDocument = true;

	bson_t emptyBson;
	bson_init(&emptyBson);

	return CreatePgbsonfromBson_t(&emptyBson, destroyDocument);
}


/*
 * Initializes a pgbson structure from a bytea buffer.
 *
 * That means, it doesn't copy raw bytes of given bytea but returns a
 * pgbson view for it.
 */
pgbson *
CastByteaToPgbson(bytea *byteBuffer)
{
	/* right now both pgbson and bytea are varlena */
	return (pgbson *) byteBuffer;
}


/*
 * PgbsonInitFromIterDocumentValue initializes a pgbson structure from the
 * document that given iterator holds.
 */
pgbson *
PgbsonInitFromIterDocumentValue(const bson_iter_t *iter)
{
	/* copy just to ensure we don't modify the const iter */
	bson_iter_t iterCopy = *iter;
	const bson_value_t *value = bson_iter_value(&iterCopy);
	return PgbsonInitFromDocumentBsonValue(value);
}


/*
 * PgbsonInitFromIterDocumentValue initializes a pgbson structure from the
 * document that given value holds.
 */
pgbson *
PgbsonInitFromDocumentBsonValue(const bson_value_t *value)
{
	if (value->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errmsg("expected a document to create a bson object")));
	}

	return PgbsonInitFromBuffer((char *) value->value.v_doc.data,
								value->value.v_doc.data_len);
}


/*
 * PgbsonIterDocumentToJsonForLogging converts a bson_iter_t
 * to a loggable string for tracing/error handling purposes.
 */
const char *
PgbsonIterDocumentToJsonForLogging(const bson_iter_t *iter)
{
	if (!BSON_ITER_HOLDS_DOCUMENT(iter))
	{
		ereport(ERROR, (errmsg("expected a document to create a bson object")));
	}

	/* copy just to ensure we don't modify the const iter */
	bson_iter_t iterCopy = *iter;
	const bson_value_t *value = bson_iter_value(&iterCopy);

	bson_t bson;
	if (!bson_init_static(&bson, (const uint8_t *) value->value.v_doc.data,
						  value->value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}

	/* since bson strings are palloced - we can simply return the string created. */
	return bson_as_relaxed_extended_json(&bson, NULL);
}


/*
 * Initializes a pgbson structure from a pgbson
 */
pgbson *
PgbsonCloneFromPgbson(const pgbson *bson)
{
	return CreatePgbsonfromBsonBytes((const uint8_t *) VARDATA_ANY(bson),
									 VARSIZE_ANY_EXHDR(bson));
}


/*
 * Converts a pgbson structure to a bytea buffer
 *
 * That means, it doesn't copy raw bytes of given document but returns a
 * bytea view for it.
 */
bytea *
CastPgbsonToBytea(pgbson *bsonDocument)
{
	/* right now both pgbson and bytea are varlena */
	return (bytea *) bsonDocument;
}


/*
 * ConvertPgbsonToBsonValue returns a bson value based on given document.
 *
 * That means, it doesn't copy raw bytes of given document but returns a
 * bson value view for it.
 */
bson_value_t
ConvertPgbsonToBsonValue(const pgbson *document)
{
	bson_value_t value = {
		.value_type = BSON_TYPE_DOCUMENT,
		.value.v_doc.data = (uint8_t *) VARDATA_ANY(document),
		.value.v_doc.data_len = (uint32_t) VARSIZE_ANY_EXHDR(document)
	};

	return value;
}


/*
 * Initializes a bson iterator from a pgbson structure at a
 * specified dot-notation path. if the path does not exist, returns false.
 */
bool
PgbsonInitIteratorAtPath(const pgbson *bson, const char *path, bson_iter_t *iterator)
{
	bson_iter_t documentIterator;
	PgbsonInitIterator(bson, &documentIterator);
	return bson_iter_find_descendant(&documentIterator, path, iterator);
}


/*
 * Initializes a bson iterator from a pgbson structure at the root of the
 * buffer.
 */
void
PgbsonInitIterator(const pgbson *bson, bson_iter_t *iterator)
{
	if (!bson_iter_init_from_data(iterator, (const uint8_t *) VARDATA_ANY(bson),
								  VARSIZE_ANY_EXHDR(bson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}
}


/*
 * Initializes a bson iterator from given array / document value at the root
 * of the buffer.
 */
void
BsonValueInitIterator(const bson_value_t *value, bson_iter_t *iterator)
{
	if (value->value_type != BSON_TYPE_DOCUMENT && value->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errmsg("expected a document or array to init iterator")));
	}

	if (!bson_iter_init_from_data(iterator, value->value.v_doc.data,
								  value->value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("invalid input syntax for BSON")));
	}
}


/* --------------------------------------------------------- */
/* pgbson_writer functions */
/* --------------------------------------------------------- */

/*
 * Initializes a bson writer so that it's ready to write data
 */
void
PgbsonWriterInit(pgbson_writer *writer)
{
	bson_init(&(writer->innerBson));
}


/*
 * Initializes a bson writer on the heap using bson_new() so that it's ready to write data
 *
 * Note: Should be destroyed after usage with PgbsonHeapWriterFree()
 */
pgbson_heap_writer *
PgbsonHeapWriterInit()
{
	pgbson_heap_writer *writer = palloc0(sizeof(pgbson_heap_writer));
	writer->innerBsonRef = bson_new();
	return writer;
}


/*
 * Gets the length of the bson currently written into the writer.
 */
uint32_t
PgbsonWriterGetSize(pgbson_writer *writer)
{
	return writer->innerBson.len;
}


/*
 * Gets the length of the bson currently written into the array writer.
 */
uint32_t
PgbsonArrayWriterGetSize(pgbson_array_writer *writer)
{
	return writer->innerBson.len;
}


/*
 * Gets the length of the bson currently written into the heap writer.
 */
uint32_t
PgbsonHeapWriterGetSize(pgbson_heap_writer *writer)
{
	return writer->innerBsonRef->len;
}


/*
 * Copies the contents of the pgbson_writer to the target buffer.
 */
void
PgbsonWriterCopyToBuffer(pgbson_writer *writer, uint8_t *buffer, uint32_t length)
{
	const uint8_t *bytes = bson_get_data(&writer->innerBson);
	if (length < writer->innerBson.len)
	{
		ereport(ERROR, errmsg("Need at least %d bytes to serialize bson from writer",
							  writer->innerBson.len));
	}

	memcpy(buffer, bytes, writer->innerBson.len);
}


/*
 * Appends a given bson value to the writer with the specified path.
 */
void
PgbsonWriterAppendValue(pgbson_writer *writer, const char *path, uint32_t pathLength,
						const bson_value_t *value)
{
	if (!bson_append_value(&(writer->innerBson), path, pathLength, value))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding %s value: failed due to value being too large",
							BsonTypeName(value->value_type)))
				);
	}
}


/*
 * Appends given int64 to the writer with the specified path.
 */
void
PgbsonWriterAppendInt64(pgbson_writer *writer, const char *path, uint32_t pathLength,
						int64 value)
{
	if (!bson_append_int64(&(writer->innerBson), path, pathLength, value))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"adding Int64 value: failed due to value being too large")));
	}
}


/*
 * Appends a given integer to the writer with the specified path.
 */
void
PgbsonWriterAppendInt32(pgbson_writer *writer, const char *path, uint32_t pathLength,
						int value)
{
	if (!bson_append_int32(&(writer->innerBson), path, pathLength, value))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"adding Int32 value: failed due to value being too large")));
	}
}


/*
 * Appends given double to the writer with the specified path.
 */
void
PgbsonWriterAppendDouble(pgbson_writer *writer, const char *path, uint32_t pathLength,
						 double value)
{
	if (!bson_append_double(&(writer->innerBson), path, pathLength, value))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"adding Double value: failed due to value being too large")));
	}
}


/*
 * Appends a given string to the writer with the specified path.
 */
void
PgbsonWriterAppendUtf8(pgbson_writer *writer, const char *path, uint32_t pathLength,
					   const char *string)
{
	if (!bson_append_utf8(&(writer->innerBson), path, pathLength, string, strlen(string)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"adding UTF8 value: failed due to value being too large")));
	}
}


/*
 * Appends a given timestamp to the writer with the specified path.
 */
void
PgbsonWriterAppendTimestampTz(pgbson_writer *writer, const char *path, uint32_t
							  pathLength,
							  TimestampTz timestamp)
{
	/*
	 * NOTE: This is based on the PG timestamptz_to_time_t calculation and
	 * should be kept in sync with the calculation there.
	 */
	int64_t milliSecondsSinceEpoch = timestamp / 1000 +
									 ((POSTGRES_EPOCH_JDATE - UNIX_EPOCH_JDATE) *
									  (USECS_PER_DAY / 1000));

	if (!bson_append_date_time(&(writer->innerBson), path, pathLength,
							   milliSecondsSinceEpoch))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"adding TimeStamp value: failed due to value being too large")));
	}
}


/*
 * Appends given bool to the writer with the specified path.
 */
void
PgbsonWriterAppendBool(pgbson_writer *writer, const char *path, uint32_t pathLength,
					   bool value)
{
	if (!bson_append_bool(&(writer->innerBson), path, pathLength, value))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg(
							"adding Bool value: failed due to value being too large")));
	}
}


/*
 * Appends given bson document to the writer with the specified path.
 */
void
PgbsonWriterAppendDocument(pgbson_writer *writer, const char *path, uint32_t pathLength,
						   const pgbson *bson)
{
	bson_t rightBson;
	bson_init_static(&rightBson, (const uint8_t *) VARDATA_ANY(bson),
					 (uint32_t) VARSIZE_ANY_EXHDR(bson));
	if (!bson_append_document(&(writer->innerBson), path, pathLength, &rightBson))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("adding document: failed due to document "
							   "being too large")));
	}
}


/*
 * Appends a "start array" to the writer and returns a writer to append to the child array inserted.
 */
void
PgbsonWriterStartArray(pgbson_writer *writer, const char *path, uint32_t pathLength,
					   pgbson_array_writer *childWriter)
{
	if (!bson_append_array_begin(&(writer->innerBson), path, pathLength,
								 &(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding StartArray value: failed due to value being too large"))
				);
	}

	childWriter->index = 0;
}


/*
 * Appends a "start array" to the writer and returns a writer to append to the child array inserted.
 */
void
PgbsonHeapWriterStartArray(pgbson_heap_writer *writer, const char *path, uint32_t
						   pathLength,
						   pgbson_array_writer *childWriter)
{
	if (!bson_append_array_begin(writer->innerBsonRef, path, pathLength,
								 &(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding HeapWriterStartArray value: failed due to value being too large"))
				);
	}

	childWriter->index = 0;
}


/*
 * Finishes appending an array to the current writer.
 */
void
PgbsonWriterEndArray(pgbson_writer *writer, pgbson_array_writer *childWriter)
{
	if (!bson_append_array_end(&(writer->innerBson), &(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding EndArray value: failed due to value being too large"))
				);
	}
}


/*
 * Finishes appending an array to the current pgbson_heap_writer.
 */
void
PgbsonHeapWriterEndArray(pgbson_heap_writer *writer, pgbson_array_writer *childWriter)
{
	if (!bson_append_array_end(writer->innerBsonRef, &(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding End Array value: failed due to value being too large"))
				);
	}
}


/*
 * Starts writing a nested array in the context of a bson array
 */
void
PgbsonArrayWriterStartArray(pgbson_array_writer *writer, pgbson_array_writer *childWriter)
{
	char buffer[20];
	const char *key;
	uint32_t keyLength = bson_uint32_to_string(writer->index, &key, buffer,
											   sizeof buffer);
	if (!bson_append_array_begin(&(writer->innerBson), key, keyLength,
								 &(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding ArrayWriterStartArray value: failed due to value being too large"))
				);
	}

	childWriter->index = 0;
}


/*
 * Finishes appending a nested array to the current array writer.
 */
void
PgbsonArrayWriterEndArray(pgbson_array_writer *writer, pgbson_array_writer *childWriter)
{
	if (!bson_append_array_end(&(writer->innerBson), &(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding ArrayWriterEndArray value: failed due to value being too large"))
				);
	}

	writer->index++;
}


/*
 * Gets the underlying value pointed to by the current array writer.
 */
bson_value_t
PgbsonArrayWriterGetValue(pgbson_array_writer *writer)
{
	const uint8_t *bytes = bson_get_data(&writer->innerBson);
	bson_value_t value = {
		.value_type = BSON_TYPE_ARRAY,
		.value.v_doc.data = (uint8_t *) bytes,
		.value.v_doc.data_len = writer->innerBson.len
	};

	return value;
}


/* Allocates the necessary memory on the target bson_value_t and copies using memcpy the data
 * on the array writer into the allocated memory. */
void
PgbsonArrayWriterCopyDataToBsonValue(pgbson_array_writer *writer, bson_value_t *bsonValue)
{
	uint32_t writerSize = PgbsonArrayWriterGetSize(writer);

	if (writerSize == 0)
	{
		return;
	}

	bsonValue->value_type = BSON_TYPE_ARRAY;
	bsonValue->value.v_doc.data = palloc0(sizeof(uint8_t) * writerSize);
	bsonValue->value.v_doc.data_len = writerSize;

	memcpy(bsonValue->value.v_doc.data, bson_get_data(&writer->innerBson), writerSize);
}


/*
 * Appends a "Start document" to the current writer and returns the writer to append to the child document.
 */
void
PgbsonWriterStartDocument(pgbson_writer *writer, const char *path, uint32_t pathLength,
						  pgbson_writer *childWriter)
{
	if (!bson_append_document_begin(&(writer->innerBson), path, pathLength,
									&(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding StartDocument( value: failed due to value being too large"))
				);
	}
}


/*
 * Ends writing a document written with "Start document" to the current writer.
 */
void
PgbsonWriterEndDocument(pgbson_writer *writer, pgbson_writer *childWriter)
{
	if (!bson_append_document_end(&(writer->innerBson), &(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding EndDocument value: failed due to value being too large"))
				);
	}
}


/*
 * Appends a document to a nested array writer and returns the nested document writer.
 */
void
PgbsonArrayWriterStartDocument(pgbson_array_writer *writer, pgbson_writer *childWriter)
{
	char buffer[20];
	const char *key;
	uint32_t keyLength = bson_uint32_to_string(writer->index, &key, buffer,
											   sizeof buffer);
	if (!bson_append_document_begin(&(writer->innerBson), key, keyLength,
									&(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding ArrayWriterStartDocument value: failed due to value being too large"))
				);
	}
}


/*
 * Ends writing the document written with the StartDocument in an array.
 */
void
PgbsonArrayWriterEndDocument(pgbson_array_writer *writer, pgbson_writer *childWriter)
{
	if (!bson_append_document_end(&(writer->innerBson), &(childWriter->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding ArrayWriterEndDocument value: failed due to value being too large"))
				);
	}

	writer->index++;
}


/*
 * PgbsonArrayWriterWriteUtf8WithLength is responsible for writing a provided string to a nested array
 * at the current index, for the given length.
 */
void
PgbsonArrayWriterWriteUtf8WithLength(pgbson_array_writer *writer, const char *string, int
									 length)
{
	const bson_value_t utf8Value = {
		.value_type = BSON_TYPE_UTF8,
		.value.v_utf8.str = (char *) string,
		.value.v_utf8.len = length
	};

	PgbsonArrayWriterWriteValue(writer, &utf8Value);
}


/*
 * PgbsonArrayWriterWriteUtf8 writes given string to given nested array at
 * the current index.
 */
void
PgbsonArrayWriterWriteUtf8(pgbson_array_writer *writer, const char *string)
{
	const bson_value_t utf8Value = {
		.value_type = BSON_TYPE_UTF8,
		.value.v_utf8.str = (char *) string,
		.value.v_utf8.len = strlen(string)
	};

	PgbsonArrayWriterWriteValue(writer, &utf8Value);
}


/*
 * Gets the index from Array Writer
 *
 */
uint32_t
PgbsonArrayWriterGetIndex(pgbson_array_writer *writer)
{
	return writer->index;
}


/*
 * Writes a value to a nested array at the current index.
 */
void
PgbsonArrayWriterWriteValue(pgbson_array_writer *writer,
							const bson_value_t *value)
{
	char buffer[20];
	const char *key;
	uint32_t keyLength = bson_uint32_to_string(writer->index, &key, buffer,
											   sizeof buffer);
	if (!bson_append_value(&(writer->innerBson), key, keyLength, value))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding ArrayWriterWriteValue %s value: failed due to value being too large",
							BsonTypeName(value->value_type)))
				);
	}
	writer->index++;
}


/*
 * Writes bson null to a nested array at the current index.
 */
void
PgbsonArrayWriterWriteNull(pgbson_array_writer *writer)
{
	bson_value_t nullValue;
	memset(&nullValue, 0, sizeof(bson_value_t));
	nullValue.value_type = BSON_TYPE_NULL;

	PgbsonArrayWriterWriteValue(writer, &nullValue);
}


/*
 * Writes a bson document to a nested array at the current index.
 */
void
PgbsonArrayWriterWriteDocument(pgbson_array_writer *writer,
							   const pgbson *bson)
{
	bson_t rightBson;
	bson_init_static(&rightBson, (const uint8_t *) VARDATA_ANY(bson),
					 (uint32_t) VARSIZE_ANY_EXHDR(bson));

	char buffer[20];
	const char *key;
	uint32_t keyLength = bson_uint32_to_string(writer->index, &key, buffer,
											   sizeof buffer);
	if (!bson_append_document(&(writer->innerBson), key, keyLength, &rightBson))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						errmsg("adding document: failed due to document "
							   "being too large")));
	}
	writer->index++;
}


/*
 * Appends an empty array to the writer with a specified path.
 */
void
PgbsonWriterAppendEmptyArray(pgbson_writer *writer, const char *path, uint32_t pathLength)
{
	bson_t emptyArray;
	bson_init(&emptyArray);
	if (!bson_append_array(&(writer->innerBson), path, pathLength, &emptyArray))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding AppendEmptyArray value: failed due to value being too large"))
				);
	}
}


/*
 * Appends a bson value as a single element array to pgbson writer.
 */
void
PgbsonWriterAppendBsonValueAsArray(pgbson_writer *writer, const char *path, uint32_t
								   pathLength, const bson_value_t *value)
{
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(writer, path, pathLength, &arrayWriter);
	PgbsonArrayWriterWriteValue(&arrayWriter, value);
	PgbsonWriterEndArray(writer, &arrayWriter);
}


/*
 * Appends a bson value as a single element array to pgbson heap writer.
 */
void
PgbsonHeapWriterAppendBsonValueAsArray(pgbson_heap_writer *writer, const char *path,
									   uint32_t
									   pathLength, const bson_value_t *value)
{
	pgbson_array_writer arrayWriter;
	PgbsonHeapWriterStartArray(writer, path, pathLength, &arrayWriter);
	PgbsonArrayWriterWriteValue(&arrayWriter, value);
	PgbsonHeapWriterEndArray(writer, &arrayWriter);
}


/*
 * Appends a given null value to the writer with the specified path.
 */
void
PgbsonWriterAppendNull(pgbson_writer *writer, const char *path, uint32_t pathLength)
{
	if (!bson_append_null(&(writer->innerBson), path, pathLength))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding AppendEmptyArray value: failed due to value being too large"))
				);
	}
}


/*
 * Appends a given bson_iter_t to the writer
 */
void
PgbsonWriterAppendIter(pgbson_writer *writer, const bson_iter_t *iter)
{
	/* No key is necessary as it picks the key from the iterator */
	if (!bson_append_iter(&(writer->innerBson), NULL, -1, iter))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"adding iter: failed due to value being too large"))
				);
	}
}


/* Allocates the necessary memory in the target bson_value_t document data and copies the writer bytes into it. */
void
PgbsonWriterCopyDocumentDataToBsonValue(pgbson_writer *writer, bson_value_t *bsonValue)
{
	uint32_t writerSize = PgbsonWriterGetSize(writer);

	if (writerSize == 0)
	{
		return;
	}

	bsonValue->value_type = BSON_TYPE_DOCUMENT;
	bsonValue->value.v_doc.data = palloc0(sizeof(uint8_t) * writerSize);
	bsonValue->value.v_doc.data_len = writerSize;

	memcpy(bsonValue->value.v_doc.data, bson_get_data(&writer->innerBson), writerSize);
}


/*
 * Finalizes the pgbson_writer and creates a pgbson structure from the writer.
 * The writer is deemed unusable after this point.
 */
pgbson *
PgbsonWriterGetPgbson(pgbson_writer *writer)
{
	return CreatePgbsonfromBson_t(&(writer->innerBson), true);
}


/*
 * Gets an iterator over the current pgbson_writer.
 */
void
PgbsonWriterGetIterator(pgbson_writer *writer, bson_iter_t *iterator)
{
	bson_iter_init(iterator, &writer->innerBson);
}


/*
 * Frees any underlying buffers held by this writer.
 * The writer is deemed unusable after this point.
 */
void
PgbsonWriterFree(pgbson_writer *writer)
{
	bson_destroy(&writer->innerBson);
}


/*
 * Frees any underlying buffers held by this heap writer.
 * The writer is deemed unusable after this point.
 */
void
PgbsonHeapWriterFree(pgbson_heap_writer *writer)
{
	bson_destroy(writer->innerBsonRef);
}


/*
 * Concatenates a pgbson structure with the contents of the current writer.
 * All the fields of the bson structure are copied in to the same level.
 */
void
PgbsonWriterConcat(pgbson_writer *writer, const pgbson *bson)
{
	PgbsonWriterConcatBytes(writer, (const uint8_t *) VARDATA_ANY(bson),
							(uint32_t) VARSIZE_ANY_EXHDR(bson));
}


/*
 * Concatenates a pgbson_writer's inner bufferwith the contents of the current writer.
 * All the fields of the bson structure are copied in to the same level.
 */
void
PgbsonWriterConcatWriter(pgbson_writer *writer, pgbson_writer *writerToConcat)
{
	if (!bson_concat(&(writer->innerBson), &(writerToConcat->innerBson)))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"ConcatWriter concatenating bson: failed due to value being too large"))
				);
	}
}


/*
 * Concatenates a pgbson_writer's inner bufferwith the contents of the current pgbson_heap_writer.
 * All the fields of the bson structure are copied in to the same level.
 */
void
PgbsonWriterConcatHeapWriter(pgbson_writer *writer, pgbson_heap_writer *writerToConcat)
{
	if (!bson_concat(&(writer->innerBson), writerToConcat->innerBsonRef))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"ConcatHeapWriter concatenating bson: failed due to value being too large"))
				);
	}
}


void
PgbsonWriterConcatBytes(pgbson_writer *writer, const uint8_t *bsonBytes, uint32_t
						bsonBytesLength)
{
	bson_t rightBson;
	bson_init_static(&rightBson, bsonBytes, bsonBytesLength);
	if (!bson_concat(&(writer->innerBson), &rightBson))
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg(
							"ConcatBytesWriter concatenating bson: failed due to value being too large"))
				);
	}
}


/*
 * Initializes an elementwriter to write to the current index of an array.
 */
void
PgbsonInitArrayElementWriter(pgbson_array_writer *arrayWriter,
							 pgbson_element_writer *elementWriter)
{
	elementWriter->isArray = true;
	elementWriter->arrayWriter = arrayWriter;
}


/*
 * Initializes an elementwriter to write to the specified field of an object.
 */
void
PgbsonInitObjectElementWriter(pgbson_writer *objectWriter,
							  pgbson_element_writer *elementWriter,
							  const char *path, uint32_t pathLength)
{
	elementWriter->isArray = false;
	elementWriter->objectWriterState.objectWriter = objectWriter;
	elementWriter->objectWriterState.path = path;
	elementWriter->objectWriterState.pathLength = pathLength;
}


/*
 * Write a bson_value_t to the current element (either object or array).
 */
void
PgbsonElementWriterWriteValue(pgbson_element_writer *elementWriter,
							  const bson_value_t *value)
{
	if (elementWriter->isArray)
	{
		PgbsonArrayWriterWriteValue(elementWriter->arrayWriter, value);
	}
	else
	{
		PgbsonWriterAppendValue(elementWriter->objectWriterState.objectWriter,
								elementWriter->objectWriterState.path,
								elementWriter->objectWriterState.pathLength,
								value);
	}
}


/*
 * Gets the value stored in the current pgbson_element_writer
 * based on the field pointed to by the writer.
 * Returns BSON_TYPE_EOD if not found.
 */
bson_value_t
PgbsonElementWriterGetValue(pgbson_element_writer *elementWriter)
{
	char pathPointer[UINT32_MAX_STR_LEN];
	const char *pathString = NULL;
	uint32_t pathStringLength = 0;
	bson_t *innerBson = NULL;

	if (elementWriter->isArray)
	{
		pathStringLength = bson_uint32_to_string(
			elementWriter->arrayWriter->index,
			&pathString, &pathPointer[0], UINT32_MAX_STR_LEN);
		innerBson = &elementWriter->arrayWriter->innerBson;
	}
	else
	{
		pathString = elementWriter->objectWriterState.path;
		pathStringLength = elementWriter->objectWriterState.pathLength;
		innerBson = &elementWriter->objectWriterState.objectWriter->innerBson;
	}

	bson_iter_t iterator;
	if (bson_iter_init_find_w_len(&iterator, innerBson, pathString, pathStringLength))
	{
		return *bson_iter_value(&iterator);
	}

	bson_value_t returnValue = { 0 };
	return returnValue;
}


/*
 * Write a start array to the current element (either object or array).
 */
void
PgbsonElementWriterStartArray(pgbson_element_writer *elementWriter,
							  pgbson_array_writer *startArray)
{
	if (elementWriter->isArray)
	{
		PgbsonArrayWriterStartArray(elementWriter->arrayWriter,
									startArray);
	}
	else
	{
		PgbsonWriterStartArray(elementWriter->objectWriterState.objectWriter,
							   elementWriter->objectWriterState.path,
							   elementWriter->objectWriterState.pathLength,
							   startArray);
	}
}


/*
 * Writes the end array to the current element (either object or array).
 */
void
PgbsonElementWriterEndArray(pgbson_element_writer *elementWriter,
							pgbson_array_writer *arrayWriter)
{
	if (elementWriter->isArray)
	{
		PgbsonArrayWriterEndArray(elementWriter->arrayWriter,
								  arrayWriter);
	}
	else
	{
		PgbsonWriterEndArray(elementWriter->objectWriterState.objectWriter,
							 arrayWriter);
	}
}


/*
 * Write a start document to the current element (either object or array).
 */
void
PgbsonElementWriterStartDocument(pgbson_element_writer *elementWriter,
								 pgbson_writer *startDocument)
{
	if (elementWriter->isArray)
	{
		PgbsonArrayWriterStartDocument(elementWriter->arrayWriter,
									   startDocument);
	}
	else
	{
		PgbsonWriterStartDocument(elementWriter->objectWriterState.objectWriter,
								  elementWriter->objectWriterState.path,
								  elementWriter->objectWriterState.pathLength,
								  startDocument);
	}
}


/*
 * Write the end document to the current element (either object or array).
 */
void
PgbsonElementWriterEndDocument(pgbson_element_writer *elementWriter,
							   pgbson_writer *endDocument)
{
	if (elementWriter->isArray)
	{
		PgbsonArrayWriterEndDocument(elementWriter->arrayWriter,
									 endDocument);
	}
	else
	{
		PgbsonWriterEndDocument(elementWriter->objectWriterState.objectWriter,
								endDocument);
	}
}


/*
 * Converts a bson_value_t into a serializable pgbson bson document using the format
 * { "": <value> } - this is equivalent to writing a single pgbson_element with the empty path
 */
pgbson *
BsonValueToDocumentPgbson(const bson_value_t *value)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendValue(&writer, "", 0, value);
	pgbson *bson = PgbsonWriterGetPgbson(&writer);
	return bson;
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

/*
 * Creates a pgbson structure from a libbson bson_t type.
 * the bson_t is optionally destroyed and the memory reclaimed after conversion.
 */
pgbson *
CreatePgbsonfromBson_t(bson_t *document, bool destroyDocument)
{
	pgbson *pgbsonValue = CreatePgbsonfromBsonBytes(bson_get_data(document),
													document->len);
	if (destroyDocument)
	{
		bson_destroy(document);
	}

	return pgbsonValue;
}


/*
 * Creates a pgbson structure from a raw buffer of bytes
 */
pgbson *
CreatePgbsonfromBsonBytes(const uint8_t *rawbytes, uint32_t length)
{
	int bson_size = length + VARHDRSZ;
	pgbson *pgbsonVal = (pgbson *) palloc(bson_size);
	SET_VARSIZE(pgbsonVal, bson_size);
	memcpy(VARDATA(pgbsonVal), rawbytes, length);
	return pgbsonVal;
}


/*
 * CopyPgbsonIntoMemoryContext can be used to copy a pgbson into a
 * long-lived memory context.
 */
pgbson *
CopyPgbsonIntoMemoryContext(const pgbson *document, MemoryContext context)
{
	pgbson *copiedDoc = MemoryContextAlloc(context, VARSIZE_ANY(document));
	memcpy(copiedDoc, document, VARSIZE_ANY(document));

	return copiedDoc;
}


/*
 * PgbsonHasDocumentId returns whether a pgbson has the _id field set.
 */
bool
PgbsonHasDocumentId(const pgbson *document)
{
	bson_iter_t it;
	return PgbsonInitIteratorAtPath(document, "_id", &it);
}


/*
 * Similar to PgbsonHasDocumentId but for bson_values
 */
bool
DocumentBsonValueHasDocumentId(const bson_value_t *document)
{
	if (document->value_type != BSON_TYPE_DOCUMENT)
	{
		return false;
	}

	bson_iter_t it;
	BsonValueInitIterator(document, &it);
	return bson_iter_find(&it, "_id");
}


/*
 * PgbsonGetDocumentId returns the _id of a BSON document in projected form
 * as a bytea.
 */
pgbson *
PgbsonGetDocumentId(const pgbson *document)
{
	bson_iter_t iterator;
	if (!PgbsonInitIteratorAtPath(document, "_id", &iterator))
	{
		ereport(ERROR, (errmsg("unexpected: document does not have an _id")));
	}

	const bson_value_t *objectIdValue = bson_iter_value(&iterator);
	return BsonValueToDocumentPgbson(objectIdValue);
}


/*
 * Initializes a BSON value as an empty array.
 * outValue - Pointer to the BSON value to initialize.
 */
void
InitBsonValueAsEmptyArray(bson_value_t *outValue)
{
	const pgbson *emptyDoc = PgbsonInitEmpty();
	outValue->value_type = BSON_TYPE_ARRAY;
	outValue->value.v_doc.data_len = VARSIZE_ANY_EXHDR(emptyDoc);
	outValue->value.v_doc.data = (uint8_t *) VARDATA_ANY(emptyDoc);
}


/*
 * Validate if the bson inside pgbson_writer is an empty document.
 * Note that the bson spec (https://bsonspec.org/spec.html) implies that an array has
 * 4 bytes for the array length, followed by a list of [ 1 byte type code, path, value]
 * This implies that a non empty array has 4 bytes of length, 1 byte of type code
 * (for the first element), and at least 1 byte for the index path which is 6 bytes at least.
 */
bool
IsPgbsonWriterEmptyDocument(pgbson_writer *writer)
{
	return writer != NULL && PgbsonWriterGetSize(writer) < 6;
}


/*
 * Validate if the bson inside pgbson_heap_writer is an empty document.
 * Note that the bson spec (https://bsonspec.org/spec.html) implies that an array has
 * 4 bytes for the array length, followed by a list of [ 1 byte type code, path, value]
 * This implies that a non empty array has 4 bytes of length, 1 byte of type code
 * (for the first element), and at least 1 byte for the index path which is 6 bytes at least.
 */
bool
IsPgbsonHeapWriterEmptyDocument(pgbson_heap_writer *writer)
{
	return writer != NULL && PgbsonHeapWriterGetSize(writer) < 6;
}
