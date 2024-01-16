/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_set_operators.c
 *
 * Set Operator expression implementations of BSON.
 * See also: https://www.mongodb.com/docs/manual/reference/operator/aggregation/#set-expression-operators
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <math.h>
#include <common/hashfn.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "types/decimal128.h"
#include "utils/mongo_errors.h"
#include "query/helio_bson_compare.h"
#include "utils/hashset_utils.h"

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */

typedef struct DollarSetOperatorState
{
	/* Number of Total array for intersection */
	int arrayCount;

	/* This boolean variable indicates whether the current set matches the previous set. */
	bool isMatchWithPreviousSet;

	/* Hash Table to store frequency of elements */
	HTAB *arrayElementsHashTable;
} DollarSetOperatorState;

typedef struct BsonValueHashEntry
{
	/* key for hash Entry */
	bson_value_t bsonValue;

	/* value for hash Entry */
	int count;

	/* store the number of array where element has seen last */
	int lastSeenArray;
} BsonValueHashEntry;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static HTAB * CreateBsonValueElementHashSet(void);
static int BsonValueHashEntryCompareFunc(const void *obj1, const void *obj2,
										 Size objsize);
static uint32 BsonValueHashEntryHashFunc(const void *obj, size_t objsize);
static bool ProcessDollarSetIntersectionElement(bson_value_t *result,
												const bson_value_t *currentValue,
												bool isFieldPathExpression, void *state);
static void ProcessDollarSetIntersectionResult(bson_value_t *result, void *state);
static bool ProcessDollarSetUnionElement(bson_value_t *result,
										 const bson_value_t *currentValue,
										 bool isFieldPathExpression, void *state);
static void ProcessDollarSetUnionResult(bson_value_t *result, void *state);
static bool ProcessDollarSetEqualsElement(bson_value_t *result,
										  const bson_value_t *currentValue,
										  bool isFieldPathExpression, void *state);
static void ProcessDollarSetEqualsResult(bson_value_t *result, void *state);
static void ProcessDollarSetDifference(bson_value_t *result, void *state);
static void ProcessDollarSetIsSubset(bson_value_t *result, void *state);
static void ProcessSetElement(const bson_value_t *currentValue,
							  DollarSetOperatorState *state);
static bool ProcessDollarAllOrAnyElementsTrue(bson_value_t *result,
											  const bson_value_t *currentValue,
											  bool isFieldPathExpression, void *state);

/*
 * Evaluates the output of an $setIntersection expression.
 * Since $setIntersection is expressed as { "$setIntersection": [ [<expression1>], [<expression2>], ... ] }
 * We evaluate the inner expressions and then return the Intersection of them.
 */
void
HandleDollarSetIntersection(pgbson *doc, const bson_value_t *operatorValue,
							ExpressionResult *expressionResult)
{
	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueElementHashSet(),
	};

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarSetIntersectionElement,
		.processExpressionResultFunc = ProcessDollarSetIntersectionResult,
		.state = &state,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_ARRAY;
	HandleVariableArgumentExpression(doc, operatorValue, expressionResult, &startValue,
									 &context);
}


/*
 * Evaluates the output of an $setUnion expression.
 * Since $setUnion is expressed as { "$setUnion": [ [<expression1>], [<expression2>], ... ] }
 * We evaluate the inner expressions and then return the Union of them.
 */
void
HandleDollarSetUnion(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult)
{
	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueElementHashSet(),
	};

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarSetUnionElement,
		.processExpressionResultFunc = ProcessDollarSetUnionResult,
		.state = &state,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_ARRAY;
	HandleVariableArgumentExpression(doc, operatorValue, expressionResult, &startValue,
									 &context);
}


/*
 * Evaluates the output of an $setDifference expression.
 * Since $setDifference is expressed as { "$setDifference": [ [<expression1>], [<expression2>] ] }
 * We evaluate the inner expressions and then return the difference of them.
 */
void
HandleDollarSetDifference(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarSetDifference,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$setDifference", &context);
}


/*
 * Evaluates the output of an $setIsSubset expression.
 * Since $setIsSubset is expressed as { "$setIsSubset": [ [<expression1>], [<expression2>] ] }
 * We evaluate the inner expressions and then return true if set1 is subset of set2.
 */
void
HandleDollarSetIsSubset(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarSetIsSubset,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$setIsSubset", &context);
}


/*
 * Evaluates the output of an $setEquals expression.
 * Since $setEquals is expressed as { "$setEquals": [ [<expression1>], [<expression2>], ... ] }
 * We evaluate the inner expressions and then return true if sets are equal.
 */
