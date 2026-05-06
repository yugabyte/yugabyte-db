#include "duckdb.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parsed_data/create_info.hpp"
#include "duckdb/common/unordered_set.hpp"
#include "duckdb/parser/column_definition.hpp"
#include "duckdb/parser/constraint.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/catalog/catalog_entry/column_dependency_manager.hpp"
#include "duckdb/parser/column_list.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/storage/table_storage_info.hpp"
#include "duckdb/main/attached_database.hpp"
#include "pgduckdb/pgduckdb_ddl.hpp"
#include "pgduckdb/pgduckdb_fdw.hpp"
#include "pgduckdb/pgduckdb_types.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"
#include "pgduckdb/pg/relations.hpp"
#include "pgduckdb/utility/cpp_wrapper.hpp"
#include "pgduckdb/pg/string_utils.hpp"
#include <string>
#include <unordered_map>
#include <sys/file.h>
#include <fcntl.h>

extern "C" {
#include "postgres.h"
#include "access/xact.h"
#include "catalog/dependency.h"
#include "catalog/namespace.h"
#include "catalog/objectaddress.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_class.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_namespace.h"
#include "commands/dbcommands.h"
#include "common/file_utils.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "storage/shmem.h"
#include "tcop/tcopprot.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/palloc.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
}

#include "pgduckdb/pgduckdb.h"
#include "pgduckdb/pgduckdb_guc.hpp"
#include "pgduckdb/pgduckdb_duckdb.hpp"
#include "pgduckdb/pgduckdb_background_worker.hpp"
#include "pgduckdb/pgduckdb_metadata_cache.hpp"
#include "pgduckdb/pgduckdb_userdata_cache.hpp"

static std::unordered_map<std::string, std::string> last_known_motherduck_catalog_versions;
static duckdb::unique_ptr<duckdb::Connection> ddb_connection = nullptr;
static uint64_t initial_cache_version = 0;

namespace pgduckdb {

bool is_background_worker = false;
/*
 * This stores a UUIDv4 that's generated on Postgres statrup. This cannot be a
 * hardcoded token otherwise you could get contention on the matching
 * MotherDuck read-instance if multiple pg_duckdb instances connect to the same
 * MotherDuck account.
 *
 * The length is 37, because a UUID has 36 characters and we need to add a NULL
 * byte.
 */
static char bgw_session_hint[37];

/* Did this backend reuse the session hint of the background worker? */
static bool reused_bgw_session_hint = false;
/*
 * For some reason we cannot configure the before_shmem_exit hook in _PG_init,
 * nor in shmem_startup_hook. So instead we configure it lazily for a backend
 * whenever it is needed. We need to make sure we only do that at most once for
 * each backend though. So this boolean keeps track of that.
 */
static bool set_up_unclaim_session_hint_hook = false;

void SyncMotherDuckCatalogsWithPg(bool drop_with_cascade, duckdb::ClientContext &context);
void SyncMotherDuckCatalogsWithPg_Cpp(bool drop_with_cascade, duckdb::ClientContext &context);

typedef struct BgwStatePerDB {
	Oid database_oid;

	int64 activity_count; /* the number of times activity was triggered by other backends */

	bool bgw_session_hint_is_reused;

	Latch *latch;
} BgwStatePerDB;

typedef struct BackgroundWorkerShmemStruct {
	slock_t lock; /* protects all the fields below */

	HTAB *statePerDB; /* Map of Database Oid -> {Latch, activity count, etc.} */
} BackgroundWorkerShmemStruct;

static BackgroundWorkerShmemStruct *BgwShmemStruct;

/*
MUST be called under a lock
Get the BGW state for the current database (MyDatabaseId)
*/
BgwStatePerDB *
FindState() {
	bool found = false;
	auto state = (BgwStatePerDB *)hash_search(BgwShmemStruct->statePerDB, &MyDatabaseId, HASH_FIND, &found);
	return found ? state : nullptr;
}

BgwStatePerDB *
GetState() {
	Assert(is_background_worker);
	auto state = FindState();
	if (!state) {
		SpinLockRelease(&BgwShmemStruct->lock);
		elog(ERROR, "pg_duckdb background worker: could not find state for database %u", MyDatabaseId);
	}
	return state;
}

static void
BackgroundWorkerCheck(duckdb::Connection &connection, int64_t &last_activity_count) {
	SpinLockAcquire(&BgwShmemStruct->lock);
	int64_t new_activity_count = GetState()->activity_count;
	SpinLockRelease(&BgwShmemStruct->lock);
	if (last_activity_count != new_activity_count) {
		last_activity_count = new_activity_count;
		/* Trigger some activity to restart the syncing */
		pgduckdb::DuckDBQueryOrThrow(connection, "FROM duckdb_tables() limit 0");
	}

	/*
	 * If the extension is not registerid this loop is a no-op, which
	 * means we essentially keep polling until the extension is
	 * installed
	 */
	pgduckdb::SyncMotherDuckCatalogsWithPg_Cpp(false, *connection.context);
}

bool CanTakeBgwLockForDatabase(Oid database_oid);

bool
RunOneCheck(int64_t &last_activity_count) {
	// No need to run if MD is not enabled.
	if (!IsMotherDuckEnabled()) {
		elog(LOG, "pg_duckdb background worker: MotherDuck is not enabled, will exit.");
		return true; // should exit
	}

	if (!IsExtensionRegistered()) {
		return false; // XXX: shouldn't we exit actually?
	}

	if (!ddb_connection) {
		try {
			ddb_connection = DuckDBManager::CreateConnection();
		} catch (std::exception &ex) {
			elog(LOG, "pg_duckdb background worker: failed to create connection: %s", ex.what());
			return true; // should exit
		}
	}

	InvokeCPPFunc(pgduckdb::BackgroundWorkerCheck, *ddb_connection, last_activity_count);
	return false;
}

void
SetBackgroundWorkerState(Oid database_oid) {
	auto state = (BgwStatePerDB *)hash_search(BgwShmemStruct->statePerDB, &database_oid, HASH_ENTER, NULL);
	state->latch = MyLatch;
	state->activity_count = 0;
	state->bgw_session_hint_is_reused = false;
}

void
BgwMainLoop() {
	elog(LOG, "pg_duckdb background worker: starting");

	char *db_name = nullptr;
	{
		StartTransactionCommand();
		db_name = strdup(get_database_name(MyDatabaseId));
		CommitTransactionCommand();
		elog(LOG, "pg_duckdb background worker: started for database '%s' (%u)", db_name, MyDatabaseId);
	}

	doing_motherduck_sync = true;
	is_background_worker = true;

	int64_t last_activity_count = -1; // force a check on the first iteration

	while (true) {
		// Initialize SPI (Server Programming Interface) and connect to the database
		SetCurrentStatementStartTimestamp();
		StartTransactionCommand();
		SPI_connect();
		PushActiveSnapshot(GetTransactionSnapshot());

		const bool should_exit = RunOneCheck(last_activity_count);

		// Commit the transaction
		PopActiveSnapshot();
		SPI_finish();
		CommitTransactionCommand();

		if (should_exit) {
			break;
		}

		pgstat_report_stat(false);
		pgstat_report_activity(STATE_IDLE, NULL);

		// Wait for a second or until the latch is set
		WaitLatch(MyLatch, WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH, 1000L, PG_WAIT_EXTENSION);
		CHECK_FOR_INTERRUPTS();
		ResetLatch(MyLatch);
	}

	ddb_connection.reset();
	DuckDBManager::Reset();

	elog(LOG, "pg_duckdb background worker for database '%s' (%u) has now terminated.", db_name, MyDatabaseId);
}

} // namespace pgduckdb

