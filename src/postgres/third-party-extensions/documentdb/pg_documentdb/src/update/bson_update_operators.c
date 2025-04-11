/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_update_operators.c
 *
 * Implementation of the update operation for update operators.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <math.h>
#include <utils/builtins.h>

#include "update/bson_update_operators.h"
#include "query/bson_compare.h"
#include "types/decimal128.h"
#include "utils/documentdb_errors.h"
#include "io/bson_traversal.h"
#include "utils/sort_utils.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

typedef enum
{
	BITWISE_OPERATOR_AND,
	BITWISE_OPERATOR_OR,
	BITWISE_OPERATOR_XOR,
	BITWISE_OPERATOR_UNKNOWN
} MongoBitwiseOperatorType;

typedef struct
{
	/* bitwise operator name could be "and", "or", "xor" */
	char *mongoOperatorName;
	MongoBitwiseOperatorType operatorType;
} MongoBitwiseOperator;


/*
 * Mongo BitWise operator types
 */
static MongoBitwiseOperator BitwiseOperators[] = {
	{ "and", BITWISE_OPERATOR_AND },
	{ "or", BITWISE_OPERATOR_OR },
	{ "xor", BITWISE_OPERATOR_XOR },
	{ NULL, BITWISE_OPERATOR_UNKNOWN }
};


/*
 * The struct represent the modifiers given for an specific $push
 * update operators & is processed in the priority
 * $each->$position->$sort->$slice
 *
 */
typedef struct DollarPushUpdateState
{
	/* $each array */
	bson_value_t dollarEachArray;

	/* length of elements in $each array */
	int64_t dollarEachElementCount;

	/* position value to start insert in exisiting array in original update spec, can be positive or negative */
	int64_t position;

	/* slice value in original update spec, can be positive or negative */
	int64_t slice;

	/* slice start index, if $slice is present this is computed otherwise will be set to 0 */
	int64_t sliceStart;

	/* slice end index, if $slice is present this is computed otherwise will be set to last index of array */
	int64_t sliceEnd;

	/* Sort context that defines whether whole element level sort or field level sort is needed */
	SortContext *sortContext;

	/* If no valid modifiers exist this is false */
	bool modifiersExist;
} DollarPushUpdateState;

static MongoBitwiseOperatorType GetMongoBitwiseOperator(const char *key);

static void ValidateBitwiseInputParams(const MongoBitwiseOperatorType operatorType,
									   const char *updatePath,
									   const bson_value_t *state,
									   const char *key,
									   const bson_value_t *modifier,
									   bson_iter_t *updateSpec,
									   const CurrentDocumentState *docState);
static bool RenameVisitTopLevelField(pgbsonelement *element, const StringView *filterPath,
									 void *state);
static void RenameSetTraverseErrorResult(void *state, TraverseBsonResult traverseResult);
static bool RenameProcessIntermediateArray(void *state, const bson_value_t *value);

static bson_value_t RenameSourceGetValue(const pgbson *sourceDocument, const
										 char *sourcePathString);
static void ValidateAddToSetWithDollarEach(const bson_value_t *updateValue,
										   bool *isEach,
										   bson_value_t *elementsToAdd);
static void AddToSetWriteFinalArray(UpdateOperatorWriter *writer,
									const bson_value_t *existingValue,
									const bson_value_t *elementsToAdd,
									const bool isEach);
static void ValidateUpdateSpecAndSetPushUpdateState(const bson_value_t *fieldUpdateValue,
													DollarPushUpdateState *pushState);
static void ApplyDollarPushModifiers(const bson_value_t *bsonArray,
									 DollarPushUpdateState *pushState,
									 ElementWithIndex *elementsArr, int64_t
									 elementsArrLen);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */


/*
 * HandleUpdateDollarSet takes an existing value from the document, and
 * an updateValue presented from the updateSpec and writes the updateValue
 * to the target element_writer. $set has no update specific context.
 */
void
HandleUpdateDollarSet(const bson_value_t *existingValue,
					  UpdateOperatorWriter *writer,
					  const bson_value_t *updateValue,
					  void *updateNodeContext,
					  const UpdateSetValueState *setValueState,
					  const CurrentDocumentState *state)
{
	if (!state->isUpsert && BsonValueEqualsStrict(updateValue, existingValue))
	{
		return;
	}

	/* set the new value. */
	UpdateWriterWriteModifiedValue(writer, updateValue);
}


/*
 * HandleUpdateDollarSetOnInsert writes the value specified by the
 * updateSpec into the target writer if the update results in an
 * upsert.
 * $setOnInsert has no update specific context.
 */
void
HandleUpdateDollarSetOnInsert(const bson_value_t *existingValue,
							  UpdateOperatorWriter *writer,
							  const bson_value_t *updateValue,
							  void *updateNodeContext,
							  const UpdateSetValueState *setValueState,
							  const CurrentDocumentState *state)
{
	if (state->isUpsert)
	{
		UpdateWriterWriteModifiedValue(writer, updateValue);
	}
}


/*
 * HandleUpdateDollarUnset takes an existing value from the document, and
 * skips writing it into the target writer. If the target is an array, then
 * writes null.
 */
void
HandleUpdateDollarUnset(const bson_value_t *existingValue,
						UpdateOperatorWriter *writer,
						const bson_value_t *updateValue,
						void *updateNodeContext,
						const UpdateSetValueState *setValueState,
						const CurrentDocumentState *state)
{
	if (setValueState->isArray)
	{
		bson_value_t nullValue = { 0 };
		nullValue.value_type = BSON_TYPE_NULL;
		UpdateWriterWriteModifiedValue(writer, &nullValue);
		return;
	}

	if (existingValue->value_type != BSON_TYPE_EOD)
	{
		UpdateWriterSkipValue(writer);
		return;
	}
}


/*
 * HandleUpdateDollarInc takes an existing value from the document, and
 * an updateValue presented from the updateSpec and writes the incremented
 * value to the target element_writer. $inc has no update specific context.
 */
