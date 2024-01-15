/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/diagnostic_commands_common.h
 *
 * Common declarations of Mongo Diagnostic commands.
 * These are used in diag command scenarios like CurrentOp, IndexStats, CollStats
 *
 *-------------------------------------------------------------------------
 */

#ifndef DIAGNOSTIC_COMMANDS_COMMON_H
#define DIAGNOSTIC_COMMANDS_COMMON_H


List * GetWorkerBsonsFromAllWorkers(const char *query, Datum *paramValues,
									Oid *types, int numValues,
									const char *commandName);

pgbson * RunWorkerDiagnosticLogic(pgbson *(*workerFunc)(void *state), void *state);


/* Common keys (for parsing error messages and codes from worker to coordinator)
 * Note that these are #defines instead of consts since the C compiler complains
 * if any of these aren't explicitly used in any file it's included in.
 */
#define ErrMsgKey "err_msg"
#define ErrMsgLength 7
#define ErrCodeKey "err_code"
#define ErrCodeLength 8

#endif
