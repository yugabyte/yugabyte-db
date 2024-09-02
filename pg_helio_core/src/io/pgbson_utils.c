/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/io/pgbson_utils.c
 *
 * Implementation of internal helpers for the BSON type.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
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

#include "io/helio_bson_core.h"
#include "utils/type_cache.h"
#include "utils/mongo_errors.h"
#include "types/decimal128.h"
#include "io/bson_traversal.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

PGDLLEXPORT const StringView IdFieldStringView = { .string = "_id", .length = 3 };

/* arithmetic functions */
static void AddDoubleToValue(bson_value_t *current, double value);
static void AddInt64ToValue(bson_value_t *current, int64_t value,
							bool *overflowedFromInt64);
static void AddInt32ToValue(bson_value_t *current, int32_t value,
							bool *overflowedFromInt64);
static void AddDecimal128ToValue(bson_value_t *current, const bson_value_t *value);
static void SubtractDecimal128FromValue(bson_value_t *current, const bson_value_t *value);
static bool TraverseBsonCore(bson_iter_t *documentIterator, const StringView *filterPath,
							 void *state,
							 const TraverseBsonExecutionFuncs *executionFunctions,
							 bool inArrayContext);


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */


/*
 * Checks if the bson value is of type array and all the elements are numbers.
 */
bool
BsonValueHoldsNumberArray(const bson_value_t *currentValue, int32_t *numElements)
{
	if (currentValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg(
							"value must be of type array")));
	}
	bson_iter_t arrayIter;
	BsonValueInitIterator(currentValue, &arrayIter);

	*numElements = 0;
	bool is_number_array = true;
	while (bson_iter_next(&arrayIter))
	{
		if (!BSON_ITER_HOLDS_NUMBER(&arrayIter))
		{
			is_number_array = false;
			break;
		}
		*numElements = *numElements + 1;
	}

	return is_number_array;
}


/*
 * PgbsonDecomposeFields takes a bson object and splits its fields into
 * individual (single-element) bson objects.
 *
 * .e.g.: {"a": [1,2,3], "b": {"x": 1, "y": 1}} ->
 *        List[{"a": [1,2,3]}, {"b": {"x": 1, "y": 1}}]
 */
List *
PgbsonDecomposeFields(const pgbson *document)
{
	List *documents = NIL;

	bson_iter_t iter;
	PgbsonInitIterator(document, &iter);
	while (bson_iter_next(&iter))
	{
		pgbsonelement element;
		BsonIterToPgbsonElement(&iter, &element);
		documents = lappend(documents, PgbsonElementToPgbson(&element));
	}

	return documents;
}


/*
 * Adds the number stored in number to state and modifies state.
 * returns true if addition happened (type was supported)
 * @state: The augend
 * @number: The addend
 * @overflowedFromInt64: Used for AddInt64ToValue(); set if overflow from Int64 occurs, unset otherwise.
 */
bool
AddNumberToBsonValue(bson_value_t *state, const bson_value_t *number,
					 bool *overflowedFromInt64)
{
	if (!BsonValueIsNumberOrBool(state))
	{
		return false;
	}

	switch (number->value_type)
	{
		case BSON_TYPE_INT64:
		{
			AddInt64ToValue(state, number->value.v_int64, overflowedFromInt64);
			return true;
		}

		case BSON_TYPE_INT32:
		{
			AddInt32ToValue(state, number->value.v_int32, overflowedFromInt64);
			return true;
		}

		case BSON_TYPE_DOUBLE:
		{
			AddDoubleToValue(state, number->value.v_double);
			return true;
		}

		case BSON_TYPE_DECIMAL128:
		{
			AddDecimal128ToValue(state, number);
			return true;
		}

		default:
		{
			return false;
		}
	}
}


/*
 * Subtracts the number stored in subtrahend to state and modifies state.
 * returns true if substraction happened (type was supported)
 */
