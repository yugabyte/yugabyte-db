/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/list_utils.h
 *
 * Utilities for List objects.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <nodes/pg_list.h>
#include <utils/array.h>

#ifndef LIST_UTILS_H
#define LIST_UTILS_H

char * StringListJoin(const List *stringList, const char *delim);
void SortStringList(List *stringList);
bool StringListsAreEqual(const List *leftStringList, const List *rightStringList);
const char * StringListGetBsonArrayRepr(const List *stringList);
void AddStringListToBsonArrayRepr(pgbson_writer *bsonWriter, const List *stringList,
								  const char *arrayName);
ArrayType * IntListGetPgIntArray(const List *intList);
ArrayType * PointerListGetPgArray(const List *list, const Oid elemType);

/* Other public helpers */
void ArrayExtractDatums(ArrayType *array, Oid elemType,
						Datum **elems, bool **nulls, int *nelems);

#endif
