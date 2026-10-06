/*-------------------------------------------------------------------------
 *
 * yb_analyze.h
 *	  ANALYZE width skipping: width caps and stand-ins.
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
 * src/include/commands/yb_analyze.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "access/detoast.h"
#include "commands/vacuum.h"

/*
 * Each analyzed column has a width cap (VacAttrStats.yb_width_cap).  The
 * sampler keeps a value wider than its cap, as toast_raw_datum_size()
 * measures it, only as a stand-in: an on-disk TOAST pointer that carries the
 * value's real size but names no TOAST table (va_toastrelid is InvalidOid).
 * So a compute_stats routine may read the size of a value wider than its
 * column's cap, but never its contents; detoasting a stand-in fails.
 */
#define YB_ANALYZE_WIDTH_SKIP_ALL	0			/* the payload is never read */
#define YB_ANALYZE_WIDTH_SKIP_NONE	PG_INT32_MAX	/* the payload is always read */

/* Whether a value is a stand-in: a real TOAST pointer always names a table. */
static inline bool
YbIsAnalyzeStandin(Pointer ptr)
{
	varatt_external toast_pointer;

	if (!VARATT_IS_EXTERNAL_ONDISK(ptr))
		return false;
	VARATT_EXTERNAL_GET_POINTER(toast_pointer, ptr);
	return toast_pointer.va_toastrelid == InvalidOid;
}

/* The array statistics routine and its cap, for yb_set_width_cap() */
extern PGDLLIMPORT AnalyzeAttrComputeStatsFunc const yb_compute_array_stats;
extern PGDLLIMPORT const int yb_array_width_cap;

/* GUC: whether ANALYZE may skip sampled values (see above) */
extern PGDLLIMPORT bool yb_enable_analyze_width_skip;