bool
SubtractNumberFromBsonValue(bson_value_t *state, const bson_value_t *subtrahend,
							bool *overflowedFromInt64)
{
	if (!BsonValueIsNumberOrBool(state))
	{
		return false;
	}

	switch (subtrahend->value_type)
	{
		case BSON_TYPE_INT64:
		{
			AddInt64ToValue(state, -subtrahend->value.v_int64, overflowedFromInt64);
			return true;
		}

		case BSON_TYPE_INT32:
		{
			AddInt32ToValue(state, -subtrahend->value.v_int32, overflowedFromInt64);
			return true;
		}

		case BSON_TYPE_DOUBLE:
		{
			AddDoubleToValue(state, -subtrahend->value.v_double);
			return true;
		}

		case BSON_TYPE_DECIMAL128:
		{
			SubtractDecimal128FromValue(state, subtrahend);
			return true;
		}

		default:
		{
			return false;
		}
	}
}


/*
 * Bitwise AND the number stored in number to state and modifies state.
 */
void
BitwiseAndToBsonValue(bson_value_t *state, const bson_value_t *number)
{
	if (state->value_type == BSON_TYPE_INT64 || number->value_type == BSON_TYPE_INT64)
	{
		int64_t result = BsonValueAsInt64(state) & BsonValueAsInt64(number);
		state->value.v_int64 = result;
		state->value_type = BSON_TYPE_INT64;
	}
	else
	{
		int32_t result = state->value.v_int32 & number->value.v_int32;
		state->value.v_int32 = result;
		state->value_type = BSON_TYPE_INT32;
	}
}


/*
 * Bitwise OR the number stored in number to state and modifies state.
 */
void
BitwiseOrToBsonValue(bson_value_t *state, const bson_value_t *number)
{
	if (state->value_type == BSON_TYPE_INT64 || number->value_type == BSON_TYPE_INT64)
	{
		int64_t result = BsonValueAsInt64(state) | BsonValueAsInt64(number);
		state->value.v_int64 = result;
		state->value_type = BSON_TYPE_INT64;
	}
	else
	{
		int32_t result = state->value.v_int32 | number->value.v_int32;
		state->value.v_int32 = result;
		state->value_type = BSON_TYPE_INT32;
	}
}


/*
 * Bitwise XOR the number stored in number to state and modifies state.
 */
void
BitwiseXorToBsonValue(bson_value_t *state, const bson_value_t *number)
{
	if (state->value_type == BSON_TYPE_INT64 || number->value_type == BSON_TYPE_INT64)
	{
		int64_t result = BsonValueAsInt64(state) ^ BsonValueAsInt64(number);
		state->value.v_int64 = result;
		state->value_type = BSON_TYPE_INT64;
	}
	else
	{
		int32_t result = state->value.v_int32 ^ number->value.v_int32;
		state->value.v_int32 = result;
		state->value_type = BSON_TYPE_INT32;
	}
}


/*
 * Multiplies the bson value with factor and updates types based on below logic:
 * (decimal128 * int32/int64/double/decimal128) or (int32/int64/double/decimal128 * decimal128) => decimal128 ( Infinity / -Infinity in case of overflow or underflow for decimal128 )
 * (double * int32/int64) or (int32/int64 * double) or (double * double) => double
 * (int32 * int64) or (int64 * int64) => int64: in case of no overflow for int64
 *                                       double: if convertInt64OverflowToDouble is true and case of integer overflow for int64
 *                                       return false : if convertInt64OverflowToDouble is false and case of integer overflow for int64  & cannot complete multiplication
 * (int32 * int32) => int32 or int64 in case of integer overflow for int32
 */
