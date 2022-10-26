/*-------------------------------------------------------------------------
 *
 * yb_safe_funcs_for_pushdown.c
 *    List of non-immutable functions that do not perform any accesses to
 *    the database.
 *
 * Copyright (c) YugaByte, Inc.
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
 * IDENTIFICATION
 *    src/backend/utils/misc/yb_safe_funcs_for_pushdown.c
 *
 *-------------------------------------------------------------------------
 */

#include "c.h"
#include "utils/fmgroids.h"

const uint32 yb_funcs_safe_for_pushdown[] = {
	F_RANDOM
};

const int yb_funcs_safe_for_pushdown_count =
	sizeof(yb_funcs_safe_for_pushdown) /
	sizeof(yb_funcs_safe_for_pushdown[0]);
