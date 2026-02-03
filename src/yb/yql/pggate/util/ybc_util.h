// Copyright (c) YugabyteDB, Inc.
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

#include "yb/yql/pggate/util/ybc_guc.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

#ifdef __cplusplus
extern "C" {

struct varlena;

#endif

typedef struct YbcStatusStruct* YbcStatus;

bool YBCStatusIsNotFound(YbcStatus s);
bool YBCStatusIsUnknownSession(YbcStatus s);
bool YBCStatusIsDuplicateKey(YbcStatus s);
bool YBCStatusIsSnapshotTooOld(YbcStatus s);
bool YBCStatusIsTryAgain(YbcStatus s);
bool YBCStatusIsAlreadyPresent(YbcStatus s);
bool YBCStatusIsReplicationSlotLimitReached(YbcStatus s);
bool YBCStatusIsFatalError(YbcStatus s);
uint32_t YBCStatusPgsqlError(YbcStatus s);
void YBCFreeStatus(YbcStatus s);

const char* YBCStatusFilename(YbcStatus s);
int YBCStatusLineNumber(YbcStatus s);
const char* YBCStatusFuncname(YbcStatus s);
size_t YBCStatusMessageLen(YbcStatus s);
const char* YBCStatusMessageBegin(YbcStatus s);
const char* YBCMessageAsCString(YbcStatus s);
unsigned int YBCStatusRelationOid(YbcStatus s);
const char** YBCStatusArguments(YbcStatus s, size_t* nargs);

void YBCResolveHostname();

#define CHECKED_YBC_STATUS __attribute__ ((warn_unused_result)) YbcStatus

typedef void* (*YbcPallocFn)(size_t size);

typedef struct varlena* (*YbcCstringToTextWithLenFn)(const char* c, int size);

typedef void* (*YbcSwitchMemoryContextFn)(void* context);

typedef void* (*YbcCreateMemoryContextFn)(void* parent, const char* name);

typedef void (*YbcDeleteMemoryContextFn)(void* context);

// Global initialization of the YugaByte subsystem.
CHECKED_YBC_STATUS YBCInit(
    const char* argv0,
    YbcPallocFn palloc_fn,
    YbcCstringToTextWithLenFn cstring_to_text_with_len_fn,
    YbcSwitchMemoryContextFn switch_mem_context_fn,
    YbcCreateMemoryContextFn create_mem_context_fn,
    YbcDeleteMemoryContextFn delete_mem_context_fn);

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
const char* YBCGetWaitEventAuxDescription(uint32_t wait_event_info);
uint8_t YBCGetConstQueryId(YbcAshConstQueryIdType type);
uint32_t YBCWaitEventForWaitingOnTServer();
int YBCGetRandomUniformInt(int a, int b);
YbcWaitEventDescriptor YBCGetWaitEventDescription(size_t index);
int YBCGetCircularBufferSizeInKiBs();
const char* YBCGetPggateRPCName(uint32_t pggate_rpc_enum_value);
uint32_t YBCAshNormalizeComponentForTServerEvents(uint32_t code, bool component_bits_set);

int YBCGetCallStackFrames(void** result, int max_depth, int skip_count);

bool YBCIsInitDbModeEnvVarSet();

bool YBIsMajorUpgradeInitDb();

const char *YBCGetOutFuncName(YbcPgOid typid);

typedef void (*YbcUpdateInitPostgresMetricsFn)(void);
void YBCSetUpdateInitPostgresMetricsFn(YbcUpdateInitPostgresMetricsFn foo);
void YBCUpdateInitPostgresMetrics();

// Partition key hash decoding helpers
uint16_t YBCDecodeMultiColumnHashLeftBound(const char* partition_key, size_t key_len);
uint16_t YBCDecodeMultiColumnHashRightBound(const char* partition_key, size_t key_len);

bool YBCIsObjectLockingEnabled();
bool YBCIsLegacyModeForCatalogOps();

bool YBCIsAutoAnalyzeEnabled();

#ifdef __cplusplus
} // extern "C"
#endif
