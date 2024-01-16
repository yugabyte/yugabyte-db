/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_sorted_accumulator.c
 *
 * Functions related to custom aggregates for group by
 * accumulator with sort specification:
 * first (implicit)
 * last (implicit)
 * top (explicit)
 * bottom (explicit)
 * firstN (implicit)
 * lastN (implicit)
 * topN (explicit)
 * bottomN (explicit)
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <funcapi.h>


#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "query/bson_dollar_operators.h"
#include "operators/bson_expression.h"
#include "aggregation/bson_sorted_accumulator.h"
#include "utils/array.h"

/*
 * Converts a BsonOrderAggState into a serialized form to allow the internal type to be bytea
 * Resulting bytes look like:
 * | Varlena Header | numAggValues | numSortKeys | BsonOrderAggValue * numAggValues | sortDirections |
 */
bytea *
SerializeOrderState(MemoryContext aggregateContext,
					BsonOrderAggState *state,
					bytea *byteArray)
{
	int valueSize = 0;
	for (int i = 0; i < state->currentCount; i++)
	{
		/* Flag for not null */
		valueSize += 1;

		/* Get total size of currentResult. Null values are marked with 0 byte */
		if (state->currentResult[i] != NULL)
		{
			/* flag for value not null */
			valueSize += 1;
			if (state->currentResult[i]->value != NULL)
			{
				valueSize += VARSIZE(state->currentResult[i]->value);
			}

			int sortKeyValuesSize = 0;

			/* Accumulate the total size of the sortKeys, nulls are marked with a 0 byte */
			for (int j = 0; j < state->numSortKeys; j++)
			{
				/* Flag for sort key not null */
				sortKeyValuesSize += 1;
				if (state->currentResult[i]->sortKeyValues[j] == 0)
				{
					continue;
				}
				pgbson *bson = DatumGetPgBson(state->currentResult[i]->sortKeyValues[j]);
				sortKeyValuesSize += VARSIZE(bson);
			}
			valueSize += sortKeyValuesSize;
		}
	}

	/*
	 * Make sure the byte array is big enough to fit the whole state if numAggValues is 1:
	 * varlena Header | numAggValues | numSortKeys  | BsonOrderAggValues * numAggValues | sortDirections (bool)
	 *
	 * We can't reuse the same block with muliple values as we would potentially
	 * overwrite the existing values if it was shifted to later in the list.
	 * Sometimes we need to increase the size with single values, e.g., when
	 * the new currentValue becomes bigger. In that case we repalloc.
	 */
	int requiredByteSize = VARHDRSZ + /*varlena Header */
						   sizeof(int64) + /* numAggValues */
						   sizeof(int64) + /* currentCount */
						   sizeof(int) + /* numSortKeys */
						   valueSize + /* BsonOrderAggValues */
						   state->numSortKeys * sizeof(bool); /* sortDirections */
	char *bytes;
	int existingByteSize = (byteArray == NULL) ? 0 : VARSIZE(byteArray);

	if (state->numAggValues == 1 && existingByteSize >= requiredByteSize)
	{
		/* Reuse existing bytes */
		bytes = (char *) byteArray;
	}
	else
	{
		/* Otherwise get more
		 * We don't need to do anything with the old memory (free/repalloc) since Postgres will free it as
		 * it will track that the memory block changed in the transition function.
		 * If we intent to free or repalloc the old memory, when Postgres tries to free it would crash as the old memory would be invalid.
		 */
		bytes = (char *) MemoryContextAlloc(aggregateContext, requiredByteSize);
		SET_VARSIZE(bytes, requiredByteSize);
	}


	/* Copy in the currentValue */
	char *byteAllocationPointer = (char *) VARDATA(bytes);

	/* Set the number of Aggregation Values */
	*((int64 *) (byteAllocationPointer)) = state->numAggValues;
	byteAllocationPointer += sizeof(int64);

	/* Set the number of Current Values */
	*((int64 *) (byteAllocationPointer)) = state->currentCount;
	byteAllocationPointer += sizeof(int64);

	/* Set the number of sortKeys after the numAggValues */
	*((int *) (byteAllocationPointer)) = state->numSortKeys;
	byteAllocationPointer += sizeof(int);

	/* Write each result */
	for (int i = 0; i < state->currentCount; i++)
	{
		/* Set Not Null for currentResult */
		*byteAllocationPointer = (char) (state->currentResult[i] != NULL);
		byteAllocationPointer += 1;
		if (state->currentResult[i] != NULL)
		{
			/* Set Not Null for currentResult */
			*byteAllocationPointer = (char) (state->currentResult[i]->value != NULL);
			byteAllocationPointer += 1;
			if (state->currentResult[i]->value != NULL)
			{
				int size = VARSIZE(state->currentResult[i]->value);
				memcpy(byteAllocationPointer, state->currentResult[i]->value, size);
				byteAllocationPointer += size;
			}

			/* Write each sortKey */
			for (int j = 0; j < state->numSortKeys; j++)
			{
				if (state->currentResult[i]->sortKeyValues[j] == 0)
				{
					*byteAllocationPointer = 0;
					byteAllocationPointer += 1;
					continue;
				}

				*byteAllocationPointer = 1;
				byteAllocationPointer += 1;
				pgbson *currentSortKey = DatumGetPgBson(
					state->currentResult[i]->sortKeyValues[j]);
				memcpy(byteAllocationPointer, currentSortKey, VARSIZE(currentSortKey));

				byteAllocationPointer += VARSIZE(currentSortKey);
			}
		}
	}

	/* Write sortDirection */
	for (int i = 0; i < state->numSortKeys; i++)
	{
		*((bool *) (byteAllocationPointer)) = state->sortDirections[i];
		byteAllocationPointer += sizeof(bool);
	}

	return (bytea *) bytes;
}


