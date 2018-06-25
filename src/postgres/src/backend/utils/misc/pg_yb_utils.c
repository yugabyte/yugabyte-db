/*-------------------------------------------------------------------------
 *
 * pg_yb_utils.c
 *	  Utilities for YugaByte/PostgreSQL integration that have to be defined on
 *	  the PostgreSQL side.
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
 *	  src/backend/utils/misc/pg_yb_utils.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "pg_yb_utils.h"

void HandleYBStatus(YBCStatus status) {
	if (!status)
		return;
	/* TODO: consider creating PostgreSQL error codes for YB statuses. */
	ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg(status->msg)));
}
