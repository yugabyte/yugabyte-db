/*-------------------------------------------------------------------------
 *
 * ybgate_api.c
 *	  YbGate interface functions, error status manipulation.
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
 *	  src/backend/ybgate/ybgate_status.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "utils/errcodes.h"
#include "utils/memutils.h"
#include "ybgate/ybgate_api.h"
#include "ybgate/ybgate_status.h"
#include "yb/yql/pggate/ybc_pggate.h"

/*
 * YbgStatusData structure designed to catch details of an error, generated
 * by Postgres' elog/ereport and conveniently return from the YbGate functions.
 *
 * Postgres' elog/ereport macros are implemented to call a number of functions,
 * some of them are mandatory, some are optional, and each if them sets certain
 * attributes in global ErrorData structure. In Yugabyte's multithreaded mode
 * globals are not acceptable, and in general ErrorData is too complex and too
 * Postgres specific, so Yugabyte introduces private YbgErrorData which
 * is shared between multiple ereport's functions call via edata field. Other
 * fields in YbgStatusData structure are to expose the summary to DocDB.
 */
typedef struct YbgStatusData {
	int32_t err_level;		/* error code (from elog.h), 0 means no error */
	const char *err_msg;	/* template to format error message
							   NULL if no error, else may still be NULL */
	int32_t nargs;
	const char **err_args;	/* arguments to format error message */
	const char *filename;	/* error file name */
	int lineno;				/* error line number */
	const char *funcname;	/* error function name */
	uint32_t sqlerror;		/* SQL error code */
	void *edata;			/* detailed error data, see elog.c */
	YbgMemoryContext error_ctx;	/* memory context for YbgStatusData and other
								   dynamically allocated data */
} YbgStatusData;

static struct YbgStatusData YbgFailedErrorReporting = {
	FATAL, /* err_level */
	"PG error reporting went wrong", /* err_msg */
	0, /* nargs */
	NULL, /* err_args */
	NULL, /* filename */
	0, /* lineno */
	NULL, /* funcname */
	ERRCODE_INTERNAL_ERROR, /* SQL error code */
	NULL, /* edata */
	NULL /* error_ctx */
};

/*
 * YbgStatusErrorReportingError
 *
 * Return preformatted YbgStatus instance indicating that YbGate error handling
 * has not been correctly set up.
 */
inline YbgStatus
YbgStatusErrorReportingError()
{
	return &YbgFailedErrorReporting;
}

/*
 * YbgStatusCreate - create new instance of YbgStatus
 */
YbgStatus
YbgStatusCreate()
{
	YbgMemoryContext error_ctx;
	YbgMemoryContext old_ctx;
	YbgStatus		status;

	error_ctx = CreateThreadLocalCurrentMemoryContext(NULL, "DocDBErrorContext");
	old_ctx = SetThreadLocalCurrentMemoryContext(error_ctx);
	status = (YbgStatus) palloc0(sizeof(YbgStatusData));
	status->error_ctx = error_ctx;
	SetThreadLocalCurrentMemoryContext(old_ctx);
	return status;
}

/*
 * YbgStatusCreateError - create new instance of YbgStatus of ERROR level
 * and specified error message.
 *
 * One-off function to create new YbgStatus object, set its level to ERROR and
 * attach specified message. Convenient where elog.h can not be included to
 * get access to the ERROR constant.
 */
YbgStatus
YbgStatusCreateError(const char *msg, const char *filename, int line)
{
	YbgMemoryContext old_ctx;
	YbgStatus status = YbgStatusCreate();
	old_ctx = SetThreadLocalCurrentMemoryContext(status->error_ctx);
	status->err_level = ERROR;
	status->err_msg = strdup(msg);
	status->filename = filename; /* no dup, expect static value */
	status->lineno = line;
	SetThreadLocalCurrentMemoryContext(old_ctx);
	return status;
}

/*
 * YbgStatusDestroy - release memory allocated for the status
 *
 * Safe to use on NULL (OK status) and static (like YbgFailedErrorReporting).
 */
void
YbgStatusDestroy(YbgStatus status)
{
	/* NULL status->error_ctx indicates static YbgStatus */
	if (status && status->error_ctx)
		MemoryContextDelete(status->error_ctx);
}

/*
 * YbgStatusGetContext - memory context to put data owned by the instance
 *
 * Indirectly owned data (e.g. owned by edata) should be allocated in that
 * context as well.
 */