/*
 * Converts a BsonOrderAggState from a serialized form to allow the internal type to be bytea
 * Incoming bytes look like:
 * varlena Header | numAggValues | numSortKeys  | BsonOrderAggValues * numAggValues | sortDirections (bool)
 */
void
DeserializeOrderState(bytea *byteArray,
					  BsonOrderAggState *state)
{
	/* Use a char* instead for finer control over offsets */
	char *bytes = (char *) VARDATA(byteArray);

	/* Extract the number of numAggValues */
	state->numAggValues = *(int64 *) (bytes);
	bytes += sizeof(int64);

	/* Extract the number of curretn Values */
	state->currentCount = *(int64 *) (bytes);
	bytes += sizeof(int64);

	/* Check to see if we should allocate one extra position for the new data */
	if (state->currentCount >= state->numAggValues)
	{
		state->currentResult = palloc(sizeof(BsonOrderAggValue *) * state->currentCount);
	}
	else
	{
		state->currentResult = palloc(sizeof(BsonOrderAggValue *) * (state->currentCount +
																	 1));
	}

	/* Extract the number of sortKeys from after numAggValues */
	state->numSortKeys = *(int *) (bytes);
	bytes += sizeof(int);

	/* Extract each of the current results */
	for (int i = 0; i < state->currentCount; i++)
	{
		if (*bytes == 0)
		{
			/* NULL value */
			state->currentResult[i] = NULL;
			bytes++;
		}
		else
		{
			/* Skip 1 byte for currentResult not null. */
			bytes++;
			state->currentResult[i] = palloc0(sizeof(BsonOrderAggValue));
			if (*bytes == 0)
			{
				state->currentResult[i]->value = NULL;
				bytes++;
			}
			else
			{
				bytes++;
				state->currentResult[i]->value = (pgbson *) (bytes);
				bytes += VARSIZE(state->currentResult[i]->value);
			}

			/* Extract each sortKey */
			for (int j = 0; j < state->numSortKeys; j++)
			{
				if (*bytes == 0)
				{
					state->currentResult[i]->sortKeyValues[j] = (Datum) 0;
					bytes++;
				}
				else
				{
					bytes++;
					state->currentResult[i]->sortKeyValues[j] = PointerGetDatum(
						(pgbson *) bytes);
					bytes += VARSIZE(state->currentResult[i]->sortKeyValues[j]);
				}
			}
		}
	}

	for (int i = 0; i < state->numSortKeys; i++)
	{
		state->sortDirections[i] = *((bool *) bytes);
		bytes += sizeof(bool);
	}
}


/*
 * Applies the "state transition" (SFUNC) for sorted accumulators.
 * The args in PG_FUNCTION_ARGS:
 *      0) The current aggregation state
 *      1) The document/evaluated expression which will eventually be returned
 * For isSingle == true:
 *      2) An array of bsons representing the sort specs.
 * For isSingle == false:
 *      2) The number of documents to be returned or 'N'
 *      3) An array of bsons representing the sort specs.
 *
 * invertSort is 'false' for ascending and 'true' for descending.
 * isSingle is 'false' for N param accumulators (i.e. firstN/lastN/topN/bottomN)
 *      and 'true' for single accumulators (i.e. first/last/top/bottom).
 */
