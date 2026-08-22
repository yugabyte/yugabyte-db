/*-------------------------------------------------------------------------
 *
 * yb_table_scan_options.h
 *	  YB-specific scan parameters for table_beginscan_*_yb().
 *
 *	  Defines YbTableScanOptions, the struct that carries YB-
 *	  specific parameters (pushdown expressions, row marks,
 *	  parallel state, etc.) through the tableam scan_begin
 *	  callback.  Intentionally lightweight: uses only forward
 *	  declarations so that includers do not pull in AM internals.
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
 * src/include/access/yb_table_scan_options.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "nodes/lockoptions.h"

struct IndexInfo;
struct Scan;
struct YBParallelPartitionKeysData;
struct YbPushdownExprs;
struct YbcPgExecParameters;

/*
 * YB-specific scan options for table_beginscan_*_yb().  These
 * bundle the parameters that YugabyteDB needs on top of the
 * standard scan_begin args (pushdown expressions, aggregate refs,
 * exec parameters, etc.).
 */
typedef struct YbTableScanOptions
{
	struct Scan *pg_scan_plan;	/* plan node for target projection */
	struct YbPushdownExprs *rel_pushdown;	/* WHERE pushdown to DocDB */
	List	   *aggrefs;		/* aggregate pushdown refs, or NIL */
	int			distinct_prefixlen; /* prefix length for DISTINCT */
	struct YbcPgExecParameters *exec_params;	/* per-query exec params */
	bool		is_internal_scan;	/* true for internal catalog reads */
	bool		fetch_ybctids_only; /* only fetch ybctids, not rows */
	int			rowmark;		/* RowMarkType, or YBC_NO_ROW_MARK */
	LockWaitPolicy wait_policy; /* used when rowmark != YBC_NO_ROW_MARK */
	struct YBParallelPartitionKeysData *pscan;	/* parallel scan state */
	struct IndexInfo *index_info;	/* for column-projected index builds */
} YbTableScanOptions;
