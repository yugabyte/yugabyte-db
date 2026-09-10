/*-------------------------------------------------------------------------
 *
 * yb_cost.h
 *	  YB cost estimation.
 *
 *	  Selectivity constants and cost functions used by the optimizer
 *	  to estimate costs.
 *
 *	  Used by optimizer paths (indxpath, allpaths, pathnode,
 *	  plancat), the amcostestimate implementations (yb_lsm, ybgin),
 *	  and analyze.c.
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
 * src/include/access/yb_cost.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "nodes/pathnodes.h"
#include "utils/relcache.h"

/* Number of rows assumed for a YB table if no size estimates exist */
#define YBC_DEFAULT_NUM_ROWS  1000

#define YBC_SINGLE_ROW_SELECTIVITY	(1.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_SINGLE_KEY_SELECTIVITY	(10.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_HASH_SCAN_SELECTIVITY	(100.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_FULL_SCAN_SELECTIVITY	1.0

/*
 * For a partial index the index predicate will filter away some rows.
 * TODO: Evaluate this based on the predicate itself and table stats.
 */
#define YBC_PARTIAL_IDX_PRED_SELECTIVITY 0.8

/*
 * Backwards scans are more expensive in DocDB.
 */
#define YBC_BACKWARDS_SCAN_COST_FACTOR 1.1

/*
 * Uncovered indexes will require extra RPCs to the main table to retrieve the
 * values for all required columns. These requests are now batched in PgGate
 * so the extra cost should be relatively low in general.
 */
#define YBC_UNCOVERED_INDEX_COST_FACTOR 1.1

extern void ybcGetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel,
								 Oid foreigntableid);
extern void ybcCostEstimate(RelOptInfo *baserel, Selectivity selectivity,
							bool is_backwards_scan, bool is_seq_scan,
							bool is_uncovered_idx_scan, Cost *startup_cost,
							Cost *total_cost, Oid index_tablespace_oid);
extern void ybcIndexCostEstimate(struct PlannerInfo *root, IndexPath *path,
								 Selectivity *selectivity, Cost *startup_cost,
								 Cost *total_cost);

extern PGDLLIMPORT int yb_parallel_range_rows;

extern int	ybParallelWorkers(double numrows);

extern bool YbIsScanningEmbeddedIdx(Relation table, Relation index);