Datum
BsonOrderTransition(PG_FUNCTION_ARGS, bool invertSort, bool isSingle)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
							"aggregate function called in non-aggregate context")));
	}

	BsonOrderAggState inputAggregateState = { 0 };
	bytea *result;

	/* The 2nd argument is the input document*/
	pgbson *inputDocument = PG_GETARG_MAYBE_NULL_PGBSON(1);
	int64 numResults = 1;
	ArrayType *val_array = NULL;

	if (isSingle)
	{
		/* The 3rd argument is a list of sort specs */
		val_array = PG_GETARG_ARRAYTYPE_P(2);
	}
	else
	{
		/* The 3rd argument is number of results to return */
		numResults = PG_GETARG_INT64(2);
		Assert(numResults > 0);

		/* The 4th argument is a list of sort specs */
		val_array = PG_GETARG_ARRAYTYPE_P(3);
	}

	Datum *sortSpecs;
	bool *nulls;
	int numSortKeys;
	deconstruct_array(val_array,
					  ARR_ELEMTYPE(val_array), -1, false, TYPALIGN_INT,
					  &sortSpecs, &nulls, &numSortKeys);

	Assert(numSortKeys != 0);
	if (numSortKeys > 32)
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
							"Too many compound keys. A maximum of 32 keys is allowed.")));
	}


	bool validateSort = false;

	/* If this is the first call, allocate the state, otherwise deserialize it */
	if (!PG_ARGISNULL(0))
	{
		DeserializeOrderState(PG_GETARG_BYTEA_P(0),
							  &inputAggregateState);
	}
	else
	{
		inputAggregateState.numAggValues = numResults;
		inputAggregateState.currentCount = 0;

		/* Only need one value right now. */
		inputAggregateState.currentResult = palloc0(sizeof(BsonOrderAggValue *));
		inputAggregateState.numSortKeys = numSortKeys;

		for (int i = 0; i < numSortKeys; i++)
		{
			pgbson *sortKeySpec = DatumGetPgBson(sortSpecs[i]);
			pgbsonelement sortKeyElement;
			PgbsonToSinglePgbsonElement(sortKeySpec, &sortKeyElement);
			sortKeyElement.pathLength = 0;
			bool ascOrder = (BsonValueAsInt32(&sortKeyElement.bsonValue) == 1) ?
							true : false;
			inputAggregateState.sortDirections[i] = ascOrder;
		}
	}


	int currentParsePos = 0;
	int currentPos;
	BsonOrderAggValue *newValue = palloc0(sizeof(BsonOrderAggValue));

	/* Find which position newValue needs ot be inserted into */
	for (currentPos = inputAggregateState.currentCount - 1; currentPos >= 0; currentPos--)
	{
		bool replaceCurrentAggregate = false;

		/* Compare the newValue sortValues to currentPos */
		for (int i = 0; i < inputAggregateState.numSortKeys; i++)
		{
			/* Get the possible null bsons to compare */
			pgbson *currentAggregateBson =
				(inputAggregateState.currentResult[currentPos]->sortKeyValues[i] == 0) ?
				NULL :
				DatumGetPgBson(
					inputAggregateState.currentResult[currentPos]->sortKeyValues[i]);

			if (currentParsePos <= i)
			{
				if (inputDocument == NULL)
				{
					newValue->sortKeyValues[currentParsePos++] = 0;
				}
				else
				{
					newValue->sortKeyValues[currentParsePos++] = BsonOrderby(
						inputDocument,
						DatumGetPgBson(sortSpecs[i]),
						validateSort);
				}
			}

			pgbson *newBson = (newValue->sortKeyValues[i] == 0) ? NULL : DatumGetPgBson(
				newValue->sortKeyValues[i]);

			int comparisonResult = CompareNullablePgbson(currentAggregateBson, newBson);
			bool ascOrder = inputAggregateState.sortDirections[i];

			/* If the call is coming from LAST, we have to invert the comparison to take the correct one */
			if (invertSort)
			{
				comparisonResult *= -1;
			}

			if (comparisonResult > 0)
			{
				if (ascOrder)
				{
					replaceCurrentAggregate = true;
				}
				break;
			}
			else if (comparisonResult < 0)
			{
				if (!ascOrder)
				{
					replaceCurrentAggregate = true;
				}
				break;
			}
		}
		if (!replaceCurrentAggregate)
		{
			break;
		}
		else if (currentPos == inputAggregateState.numAggValues - 1)
		{
			/* Last item in the results can be removed */
			pfree(inputAggregateState.currentResult[currentPos]);
			inputAggregateState.currentResult[currentPos] = NULL;
		}
		else
		{
			/* Shift result current at currentPos to the next position */
			inputAggregateState.currentResult[currentPos + 1] =
				inputAggregateState.currentResult[currentPos];
			inputAggregateState.currentResult[currentPos] = NULL;
		}
	}

	/* Insert new item after the current position */
	currentPos++;

	/* Check to see if the currentPos for the new item is in the result set count.
	 */
	if (currentPos != inputAggregateState.numAggValues)
	{
		newValue->value = inputDocument;

		/* Finish updating any remaining SortKeys that need to be parsed */
		while (currentParsePos < inputAggregateState.numSortKeys)
		{
			if (inputDocument == NULL)
			{
				newValue->sortKeyValues[currentParsePos] = 0;
			}
			else
			{
				newValue->sortKeyValues[currentParsePos] =
					BsonOrderby(inputDocument,
								DatumGetPgBson(
									sortSpecs
									[currentParsePos]),
								validateSort);
			}
			currentParsePos++;
		}
		inputAggregateState.currentResult[currentPos] = newValue;
		if (inputAggregateState.numAggValues > inputAggregateState.currentCount)
		{
			inputAggregateState.currentCount += 1;
		}
		result = SerializeOrderState(aggregateContext,
									 &inputAggregateState,
									 PG_ARGISNULL(0) ?
									 NULL :
									 PG_GETARG_BYTEA_P(0));
	}
	else
	{
		/* Free up newly allocated object and return old pointer. */
		result = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);
	}

	return PointerGetDatum(result);
}