bool
MultiplyWithFactorAndUpdate(bson_value_t *state, const bson_value_t *factor, bool
							convertInt64OverflowToDouble)
{
	if (!BsonValueIsNumber(state) || !BsonValueIsNumber(factor))
	{
		return false;
	}

	if (state->value_type == BSON_TYPE_DECIMAL128 || factor->value_type ==
		BSON_TYPE_DECIMAL128)
	{
		/* Try type promotion to decimal 128 for both */
		state->value.v_decimal128 = GetBsonValueAsDecimal128Quantized(state);
		state->value_type = BSON_TYPE_DECIMAL128;

		bson_value_t decimalFactor = *factor;
		decimalFactor.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
			&decimalFactor);
		decimalFactor.value_type = BSON_TYPE_DECIMAL128;

		/* All decimal128 flag exceptions are ignored */
		MultiplyDecimal128Numbers(state, &decimalFactor, state);
	}
	else if (state->value_type == BSON_TYPE_DOUBLE || factor->value_type ==
			 BSON_TYPE_DOUBLE)
	{
		state->value.v_double = BsonValueAsDouble(state) * BsonValueAsDouble(factor);
		state->value_type = BSON_TYPE_DOUBLE;
	}
	else if (state->value_type == BSON_TYPE_INT64 || factor->value_type ==
			 BSON_TYPE_INT64)
	{
		/* any one operand is int64 and other is not or both are int64 */
		int64_t product = BsonValueAsInt64(state) * BsonValueAsInt64(factor);

		/* Check product is not zero to remove Dividebyzero error and then check int64 overflow */
		if (product != 0 && BsonValueAsInt64(state) != product / BsonValueAsInt64(factor))
		{
			if (convertInt64OverflowToDouble)
			{
				/* coerce to double if there is an overflow. */
				state->value.v_double = BsonValueAsDouble(state) * BsonValueAsDouble(
					factor);
				state->value_type = BSON_TYPE_DOUBLE;
			}
			else
			{
				/* fail in case multiplication results in overflow from Int64 */
				return false;
			}
		}
		else
		{
			state->value.v_int64 = product;
			state->value_type = BSON_TYPE_INT64;
		}
	}
	else
	{
		/* if operands are int32, then promote to int64 if necessary */
		int64_t product = BsonValueAsInt64(state) * BsonValueAsInt64(factor);

		/* Check overflow */
		if (product < INT32_MIN || product > INT32_MAX)
		{
			state->value.v_int64 = product;
			state->value_type = BSON_TYPE_INT64;
		}
		else
		{
			state->value.v_int32 = (int32_t) product;
		}
	}
	return true;
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */


/*
 * BsonTypeName returns the type code of a bson type by name.
 * It is the inverse function of BsonTypeName() and is used in scenarios
 * like $type.
 */
bson_type_t
BsonTypeFromName(const char *name)
{
	if (strcmp(name, "null") == 0)
	{
		return BSON_TYPE_NULL;
	}

	if (strcmp(name, "undefined") == 0)
	{
		return BSON_TYPE_UNDEFINED;
	}

	if (strcmp(name, "minKey") == 0)
	{
		return BSON_TYPE_MINKEY;
	}

	if (strcmp(name, "maxKey") == 0)
	{
		return BSON_TYPE_MAXKEY;
	}

	if (strcmp(name, "bool") == 0)
	{
		return BSON_TYPE_BOOL;
	}

	if (strcmp(name, "int") == 0)
	{
		return BSON_TYPE_INT32;
	}

	if (strcmp(name, "long") == 0)
	{
		return BSON_TYPE_INT64;
	}

	if (strcmp(name, "double") == 0)
	{
		return BSON_TYPE_DOUBLE;
	}

	if (strcmp(name, "date") == 0)
	{
		return BSON_TYPE_DATE_TIME;
	}

	if (strcmp(name, "timestamp") == 0)
	{
		return BSON_TYPE_TIMESTAMP;
	}

	if (strcmp(name, "objectId") == 0)
	{
		return BSON_TYPE_OID;
	}

	if (strcmp(name, "decimal") == 0)
	{
		return BSON_TYPE_DECIMAL128;
	}

	if (strcmp(name, "string") == 0)
	{
		return BSON_TYPE_UTF8;
	}

	if (strcmp(name, "array") == 0)
	{
		return BSON_TYPE_ARRAY;
	}

	if (strcmp(name, "object") == 0)
	{
		return BSON_TYPE_DOCUMENT;
	}

	if (strcmp(name, "symbol") == 0)
	{
		return BSON_TYPE_SYMBOL;
	}

	if (strcmp(name, "binData") == 0)
	{
		return BSON_TYPE_BINARY;
	}

	if (strcmp(name, "regex") == 0)
	{
		return BSON_TYPE_REGEX;
	}

	if (strcmp(name, "dbPointer") == 0)
	{
		return BSON_TYPE_DBPOINTER;
	}

	if (strcmp(name, "javascript") == 0)
	{
		return BSON_TYPE_CODE;
	}

	if (strcmp(name, "javascriptWithScope") == 0)
	{
		return BSON_TYPE_CODEWSCOPE;
	}

	ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
					errmsg("Unknown type name alias: %s", name)));
}


