/*-------------------------------------------------------------------------
 *
 * yb_saop_merge.h
 *	  Utilities for SAOP merge
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/optimizer/yb_saop_merge.h
 *
 *-------------------------------------------------------------------------
 */
#pragma once

#include "postgres.h"

#include "access/attnum.h"
#include "nodes/pathnodes.h"
#include "nodes/pg_list.h"
#include "nodes/primnodes.h"

/* GUC options */
extern PGDLLIMPORT bool yb_enable_derived_saops;
extern PGDLLIMPORT int yb_max_saop_merge_streams;

extern bool yb_indexcol_can_saop_merge(PlannerInfo *root,
									   IndexOptInfo *index,
									   Expr *expr,
									   int indexcol,
									   int *saop_merge_cardinality,
									   List **saop_merge_saop_cols);

extern void yb_get_sort_info_from_pathkeys(List *tlist,
										   List *pathkeys,
										   Relids relids,
										   Bitmapset *saop_col_idxs,
										   int *p_numsortkeys,
										   AttrNumber **p_sortColIdx,
										   Oid **p_sortOperators,
										   Oid **p_collations,
										   bool **p_nullsFirst);
