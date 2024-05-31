/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/io/bson_io.c
 *
 * Implementation of the BSON type input and output functions and manipulating BSON.
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
#include <utils/uuid.h>

#include "utils/type_cache.h"
#include "utils/mongo_errors.h"
#include "io/helio_bson_core.h"


extern bool BsonTextUseJsonRepresentation;


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/* state for bson_object_keys */
typedef struct BsonObjectKeysState
{
	Datum *result;
	int resultSize;
	int resultCount;
	int sentCount;
} BsonObjectKeysState;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(bson_out);
PG_FUNCTION_INFO_V1(bson_in);
PG_FUNCTION_INFO_V1(bson_recv);
PG_FUNCTION_INFO_V1(bson_send);
PG_FUNCTION_INFO_V1(bson_get_value);
PG_FUNCTION_INFO_V1(bson_get_value_text);
PG_FUNCTION_INFO_V1(bson_from_bytea);
PG_FUNCTION_INFO_V1(bson_to_bytea);
PG_FUNCTION_INFO_V1(bson_object_keys);
PG_FUNCTION_INFO_V1(row_get_bson);
PG_FUNCTION_INFO_V1(bson_repath_and_build);
PG_FUNCTION_INFO_V1(bson_to_bson_hex);
PG_FUNCTION_INFO_V1(bson_hex_to_bson);
PG_FUNCTION_INFO_V1(bson_json_to_bson);
PG_FUNCTION_INFO_V1(bson_to_json_string);


/*
 * bson_out converts a binary serialized bson document to the extended json text format.
 */
Datum
bson_out(PG_FUNCTION_ARGS)
{
	pgbson *arg = PG_GETARG_PGBSON(0);

	const char *jsonString;
	if (BsonTextUseJsonRepresentation)
	{
		jsonString = PgbsonToCanonicalExtendedJson(arg);
	}
	else
	{
		jsonString = PgbsonToHexadecimalString(arg);
	}

	PG_RETURN_CSTRING(jsonString);
}


/*
 * bson_in converts an extended json text format string to a binary serialized bson document.
 */
Datum
bson_in(PG_FUNCTION_ARGS)
{
	char *arg = PG_GETARG_CSTRING(0);

	pgbson *bson;
	if (arg == NULL)
	{
		bson = NULL;
	}
	else if (IsBsonHexadecimalString(arg))
	{
		bson = PgbsonInitFromHexadecimalString(arg);
	}
	else
	{
		/* It's a json string use json deserialization */
		bson = PgbsonInitFromJson(arg);
	}

	PG_RETURN_POINTER(bson);
}


/*
 * Converts a bson to a hex string representation.
 */
Datum
bson_to_bson_hex(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON(0);
	const char *hexString = PgbsonToHexadecimalString(bson);
	PG_RETURN_CSTRING(hexString);
}


/*
 * Converts a bson hex string to a pgbson.
 */
Datum
bson_hex_to_bson(PG_FUNCTION_ARGS)
{
	char *hexString = PG_GETARG_CSTRING(0);
	pgbson *bson = PgbsonInitFromHexadecimalString(hexString);
	PG_RETURN_POINTER(bson);
}


/*
 * Converts a bson extended json string to a pgbson.
 */
Datum
bson_json_to_bson(PG_FUNCTION_ARGS)
{
	text *jsonText = PG_GETARG_TEXT_P(0);
	const char *jsonString = text_to_cstring(jsonText);
	pgbson *bson = PgbsonInitFromJson(jsonString);
	PG_RETURN_POINTER(bson);
}


/*
 * bson_to_json_string converts a pgbson to a bson extended json string.
 */
Datum
bson_to_json_string(PG_FUNCTION_ARGS)
{
	pgbson *arg = PG_GETARG_PGBSON(0);
	const char *jsonString = PgbsonToCanonicalExtendedJson(arg);
	PG_RETURN_CSTRING(jsonString);
}


/*
 * bson_recv is the internal receive function for bson.
 * TODO: This is still relatively untested and is used only in the CREATE TYPE.
 * This function still needs to be validated and tested
 */
Datum
bson_recv(PG_FUNCTION_ARGS)
{
	StringInfo buf = (StringInfo) PG_GETARG_POINTER(0);
	pgbson *bsonValue = PgbsonInitFromBuffer(buf->data, buf->len);

	/* let caller know we consumed whole buffer */
	buf->cursor = buf->len;
	PG_RETURN_POINTER(bsonValue);
}


/*
 * bson_send is the internal send function for bson.
 * TODO: This is still relatively untested and is used only in the CREATE TYPE.
 * This function still needs to be validated and tested
 */
Datum
bson_send(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(PG_GETARG_PGBSON(0));
}


