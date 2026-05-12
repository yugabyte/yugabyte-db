/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/index_am/index_am_utils.h
 *
 * Common declarations for RUM specific helper functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef INDEX_AM_UTILS_H
#define INDEX_AM_UTILS_H

#include <postgres.h>
#include <utils/rel.h>
#include "metadata/metadata_cache.h"
#include "utils/version_utils.h"
#include "index_am/index_am_exports.h"

#define MAX_ALTERNATE_INDEX_AMS 5


int SetDynamicIndexAmOidsAndGetCount(Datum *indexAmArray, int32_t indexAmArraySize);

/*
 * Gets an index AM entry by name.
 */
const BsonIndexAmEntry * GetBsonIndexAmByIndexAmName(const char *index_am_name);

/*
 * Is the Index Acess Method used for indexing bson (as opposed to indexing TEXT, Vector, Points etc)
 * as indicated by enum MongoIndexKind_Regular.
 */
bool IsBsonRegularIndexAm(Oid indexAm);

bool BsonIndexAmRequiresRangeOptimization(Oid indexAm, Oid opFamilyOid);

/*
 * Whether the index relation was created via a composite index opclass
 */
bool IsCompositeOpClass(Relation indexRelation);

bool IsCompositeOpFamilyOid(Oid relam, Oid opFamilyOid);
bool IsCompositeOpFamilyOidWithParallelSupport(Oid relam, Oid opFamilyOid);

/*
 * Whether the Oid of the oprator family points to a single path operator family.
 */
bool IsSinglePathOpFamilyOid(Oid relam, Oid opFamilyOid);

/*
 * Whether the Oid of the oprator family points to a text path operator family.
 */
bool IsTextPathOpFamilyOid(Oid relam, Oid opFamilyOid);
Oid GetTextPathOpFamilyOid(Oid relam);

bool IsUniqueCheckOpFamilyOid(Oid relam, Oid opFamilyOid);
bool IsHashedPathOpFamilyOid(Oid relam, Oid opFamilyOid);

bool IsOrderBySupportedOnOpClass(Oid indexAm, Oid IndexPathOpFamilyAm);

GetMultikeyStatusFunc GetMultiKeyStatusByRelAm(Oid relam);
bool GetIndexSupportsBackwardsScan(Oid relam, bool *indexCanOrder);

bool GetIndexAmSupportsIndexOnlyScan(Oid indexAm, Oid opFamilyOid,
									 GetMultikeyStatusFunc *getMultiKeyStatus,
									 GetTruncationStatusFunc *getTruncationStatus);

void TryExplainByIndexAm(struct IndexScanDescData *scan, struct ExplainState *es);

#endif
