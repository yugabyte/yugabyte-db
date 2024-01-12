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

extern char *CoreSchemaName;

/* types */
Oid BsonTypeId(void);
Oid BsonQueryTypeId(void);

#endif
