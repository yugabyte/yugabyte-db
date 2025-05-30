/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bsonvalue_hash.h
 *
 * Declaration of hashing of the BSON type.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_HASH_H
#define BSON_HASH_H

#include "io/bson_core.h"

uint64 HashBsonComparableExtended(bson_iter_t *bsonIterValue, int64 seed);
uint32_t HashBsonComparable(bson_iter_t *bsonIterValue, uint32_t seed);

uint64 HashBsonValueComparableExtended(const bson_value_t *bsonIterValue, int64 seed);
uint32_t HashBsonValueComparable(const bson_value_t *bsonIterValue, uint32_t seed);

#endif
