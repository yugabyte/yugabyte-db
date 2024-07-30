/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/helio_custom_scan.h
 *
 *  Implementation of a custom scan plan.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_CUSTOM_SCAN_H
#define HELIO_CUSTOM_SCAN_H

#include <optimizer/plancat.h>

bool UpdatePathsWithExtensionCustomPlans(PlannerInfo *root, RelOptInfo *rel,
										 RangeTblEntry *rte);

void UpdatePathsToForceRumIndexScanToBitmapHeapScan(PlannerInfo *root, RelOptInfo *rel);

Query * ReplaceCursorParamValues(Query *query, ParamListInfo boundParams);

void ValidateCursorCustomScanPlan(Plan *plan);

void UpdatePathsWithOptimizedExtensionCustomPlans(PlannerInfo *root, RelOptInfo *rel,
												  RangeTblEntry *rte);

Path * CreateRumJoinScanPathForBitmapAnd(PlannerInfo *root, RelOptInfo *rel,
										 RangeTblEntry *rte, BitmapHeapPath *heapPath);


bool IsRumJoinScanPath(Path *path);

PathTarget * BuildBaseRelPathTarget(Relation tableRel, Index relIdIndex);
#endif
