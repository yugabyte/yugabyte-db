/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/helio_custom_query_scan.h
 *
 *  Implementation of a custom scan plan for PGMongo.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_CUSTOM_QUERY_SCAN_H
#define HELIO_CUSTOM_QUERY_SCAN_H

#include <optimizer/plancat.h>
#include <utils/builtins.h>
#include <utils/varlena.h>
#include <opclass/helio_index_support.h>

void AddExtensionQueryScanForTextQuery(PlannerInfo *root, RelOptInfo *rel,
									   RangeTblEntry *rte,
									   QueryTextIndexData *textIndexOptions);

void AddExtensionQueryScanForVectorQuery(PlannerInfo *root, RelOptInfo *rel,
										 RangeTblEntry *rte,
										 const SearchQueryEvalData *searchQueryData);

#endif
