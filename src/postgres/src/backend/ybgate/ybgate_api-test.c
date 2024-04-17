/*-------------------------------------------------------------------------
 *
 * ybgate_api-test.c
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
 *	  src/backend/ybgate/ybgate_api-test.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <stdio.h>

#include "catalog/yb_type.h"
#include "utils/elog.h"
#include "ybgate/ybgate_api-test.h"
#include "ybgate/ybgate_status.h"

/*
 * Utility function to ereport either LOG or ERROR.
 * To be used while other ereport is composed.
 */
static bool
yb_log_or_fail(bool throw)
{
	ereport(throw ? ERROR : LOG,
			(errmsg("Another ereport while composing an error")));
	/* Unreachable when throw is true */
	return true;
}

/*
 * Utility function to ereport an error in a PG_TRY / PG_CATCH block,
 * optionally rethrow.
 * Used to nest PG_TRY / PG_CATCH block.
 * Postgres PG_TRY / PG_CATCH blocks can not nest directly, their local
 * variables would conflict.
 */
static bool
yb_try_or_fail(bool rethrow)
{
	PG_TRY();
	{
		ereport(ERROR,
				(errmsg("Nested PG_TRY with ereport")));
	}
	PG_CATCH();
	{
		if (rethrow)
			PG_RE_THROW();
	}
	PG_END_TRY();
	return true;
}

/*
 * yb_test - main function implementing test cases
 */
