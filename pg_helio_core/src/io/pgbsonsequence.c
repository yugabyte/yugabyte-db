/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/io/bsonsequence_io.c
 *
 * Implementation of the BSON Sequence type input and output functions
 * and manipulating BSON Sequences.
 *
 * Note that BsonSequences are raw streams of bson used in I/O in native Mongo.
 * See http://mongoc.org/libbson/current/bson_reader_t.html
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <utils/builtins.h>
#include <lib/stringinfo.h>

#define PRIVATE_PGBSON_H
#include "io/pgbson.h"
#undef PRIVATE_PGBSON_H

#include "io/pgbsonsequence.h"
#include "io/pgbsonelement.h"
#include "io/bson_set_returning_functions.h"
#include "utils/mongo_errors.h"

extern char * BsonTypeName(bson_type_t type);

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

static const char *BsonSeqHexPrefix = "SEQHEX";
static const uint32_t BsonSeqHexPrefixLength = 6;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static bool IsBsonSequenceHexadecimalString(const char *string);
static pgbsonsequence * PgbsonSequenceInitFromJson(const char *jsonString);
static pgbsonsequence * PgbsonSequenceInitFromHexadecimalString(const char *hexString);
static const char * PgbsonSequenceToHexadecimalString(const pgbsonsequence *bsonSequence);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(bsonsequence_out);
PG_FUNCTION_INFO_V1(bsonsequence_in);
PG_FUNCTION_INFO_V1(bsonsequence_recv);
PG_FUNCTION_INFO_V1(bsonsequence_send);
PG_FUNCTION_INFO_V1(bsonsequence_from_bytea);
PG_FUNCTION_INFO_V1(bsonsequence_to_bytea);
PG_FUNCTION_INFO_V1(bsonsequence_get_bson);
PG_FUNCTION_INFO_V1(bson_to_bsonsequence);


/*
 * bsonsequence_out converts a binary serialized bson document sequence
 * to the extended json text format.
 */
Datum
bsonsequence_out(PG_FUNCTION_ARGS)
{
	pgbsonsequence *arg = PG_GETARG_PGBSON_SEQUENCE(0);

	const char *hexString = PgbsonSequenceToHexadecimalString(arg);

	PG_RETURN_CSTRING(hexString);
}


/*
 * bsonsequence_in converts an extended json text format string
 * to a binary serialized bsonsequence.
 */
Datum
bsonsequence_in(PG_FUNCTION_ARGS)
{
	char *arg = PG_GETARG_CSTRING(0);

	pgbsonsequence *bsonSequence;
	if (arg == NULL)
	{
		bsonSequence = NULL;
	}
	else if (IsBsonSequenceHexadecimalString(arg))
	{
		bsonSequence = PgbsonSequenceInitFromHexadecimalString(arg);
	}
	else
	{
		/* It's a json string use json deserialization */
		bsonSequence = PgbsonSequenceInitFromJson(arg);
	}

	PG_RETURN_POINTER(bsonSequence);
}


/*
 * bsonsequence_recv is the internal receive function for bsonsequence.
 */
Datum
bsonsequence_recv(PG_FUNCTION_ARGS)
{
	StringInfo buf = (StringInfo) PG_GETARG_POINTER(0);

	int allocSize = buf->len + VARHDRSZ;
	pgbsonsequence *sequenceValue = (pgbsonsequence *) palloc(allocSize);
	SET_VARSIZE(sequenceValue, allocSize);

	memcpy(VARDATA(sequenceValue), buf->data, buf->len);

	/* let caller know we consumed whole buffer */
	buf->cursor = buf->len;
	PG_RETURN_POINTER(sequenceValue);
}


/*
 * bsonsequence_send is the internal send function for bsonsequence.
 */
Datum
bsonsequence_send(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(PG_GETARG_PGBSON_SEQUENCE(0));
}


/*
 * Converts a bytea into a pgbsonsequence.
 * Since both are varlena - we simply cast from one to the other.
 */
