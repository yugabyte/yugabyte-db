/*-------------------------------------------------------------------------
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
 * IDENTIFICATION
 *    src/backend/utils/misc/yb_pg_conf_validator.c
 *
 *
 * yb_pg_conf_validator.c
 *    Validate PostgreSQL configuration files without applying settings.
 *
 * This file implements yb_pg_validate_conf_file(), a SQL-callable function
 * that validates all three configuration files:
 *  - YB postgres conf file with GUCs (ysql_pg.conf)
 *  - HBA conf file ysql_hba.conf
 *  - Ident conf file ysql_ident.conf
 *
 *
 * This function is used for gflag validation of the corresponding gflags.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "fmgr.h"
#include "funcapi.h"
#include "libpq/hba.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/guc.h"

PG_FUNCTION_INFO_V1(yb_pg_validate_conf_file);

static char *yb_capture_error(MemoryContext oldcxt);
static void
			yb_validate_guc_conf_file(const char *conf_file_path);

/*
 * yb_pg_validate_conf_file(hba_path text, guc_path text, ident_path text,
 *     OUT hba_error text, OUT guc_error text, OUT ident_error text)
 *
 * Validates all three configuration files. NULL path skips validation for
 * that conf file, however it is recommended to validate all confs together.
 * Validation errors from one file do not prevent validation of the others.
 * Each OUT parameter is NULL on success or contains the error message.
 *
 * Superuser-only for security (reads arbitrary file paths).
 */
Datum
yb_pg_validate_conf_file(PG_FUNCTION_ARGS)
{
	char	   *hba_path = PG_ARGISNULL(0) ? NULL
		: text_to_cstring(PG_GETARG_TEXT_PP(0));
	char	   *guc_path = PG_ARGISNULL(1) ? NULL
		: text_to_cstring(PG_GETARG_TEXT_PP(1));
	char	   *ident_path = PG_ARGISNULL(2) ? NULL
		: text_to_cstring(PG_GETARG_TEXT_PP(2));
	TupleDesc	tupdesc;
	Datum		values[3];
	bool		nulls[3] = {true, true, true};
	MemoryContext oldcxt;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to validate configuration files")));

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("function returning record called in context "
						"that cannot accept type record")));

	tupdesc = BlessTupleDesc(tupdesc);
	oldcxt = CurrentMemoryContext;

	/*
	 * Validate GUC config. Errors are reported via ereport ERROR.
	 */
	if (guc_path)
	{
		PG_TRY();
		{
			yb_validate_guc_conf_file(guc_path);
		}
		PG_CATCH();
		{
			char	   *err = yb_capture_error(oldcxt);

			Assert(err != NULL);
			values[1] = CStringGetTextDatum(err);
			nulls[1] = false;
			pfree(err);
		}
		PG_END_TRY();
	}

	/*
	 * Validate HBA config. load_hba in validation mode reports errors at
	 * ERROR level, which we capture via PG_CATCH. The other approach of
	 * properly propagating all errors involves too many modifications to PG
	 * code that are harder to maintain. One disadvantage of the current
	 * approach is that we only get the first error in the file.
	 */
	if (hba_path)
	{
		PG_TRY();
		{
			if (!load_hba(hba_path))
				ereport(ERROR,
						(errcode(ERRCODE_CONFIG_FILE_ERROR),
						 errmsg("HBA configuration file \"%s\" contains errors",
								hba_path)));
		}
		PG_CATCH();
		{
			char	   *err = yb_capture_error(oldcxt);

			Assert(err != NULL);
			values[0] = CStringGetTextDatum(err);
			nulls[0] = false;
			pfree(err);
		}
		PG_END_TRY();
	}

	/*
	 * Validate ident config.
	 * Similar to load_hba, we capture errors via PG_CATCH to avoid modifying
	 * PG code significantly.
	 */
	if (ident_path)
	{
		PG_TRY();
		{
			if (!load_ident(CurrentMemoryContext, ident_path))
				ereport(ERROR,
						(errcode(ERRCODE_CONFIG_FILE_ERROR),
						 errmsg("ident configuration file \"%s\" contains errors",
								ident_path)));
		}
		PG_CATCH();
		{
			char	   *err = yb_capture_error(oldcxt);

			Assert(err != NULL);
			values[2] = CStringGetTextDatum(err);
			nulls[2] = false;
			pfree(err);
		}
		PG_END_TRY();
	}

	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}

static void
yb_validate_guc_conf_file(const char *conf_file_path)
{
	ConfigVariable *head = NULL;
	ConfigVariable *tail = NULL;
	ConfigVariable *item;
	bool		parse_ok;
	StringInfoData errbuf;

	/* Attempt to parse and load conf file entries */
	parse_ok = ParseConfigFile(conf_file_path, true /* strict */ ,
							   NULL, 0, 0, LOG,
							   &head, &tail);

	if (!parse_ok)
	{
		initStringInfo(&errbuf);
		for (item = head; item; item = item->next)
		{
			if (item->errmsg)
				appendStringInfo(&errbuf, "%s\n",
								 item->errmsg);
		}
		FreeConfigVariables(head);
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("syntax error in configuration file \"%s\"",
						conf_file_path),
				 errdetail("%s", errbuf.data)));
	}

	initStringInfo(&errbuf);

	/*
	 * Validate each entry by calling set_config_option with ERROR elevel
	 * wrapped in PG_TRY/PG_CATCH. This lets PG construct the full ErrorData
	 * including the error hint - the hint is useful to pass back to the user to fix
	 * the problem.
	 */
	for (item = head; item; item = item->next)
	{
		MemoryContext loop_cxt = CurrentMemoryContext;

		if (item->ignore)
			continue;

		if (item->errmsg)
			continue;

		PG_TRY();
		{
			set_config_option(item->name, item->value,
							  PGC_SIGHUP, PGC_S_FILE,
							  GUC_ACTION_SET, false /* changeVal */ ,
							  ERROR, false /* is_reload */ );
		}
		PG_CATCH();
		{
			char	   *err = yb_capture_error(loop_cxt);

			Assert(err != NULL);
			appendStringInfo(&errbuf, "%s\n", err);
			pfree(err);
		}
		PG_END_TRY();
	}

	FreeConfigVariables(head);

	if (errbuf.len > 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_CONFIG_FILE_ERROR),
				 errmsg("configuration file contains invalid settings"),
				 errdetail("%s", errbuf.data)));
	}
}

/*
 * Captures PG error information from within a PG_CATCH block.
 * Returns a palloc'd C string with the error message.
 */
static char *
yb_capture_error(MemoryContext oldcxt)
{
	ErrorData  *edata;
	StringInfoData buf;

	MemoryContextSwitchTo(oldcxt);
	edata = CopyErrorData();
	FlushErrorState();

	initStringInfo(&buf);
	appendStringInfoString(&buf, edata->message);
	if (edata->detail)
		appendStringInfo(&buf, " (%s)", edata->detail);
	if (edata->hint)
		appendStringInfo(&buf, " HINT: %s", edata->hint);
	FreeErrorData(edata);

	return buf.data;
}
