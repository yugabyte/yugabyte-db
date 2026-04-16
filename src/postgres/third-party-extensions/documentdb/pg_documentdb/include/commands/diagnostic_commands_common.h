/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/diagnostic_commands_common.h
 *
 * Common declarations of Diagnostic commands.
 * These are used in diag command scenarios like CurrentOp, IndexStats, CollStats
 *
 *-------------------------------------------------------------------------
 */

#ifndef DIAGNOSTIC_COMMANDS_COMMON_H
#define DIAGNOSTIC_COMMANDS_COMMON_H

List * RunQueryOnAllServerNodes(const char *commandName, Datum *values, Oid *types, int
								numValues, PGFunction directFunc,
								const char *nameSpaceName, const char *functionName);


pgbson * RunWorkerDiagnosticLogic(pgbson *(*workerFunc)(void *state), void *state);


/* Common keys (for parsing error messages and codes from worker to coordinator)
 * Note that these are #defines instead of consts since the C compiler complains
 * if any of these aren't explicitly used in any file it's included in.
 */
#define ErrMsgKey "err_msg"
#define ErrMsgLength 7
#define ErrCodeKey "err_code"
#define ErrCodeLength 8

/*
 * For Single node scenarios the nodeId always points to itself.
 */
#define SINGLE_NODE_ID 10000000000LL
#define SINGLE_NODE_ID_STR "10000000000"

#endif