Datum
bsonsequence_from_bytea(PG_FUNCTION_ARGS)
{
	bytea *buf = PG_GETARG_BYTEA_P(0);
	PG_RETURN_POINTER((pgbsonsequence *) buf);
}


/*
 * Converts a pgbsonsequence to a bytea.
 * Since both are varlena - we simply cast from one to the other.
 */
Datum
bsonsequence_to_bytea(PG_FUNCTION_ARGS)
{
	pgbsonsequence *buf = PG_GETARG_PGBSON_SEQUENCE(0);
	PG_RETURN_POINTER((bytea *) buf);
}


/*
 * Gets the set of documents that are serialized by the bsonsequence.
 */
Datum
bsonsequence_get_bson(PG_FUNCTION_ARGS)
{
	pgbsonsequence *sequence = PG_GETARG_PGBSON_SEQUENCE(0);
	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);

	List *docsList = PgbsonSequenceGetDocumentBsonValues(sequence);


	ListCell *cell;
	foreach(cell, docsList)
	{
		const bson_value_t *docValue = lfirst(cell);
		Datum values[1];
		bool nulls[1];

		values[0] = PointerGetDatum(PgbsonInitFromDocumentBsonValue(docValue));
		nulls[0] = false;

		tuplestore_putvalues(tupleStore, descriptor, values, nulls);
	}

	PG_RETURN_VOID();
}


/*
 * Creates a bsonsequence from a single pgbson.
 */
Datum
bson_to_bsonsequence(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON(0);
	bson_writer_t *writer;
	uint8_t *buf = NULL;
	size_t buflen = 0;

	writer = bson_writer_new(&buf, &buflen, 0, bson_realloc_ctx, NULL);

	bson_value_t currentValue = ConvertPgbsonToBsonValue(bson);
	bson_t *doc;
	bson_t currentDoc;

	bson_writer_begin(writer, &doc);

	if (!bson_init_static(&currentDoc, currentValue.value.v_doc.data,
						  currentValue.value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg("Failed to initialize single bson value")));
	}

	bson_concat(doc, &currentDoc);
	bson_writer_end(writer);
	buflen = bson_writer_get_length(writer);
	bson_writer_destroy(writer);

	uint32_t sequenceSize = VARHDRSZ + buflen;
	pgbsonsequence *sequence = palloc(sequenceSize);
	SET_VARSIZE(sequence, sequenceSize);
	memcpy(VARDATA(sequence), buf, buflen);

	bson_free(buf);
	PG_RETURN_POINTER(sequence);
}


/*
 * Creates a list of bson_value_t for the documents that are stored in
 * the bsonsequence.
 */
List *
PgbsonSequenceGetDocumentBsonValues(const pgbsonsequence *bsonSequence)
{
	const uint8_t *data = (const uint8_t *) VARDATA_ANY(bsonSequence);
	uint32_t dataSize = VARSIZE_ANY_EXHDR(bsonSequence);

	List *docsList = NIL;
	bson_reader_t *reader = bson_reader_new_from_data(data, dataSize);
	while (true)
	{
		const bson_t *document = bson_reader_read(reader, NULL);
		if (document == NULL)
		{
			break;
		}

		bson_value_t *valuePointer = palloc(sizeof(bson_value_t));
		valuePointer->value_type = BSON_TYPE_DOCUMENT;
		valuePointer->value.v_doc.data = (uint8_t *) bson_get_data(document);
		valuePointer->value.v_doc.data_len = document->len;
		docsList = lappend(docsList, valuePointer);
	}

	bson_reader_destroy(reader);
	return docsList;
}


/*
 * Checks whether the string representation of the bson sequence
 * is hex encoded or not.
 */
static bool
IsBsonSequenceHexadecimalString(const char *string)
{
	return string[0] == BsonSeqHexPrefix[0];
}


/*
 * Initializes a bsonsequence from a text hexadecimal string
 * representation.
 */
