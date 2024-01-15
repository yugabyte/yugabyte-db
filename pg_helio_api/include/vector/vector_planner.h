/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/vector/vector_planner.h
 *
 * Function declarations for vector index used in planner and custom scan.
 *
 *-------------------------------------------------------------------------
 */
#ifndef VECTOR_PLANNER__H
#define VECTOR_PLANNER__H

#include <nodes/pathnodes.h>
#include <nodes/pg_list.h>

#include "io/bson_core.h"
#include "vector/vector_utilities.h"


void CalculateDefaultNumProbesAndSearch(IndexPath *vectorSearchPath, double
										indexRows, int *defaultNumProbes,
										int *defaultEfSearch);

void TryParseUserFilterClause(RelOptInfo *rel, List **userFilters);

void SetSearchParametersToGUC(pgbson *searchParamBson);

void TrySetDefaultSearchParamForCustomScan(SearchQueryEvalData *querySearchData);

#endif