/*
 * bson_get_value returns the value of a path within a BSON document as another
 * BSON document.
 */
Datum
bson_get_value(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	char *path = text_to_cstring(PG_GETARG_TEXT_PP(1));
	bson_iter_t pathIterator;

	if (PgbsonInitIteratorAtPath(document, path, &pathIterator))
	{
		const bson_value_t *value = bson_iter_value(&pathIterator);
		PG_RETURN_POINTER(BsonValueToDocumentPgbson(value));
	}
	else
	{
		PG_RETURN_NULL();
	}
}


/*
 * bson_get_value_text returns the value of a path as 'text' within a BSON document which can be further type casted
 * to other native postgres data types
 *
 * Note: Only numbers, boolean and string values are returned as text representation of value other types are still returned as objects e.g.: ObjectId etc
 */
Datum
bson_get_value_text(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	char *path = text_to_cstring(PG_GETARG_TEXT_PP(1));
	bson_iter_t pathIterator;

	if (PgbsonInitIteratorAtPath(document, path, &pathIterator))
	{
		const bson_value_t *value = bson_iter_value(&pathIterator);
		const char *strRepr = BsonValueToJsonForLogging(value);
		PG_RETURN_POINTER(cstring_to_text(strRepr));
	}
	else
	{
		PG_RETURN_NULL();
	}
}


/*
 * bson_from_bytea returns bson from an input byte array.
 */
Datum
bson_from_bytea(PG_FUNCTION_ARGS)
{
	bytea *buf = PG_GETARG_BYTEA_P(0);
	PG_RETURN_POINTER(CastByteaToPgbson(buf));
}


/*
 * bson_to_bytea returns bytea from an input bson.
 */
Datum
bson_to_bytea(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON(0);
	PG_RETURN_BYTEA_P(CastPgbsonToBytea(bson));
}


/*
 * Converts a PostgreSQL row to a bson document
 */
Datum
row_get_bson(PG_FUNCTION_ARGS)
{
	Datum record = PG_GETARG_DATUM(0);

	/* Extract data about the row. */
	pgbson_writer writer;
	HeapTupleHeader tupleHeader = DatumGetHeapTupleHeader(record);
	Oid tupleType = HeapTupleHeaderGetTypeId(tupleHeader);
	int32 tupleTypeMod = HeapTupleHeaderGetTypMod(tupleHeader);
	TupleDesc tupleDescriptor = lookup_rowtype_tupdesc(tupleType, tupleTypeMod);
	HeapTupleData tupleValue;
	tupleValue.t_len = HeapTupleHeaderGetDatumLength(tupleHeader);
	tupleValue.t_data = tupleHeader;

	/* walk the row and build the bson document. */
	PgbsonWriterInit(&writer);
	for (int i = 0; i < tupleDescriptor->natts; i++)
	{
		bool isnull;
		if (tupleDescriptor->attrs[i].attisdropped)
		{
			continue;
		}

		/* get the SQL value. */
		Datum fieldValue = heap_getattr(&tupleValue, i + 1, tupleDescriptor, &isnull);

		if (isnull)
		{
			continue;
		}

		/* get the field name. */
		const char *fieldName = NameStr(tupleDescriptor->attrs[i].attname);
		if (fieldName == NULL)
		{
			fieldName = "";
		}

		pgbson_element_writer elementWriter;
		PgbsonInitObjectElementWriter(&writer, &elementWriter, fieldName, strlen(
										  fieldName));

		/* convert the SQL Value and write it into the bson Writer. */
		PgbsonElementWriterWriteSQLValue(&elementWriter, isnull, fieldValue,
										 tupleDescriptor->attrs[i].atttypid);
	}

	ReleaseTupleDesc(tupleDescriptor);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * bson_object_keys returns the set of keys for the object argument.
 *
 * The implementation is heavily based on jsonb_object_keys.
 *
 * This SRF operates in value-per-call mode. It processes the
 * object during the first call, and the keys are simply stashed
 * in an array, whose size is expanded as necessary. This is probably
 * safe enough for a list of keys of a single object, since they are
 * limited in size to NAMEDATALEN and the number of keys is unlikely to
 * be so huge that it has major memory implications.
 */
Datum
bson_object_keys(PG_FUNCTION_ARGS)
{
	FuncCallContext *functionContext;
	BsonObjectKeysState *state;

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext oldcontext;
		pgbson *sourceBson = PG_GETARG_PGBSON(0);
		bson_iter_t sourceBsonIterator;

		PgbsonInitIterator(sourceBson, &sourceBsonIterator);

		functionContext = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(functionContext->multi_call_memory_ctx);

		state = palloc(sizeof(BsonObjectKeysState));

		state->resultSize = PgbsonCountKeys(sourceBson);
		state->resultCount = 0;
		state->sentCount = 0;
		state->result = palloc(state->resultSize * sizeof(Datum));

		while (bson_iter_next(&sourceBsonIterator))
		{
			const char *key = bson_iter_key(&sourceBsonIterator);

			state->result[state->resultCount++] = CStringGetTextDatum(key);
		}

		MemoryContextSwitchTo(oldcontext);
		functionContext->user_fctx = (void *) state;
	}

	functionContext = SRF_PERCALL_SETUP();
	state = (BsonObjectKeysState *) functionContext->user_fctx;

	if (state->sentCount < state->resultCount)
	{
		Datum next = state->result[state->sentCount++];

		SRF_RETURN_NEXT(functionContext, next);
	}

	SRF_RETURN_DONE(functionContext);
}