/*
 * Given an int64 input representing a bson type, it validates it correctly represents one,
 * if it is valid, it returns the corresponding bson type and returns true, otherwise, it
 * returns false.
 */
bool
TryGetTypeFromInt64(int64_t typeCode, bson_type_t *output)
{
	bson_type_t type = (bson_type_t) typeCode;
	if ((type >= BSON_TYPE_EOD && type <= BSON_TYPE_DECIMAL128) ||
		type == BSON_TYPE_MAXKEY || typeCode == -1)
	{
		/* Type codes are continuous from EOD through DECIMAL128, MAXKEY and MINKEY (-1) are not in that range.
		 * We can't use MINKEY literally for the validation, since the underlying type of an enum is int32 and MINKEY = 0xff, that will be 255,
		 * however, in bson it is an int8_t and 0xff is -1, so for literal conversion from int to type, we need to make sure
		 * the value provided is -1 as 255 is not a valid type code. If -1 is provided, we convert it to MINKEY for our internal operations. */

		*output = typeCode == -1 ? BSON_TYPE_MINKEY : type;
		return true;
	}

	/* The typeCode is not valid. */
	return false;
}


/*
 * BsonTypeName returns the name of a bson type to use in error messages
 * and scenarios such as $type.
 */
char *
BsonTypeName(bson_type_t type)
{
	switch (type)
	{
		case BSON_TYPE_NULL:
		{
			return "null";
		}

		case BSON_TYPE_UNDEFINED:
		{
			return "undefined";
		}

		case BSON_TYPE_MINKEY:
		{
			return "minKey";
		}

		case BSON_TYPE_MAXKEY:
		{
			return "maxKey";
		}

		case BSON_TYPE_BOOL:
		{
			return "bool";
		}

		case BSON_TYPE_INT32:
		{
			return "int";
		}

		case BSON_TYPE_INT64:
		{
			return "long";
		}

		case BSON_TYPE_DOUBLE:
		{
			return "double";
		}

		case BSON_TYPE_DATE_TIME:
		{
			return "date";
		}

		case BSON_TYPE_TIMESTAMP:
		{
			return "timestamp";
		}

		case BSON_TYPE_OID:
		{
			return "objectId";
		}

		case BSON_TYPE_DECIMAL128:
		{
			return "decimal";
		}

		case BSON_TYPE_UTF8:
		{
			return "string";
		}

		case BSON_TYPE_ARRAY:
		{
			return "array";
		}

		case BSON_TYPE_DOCUMENT:
		{
			return "object";
		}

		case BSON_TYPE_SYMBOL:
		{
			return "symbol";
		}

		case BSON_TYPE_BINARY:
		{
			return "binData";
		}

		case BSON_TYPE_REGEX:
		{
			return "regex";
		}

		case BSON_TYPE_DBPOINTER:
		{
			return "dbPointer";
		}

		case BSON_TYPE_CODE:
		{
			return "javascript";
		}

		case BSON_TYPE_CODEWSCOPE:
		{
			return "javascriptWithScope";
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("unknown BSON type code %d", type)));
		}
	}
}


/*
 * Top level function that traverses a document given by documentIterator,
 * for a specified traversalPath and applies the various Execution functions
 * depending on the state of the traversal.
 * The execution functions are an extensibility model allowing callers to specify
 * behavior on each level of traversal. The state variable is also passed in to the
 * execution to allow for customization and persistence of state during traversal.
 */
void
TraverseBson(bson_iter_t *documentIterator, const char *traversalPath,
			 void *state, const TraverseBsonExecutionFuncs *executionFuncs)
{
	bool inArrayContext = false;
	StringView traverseView = CreateStringViewFromString(traversalPath);
	TraverseBsonCore(documentIterator, &traverseView, state, executionFuncs,
					 inArrayContext);
}


/*
 * Similar to the above function TraverseBson, only difference is it accepts a
 * const StringView for traversalPath
 */
void
TraverseBsonPathStringView(bson_iter_t *documentIterator,
						   const StringView *traversePathView,
						   void *state,
						   const TraverseBsonExecutionFuncs *executionFuncs)
{
	bool inArrayContext = false;
	TraverseBsonCore(documentIterator, traversePathView, state, executionFuncs,
					 inArrayContext);
}


