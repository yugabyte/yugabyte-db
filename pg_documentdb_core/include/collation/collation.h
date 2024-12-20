/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/collation/collation.h
 *
 * Common declarations of functions for handling collation.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_CORE_COLLATION_H
#define HELIO_CORE_COLLATION_H

#include "io/helio_bson_core.h"

/* This value is calulated assuming all parameters in collation document is specified
 * and each of them are set to the longest possible character values*/
#define MAX_ICU_COLLATION_LENGTH 110

extern bool EnableCollation;

void ParseAndGetCollationString(const bson_value_t *collationValue,
								const char *collationString);
char * GetCollationSortKey(const char *collationString, char *key, int keyLength);

int StringCompareWithCollation(const char *left, uint32_t leftLength,
							   const char *right, uint32_t rightLength, const
							   char *collationStr);

static inline bool
IsCollationValid(const char *collationString)
{
	return collationString != NULL && strlen(collationString) > 2;
}


static inline bool
IsCollationApplicable(const char *collationString)
{
	return EnableCollation && IsCollationValid(collationString);
}


#endif