void
HandleUpdateDollarInc(const bson_value_t *existingValue,
					  UpdateOperatorWriter *writer,
					  const bson_value_t *updateValue,
					  void *updateNodeContext,
					  const UpdateSetValueState *setValueState,
					  const CurrentDocumentState *state)
{
	if (!BsonTypeIsNumber(updateValue->value_type))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg("Increment should be numeric")));
	}

	/**
	 * TODO: FIXME - Verify whether strict number check is required here
	 * */
	if (!BsonValueIsNumberOrBool(updateValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg("Cannot increment with non-numeric argument")));
	}

	bool overflowedFromInt64 = false;
	bson_value_t valueToModify = *existingValue;
	if (existingValue->value_type == BSON_TYPE_EOD)
	{
		/* value is unset - set it to the value. */
		UpdateWriterWriteModifiedValue(writer, updateValue);
	}
	else if (!AddNumberToBsonValue(&valueToModify, updateValue, &overflowedFromInt64))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"Cannot apply $inc to a value of non-numeric type. { _id: %s } has the field '%.*s' of non-numeric type %s",
							BsonValueToJsonForLogging(&state->documentId),
							setValueState->fieldPath->length,
							setValueState->fieldPath->string,
							BsonTypeName(existingValue->value_type)),
						errdetail_log(
							"Cannot apply $inc to a value of non-numeric type %s",
							BsonTypeName(existingValue->value_type))));
	}
	else
	{
		UpdateWriterWriteModifiedValue(writer, &valueToModify);
	}

	if (overflowedFromInt64)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Failed to apply $inc operations to current value (%s) for document {_id: %s}",
							FormatBsonValueForShellLogging(existingValue),
							BsonValueToJsonForLogging(&state->documentId)
							)));
	}
}


/*
 * HandleUpdateDollarMin takes an existing value from the document, and
 * an updateValue presented from the updateSpec and writes the minimum
 * value to the target element_writer. $min has no update specific context.
 */
void
HandleUpdateDollarMin(const bson_value_t *existingValue,
					  UpdateOperatorWriter *writer,
					  const bson_value_t *updateValue,
					  void *updateNodeContext,
					  const UpdateSetValueState *setValueState,
					  const CurrentDocumentState *state)
{
	bool isComparisonValid = true;
	if (existingValue->value_type == BSON_TYPE_EOD)
	{
		/* value is unset - set it to the value. */
		UpdateWriterWriteModifiedValue(writer, updateValue);
	}
	else if (CompareBsonValueAndType(updateValue, existingValue,
									 &isComparisonValid) < 0 &&
			 isComparisonValid)
	{
		/* update value is less than current field value, update it. */
		UpdateWriterWriteModifiedValue(writer, updateValue);
	}
}


/*
 * HandleUpdateDollarMax takes an existing value from the document, and
 * an updateValue presented from the updateSpec and writes the maximum
 * value to the target element_writer. $max has no update specific context.
 */
void
HandleUpdateDollarMax(const bson_value_t *existingValue,
					  UpdateOperatorWriter *writer,
					  const bson_value_t *updateValue,
					  void *updateNodeContext,
					  const UpdateSetValueState *setValueState,
					  const CurrentDocumentState *state)
{
	bool isComparisonValid = true;
	if (existingValue->value_type == BSON_TYPE_EOD)
	{
		/* value is unset - set it to the value. */
		UpdateWriterWriteModifiedValue(writer, updateValue);
	}
	else if (CompareBsonValueAndType(updateValue, existingValue,
									 &isComparisonValid) > 0 &&
			 isComparisonValid)
	{
		/* update value is greater than current field value, update it. */
		UpdateWriterWriteModifiedValue(writer, updateValue);
	}
}


/*
 * HandleUpdateDollarBit takes an existing value from the document, and
 * an updateValue presented from the updateSpec and writes the bit operation
 * of the value as specified in the update spec to the target element_writer.
 * $bit has no update specific context.
 */
void
HandleUpdateDollarBit(const bson_value_t *existingValue,
					  UpdateOperatorWriter *writer,
					  const bson_value_t *updateValue,
					  void *updateNodeContext,
					  const UpdateSetValueState *setValueState,
					  const CurrentDocumentState *state)
{
	if (updateValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("$bit should be a document")));
	}

	/* Get the bitwise operatorType and the its modifier field value */
	bson_iter_t updateValueSpec;
	BsonValueInitIterator(updateValue, &updateValueSpec);
	if (IsBsonValueEmptyDocument(updateValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"You must pass in at least one bitwise operation. The format is: {$bit: {field: {and/or/xor: #}}")));
	}

	bson_value_t valueToModify = *existingValue;

	while (bson_iter_next(&updateValueSpec))
	{
		const char *key = bson_iter_key(&updateValueSpec);
		const bson_value_t *value = bson_iter_value(&updateValueSpec);

		/* validate the correct bitwise operator */
		MongoBitwiseOperatorType operatorType = GetMongoBitwiseOperator(key);

		/* bitwise operation is used only for integer fields. */
		ValidateBitwiseInputParams(operatorType, setValueState->fieldPath->string,
								   existingValue, key, value,
								   &updateValueSpec, state);

		switch (operatorType)
		{
			case BITWISE_OPERATOR_AND:
			{
				BitwiseAndToBsonValue(&valueToModify, value);
				break;
			}

			case BITWISE_OPERATOR_OR:
			{
				BitwiseOrToBsonValue(&valueToModify, value);
				break;
			}

			case BITWISE_OPERATOR_XOR:
			{
				BitwiseXorToBsonValue(&valueToModify, value);
				break;
			}

			case BITWISE_OPERATOR_UNKNOWN:
			default:
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"The $bit modifier only supports 'and', 'or', and 'xor', not '%s' which is an unknown operator",
									key)));
			}
		}
	}

	/* if is upsert or the original value changed, we have an update. */
	if (state->isUpsert || !BsonValueEqualsStrict(existingValue,
												  &valueToModify))
	{
		UpdateWriterWriteModifiedValue(writer, &valueToModify);
	}
}


/*
 * HandleUpdateDollarMul takes an existing value from the document, and
 * an updateValue presented from the updateSpec and writes the product of the
 * values to the target element_writer. $mul has no update specific context.
 */
void
HandleUpdateDollarMul(const bson_value_t *existingValue,
					  UpdateOperatorWriter *writer,
					  const bson_value_t *updateValue,
					  void *updateNodeContext,
					  const UpdateSetValueState *setValueState,
					  const CurrentDocumentState *state)
{
	/* get the multiplication factor */
	const bson_value_t *mulFactor = updateValue;
	if (!BsonTypeIsNumber(mulFactor->value_type))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"Cannot multiply with non-numeric argument: { %s : %s }",
							setValueState->relativePath, BsonValueToJsonForLogging(
								mulFactor)),
						errdetail_log(
							"Cannot multiply with non-numeric argument of type %s ",
							BsonTypeName(mulFactor->value_type))));
	}

	bson_value_t valueToModify = *existingValue;

	/* As per Mongo 5.0 behaviour of $mul update operator (int64 * int64) and (int64 * int32) overflow will result into multiplication failure and returns error */
	bool convertInt64OverflowToDouble = false;

	if (valueToModify.value_type == BSON_TYPE_EOD)
	{
		switch (mulFactor->value_type)
		{
			case BSON_TYPE_INT32:
			{
				valueToModify.value.v_int32 = (int32_t) 0;
				break;
			}

			case BSON_TYPE_INT64:
			{
				valueToModify.value.v_int64 = (int64_t) 0;
				break;
			}

			case BSON_TYPE_DOUBLE:
			{
				valueToModify.value.v_double = (double) 0;
				break;
			}

			case BSON_TYPE_DECIMAL128:
			{
				/* Exponents in dec128 are offsetted with BID128_EXP_BIAS, this sets the exponent as 0 */
				SetDecimal128Zero(&valueToModify);
				break;
			}

			default:
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg("Unexpected data type")));
			}
		}
		valueToModify.value_type = mulFactor->value_type;
		UpdateWriterWriteModifiedValue(writer, &valueToModify);
		return;
	}
	else if (!BsonValueIsNumber(existingValue))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"Cannot apply $mul to a value of non-numeric type. { _id: %s } has the field '%.*s' of non-numeric type %s",
							BsonValueToJsonForLogging(&state->documentId),
							setValueState->fieldPath->length,
							setValueState->fieldPath->string,
							BsonTypeName(existingValue->value_type)),
						errdetail_log(
							"Cannot apply $mul to a value of non-numeric type %s",
							BsonTypeName(existingValue->value_type))));
	}
	else if (!MultiplyWithFactorAndUpdate(&valueToModify, mulFactor,
										  convertInt64OverflowToDouble))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Failed to apply $mul operations to current (%s) value for document { _id: %s }",
							FormatBsonValueForShellLogging(existingValue),
							BsonValueToJsonForLogging(&state->documentId))));
	}

	/* if is upsert or the original value changed, we have an update. */
	if (state->isUpsert || !BsonValueEqualsStrict(&valueToModify,
												  existingValue))
	{
		UpdateWriterWriteModifiedValue(writer, &valueToModify);
	}
}


/*
 * HandleUpdateDollarPull takes an existing array value from the document,
 * and removes all the matching instances from the array which satisfies the expression
 *
 * Expression is compiled and is passed as updateNodeContext
 */
void
HandleUpdateDollarPull(const bson_value_t *existingValue,
					   UpdateOperatorWriter *writer,
					   const bson_value_t *updateValue,
					   void *updateNodeContext,
					   const UpdateSetValueState *setValueState,
					   const CurrentDocumentState *state)
{
	if (updateNodeContext == NULL)
	{
		ereport(ERROR, errmsg("$pull expressions context should not be NULL"));
	}

	BsonUpdateDollarPullState *pullUpdateState = updateNodeContext;

	if (existingValue->value_type == BSON_TYPE_EOD)
	{
		/* This is treated as no op */
		return;
	}

	if (existingValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Cannot apply $pull to a non-array value")));
	}

	/* Run the match and get all matching indices */

	/* We should recurse into the array if the $pull spec was an expression and not a plain value */
	bool shouldRecurseIfValueIsArray = !pullUpdateState->isValue;

	List *matchingIndices = EvalExpressionAgainstArrayGetAllMatchingIndices(
		pullUpdateState->evalState,
		existingValue,
		shouldRecurseIfValueIsArray);

	if (matchingIndices == NIL)
	{
		/* No Op */
		return;
	}

	UpdateArrayWriter *arrayWriter = UpdateWriterGetArrayWriter(writer);
	bson_iter_t existingArrItr;
	BsonValueInitIterator(existingValue, &existingArrItr);

	int existingValueIndex = 0, pullArrayIndex = 0;
	int currentPullIndex = list_nth_int(matchingIndices, pullArrayIndex);
	while (bson_iter_next(&existingArrItr))
	{
		if (existingValueIndex == currentPullIndex)
		{
			/* Skip writing the value if it's a pull matched index */
			pullArrayIndex++;
			if (pullArrayIndex < matchingIndices->length)
			{
				currentPullIndex = list_nth_int(matchingIndices, pullArrayIndex);
			}
			UpdateArrayWriterSkipValue(arrayWriter);
		}
		else
		{
			UpdateArrayWriterWriteOriginalValue(arrayWriter,
												bson_iter_value(&existingArrItr));
		}
		existingValueIndex++;
	}
	UpdateArrayWriterFinalize(writer, arrayWriter);
}


/*
 * HandleUpdateDollarCurrentDate takes an existing value from the document, and
 * an updateValue presented from the updateSpec and writes the current date with
 * the type requirements of the update spec to the target element_writer.
 * $currentDate has no update specific context.
 */