/*
 * Adds an Int32 to the current value. If the current value is an int64/double
 * applies the sum as an int64/double. If the value overflows, applies
 * the sum as an int64
 * @overflowedFromInt64: Used for AddInt64ToValue(); Set if overflow from Int64 occurs, unset otherwise.
 */
static void
AddInt32ToValue(bson_value_t *current, int32_t value, bool *overflowedFromInt64)
{
	if (current->value_type == BSON_TYPE_DOUBLE)
	{
		/* current value is already double - just do double add. */
		AddDoubleToValue(current, (double) value);
		return;
	}
	else if (current->value_type == BSON_TYPE_INT64)
	{
		/* current value is int64 - push to int64 handling and let it do */
		/* overflow checks. */
		AddInt64ToValue(current, (int64_t) value, overflowedFromInt64);
		return;
	}
	else if (current->value_type == BSON_TYPE_DECIMAL128)
	{
		bson_value_t valueToAdd;
		valueToAdd.value.v_int32 = value;
		valueToAdd.value_type = BSON_TYPE_INT32;

		valueToAdd.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&valueToAdd);
		valueToAdd.value_type = BSON_TYPE_DECIMAL128;
		AddDecimal128Numbers(current, &valueToAdd, current);
		return;
	}

	/* current sum is int32. */
	int64_t currentValue = BsonValueAsInt64(current);
	currentValue += value;

	/* Handle overflow. */
	if (currentValue > INT32_MAX || currentValue < INT32_MIN)
	{
		current->value.v_int64 = currentValue;
		current->value_type = BSON_TYPE_INT64;
	}
	else
	{
		current->value.v_int32 = (int32_t) currentValue;
		current->value_type = BSON_TYPE_INT32;
	}
}


/*
 * Adds an Int64 to the current value. If the current value is an double
 * applies the sum as an double. If the value overflows, applies
 * the sum as a double
 * @overflowedFromInt64: Set if overflow from Int64 occurs, unset otherwise.
 */
static void
AddInt64ToValue(bson_value_t *current, int64_t value, bool *overflowedFromInt64)
{
	if (current->value_type == BSON_TYPE_DOUBLE)
	{
		overflowedFromInt64 = false;

		/* current is already double - just do double add. */
		AddDoubleToValue(current, (double) value);
		return;
	}
	else if (current->value_type == BSON_TYPE_DECIMAL128)
	{
		bson_value_t valueToAdd;
		valueToAdd.value.v_int64 = value;
		valueToAdd.value_type = BSON_TYPE_INT64;

		valueToAdd.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&valueToAdd);
		valueToAdd.value_type = BSON_TYPE_DECIMAL128;
		AddDecimal128Numbers(current, &valueToAdd, current);
		return;
	}

	/* current is int64 or int32. */
	int64_t currentSum = BsonValueAsInt64(current);

	/* check for overflow: */
	/* if current + value > INT64_MAX -> current > INT64_MAX - value */
	/* if current + (-value) < INT64_MIN -> current < INT64_MIN - (-value) */
	if ((value > 0 && currentSum > INT64_MAX - value) ||
		(value < 0 && currentSum < INT64_MIN - value))
	{
		*overflowedFromInt64 = true;

		/* coerce to double. */
		AddDoubleToValue(current, (double) value);
	}
	else
	{
		*overflowedFromInt64 = false;

		current->value.v_int64 = currentSum + value;
		current->value_type = BSON_TYPE_INT64;
	}
}


/*
 * Adds a double to the current value.
 *
 */
static void
AddDoubleToValue(bson_value_t *current, double value)
{
	/* If existing value is decimal128, then needs to convert the value to decimal128 */
	if (current->value_type == BSON_TYPE_DECIMAL128)
	{
		bson_value_t valueBson;
		valueBson.value.v_double = value;
		valueBson.value_type = BSON_TYPE_DOUBLE;

		/* Convert value to decimal128 */
		valueBson.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&valueBson);
		valueBson.value_type = BSON_TYPE_DECIMAL128;

		AddDecimal128Numbers(current, &valueBson, current);
	}
	else
	{
		/* We match native mongo behavior, which doesn't coerce to decimal128
		 * in case of overflow and just returns infinity which is the same
		 * behavior in C in case of double overflow. */
		double currentValue = BsonValueAsDouble(current);
		current->value_type = BSON_TYPE_DOUBLE;
		current->value.v_double = currentValue + value;
	}
}