/*
 * bson_repath_and_build take an array of paths and an array of bsons and constructs a bson where
 * the root paths of each bson become the corresponding text in the array of paths and are merged into one object
 * ["l1", "l2"] [{"":x}, {"":y}] becomes {"l1":x, "l2":y}
 */
Datum
bson_repath_and_build(PG_FUNCTION_ARGS)
{
	Datum *args;
	bool *nulls;
	Oid *types;

	/* fetch argument values to build the object */
	int nargs = extract_variadic_args(fcinfo, 0, false, &args, &types, &nulls);
	if (nargs < 0)
	{
		PG_RETURN_NULL();
	}
	if (nargs % 2 != 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("argument list must have an even number of elements"),

		         /* translator: %s is a SQL function name */
				 errhint(
					 "The arguments of %s must consist of alternating keys and values.",
					 "bson_repath_and_build()")));
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	for (int i = 0; i < nargs; i += 2)
	{
		if (nulls[i])
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument %d cannot be null", i + 1),
					 errhint("Object keys should be text.")));
		}

		if (types[i] != TEXTOID)
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("argument %d must be a text", i)));
		}

		text *pathText = DatumGetTextP(args[i]);
		char *path = (char *) VARDATA_ANY(pathText);
		int len = VARSIZE_ANY_EXHDR(pathText);
		StringView pathView = { .length = len, .string = path };

		if (StringViewContains(&pathView, '.'))
		{
			/* Paths here cannot be dotted paths */
			ereport(ERROR, (errcode(MongoLocation40235),
							errmsg("The field name %.*s cannot contain '.'", len, path)));
		}

		if (pathView.length == 0 || StringViewStartsWith(&pathView, '$'))
		{
			/* We don't support dollar prefixed-paths here */
			ereport(ERROR, (errcode(MongoDollarPrefixedFieldName),
							errmsg("The field name %.*s cannot be an operator name",
								   len, path)));
		}

		if (nulls[i + 1])
		{
			PgbsonWriterAppendNull(&writer, path, len);
		}
		else if (types[i + 1] == BsonTypeId() || types[i + 1] == HelioCoreBsonTypeId())
		{
			pgbsonelement elem;
			pgbson *pbson = DatumGetPgBson(args[i + 1]);
			if (TryGetSinglePgbsonElementFromPgbson(pbson, &elem))
			{
				PgbsonWriterAppendValue(&writer, path, len, &elem.bsonValue);
			}
			else
			{
				ereport(ERROR, (errcode(MongoBadValue), (errmsg(
															 "Expecting a single element value"))));
			}
		}
		else
		{
			pgbson_element_writer elementWriter;
			bool isNull = false;
			PgbsonInitObjectElementWriter(&writer, &elementWriter, path, len);
			PgbsonElementWriterWriteSQLValue(&elementWriter, isNull, args[i + 1],
											 types[i + 1]);
		}
	}

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * Writes a SQL field to the writer specified doing the appropriate conversions
 */
