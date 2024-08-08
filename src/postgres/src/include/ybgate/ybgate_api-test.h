/*-------------------------------------------------------------------------
 *
 * ybgate_api-test.h
 *	  YbGate unit tests.
 *	  YbGate allows to execute Postgres code from DocDB
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
 *	  src/include/ybgate/ybgate_api-test.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "ybgate/ybgate_api.h"
#include "yb/yql/pggate/ybc_pggate.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum YbGateTestCase
{
	YBGATE_TEST_ELOG_LOG,
	YBGATE_TEST_ELOG_NOTICE,
	YBGATE_TEST_ELOG_WARN,
	YBGATE_TEST_ELOG_ERROR,
	YBGATE_TEST_EREPORT_LOG,
	YBGATE_TEST_EREPORT_WARN,
	YBGATE_TEST_EREPORT_ERROR,
	YBGATE_TEST_EREPORT_FATAL,
	YBGATE_TEST_EREPORT_PANIC,
	YBGATE_TEST_EREPORT_ERROR_LOCATION,
	YBGATE_TEST_EREPORT_ERROR_CODE,
	YBGATE_TEST_EREPORT_FORMAT,
	YBGATE_TEST_EREPORT_FORMAT_TOO_LONG_ARG,
	YBGATE_TEST_ELOG_FORMAT_ERRNO,
	YBGATE_TEST_EREPORT_AND_LOG,
	YBGATE_TEST_EREPORT_AND_ERROR,
	YBGATE_TEST_EREPORT_AND_TRY_CATCH,
	YBGATE_TEST_TRY_CATCH,
	YBGATE_TEST_TRY_CATCH_RETHROW,
	YBGATE_TEST_TRY_CATCH_LOG,
	YBGATE_TEST_TRY_CATCH_ERROR,
	YBGATE_TEST_TRY_CATCH_DOUBLE_ERROR,
	YBGATE_TEST_NESTED_TRY_TRY_CATCH,
	YBGATE_TEST_NESTED_TRY_TRY_CATCH_RETHROW,
	YBGATE_TEST_NESTED_CATCH_TRY_CATCH,
	YBGATE_TEST_NESTED_CATCH_TRY_CATCH_RETHROW,
	YBGATE_TEST_EDATA_LOG,
	YBGATE_TEST_EDATA_THROW,
	YBGATE_TEST_INVALID
} YbGateTestCase;


YbgStatus YbgTest(YbGateTestCase case_no);
void YbgTestNoReporting(YbGateTestCase case_no);
bool YbTypeDetailsTest(
	unsigned int elmtype, int16_t *elmlen, bool *elmbyval, char *elmalign);

#ifdef __cplusplus
}
#endif