void
HandleUpdateDollarCurrentDate(const bson_value_t *existingValue,
							  UpdateOperatorWriter *writer,
							  const bson_value_t *updateValue,
							  void *updateNodeContext,
							  const UpdateSetValueState *setValueState,
							  const CurrentDocumentState *state)
{
	struct timespec spec;
	clock_gettime(CLOCK_REALTIME, &spec);

	time_t epochSeconds = spec.tv_sec;
	uint32_t millisecondsInSecond = spec.tv_nsec / 1000000;
	uint64_t epochMilliseconds = (epochSeconds * 1000UL) + millisecondsInSecond;

	bson_value_t timestampBsonValue;
	timestampBsonValue.value_type = BSON_TYPE_TIMESTAMP;
	timestampBsonValue.value.v_timestamp.timestamp = epochSeconds;

	/* TODO: Add logic for "increment" field if it's requirement comes up.
	 * For now storing tv_nsec in the "increment" field. */
	timestampBsonValue.value.v_timestamp.increment = spec.tv_nsec;

	bson_value_t dateBsonValue;
	dateBsonValue.value_type = BSON_TYPE_DATE_TIME;
	dateBsonValue.value.v_datetime = epochMilliseconds;


	if (updateValue->value_type == BSON_TYPE_BOOL)
	{
		/* Specification says bool value should be true, but mongoDB impl works with false as well.
		 * So ignoring the check whether the provided bool val is true (or false) */

		/* Also, no need to check if the updateNode->fieldValue existed or what type it was,
		 * mongodb impl creates new (if field doesn't already exist),
		 * or changes it's type if already existed but of different type */
		UpdateWriterWriteModifiedValue(writer, &dateBsonValue);
		return;
	}
	else if (updateValue->value_type == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t nestedIterator;
		if (!bson_iter_init_from_data(&nestedIterator, updateValue->value.v_doc.data,
									  updateValue->value.v_doc.data_len) ||
			!bson_iter_next(&nestedIterator))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"The '$type' string field is required to be 'date' or 'timestamp': {$currentDate: {field : {$type: 'date'}}}")));
		}

		const char *key = bson_iter_key(&nestedIterator);

		/* Note: As of 23-03-2022, due to a potential bug in libbson,
		 * the function bson_init_from_json() fails when $currentDate{} contains {$type: "date"}
		 * So temporarily "$$type" is supported to overcome this limitation.
		 * TODO: Remove the "$$type" support once the bson_init_from_json() func is fixed in libbson */
		if ((strcmp(key, "$type") != 0 && strcmp(key, "$$type") != 0))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unrecognized $currentDate option: %s", key)));
		}

		if (!BSON_ITER_HOLDS_UTF8(&nestedIterator))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"The '$type' string field is required to be 'date' or 'timestamp': {$currentDate: {field : {$type: 'date'}}}")));
		}

		uint32_t pathLength;
		const char *typename = bson_iter_utf8(&nestedIterator, &pathLength);
		if (strcmp(typename, "timestamp") == 0)
		{
			/* No need to check if the updateNode->fieldValue existed or what type it was,
			 * mongodb impl creates new (if field doesn't already exist),
			 * or changes it's type if already existed but of different type */
			UpdateWriterWriteModifiedValue(writer, &timestampBsonValue);
			return;
		}
		else if (strcmp(typename, "date") == 0)
		{
			/* No need to check if the updateNode->fieldValue existed or what type it was,
			 * mongodb impl creates new (if field doesn't already exist),
			 * or changes it's type if already existed but of different type */
			UpdateWriterWriteModifiedValue(writer, &dateBsonValue);
			return;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"The '$type' string field is required to be 'date' or 'timestamp': {$currentDate: {field : {$type: 'date'}}}")));
		}
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"%s is not valid type for $currentDate. Please use a boolean ('true') or a $type expression ({$type: 'timestamp/date'})",
							BsonTypeName(updateValue->value_type))));
	}
}


/*
 * HandleUpdateDollarRename takes an existing value from the document, and
 * a source path from the updateContext presented from the updateSpec and
 * looks up the sourcePath in the document and writes the value
 * to the target element_writer. If the source doesn't exist, the current
 * value is replayed.
 * $rename stores the sourcePath in the updateContext.
 */
void
HandleUpdateDollarRename(const bson_value_t *existingValue,
						 UpdateOperatorWriter *writer,
						 const bson_value_t *updateValue,
						 void *updateNodeContext,
						 const UpdateSetValueState *setValueState,
						 const CurrentDocumentState *state)
{
	if (setValueState->isArray || setValueState->hasArrayAncestors)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"The target field of a rename cannot be an array element")));
	}

	bson_value_t renameSourceValue = RenameSourceGetValue(state->sourceDocument,
														  (const
														   char *) updateNodeContext);

	if (renameSourceValue.value_type != BSON_TYPE_EOD)
	{
		UpdateWriterWriteModifiedValue(writer, &renameSourceValue);
	}
}


/*
 * HandleUpdateDollarRenameSource takes an existing value from the document, and
 * validates the source value state and treats it as an unset value.
 */
void
HandleUpdateDollarRenameSource(const bson_value_t *existingValue,
							   UpdateOperatorWriter *writer,
							   const bson_value_t *updateValue,
							   void *updateNodeContext,
							   const UpdateSetValueState *setValueState,
							   const CurrentDocumentState *state)
{
	if (setValueState->isArray || setValueState->hasArrayAncestors)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"The source field of a rename cannot be an array element")));
	}

	HandleUpdateDollarUnset(existingValue,
							writer,
							updateValue,
							updateNodeContext,
							setValueState,
							state);
}


/*
 * HandleUpdateDollarAddToSet writes the value specified by the
 * updateSpec into the target writer if the source value is an
 * array and if it doesn't contain any of the values specified
 * in the $addToSet value(s).
 * $addToSet has no update specific context.
 */
void
HandleUpdateDollarAddToSet(const bson_value_t *existingValue,
						   UpdateOperatorWriter *writer,
						   const bson_value_t *updateValue,
						   void *updateNodeContext,
						   const UpdateSetValueState *setValueState,
						   const CurrentDocumentState *state)
{
	/* If $addToSet is used on a field that is not an array, the operation should fail. */
	if (existingValue->value_type != BSON_TYPE_EOD &&
		existingValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Cannot apply $addToSet to non-array field. Field named '%.*s' has non-array type %s",
							setValueState->fieldPath->length,
							setValueState->fieldPath->string,
							BsonTypeName(existingValue->value_type))));
	}

	/* If the update spec is a bson doc, validate if it is with $each modifier or
	 * we are adding a new doc
	 */
	bool isEach = false;
	bson_value_t elementsToAdd = { 0 };
	if (updateValue->value_type == BSON_TYPE_DOCUMENT &&
		!IsBsonValueEmptyDocument(updateValue))
	{
		ValidateAddToSetWithDollarEach(updateValue,
									   &isEach,
									   &elementsToAdd);
	}

	AddToSetWriteFinalArray(writer,
							existingValue,
							isEach ? &elementsToAdd : updateValue,
							isEach);
}