void
PgbsonElementWriterWriteSQLValue(pgbson_element_writer *writer,
								 bool isNull, Datum fieldValue, Oid fieldTypeId)
{
	if (type_is_array(fieldTypeId))
	{
		/* array type */
		ArrayType *val_array = DatumGetArrayTypeP(fieldValue);

		Datum *val_datums;
		bool *val_is_null_marker;
		int val_count;

		bool arrayByVal = false;
		int elementLength = -1;
		Oid arrayElementType = ARR_ELEMTYPE(val_array);
		deconstruct_array(val_array,
						  arrayElementType, elementLength, arrayByVal,
						  TYPALIGN_INT, &val_datums, &val_is_null_marker,
						  &val_count);

		pgbson_array_writer arrayWriter;
		PgbsonElementWriterStartArray(writer, &arrayWriter);

		pgbson_element_writer arrayElemWriter;
		PgbsonInitArrayElementWriter(&arrayWriter, &arrayElemWriter);
		for (int i = 0; i < val_count; i++)
		{
			PgbsonElementWriterWriteSQLValue(&arrayElemWriter, val_is_null_marker[i],
											 val_datums[i],
											 arrayElementType);
		}

		PgbsonElementWriterEndArray(writer, &arrayWriter);
		return;
	}

	Oid bsonTypeOid = BsonTypeId();
	if (fieldTypeId == bsonTypeOid)
	{
		/* extract the bson. */
		pgbson *nestedBson = (pgbson *) DatumGetPointer(fieldValue);

		/* if it's a single value bson ({ "": <value> }) */
		/* then write the *value* as a child element. */
		pgbsonelement element;
		bool isSingleElement = TryGetSinglePgbsonElementFromPgbson(nestedBson, &element);
		if (isSingleElement && element.pathLength == 0)
		{
			PgbsonElementWriterWriteValue(writer, &element.bsonValue);
		}
		else
		{
			/* if it's not a single value element, treat it as a nested object. */
			pgbson_writer childWriter;
			bson_iter_t childIter;
			PgbsonElementWriterStartDocument(writer, &childWriter);
			PgbsonInitIterator(nestedBson, &childIter);
			while (bson_iter_next(&childIter))
			{
				PgbsonWriterAppendValue(&childWriter, bson_iter_key(&childIter),
										bson_iter_key_len(&childIter), bson_iter_value(
											&childIter));
			}

			PgbsonElementWriterEndDocument(writer, &childWriter);
		}

		return;
	}

	bson_value_t fieldBsonValue;
	switch (fieldTypeId)
	{
		case INT8OID:
		{
			int64_t int64Value = DatumGetInt64(fieldValue);

			if (int64Value <= INT32_MAX && int64Value >= INT32_MIN)
			{
				fieldBsonValue.value_type = BSON_TYPE_INT32;
				fieldBsonValue.value.v_int32 = (int32_t) int64Value;
			}
			else
			{
				fieldBsonValue.value_type = BSON_TYPE_INT64;
				fieldBsonValue.value.v_int64 = int64Value;
			}

			PgbsonElementWriterWriteValue(writer, &fieldBsonValue);
			return;
		}

		case INT4OID:
		{
			fieldBsonValue.value_type = BSON_TYPE_INT32;
			fieldBsonValue.value.v_int32 = DatumGetInt32(fieldValue);
			PgbsonElementWriterWriteValue(writer, &fieldBsonValue);
			return;
		}

		case INT2OID:
		{
			fieldBsonValue.value_type = BSON_TYPE_INT32;
			fieldBsonValue.value.v_int32 = (int32_t) DatumGetInt16(fieldValue);
			PgbsonElementWriterWriteValue(writer, &fieldBsonValue);
			return;
		}

		case FLOAT4OID:
		{
			fieldBsonValue.value_type = BSON_TYPE_DOUBLE;
			fieldBsonValue.value.v_double = (double) DatumGetFloat4(fieldValue);
			PgbsonElementWriterWriteValue(writer, &fieldBsonValue);
			return;
		}

		case FLOAT8OID:
		{
			fieldBsonValue.value_type = BSON_TYPE_DOUBLE;
			fieldBsonValue.value.v_double = (double) DatumGetFloat8(fieldValue);
			PgbsonElementWriterWriteValue(writer, &fieldBsonValue);
			return;
		}

		case BOOLOID:
		{
			fieldBsonValue.value_type = BSON_TYPE_BOOL;
			fieldBsonValue.value.v_bool = DatumGetBool(fieldValue);
			PgbsonElementWriterWriteValue(writer, &fieldBsonValue);
			return;
		}

		case TEXTOID:
		{
			text *textValue = DatumGetTextP(fieldValue);
			fieldBsonValue.value_type = BSON_TYPE_UTF8;
			fieldBsonValue.value.v_utf8.len = VARSIZE_ANY_EXHDR(textValue);
			fieldBsonValue.value.v_utf8.str = VARDATA_ANY(textValue);
			PgbsonElementWriterWriteValue(writer, &fieldBsonValue);
			return;
		}

		case UUIDOID:
		{
			pg_uuid_t *uuid = DatumGetUUIDP(fieldValue);
			fieldBsonValue.value_type = BSON_TYPE_BINARY;
			fieldBsonValue.value.v_binary.subtype = BSON_SUBTYPE_UUID;
			fieldBsonValue.value.v_binary.data = &uuid->data[0];
			fieldBsonValue.value.v_binary.data_len = 16;
			PgbsonElementWriterWriteValue(writer, &fieldBsonValue);
			return;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg(
								"Type oid not supported %d", fieldTypeId)));
		}
	}
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */
