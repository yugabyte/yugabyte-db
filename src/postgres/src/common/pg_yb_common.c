/*-------------------------------------------------------------------------
 *
 * pg_yb_common.c
 *	  Common utilities for YugaByte/PostgreSQL integration that are reused
 *	  between PostgreSQL server code and other PostgreSQL programs such as
 *    initdb.
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
 *	  src/common/pg_yb_common.cc
 *
 *-------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "common/pg_yb_common.h"

bool
YBCIsEnvVarTrue(const char* env_var_name)
{
	const char* env_var_value = getenv(env_var_name);
	return env_var_value != NULL && strcmp(env_var_value, "1") == 0;
}

bool
YBIsEnabledInPostgresEnvVar() {
	static int cached_value = -1;
	if (cached_value == -1)
    {
		cached_value = YBCIsEnvVarTrue("YB_ENABLED_IN_POSTGRES");
	}
	return cached_value;
}

bool
YBShouldAllowRunningAsAnyUser() {
	if (YBIsEnabledInPostgresEnvVar())
    {
		return true;
	}
	static int cached_value = -1;
	if (cached_value == -1)
    {
		cached_value = YBCIsEnvVarTrue("YB_PG_ALLOW_RUNNING_AS_ANY_USER");
	}
	return cached_value;
}