/*
 * HandleUpdateDollarPullAll writes the value specified by the
 * original existingValue into the target writer but removes
 * all the elements specified by the $pullAll operator updateSpec
 * value.
 * $pullAll has no update specific context.
 */
void
HandleUpdateDollarPullAll(const bson_value_t *existingValue,
						  UpdateOperatorWriter *writer,
						  const bson_value_t *updateValue,
						  void *updateNodeContext,
						  const UpdateSetValueState *setValueState,
						  const CurrentDocumentState *state)
{
	/* If $pullAll argument is not an array, the operation should fail. */
	if (updateValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"$pullAll requires an array argument but was given a %s",
							BsonTypeName(updateValue->value_type))));
	}

	if (existingValue->value_type == BSON_TYPE_EOD)
	{
		/* if the path doesn't exist in the source document, it is a no-op. */
		return;
	}

	/* If $pullAll is applicable only on the array field */
	if (existingValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Cannot apply $pullAll to a non-array value")));
	}

	bson_iter_t currentArrayIter;
	bson_iter_t pullAllIter;
	BsonValueInitIterator(existingValue, &currentArrayIter);
	BsonValueInitIterator(updateValue, &pullAllIter);

	UpdateArrayWriter *arrayWriter = UpdateWriterGetArrayWriter(writer);

	/* For each existing array elements, check if this needs to be removed, by comparing it
	 * from the update values. */
	while (bson_iter_next(&currentArrayIter))
	{
		bool found = false;

		/* create a copy of pullAllIter to avoid init everytime */
		bson_iter_t pullAllIterCopy = pullAllIter;

		while (bson_iter_next(&pullAllIterCopy))
		{
			if (BsonValueEquals(bson_iter_value(&currentArrayIter),
								bson_iter_value(&pullAllIterCopy)))
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			const bson_value_t *fieldValue = bson_iter_value(&currentArrayIter);
			UpdateArrayWriterWriteOriginalValue(arrayWriter, fieldValue);
		}
		else
		{
			UpdateArrayWriterSkipValue(arrayWriter);
		}
	}

	UpdateArrayWriterFinalize(writer, arrayWriter);
}


/*
 * HandleUpdateDollarPush writes the value specified by the
 * original existingValue into the target writer and appends the
 * new elements specified by the $push updateSpec value.
 * $push has no update specific context.
 */
void
HandleUpdateDollarPush(const bson_value_t *existingValue,
					   UpdateOperatorWriter *writer,
					   const bson_value_t *updateValue,
					   void *updateNodeContext,
					   const UpdateSetValueState *setValueState,
					   const CurrentDocumentState *state)
{
	/* Validate the update spec and get a state which can be processed uniformly */
	DollarPushUpdateState pushState;
	memset(&pushState, 0, sizeof(pushState));
	ValidateUpdateSpecAndSetPushUpdateState(updateValue, &pushState);

	bson_value_t currentValue = *existingValue;
	if (currentValue.value_type == BSON_TYPE_EOD)
	{
		/* Field is not present, insert the update value in newly created array
		 * First create a doc that holds array and project the array in the tree update node
		 */
		pgbson_writer writer;
		PgbsonWriterInit(&writer);
		bson_iter_t emptyArrItr;
		PgbsonInitIterator(PgbsonWriterGetPgbson(&writer), &emptyArrItr);
		currentValue = *bson_iter_value(&emptyArrItr);
		currentValue.value_type = BSON_TYPE_ARRAY;
	}

	if (currentValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"The field '%.*s' must be an array but is of type %s in document { _id: %s }",
							setValueState->fieldPath->length,
							setValueState->fieldPath->string,
							BsonTypeName(currentValue.value_type),
							BsonValueToJsonForLogging(&state->documentId)),
						errdetail_log(
							"The field in $push must be an array but is of type %s",
							BsonTypeName(currentValue.value_type))));
	}

	UpdateArrayWriter *arrayWriter = UpdateWriterGetArrayWriter(writer);
	if (!pushState.modifiersExist)
	{
		/* No modifiers exist for this case, simply push the value at the end */
		bson_iter_t existingArrItr;
		BsonValueInitIterator(&currentValue, &existingArrItr);
		while (bson_iter_next(&existingArrItr))
		{
			UpdateArrayWriterWriteOriginalValue(arrayWriter,
												bson_iter_value(&existingArrItr));
		}

		/* Add the item to push at the end */
		UpdateArrayWriterWriteModifiedValue(arrayWriter, updateValue);
	}
	else
	{
		/* Modifiers exist, create a temp array for performing all the modification before adding array items as children to the node*/
		ElementWithIndex *elementsArr;
		uint32_t elementsArrLen;

		/* memory allocation needed for existing element and elements of $each values */
		int64_t existingArrayLength = BsonDocumentValueCountKeys(&currentValue);

		/* this is the length before slice and calculated here to create a temp array upfront for other modification */
		elementsArrLen = existingArrayLength + pushState.dollarEachElementCount;
		elementsArr = palloc(elementsArrLen * sizeof(ElementWithIndex));

		/* Apply the modifiers for $push */
		ApplyDollarPushModifiers(&currentValue, &pushState,
								 elementsArr, elementsArrLen);

		/* Write the target values into the writer based on the slice. */
		for (int64_t i = pushState.sliceStart; i < pushState.sliceEnd; i++)
		{
			UpdateArrayWriterWriteModifiedValue(arrayWriter, &elementsArr[i].bsonValue);
		}

		/* All done with temp resources, release*/
		pfree(elementsArr);
	}

	UpdateArrayWriterFinalize(writer, arrayWriter);
}


/*
 * HandleUpdateDollarPop writes the value specified by the
 * original existingValue into the target writer but trims
 * the leading or trailing element from the array based on
 * the value provided by the update specification.
 * $pop has no update specific context.
 */
