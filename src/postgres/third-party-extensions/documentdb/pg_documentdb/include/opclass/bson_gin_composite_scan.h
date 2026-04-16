/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/opclass/bson_gin_index_mgmt.h
 *
 * Common declarations of the bson index management methods.
 *
 *-------------------------------------------------------------------------
 */

 #ifndef BSON_GIN_COMPOSITE_SCAN_H
 #define BSON_GIN_COMPOSITE_SCAN_H

 #include <access/skey.h>

struct IndexPath;
bool GetEqualityRangePredicatesForIndexPath(struct IndexPath *indexPath, void *options,
											bool equalityPrefixes[INDEX_MAX_KEYS], bool
											nonEqualityPrefixes[INDEX_MAX_KEYS]);
bool CompositePathHasFirstColumnSpecified(IndexPath *indexPath);
char *SerializeBoundsStringForExplain(bytea * entry, void *extraData, PG_FUNCTION_ARGS);

Datum FormCompositeDatumFromQuals(List *indexQuals, List *indexOrderBy, bool isMultiKey,
								  bool hasCorrelatedReducedTerm);
char * SerializeCompositeIndexKeyForExplain(bytea *entry);
bool ModifyScanKeysForCompositeScan(ScanKey scankey, int nscankeys, ScanKey
									targetScanKey, bool hasArrayKeys, bool
									hasCorrelatedReducedTerms,
									bool hasOrderBys, ScanDirection scanDirection);
ScanDirection DetermineCompositeScanDirection(bytea *compositeScanOptions,
											  ScanKey orderbys, int norderbys);
 #endif
