/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/sort_utils.c
 *
 * Utilities for sort operator.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "utils/sort_utils.h"
#include "utils/mongo_errors.h"
#include "query/helio_bson_compare.h"

/**
 * Validate sort spec and set SortContext once verified
 *
 * Parameters:
 * bson_value_t sortBsonValue : bson value of sort spec
 * SortContext *sortContext : sortContext
 */
void
ValidateSortSpecAndSetSortContext(bson_value_t sortBsonValue, SortContext *sortContext)
{
	if (sortBsonValue.value_type == BSON_TYPE_EOD)
	{
		sortContext->sortType = SortType_No_Sort;
	}
	else if (!BsonValueIsNumber(&sortBsonValue))
	{
		if (sortBsonValue.value_type == BSON_TYPE_DOCUMENT)
		{
			bson_iter_t sortItr;
			BsonValueInitIterator(&sortBsonValue, &sortItr);

			/* Validate all the fields of $sort spec as well as create a valid list of sorting field key and sort order for comparator */
			List *sortSpecList = NIL;
			int sortFields = 0;
			while (bson_iter_next(&sortItr))
			{
				sortFields++;
				const char *key = bson_iter_key(&sortItr);

				int keyLength = strlen(key);
				if (keyLength == 0)
				{
					ereport(ERROR, (errcode(MongoBadValue), errmsg(
										"The $sort field cannot be empty")));
				}

				if (key[0] == '.' || key[keyLength - 1] == '.')
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg(
										"The $sort field is a dotted field but has an empty part: %s",
										key)));
				}
				const bson_value_t *sortVal = bson_iter_value(&sortItr);
				if (!BsonValueIsNumber(sortVal) || (BsonValueAsDouble(sortVal) != 1 &&
													BsonValueAsDouble(sortVal) != -1))
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg(
										"The $sort element value must be either 1 or -1")));
				}

				SortSpecData *sortSpec = palloc0(
					sizeof(SortSpecData));
				sortSpec->key = bson_iter_key(&sortItr);
				sortSpec->direction = BsonValueAsInt64(sortVal) == 1 ?
									  SortDirection_Ascending : SortDirection_Descending;
				sortSpecList = lappend(sortSpecList, sortSpec);
			}
			if (sortFields == 0)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"The $sort pattern is empty when it should be a set of fields.")));
			}
			sortContext->sortSpecList = sortSpecList;
			sortContext->sortType = SortType_ObjectFieldSort;
		}
		else
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"The $sort is invalid: use 1/-1 to sort the whole element, or {field:1/-1} to sort embedded fields")));
		}
	}
	else
	{
		/* Sort value is a number, check if it is strictly -1 or 1, this represent whole element sort */
		double sortOrder = BsonValueAsDouble(&sortBsonValue);
		if (sortOrder == (double) 1 || sortOrder == (double) -1)
		{
			sortContext->sortType = SortType_WholeElementSort;
			sortContext->sortDirection = (sortOrder == (double) 1) ?
										 SortDirection_Ascending :
										 SortDirection_Descending;
		}
		else
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"The $sort element value must be either 1 or -1")));
		}
	}
}


/*
 * Helper method for $sort or $sortArray to create an element with its associated
 * index in the array.
 */
ElementWithIndex *
GetElementWithIndex(const bson_value_t *val, uint32_t index)
{
	ElementWithIndex *elem = palloc(sizeof(ElementWithIndex));
	elem->bsonValue = *val;
	elem->index = index;
	return elem;
}


/*
 * Helper method for $sort or $sortArray to update an element with its associated
 * index in the array.
 */
void
UpdateElementWithIndex(const bson_value_t *val, uint32_t index, ElementWithIndex *element)
{
	ElementWithIndex *elem = GetElementWithIndex(val,
												 index);
	*element = *elem;

	pfree(elem);
}


/**
 * Compares the BSON values for $push's $sort stage or sortBy specification in $sortArray operator
 * e.g: if update spec is => {$push: {a : {$sort: {"b" : 1, "c" : -1}}}}
 *
 * This function will compare first "b" field and then the "c" field to identify the sort order.
 *
 * $sortArray operator => $sortArray: { input: [ 1, 4, 1, 6, 12, 5 ], sortBy: 1}
 *
 * Note: If the value for all sort spec are same, then element index is used to mainain original order
 * */
int
CompareBsonValuesForSort(const void *a, const void *b, void *args)
{
	/* Sort the values according to the sort context which has sort pattern */
	int result = 0;
	const ElementWithIndex *left = (ElementWithIndex *) a;
	const ElementWithIndex *right = (ElementWithIndex *) b;
	SortContext *sortContext = (SortContext *) args;
	bool isCompareValid = false;
	SortDirection direction = SortDirection_Ascending;

	if (sortContext->sortType == SortType_WholeElementSort)
	{
		direction = sortContext->sortDirection;
		result = CompareBsonValueAndType(&left->bsonValue, &right->bsonValue,
										 &isCompareValid);
	}
	else
	{
		bson_value_t leftValue, rightValue;
		ListCell *sortSpecCell = NULL;
		foreach(sortSpecCell, sortContext->sortSpecList)
		{
			leftValue.value_type = BSON_TYPE_NULL;
			rightValue.value_type = BSON_TYPE_NULL;
			SortSpecData *sortSpec =
				(SortSpecData *) lfirst(
					sortSpecCell);

			direction = sortSpec->direction;
			if (left->bsonValue.value_type == BSON_TYPE_DOCUMENT)
			{
				bson_iter_t leftDoc_iter, key_iter;
				BsonValueInitIterator(&left->bsonValue, &leftDoc_iter);
				if (bson_iter_find_descendant(&leftDoc_iter, sortSpec->key,
											  &key_iter))
				{
					leftValue = *bson_iter_value(&key_iter);
				}
			}
			if (right->bsonValue.value_type == BSON_TYPE_DOCUMENT)
			{
				bson_iter_t rightDoc_iter, key_iter;
				BsonValueInitIterator(&right->bsonValue, &rightDoc_iter);
				if (bson_iter_find_descendant(&rightDoc_iter, sortSpec->key,
											  &key_iter))
				{
					rightValue = *bson_iter_value(&key_iter);
				}
			}

			/**
			 * Compare the leftValue and rightValue.
			 * If dotted path is non-existing then these are treated as BSON_TYPE_NULL for comparision
			 */
			result = CompareBsonValueAndType(&leftValue, &rightValue,
											 &isCompareValid);
			if (result != 0)
			{
				break;
			}
		}
	}
	if (result == 0)
	{
		/* Maintain the order of original documents in case both sort fields are equal*/
		return left->index - right->index;
	}
	return direction == SortDirection_Ascending ? result : -result;
}
