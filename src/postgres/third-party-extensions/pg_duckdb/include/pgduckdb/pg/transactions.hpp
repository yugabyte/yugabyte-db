#pragma once

#include "pgduckdb/pg/declarations.hpp"

extern "C" {
extern bool IsSubTransaction(void);

#define FirstCommandId ((CommandId)0)

/*
 * These enum definitions are vendored in so we can implement a postgres
 * XactCallback in C++. It's not expected that these will ever change.
 */
typedef enum {
	XACT_EVENT_COMMIT,
	XACT_EVENT_PARALLEL_COMMIT,
	XACT_EVENT_ABORT,
	XACT_EVENT_PARALLEL_ABORT,
	XACT_EVENT_PREPARE,
	XACT_EVENT_PRE_COMMIT,
	XACT_EVENT_PARALLEL_PRE_COMMIT,
	XACT_EVENT_PRE_PREPARE,
} XactEvent;

typedef void (*XactCallback)(XactEvent event, void *arg);

typedef enum {
	SUBXACT_EVENT_START_SUB,
	SUBXACT_EVENT_COMMIT_SUB,
	SUBXACT_EVENT_ABORT_SUB,
	SUBXACT_EVENT_PRE_COMMIT_SUB,
} SubXactEvent;

typedef void (*SubXactCallback)(SubXactEvent event, SubTransactionId mySubid, SubTransactionId parentSubid, void *arg);
}

namespace pgduckdb::pg {
void CommandCounterIncrement();
CommandId GetCurrentCommandId(bool used = false);
bool IsInTransactionBlock(bool top_level);
void PreventInTransactionBlock(bool is_top_level, const char *statement_type);
void RegisterXactCallback(XactCallback callback, void *arg);
void UnregisterXactCallback(XactCallback callback, void *arg);
void RegisterSubXactCallback(SubXactCallback callback, void *arg);
void UnregisterSubXactCallback(SubXactCallback callback, void *arg);
} // namespace pgduckdb::pg
