/*--------------------------------------------------------------------------------------------------
 *
 * yb_inheritscache.h
 *    Declaration of the convenience methods to access the pg_inherits catcache.
 *
 * Copyright (c) YugaByte, Inc.
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
 * src/include/utils/yb_inheritscache.h
 *
 *--------------------------------------------------------------------------------------------------
 */
#ifndef YBPGINHERITSCACHE_H
#define YBPGINHERITSCACHE_H

#include "postgres.h"
#include "access/htup.h"
#include "nodes/pg_list.h"

typedef struct YbPgInheritsCacheEntryData
{
	Oid	parentOid;
	List *childTuples;
	int refcount;
} YbPgInheritsCacheEntryData;

typedef YbPgInheritsCacheEntryData *YbPgInheritsCacheEntry;

typedef struct YbPgInheritsCacheChildEntryData
{
	Oid childrelid;
	HeapTuple childTuple;
	YbPgInheritsCacheEntry cacheEntry;
} YbPgInheritsCacheChildEntryData;

typedef YbPgInheritsCacheChildEntryData *YbPgInheritsCacheChildEntry;

extern void YbInitPgInheritsCache();

extern void YbPreloadPgInheritsCache();

extern YbPgInheritsCacheEntry GetYbPgInheritsCacheEntry(Oid parentOid);

extern YbPgInheritsCacheChildEntry GetYbPgInheritsChildCacheEntry(Oid relid);

extern void ReleaseYbPgInheritsCacheEntry(YbPgInheritsCacheEntry entry);

extern void ReleaseYbPgInheritsChildEntry(YbPgInheritsCacheChildEntry entry);

extern void YbPgInheritsCacheInvalidate(Oid relid);

#endif