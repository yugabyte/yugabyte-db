/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/pgbson.h
 *
 * The BSON type serialization.
 *
 *-------------------------------------------------------------------------
 */

#ifndef PRIVATE_PGBSON_H
#error Do not import this header file. Import bson_core.h instead
#endif

#ifndef PG_BSON_WRITER_H
#define PG_BSON_WRITER_H

#include <server/datatype/timestamp.h>

/* bson writer interface */
typedef struct
{
	bson_t innerBson;
} pgbson_writer;


/*
 * bson writer on heap interface
 * Note - This needs to be destroyed after usage
 */
typedef struct
{
	bson_t *innerBsonRef;
} pgbson_heap_writer;

typedef struct
{
	bson_t innerBson;
	uint32_t index;
} pgbson_array_writer;

/*
 * interface that abstracts writing a single field
 * in an array or object. On instantiation, the object
 * is prepped with information so that it can be used to write
 * to either an object or an array
 */
typedef struct pgbson_element_writer
{
	union
	{
		pgbson_array_writer *arrayWriter;
		struct object_writer
		{
			pgbson_writer *objectWriter;
			const char *path;
			uint32_t pathLength;
		} objectWriterState;
	};
	bool isArray;
} pgbson_element_writer;


void PgbsonWriterInit(pgbson_writer *writer);
uint32_t PgbsonWriterGetSize(pgbson_writer *writer);
uint32_t PgbsonArrayWriterGetSize(pgbson_array_writer *writer);
void PgbsonWriterCopyToBuffer(pgbson_writer *writer, uint8_t *buffer, uint32_t length);

void PgbsonWriterAppendValue(pgbson_writer *writer, const char *path, uint32_t pathLength,
							 const bson_value_t *value);
void PgbsonWriterAppendInt64(pgbson_writer *writer, const char *path, uint32_t pathLength,
							 int64 value);
void PgbsonWriterAppendDouble(pgbson_writer *writer, const char *path, uint32_t
							  pathLength,
							  double value);
void PgbsonWriterAppendInt32(pgbson_writer *writer, const char *path, uint32_t pathLength,
							 int value);
void PgbsonWriterAppendUtf8(pgbson_writer *writer, const char *path, uint32_t pathLength,
							const char *value);
void PgbsonWriterAppendTimestampTz(pgbson_writer *writer, const char *path,
								   uint32_t pathLength, TimestampTz timestamp);
void PgbsonWriterAppendBool(pgbson_writer *writer, const char *path, uint32_t pathLength,
							bool value);
void PgbsonWriterAppendDocument(pgbson_writer *writer, const char *path,
								uint32_t pathLength, const pgbson *bson);
void PgbsonWriterAppendEmptyArray(pgbson_writer *writer, const char *path, uint32_t
								  pathLength);
void PgbsonWriterAppendBsonValueAsArray(pgbson_writer *writer, const char *path, uint32_t
										pathLength, const bson_value_t *value);
void PgbsonWriterAppendNull(pgbson_writer *writer, const char *path, uint32_t
							pathLength);
void PgbsonWriterAppendIter(pgbson_writer *writer, const bson_iter_t *iter);

void PgbsonWriterStartArray(pgbson_writer *writer, const char *path, uint32_t pathLength,
							pgbson_array_writer *childWriter);
void PgbsonWriterEndArray(pgbson_writer *writer, pgbson_array_writer *childWriter);
void PgbsonWriterStartDocument(pgbson_writer *writer, const char *path,
							   uint32_t pathLength, pgbson_writer *childWriter);
void PgbsonWriterEndDocument(pgbson_writer *writer, pgbson_writer *childWriter);

void PgbsonArrayWriterWriteUtf8(pgbson_array_writer *writer, const char *string);
void PgbsonArrayWriterWriteUtf8WithLength(pgbson_array_writer *writer, const char *string,
										  int length);

void PgbsonArrayWriterWriteValue(pgbson_array_writer *writer,
								 const bson_value_t *value);