void *
YbgStatusGetContext(YbgStatus status)
{
	return status->error_ctx;
}

/*
 * YbgStatusSetLevel - set the error level
 *
 * Constants for error levels are defined in utils/elog.h
 */
void
YbgStatusSetLevel(YbgStatus status, int32_t elevel)
{
	status->err_level = elevel;
}

/*
 * YbgStatusIsError - check the error level
 *
 * Returns true if error level is ERROR or higher.
 */
bool
YbgStatusIsError(YbgStatus status)
{
	return status && status->err_level >= ERROR;
}

/*
 * YbgStatusSetMessageAndArgs - sets the error message and arguments to the
 * status
 *
 * Error message is translatable by Postgres. If client set locale for the
 * messages and the translation for the exact message exists, postgres would
 * use the translation to render the message before presenting it to the client.
 *
 * The arguments must be strings, regardless of data type code in the template.
 * In fact, to properly translate, the message must be equal to the key, hence
 * the data type codes, and currently Status supports only string arguments.
 */
void
YbgStatusSetMessageAndArgs(YbgStatus status, const char *msg,
						   int32_t nargs, const char **args)
{
	YbgMemoryContext old_ctx;

	old_ctx = SetThreadLocalCurrentMemoryContext(status->error_ctx);
	status->err_msg = msg == NULL ? NULL : pstrdup(msg);
	status->nargs = nargs;
	if (nargs > 0)
	{
		int i;
		status->err_args = (const char **) palloc(nargs * sizeof(const char *));
		for (i = 0; i < nargs; i++)
			status->err_args[i] = pstrdup(args[i]);
	}
	else
		status->err_args = NULL;
	SetThreadLocalCurrentMemoryContext(old_ctx);
}

/*
 * YbgStatusGetMessage - get the message from the status.
 */
const char *
YbgStatusGetMessage(YbgStatus status)
{
	if (!status)
		return "OK";
	return status->err_msg ? status->err_msg : "Unexpected error in YbGate";
}

/*
 * YbgStatusGetMessageArgs - get the message arguments from the status.
 */
const char **
YbgStatusGetMessageArgs(YbgStatus status, int32_t *nargs)
{
	if (status)
	{
		if (nargs)
			*nargs = status->nargs;
		return status->err_args;
	}
	if (nargs)
		*nargs = 0;
	return NULL;
}

/*
 * YbgStatusGetSqlError - get the SQL Code from the status.
 */
uint32_t
YbgStatusGetSqlError(YbgStatus status)
{
	return status->sqlerror;
}

/*
 * YbgStatusSetSqlError - set the SQL Code to the status.
 */
void
YbgStatusSetSqlError(YbgStatus status, uint32_t sqlerror)
{
	status->sqlerror = sqlerror;
}

/*
 * YbgStatusGetFilename - get the error location filename.
 */
const char *
YbgStatusGetFilename(YbgStatus status)
{
	return status->filename;
}

/*
 * YbgStatusSetFilename - set the error location filename.
 */
void
YbgStatusSetFilename(YbgStatus status, const char *filename)
{
	status->filename = filename;
}

/*
 * YbgStatusGetLineNumber - get the error location line.
 */
int
YbgStatusGetLineNumber(YbgStatus status)
{
	return status->lineno;
}

/*
 * YbgStatusSetLineNumber - set the error location line.
 */
void
YbgStatusSetLineNumber(YbgStatus status, int lineno)
{
	status->lineno = lineno;
}

/*
 * YbgStatusGetFuncname - get the error location function.
 */
const char *
YbgStatusGetFuncname(YbgStatus status)
{
	return status->funcname;
}

/*
 * YbgStatusSetFuncname - get the error location function.
 */
void
YbgStatusSetFuncname(YbgStatus status, const char *funcname)
{
	status->funcname = funcname;
}

/*
 * YbgStatusGetEdata - get the error data.
 *
 * Error data is an opaque data structure private to elog.c. It is allocated
 * in the instance's error context, as well as any data it owns, so it is
 * automatically cleaned up when status is destroyed.
 */
void *YbgStatusGetEdata(YbgStatus status)
{
	return status->edata;
}

/*
 * YbgStatusSetEdata - set the error data.
 *
 * That is the caller's responsibility to make sure the edata is allocated in
 * correct memory context. Use YbgStatusGetContext() to obtain the context.
 */
void
YbgStatusSetEdata(YbgStatus status, void *edata)
{
	status->edata = edata;
}