/**
 *  Adds a decimal128 to the current value.
 */
static void
AddDecimal128ToValue(bson_value_t *current, const bson_value_t *value)
{
	bson_decimal128_t currentValue = GetBsonValueAsDecimal128Quantized(current);
	current->value_type = BSON_TYPE_DECIMAL128;
	current->value.v_decimal128 = currentValue;

	/* All decimal128 flag exceptions are ignored */
	AddDecimal128Numbers(current, value, current);
}


/*
 *  Subtract a decimal128 from the current value.
 */
static void
SubtractDecimal128FromValue(bson_value_t *current, const bson_value_t *value)
{
	bson_decimal128_t currentValue = GetBsonValueAsDecimal128Quantized(current);
	current->value_type = BSON_TYPE_DECIMAL128;
	current->value.v_decimal128 = currentValue;

	/* All decimal128 flag exceptions are ignored */
	SubtractDecimal128Numbers(current, value, current);
}


/*
 * Core traversal logic into a bson document. This walks the documentIterator
 * to look for a given traversePath and applies the extension functions from
 * ExtensionFuncs based on the traverse behavior.
 * This is a purely internal function used by TraverseBson to handle parsing.
 *
 * This function returns true if the path being searched for (or descendant paths)
 * are not found.
 */
static bool
TraverseBsonCore(bson_iter_t *documentIterator, const StringView *traversePath,
				 void *state,
				 const TraverseBsonExecutionFuncs *executionFunctions,
				 bool inArrayContext)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();
	const StringView dotKeyStr = StringViewFindPrefix(traversePath, '.');
	pgbsonelement documentFieldElement;

	/*
	 * When the traversal path is a single field (no dots),
	 * We first call the Visit function against the top level field,
	 * and if it's an array the VisitArrayField as well.
	 *
	 * When the traversePath has composite fields (e.g. "a.b.c"), we recurse into objects/arrays
	 * in the document until we have a single field.
	 */
	if (dotKeyStr.string == NULL)
	{
		/* no dot key - find the field in the current bson. */
		if (!bson_iter_find_string_view(documentIterator, traversePath))
		{
			if (executionFunctions->SetTraverseResult != NULL)
			{
				executionFunctions->SetTraverseResult(state,
													  TraverseBsonResult_PathNotFound);
			}

			return true;
		}

		BsonIterToPgbsonElement(documentIterator, &documentFieldElement);
		bool shouldContinue = executionFunctions->VisitTopLevelField(
			&documentFieldElement, traversePath, state);
		if (!shouldContinue)
		{
			return false;
		}

		/* if the last field is an array, compare the value against the elements in the array as well.
		 * Note that mongo does not traverse arrays of arrays, so if the caller is an array, skip this
		 * recursion.
		 */
		if (BSON_ITER_HOLDS_ARRAY(documentIterator) &&
			!inArrayContext &&
			executionFunctions->VisitArrayField != NULL)
		{
			bson_iter_t nestedIterator;
			bson_iter_recurse(documentIterator, &nestedIterator);
			int arrayIndex = 0;
			while (bson_iter_next(&nestedIterator))
			{
				/* Comparisons only work on the same sort order type. */
				BsonIterToPgbsonElement(&nestedIterator, &documentFieldElement);
				shouldContinue = executionFunctions->VisitArrayField(
					&documentFieldElement, traversePath, arrayIndex, state);
				if (!shouldContinue)
				{
					return false;
				}

				arrayIndex++;
			}
		}

		if (executionFunctions->SetTraverseResult != NULL)
		{
			executionFunctions->SetTraverseResult(state, TraverseBsonResult_TypeMismatch);
		}

		return false;
	}

	if (!bson_iter_find_string_view(documentIterator, &dotKeyStr))
	{
		if (executionFunctions->SetTraverseResult != NULL)
		{
			executionFunctions->SetTraverseResult(state, TraverseBsonResult_PathNotFound);
		}

		return true;
	}

	StringView remainingPath = StringViewSubstring(traversePath, dotKeyStr.length + 1);
	if (BSON_ITER_HOLDS_DOCUMENT(documentIterator))
	{
		bson_iter_t nestedIterator;
		bson_iter_recurse(documentIterator, &nestedIterator);
		return TraverseBsonCore(&nestedIterator, &remainingPath,
								state, executionFunctions,
								false /* inArrayContext */);
	}
	else if (BSON_ITER_HOLDS_ARRAY(documentIterator))
	{
		/* if the field is an array, there's 2 possibilities, it could be an array index so try finding it as is. */
		bson_iter_t nestedIterator;
		bson_iter_recurse(documentIterator, &nestedIterator);
		bool inArrayContextInner = true;
		bool hasPathNotFound = TraverseBsonCore(&nestedIterator,
												&remainingPath, state,
												executionFunctions,
												inArrayContextInner);
		if (!executionFunctions->ContinueProcessIntermediateArray(state, bson_iter_value(
																	  documentIterator)))
		{
			return false;
		}

		/* or it could be a nested object in the array. Reinitialize and scan the array. */
		bson_iter_recurse(documentIterator, &nestedIterator);
		bool arrayElementsHasPathNotFound = false;

		int32_t intermediateIndex = 0;
		while (bson_iter_next(&nestedIterator))
		{
			if (BSON_ITER_HOLDS_DOCUMENT(&nestedIterator))
			{
				bson_iter_t innerNestedIterator;
				bson_iter_recurse(&nestedIterator, &innerNestedIterator);
				inArrayContextInner = false;

				if (executionFunctions->SetIntermediateArrayIndex != NULL)
				{
					executionFunctions->SetIntermediateArrayIndex(state,
																  intermediateIndex);
				}

				bool arrayElementPathNotFound = TraverseBsonCore(&innerNestedIterator,
																 &remainingPath,
																 state,
																 executionFunctions,
																 inArrayContextInner);

				const bson_value_t *nestedValue = bson_iter_value(&nestedIterator);
				if (!executionFunctions->ContinueProcessIntermediateArray(state,
																		  nestedValue))
				{
					return false;
				}

				arrayElementsHasPathNotFound = arrayElementsHasPathNotFound ||
											   arrayElementPathNotFound;
			}

			intermediateIndex++;
		}

		/*
		 * if traversal via the array index (e.g. a.1) returns a mismatch, then we return a mismatch
		 * Alternatively if traversal via nested objects (e.g a.b) also returns a mismatch, we return
		 * a mismatch. If both paths aren't found, then we return path not found
		 */
		if (hasPathNotFound && arrayElementsHasPathNotFound)
		{
			if (executionFunctions->SetTraverseResult != NULL)
			{
				executionFunctions->SetTraverseResult(state,
													  TraverseBsonResult_PathNotFound);
			}

			return true;
		}
	}
	else
	{
		if (executionFunctions->SetTraverseResult != NULL)
		{
			executionFunctions->SetTraverseResult(state, dotKeyStr.string == NULL ?
												  TraverseBsonResult_TypeMismatch :
												  TraverseBsonResult_PathNotFound);
		}

		return false;
	}

	if (executionFunctions->SetTraverseResult != NULL)
	{
		executionFunctions->SetTraverseResult(state, TraverseBsonResult_TypeMismatch);
	}

	return false;
}


/* traverses next and child elements recursivley and returns true if the key is found
 * e.g, document : { a : {b : 1}, {c: 2}, {d : {e: 1}} } , key : 'e'  => returns true because e exist on path "a.d.e".
 */
bool
BsonIterSearchKeyRecursive(bson_iter_t *iter, const char *key)
{
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();

	while (bson_iter_next(iter))
	{
		const char *currentKey = bson_iter_key(iter);

		if (strcmp(currentKey, key) == 0)
		{
			return true;
		}

		if (BSON_ITER_HOLDS_DOCUMENT(iter) || BSON_ITER_HOLDS_ARRAY(iter))
		{
			bson_iter_t nestedIterator;
			if (bson_iter_recurse(iter, &nestedIterator) &&
				BsonIterSearchKeyRecursive(&nestedIterator, key))
			{
				return true;
			}
		}
	}
	return false;
}
