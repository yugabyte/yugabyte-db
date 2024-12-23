/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/sort_utils.h
 *
 * Utilities to execute a sort.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include "io/helio_bson_core.h"

#ifndef SORT_UTILS_H
#define SORT_UTILS_H


/*
 * Sort direction
 * it is for $sort modifier in $push update operator
 * or sortBy specification in $sortArray operator
 */
typedef enum SortDirection
{
	SortDirection_Ascending,
	SortDirection_Descending,
} SortDirection;


/**
 * Defines the Sort type needed for Sort Context
 *     1) SortType_No_Sort: no sort required,
 *     2) SortType_ObjectFieldSort: sort is required on specific field paths e.g: { $sort: {'a.b': 1, 'b.c': -1}}
 *        or { $sortBy: {'a.b': 1, 'b.c': -1}}
 *     3) SortType_WholeElementSort: sort on element or type e.g: {$sort : -1} / {$sort : 1}
 *        or {$sortBy : -1} / {$sortBy : 1}
 * */
typedef enum SortType
{
	SortType_No_Sort,
	SortType_ObjectFieldSort,
	SortType_WholeElementSort,
} SortType;


/**
 * This is used by the push operator's sort stage or $sortArray operator,
 *      1) sortType
 *      2) SortDirection: Represents the direction in case of SortType_WholeElementSort
 *      3) sortSpecHead: Head referrence to sort spec list in case of SortType_ObjectFieldSort
 * */
typedef struct SortContext
{
	List *sortSpecList;
	SortDirection sortDirection;
	SortType sortType;
} SortContext;


/*
 * A structure holding the bson_value_t and the index for sort comparator
 *
 * "index" is used to perform well order sort similar to Mongo protocol
 * in case when sorting is needed on object specific fields
 */
typedef struct ElementWithIndex
{
	bson_value_t bsonValue;
	uint32_t index;
} ElementWithIndex;


/*
 * A structure to hold the data for $sort spec of $push operator,
 * or sortBy in $sortArray operator
 * It holds the key and the sort direction
 *
 */
typedef struct SortSpecData
{
	const char *key;
	SortDirection direction;
} SortSpecData;

int CompareBsonValuesForSort(const void *a, const void *b, void *args);
void ValidateSortSpecAndSetSortContext(bson_value_t sortBsonValue,
									   SortContext *sortContext);
ElementWithIndex * GetElementWithIndex(const bson_value_t *val, uint32_t index);
void UpdateElementWithIndex(const bson_value_t *val, uint32_t index,
							ElementWithIndex *element);

#endif