extern "C" {

PGDLLEXPORT void
pgduckdb_background_worker_main(Datum main_arg) {
	Oid database_oid = DatumGetObjectId(main_arg);
	if (!pgduckdb::CanTakeBgwLockForDatabase(database_oid)) {
		elog(LOG, "pg_duckdb background worker: could not take lock for database %u. Will exit.", database_oid);
		return;
	}

	// Set up a signal handler for SIGTERM
	pqsignal(SIGTERM, die);
	BackgroundWorkerUnblockSignals();

	BackgroundWorkerInitializeConnectionByOid(database_oid, InvalidOid, 0);

	SpinLockAcquire(&pgduckdb::BgwShmemStruct->lock);
	pgduckdb::SetBackgroundWorkerState(database_oid);
	SpinLockRelease(&pgduckdb::BgwShmemStruct->lock);

	PG_TRY();
	{
		pgduckdb::BgwMainLoop();
	}
	PG_FINALLY();
	{
		// Remove state entry.
		SpinLockAcquire(&pgduckdb::BgwShmemStruct->lock);
		hash_search(pgduckdb::BgwShmemStruct->statePerDB, &MyDatabaseId, HASH_REMOVE, NULL);
		SpinLockRelease(&pgduckdb::BgwShmemStruct->lock);
	}
	PG_END_TRY();
}

PG_FUNCTION_INFO_V1(force_motherduck_sync);
Datum
force_motherduck_sync(PG_FUNCTION_ARGS) {
	Datum drop_with_cascade = PG_GETARG_BOOL(0);
	/* clear the cache of known catalog versions to force a full sync */
	last_known_motherduck_catalog_versions.clear();

	/*
	 * We don't use GetConnection, because we want to be able to precisely
	 * control the transaction lifecycle. We commit Postgres connections
	 * throughout this function, and the GetConnect its cached connection its
	 * lifecycle would be linked to those postgres transactions, which we
	 * don't want.
	 */
	auto connection = pgduckdb::DuckDBManager::Get().CreateConnection();
	SPI_connect_ext(SPI_OPT_NONATOMIC);
	PG_TRY();
	{
		pgduckdb::doing_motherduck_sync = true;
		pgduckdb::SyncMotherDuckCatalogsWithPg(drop_with_cascade, *connection->context);
	}
	PG_FINALLY();
	{
		pgduckdb::doing_motherduck_sync = false;
	}
	PG_END_TRY();
	SPI_finish();
	PG_RETURN_VOID();
}
}

namespace pgduckdb {
#if PG_VERSION_NUM >= 150000
static shmem_request_hook_type prev_shmem_request_hook = NULL;
#endif
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

/*
 * shmem_request hook: request additional shared resources.  We'll allocate or
 * attach to the shared resources in pgss_shmem_startup().
 */
static void
ShmemRequest(void) {
#if PG_VERSION_NUM >= 150000
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();
#endif

	RequestAddinShmemSpace(sizeof(BackgroundWorkerShmemStruct));
}

/*
 * CheckpointerShmemInit
 *		Allocate and initialize checkpointer-related shared memory
 */
static void
ShmemStartup(void) {
	if (prev_shmem_startup_hook) {
		prev_shmem_startup_hook();
	}

	Size size = sizeof(BackgroundWorkerShmemStruct);
	bool found;

	/*
	 * Create or attach to the shared memory state, including hash table
	 */
	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	BgwShmemStruct = (BackgroundWorkerShmemStruct *)ShmemInitStruct("DuckdbBackgroundWorker Data", size, &found);

	if (!found) {
		/*
		 * First time through, so initialize.  Note that we zero the whole
		 * requests array; this is so that CompactCheckpointerRequestQueue can
		 * assume that any pad bytes in the request structs are zeroes.
		 */
		MemSet(BgwShmemStruct, 0, size);
		SpinLockInit(&BgwShmemStruct->lock);

		HASHCTL info;
		info.keysize = sizeof(Oid);
		info.entrysize = sizeof(BgwStatePerDB);
		BgwShmemStruct->statePerDB =
		    ShmemInitHash("ProcBgwStatePerDB", 1, max_worker_processes, &info, HASH_ELEM | HASH_BLOBS);
	}

	LWLockRelease(AddinShmemInitLock);
}

constexpr const char *PGDUCKDB_SYNC_WORKER_NAME = "pg_duckdb sync worker";

bool
HasBgwRunningForMyDatabase() {
	const auto num_backends = pgstat_fetch_stat_numbackends();
	for (int backend_idx = 1; backend_idx <= num_backends; ++backend_idx) {
#if PG_VERSION_NUM >= 140000 && PG_VERSION_NUM < 160000
		PgBackendStatus *beentry = pgstat_fetch_stat_beentry(backend_idx);
#else
		LocalPgBackendStatus *local_beentry = pgstat_get_local_beentry_by_index(backend_idx);
		PgBackendStatus *beentry = &local_beentry->backendStatus;
#endif
		if (beentry->st_databaseid == InvalidOid) {
			continue; // backend is not connected to a database
		}

		auto datid = ObjectIdGetDatum(beentry->st_databaseid);
		if (datid != MyDatabaseId) {
			continue; // backend is connected to a different database
		}

		auto backend_type = GetBackgroundWorkerTypeByPid(beentry->st_procpid);
		if (!backend_type || strcmp(backend_type, PGDUCKDB_SYNC_WORKER_NAME) != 0) {
			continue; // backend is not a pg_duckdb sync worker
		}

		return true;
	}

	return false;
}

/*
Attempts to take a lock on a file named 'pgduckdb_worker_<database_oid>.lock'
If the lock is taken, the function returns true. If the lock is not taken, the function returns false.
*/
bool
CanTakeBgwLockForDatabase(Oid database_oid) {
	char lock_file_name[MAXPGPATH];
	snprintf(lock_file_name, MAXPGPATH, "%s/%s.pgduckdb_worker.%d", DataDir, PG_TEMP_FILE_PREFIX, database_oid);

	auto fd = open(lock_file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		elog(ERROR, "Could not open file '%s': %m", lock_file_name);
	}

	// Take exclusive lock on the file
	auto ret = flock(fd, LOCK_EX | LOCK_NB);
	if (ret != 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			return false;
		}

		elog(ERROR, "Could not take lock on file '%s': %m", lock_file_name);
	}