/*
 * Applies the "state transition" (SFUNC) for accumulator.
 * The args are:
 *      0) The current aggregation state
 *      1) The document/evaluated expression which will eventually be returned
 * For isSingle == 'false'
 *      2) The number of documents to be returned or 'N'
 */
Datum
BsonOrderTransitionOnSorted(PG_FUNCTION_ARGS, bool invertSort, bool isSingle)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
							"aggregate function called in non-aggregate context")));
	}

	bytea *byteArray = PG_ARGISNULL(0) ? NULL : PG_GETARG_BYTEA_P(0);
	pgbson *newValue = PG_GETARG_MAYBE_NULL_PGBSON(1);
	int64 returnCount = 1;
	char *sourcePtr;
	int64 currentCount = 0;
	int64 copySize = 0;

	/*
	 *  If no sort keys are provided, or the data is pre-sorted we do the following:
	 *      (1) Initialize the current value with the first N elements. And after that
	 *          (1.a.) keep ignoring any new element for invertSort = false
	 *          (1.b.) keep replacing the lease recent value with the new one for invertSort = true
	 *  Format is:
	 *  VARHDR | ByteSize | ReturnCount |CurrentCount | Result 1 | Size 1 | Result 2 | Size 2 | ...
	 */

	/* Get the current count and setup*/
	if (byteArray == NULL)
	{
		copySize = 0;
		sourcePtr = NULL;
		currentCount = 1;
		if (!isSingle)
		{
			/* The 3rd argument is number of results to return */
			returnCount = PG_GETARG_INT64(2);
		}
	}
	else
	{
		sourcePtr = (char *) VARDATA(byteArray);

		/* Copy size is total size - VARHDRSZ, the 3 ints we are extracting, and ptr. */
		copySize = *(int64 *) sourcePtr - sizeof(int64) * 3 - VARHDRSZ;
		sourcePtr += sizeof(int64);
		returnCount = *(int64 *) sourcePtr;
		sourcePtr += sizeof(int64);
		currentCount = *(int64 *) sourcePtr;
		sourcePtr += sizeof(int64);

		if (currentCount > returnCount)
		{
			/* Should never encounter this state unless somethin is very wrong */
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
								"Invalid state for aggregate function BsonOrderTransitionOnSorted")));
		}
		else if (currentCount == returnCount && !invertSort)
		{
			/* First N is already at N elements so just return */
			PG_RETURN_POINTER(byteArray);
		}
		else if (currentCount == returnCount && invertSort)
		{
			/* Need to drop the least recently seen element. */
			/* Get the size of the last element and subtract from copySize. */
			char *lastPtr = sourcePtr + copySize - sizeof(uint32);
			uint32 dataSize = *(uint32 *) lastPtr;
			copySize -= sizeof(uint32) + dataSize;
		}
		else
		{
			/* Just append the new element to the current results */
			currentCount++;
		}
	}

	/* Output is VARHDR | ByteSize | ReturnCount | CurrentCount | values... */
	uint32 totalSize = VARHDRSZ + sizeof(int64) * 3 + copySize;

	/* Add in newValue to total size */
	if (newValue != NULL)
	{
		totalSize += VARSIZE(newValue) + sizeof(uint32);
	}
	else
	{
		/* Write NULL as byte with value 0 */
		totalSize += 1 + sizeof(uint32);
	}

	bytea *returnData = byteArray;
	if (byteArray == NULL || totalSize > VARSIZE(byteArray))
	{
		/* Don't need to pfree byteArray as it get cleaned up by postgres */
		returnData = (bytea *) palloc0(totalSize);
		SET_VARSIZE(returnData, totalSize);
	}

	char *returnDataPtr = (char *) VARDATA(returnData);
	*((int64 *) (returnDataPtr)) = totalSize;
	returnDataPtr += sizeof(int64);
	*((int64 *) (returnDataPtr)) = returnCount;
	returnDataPtr += sizeof(int64);
	*((int64 *) (returnDataPtr)) = currentCount;
	returnDataPtr += sizeof(int64);


	/* Need to copy first so we don't overwrite with newValue if we are reusing the array. */
	/* Use memmove to account for the fact that we may be overwriting the data we are copying. */
	/* Values for previously existing return values are no longer reliable after this. */
	if (copySize != 0)
	{
		if (invertSort)
		{
			/* Copy over any bytes that need to be maintained offset by the newValue size so the last value is first */
			memmove(returnDataPtr + ((newValue == NULL) ? 1 : VARSIZE(newValue)) +
					sizeof(uint32), sourcePtr,
					copySize);
		}
		else
		{
			/* Copy over the first values we've already seen and adjust pointer to next position. */
			memmove(returnDataPtr, sourcePtr, copySize);
			returnDataPtr += copySize;
		}
	}

	/* Add in new value */
	if (newValue != NULL)
	{
		memcpy(returnDataPtr, newValue, VARSIZE(newValue));
		returnDataPtr += VARSIZE(newValue);
		*((uint32 *) (returnDataPtr)) = VARSIZE(newValue);
	}
	else
	{
		*returnDataPtr = 0;
		returnDataPtr += 1;
		*((uint32 *) (returnDataPtr)) = (uint32) 1;
	}

	PG_RETURN_POINTER(returnData);
}


