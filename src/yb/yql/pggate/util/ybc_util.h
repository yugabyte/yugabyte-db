// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

// C wrappers around some YB utilities. Suitable for inclusion into C codebases such as our modified
// version of PostgreSQL.

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "yb/yql/pggate/ybc_pg_typedefs.h"

#ifdef __cplusplus
extern "C" {

struct varlena;

#endif

/*
 * Guc variable to log the protobuf string for every outgoing (DocDB) read/write request.
 * See the "YB Debug utils" section in pg_yb_utils.h (as well as guc.c) for more information.
 */
extern bool yb_debug_log_docdb_requests;

/*
 * Toggles whether formatting functions exporting system catalog information
 * include DocDB metadata (such as tablet split information).
 */
extern bool yb_format_funcs_include_yb_metadata;

/*
 * Guc variable to enable the use of regular transactions for operating on system catalog tables
 * in case a DDL transaction has not been started.
 */
extern bool yb_non_ddl_txn_for_sys_tables_allowed;

/*
 * Toggles whether to force use of global transaction status table.
 */
extern bool yb_force_global_transaction;

/*
 * Guc that toggles whether strict inequalities are pushed down.
 */
extern bool yb_pushdown_strict_inequality;

/*
 * Guc that toggles whether IS NOT NULL is pushed down.
 */
extern bool yb_pushdown_is_not_null;

/*
 * Guc that toggles the pg_locks view on/off.
 */
extern bool yb_enable_pg_locks;

/*
 * Guc variable to suppress non-Postgres logs from appearing in Postgres log file.
 */
extern bool suppress_nonpg_logs;

/*
 * Guc variable to control the max session batch size before flushing.
 */
extern int ysql_session_max_batch_size;

/*
 * Guc variable to control the max number of in-flight operations from YSQL to tablet server.
 */
extern int ysql_max_in_flight_ops;

/*
 * Guc variable that sets the minimum transaction age, in ms, to report when using yb_lock_status().
 */
extern int yb_locks_min_txn_age;

/*
 * Guc variable that sets the number of transactions to return results for in yb_lock_status().
 */
extern int yb_locks_max_transactions;

/*
 * GUC variable to set the number of locks to return per transaction per tablet in yb_lock_status().
 */
extern int yb_locks_txn_locks_per_tablet;

/*
 * Guc variable to enable binary restore from a binary backup of YSQL tables. When doing binary
 * restore, we copy the docdb SST files of those tables from the source database and reuse them
 * for a newly created target database to restore those tables.
 */
extern bool yb_binary_restore;

/*
 * Guc variable for ignoring requests to set heap pg_class oids when yb_binary_restore is set.
 *
 * If true then calls to pg_catalog.binary_upgrade_set_next_heap_pg_class_oid will have no effect.
 */
extern bool yb_ignore_heap_pg_class_oids;

/*
 * Set to true only for runs with EXPLAIN ANALYZE
 */
extern bool yb_run_with_explain_analyze;

/*
 * GUC variable that enables batching RPCs of generated for IN queries
 * on hash keys issued to the same tablets.
 */
extern bool yb_enable_hash_batch_in;

/*
 * GUC variable that enables using the default value for existing rows after
 * an ADD COLUMN ... DEFAULT operation.
 */
extern bool yb_enable_add_column_missing_default;

/*
 * Guc variable that enables replication commands.
 */
extern bool yb_enable_replication_commands;

/*
 * Guc variable that enables replication slot consumption.
 */
extern bool yb_enable_replication_slot_consumption;

/*
 * GUC variable that enables ALTER TABLE rewrite operations.
 */
extern bool yb_enable_alter_table_rewrite;

/*
 * GUC variable that enables replica identity command in Alter Table Query
 */
extern bool yb_enable_replica_identity;

/*
 * GUC variable that specifies default replica identity for tables at the time of creation.
 */
extern char* yb_default_replica_identity;

/*
 * xcluster consistency level
 */
#define XCLUSTER_CONSISTENCY_TABLET 0
#define XCLUSTER_CONSISTENCY_DATABASE 1

/*
 * Enables atomic and ordered reads of data in xCluster replicated databases. This may add a delay
 * to the visibility of all data in the database.
 */
extern int yb_xcluster_consistency_level;

/*
 * Allows user to query a databases as of the point in time.
 * yb_read_time can be expressed in the following 2 ways -
 *  - UNIX timestamp in microsecond (default unit)
 *  - as a uint64 representation of HybridTime with unit "ht"
 * Zero value means reading data as of current time.
 */
extern uint64_t yb_read_time;
extern bool yb_is_read_time_ht;

/*
 * Allows for customizing the number of rows to be prefetched.
 */
extern int yb_fetch_row_limit;
extern int yb_fetch_size_limit;

/*
 * GUC flag: Time in milliseconds for which Walsender waits before fetching the next batch of
 * changes from the CDC service in case the last received response was non-empty.
 */
extern int yb_walsender_poll_sleep_duration_nonempty_ms;

/*
 * GUC flag:  Time in milliseconds for which Walsender waits before fetching the next batch of
 * changes from the CDC service in case the last received response was empty. The response can be
 * empty in case there are no DMLs happening in the system.
 */
extern int yb_walsender_poll_sleep_duration_empty_ms;

/*
 * GUC flag: Specifies the maximum number of changes kept in memory per transaction in reorder
 * buffer, which is used in streaming changes via logical replication. After that changes are
 * spooled to disk.
 */
extern int yb_reorderbuffer_max_changes_in_memory;

/*
 * Allows for customizing the maximum size of a batch of explicit row lock operations.
 */
extern int yb_explicit_row_locking_batch_size;

/*
 * Ease transition to YSQL by reducing read restart errors for new apps.
 *
 * This option doesn't affect SERIALIZABLE isolation level since
 * SERIALIZABLE can't face read restart errors anyway.
 *
 * See the help text for yb_read_after_commit_visibility GUC for more
 * information.
 *
 * XXX: This GUC is meant as a workaround only by relaxing the
 * read-after-commit-visibility guarantee. Ideally,
 * (a) Users should fix their apps to handle read restart errors, or
 * (b) TODO(#22317): YB should use very accurate clocks to avoid read restart
 *     errors altogether.
 */
typedef enum {
  YB_STRICT_READ_AFTER_COMMIT_VISIBILITY = 0,
  YB_RELAXED_READ_AFTER_COMMIT_VISIBILITY = 1,
} YBReadAfterCommitVisibilityEnum;

/* GUC for the enum above. */
extern int yb_read_after_commit_visibility;

typedef struct YBCStatusStruct* YBCStatus;

bool YBCStatusIsNotFound(YBCStatus s);
bool YBCStatusIsUnknownSession(YBCStatus s);
bool YBCStatusIsDuplicateKey(YBCStatus s);
bool YBCStatusIsSnapshotTooOld(YBCStatus s);
bool YBCStatusIsTryAgain(YBCStatus s);
bool YBCStatusIsAlreadyPresent(YBCStatus s);
bool YBCStatusIsReplicationSlotLimitReached(YBCStatus s);
bool YBCStatusIsFatalError(YBCStatus s);
uint32_t YBCStatusPgsqlError(YBCStatus s);
uint16_t YBCStatusTransactionError(YBCStatus s);
void YBCFreeStatus(YBCStatus s);

const char* YBCStatusFilename(YBCStatus s);
int YBCStatusLineNumber(YBCStatus s);
const char* YBCStatusFuncname(YBCStatus s);
size_t YBCStatusMessageLen(YBCStatus s);
const char* YBCStatusMessageBegin(YBCStatus s);
const char* YBCMessageAsCString(YBCStatus s);
unsigned int YBCStatusRelationOid(YBCStatus s);
const char** YBCStatusArguments(YBCStatus s, size_t* nargs);

bool YBCIsRestartReadError(uint16_t txn_errcode);
bool YBCIsTxnConflictError(uint16_t txn_errcode);
bool YBCIsTxnSkipLockingError(uint16_t txn_errcode);
bool YBCIsTxnDeadlockError(uint16_t txn_errcode);
bool YBCIsTxnAbortedError(uint16_t txn_errcode);
const char* YBCTxnErrCodeToString(uint16_t txn_errcode);
uint16_t YBCGetTxnConflictErrorCode();

void YBCResolveHostname();

#define CHECKED_YBCSTATUS __attribute__ ((warn_unused_result)) YBCStatus

typedef void* (*YBCPAllocFn)(size_t size);

typedef struct varlena* (*YBCCStringToTextWithLenFn)(const char* c, int size);

// Global initialization of the YugaByte subsystem.
CHECKED_YBCSTATUS YBCInit(
    const char* argv0,
    YBCPAllocFn palloc_fn,
    YBCCStringToTextWithLenFn cstring_to_text_with_len_fn);

// From glog's log_severity.h:
// const int GLOG_INFO = 0, GLOG_WARNING = 1, GLOG_ERROR = 2, GLOG_FATAL = 3;

// Logging macros with printf-like formatting capabilities.
#define YBC_LOG_INFO(...) \
    YBCLogImpl(/* severity */ 0, __FILE__, __LINE__, /* stack_trace */ false, __VA_ARGS__)
#define YBC_LOG_WARNING(...) \
    YBCLogImpl(/* severity */ 1, __FILE__, __LINE__, /* stack_trace */ false, __VA_ARGS__)
#define YBC_LOG_ERROR(...) \
    YBCLogImpl(/* severity */ 2, __FILE__, __LINE__, /* stack_trace */ false, __VA_ARGS__)
#define YBC_LOG_FATAL(...) \
    YBCLogImpl(/* severity */ 3, __FILE__, __LINE__, /* stack_trace */ false, __VA_ARGS__)

// Versions of these warnings that do nothing in debug mode. The fatal version logs a warning
// in release mode but does not crash.
#ifndef NDEBUG
// Logging macros with printf-like formatting capabilities.
#define YBC_DEBUG_LOG_INFO(...) YBC_LOG_INFO(__VA_ARGS__)
#define YBC_DEBUG_LOG_WARNING(...) YBC_LOG_WARNING(__VA_ARGS__)
#define YBC_DEBUG_LOG_ERROR(...) YBC_LOG_ERROR(__VA_ARGS__)
#define YBC_DEBUG_LOG_FATAL(...) YBC_LOG_FATAL(__VA_ARGS__)
#else
#define YBC_DEBUG_LOG_INFO(...)
#define YBC_DEBUG_LOG_WARNING(...)
#define YBC_DEBUG_LOG_ERROR(...)
#define YBC_DEBUG_LOG_FATAL(...) YBC_LOG_ERROR(__VA_ARGS__)
#endif

// The following functions log the given message formatted similarly to printf followed by a stack
// trace.

#define YBC_LOG_INFO_STACK_TRACE(...) \
    YBCLogImpl(/* severity */ 0, __FILE__, __LINE__, /* stack_trace */ true, __VA_ARGS__)
#define YBC_LOG_WARNING_STACK_TRACE(...) \
    YBCLogImpl(/* severity */ 1, __FILE__, __LINE__, /* stack_trace */ true, __VA_ARGS__)
#define YBC_LOG_ERROR_STACK_TRACE(...) \
    YBCLogImpl(/* severity */ 2, __FILE__, __LINE__, /* stack_trace */ true, __VA_ARGS__)

// 5 is the index of the format string, 6 is the index of the first printf argument to check.
void YBCLogImpl(int severity,
                const char* file_name,
                int line_number,
                bool stack_trace,
                const char* format,
                ...) __attribute__((format(printf, 5, 6)));

// VA version of YBCLogImpl
void YBCLogVA(int severity,
              const char* file_name,
              int line_number,
              bool stack_trace,
              const char* format,
              va_list args);

// Returns a string representation of the given block of binary data. The memory for the resulting
// string is allocated using palloc.
const char* YBCFormatBytesAsStr(const char* data, size_t size);

const char* YBCGetStackTrace();

// Initializes global state needed for thread management, including CDS library initialization.
void YBCInitThreading();

double YBCEvalHashValueSelectivity(int32_t hash_low, int32_t hash_high);

// Helper functions for Active Session History
void YBCGenerateAshRootRequestId(unsigned char *root_request_id);
const char* YBCGetWaitEventName(uint32_t wait_event_info);
const char* YBCGetWaitEventClass(uint32_t wait_event_info);
const char* YBCGetWaitEventComponent(uint32_t wait_event_info);
const char* YBCGetWaitEventType(uint32_t wait_event_info);
uint8_t YBCGetQueryIdForCatalogRequests();
int YBCGetRandomUniformInt(int a, int b);
YBCWaitEventDescriptor YBCGetWaitEventDescription(size_t index);

int YBCGetCallStackFrames(void** result, int max_depth, int skip_count);

#ifdef __cplusplus
} // extern "C"
#endif
