#include "duckdb/common/exception.hpp"
#include "pgduckdb/pgduckdb_ddl.hpp"
#include "pgduckdb/pgduckdb_duckdb.hpp"
#include "pgduckdb/pgduckdb_guc.hpp"
#include "pgduckdb/pgduckdb_xact.hpp"
#include "pgduckdb/pgduckdb_hooks.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"
#include "pgduckdb/pgduckdb_background_worker.hpp"

#include "pgduckdb/pg/transactions.hpp"
#include "pgduckdb/utility/cpp_wrapper.hpp"

namespace pgduckdb {

static CommandId next_expected_command_id = FirstCommandId;
static bool top_level_statement = true;

namespace pg {

static bool force_allow_writes;

/*
 * Returns if we're currently in a transaction block. To determine if we are in
 * a function or not, this uses the tracked top_level_statement variable.
 */
bool
IsInTransactionBlock() {
	return IsInTransactionBlock(top_level_statement);
}

/*
 * Throws an error if we're in a transaction block. To determine if we are in
 * a function or not, this uses the tracked top_level_statement variable.
 */
void
PreventInTransactionBlock(const char *statement_type) {
	PreventInTransactionBlock(top_level_statement, statement_type);
}

/*
 * Check if Postgres did any writes.
 *
 * We only update the next_expected_command_id when pgduckdb did a write. This
 * means that Postgres did a write if the GetCurrentCommandId returns another
 * number than the expected one.
 *
 * NOTE: This function can return a false-negative (i.e. Postgres did writes,
 * but this function doesn't return true). This only happens when Postgres
 * already called GetCurrentCommandId() with used=true, but hasn't called
 * CommandCounterIncrement() yet. An easy way to work around this is by calling
 * DidWrites() before Postgres has done the above, so very early in the command
 * handling. Even if you don't it's often not a problem, because we check again
 * at transaction end and also during any next call to ClaimCurrentCommandId().
 */
static bool
DidWrites() {
	return pg::GetCurrentCommandId() > next_expected_command_id;
}

void
SetForceAllowWrites(bool force) {
	force_allow_writes = force;
}

bool
AllowWrites() {
	if (MixedWritesAllowed()) {
		return true;
	}
	return !pgduckdb::ddb::DidWrites();
}

} // namespace pg

bool
MixedWritesAllowed() {
	return !pg::IsInTransactionBlock() || duckdb_unsafe_allow_mixed_transactions || pg::force_allow_writes ||
	       pgduckdb::doing_motherduck_sync;
}

bool
DidDisallowedMixedWrites() {
	return !MixedWritesAllowed() && pg::DidWrites() && ddb::DidWrites();
}

/*
 * Check if both Postgres and DuckDB did writes in this transaction and throw
 * an error if they did.
 */
void
CheckForDisallowedMixedWrites() {
	if (DidDisallowedMixedWrites()) {
		throw duckdb::NotImplementedException(
		    "Writing to DuckDB and Postgres tables in the same transaction block is not supported");
	}
}

/*
 * Claim the current command id as being executed by a DuckDB write query.
 *
 * Postgres increments its command id counter for every write query that
 * happens in a transaction. We use this counter to detect if the transaction
 * wrote to both Postgres and DuckDB within the same transaction. The way we do
 * this is by consuming a command ID for every DuckDB write query that we do
 * and checking that it was only increased by one since the last query. If it
 * ever increases by more than one it means that there was some Postgres query
 * in the middle.
 */
void
ClaimCurrentCommandId(bool force) {
	/*
	 * For INSERT/UPDATE/DELETE statements Postgres will already mark the
	 * command counter as used, but not for writes that occur within a PG
	 * select statement. But it's fine to call GetCurrentCommandId again. We
	 * will get the same command id. Only after a call to
	 * CommandCounterIncrement the next call to GetCurrentCommandId will
	 * receive a new command id.
	 */
	CommandId new_command_id = pg::GetCurrentCommandId(true);

	if (new_command_id != next_expected_command_id && !MixedWritesAllowed() && !force) {
		throw duckdb::NotImplementedException(
		    "Writing to DuckDB and Postgres tables in the same transaction block is not supported");
	}

	pg::CommandCounterIncrement();
	next_expected_command_id = pg::GetCurrentCommandId();
}

/*
 * Mark the current statement as not being a top level statement.
 *
 * This is used to track if a DuckDB query is executed within a Postgres
 * function. If it is, we don't want to autocommit the query, because the
 * function implicitly runs in a transaction.
 *
 * Sadly there's no easy way to request from Postgres whether we're in a top
 * level statement or not. So we have to track this ourselves.
 */
void
MarkStatementNotTopLevel() {
	top_level_statement = false;
}

void
SetStatementTopLevel(bool top_level) {
	top_level_statement = top_level;
}

bool
IsStatementTopLevel() {
	return top_level_statement;
}

/*
 * Trigger Postgres to autocommit single statement queries.
 *
 * We use this as an optimization to avoid the overhead of starting and
 * committing a DuckDB transaction for cases where the user runs only a single
 * query.
 */
void
AutocommitSingleStatementQueries() {
	if (pg::IsInTransactionBlock()) {
		/* We're in a transaction block, we can just execute the query */
		return;
	}

	pg::PreventInTransactionBlock(top_level_statement,
	                              "BUG: You should never see this error we checked IsInTransactionBlock before.");
}

/*
 * Stores the oids of temporary DuckDB tables for this backend. We cannot store
 * these Oids in the duckdb.tables table. This is because these tables are
 * automatically dropped when the backend terminates, but for this type of drop
 * no event trigger is fired. So if we would store these Oids in the
 * duckdb.tables table, then the oids of temporary tables would stay in there
 * after the backend terminates (which would be bad, because the table doesn't
 * exist anymore). To solve this, we store the oids in this in-memory set
 * instead, because that memory will automatically be cleared when the current
 * backend terminates.
 *
 * To make sure that we restore the state of this set preserves transactional
 * semantics, we keep two sets. One for the current transaction and one that it
 * was at the start of the transaction (which we restore in case of rollback).
 */
static bool modified_temporary_duckdb_tables = false;
static std::unordered_set<Oid> temporary_duckdb_tables;
static std::unordered_set<Oid> temporary_duckdb_tables_old;

void
RegisterDuckdbTempTable(Oid relid) {
	if (!modified_temporary_duckdb_tables) {
		modified_temporary_duckdb_tables = true;
		temporary_duckdb_tables_old = temporary_duckdb_tables;
	}
	temporary_duckdb_tables.insert(relid);
}

void
UnregisterDuckdbTempTable(Oid relid) {
	if (!modified_temporary_duckdb_tables) {
		modified_temporary_duckdb_tables = true;
		temporary_duckdb_tables_old = temporary_duckdb_tables;
	}
	temporary_duckdb_tables.erase(relid);
}

bool
IsDuckdbTempTable(Oid relid) {
	return temporary_duckdb_tables.count(relid) > 0;
}

static void
DuckdbXactCallback_Cpp(XactEvent event) {
	/*
	 * We're in a committing phase. Some global variables we modify even when
	 * we're not using DuckDB execution. So we always reset those.
	 */
	top_level_statement = true;
	top_level_duckdb_ddl_type = DDLType::NONE;
	executor_nest_level = 0;

	/* If DuckDB is not initialized there's no need to do anything */
	if (!DuckDBManager::IsInitialized()) {
		return;
	}

	auto connection = DuckDBManager::GetConnectionUnsafe();
	auto &context = *connection->context;

	switch (event) {
	case XACT_EVENT_PRE_COMMIT:
	case XACT_EVENT_PARALLEL_PRE_COMMIT:
		CheckForDisallowedMixedWrites();

		next_expected_command_id = FirstCommandId;
		pg::force_allow_writes = false;
		if (modified_temporary_duckdb_tables) {
			modified_temporary_duckdb_tables = false;
			temporary_duckdb_tables_old.clear();
		}

		if (context.transaction.HasActiveTransaction()) {
			// Commit the DuckDB transaction too
			context.transaction.Commit();
		}
		break;

	case XACT_EVENT_ABORT:
	case XACT_EVENT_PARALLEL_ABORT:
		next_expected_command_id = FirstCommandId;
		pg::force_allow_writes = false;
		if (modified_temporary_duckdb_tables) {
			modified_temporary_duckdb_tables = false;
			/* The transaction failed, so we restore original set of temporary
			 * tables. */
			temporary_duckdb_tables = temporary_duckdb_tables_old;
			temporary_duckdb_tables_old.clear();
		}
		if (context.transaction.HasActiveTransaction()) {
			// Abort the DuckDB transaction too
			context.transaction.Rollback(nullptr);
		}
		break;

	case XACT_EVENT_PREPARE:
	case XACT_EVENT_PRE_PREPARE:
		if (context.transaction.HasActiveTransaction()) {
			// Throw an error for prepare events. We don't support COMMIT PREPARED.
			throw duckdb::NotImplementedException("Prepared transactions are not implemented in DuckDB.");
		}

	case XACT_EVENT_COMMIT:
	case XACT_EVENT_PARALLEL_COMMIT:
		// No action needed for commit event, we already did committed the
		// DuckDB transaction in the PRE_COMMIT event. We don't commit the
		// DuckDB transaction here, because any failure to commit would
		// then turn into a Postgres PANIC (i.e. a crash). To quote the
		// relevant Postgres comment:
		// > Note that if an error is raised here, it's too late to abort
		// > the transaction. This should be just noncritical resource
		// > releasing.
		break;

	default:
		// Fail hard if future PG versions introduce a new event
		throw duckdb::NotImplementedException("Not implemented XactEvent: %d", event);
	}
}

static void
DuckdbXactCallback(XactEvent event, void * /*arg*/) {
	InvokeCPPFunc(DuckdbXactCallback_Cpp, event);
}

/*
 * Throws an error when starting a new subtransaction in a DuckDB transaction.
 * Existing subtransactions are handled at creation of the DuckDB connection.
 * Throwing here for every event type is problematic, because that would also
 * cause a failure in the resulting savepoint abort event. Which in turn would
 * cause the postgres error stack to overflow.
 */
static void
DuckdbSubXactCallback_Cpp(SubXactEvent event) {
	if (!DuckDBManager::IsInitialized()) {
		return;
	}
	auto connection = DuckDBManager::GetConnectionUnsafe();
	auto &context = *connection->context;
	if (!context.transaction.HasActiveTransaction()) {
		return;
	}

	if (event == SUBXACT_EVENT_START_SUB) {
		throw duckdb::NotImplementedException("SAVEPOINT is not supported in DuckDB");
	}
}

static void
DuckdbSubXactCallback(SubXactEvent event, SubTransactionId /*my_subid*/, SubTransactionId /*parent_subid*/,
                      void * /*arg*/) {
	InvokeCPPFunc(DuckdbSubXactCallback_Cpp, event);
}

static bool transaction_handler_configured = false;
void
RegisterDuckdbXactCallback() {
	if (transaction_handler_configured) {
		return;
	}
	pg::RegisterXactCallback(DuckdbXactCallback, nullptr);
	pg::RegisterSubXactCallback(DuckdbSubXactCallback, nullptr);
	transaction_handler_configured = true;
}
} // namespace pgduckdb