/*
 * Applies the "state transition" (SFUNC) for accumulator.
 *
 * The args of PG_FUNCTION_ARGS are:
 *      0) left side partial aggregate
 *      1) right side partial aggregate
 *
 * Both left and right state are non null as the combine func was marked as strict.
 * 'strict' implies that if one the state is NULL, the combine func won't be called,
 * and the other state will be treated as the result.
 */
Datum
BsonOrderCombine(PG_FUNCTION_ARGS, bool invertSort)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
							"aggregate function called in non-aggregate context")));
	}

	/* Check if either result is NULL and return Non-NULL */
	if (PG_ARGISNULL(0))
	{
		return PG_GETARG_DATUM(1);
	}

	if (PG_ARGISNULL(1))
	{
		return PG_GETARG_DATUM(0);
	}

	BsonOrderAggState leftState = { 0 }, rightState = { 0 };
	DeserializeOrderState(PG_GETARG_BYTEA_P(0), &leftState);
	DeserializeOrderState(PG_GETARG_BYTEA_P(1), &rightState);

	int minPos = 0; /* Minimum position that new result can be inserted at */
	int leftPos, rightPos; /* current result position for left/right states*/
	bool updatedLeft = false; /* Flag to indicate that we updated the leftState */

	/* Allocate additional memory for merging right results. */
	if (leftState.currentCount != leftState.numAggValues)
	{
		int newSize = leftState.numAggValues;
		if (leftState.currentCount + rightState.currentCount < leftState.numAggValues)
		{
			newSize = leftState.currentCount + rightState.currentCount;
		}
		BsonOrderAggValue **newPtr = palloc0(sizeof(BsonOrderAggValue *) * newSize);

		/* Copy over pointers to values to new array */
		memcpy(newPtr, leftState.currentResult, sizeof(BsonOrderAggValue *) *
			   leftState.currentCount);

		/* Free current pointer and update */
		pfree(leftState.currentResult);
		leftState.currentResult = newPtr;
	}

	/* Loop through each object in rightState and insert it into leftState*/
	for (rightPos = 0; rightPos < rightState.currentCount; rightPos++)
	{
		if (rightState.currentResult[rightPos] == NULL)
		{
			/* No more results in right */
			break;
		}
		for (leftPos = leftState.currentCount - 1; leftPos >= minPos; leftPos--)
		{
			bool replaceCurrentAggregate = false;
			if (leftState.currentResult[leftPos] == NULL)
			{
				/* Current result isn't set so skip it. */
				continue;
			}

			for (int i = 0; i < leftState.numSortKeys; i++)
			{
				/* Get the possible null bsons to compare */
				pgbson *leftBson = (leftState.currentResult[leftPos]->sortKeyValues[i] ==
									0) ? NULL :
								   DatumGetPgBson(
					leftState.currentResult[leftPos]->sortKeyValues[i]);
				pgbson *rightBson =
					(rightState.currentResult[rightPos]->sortKeyValues[i] == 0) ? NULL :
					DatumGetPgBson(
						rightState.currentResult[rightPos]->sortKeyValues[i]);

				int comparisonResult = CompareNullablePgbson(leftBson, rightBson);
				bool ascOrder = leftState.sortDirections[i];

				/* Check if we have to invert the comparison to take the correct one */
				if (invertSort)
				{
					comparisonResult *= -1;
				}

				if (comparisonResult > 0)
				{
					if (ascOrder)
					{
						replaceCurrentAggregate = true;
					}
					break;
				}
				else if (comparisonResult < 0)
				{
					if (!ascOrder)
					{
						replaceCurrentAggregate = true;
					}
					break;
				}
			}

			if (!replaceCurrentAggregate)
			{
				break;
			}
			else if (leftPos == leftState.numAggValues - 1)
			{
				/* Last Item in leftState results can be removed as it will be replaced */
				pfree(leftState.currentResult[leftPos]);
				leftState.currentResult[leftPos] = NULL;
			}
			else
			{
				/* Shift result currently at leftPos to the next position */
				leftState.currentResult[leftPos + 1] =
					leftState.currentResult[leftPos];
				leftState.currentResult[leftPos] = NULL;
			}
		}

		/* Insert current result after this position */
		leftPos++;

		if (leftPos != leftState.numAggValues)
		{
			/* If currentCount isn't already numAggValues we are adding
			 * an additional value.  Otherwise we are replacing one.
			 */
			if (leftState.currentCount != leftState.numAggValues)
			{
				leftState.currentCount++;
			}

			/* Replace the object currently pointed at with the right one*/
			leftState.currentResult[leftPos] = rightState.currentResult[rightPos];

			/* Clear rightState pointer to prevent double free */
			rightState.currentResult[rightPos] = NULL;

			/* Set flag to indicate that we need to reserialize leftState */
			updatedLeft = true;
		}

		/* All remaining rightState elements will have to be inserted after current leftPos */
		minPos = leftPos + 1;
	}


	bytea *result = NULL;

	if (updatedLeft)
	{
		result = SerializeOrderState(aggregateContext, &leftState, PG_ARGISNULL(0) ?
									 NULL :
									 PG_GETARG_BYTEA_P(0));
	}
	else
	{
		result = PG_GETARG_BYTEA_P(0);
	}

	return PointerGetDatum(result);
}


