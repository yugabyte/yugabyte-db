/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/query/bson_dollar_selectivity.h
 *
 * Exports for Query Selectivity for DocumentDB boolean index operators/functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_DOLLAR_SELECTIVITY_H
#define BSON_DOLLAR_SELECTIVITY_H

#include <postgres.h>
#include <optimizer/optimizer.h>

/* The low selectivity - based on prior guess. */
static const double LowSelectivity = 0.01;

/* Selectivity when most of the table is accessed (Selectivity max is 1) */
static const double HighSelectivity = 0.9;

double GetDollarOperatorSelectivity(PlannerInfo *planner, Oid selectivityOpExpr,
									List *args, Oid collation, int varRelId, double
									defaultExprSelectivity);

#endif
