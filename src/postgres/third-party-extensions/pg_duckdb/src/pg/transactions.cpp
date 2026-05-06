#include "pgduckdb/pgduckdb_utils.hpp"

extern "C" {
#include "postgres.h"
#include "access/xact.h" // RegisterXactCallback, XactEvent, SubXactEvent, SubTransactionId
#include "access/xlog.h" // XactLastRecEnd
}

namespace pgduckdb::pg {

CommandId
GetCurrentCommandId(bool used = false) {
	return PostgresFunctionGuard(::GetCurrentCommandId, used);
}

void
CommandCounterIncrement() {
	return PostgresFunctionGuard(::CommandCounterIncrement);
}

bool
IsInTransactionBlock(bool is_top_level) {
	return PostgresFunctionGuard(::IsInTransactionBlock, is_top_level);
}

void
PreventInTransactionBlock(bool is_top_level, const char *statement_type) {
	return PostgresFunctionGuard(::PreventInTransactionBlock, is_top_level, statement_type);
}

void
RegisterXactCallback(XactCallback callback, void *arg) {
	return PostgresFunctionGuard(::RegisterXactCallback, callback, arg);
}

void
UnregisterXactCallback(XactCallback callback, void *arg) {
	return PostgresFunctionGuard(::UnregisterXactCallback, callback, arg);
}

void
RegisterSubXactCallback(SubXactCallback callback, void *arg) {
	return PostgresFunctionGuard(::RegisterSubXactCallback, callback, arg);
}

void
UnregisterSubXactCallback(SubXactCallback callback, void *arg) {
	return PostgresFunctionGuard(::UnregisterSubXactCallback, callback, arg);
}

} // namespace pgduckdb::pg
