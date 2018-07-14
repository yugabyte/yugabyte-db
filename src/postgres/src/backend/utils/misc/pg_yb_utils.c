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

#include "yb/yql/pggate/ybc_pggate.h"

YBCPgSession ybc_pg_session = NULL;

void HandleYBStatus(YBCStatus status) {
	if (!status)
		return;
	/* TODO: consider creating PostgreSQL error codes for YB statuses. */
	ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("%s", status->msg)));
}

void YBInitPostgresBackend(
		const char* program_name,
		const char* db_name,
		const char* user_name) {
	HandleYBStatus(YBCInit(program_name, palloc));

	/*
	 * Enable "YB mode" for PostgreSQL so that we will initiate a connection
	 * to the YugaByte cluster right away from every backend process. We
	 * only do this if this env variable is set, so we can still run the
	 * regular PostgreSQL "make check".
	 */
	const char* pg_yb_mode = getenv("YB_ENABLED_IN_POSTGRES");

	if (pg_yb_mode != NULL && strcmp(pg_yb_mode, "1") == 0) {
		YBCInitPgGate();

		/*
			* For each process, we create one YBC session for PostgreSQL to use
			* when accessing YugaByte storage.
			*
			* TODO: do we really need to DB name / username here?
			*/
		if (db_name != NULL) {
			HandleYBStatus(YBCPgCreateSession(
				/* pg_env */ NULL, db_name, &ybc_pg_session));
		} else if (user_name != NULL) {
			HandleYBStatus(
				YBCPgCreateSession(
					/* pg_env */ NULL, user_name, &ybc_pg_session));
		}
	}
}

void YBOnPostgresBackendShutdown() {
	YBCLogInfo("YBOnPostgresBackendShutdown called");
	static bool shutdown_done = false;
	if (shutdown_done) {
		return;
	}
	if (ybc_pg_session) {
		YBCPgDestroySession(ybc_pg_session);
		ybc_pg_session = NULL;
	}
	shutdown_done = true;
}