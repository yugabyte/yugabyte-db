/*-------------------------------------------------------------------------
 *
 * ybgate_status.h
 *	  YbGate interface functions.
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
 *	  src/include/ybgate/ybgate_status.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <setjmp.h>

#include "yb/yql/pggate/ybc_pggate.h"

#ifdef __cplusplus
extern "C" {
#endif

//-----------------------------------------------------------------------------
// Error Reporting
//-----------------------------------------------------------------------------
// !!! IMPORTANT: All functions invoking Postgres code MUST:
// - return YbgStatus;
// - call PG_SETUP_ERROR_REPORTING() before any Postgres code is called. It is
//   a good idea to put it as the first line of the function;
// - use PG_STATUS_OK() macro to exit from the function, put it at the and, and
//   wherever in the middle where shortcut return is needed.
//
// Postgres backends and auxillary processes set up error handling
// infrastructure at the very top level, and keep it forever, so elog / ereport,
// as well as PG_TRY / PG_CATCH work as expected.
// However, inside of the master / tserver processes this infrastructure is not
// available, and that may make respective process to crash.
// The PG_SETUP_ERROR_REPORTING() sets up equivalent infrastructure in
// multithreaded mode. Whether Postgres code throws an error, it is caught and
// wrapped into a YbgStatus, which is subsequently returned to the Postgres
// client.
//-----------------------------------------------------------------------------

typedef struct YbgStatusData *YbgStatus;

/*
 * Macros to handle Postgres errors by YbGate.
 *
 * To be used in a function returning YbgStatus to surround calls to Postgres
 * code:
 *
 *  PG_SETUP_ERROR_REPORTING();
 *  ... PG calls go here ...
 *  PG_STATUS_OK();
 *
 * The PG_SETUP_ERROR_REPORTING sets up a sigjmp buffer, similar to those set
 * up by main routines of Postgres' processes. So if subsequent Postgres code
 * executes any elog or ereport statement with elevel ERROR or higher, it jumps
 * into the "if (r != 0)" block. By that time it is expected that thread local
 * YbgStatus is set, so it is returned to the YbGate function's caller where it
 * is typically converted to a regular Status. If the thread local YbgStatus is
 * not set, that's a bug and we have a static YbgStatus instance to return in
 * this case.
 *
 * The PG_STATUS_OK removes the sigjmp buffer and returns the current thread
 * local status, which is normally NULL here.
 *
 * Do not nest PG_SETUP_ERROR_REPORTING / PG_STATUS_OK blocks, as the inner
 * PG_STATUS_OK would end the protected block, and the rest of the code until
 * the outer PG_STATUS_OK would run unprotected.
 *
 * Running code without setting up error reporting is allowed, if code does not
 * emit Postgres errors. It is OK if error is handled in a PG_CATCH block, but
 * any uncaught error would crash the tserver.
 */
#define PG_SETUP_ERROR_REPORTING() \
	do { \
		sigjmp_buf ybgate_sigjmp_buffer; \
		int r = sigsetjmp(ybgate_sigjmp_buffer, 0); \
		if (r == 0) \
		{ \
			YBCPgSetThreadLocalJumpBuffer(&ybgate_sigjmp_buffer)

#define PG_STATUS_OK() \
		} \
		else \
		{ \
			YbgStatus status = (YbgStatus) YBCPgSetThreadLocalErrStatus(NULL); \
			YBCPgSetThreadLocalJumpBuffer(NULL); \
			return status ? status : YbgStatusErrorReportingError(); \
		} \
		YBCPgSetThreadLocalJumpBuffer(NULL); \
		return (YbgStatus) YBCPgSetThreadLocalErrStatus(NULL); \
	} while(0)

YbgStatus YbgStatusErrorReportingError();
YbgStatus YbgStatusCreate();
YbgStatus YbgStatusCreateError(const char *msg, const char *filename, int line);
void YbgStatusDestroy(YbgStatus status);
void *YbgStatusGetContext(YbgStatus status);
void YbgStatusSetLevel(YbgStatus status, int32_t elevel);
bool YbgStatusIsError(YbgStatus status);
void YbgStatusSetMessageAndArgs(YbgStatus status, const char *msg,
						   int32_t nargs, const char **args);
const char *YbgStatusGetMessage(YbgStatus status);
const char **YbgStatusGetMessageArgs(YbgStatus status, int32_t *nargs);
uint32_t YbgStatusGetSqlError(YbgStatus status);
void YbgStatusSetSqlError(YbgStatus status, uint32_t sqlerror);
const char *YbgStatusGetFilename(YbgStatus status);
void YbgStatusSetFilename(YbgStatus status, const char *filename);
int YbgStatusGetLineNumber(YbgStatus status);
void YbgStatusSetLineNumber(YbgStatus status, int lineno);
const char *YbgStatusGetFuncname(YbgStatus status);
void YbgStatusSetFuncname(YbgStatus status, const char *funcname);
void YbgStatusSetEdata(YbgStatus status, void *edata);
void *YbgStatusGetEdata(YbgStatus status);

#ifdef __cplusplus
}
#endif