/*
 * Applies the FINALFUNC for accumulator.
 *
 * The args of PG_FUNCTION_ARGS are:
 *      0) Current state
 * if IsSingle is true a single value will be returned
 * otherwise an array of values will be returned.
 */
Datum
BsonOrderFinal(PG_FUNCTION_ARGS, bool isSingle)
{
	MemoryContext aggregateContext;
	if (!AggCheckCallContext(fcinfo, &aggregateContext))
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
							"aggregate function called in non-aggregate context")));
	}
	bool returnNull = false;
	BsonOrderAggState state = { 0 };

	if (PG_ARGISNULL(0))
	{
		returnNull = true;
	}
	else
	{
		DeserializeOrderState(PG_GETARG_BYTEA_P(0), &state);
		if (isSingle)
		{
			/* Validate there is a value to return. */
			bson_iter_t pathSpecIter;
			PgbsonInitIterator(state.currentResult[0]->value, &pathSpecIter);
			if (!bson_iter_next(&pathSpecIter))
			{
				returnNull = true;
			}
		}
	}

	/* If there were no regular rows, or the last result was NULL, the result is $null */
	if (returnNull)
	{
		/* Mongo returns $null for empty sets */
		pgbsonelement finalValue;
		finalValue.path = "";
		finalValue.pathLength = 0;
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;

		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}

	pgbson *result = NULL;
	if (isSingle)
	{
		result = state.currentResult[0]->value;
	}
	else
	{
		/* Create array of results */
		pgbson_writer writer;
		pgbson_array_writer arrayWriter;
		PgbsonWriterInit(&writer);
		PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);
		for (int i = 0; i < state.currentCount; i++)
		{
			if (state.currentResult[i] == NULL)
			{
				/* No more Results*/
				break;
			}

			/* Check for Null value*/
			if (state.currentResult[i]->value != NULL)
			{
				PgbsonArrayWriterWriteDocument(&arrayWriter,
											   state.currentResult[i]->value);
			}
			else
			{
				PgbsonArrayWriterWriteNull(&arrayWriter);
			}
		}
		PgbsonWriterEndArray(&writer, &arrayWriter);
		result = PgbsonWriterGetPgbson(&writer);
	}

	PG_RETURN_POINTER(result);
}


