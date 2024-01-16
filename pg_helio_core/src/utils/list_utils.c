/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/list_utils.c
 *
 * Utilities for List objects.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <catalog/pg_type.h>
#include <lib/stringinfo.h>
#include <nodes/pg_list.h>
#include <utils/array.h>
#include "utils/lsyscache.h"
#include <utils/syscache.h>
#include <access/htup_details.h>

#include "io/helio_bson_core.h"
#include "utils/list_utils.h"


static int StringListComparator(const ListCell *leftListCell,
								const ListCell *rightListCell);


/*
 * StringListJoin takes a list of strings and returns a string by joining
 * them using given delimiter.
 */
char *
StringListJoin(const List *stringList, const char *delim)
{
	StringInfo str = makeStringInfo();

	bool firstWritten = false;

	ListCell *stringListCell = NULL;
	foreach(stringListCell, stringList)
	{
		appendStringInfo(str, "%s%s", firstWritten ? delim : "",
						 (char *) lfirst(stringListCell));

		firstWritten = true;
	}

	return str->data;
}


/*
 * SortStringList sorts given string list in-place in lexicographical order.
 */
void
SortStringList(List *stringList)
{
	list_sort(stringList, StringListComparator);
}


/*
 * StringListComparator compares two string list cells using strcmp().
 */
static int
StringListComparator(const ListCell *leftListCell, const ListCell *rightListCell)
{
	return strcmp(lfirst(leftListCell), lfirst(rightListCell));
}


/*
 * StringListsAreEqual returns true if given two string lists are same.
 */
bool
StringListsAreEqual(const List *leftStringList, const List *rightStringList)
{
	if (list_length(leftStringList) != list_length(rightStringList))
	{
		return false;
	}

	ListCell *leftStringCell = NULL;
	ListCell *rightStringCell = NULL;
	forboth(leftStringCell, leftStringList, rightStringCell, rightStringList)
	{
		if (strcmp(lfirst(leftStringCell), lfirst(rightStringCell)) != 0)
		{
			return false;
		}
	}

	return true;
}


/*
 * AddStringListToBsonArrayRepr adds the items from a stringlist as an
 * array to a BSON writer.
 */
void
AddStringListToBsonArrayRepr(pgbson_writer *bsonWriter, const List *stringList,
							 const char *arrayName)
{
	pgbson_array_writer arrayWriter;

	PgbsonWriterStartArray(bsonWriter, arrayName,
						   strlen(arrayName), &arrayWriter);
	ListCell *stringCell = NULL;
	foreach(stringCell, stringList)
	{
		PgbsonArrayWriterWriteUtf8(&arrayWriter, lfirst(stringCell));
	}

	PgbsonWriterEndArray(bsonWriter, &arrayWriter);
}


/*
 * StringListGetBsonArrayRepr returns bson array representation of given
 * list of strings.
 */
const char *
StringListGetBsonArrayRepr(const List *stringList)
{
	pgbson_writer dummyDocWriter;
	PgbsonWriterInit(&dummyDocWriter);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&dummyDocWriter, "", 0, &arrayWriter);

	ListCell *stringCell = NULL;
	foreach(stringCell, stringList)
	{
		PgbsonArrayWriterWriteUtf8(&arrayWriter, lfirst(stringCell));
	}

	PgbsonWriterEndArray(&dummyDocWriter, &arrayWriter);

	pgbsonelement element;
	PgbsonToSinglePgbsonElement(PgbsonWriterGetPgbson(&dummyDocWriter), &element);
	return BsonValueToJsonForLogging(&element.bsonValue);
}


/*
 * IntListGetPgIntArray creates a Postgres array based on given list of integers.
 */
ArrayType *
IntListGetPgIntArray(const List *intList)
{
	int nelems = list_length(intList);
	if (nelems == 0)
	{
		return construct_empty_array(INT4OID);
	}

	Datum *intDatumArray = palloc(sizeof(Datum) * nelems);
	for (int i = 0; i < nelems; i++)
	{
		intDatumArray[i] = Int32GetDatum(list_nth_int(intList, i));
	}

	bool elmbyval = true;
	return construct_array(intDatumArray, nelems, INT4OID, sizeof(int),
						   elmbyval, TYPALIGN_INT);
}


/*
 * PointerListGetPgArray creates a Postgres array based on given list of types.
 *
 * Type should be a pointer based type.
 */
ArrayType *
PointerListGetPgArray(const List *list, const Oid elemType)
{
	if (list == NIL)
	{
		return construct_empty_array(elemType);
	}
	Assert(IsA((list), List));
	int nelems = list_length(list);
	if (nelems == 0)
	{
		return construct_empty_array(elemType);
	}

	Datum *datumArray = palloc(sizeof(Datum) * nelems);
	for (int i = 0; i < nelems; i++)
	{
		datumArray[i] = PointerGetDatum(list_nth(list, i));
	}

	int16 typLen;
	bool typByVal;
	char typAlign;
	get_typlenbyvalalign(elemType, &typLen, &typByVal, &typAlign);
	return construct_array(datumArray, nelems, elemType, typLen,
						   typByVal, typAlign);
}


/*
 * ArrayExtractDatums extracts Datums of given type from given array.
 */
void
ArrayExtractDatums(ArrayType *array, Oid elemType,
				   Datum **elems, bool **nulls, int *nelems)
{
	HeapTuple typeTup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(elemType));
	if (!HeapTupleIsValid(typeTup))
	{
		ereport(ERROR, (errmsg("cache lookup failed for type %u", elemType)));
	}

	Form_pg_type typeStruct = (Form_pg_type) GETSTRUCT(typeTup);

	deconstruct_array(array, elemType, typeStruct->typlen,
					  typeStruct->typbyval, typeStruct->typalign,
					  elems, nulls, nelems);

	ReleaseSysCache(typeTup);
}
