/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_deduplicate.c
 *
 * Implementation of field deduplication operation for bson documents.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <nodes/pg_list.h>
#include <utils/hsearch.h>

#include "io/helio_bson_core.h"
#include "utils/hashset_utils.h"


/*
 * Other helper functions.
 */
static pgbson * PgbsonDeduplicateFieldsHandleDocumentIter(bson_iter_t *documentIter);
static bson_value_t PgbsonDeduplicateFieldsRecurseArrayElements(bson_iter_t *arrayIter);


PG_FUNCTION_INFO_V1(bson_deduplicate_fields);


/*
 * bson_deduplicate_fields is the SQL interface for PgbsonDeduplicateFields.
 */
Datum
bson_deduplicate_fields(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("p_document cannot be NULL")));
	}

	PG_RETURN_POINTER(PgbsonDeduplicateFields(PG_GETARG_PGBSON(0)));
}


/*
 * PgbsonDeduplicateFields takes a document and returns a new one by
 * deduplicating its (and recursively child documents') fields.
 *
 * Note that new document preserves order of the fields in the original
 * document.
 *
 * For example, given;
 *   {"a": 1, "b": [{"c": 1}, {"c": {"e": 1, "e": 2}}], "a": 2}
 * returns;
 *   {"a": 2, "b" : [{"c": 1}, {"c": {"e": 2}}]}
 */
pgbson *
PgbsonDeduplicateFields(const pgbson *document)
{
	bson_iter_t iter;
	PgbsonInitIterator(document, &iter);
	return PgbsonDeduplicateFieldsHandleDocumentIter(&iter);
}


/*
 * PgbsonDeduplicateFieldsHandleDocumentIter is the helper function for
 * PgbsonDeduplicateFields that instead takes an iterator and so can be used
 * during recursive calls too.
 */
static pgbson *
PgbsonDeduplicateFieldsHandleDocumentIter(bson_iter_t *documentIter)
{
	check_stack_depth();

	HTAB *bsonElementHashSet = CreatePgbsonElementHashSet();

	List *hashEntryList = NIL;

	while (bson_iter_next(documentIter))
	{
		CHECK_FOR_INTERRUPTS();

		pgbsonelement element;
		BsonIterToPgbsonElement(documentIter, &element);

		PgbsonElementHashEntry searchEntry = {
			.element = element
		};

		bool found = false;
		PgbsonElementHashEntry *hashEntry = hash_search(bsonElementHashSet, &searchEntry,
														HASH_ENTER, &found);

		if (!found)
		{
			hashEntryList = lappend(hashEntryList, hashEntry);
		}

		hashEntry->element = element;
	}

	/*
	 * Now iterate all the elements in the order that they appear in the original
	 * document and append them to the new document.
	 */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	ListCell *hashEntryCell = NULL;
	foreach(hashEntryCell, hashEntryList)
	{
		CHECK_FOR_INTERRUPTS();

		PgbsonElementHashEntry *hashEntry =
			(PgbsonElementHashEntry *) lfirst(hashEntryCell);
		pgbsonelement element = hashEntry->element;

		/*
		 * Deduplicate child documents / recurse down to the arrays before
		 * appending them to the new document.
		 */
		bson_value_t value;
		if (element.bsonValue.value_type == BSON_TYPE_DOCUMENT)
		{
			bson_iter_t innerDocumentIter;
			BsonValueInitIterator(&element.bsonValue, &innerDocumentIter);
			pgbson *dedupInnerDocument =
				PgbsonDeduplicateFieldsHandleDocumentIter(&innerDocumentIter);
			value = ConvertPgbsonToBsonValue(dedupInnerDocument);
		}
		else if (element.bsonValue.value_type == BSON_TYPE_ARRAY)
		{
			bson_iter_t arrayIter;
			BsonValueInitIterator(&element.bsonValue, &arrayIter);
			value = PgbsonDeduplicateFieldsRecurseArrayElements(&arrayIter);
		}
		else
		{
			value = element.bsonValue;
		}

		PgbsonWriterAppendValue(&writer, element.path, element.pathLength, &value);
	}

	hash_destroy(bsonElementHashSet);

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * PgbsonDeduplicateFieldsRecurseArrayElements is the helper function for
 * PgbsonDeduplicateFieldsHandleDocumentIter that deduplicates each document
 * (if any) stored in given array.
 */
static bson_value_t
PgbsonDeduplicateFieldsRecurseArrayElements(bson_iter_t *arrayIter)
{
	check_stack_depth();

	pgbson_writer singleElementDocWriter;
	PgbsonWriterInit(&singleElementDocWriter);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&singleElementDocWriter, "", 0, &arrayWriter);

	while (bson_iter_next(arrayIter))
	{
		CHECK_FOR_INTERRUPTS();

		if (BSON_ITER_HOLDS_DOCUMENT(arrayIter))
		{
			bson_iter_t documentIter;
			bson_iter_recurse(arrayIter, &documentIter);
			pgbson *dedupDocument =
				PgbsonDeduplicateFieldsHandleDocumentIter(&documentIter);
			PgbsonArrayWriterWriteDocument(&arrayWriter, dedupDocument);
		}
		else if (BSON_ITER_HOLDS_ARRAY(arrayIter))
		{
			bson_iter_t innerArrayIter;
			bson_iter_recurse(arrayIter, &innerArrayIter);
			bson_value_t innerArray =
				PgbsonDeduplicateFieldsRecurseArrayElements(&innerArrayIter);
			PgbsonArrayWriterWriteValue(&arrayWriter, &innerArray);
		}
		else
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, bson_iter_value(arrayIter));
		}
	}

	PgbsonWriterEndArray(&singleElementDocWriter, &arrayWriter);

	pgbsonelement element;
	PgbsonToSinglePgbsonElement(PgbsonWriterGetPgbson(&singleElementDocWriter), &element);
	return element.bsonValue;
}
