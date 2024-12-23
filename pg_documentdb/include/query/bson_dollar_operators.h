/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_dollar_operators.h
 *
 * Common declarations of the bson dollar operators.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_DOLLAR_OPERATOR_H
#define BSON_DOLLAR_OPERATOR_H

#include "types/pcre_regex.h"

/*
 * This enum defines the result of a traverse operation to a document
 * when attempting to compare a document's field against a filter.
 */
typedef enum CompareResult
{
	/*
	 * The field was not found on attempting to traverse the document
	 * for a dotted path.
	 */
	CompareResult_PathNotFound = 0,

	/*
	 * The field had a type or value mismatch, or the parent path
	 * had a value that was incompatible with the dotted path traversal.
	 */
	CompareResult_Mismatch = 1,

	/*
	 * The value was a match based on the comparison criteria selected.
	 */
	CompareResult_Match = 2,
} CompareResult;

/* forward declaration of validation state */
typedef struct TraverseValidateState *TraverseValidateStatePointer;

/* The core comparison function extension for comparisons of Bson operators */
typedef bool (*CompareMatchValueFunc)(const pgbsonelement *element,
									  TraverseValidateStatePointer state, bool
									  isArrayInnerTerm);

/* To store the Regex pattern strings and its options used for matching */
typedef struct RegexData
{
	/* Regular expression pattern string */
	char *regex;

	/* Options that can be used for regex pattern matching */
	char *options;

	/* PCRE context, compiled Regex, matched data etc */
	PcreData *pcreData;
} RegexData;

/*
 * This is state that is used by TraverseBson for Comparison functions
 * by TraverseBsonExecutionFuncs
 * Each intent for Comparison can create sub-structs that contain their own state.
 * The base state is represented here and used by the BsonExecutionFuncs to process
 * comparisons.
 */
typedef struct TraverseValidateState
{
	CompareMatchValueFunc matchFunc;
	CompareResult compareResult;
} TraverseValidateState;

/* The core comparison function extension for compare set bit position array for Bitwise operators */
typedef bool (*CompareArrayForBitwiseOp)(bson_iter_t *sourceArrayIter,
										 bson_iter_t *filterArrayIter,
										 bool isSignExtended);

bool CompareRegexTextMatch(const bson_value_t *docBsonVal, RegexData *regexData);
bool CompareModOperator(const bson_value_t *srcVal, const bson_value_t *modArrVal);
void GetRemainderFromModBsonValues(const bson_value_t *dividendValue, const
								   bson_value_t *divisorValue, bool validateInputs,
								   bson_value_t *result);
void WriteSetBitPositionArray(uint8_t *src, int srcLength, pgbson_writer *writer);
bool CompareBitwiseOperator(const bson_value_t *documentIterator, const
							bson_value_t *filterArray, CompareArrayForBitwiseOp
							bitsCompareFunc);

/* core functions for compare set bit position array for bitwise operators*/
bool CompareArrayForBitsAllClear(bson_iter_t *sourceArrayIter,
								 bson_iter_t *filterArrayIter,
								 bool isSignExtended);
bool CompareArrayForBitsAnyClear(bson_iter_t *sourceArrayIter,
								 bson_iter_t *filterArrayIter,
								 bool isSignExtended);
bool CompareArrayForBitsAllSet(bson_iter_t *sourceArrayIter,
							   bson_iter_t *filterArrayIter,
							   bool isSignExtended);
bool CompareArrayForBitsAnySet(bson_iter_t *sourceArrayIter,
							   bson_iter_t *filterArrayIter,
							   bool isSignExtended);

Datum BsonOrderby(pgbson *leftBson, pgbson *rightBson, bool validateSort);

#endif