void
HandleUpdateDollarPop(const bson_value_t *existingValue,
					  UpdateOperatorWriter *writer,
					  const bson_value_t *updateValue,
					  void *updateNodeContext,
					  const UpdateSetValueState *setValueState,
					  const CurrentDocumentState *state)
{
	if (!BsonTypeIsNumber(updateValue->value_type))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"Expected a number in: %s: %s",
							setValueState->relativePath,
							BsonValueToJsonForLogging(updateValue)),
						errdetail_log(
							"Expected a number in $pop, found: %s",
							BsonTypeName(updateValue->value_type))));
	}

	double doubleVal = BsonValueAsDouble(updateValue);
	if (floor(doubleVal) != doubleVal)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"Expected an integer: %s: %s",
							setValueState->relativePath,
							BsonValueToJsonForLogging(updateValue)),
						errdetail_log(
							"Expected a number in $pop, found: %s",
							BsonTypeName(updateValue->value_type))));
	}

	if ((int) doubleVal != 1 && (int) doubleVal != -1)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"$pop expects 1 or -1, found: %s",
							BsonValueToJsonForLogging(updateValue))));
	}

	bool isFirst = (int) doubleVal == -1 ? true : false;

	if (existingValue->value_type == BSON_TYPE_EOD)
	{
		/* if the path doesn't exist in the source document, it is a no-op. */
		return;
	}

	if (existingValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Path '%s' contains an element of non-array type '%s'",
							setValueState->relativePath,
							BsonTypeName(existingValue->value_type)),
						errdetail_log(
							"Path in $pop contains an element of non-array type '%s'",
							BsonTypeName(existingValue->value_type))));
	}

	bson_iter_t arrItr;
	BsonValueInitIterator(existingValue, &arrItr);
	UpdateArrayWriter *arrayWriter = UpdateWriterGetArrayWriter(writer);

	/* fieldIndex is index of children of a node in Tree. 'i' is 'while loop' iterator */
	int i = 0;
	bson_value_t currentValue, previousValue, valueSelected;
	while (bson_iter_next(&arrItr))
	{
		previousValue = currentValue;
		currentValue = *(bson_value_t *) bson_iter_value(&arrItr);
		valueSelected = isFirst ? currentValue : previousValue;
		if (i >= 1)
		{
			UpdateArrayWriterWriteModifiedValue(arrayWriter, &valueSelected);
		}
		else
		{
			UpdateArrayWriterSkipValue(arrayWriter);
		}
		i++;
	}

	UpdateArrayWriterFinalize(writer, arrayWriter);
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */


/*
 * Visits the top level field of a given path (e.g. the value at a.b.c given a filterPath of a.b.c)
 * and stores the value at the field into the state.
 */
static bool
RenameVisitTopLevelField(pgbsonelement *element, const StringView *filterPath,
						 void *state)
{
	bson_value_t *value = (bson_value_t *) state;
	*value = element->bsonValue;
	return value->value_type == BSON_TYPE_EOD;
}


/*
 * Updates the state to reset it if the path is a mismatch.
 */
static void
RenameSetTraverseErrorResult(void *state, TraverseBsonResult traverseResult)
{
	/* On a type mismatch or path not found, we simply set the value to EOD */
	bson_value_t *value = (bson_value_t *) state;
	value->value_type = BSON_TYPE_EOD;
}


/*
 * Returns whether or not traversal should continue.
 * This is only if we haven't already found the rename source.
 */
static bool
RenameProcessIntermediateArray(void *state, const bson_value_t *value)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
					errmsg("The source field of a rename cannot be an array element")));
}


/* Return the BitWise operator type */
static MongoBitwiseOperatorType
GetMongoBitwiseOperator(const char *key)
{
	MongoBitwiseOperatorType operatorType = BITWISE_OPERATOR_UNKNOWN;
	int operatorIndex = 0;
	while (BitwiseOperators[operatorIndex].mongoOperatorName != NULL)
	{
		if (strcmp(key, BitwiseOperators[operatorIndex].mongoOperatorName) == 0)
		{
			operatorType = BitwiseOperators[operatorIndex].operatorType;
			break;
		}
		operatorIndex++;
	}
	return operatorType;
}


/* Validate the fields and modifier value.
 * We should use this operator with integer fields (either 32-bit integer or 64-bit
 * integer) only.
 */
static void
ValidateBitwiseInputParams(const MongoBitwiseOperatorType operatorType,
						   const char *updatePath,
						   const bson_value_t *state,
						   const char *key,
						   const bson_value_t *modifier,
						   bson_iter_t *updateSpec,
						   const CurrentDocumentState *docState)
{
	if (operatorType == BITWISE_OPERATOR_UNKNOWN)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"The $bit modifier only supports 'and', 'or', and 'xor', "
							"not '%s' which is an unknown operator: { \"%s\" : %s }",
							key, key,
							BsonValueToJsonForLogging(modifier))));
	}

	if (!(modifier->value_type == BSON_TYPE_INT32 ||
		  modifier->value_type == BSON_TYPE_INT64))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("The $bit modifier field must be an Integer(32/64 bit);"
							   " a '%s' is not supported here: { \"%s\" : %s }",
							   BsonTypeName(modifier->value_type),
							   key, BsonValueToJsonForLogging(modifier)),
						errdetail_log(
							"The $bit modifier field must be an Integer(32/64 bit);"
							" a '%s' is not supported here",
							BsonTypeName(modifier->value_type))));
	}

	if (state->value_type != BSON_TYPE_EOD)
	{
		if (!(state->value_type == BSON_TYPE_INT32 ||
			  state->value_type == BSON_TYPE_INT64))
		{
			/* Get the document Id for error reporting */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Cannot apply $bit to a value of non-integral type."
								   "{ \"_id\" : %s } has the field %s of non-integer type %s",
								   BsonValueToJsonForLogging(&docState->documentId),
								   updatePath, BsonTypeName(state->value_type)),
							errdetail_log(
								"Cannot apply $bit to a value of non-integral type %s",
								BsonTypeName(state->value_type))));
		}
	}
}


/*
 * Traverses the source document for that rename path source dotted path
 * and gets the value at the path. If it doesn't exist, returns
 * BSON_TYPE_EOD
 */
static bson_value_t
RenameSourceGetValue(const pgbson *sourceDocument, const char *sourcePathString)
{
	bson_iter_t sourceDocIterator;
	PgbsonInitIterator(sourceDocument, &sourceDocIterator);

	TraverseBsonExecutionFuncs renameExecutionFuncs =
	{
		.VisitTopLevelField = RenameVisitTopLevelField,
		.VisitArrayField = NULL,
		.SetTraverseResult = RenameSetTraverseErrorResult,
		.ContinueProcessIntermediateArray = RenameProcessIntermediateArray,
		.SetIntermediateArrayIndex = NULL,
	};

	bson_value_t renameSourceValue = { 0 };
	TraverseBson(&sourceDocIterator,
				 sourcePathString,
				 &renameSourceValue,
				 &renameExecutionFuncs);
	return renameSourceValue;
}


