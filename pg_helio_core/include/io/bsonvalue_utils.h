/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bsonvalue_utils.h
 *
 * Core helper function declarations for bsonValues.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSONVALUE_UTILS_H
#define BSONVALUE_UTILS_H

#include "utils/string_view.h"

/* This struct defines conversion from double/decimal128
 * integer conversion rounding modes.
 */
typedef enum ConversionRoundingMode
{
	ConversionRoundingMode_Floor = 0,
	ConversionRoundingMode_NearestEven = 1,
} ConversionRoundingMode;


/* also see inlined methods: BsonValueIsNumber & BsonValueIsNumberOrBool */
bool BsonTypeIsNumber(bson_type_t type);
bool BsonTypeIsNumberOrBool(bson_type_t type);

bool IsBsonValue32BitInteger(const bson_value_t *value, bool checkFixedInteger);
bool IsBsonValue64BitInteger(const bson_value_t *value, bool checkFixedInteger);
bool IsBsonValueFixedInteger(const bson_value_t *value);
bool IsBsonValueNaN(const bson_value_t *value);
int IsBsonValueInfinity(const bson_value_t *value);

int32_t BsonValueAsInt32(const bson_value_t *value);
int32_t BsonValueAsInt32WithRoundingMode(const bson_value_t *value,
										 ConversionRoundingMode roundingMode);
int64_t BsonValueAsInt64(const bson_value_t *value);
int64_t BsonValueAsInt64WithRoundingMode(const bson_value_t *value,
										 ConversionRoundingMode roundingMode,
										 bool throwErrorIfFailed);
bool BsonValueAsBool(const bson_value_t *value);
double BsonValueAsDouble(const bson_value_t *value);
double BsonValueAsDoubleQuiet(const bson_value_t *value);
int64_t BsonValueAsDateTime(const bson_value_t *value);

bool AddNumberToBsonValue(bson_value_t *state, const bson_value_t *number,
						  bool *overflowedFromInt64);
bool SubtractNumberFromBsonValue(bson_value_t *state, const bson_value_t *subtrahend,
								 bool *overflowedFromInt64);
void BitwiseAndToBsonValue(bson_value_t *state, const bson_value_t *number);
void BitwiseOrToBsonValue(bson_value_t *state, const bson_value_t *number);
void BitwiseXorToBsonValue(bson_value_t *state, const bson_value_t *number);
bool MultiplyWithFactorAndUpdate(bson_value_t *state, const bson_value_t *mf,
								 bool convertInt64OverflowToDouble);

void InitBsonValueAsEmptyArray(bson_value_t *outValue);
bool BsonValueHoldsNumberArray(const bson_value_t *value, int32_t *numElements);

bool TryGetTypeFromInt64(int64_t typeCode, bson_type_t *output);
bson_type_t BsonTypeFromName(const char *name);

/* also see inlined method: BsonIterTypeName */
char * BsonTypeName(bson_type_t type);

bool BsonIterSearchKeyRecursive(bson_iter_t *iter, const char *key);
int64 BsonValueHash(const bson_value_t *value, int64 seed);
uint32 BsonValueHashUint32(const bson_value_t *bsonValue);

/* Inlined Utility Methods */

/*
 * BsonIterTypeName returns type name of the bson that given iterator holds.
 */
static inline char *
BsonIterTypeName(const bson_iter_t *iter)
{
	return BsonTypeName(bson_iter_type(iter));
}


static inline bool
BsonValueIsNumber(const bson_value_t *value)
{
	return BsonTypeIsNumber(value->value_type);
}


static inline bool
BsonValueIsNumberOrBool(const bson_value_t *value)
{
	return BsonTypeIsNumberOrBool(value->value_type);
}


/*
 * Validate if the bson is an empty document.
 * Note that the bson spec (https://bsonspec.org/spec.html) implies that a document has
 * 4 bytes for the doc length, followed by a list of [ 1 byte type code, path, value]
 * This implies that a non empty doc has 4 bytes of length, 1 byte of type code
 * (for the first element), and at least 1 byte for the index path which is 6 bytes at least.
 */
inline static bool
IsBsonValueEmptyDocument(const bson_value_t *value)
{
	return (value->value_type == BSON_TYPE_DOCUMENT && value->value.v_doc.data_len < 6);
}


/*
 * Validate if the bson is an empty document.
 * Note that the bson spec (https://bsonspec.org/spec.html) implies that an array has
 * 4 bytes for the array length, followed by a list of [ 1 byte type code, path, value]
 * This implies that a non empty array has 4 bytes of length, 1 byte of type code
 * (for the first element), and at least 1 byte for the index path which is 6 bytes at least.
 */
inline static bool
IsBsonValueEmptyArray(const bson_value_t *value)
{
	return (value->value_type == BSON_TYPE_ARRAY && value->value.v_doc.data_len < 6);
}


/*
 * Extracts a StringView from a bson_iter_t.
 * This is equivalent to constructing a StringView from the 2 separate calls
 * but extracted to an inline function for convenience.
 */
inline static StringView
bson_iter_key_string_view(const bson_iter_t *iter)
{
	StringView s =
	{
		.string = bson_iter_key(iter),
		.length = bson_iter_key_len(iter)
	};
	return s;
}


inline static bool
bson_iter_find_string_view(bson_iter_t *iter, const StringView *view)
{
	return bson_iter_find_w_len(iter, view->string, view->length);
}


#endif
