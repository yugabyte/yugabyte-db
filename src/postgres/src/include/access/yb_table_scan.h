/*-------------------------------------------------------------------------
 *
 * yb_table_scan.h
 *	  Public YB table scan interface.
 *
 *	  Opaque accessor functions that let executor nodes interact
 *	  with a YB-backed TableScanDesc without depending on the
 *	  internal scan types defined in yb_scan_core.h.
 *
 *	  To start a scan, callers use the table_beginscan_*_yb()
 *	  variants in tableam.h together with yb_table_scan_options.h.
 *	  This header only covers post-beginscan operations.
 *
 *	  Each accessor here is an interim workaround for a
 *	  YB-specific operation not yet routed through tableam.
 *	  The goal is to eventually replace each one with a proper
 *	  tableam callback or existing API.
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
 * src/include/access/yb_table_scan.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "access/relscan.h"

struct YBParallelPartitionKeysData;

extern struct YBParallelPartitionKeysData *yb_scan_desc_pscan(TableScanDesc scan);
extern bool yb_scan_desc_is_exec_done(TableScanDesc scan);
extern void yb_scan_desc_set_exec_done(TableScanDesc scan, bool done);
extern void yb_scan_desc_exec_select(TableScanDesc scan);
extern void yb_scan_desc_fetch_next(TableScanDesc scan,
									TupleTableSlot *slot, Oid relid);
extern bool yb_scan_desc_apply_next_parallel_range(TableScanDesc scan);
extern int	yb_scan_desc_get_fetch_row_limit(TableScanDesc scan);
extern int	yb_scan_desc_get_fetch_size_limit(TableScanDesc scan);
extern void yb_scan_desc_fetch_ybctids(TableScanDesc scan,
									   YbcConstSliceVector ybctids);
extern void yb_scan_desc_apply_primary_pushdown(TableScanDesc scan,
												const YbPushdownExprs *pushdown);
extern void yb_scan_desc_bind_ybctids(TableScanDesc scan,
									  int nybctids, Datum *ybctids);
