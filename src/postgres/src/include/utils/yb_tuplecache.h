/*-------------------------------------------------------------------------
 *
 * yb_tuplecache.h
 *	  Declarations for YugabyteDB tuple cache.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/utils/yb_tuplecache.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef YB_TUPLECACHE_H
#define YB_TUPLECACHE_H

#include "postgres.h"

#include "nodes/pg_list.h"
#include "utils/hsearch.h"
#include "utils/rel.h"

/*
 * Group all tuples under the same relation into a list for partial key
 * searches.
 */
typedef struct YbTupleCacheEntry
{
	/* Key must be the first */
	Oid			key;
	List	   *tuples;
} YbTupleCacheEntry;

typedef struct YbTupleCache
{
	Relation	rel;
	HTAB	   *data;
} YbTupleCache;

typedef Oid (*YbTupleCacheKeyExtractor) (HeapTuple);

struct YbTupleCacheIteratorData
{
	List	   *list;
	ListCell   *current;
};

typedef struct YbTupleCacheIteratorData *YbTupleCacheIterator;

void		YbLoadTupleCache(YbTupleCache *cache, Oid relid,
							 YbTupleCacheKeyExtractor key_extractor, const char *cache_name);
void		YbCleanupTupleCache(YbTupleCache *cache);

YbTupleCacheIterator YbTupleCacheIteratorBegin(const YbTupleCache *cache, const void *key_ptr);
HeapTuple	YbTupleCacheIteratorGetNext(YbTupleCacheIterator iter);
void		YbTupleCacheIteratorEnd(YbTupleCacheIterator iter);

#endif							/* YB_TUPLECACHE_H */
