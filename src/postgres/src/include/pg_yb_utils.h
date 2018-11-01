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

/**
 * Checks whether YugaByte functionality is enabled within PostgreSQL.
 * This relies on ybc_pg_session being non-NULL, so probably should not be used
 * in postmaster (which does not need to talk to YB backend) or early
 * in backend process initialization. In those cases the
 * YBIsEnabledInPostgresEnvVar function might be more appropriate.
 */
extern bool IsYugaByteEnabled();

/*
 * Given a relation (table) id, returns whether this table is handled by
 * YugaByte: i.e. it is not a system table or in the template1 database.
 */
extern bool IsYBSupportedTable(Oid relid);

extern void YBReportFeatureUnsupported(const char *err_msg);

/**
 * Whether to route BEGIN / COMMIT / ROLLBACK to YugaByte's distributed
 * transactions. This will be enabled by default soon after 10/12/2018.
 */
extern bool YBTransactionsEnabled();

/*
 * Given a status returned by YB C++ code, reports that status using ereport if
 * it is not OK.
 */
extern void	HandleYBStatus(YBCStatus status);

/*
 * Same as HandleYBStatus but delete the statement first if the status is
 * not ok.
 */
extern void	HandleYBStmtStatus(YBCStatus status, YBCPgStatement ybc_stmt);

/*
 * Same as HandleYBStatus but delete the table description first if the
 * status is not ok.
 */
extern void HandleYBTableDescStatus(YBCStatus status, YBCPgTableDesc table);
/*
 * YB initialization that needs to happen when a PostgreSQL backend process
 * is started. Reports errors using ereport.
 */
extern void YBInitPostgresBackend(
					  const char *program_name,
					  const char *db_name,
					  const char *user_name);

/*
 * This should be called on all exit paths from the PostgreSQL backend process.
 * Only main PostgreSQL backend thread is expected to call this.
 */
extern void	YBOnPostgresBackendShutdown();

/**
 * Commits the current YugaByte-level transaction. Returns true in case of
 * successful commit and false in case of failure. If there is no transaction in
 * progress, also returns true.
 */
extern bool YBCCommitTransaction();

/**
 * Handle a commit error if it happened during a previous call to
 * YBCCommitTransaction. We allow deferring this handling in order to be able
 * to make PostgreSQL transaction block state transitions before calling
 * ereport.
 */
extern void YBCHandleCommitError();

/**
 * Checks if the given environment variable is set to "1".
 */
extern bool YBCIsEnvVarTrue(const char* env_var_name);

/**
 * Return true if we want to allow PostgreSQL's own locking. This is needed
 * while system tables are still managed by PostgreSQL.
 */
extern bool YBIsPgLockingEnabled();

/**
 * Return a string representation of the given type id, or say it is unknown.
 * What is returned is always a static C string constant.
 */
extern const char* YBPgTypeOidToStr(Oid type_id);

/**
 * Report an error saying the given type as not supported by YugaByte.
 */
extern void YBReportTypeNotSupported(Oid type_id);

/**
 * Log whether or not YugaByte is enabled.
 */
extern void YBReportIfYugaByteEnabled();

/**
 * Checks if the YB_ENABLED_IN_POSTGRES is set.
 */
bool YBIsEnabledInPostgresEnvVar();

#define YB_REPORT_TYPE_NOT_SUPPORTED(type_id) do { \
		Oid computed_type_id = type_id; \
		ereport(ERROR, \
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED), \
					errmsg("Type not yet supported in YugaByte: %d (%s)", \
						computed_type_id, YBPgTypeOidToStr(computed_type_id)))); \
	} while (0)


/**
 * Determines if PostgreSQL should restart all child processes if one of them
 * crashes. This behavior usually shows up in the log like so:
 *
 * WARNING:  terminating connection because of crash of another server process
 * DETAIL:  The postmaster has commanded this server process to roll back the
 *          current transaction and exit, because another server process exited
 *          abnormally and possibly corrupted shared memory.
 *
 * However, we want to avoid this behavior in some cases, e.g. when our test
 * framework is trying to intentionally cause core dumps of stuck backend
 * processes and analyze them. Disabling this behavior is controlled by setting
 * the YB_PG_NO_RESTART_ALL_CHILDREN_ON_CRASH_FLAG_PATH variable to a file path,
 * which could be created or deleted at run time, and its existence is always
 * checked.
 */
bool YBShouldRestartAllChildrenIfOneCrashes();

/**
 * Define additional inline wrappers around _Status functions that return the
 * real return value and ereport the error status.
 */
#include "yb/yql/pggate/if_macros_c_pg_wrapper_inl.h"
#include "yb/yql/pggate/pggate_if.h"
#include "yb/yql/pggate/if_macros_undef.h"

/*
 * These functions help indicating if we are creating system catalog.
 */
void YBSetPreparingTemplates();
bool YBIsPreparingTemplates();

/**
 * Whether every ereport of the ERROR level and higher should log a stack trace.
 */
bool YBShouldLogStackTraceOnError();

/**
 * Converts the PostgreSQL error level as listed in elog.h to a string. Always
 * returns a static const char string.
 */
const char* YBPgErrorLevelToString(int elevel);

#endif /* PG_YB_UTILS_H */
