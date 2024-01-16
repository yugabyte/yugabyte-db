/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/helio_custom_scan.h
 *
 *  Implementation of a custom scan plan for PGMongo.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_CUSTOM_SCAN_H
#define HELIO_CUSTOM_SCAN_H

#include <optimizer/plancat.h>

void UpdatePathsWithExtensionCustomPlans(PlannerInfo *root, RelOptInfo *rel,
										 RangeTblEntry *rte);

void UpdatePathsToForceRumIndexScanToBitmapHeapScan(PlannerInfo *root, RelOptInfo *rel);

Query * ReplaceCursorParamValues(Query *query, ParamListInfo boundParams);

void ValidateCursorCustomScanPlan(Plan *plan);

List * GetBaseRelationExprs(Relation tableRel, Index relIdIndex);
#endif