void
HandleDollarSetEquals(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult)
{
	int numArgs = operatorValue->value_type == BSON_TYPE_ARRAY ?
				  BsonDocumentValueCountKeys(operatorValue) : 1;

	if (numArgs < 2)
	{
		ereport(ERROR, (errcode(MongoLocation17045), errmsg(
							"$setEquals needs at least two arguments had: %d",
							numArgs)));
	}

	DollarSetOperatorState state =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueElementHashSet(),
	};

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarSetEqualsElement,
		.processExpressionResultFunc = ProcessDollarSetEqualsResult,
		.state = &state,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_BOOL;
	startValue.value.v_bool = false;

	HandleVariableArgumentExpression(doc, operatorValue, expressionResult, &startValue,
									 &context);
}


/*
 * Evaluates the output of an $allElementsTrue expression.
 * Since $allElementsTrue is expressed as { "$allElementsTrue": [ [<expression1>,<expression2>] ] }
 * We evaluate the inner expressions and then return true if all the elements are true.
 */
void
HandleDollarAllElementsTrue(pgbson *doc, const bson_value_t *operatorValue,
							ExpressionResult *expressionResult)
{
	bool checkAllElementsTrueInArray = true;
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarAllOrAnyElementsTrue,
		.processExpressionResultFunc = NULL,
		.state = &checkAllElementsTrueInArray,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$allElementsTrue", &context);
}


/*
 * Evaluates the output of an $anyElementTrue expression.
 * Since $anyElementTrue is expressed as { "$anyElementTrue": [ [<expression1>,<expression2>] ] }
 * We evaluate the inner expressions and then return true if any element is true.
 */
void
HandleDollarAnyElementTrue(pgbson *doc, const bson_value_t *operatorValue,
						   ExpressionResult *expressionResult)
{
	bool checkAllElementsTrueInArray = false;
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarAllOrAnyElementsTrue,
		.processExpressionResultFunc = NULL,
		.state = &checkAllElementsTrueInArray,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$anyElementsTrue", &context);
}


/* Function that processes a single argument for $setIntersection. */
static bool
ProcessDollarSetIntersectionElement(bson_value_t *result, const
									bson_value_t *currentElement,
									bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false; /* stop processing more arguments. */
	}

	if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation17047), errmsg(
							"All operands of $setIntersection must be arrays. One argument is of type: %s",
							BsonTypeName(currentElement->value_type))));
	}

	ProcessSetElement(currentElement, (DollarSetOperatorState *) state);
	return true;
}


/* Function that validates the final state before returning the result for $setIntersection. */
static void
ProcessDollarSetIntersectionResult(bson_value_t *result, void *state)
{
	DollarSetOperatorState *intersectionState = (DollarSetOperatorState *) state;

	if (result->value_type == BSON_TYPE_NULL)
	{
		hash_destroy(intersectionState->arrayElementsHashTable);
		return;
	}

	HASH_SEQ_STATUS seq_status;
	BsonValueHashEntry *entry;

	hash_seq_init(&seq_status, intersectionState->arrayElementsHashTable);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	while ((entry = hash_seq_search(&seq_status)) != NULL)
	{
		if (entry->count == intersectionState->arrayCount)
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, &entry->bsonValue);
		}
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	hash_destroy(intersectionState->arrayElementsHashTable);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Function that processes a single argument for $setUnion. */
static bool
ProcessDollarSetUnionElement(bson_value_t *result, const
							 bson_value_t *currentElement,
							 bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false; /* stop processing more arguments. */
	}

	if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation17043), errmsg(
							"All operands of $setUnion must be arrays. One argument is of type: %s",
							BsonTypeName(currentElement->value_type))));
	}

	ProcessSetElement(currentElement, (DollarSetOperatorState *) state);
	return true;
}


/* Function that validates the final state before returning the result for $setUnion. */
static void
ProcessDollarSetUnionResult(bson_value_t *result, void *state)
{
	DollarSetOperatorState *unionState = (DollarSetOperatorState *) state;

	if (result->value_type == BSON_TYPE_NULL)
	{
		hash_destroy(unionState->arrayElementsHashTable);
		return;
	}

	HASH_SEQ_STATUS seq_status;
	BsonValueHashEntry *entry;

	hash_seq_init(&seq_status, unionState->arrayElementsHashTable);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	while ((entry = hash_seq_search(&seq_status)) != NULL)
	{
		PgbsonArrayWriterWriteValue(&arrayWriter, &entry->bsonValue);
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	hash_destroy(unionState->arrayElementsHashTable);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Function that processes a single argument for $setEquals. */
static bool
ProcessDollarSetEqualsElement(bson_value_t *result, const
							  bson_value_t *currentElement,
							  bool isFieldPathExpression, void *state)
{
	if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation17044), errmsg(
							"All operands of $setEquals must be arrays. One argument is of type: %s",
							currentElement->value_type == BSON_TYPE_EOD ?
							MISSING_TYPE_NAME :
							BsonTypeName(currentElement->value_type))));
	}

	DollarSetOperatorState *setEqualsState = (DollarSetOperatorState *) state;
	ProcessSetElement(currentElement, setEqualsState);

	/* Since the current set did not match the previous set, there is no need to proceed with further checks, and an early exit can be taken */
	if (!setEqualsState->isMatchWithPreviousSet)
	{
		return false;
	}

	return true;
}