/*
 * For an $addToSet operator, inspects the updateSpec value
 * and validates whether it is a $each modifier or just add a single
 * value.
 */
static void
ValidateAddToSetWithDollarEach(const bson_value_t *updateValue,
							   bool *isEach,
							   bson_value_t *elementsToAdd)
{
	*isEach = false;
	pgbsonelement element;
	if (TryGetBsonValueToPgbsonElement(updateValue, &element) &&
		(strcmp(element.path, "$each") == 0))
	{
		*isEach = true;

		/* The argument to $each in $addToSet must be an array, else error out */
		if (element.bsonValue.value_type != BSON_TYPE_ARRAY)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
							errmsg(
								"The argument to $each in $addToSet must be an array but it was of type %s",
								BsonTypeName(element.bsonValue.value_type))));
		}
		*elementsToAdd = element.bsonValue;
	}
}


/*
 * Applies the $addToSet values into the target document in addition
 * to all the elements in the existing array, if the new values
 * are not present in the existing array.
 */
static void
AddToSetWriteFinalArray(UpdateOperatorWriter *writer,
						const bson_value_t *existingValue,
						const bson_value_t *elementsToAdd,
						const bool isEach)
{
	bson_iter_t currentArrayIter;

	UpdateArrayWriter *arrayWriter = UpdateWriterGetArrayWriter(writer);

	List *existingElements = NIL;
	if (existingValue->value_type != BSON_TYPE_EOD)
	{
		BsonValueInitIterator(existingValue, &currentArrayIter);

		/* Add all the existing elements first. */
		while (bson_iter_next(&currentArrayIter))
		{
			const bson_value_t *value = bson_iter_value(&currentArrayIter);

			bson_value_t *elementInList = palloc(sizeof(bson_value_t));
			*elementInList = *value;
			existingElements = lappend(existingElements, elementInList);

			UpdateArrayWriterWriteOriginalValue(arrayWriter, value);
		}
	}

	/* For every new elements, iterate over the child elements list and add a new Node, if it is not a duplicate */
	if (isEach)
	{
		bson_iter_t newElementsIter;
		BsonValueInitIterator(elementsToAdd, &newElementsIter);
		while (bson_iter_next(&newElementsIter))
		{
			const bson_value_t *newValue = bson_iter_value(&newElementsIter);
			ListCell *cell;
			bool found = false;
			foreach(cell, existingElements)
			{
				bson_value_t *item = lfirst(cell);
				if (BsonValueEquals(item, newValue))
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				bson_value_t *elementInList = palloc(sizeof(bson_value_t));
				*elementInList = *newValue;
				existingElements = lappend(existingElements, elementInList);
				UpdateArrayWriterWriteModifiedValue(arrayWriter, newValue);
			}
		}
	}
	else
	{
		ListCell *cell;
		bool found = false;
		foreach(cell, existingElements)
		{
			bson_value_t *item = lfirst(cell);
			if (BsonValueEquals(item, elementsToAdd))
			{
				found = true;
				break;
			}
		}

		if (!found)
		{
			UpdateArrayWriterWriteModifiedValue(arrayWriter, elementsToAdd);
		}
	}

	list_free_deep(existingElements);
	UpdateArrayWriterFinalize(writer, arrayWriter);
}


/*
 * Validates the updateSpec value for $push and updates the DollarPushUpdateState
 * for a given updateSpec to be used to write the target document into the writer.
 */
