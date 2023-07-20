/*--------------------------------------------------------------------------
 *
 * yb_like_support.h
 *	  External like support functions.
 *
 * Copyright (c) Yugabyte, Inc.
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
 *			src/include/utils/yb_like_support.h
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "fmgr.h"
#include "nodes/primnodes.h"
#include "postgres_ext.h"

/*
 * Postgres 11 -> 15 changed make_greater_string to static, so reexpose as
 * yb_make_greater_string.  If, in the future, postgres decides to expose
 * make_greater_string via a like_support.h, this can be replaced by that.
 */
Const *yb_make_greater_string(const Const *str_const, FmgrInfo *ltproc,
							  Oid collation);