/* Function that validates the final state before returning the result for $setEquals. */
static void
ProcessDollarSetEqualsResult(bson_value_t *result, void *state)
{
	DollarSetOperatorState *setEqualsState = (DollarSetOperatorState *) state;

	if (!setEqualsState->isMatchWithPreviousSet)
	{
		hash_destroy(setEqualsState->arrayElementsHashTable);
		return;
	}

	HASH_SEQ_STATUS seq_status;
	BsonValueHashEntry *entry;

	hash_seq_init(&seq_status, setEqualsState->arrayElementsHashTable);


	bool isEqual = true;
	while ((entry = hash_seq_search(&seq_status)) != NULL)
	{
		if (entry->count != setEqualsState->arrayCount)
		{
			isEqual = false;
			hash_seq_term(&seq_status);
			break;
		}
	}

	result->value.v_bool = isEqual;
	hash_destroy(setEqualsState->arrayElementsHashTable);
}


/* Function that validates the final state before returning the result for $setDifference. */
static void
ProcessDollarSetDifference(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *context = (DualArgumentExpressionState *) state;

	if (context->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (context->firstArgument.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation17048), errmsg(
							"both operands of $setDifference must be arrays. First argument is of type: %s",
							BsonTypeName(context->firstArgument.value_type))));
	}

	if (context->secondArgument.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation17049), errmsg(
							"both operands of $setDifference must be arrays. Second argument is of type: %s",
							BsonTypeName(context->secondArgument.value_type))));
	}

	DollarSetOperatorState setDifferenceState =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueElementHashSet(),
	};

	ProcessSetElement(&context->secondArgument, &setDifferenceState);

	bson_iter_t arrayIterator;
	BsonValueInitIterator(&context->firstArgument, &arrayIterator);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(&writer, "", 0, &arrayWriter);

	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayElement = bson_iter_value(&arrayIterator);
		BsonValueHashEntry elementToFind = { .bsonValue = *arrayElement };

		bool found = false;

		hash_search(setDifferenceState.arrayElementsHashTable,
					&elementToFind,
					HASH_ENTER,
					&found);

		if (!found)
		{
			PgbsonArrayWriterWriteValue(&arrayWriter, arrayElement);
		}
	}

	PgbsonWriterEndArray(&writer, &arrayWriter);
	hash_destroy(setDifferenceState.arrayElementsHashTable);
	*result = PgbsonArrayWriterGetValue(&arrayWriter);
}


/* Function that validates the final state before returning the result for $setIsSubset. */
static void
ProcessDollarSetIsSubset(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *context = (DualArgumentExpressionState *) state;

	if (context->firstArgument.value_type != BSON_TYPE_ARRAY)
	{
		int errorCode = MongoLocation17310;
		char *typeName = MISSING_TYPE_NAME;

		if (context->firstArgument.value_type != BSON_TYPE_EOD)
		{
			typeName = BsonTypeName(context->firstArgument.value_type);
			errorCode = MongoLocation17046;
		}

		ereport(ERROR, (errcode(errorCode), errmsg(
							"both operands of $setIsSubset must be arrays. First argument is of type: %s",
							typeName)));
	}

	if (context->secondArgument.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation17042), errmsg(
							"both operands of $setIsSubset must be arrays. Second argument is of type: %s",
							context->secondArgument.value_type == BSON_TYPE_EOD ?
							MISSING_TYPE_NAME :
							BsonTypeName(context->secondArgument.value_type))));
	}

	DollarSetOperatorState setIsSubsetState =
	{
		.arrayCount = 0,
		.isMatchWithPreviousSet = true,
		.arrayElementsHashTable = CreateBsonValueElementHashSet(),
	};

	ProcessSetElement(&context->secondArgument, &setIsSubsetState);

	bson_iter_t arrayIterator;
	BsonValueInitIterator(&context->firstArgument, &arrayIterator);

	bool isSubset = true;
	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayElement = bson_iter_value(&arrayIterator);
		BsonValueHashEntry elementToFind = { .bsonValue = *arrayElement };

		bool found = false;

		hash_search(setIsSubsetState.arrayElementsHashTable,
					&elementToFind,
					HASH_FIND,
					&found);

		if (!found)
		{
			isSubset = false;
			break;
		}
	}

	hash_destroy(setIsSubsetState.arrayElementsHashTable);
	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = isSubset;
}