static pgbsonsequence *
PgbsonSequenceInitFromHexadecimalString(const char *hexString)
{
	uint32_t strLength = strlen(hexString);

	uint32_t hexStringLength = (strLength - BsonSeqHexPrefixLength);
	if (hexStringLength <= 0 || (hexStringLength % 2 != 0))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"Invalid Hex string for pgbson input")));
	}

	if (strncmp(hexString, BsonSeqHexPrefix, BsonSeqHexPrefixLength) != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("BsonSequence Hex string does not have valid prefix %s",
							   BsonSeqHexPrefix)));
	}

	uint32_t binaryLength = hexStringLength / 2;

	int allocSize = binaryLength + VARHDRSZ;
	pgbsonsequence *pgbsonVal = (pgbsonsequence *) palloc(allocSize);

	uint64 actualBinarySize = hex_decode(&hexString[BsonSeqHexPrefixLength],
										 hexStringLength, VARDATA(pgbsonVal));
	Assert(actualBinarySize == binaryLength);
	SET_VARSIZE(pgbsonVal, (uint32_t) actualBinarySize + VARHDRSZ);
	return pgbsonVal;
}


/*
 * Converts a bsonsequence to a text hexadecimal string
 * representation.
 */
static const char *
PgbsonSequenceToHexadecimalString(const pgbsonsequence *bsonSequence)
{
	size_t binarySize = VARSIZE_ANY_EXHDR(bsonSequence);
	size_t hexEncodedSize = binarySize * 2;
	size_t hexStringSize = hexEncodedSize + 1 + BsonSeqHexPrefixLength; /* add \0 and prefix "SEQHEX"; */
	char *hexString = palloc(hexStringSize);

	memcpy(hexString, BsonSeqHexPrefix, BsonSeqHexPrefixLength);

	const char *pgbsonData = VARDATA_ANY(bsonSequence);

	uint64 hexStringActualSize = hex_encode(pgbsonData, binarySize,
											&hexString[BsonSeqHexPrefixLength]);
	Assert(hexStringActualSize == hexEncodedSize);

	hexString[hexStringActualSize + BsonSeqHexPrefixLength] = 0;
	return hexString;
}


/*
 * Initializes a bsonsequence from a text json string
 * representation. This is expected to be a single value
 * array that contains documents.
 * (e.g. { "": [ { "a": 1 }]})
 * Primarily used for testing.
 */
static pgbsonsequence *
PgbsonSequenceInitFromJson(const char *jsonString)
{
	pgbson *bson = PgbsonInitFromJson(jsonString);
	pgbsonelement element;
	PgbsonToSinglePgbsonElement(bson, &element);

	if (element.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg("Json value for bsonsequence must be an array. got %s",
							   BsonTypeName(element.bsonValue.value_type))));
	}

	bson_iter_t arrayIterator;

	bson_writer_t *writer;
	uint8_t *buf = NULL;
	size_t buflen = 0;
	bson_t *doc;
	bson_t currentbson;

	writer = bson_writer_new(&buf, &buflen, 0, bson_realloc_ctx, NULL);

	BsonValueInitIterator(&element.bsonValue, &arrayIterator);
	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *currentValue = bson_iter_value(&arrayIterator);
		if (currentValue->value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
							errmsg("bsonsequence must be an array of documents. got %s",
								   BsonTypeName(currentValue->value_type))));
		}

		if (!bson_writer_begin(writer, &doc))
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("Could not initialize bson writer for sequence")));
		}

		if (!bson_init_static(&currentbson,
							  currentValue->value.v_doc.data,
							  currentValue->value.v_doc.data_len))
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("Could not initialize bson from value")));
		}

		if (!bson_concat(doc, &currentbson))
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg(
								"Could not write value into bson writer for sequence")));
		}

		bson_writer_end(writer);
	}

	buflen = bson_writer_get_length(writer);
	bson_writer_destroy(writer);

	uint32_t sequenceSize = VARHDRSZ + buflen;
	pgbsonsequence *sequence = palloc(sequenceSize);
	SET_VARSIZE(sequence, sequenceSize);
	memcpy(VARDATA(sequence), buf, buflen);

	bson_free(buf);
	return sequence;
}