static void
yb_test(YbGateTestCase case_no)
{
	switch (case_no)
	{
		case YBGATE_TEST_ELOG_LOG:
			/* Plain elog(LOG) */
			elog(LOG, "Log message");
			break;
		case YBGATE_TEST_ELOG_NOTICE:
			/* Plain elog(NOTICE) */
			elog(NOTICE, "Notice message");
			break;
		case YBGATE_TEST_ELOG_WARN:
			/* Plain elog(WARNING) */
			elog(WARNING, "Warning message");
			break;
		case YBGATE_TEST_ELOG_ERROR:
			/* Plain elog(ERROR) */
			elog(ERROR, "Error message");
			break;
		case YBGATE_TEST_EREPORT_LOG:
			/* Plain ereport(LOG) */
			ereport(LOG,
					(errmsg("Log message")));
			break;
		case YBGATE_TEST_EREPORT_WARN:
			/* Plain ereport(WARNING) */
			ereport(WARNING,
					(errmsg("Warning message")));
			break;
		case YBGATE_TEST_EREPORT_ERROR:
			/* Plain ereport(ERROR) */
			ereport(ERROR,
					(errmsg("Error message")));
			break;
		case YBGATE_TEST_EREPORT_FATAL:
			/* Plain ereport(FATAL) */
			ereport(FATAL,
					(errmsg("Fatal message")));
			break;
		case YBGATE_TEST_EREPORT_PANIC:
			/* Plain ereport(PANIC) */
			ereport(PANIC,
					(errmsg("Panic message")));
			break;
		case YBGATE_TEST_EREPORT_ERROR_LOCATION:
			/* one line ereport(ERROR) to avoid __LINE__ ambiguity */
			ereport(ERROR, (errmsg("Error message with param: %d", 1)));
			break;
		case YBGATE_TEST_EREPORT_ERROR_CODE:
			/* Error with extra fields */
			ereport(ERROR,
					(errmsg("Error message: %s", "OK"),
					 errcode(ERRCODE_DIVISION_BY_ZERO)));
			break;
		case YBGATE_TEST_EREPORT_FORMAT:
		{
			/* Test various formatting templates */
			const char *format =
				"1. %% - percent symbol, not a template\n"
				"2. %d - simple integer\n"
				"3. %f - simple float\n"
				"4. %s - simple string\n"
				"5. %x - simple hex integer\n"
				"6. %4d - integer with explicit length\n"
				"7. %ld - long integer\n"
				"8. %6.2f%% - float with length and precision\n"
				"9. %*.*f - float with dynamic length and precision\n"
				"10. %+E - float in scientific notation with sign\n";
			ereport(ERROR,
					(errmsg(format,
							42, /* 2 */
							12.3, /* 3 */
							"foo", /* 4 */
							256, /* 5 */
							34, /* 6 */
							123456789012, /* 7 */
							87.654321, /* 8 */
							10, 3, 1024.9, /* 9 */
							3579.864 /* 10 */)));
			break;
		}
		case YBGATE_TEST_EREPORT_FORMAT_TOO_LONG_ARG:
		{
			/* Too long argument to render */
			const char *format = "1. %*d - too long argument\n"
								 "2. %d - simple integer\n"
								 "3. %ld - max long integer\n"
								 "4. %ld - min long integer\n"
								 "5. %lu - max unsigned long integer\n"
								 "6. %lld - max long long integer\n"
								 "7. %lld - min long long integer\n"
								 "8. %llu - max unsigned long long integer\n"
								 "9. %.1e - max double\n"
								 "10. %.1e - min double\n";
			ereport(ERROR,
					(errmsg(format, 17000, -1,			/* 1 */
							42,							/* 2 */
							9223372036854775807LL,		/* 3 */
							-9223372036854775807LL,		/* 4 */
							18446744073709551615ULL,	/* 5 */
							9223372036854775807LL,		/* 6 */
							-9223372036854775807LL,		/* 7 */
							18446744073709551615ULL,	/* 8 */
							2.3E-308,					/* 9 */
							1.7E308						/* 10 */)));
			break;
		}
		case YBGATE_TEST_ELOG_FORMAT_ERRNO:
		{
			/* Try to open not exiting file to set errno */
			const char *fname = "/path/not/exist.txt";
			FILE *fptr = fopen(fname, "r");
			if (fptr == NULL)
				elog(ERROR, "could not open file \"%s\": %m", fname);
			fclose(fptr);
			break;
		}
		case YBGATE_TEST_EREPORT_AND_LOG:
		case YBGATE_TEST_EREPORT_AND_ERROR:
		{
			/*
			 * While ereport(ERROR) composes error message other ereport
			 * produces LOG or ERROR
			 */
			bool throw = case_no == YBGATE_TEST_EREPORT_AND_ERROR;
			ereport(ERROR,
					(errmsg("Error message: %s",
							yb_log_or_fail(throw) ? "OK" : "nope")));
			break;
		}
		case YBGATE_TEST_EREPORT_AND_TRY_CATCH:
			/*
			 * While ereport(ERROR) composes error message other ereport
			 * produces ERROR in PG_TRY/PG_CATCH block, without rethrow
			 */
			ereport(ERROR,
					(errmsg("Error message: %s",
							(yb_try_or_fail(false) ? "OK" : "nope"))));
			break;
		case YBGATE_TEST_TRY_CATCH:
		case YBGATE_TEST_TRY_CATCH_RETHROW:
			/*
			 * PG_TRY/PG_CATCH with ereport(ERROR) inside, with or without
			 * PG_RE_THROW
			 */
			PG_TRY();
			{
				ereport(ERROR,
						(errmsg("Error message with param: %d", 3)));
			}
			PG_CATCH();
			{
				if (case_no == YBGATE_TEST_TRY_CATCH_RETHROW)
					PG_RE_THROW();
			}
			PG_END_TRY();
			break;
		case YBGATE_TEST_TRY_CATCH_LOG:
		case YBGATE_TEST_TRY_CATCH_ERROR:
			/*
			 * PG_TRY/PG_CATCH with ereport(ERROR) inside, with different
			 * ereport(LOG or ERROR) in the PG_CATCH block
			 */
			PG_TRY();
			{
				ereport(ERROR,
						(errmsg("Error message with param: %d", 5)));
			}
			PG_CATCH();
			{
				ereport(case_no == YBGATE_TEST_TRY_CATCH_LOG ? LOG : ERROR,
						(errmsg("Another ereport while handling an error")));
			}
			PG_END_TRY();
			break;
		case YBGATE_TEST_TRY_CATCH_DOUBLE_ERROR:
			/*
			 * PG_TRY/PG_CATCH with nested ereport(ERROR) inside
			 * When inner error is caught and handled, make sure outer error
			 * does not show up.
			 */
			PG_TRY();
			{
				ereport(ERROR,
						(errmsg("This should not be visible"),
						 yb_log_or_fail(true)));
			}
			PG_CATCH();
			{
				ereport(LOG,
						(errmsg("This should go to log")));
			}
			PG_END_TRY();
			break;
		case YBGATE_TEST_NESTED_TRY_TRY_CATCH:
		case YBGATE_TEST_NESTED_TRY_TRY_CATCH_RETHROW:
			/* Nested PG_TRY/PG_CATCH in PG_TRY block, either rethrow or not */
			PG_TRY();
			{
				yb_try_or_fail(
					case_no == YBGATE_TEST_NESTED_TRY_TRY_CATCH_RETHROW);
				ereport(ERROR, (errmsg("Direct error")));
			}
			PG_CATCH();
			{
				PG_RE_THROW();
			}
			PG_END_TRY();
			break;
		case YBGATE_TEST_NESTED_CATCH_TRY_CATCH:
		case YBGATE_TEST_NESTED_CATCH_TRY_CATCH_RETHROW:
			/*
			 * Nested PG_TRY/PG_CATCH in PG_CATCH block, either rethrow or not
			 */
			PG_TRY();
			{
				ereport(ERROR,
						(errmsg("Direct error")));
			}
			PG_CATCH();
			{
				yb_try_or_fail(
					case_no == YBGATE_TEST_NESTED_CATCH_TRY_CATCH_RETHROW);
			}
			PG_END_TRY();
			break;
		case YBGATE_TEST_EDATA_LOG:
		case YBGATE_TEST_EDATA_THROW:
			PG_TRY();
			{
				ereport(ERROR,
						(errmsg("Direct error")));
			}
			PG_CATCH();
			{
				MemoryContext	ctx;
				ErrorData	   *edata;
				ctx = CreateThreadLocalCurrentMemoryContext(NULL, "test context");
				SetThreadLocalCurrentMemoryContext(ctx);
				edata = CopyErrorData();
				FlushErrorState();
				edata->message = pstrdup("Modified error");
				if (case_no == YBGATE_TEST_EDATA_LOG)
				{
					edata->elevel = LOG;
					ThrowErrorData(edata);
					FreeErrorData(edata);
				}
				else
					ReThrowError(edata);
			}
			PG_END_TRY();
			break;
		default:
			elog(ERROR, "Invalid test case number: %d", case_no);
	}
}

/*
 * YbgTest - set up YbGate error handling and run specified test
 */
YbgStatus YbgTest(YbGateTestCase case_no)
{
	PG_SETUP_ERROR_REPORTING();
	yb_test(case_no);
	PG_STATUS_OK();
}

/*
 * YbgTestNoReporting - run specified test without setting up error handling
 */
void YbgTestNoReporting(YbGateTestCase case_no)
{
	yb_test(case_no);
}

bool YbTypeDetailsTest(
	unsigned int elmtype, int *elmlen, bool *elmbyval, char *elmalign)
{
	return YbTypeDetails(elmtype, elmlen, elmbyval, elmalign);
}