	return true;
}

void
UnclaimBgwSessionHint(int /*code*/, Datum /*arg*/) {
	if (!reused_bgw_session_hint) {
		return;
	}

	SpinLockAcquire(&BgwShmemStruct->lock);
	auto *state = FindState();
	if (state) {
		state->bgw_session_hint_is_reused = false;
	}
	SpinLockRelease(&BgwShmemStruct->lock);
	reused_bgw_session_hint = false;
}

void
InitBackgroundWorkersShmem(void) {
	/* Set up the shared memory hooks */
#if PG_VERSION_NUM >= 150000
	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = ShmemRequest;
#else
	ShmemRequest();
#endif

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = ShmemStartup;

	Datum random_uuid = DirectFunctionCall1(gen_random_uuid, 0);
	Datum uuid_datum = DirectFunctionCall1(uuid_out, random_uuid);
	char *uuid_cstr = DatumGetCString(uuid_datum);
	strcpy(bgw_session_hint, uuid_cstr);
}

/*
Will start the background worker if:
- MotherDuck is enabled (TODO: should be database-specific)
- it is not already running for the current PG database
*/
void
StartBackgroundWorkerIfNeeded(void) {
	if (!pgduckdb::IsMotherDuckEnabled()) {
		elog(DEBUG3, "pg_duckdb background worker not started because MotherDuck is not enabled");
		return;
	}

	if (HasBgwRunningForMyDatabase()) {
		elog(DEBUG3, "pg_duckdb background worker already running for database %u", MyDatabaseId);
		return;
	}

	BackgroundWorker worker;
	// Set up the worker struct
	MemSet(&worker, 0, sizeof(BackgroundWorker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_duckdb");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "pgduckdb_background_worker_main");
	snprintf(worker.bgw_name, BGW_MAXLEN, PGDUCKDB_SYNC_WORKER_NAME);
	worker.bgw_restart_time = 1;
	worker.bgw_main_arg = ObjectIdGetDatum(MyDatabaseId);

	// Register the worker
	RegisterDynamicBackgroundWorker(&worker, NULL);
}

void
TriggerActivity(void) {
	if (!IsMotherDuckEnabled()) {
		return;
	}

	SpinLockAcquire(&BgwShmemStruct->lock);
	auto state = FindState();
	if (state) {
		state->activity_count++;
		SetLatch(state->latch);
	}
	SpinLockRelease(&BgwShmemStruct->lock);
}

/*
 * When motherduck read scaling is used we don't want to have the background
 * worker use a dedicated read-scaling instance. Instead we want to have it
 * share an instance with the backend that's doing the actual query. We do this
 * having a single normal backend use the same session_hint as the background
 * worker. This function decides if that's necessary and returns the
 * session_hint that the background uses.
 *
 * If it's not necessary this returns the empty string.
 */
const char *
PossiblyReuseBgwSessionHint(void) {
	if (is_background_worker || reused_bgw_session_hint) {
		return bgw_session_hint;
	}

	const char *result = "";
	SpinLockAcquire(&BgwShmemStruct->lock);
	auto state = FindState();
	if (state && !state->bgw_session_hint_is_reused) {
		result = bgw_session_hint;
		state->bgw_session_hint_is_reused = true;
		reused_bgw_session_hint = true;
	}
	SpinLockRelease(&BgwShmemStruct->lock);

	if (reused_bgw_session_hint && !set_up_unclaim_session_hint_hook) {
		before_shmem_exit(UnclaimBgwSessionHint, 0);
	}

	return result;
}

/* Global variables that are used to communicate with our event triggers so
 * they handle DDL of syncing differently than user-initiated DDL */
bool doing_motherduck_sync;
char *current_motherduck_catalog_version;

std::string
PgSchemaName(const std::string &db_name, const std::string &schema_name, bool is_default_db) {
	if (is_default_db) {
		/*
		 * We map the "main" DuckDB schema of the default database to the
		 * "public" Postgres schema, because they are pretty much equivalent
		 * from a user perspective even though they are named differently.
		 */
		return schema_name == "main" ? "public" : schema_name;
	}

	std::ostringstream oss;
	oss << "ddb$" << duckdb::KeywordHelper::EscapeQuotes(db_name, '$');
	if (schema_name != "main") {
		oss << "$" << duckdb::KeywordHelper::EscapeQuotes(schema_name, '$');
	}
	return oss.str();
}

std::string
DropPgRelationString(const char *postgres_schema_name, const char *relation_name, char relation_kind,
                     bool with_cascade) {
	std::ostringstream oss;
	oss << "DROP ";
	if (relation_kind == RELKIND_VIEW) {
		oss << "VIEW ";
	} else {
		oss << "TABLE ";
	}
	oss << duckdb::KeywordHelper::WriteQuoted(postgres_schema_name, '"');
	oss << ".";
	oss << duckdb::KeywordHelper::WriteQuoted(relation_name, '"');
	if (with_cascade) {
		oss << " CASCADE";
	}
	oss << "; ";
	return oss.str();
}

std::string
CreatePgViewString(duckdb::CreateViewInfo &info, bool is_default_db) {
	std::ostringstream oss;

	oss << "CREATE VIEW ";
	std::string schema_name = PgSchemaName(info.catalog, info.schema, is_default_db);
	oss << duckdb::KeywordHelper::WriteQuoted(schema_name, '"');
	oss << ".";
	oss << duckdb::KeywordHelper::WriteQuoted(info.view_name, '"');
	if (!info.aliases.empty()) {
		oss << " (";
		oss << duckdb::StringUtil::Join(info.aliases, info.aliases.size(), ", ", [](const std::string &name) {
			return duckdb::KeywordHelper::WriteQuoted(name, '"');
		});
		oss << ")";
	}
	oss << " AS SELECT ";
	auto it_names = info.names.begin();
	auto it_types = info.types.begin();

	bool first = true;

	for (; it_names != info.names.end() && it_types != info.types.end(); it_names++, it_types++) {
		Oid postgres_type = GetPostgresDuckDBType(*it_types);
		if (postgres_type == InvalidOid) {
			elog(WARNING, "Skipping column %s in table %s.%s.%s due to unsupported type", it_names->c_str(),
			     info.catalog.c_str(), info.schema.c_str(), info.view_name.c_str());
			continue;
		}
		if (!first) {
			oss << ", ";
		} else {
			first = false;
		}

		oss << "r[" << duckdb::KeywordHelper::WriteQuoted(*it_names, '\'') << "]::";
		int32_t typemod = GetPostgresDuckDBTypemod(*it_types);
		oss << format_type_with_typemod(postgres_type, typemod);
		oss << " AS " << duckdb::KeywordHelper::WriteQuoted(*it_names, '"');
	}

	if (first) {
		elog(WARNING, "Skipping view %s.%s.%s because none of its columns had supported types", info.catalog.c_str(),
		     info.schema.c_str(), info.view_name.c_str());
		return "";
	}

	oss << " FROM duckdb.view(";
	oss << duckdb::KeywordHelper::WriteQuoted(info.catalog) << ", ";
	oss << duckdb::KeywordHelper::WriteQuoted(info.schema) << ", ";
	oss << duckdb::KeywordHelper::WriteQuoted(info.view_name) << ", ";
	oss << duckdb::KeywordHelper::WriteQuoted(info.query->ToString(), '\'');
	oss << ") r;";
	return oss.str();
}

std::string
CreatePgTableString(duckdb::CreateTableInfo &info, bool is_default_db) {
	std::ostringstream oss;

	oss << "CREATE TABLE ";
	std::string schema_name = PgSchemaName(info.catalog, info.schema, is_default_db);
	oss << duckdb::KeywordHelper::WriteQuoted(schema_name, '"');
	oss << ".";
	oss << duckdb::KeywordHelper::WriteQuoted(info.table, '"');
	oss << "(";

	bool first = true;
	for (auto &column : info.columns.Logical()) {
		Oid postgres_type = GetPostgresDuckDBType(column.Type());
		if (postgres_type == InvalidOid) {

			continue;
		}

		if (first) {
			first = false;
		} else {
			oss << ", ";
		}

		oss << duckdb::KeywordHelper::WriteQuoted(column.Name(), '"');
		oss << " ";
		int32_t typemod = GetPostgresDuckDBTypemod(column.Type());
		oss << format_type_with_typemod(postgres_type, typemod);
	}

	if (first) {
		elog(WARNING, "Skipping table %s.%s.%s because non of its columns had supported types", info.catalog.c_str(),
		     info.schema.c_str(), info.table.c_str());
		return "";
	}

	oss << ") USING duckdb;";
	return oss.str();
}

static std::string
CreatePgSchemaString(std::string postgres_schema_name) {
	return "CREATE SCHEMA " + duckdb::KeywordHelper::WriteQuoted(postgres_schema_name, '"') + ";";
}

/*
 * This function is a workaround for the fact that SPI_commit() does not work
 * well in background workers. You get weird errors like:
 * ERROR: portal snapshots (0) did not account for all active snapshots (1)
 *
 * Luckily we don't really care, all we really care about is that we quickly
 * release the heavy locks we hold on tables after dropping/creating them. And
 * this can easily be done by simply finishing the transaction and opening a
 * new one.
 *
 * This workaround is only necessary in background workers, in normal sessions
 * where people call force_motherduck_sync wo still want to use SPI_commit().
 *
 * See the following thread for details:
 * https://www.postgresql.org/message-id/flat/CAFcNs%2Bp%2BfD5HEXEiZMZC1COnXkJCMnUK0%3Dr4agmZP%3D9Hi%2BYcJA%40mail.gmail.com
 */
void
SPI_commit_that_works_in_bgworker() {
	if (is_background_worker) {
		SPI_finish();
		PopActiveSnapshot();
		CommitTransactionCommand();
		StartTransactionCommand();
		SPI_connect();
		PushActiveSnapshot(GetTransactionSnapshot());
	} else {
		SPI_commit();
	}
	if (initial_cache_version != pgduckdb::CacheVersion()) {
		if (is_background_worker) {
			elog(ERROR, "DuckDB cache version changed during sync, aborting sync, background worker will restart "
			            "automatically");
		} else {
			elog(ERROR, "DuckDB cache version changed during sync, aborting sync, please try again");
		}
	}
}

/*
 * This function runs a utility command in the current SPI context. Instead of
 * throwing an ERROR on failure this will throw a WARNING and return false. It
 * does so in a way that preserves the current transaction state, so that other
 * commands can still be run.
 */
static bool
SPI_run_utility_command(const char *query) {
	MemoryContext old_context = CurrentMemoryContext;
	int ret;
	/*
	 * We create a subtransaction to be able to cleanly roll back in case of
	 * any errors.
	 */
	BeginInternalSubTransaction(NULL);
	PG_TRY();
	{
		ret = SPI_exec(query, 0);
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(old_context);
		ErrorData *edata = CopyErrorData();
		edata->elevel = WARNING;
		ThrowErrorData(edata);
		FreeErrorData(edata);
		FlushErrorState();
		RollbackAndReleaseCurrentSubTransaction();
		return false;
	}
	PG_END_TRY();

	if (ret != SPI_OK_UTILITY) {
		elog(WARNING, "SPI_execute failed: error code %d", ret);
		RollbackAndReleaseCurrentSubTransaction();
		return false;
	}
	/* Success, so we commit the subtransaction */
	ReleaseCurrentSubTransaction();
	return true;
}

static bool
CreateView(const char *postgres_schema_name, const char *view_name, const char *create_view_query,
           bool drop_with_cascade) {
	/* -1 is for the NULL terminator */
	if (strlen(view_name) > NAMEDATALEN - 1) {
		ereport(WARNING, (errmsg("Skipping sync of MotherDuck view '%s' because its name is too long", view_name),
		                  errhint("The maximum length of a view name is %d characters", NAMEDATALEN - 1)));
		return false;
	}

	/*
	 * We need to fetch this over-and-over again, because we commit the
	 * transaction and thus release locks. So in theory the schema could be
	 * deleted/renamed etc.
	 */
	Oid schema_oid = get_namespace_oid(postgres_schema_name, false);
	HeapTuple tuple = SearchSysCache2(RELNAMENSP, CStringGetDatum(view_name), ObjectIdGetDatum(schema_oid));

	bool did_delete_table = false;
	if (HeapTupleIsValid(tuple)) {
		Form_pg_class postgres_relation = (Form_pg_class)GETSTRUCT(tuple);
		/* The table already exists in Postgres, so we cannot simply create it. */

		if (!IsMotherDuckTable(postgres_relation) && !IsMotherDuckView(postgres_relation)) {
			/*
			 * Oops, we have a conflict. Let's notify the user, and
			 * not do anything else
			 */
			elog(WARNING,
			     "Skipping sync of MotherDuck view %s.%s because its name conflicts with an "
			     "already existing table/view/index in Postgres",
			     postgres_schema_name, view_name);
			ReleaseSysCache(tuple);
			return false;
		}

		char relation_kind = postgres_relation->relkind;

		ReleaseSysCache(tuple);

		/*
		 * It's an old version of this DuckDB table, we can safely
		 * drop it and recreate it.
		 */
		std::string drop_table_query =
		    DropPgRelationString(postgres_schema_name, view_name, relation_kind, drop_with_cascade);

		/* We use this to roll back the drop if the CREATE after fails */
		BeginInternalSubTransaction(NULL);

		/* Revert back to original privileges */
		if (!SPI_run_utility_command(drop_table_query.c_str())) {

			ereport(WARNING, (errmsg("Failed to sync MotherDuck view %s.%s", postgres_schema_name, view_name),

			                  errdetail("While executing command: %s", create_view_query),
			                  errhint("See previous WARNING for details")));
			/*
			 * Rollback the subtransaction to clean up the subtransaction
			 * state. Even though there's nothing actually in it. So we could
			 * we could just as well commit it, but rolling back seems more
			 * sensible.
			 */
			RollbackAndReleaseCurrentSubTransaction();
			return false;
		}

		did_delete_table = true;
	}

	Oid saved_userid;
	int sec_context;
	GetUserIdAndSecContext(&saved_userid, &sec_context);
	SetUserIdAndSecContext(MotherDuckPostgresUserOid(), sec_context | SECURITY_LOCAL_USERID_CHANGE);
	bool create_table_succeeded = SPI_run_utility_command(create_view_query);
	SetUserIdAndSecContext(saved_userid, sec_context);
	/* Revert back to original privileges */
	if (!create_table_succeeded) {

		ereport(WARNING, (errmsg("Failed to sync MotherDuck view %s.%s", postgres_schema_name, view_name),

		                  errdetail("While executing command: %s", create_view_query),
		                  errhint("See previous WARNING for details")));
		if (did_delete_table) {
			/* Rollback the drop that succeeded */
			RollbackAndReleaseCurrentSubTransaction();
		}
		return false;
	}

	if (did_delete_table) {
		/*
		 * Commit the subtransaction that contains both the drop and the create that
		 * contains the actual table creation.
		 */
		ReleaseCurrentSubTransaction();
	}

	/* And then we also commit the actual transaction to release any locks that
	 * were necessary to execute it. */
	SPI_commit_that_works_in_bgworker();
	return true;
}

static bool
CreateTable(const char *postgres_schema_name, const char *table_name, const char *create_table_query,
            bool drop_with_cascade) {
	/* -1 is for the NULL terminator */
	if (strlen(table_name) > NAMEDATALEN - 1) {
		ereport(WARNING, (errmsg("Skipping sync of MotherDuck table '%s' because its name is too long", table_name),
		                  errhint("The maximum length of a table name is %d characters", NAMEDATALEN - 1)));
		return false;
	}

	/*
	 * We need to fetch this over-and-over again, because we commit the
	 * transaction and thus release locks. So in theory the schema could be
	 * deleted/renamed etc.
	 */
	Oid schema_oid = get_namespace_oid(postgres_schema_name, false);
	HeapTuple tuple = SearchSysCache2(RELNAMENSP, CStringGetDatum(table_name), ObjectIdGetDatum(schema_oid));

	bool did_delete_table = false;
	if (HeapTupleIsValid(tuple)) {
		Form_pg_class postgres_relation = (Form_pg_class)GETSTRUCT(tuple);
		/* The table already exists in Postgres, so we cannot simply create it. */

		if (!IsMotherDuckTable(postgres_relation) && !IsMotherDuckView(postgres_relation)) {
			/*
			 * Oops, we have a conflict. Let's notify the user, and
			 * not do anything else
			 */
			elog(WARNING,
			     "Skipping sync of MotherDuck table %s.%s because its name conflicts with an "
			     "already existing table/view/index in Postgres",
			     postgres_schema_name, table_name);
			ReleaseSysCache(tuple);
			return false;
		}

		char relation_kind = postgres_relation->relkind;

		ReleaseSysCache(tuple);

		/*
		 * It's an old version of this DuckDB table, we can safely
		 * drop it and recreate it.
		 */
		std::string drop_table_query =
		    DropPgRelationString(postgres_schema_name, table_name, relation_kind, drop_with_cascade);

		/* We use this to roll back the drop if the CREATE after fails */
		BeginInternalSubTransaction(NULL);

		/* Revert back to original privileges */
		if (!SPI_run_utility_command(drop_table_query.c_str())) {

			ereport(WARNING, (errmsg("Failed to sync MotherDuck table %s.%s", postgres_schema_name, table_name),

			                  errdetail("While executing command: %s", create_table_query),
			                  errhint("See previous WARNING for details")));
			/*
			 * Rollback the subtransaction to clean up the subtransaction
			 * state. Even though there's nothing actually in it. So we could
			 * we could just as well commit it, but rolling back seems more
			 * sensible.
			 */
			RollbackAndReleaseCurrentSubTransaction();
			return false;
		}

		did_delete_table = true;
	}

	Oid saved_userid;
	int sec_context;
	GetUserIdAndSecContext(&saved_userid, &sec_context);
	SetUserIdAndSecContext(MotherDuckPostgresUserOid(), sec_context | SECURITY_LOCAL_USERID_CHANGE);
	bool create_table_succeeded = SPI_run_utility_command(create_table_query);
	SetUserIdAndSecContext(saved_userid, sec_context);
	/* Revert back to original privileges */
	if (!create_table_succeeded) {

		ereport(WARNING, (errmsg("Failed to sync MotherDuck table %s.%s", postgres_schema_name, table_name),

		                  errdetail("While executing command: %s", create_table_query),
		                  errhint("See previous WARNING for details")));
		if (did_delete_table) {
			/* Rollback the drop that succeeded */
			RollbackAndReleaseCurrentSubTransaction();
		}
		return false;
	}

	if (did_delete_table) {
		/*
		 * Commit the subtransaction that contains both the drop and the create that
		 * contains the actual table creation.
		 */
		ReleaseCurrentSubTransaction();
	}

	/* And then we also commit the actual transaction to release any locks that
	 * were necessary to execute it. */
	SPI_commit_that_works_in_bgworker();
	return true;
}

static bool
DropRelation(const char *fully_qualified_table, char relation_kind, bool drop_with_cascade) {
	const char *relkind_string = "TABLE";
	if (relation_kind == RELKIND_VIEW) {
		relkind_string = "VIEW";
	}
	const char *query =
	    psprintf("DROP %s %s%s", relkind_string, fully_qualified_table, drop_with_cascade ? " CASCADE" : "");

	if (!SPI_run_utility_command(query)) {
		ereport(WARNING,
		        (errmsg("Failed to drop deleted MotherDuck table %s", fully_qualified_table),

		         errdetail("While executing command: %s", query), errhint("See previous WARNING for details")));
		return false;
	}
	/*
	 * We explicitly don't call SPI_commit_that_works_in_background_worker
	 * here, because that makes transactional considerations easier. And when
	 * deleting tables, it doesn't matter how long we keep locks on them,
	 * because they are already deleted upstream so there can be no queries on
	 * them anyway.
	 */
	return true;
}

/*
 * We should grant access on schemas that we want to create tables in.
 *
 * Returns if access was granted successfully, in some cases we don't do this
 * for security reasons.
 */
static bool
GrantAccessToSchema(const char *postgres_schema_name) {
	/*
	 * Grant full access to the schema to the motherduck postgres user so that
	 * it can create and drop the tables.
	 */
	const char *grant_query = psprintf("GRANT ALL ON SCHEMA %s TO %s", quote_identifier(postgres_schema_name),
	                                   quote_identifier(MotherDuckPostgresUserName()));
	if (!SPI_run_utility_command(grant_query)) {
		ereport(WARNING,
		        (errmsg("Failed to grant access to MotherDuck schema %s", postgres_schema_name),
		         errdetail("While executing command: %s", grant_query), errhint("See previous WARNING for details")));
		return false;
	}

	/*
	 * Grant USAGE on the schema to duckdb.postgres_role so that members of
	 * that role can SELECT from the synced tables. The MotherDuckPostgresUser
	 * owns the tables, but regular users who are members of duckdb.postgres_role
	 * need USAGE on the schema to access them.
	 */
	if (!IsEmptyString(duckdb_postgres_role) && !AreStringEqual(duckdb_postgres_role, MotherDuckPostgresUserName())) {
		grant_query = psprintf("GRANT USAGE ON SCHEMA %s TO %s", quote_identifier(postgres_schema_name),
		                       quote_identifier(duckdb_postgres_role));
		if (!SPI_run_utility_command(grant_query)) {
			ereport(WARNING, (errmsg("Failed to grant USAGE on MotherDuck schema %s to %s", postgres_schema_name,
			                         duckdb_postgres_role),
			                  errdetail("While executing command: %s", grant_query),
			                  errhint("See previous WARNING for details")));
			return false;
		}
	}

	return true;
}

static bool
CreateSchemaIfNotExists(const char *postgres_schema_name, bool is_default_db) {
	/* -1 is for the NULL terminator */
	if (strlen(postgres_schema_name) > NAMEDATALEN - 1) {
		ereport(WARNING,
		        (errmsg("Skipping sync of MotherDuck schema '%s' because its name is too long", postgres_schema_name),
		         errhint("The maximum length of a schema name is %d characters", NAMEDATALEN - 1)));
		return false;
	}

	Oid schema_oid = get_namespace_oid(postgres_schema_name, true);
	if (schema_oid != InvalidOid) {
		/*
		 * Let's check if the MotherDuck Postgres user can actually create
		 * tables in this schema. Surprisingly the USAGE permission is not
		 * needed to create tables in a schema, only to list the tables. So we
		 * dont need to check for that one.
		 */

#if PG_VERSION_NUM >= 160000
		bool user_has_create_access =
		    object_aclcheck(NamespaceRelationId, schema_oid, MotherDuckPostgresUserOid(), ACL_CREATE) == ACLCHECK_OK;
#else
		bool user_has_create_access =
		    pg_namespace_aclcheck(schema_oid, MotherDuckPostgresUserOid(), ACL_CREATE) == ACLCHECK_OK;
#endif
		if (user_has_create_access) {
			return true;
		}
		if (is_default_db) {
			/*
			 * For non $ddb schemas that already exist we don't want to give
			 * CREATE privileges to the MotherDuck Postgres role automatically.
			 * It might be some restricted and an attacker with MotherDuck
			 * access should not be able to create tables in it unless the DBA
			 * has configured access this way.
			 */
			ereport(
			    WARNING,
			    (errmsg("MotherDuck schema %s already exists, but the configured motherduck table owner does not have "
			            "CREATE privileges on it",
			            postgres_schema_name),
			     errhint("You might want to grant ALL privileges to the user '%s' on this schema.",
			             MotherDuckPostgresUserName())));
			return false;
		}

		/*
		 * We always want to grant access for ddb$ schemas. They are
		 * effictively owned by the extension so MotherDuck users should be
		 * able to do with them whatever they want. GrantAccessToSchema will
		 * already log a WARNING if it fails.
		 */
		return GrantAccessToSchema(postgres_schema_name);
	}

	std::string create_schema_query = CreatePgSchemaString(postgres_schema_name);

	/* We want to group the CREATE SCHEMA and the GRANT in a subtransaction */
	BeginInternalSubTransaction(NULL);

	if (!SPI_run_utility_command(create_schema_query.c_str())) {
		ereport(WARNING, (errmsg("Failed to sync MotherDuck schema %s", postgres_schema_name),
		                  errdetail("While executing command: %s", create_schema_query.c_str()),
		                  errhint("See previous WARNING for details")));
		RollbackAndReleaseCurrentSubTransaction();
		return false;
	}

	/*
	 * And let's GRANT access to the schema. Even for non-$ddb schemas this
	 * should be safe, because it's a new schema so no-one depends upon it not
	 * having unexpected tables in it. So basically, because it didn't exist
	 * yet we now claim it.
	 */
	if (!GrantAccessToSchema(postgres_schema_name)) {
		/* GrantAccessToSchema already logged a WARNING */
		RollbackAndReleaseCurrentSubTransaction();
		return false;
	}

	if (!is_default_db) {
		/*
		 * For ddb$ schemas we need to record a dependency between the schema
		 * and the FDW, so that DROP SERVER motherduck also drops these schemas.
		 */
		schema_oid = get_namespace_oid(postgres_schema_name, true);
		if (schema_oid == InvalidOid) {
			elog(WARNING, "Failed to create schema '%s' for unknown reason, skipping sync", postgres_schema_name);
			RollbackAndReleaseCurrentSubTransaction();
			return false;
		}

		ObjectAddress schema_address = {
		    .classId = NamespaceRelationId,
		    .objectId = schema_oid,
		    .objectSubId = 0,
		};
		RecordDependencyOnMDServer(&schema_address);
	}

	/* Success, so we commit the subtransaction */
	ReleaseCurrentSubTransaction();

	/* And then we also commit the actual transaction to release any locks that
	 * were necessary to execute it. */
	SPI_commit_that_works_in_bgworker();
	return true;
}

void
SyncMotherDuckCatalogsWithPg(bool drop_with_cascade, duckdb::ClientContext &context) {
	InvokeCPPFunc(SyncMotherDuckCatalogsWithPg_Cpp, drop_with_cascade, context);
}

void
SyncMotherDuckCatalogsWithPg_Cpp(bool drop_with_cascade, duckdb::ClientContext &context) {
	if (!pgduckdb::IsMotherDuckEnabled()) {
		throw std::runtime_error("MotherDuck support is not enabled");
	}

	initial_cache_version = pgduckdb::CacheVersion();

	auto &db_manager = duckdb::DatabaseManager::Get(context);
	const auto &default_db = db_manager.GetDefaultDatabase(context);
	auto result =
	    context.Query("SELECT alias, server_catalog_version::text FROM __md_local_databases_metadata()", false);
	if (result->HasError()) {
		elog(WARNING, "Failed to fetch MotherDuck database metadata: %s", result->GetError().c_str());
		return;
	}

	context.transaction.BeginTransaction();
	for (auto &row : *result) {
		auto motherduck_db = row.GetValue<std::string>(0);
		auto catalog_version = row.GetValue<std::string>(1);
		if (last_known_motherduck_catalog_versions[motherduck_db] == catalog_version) {
			/* The catalog version has not changed, we can skip this database */
			continue;
		}

		/* The catalog version has changed, we need to sync the catalog */
		elog(LOG, "Syncing MotherDuck catalog for database '%s' in '%s': %s", motherduck_db.c_str(),
		     get_database_name(MyDatabaseId), catalog_version.c_str());

		/*
		 * Because of our SPI_commit_that_works_in_bgworker() workaround we need to
		 * switch to TopMemoryContext before allocating any memory that
		 * should survive a call to SPI_commit_that_works_in_bgworker(). To
		 * avoid leaking unboundedly when errors occur we free the memory
		 * that's already there if not already NULL.
		 */
		MemoryContext old_context = MemoryContextSwitchTo(TopMemoryContext);
		if (current_motherduck_catalog_version) {
			pfree(current_motherduck_catalog_version);
		}

		current_motherduck_catalog_version = pstrdup(catalog_version.c_str());
		MemoryContextSwitchTo(old_context);

		last_known_motherduck_catalog_versions[motherduck_db] = catalog_version;
		auto schemas = duckdb::Catalog::GetSchemas(context, motherduck_db);
		bool is_default_db = motherduck_db == default_db;
		bool all_tables_synced_successfully = true;
		for (duckdb::SchemaCatalogEntry &schema : schemas) {
			if (schema.name == "information_schema" || schema.name == "pg_catalog") {
				continue;
			}

			if (schema.name == "public" && is_default_db) {
				/*
				 * We already map the "main" duckdb schema to the "public"
				 * Postgres. This could cause conflicts and confusion if we
				 * also map the "public" duckdb schema to the "public"
				 * Postgres schema. Since there is unlikely to be a "public"
				 * schema in the DuckDB database, we simply skip this schema
				 * for now. If users actually turn out to run into this, we
				 * probably want to map it to something else for example to
				 * ddb$my_db$public. Users can also always rename the "public"
				 * schema in their DuckDB database to something else.
				 */
				elog(
				    WARNING,
				    "Skipping sync of MotherDuck schema %s.%s because it conflicts with the sync of the %s.main schema",
				    motherduck_db.c_str(), schema.name.c_str(), motherduck_db.c_str());
				continue;
			}

			std::string postgres_schema_name = PgSchemaName(motherduck_db, schema.name, is_default_db);
			if (!CreateSchemaIfNotExists(postgres_schema_name.c_str(), is_default_db)) {
				/* We failed to create the schema, so we skip the tables in it */
				continue;
			}

			schema.Scan(context, duckdb::CatalogType::TABLE_ENTRY, [&](duckdb::CatalogEntry &entry) {
				if (entry.type != duckdb::CatalogType::TABLE_ENTRY) {
					return;
				}

				auto &table = entry.Cast<duckdb::TableCatalogEntry>();

				auto table_info = duckdb::unique_ptr_cast<duckdb::CreateInfo, duckdb::CreateTableInfo>(table.GetInfo());
				table_info->schema = table.schema.name;
				table_info->catalog = motherduck_db;

				auto create_query = CreatePgTableString(*table_info, is_default_db);
				if (create_query.empty()) {
					return;
				}

				if (!CreateTable(postgres_schema_name.c_str(), table.name.c_str(), create_query.c_str(),
				                 drop_with_cascade)) {
					all_tables_synced_successfully = false;
					return;
				}
			});

			schema.Scan(context, duckdb::CatalogType::VIEW_ENTRY, [&](duckdb::CatalogEntry &entry) {
				if (entry.type != duckdb::CatalogType::VIEW_ENTRY) {
					return;
				}

				auto &view = entry.Cast<duckdb::ViewCatalogEntry>();
				auto view_info = duckdb::unique_ptr_cast<duckdb::CreateInfo, duckdb::CreateViewInfo>(view.GetInfo());

				view_info->schema = view.schema.name;
				view_info->catalog = motherduck_db;

				auto create_query = CreatePgViewString(*view_info, is_default_db);
				if (create_query.empty()) {
					return;
				}

				if (!CreateView(postgres_schema_name.c_str(), view.name.c_str(), create_query.c_str(),
				                drop_with_cascade)) {
					all_tables_synced_successfully = false;
					return;
				}
			});
		}

		/*
		 * Now we want drop all shell tables in this database that were not
		 * updated by the current round. Since if they were not updated that
		 * means they are not part of the MotherDuck database anymore,
		 * except...
		 */
		if (!all_tables_synced_successfully) {
			/*
			 * ...if not all tables in this catalog were synced successfully,
			 * then the user should fix that problem before we dare to clean
			 * up. Otherwise we might accidentally cleanup a table that is
			 * still there, but which failed to be updated for some reason.
			 *
			 * We rely on the database operator to fix the syncing problem, and
			 * after that's done we'll clean up the tables during that first
			 * working sync. We could probably do something smarter here, but
			 * for now this is okay.
			 */
			continue;
		}

		Oid arg_types[] = {TEXTOID, TEXTOID, TEXTOID};
		Datum values[] = {CStringGetTextDatum(motherduck_db.c_str()),
		                  CStringGetTextDatum(pgduckdb::current_motherduck_catalog_version),
		                  CStringGetTextDatum(default_db.c_str())};

		/*
		 * We use a cursor here instead of plain SPI_execute, to put a limit on
		 * the memory usage here in case many tables need to be deleted (e.g.
		 * some big schema was deleted all at once). This is probably not
		 * necessary in most cases.
		 */
		auto query_tables = R"(
			SELECT relid::text, relkind
			FROM duckdb.tables
			JOIN pg_class ON oid = relid
			WHERE duckdb_db = $1 AND (
				motherduck_catalog_version != $2 OR
				default_database != $3
			))";
		Portal deleted_tables_portal =
		    SPI_cursor_open_with_args(nullptr, query_tables, lengthof(arg_types), arg_types, values, NULL, false, 0);

		const int deleted_tables_batch_size = 100;
		int current_batch_size;
		do {
			SPI_cursor_fetch(deleted_tables_portal, true, deleted_tables_batch_size);
			SPITupleTable *deleted_tables_batch = SPI_tuptable;
			current_batch_size = SPI_processed;
			for (auto i = 0; i < current_batch_size; i++) {
				HeapTuple tuple = deleted_tables_batch->vals[i];
				char *fully_qualified_table = SPI_getvalue(tuple, SPI_tuptable->tupdesc, 1);
				char relkind = SPI_getvalue(tuple, SPI_tuptable->tupdesc, 2)[0];
				/*
				 * We need to create a new SPI context for the DROP command
				 * otherwise our batch gets invalidated.
				 */
				SPI_connect();
				DropRelation(fully_qualified_table, relkind, drop_with_cascade);
				SPI_finish();
			}
		} while (current_batch_size == deleted_tables_batch_size);
		SPI_cursor_close(deleted_tables_portal);

		SPI_commit_that_works_in_bgworker();

		pfree(current_motherduck_catalog_version);
		current_motherduck_catalog_version = nullptr;
	}
	context.transaction.Commit();
}
} // namespace pgduckdb