void PgbsonArrayWriterWriteNull(pgbson_array_writer *writer);
void PgbsonArrayWriterWriteDocument(pgbson_array_writer *writer, const pgbson *bson);
void PgbsonArrayWriterStartDocument(pgbson_array_writer *writer,
									pgbson_writer *childWriter);
void PgbsonArrayWriterEndDocument(pgbson_array_writer *writer,
								  pgbson_writer *childWriter);
void PgbsonArrayWriterStartArray(pgbson_array_writer *writer,
								 pgbson_array_writer *childWriter);
void PgbsonArrayWriterEndArray(pgbson_array_writer *writer,
							   pgbson_array_writer *childWriter);
bson_value_t PgbsonArrayWriterGetValue(pgbson_array_writer *writer);
void PgbsonArrayWriterCopyDataToBsonValue(pgbson_array_writer *writer,
										  bson_value_t *bsonValue);

void PgbsonInitArrayElementWriter(pgbson_array_writer *arrayWriter,
								  pgbson_element_writer *elementWriter);
void PgbsonInitObjectElementWriter(pgbson_writer *objectWriter,
								   pgbson_element_writer *elementWriter,
								   const char *path, uint32_t pathLength);

void PgbsonElementWriterWriteValue(pgbson_element_writer *elementWriter,
								   const bson_value_t *value);
void PgbsonElementWriterWriteSQLValue(pgbson_element_writer *writer, bool isNull, Datum
									  fieldValue, Oid fieldTypeId);

bson_value_t PgbsonElementWriterGetValue(pgbson_element_writer *elementWriter);

void PgbsonElementWriterStartArray(pgbson_element_writer *elementWriter,
								   pgbson_array_writer *startArray);
void PgbsonElementWriterEndArray(pgbson_element_writer *elementWriter,
								 pgbson_array_writer *startArray);
void PgbsonElementWriterStartDocument(pgbson_element_writer *elementWriter,
									  pgbson_writer *startDocument);
void PgbsonElementWriterEndDocument(pgbson_element_writer *elementWriter,
									pgbson_writer *endDocument);

void PgbsonWriterCopyDocumentDataToBsonValue(pgbson_writer *writer,
											 bson_value_t *bsonValue);
pgbson * PgbsonWriterGetPgbson(pgbson_writer *writer);

void PgbsonWriterGetIterator(pgbson_writer *writer, bson_iter_t *iterator);

void PgbsonWriterFree(pgbson_writer *writer);

void PgbsonWriterConcat(pgbson_writer *writer, const pgbson *document);
void PgbsonWriterConcatWriter(pgbson_writer *writer, pgbson_writer *writerToConcat);
void PgbsonWriterConcatBytes(pgbson_writer *writer, const uint8_t *bsonBytes, uint32_t
							 bsonBytesLength);

uint32_t PgbsonArrayWriterGetIndex(pgbson_array_writer *arrayWriter);
bool IsPgbsonWriterEmptyDocument(pgbson_writer *writer);
pgbson_heap_writer * PgbsonHeapWriterInit(void);
void PgbsonHeapWriterFree(pgbson_heap_writer *writer);
uint32_t PgbsonHeapWriterGetSize(pgbson_heap_writer *writer);
bool IsPgbsonHeapWriterEmptyDocument(pgbson_heap_writer *writer);
void PgbsonWriterConcatHeapWriter(pgbson_writer *writer,
								  pgbson_heap_writer *writerToConcat);
void PgbsonHeapWriterAppendBsonValueAsArray(pgbson_heap_writer *writer,
											const char *path, uint32_t pathLength,
											const bson_value_t *value);
void PgbsonHeapWriterStartArray(pgbson_heap_writer *writer, const char *path,
								uint32_t pathLength, pgbson_array_writer *childWriter);
void PgbsonHeapWriterEndArray(pgbson_heap_writer *writer,
							  pgbson_array_writer *childWriter);
#endif