static void
ValidateUpdateSpecAndSetPushUpdateState(const bson_value_t *fieldUpdateValue,
										DollarPushUpdateState *pushState)
{
	if (fieldUpdateValue->value_type != BSON_TYPE_DOCUMENT)
	{
		/* No modifiers exist append the value as it is in the array */
		pushState->modifiersExist = false;
		return;
	}

	/*
	 * Check if updateValue has either modifiers or an object without modifiers for $push
	 * Both can't be part of the spec
	 */
	bson_iter_t updateValItr;
	BsonValueInitIterator(fieldUpdateValue, &updateValItr);

	/* Initialize below to be of type BSON_TYPE_EOD */
	bson_value_t eachBsonValue;
	eachBsonValue.value_type = BSON_TYPE_EOD;
	bson_value_t positionBsonValue;
	positionBsonValue.value_type = BSON_TYPE_EOD;
	bson_value_t sliceBsonValue;
	sliceBsonValue.value_type = BSON_TYPE_EOD;
	bson_value_t sortBsonValue;
	sortBsonValue.value_type = BSON_TYPE_EOD;

	/**
	 * This holds the first key that is seen except all the $modifiers which is reported in error if $each is also present
	 *
	 * e.g: {$push: {a: {"$slice": 2, "bad_plugin": 1, "another_bad_key": 2, $each: [1,2,3]}}}
	 * Here nonClauseKey = "bad_plugin"
	 * */
	char *nonClauseKey = NULL;

	/**
	 * This holds the first dollar prefixed key that is seen including all the $modifiers which is reported in error if $each is not present
	 *
	 * e.g: {$push: {a: {"$slice": 2, "bad_plugin": 1, "another_bad_key": 2}}}
	 * Here firstDollarKey = "$slice"
	 * */
	char *firstDollarKey = NULL;
	while (bson_iter_next(&updateValItr))
	{
		const char *key = bson_iter_key(&updateValItr);
		if (key != NULL && firstDollarKey == NULL && key[0] == '$')
		{
			/* Set the first $ prefixed key this is used later for error reporting if $each is  */
			firstDollarKey = pnstrdup(key, strlen(key));
		}
		if (strcmp(key, "$position") == 0)
		{
			positionBsonValue = *bson_iter_value(&updateValItr);
		}
		else if (strcmp(key, "$each") == 0)
		{
			eachBsonValue = *bson_iter_value(&updateValItr);
		}
		else if (strcmp(key, "$sort") == 0)
		{
			sortBsonValue = *bson_iter_value(&updateValItr);
		}
		else if (strcmp(key, "$slice") == 0)
		{
			sliceBsonValue = *bson_iter_value(&updateValItr);
		}
		else if (nonClauseKey == NULL)
		{
			nonClauseKey = pnstrdup(key, strlen(key));
		}

		if (nonClauseKey != NULL && eachBsonValue.value_type != BSON_TYPE_EOD)
		{
			/* If a non clause key is present and $each is also provide,
			 * it's an invalid spec def
			 */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unrecognized clause in $push: %s",
								   nonClauseKey)));
		}
	}

	/* $each is a required modifier for other modifiers to have impact */
	/* Based on native mongo behavior and jstest, when $each is missing, other modifiers are treated as simple objects to push. */
	if (eachBsonValue.value_type == BSON_TYPE_EOD)
	{
		pushState->modifiersExist = false;
		return;
	}

	pushState->modifiersExist = true;

	/* Validate $each spec */
	if (eachBsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"The argument to $each in $push must be an array but it was of type: %s",
							BsonTypeName(eachBsonValue.value_type))));
	}
	pushState->dollarEachArray = eachBsonValue;
	pushState->dollarEachElementCount = BsonDocumentValueCountKeys(&eachBsonValue);

	/* Validate $slice spec , if $slice is not present set it to UINT32_MAX */
	bool validSliceValue = false;
	if (sliceBsonValue.value_type == BSON_TYPE_EOD)
	{
		pushState->slice = UINT32_MAX;
		validSliceValue = true;
	}
	else if (BsonValueIsNumber(&sliceBsonValue))
	{
		double sliceValue = BsonValueAsDouble(&sliceBsonValue);
		if (floor(sliceValue) == sliceValue)
		{
			validSliceValue = true;
			pushState->slice = (int64_t) sliceValue;
		}
	}
	if (!validSliceValue)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"The argument to $slice in $push must be an integer value but was given type: %s",
							BsonTypeName(sliceBsonValue.value_type))));
	}

	/* Validate $position spec, if not present set it to UINT32_MAX */
	bool validPositionValue = false;
	if (positionBsonValue.value_type == BSON_TYPE_EOD)
	{
		pushState->position = UINT32_MAX;
		validPositionValue = true;
	}
	else if (BsonValueIsNumber(&positionBsonValue))
	{
		double positionValue = BsonValueAsDouble(&positionBsonValue);
		if (floor(positionValue) == positionValue)
		{
			validPositionValue = true;
			pushState->position = (int64_t) positionValue;
		}
	}
	if (!validPositionValue)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"The value for $position must be an integer value, not of type: %s",
							BsonTypeName(positionBsonValue.value_type))));
	}

	/* Validate $sort spec, and all nested values if value is object */
	SortContext *sortContext = palloc0(sizeof(SortContext));
	ValidateSortSpecAndSetSortContext(sortBsonValue, sortContext);
	pushState->sortContext = sortContext;
}


/**
 * Accepts a BSON Array and a validated DollarPushUpdateState to apply the modification,
 * updates the elementsArr with all modification in this order
 * $position->$each->$sort->$slice
 *
 * Parameters:
 * bson_value_t *bsonArray : source array bson where all modification will be applied
 * DollarPushUpdateState *pushState : Validated push update modifier state
 * ElementWithIndex *elementsArr : An memory allocated array where the result of modification is updated
 * uint32_t elementsArrLen : Length of the array -> elementsArr
 *
 * Side effects: In case of slice operation in pushState exist, this function will set sliceStart and sliceEnd in pushState
 * otherwise sliceStart will be 0 & sliceEnd will be elementsArrLen
 */
static void
ApplyDollarPushModifiers(const bson_value_t *bsonArray,
						 DollarPushUpdateState *pushState,
						 ElementWithIndex *elementsArr, int64_t elementsArrLen)
{
	if (bsonArray->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Unexpected type other than array")));
	}
	uint32_t index = 0;

	/* Step 1: Traverse and write till first $position objects */
	/* Start writing in the temp array with all the modifications */
	bson_iter_t existingArrItr;
	BsonValueInitIterator(bsonArray, &existingArrItr);

	/* If negative position value, wrap it around the end */
	int64_t position = pushState->position;

	/* Existing items length is total - elements in $each */
	int64_t existingArrLen = elementsArrLen - pushState->dollarEachElementCount;
	if (position < 0)
	{
		position = -position;
		position = (position >= existingArrLen) ? 0 : existingArrLen - position;
	}
	while (index < (uint32_t) position &&
		   bson_iter_next(&existingArrItr))
	{
		elementsArr[index] = *GetElementWithIndex(bson_iter_value(&existingArrItr),
												  index);
		index++;
	}

	/* Step 2: Write the element from $each value */
	if (pushState->dollarEachArray.value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t dollarEachItr;
		BsonValueInitIterator(&pushState->dollarEachArray, &dollarEachItr);
		while (bson_iter_next(&dollarEachItr))
		{
			elementsArr[index] = *GetElementWithIndex(bson_iter_value(&dollarEachItr),
													  index);
			index++;
		}
	}

	/* Step 3: Write remaining elements from original doc */
	while (bson_iter_next(&existingArrItr))
	{
		elementsArr[index] = *GetElementWithIndex(bson_iter_value(&existingArrItr),
												  index);
		index++;
	}

	/* Step 4: Do Sort */
	if (pushState->sortContext->sortType != SortType_No_Sort)
	{
		/**
		 * TODO: Optimization suggestion, use std:partial_sort kind of technique to limit compute when both $sort & $slice
		 * are present
		 */
		qsort_arg(elementsArr, elementsArrLen, sizeof(ElementWithIndex),
				  CompareBsonValuesForSort, pushState->sortContext);
	}

	/* Step 5: Set the slice range in pushState */
	int64_t sliceIndex = pushState->slice;
	if (sliceIndex < 0)
	{
		/* negative slice value, skip from front*/
		sliceIndex = -sliceIndex;
		pushState->sliceStart = sliceIndex > elementsArrLen ? 0 :
								elementsArrLen - sliceIndex;
		pushState->sliceEnd = elementsArrLen;
	}
	else
	{
		/* positive slice value, trim from end*/
		pushState->sliceStart = 0;
		pushState->sliceEnd = sliceIndex > elementsArrLen ? elementsArrLen :
							  sliceIndex;
	}
}
