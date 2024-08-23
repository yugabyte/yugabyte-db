/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/collation/collation.h
 *
 * Common declarations of functions for handling collation.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_API_COLLATION_H
#define HELIO_API_COLLATION_H


#include "io/helio_bson_core.h"

#define IS_COLLATION_VALID(collationString) (collationString != NULL && strlen( \
												 collationString) > 2)
#define IS_COLLATION_APPLICABLE(collationString) (EnableCollation && IS_COLLATION_VALID( \
													  collationString))

/* This value is calulated assuming all parameters in collation document is specified
 * and each of them are set to the longest possible character values*/
#define MAX_ICU_COLLATION_LENGTH 110

void ParseAndGetCollationString(const bson_value_t *collationValue,
								const char *collationString);

#endif
