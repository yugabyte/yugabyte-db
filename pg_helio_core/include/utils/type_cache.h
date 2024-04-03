/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * utils/type_cache.h
 *
 * Common declarations for metadata caching functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef TYPE_CACHE_H
#define TYPE_CACHE_H

extern PGDLLIMPORT char *CoreSchemaName;

/* types */
PGDLLEXPORT Oid BsonTypeId(void);
PGDLLEXPORT Oid BsonQueryTypeId(void);
PGDLLEXPORT Oid HelioCoreBsonTypeId(void);
PGDLLEXPORT Oid HelioCoreBsonSequenceTypeId(void);

#endif
