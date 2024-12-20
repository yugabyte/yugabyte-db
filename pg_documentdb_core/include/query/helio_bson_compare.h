/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/helio_bson_compare.h
 *
 * Common declarations of the bson comparisons.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_BSON_COMPARE_H
#define HELIO_BSON_COMPARE_H

int ComparePgbson(const pgbson *leftBson, const pgbson *rightBson);
int CompareNullablePgbson(pgbson *leftBson, pgbson *rightBson);
bool BsonValueEquals(const bson_value_t *left, const bson_value_t *right);
bool BsonValueEqualsWithCollation(const bson_value_t *left, const bson_value_t *right,
								  const char *collationString);
bool BsonValueEqualsStrict(const bson_value_t *left, const bson_value_t *right);
bool BsonValueEqualsStrictWithCollation(const bson_value_t *left,
										const bson_value_t *right,
										const char *collationString);
int CompareBsonValueAndType(const bson_value_t *left, const bson_value_t *right,
							bool *isComparisonValid);
int CompareBsonValueAndTypeWithCollation(const bson_value_t *left, const
										 bson_value_t *right,
										 bool *isComparisonValid, const
										 char *collationString);
int CompareBsonSortOrderType(const bson_value_t *left, const bson_value_t *right);
int CompareSortOrderType(bson_type_t left, bson_type_t right);
int CompareStrings(const char *left, uint32_t leftLength, const char *right, uint32_t
				   rightLength, const char *collationString);

#endif
