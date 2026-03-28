/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/customscan/bson_custom_scan.h
 *
 *  Implementation of a custom scan plan.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_CUSTOM_SCAN_H
#define BSON_CUSTOM_SCAN_H

#include <optimizer/plancat.h>

struct ReplaceExtensionFunctionContext;
bool UpdatePathsWithExtensionStreamingCursorPlans(PlannerInfo *root, RelOptInfo *rel,
												  RangeTblEntry *rte, struct
												  ReplaceExtensionFunctionContext *context);

void UpdatePathsToForceRumIndexScanToBitmapHeapScan(PlannerInfo *root, RelOptInfo *rel);

Query * ReplaceCursorParamValues(Query *query, ParamListInfo boundParams);

void ValidateCursorCustomScanPlan(Plan *plan);

PathTarget * BuildBaseRelPathTarget(Relation tableRel, Index relIdIndex);
#endif
