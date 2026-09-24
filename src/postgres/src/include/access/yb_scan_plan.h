/*-------------------------------------------------------------------------
 *
 * yb_scan_plan.h
 *	  Plan-time YB scan analysis types.
 *
 *	  Holds the type that tracks which key columns of a scan have
 *	  bound conditions.  It is shared between scan-key setup
 *	  (yb_scan_core.c) and index cost estimation (yb_cost.c), which
 *	  predicts key binding without seeing scan execution state.
 *	  Plan-time analysis only: no execution state or pggate calls
 *	  belong here.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/include/access/yb_scan_plan.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "access/tupdesc.h"
#include "nodes/bitmapset.h"
#include "utils/relcache.h"

#define YB_MAX_SCAN_KEYS (INDEX_MAX_KEYS * 2)	/* A pair of lower/upper
												 * bounds per column max */

/*
 * Scan plan used during cost estimation and scan key setup.
 * Tracks which primary/hash key columns have search conditions.
 */
typedef struct YbScanPlanData
{
	/* The relation where to read data from */
	Relation	target_relation;

	/*
	 * Key columns of the bind relation (i.e. the relation where cols are
	 * bound).  For a sequential scan or primary index scan, this is the base
	 * table's primary key columns.  For a secondary index [only] scan, this is
	 * the secondary index's key columns.
	 * - key_cols: all of them
	 * - hash_key_cols: the subset that are HASH columns
	 * - qualified_scan_key_cols: the subset qualified from scan keys
	 */
	Bitmapset  *key_cols;
	Bitmapset  *hash_key_cols;
	Bitmapset  *qualified_scan_key_cols;

	/* Description and attnums of the columns to bind */
	TupleDesc	bind_desc;
	AttrNumber	bind_key_attnums[YB_MAX_SCAN_KEYS];
} YbScanPlanData;

typedef YbScanPlanData *YbScanPlan;
