/*--------------------------------------------------------------------------------------------------
 *
 * ybcScan.h
 *	  prototypes for ybcScan.c
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/executor/ybcScan.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#ifndef YBCSCAN_H
#define YBCSCAN_H

#include "postgres.h"

#include "utils/resowner.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"

typedef struct YbScanPlanData
{
	/* YugaByte metadata about the referenced table/relation. */
	Bitmapset *primary_key;
	Bitmapset *hash_key;

	/* Bitmap of attribute (column) numbers that we need to fetch from YB. */
	Bitmapset *target_attrs;

	/* (Equality) Conditions on hash key -- filtered by YugaByte */
	List *yb_hconds;

	/* (Equality) Conditions on range key -- filtered by YugaByte */
	List *yb_rconds;

	/* Rest of baserestrictinfo conditions -- filtered by Postgres */
	List *pg_conds;

	/*
	 * The set of columns set by YugaByte conds (i.e. in yb_hconds or yb_rconds
	 * above). Used to check if hash or primary key is fully set.
	 */
	Bitmapset *yb_cols;

} YbScanPlanData;

typedef YbScanPlanData *YbScanPlan;

typedef struct YbScanStateData
{
	/* The postgres description (schema) for the expected tuple (row) */
	TupleDesc tupleDesc;

	/* The handle for the internal YB Select statement. */
	YBCPgStatement handle;
	ResourceOwner stmt_owner;
} YbScanStateData;

typedef YbScanStateData *YbScanState;

void ybcFreeScanState(YbScanState ybc_state);
extern YbScanState ybcBeginScan(Relation rel,
								Relation index,
								List *target_attrs,
								List *yb_conds);
extern HeapTuple ybcFetchNext(YbScanState ybc_state);
extern void ybcEndScan(YbScanState ybc_handle);


#endif							/* YBCSCAN_H */
