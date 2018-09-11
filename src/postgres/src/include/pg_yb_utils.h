/* ----------
 * pg_yb_utils.h
 *
 * Utilities for YugaByte/PostgreSQL integration that have to be defined on the
 * PostgreSQL side.
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
 * src/include/pg_yb_utils.h
 * ----------
 */

#ifndef PG_YB_UTILS_H
#define PG_YB_UTILS_H

#include "postgres.h"

#include "yb/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"

extern YBCPgSession ybc_pg_session;

bool		IsYugaByteEnabled();

/*
 * Given a relation (table) id, returns whether this table is handled by
 * YugaByte: i.e. it is not a system table or in the template1 database.
 */
bool		IsYBSupportedTable(Oid relid);

void		YBReportFeatureUnsupported(const char *err_msg);

/*
 * Given a status returned by YB C++ code, reports that status using ereport if
 * it is not OK.
 */
void		HandleYBStatus(YBCStatus status);

/*
 * YB initialization that needs to happen when a PostgreSQL backend process
 * is started. Reports errors using ereport.
 */
void YBInitPostgresBackend(
					  const char *program_name,
					  const char *db_name,
					  const char *user_name);

/*
 * This should be called on all exit paths from the PostgreSQL backend process.
 * Only main PostgreSQL backend thread is expected to call this.
 */
void		YBOnPostgresBackendShutdown();

#endif							/* PG_YB_UTILS_H */
