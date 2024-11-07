/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/index_am/helio_rum.h
 *
 * Common declarations for RUM specific helper functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef HELIO_RUM_H
#define HELIO_RUM_H

#include <fmgr.h>
#include <access/amapi.h>
#include <nodes/pathnodes.h>

IndexAmRoutine *GetRumIndexHandler(PG_FUNCTION_ARGS);
void extension_rumcostestimate(PlannerInfo *root, IndexPath *path, double
							   loop_count,
							   Cost *indexStartupCost, Cost *indexTotalCost,
							   Selectivity *indexSelectivity,
							   double *indexCorrelation,
							   double *indexPages);

#endif
