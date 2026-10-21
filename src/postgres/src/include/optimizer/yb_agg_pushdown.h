/*-------------------------------------------------------------------------
 *
 * yb_agg_pushdown.h
 *	  Planner-side eligibility of aggregate pushdown to DocDB
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
 * src/include/optimizer/yb_agg_pushdown.h
 *
 *-------------------------------------------------------------------------
 */
#pragma once

#include "postgres.h"

#include "nodes/pathnodes.h"

extern bool yb_agg_pushdown_eligible(PlannerInfo *root, List *aggrefs,
									 Path *scan, PathTarget *input_target);
extern List *yb_agg_pushdown_aggrefs(PlannerInfo *root);
extern List *yb_aggrefs_below_subqueryscan(PlannerInfo *root, RelOptInfo *rel,
										   List *aggrefs);
extern List *yb_aggrefs_below_appendrel(PlannerInfo *root, RelOptInfo *rel,
										List *aggrefs);
extern bool yb_aggref_function_pushable(Aggref *aggref);
extern bool yb_aggref_arg_type_pushable(Aggref *aggref, Oid argtype);
