/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/bson_gin_index_types_core.h
 *
 * Common declarations of the serialization of index terms.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_GIN_INDEX_TYPES_CORE_H
#define BSON_GIN_INDEX_TYPES_CORE_H

bson_value_t GetUpperBound(bson_type_t type, bool *isUpperBoundInclusive);
bson_value_t GetLowerBound(bson_type_t type);

bson_type_t GetBsonTypeNameFromStringForDollarType(const char *typeNameStr);

#endif
