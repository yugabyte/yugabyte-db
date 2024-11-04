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

#include "io/helio_bson_core.h"

uint64 HashCombineUint32AsUint64(uint64 left, uint64 right);
uint64 HashBytesUint32AsUint64(const uint8_t *bytes, uint32_t bytesLength, int64 seed);
uint64 BsonHashCompare(bson_iter_t *bsonIterValue,
					   uint64 (*hash_bytes_func)(const uint8_t *bytes, uint32_t
												 bytesLength, int64 seed),
					   uint64 (*hash_combine_func)(uint64 left, uint64 right),
					   int64 seed);
#endif