/*
 * Applies the FINALFUNC for already sorted accumulator.
 *
 * The args of PG_FUNCTION_ARGS are:
 *      0) Current state
 * if IsSingle is true a single value will be returned
 * otherwise an array of values will be returned.
 */
Datum
BsonOrderFinalOnSorted(PG_FUNCTION_ARGS, bool isSingle)
{
	bool returnNull = false;
	bytea *byteArray = PG_GETARG_BYTEA_P(0);
	char *sourcePtr;
	int64 currentCount = 0;

	if (byteArray == NULL)
	{
		returnNull = true;
	}
	else
	{
		sourcePtr = (char *) VARDATA(byteArray);

		/* Skip over bytes and returnCount */
		sourcePtr += sizeof(int64);
		sourcePtr += sizeof(int64);
		currentCount = *(int64 *) sourcePtr;
		sourcePtr += sizeof(int64);

		if (isSingle)
		{
			/* validate the return value */
			if (currentCount == 0)
			{
				returnNull = true;
			}
			else
			{
				pgbson *currentValue = (pgbson *) sourcePtr;
				bson_iter_t pathSpecIter;
				PgbsonInitIterator(currentValue, &pathSpecIter);
				if (!bson_iter_next(&pathSpecIter))
				{
					returnNull = true;
				}
			}
		}
	}

	/* If there were no regular rows, or the last result was NULL, the result is $null */
	if (returnNull)
	{
		/* Mongo returns $null for empty sets */
		pgbsonelement finalValue;
		finalValue.path = "";
		finalValue.pathLength = 0;
		finalValue.bsonValue.value_type = BSON_TYPE_NULL;

		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}
	else if (isSingle)
	{
		PG_RETURN_POINTER((pgbson *) sourcePtr);
	}
	else
	{
		/* Create array of results */
		pgbson_writer writer;
		pgbson_array_writer arrayWriter;
		PgbsonWriterInit(&writer);
		PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);
		for (uint32 i = 0; i < currentCount; i++)
		{
			if (*sourcePtr == 0)
			{
				PgbsonArrayWriterWriteNull(&arrayWriter);

				/* Move to next value */
				sourcePtr += sizeof(uint32) + 1;
			}
			else
			{
				PgbsonArrayWriterWriteDocument(&arrayWriter, (pgbson *) sourcePtr);
				sourcePtr += VARSIZE(sourcePtr);
				sourcePtr += sizeof(uint32);
			}
		}
		PgbsonWriterEndArray(&writer, &arrayWriter);
		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
	}
}