/*
 * For the currentElement which is of type BSON_TYPE_ARRAY,
 * iterate through currentElement and add all unique elements hash table
 * increment the frequency of each added element in the HTable.
 */
static void
ProcessSetElement(const bson_value_t *currentElement,
				  DollarSetOperatorState *state)
{
	HTAB *arrayElementsHashTable = state->arrayElementsHashTable;
	bson_iter_t arrayIterator;
	BsonValueInitIterator(currentElement, &arrayIterator);
	state->arrayCount++;

	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayElement = bson_iter_value(&arrayIterator);
		BsonValueHashEntry elementToFind = { .bsonValue = *arrayElement };

		bool found = false;
		BsonValueHashEntry *foundElement =
			(BsonValueHashEntry *) hash_search(arrayElementsHashTable,
											   &elementToFind,
											   HASH_ENTER,
											   &found);

		/* This condition ensures that the element being checked is not a duplicate in current array. */
		if (foundElement->lastSeenArray != state->arrayCount)
		{
			foundElement->count++;
		}

		if (state->arrayCount > 1 && !found)
		{
			state->isMatchWithPreviousSet = false;
		}

		/* By assigning the current arrayCount to lastSeenArray, we can effectively disregard any subsequent occurrences of the same element in this array. */
		foundElement->lastSeenArray = state->arrayCount;
	}
}


/*
 * Function that processes a single argument for $allElementsTrue $anyElementTrue and find the result.
 */
static bool
ProcessDollarAllOrAnyElementsTrue(bson_value_t *result, const
								  bson_value_t *currentElement,
								  bool isFieldPathExpression, void *state)
{
	bool IsAllElementsTrueOp = *((bool *) state);

	if (currentElement->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(IsAllElementsTrueOp ? MongoLocation17040 :
								MongoLocation17041),
						errmsg("%s's argument must be an array, but is %s",
							   IsAllElementsTrueOp ? "$allElementsTrue" :
							   "$anyElementTrue",
							   currentElement->value_type == BSON_TYPE_EOD ?
							   MISSING_TYPE_NAME :
							   BsonTypeName(currentElement->value_type))));
	}

	bson_iter_t arrayIterator;
	BsonValueInitIterator(currentElement, &arrayIterator);
	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = IsAllElementsTrueOp;

	while (bson_iter_next(&arrayIterator))
	{
		bool currElement = BsonValueAsBool(bson_iter_value(&arrayIterator));

		/* if operator is $allElementsTrue and currElement is false then result will be false */
		if (IsAllElementsTrueOp && !currElement)
		{
			result->value.v_bool = false;
			break;
		}

		/* if operator is $anyElementTrue and currElement is true then result will be true */
		if (!IsAllElementsTrueOp && currElement)
		{
			result->value.v_bool = true;
			break;
		}
	}

	return true;
}


/*
 * BsonValueHashEntryHashFunc is the (HASHCTL.hash) callback
 * used to hash a BsonValueHashEntry object based on bsonValue
 * of the BsonValueHashEntry that it holds.
 */
static uint32
BsonValueHashEntryHashFunc(const void *obj, size_t objsize)
{
	const BsonValueHashEntry *hashEntry = obj;
	return BsonValueHashUint32(&hashEntry->bsonValue);
}


/*
 * BsonValueHashEntryCompareFunc is the (HASHCTL.match) callback (based
 * on BsonValueEquals()) used to determine if two bsonValue are same.
 *
 * Returns 0 if those two bsonValue are same, 1 otherwise.
 */
static int
BsonValueHashEntryCompareFunc(const void *obj1, const void *obj2, Size objsize)
{
	const BsonValueHashEntry *hashEntry1 = obj1;
	const BsonValueHashEntry *hashEntry2 = obj2;

	if (BsonValueEquals(&hashEntry1->bsonValue, &hashEntry2->bsonValue))
	{
		return 0;
	}
	return 1;
}


/*
 * Creates a hash table that stores bsonValue entries using
 * a hash and search based on the bsonValue.
 */
static HTAB *
CreateBsonValueElementHashSet(void)
{
	HASHCTL hashInfo = CreateExtensionHashCTL(
		sizeof(BsonValueHashEntry),
		sizeof(BsonValueHashEntry),
		BsonValueHashEntryCompareFunc,
		BsonValueHashEntryHashFunc);
	HTAB *bsonElementHashSet =
		hash_create("Bson Value Hash Table", 32, &hashInfo, DefaultExtensionHashFlags);

	return bsonElementHashSet;
}
