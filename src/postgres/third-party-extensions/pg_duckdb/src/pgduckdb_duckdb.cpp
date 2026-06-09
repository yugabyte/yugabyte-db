#include "pgduckdb/pgduckdb_duckdb.hpp"

#include <filesystem>

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

#include "pgduckdb/catalog/pgduckdb_storage.hpp"
#include "pgduckdb/pg/guc.hpp"
#include "pgduckdb/pg/permissions.hpp"
#include "pgduckdb/pg/string_utils.hpp"
#include "pgduckdb/pg/transactions.hpp"
#include "pgduckdb/pgduckdb_background_worker.hpp"
#include "pgduckdb/pgduckdb_fdw.hpp"
#include "pgduckdb/pgduckdb_guc.hpp"
#include "pgduckdb/pgduckdb_metadata_cache.hpp"
#include "pgduckdb/pgduckdb_extensions.hpp"
#include "pgduckdb/pgduckdb_secrets_helper.hpp"
#include "pgduckdb/pgduckdb_unsupported_type_optimizer.hpp"
#include "pgduckdb/pgduckdb_userdata_cache.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"
#include "pgduckdb/pgduckdb_xact.hpp"
#include "pgduckdb/scan/postgres_scan.hpp"

#include "pgduckdb/utility/cpp_wrapper.hpp"
#include "pgduckdb/utility/signal_guard.hpp"
#include "pgduckdb/vendor/pg_list.hpp"

extern "C" {
#include "postgres.h"

#include "catalog/namespace.h"
#include "common/file_perm.h"
#include "lib/stringinfo.h"
#include "miscadmin.h"        // superuser
#include "nodes/value.h"      // strVal
#include "utils/fmgrprotos.h" // pg_sequence_last_value
#include "utils/lsyscache.h"  // get_relname_relid
}

namespace pgduckdb {

const char *
GetSessionHint() {
	if (!IsEmptyString(duckdb_motherduck_session_hint)) {
		return duckdb_motherduck_session_hint;
	}
	return PossiblyReuseBgwSessionHint();
}

namespace ddb {
bool
DidWrites() {
	if (!DuckDBManager::IsInitialized()) {
		return false;
	}

	auto connection = DuckDBManager::GetConnectionUnsafe();
	auto &context = *connection->context;
	return DidWrites(context);
}

bool
DidWrites(duckdb::ClientContext &context) {
	if (!context.transaction.HasActiveTransaction()) {
		return false;
	}
	return context.ActiveTransaction().ModifiedDatabase() != nullptr;
}
} // namespace ddb

DuckDBManager DuckDBManager::manager_instance;

template <typename T>
std::string
ToString(T value) {
	return std::to_string(value);
}

template <>
std::string
ToString(char *value) {
	return std::string(value);
}

#define SET_DUCKDB_OPTION(ddb_option_name)                                                                             \
	config.options.ddb_option_name = duckdb_##ddb_option_name;                                                         \
	elog(DEBUG2, "[PGDuckDB] Set DuckDB option: '" #ddb_option_name "'=%s", ToString(duckdb_##ddb_option_name).c_str());

void
DuckDBManager::Initialize() {
	elog(DEBUG2, "(PGDuckDB/DuckDBManager) Creating DuckDB instance");

	// Block signals before initializing DuckDB to ensure signal is handled by the Postgres main thread only
	pgduckdb::ThreadSignalBlockGuard guard;

	// Make sure directories provided in config exists
	std::filesystem::create_directories(duckdb_temporary_directory);
	std::filesystem::create_directories(duckdb_extension_directory);

	duckdb::DBConfig config;
	std::string user_agent = "pg_duckdb";
	if (!IsEmptyString(duckdb_custom_user_agent)) {
		user_agent += ", ";
		user_agent += duckdb_custom_user_agent;
	}
	const char *application_name = pg::GetConfigOption("application_name", true);
	if (!IsEmptyString(application_name)) {
		user_agent += ", ";
		user_agent += application_name;
	}
	config.SetOptionByName("custom_user_agent", user_agent);
	config.SetOptionByName("default_null_order", "postgres");

	SET_DUCKDB_OPTION(allow_unsigned_extensions);
	SET_DUCKDB_OPTION(enable_external_access);
	SET_DUCKDB_OPTION(allow_community_extensions);
	SET_DUCKDB_OPTION(autoinstall_known_extensions);
	SET_DUCKDB_OPTION(autoload_known_extensions);
	SET_DUCKDB_OPTION(temporary_directory);
	SET_DUCKDB_OPTION(extension_directory);

	if (duckdb_maximum_memory > 0) {
		// Convert the memory limit from MB (as set by Postgres GUC_UNIT_MB, which is actually MiB; see
		// memory_unit_conversion_table in guc.c) to a string with the "MiB" suffix, as required by DuckDB's memory
		// parser. This ensures the value is interpreted correctly by DuckDB.
		std::string memory_limit = std::to_string(duckdb_maximum_memory) + "MiB";
		config.options.maximum_memory = duckdb::DBConfig::ParseMemoryLimit(memory_limit);
		elog(DEBUG2, "[PGDuckDB] Set DuckDB option: 'maximum_memory'=%dMB", duckdb_maximum_memory);
	}
	if (duckdb_max_temp_directory_size != NULL && strlen(duckdb_max_temp_directory_size) != 0) {
		config.SetOptionByName("max_temp_directory_size", duckdb_max_temp_directory_size);
		elog(DEBUG2, "[PGDuckDB] Set DuckDB option: 'max_temp_directory_size'=%s", duckdb_max_temp_directory_size);
	}

	if (duckdb_maximum_threads > -1) {
		SET_DUCKDB_OPTION(maximum_threads);
	}

	std::string connection_string;

	/*
	 * If MotherDuck is enabled, use it to connect to DuckDB. That way DuckDB
	 * its default database will be set to the default MotherDuck database.
	 */
	if (pgduckdb::IsMotherDuckEnabled()) {
		/*
		 * Disable web login for MotherDuck. Having support for web login could
		 * be nice for demos, but because the received token is not shared
		 * between backends you will get browser windows popped up. Sharing the
		 * token across backends could be made to work but the implementing it
		 * is not trivial, so for now we simply disable the web login.
		 */
		setenv("motherduck_disable_web_login", "1", 1);

		std::ostringstream oss;
		oss << "md:";

		// Default database
		auto default_database = FindMotherDuckDefaultDatabase();
		AppendEscapedUri(oss, default_database);

		oss << "?";

		// Session hint
		auto session_hint = GetSessionHint();
		if (!IsEmptyString(session_hint)) {
			oss << "session_hint=";
			AppendEscapedUri(oss, session_hint);
			oss << "&";
		}

		connection_string = oss.str();

		// Token
		auto token = FindMotherDuckToken();
		if (token != nullptr && !AreStringEqual(token, "::FROM_ENV::")) {
			setenv("motherduck_token", token, 1);
		}
	}

	std::string pg_time_zone(pg::GetConfigOption("TimeZone"));

	database = new duckdb::DuckDB(connection_string, &config);

	auto &dbconfig = duckdb::DBConfig::GetConfig(*database->instance);
	dbconfig.storage_extensions["pgduckdb"] = duckdb::make_uniq<PostgresStorageExtension>();

	// Register the unsupported type optimizer to run after all other optimizations
	dbconfig.optimizer_extensions.push_back(UnsupportedTypeOptimizer::GetOptimizerExtension());

	auto &extension_manager = database->instance->GetExtensionManager();
	auto extension_active_load = extension_manager.BeginLoad("pgduckdb");
	D_ASSERT(extension_active_load);
	duckdb::ExtensionInstallInfo extension_install_info;
	extension_active_load->FinishLoad(extension_install_info);

	connection = duckdb::make_uniq<duckdb::Connection>(*database);

	auto &context = *connection->context;

	auto &db_manager = duckdb::DatabaseManager::Get(context);
	default_dbname = db_manager.GetDefaultDatabase(context);
	pgduckdb::DuckDBQueryOrThrow(context, "SET TimeZone =" + duckdb::KeywordHelper::WriteQuoted(pg_time_zone));
	pgduckdb::DuckDBQueryOrThrow(context, "SET default_collation =" +
	                                          duckdb::KeywordHelper::WriteQuoted(duckdb_default_collation));
	pgduckdb::DuckDBQueryOrThrow(context, "ATTACH DATABASE 'pgduckdb' (TYPE pgduckdb)");
	pgduckdb::DuckDBQueryOrThrow(context, "ATTACH DATABASE ':memory:' AS pg_temp;");

	if (pgduckdb::IsMotherDuckEnabled()) {
		auto timeout = FindMotherDuckBackgroundCatalogRefreshInactivityTimeout();
		if (timeout != nullptr) {
			auto quoted_timeout = duckdb::KeywordHelper::WriteQuoted(timeout);
			pgduckdb::DuckDBQueryOrThrow(context, "SET motherduck_background_catalog_refresh_inactivity_timeout=" +
			                                          quoted_timeout);
		}
	}

	if (duckdb_autoinstall_known_extensions) {
		InstallExtensions(context);
	}
	LoadExtensions(context);
}

void
DuckDBManager::Reset() {
	manager_instance.connection = nullptr;
	delete manager_instance.database;
	manager_instance.database = nullptr;
	UnclaimBgwSessionHint();
}

int64
GetSeqLastValue(const char *seq_name) {
	Oid duckdb_namespace = get_namespace_oid("duckdb", false);
	Oid table_seq_oid = get_relname_relid(seq_name, duckdb_namespace);
	return PostgresFunctionGuard(DirectFunctionCall1Coll, pg_sequence_last_value, InvalidOid, table_seq_oid);
}

void
DuckDBManager::LoadSecrets(duckdb::ClientContext &context) {
	auto queries = InvokeCPPFunc(pg::ListDuckDBCreateSecretQueries);
	foreach_ptr(char, query, queries) {
		DuckDBQueryOrThrow(context, query);
	}
}

void
DuckDBManager::DropSecrets(duckdb::ClientContext &context) {
	auto secrets =
	    pgduckdb::DuckDBQueryOrThrow(context, "SELECT name FROM duckdb_secrets() WHERE name LIKE 'pgduckdb_secret_%';");
	while (auto chunk = secrets->Fetch()) {
		for (size_t i = 0, s = chunk->size(); i < s; ++i) {
			auto drop_secret_cmd = duckdb::StringUtil::Format("DROP SECRET %s;", chunk->GetValue(0, i).ToString());
			pgduckdb::DuckDBQueryOrThrow(context, drop_secret_cmd);
		}
	}
}

void
DuckDBManager::LoadExtensions(duckdb::ClientContext &context) {
	auto duckdb_extensions = ReadDuckdbExtensions();

	for (auto &extension : duckdb_extensions) {
		if (extension.autoload) {
			DuckDBQueryOrThrow(context, ddb::LoadExtensionQuery(extension.name));
		}
	}
}

void
DuckDBManager::InstallExtensions(duckdb::ClientContext &context) {
	auto duckdb_extensions = ReadDuckdbExtensions();

	for (auto &extension : duckdb_extensions) {
		DuckDBQueryOrThrow(context, ddb::InstallExtensionQuery(extension.name, extension.repository));
	}
}

static std::string
DisabledFileSystems() {
	if (pgduckdb::pg::AllowRawFileAccess()) {
		return duckdb_disabled_filesystems;
	}

	if (IsEmptyString(duckdb_disabled_filesystems)) {
		return "LocalFileSystem";
	}

	/* Ensure LocalFileSystem is added only when it's absent from duckdb_disabled_filesystems. */
	std::vector<std::string> fs_list = duckdb::StringUtil::Split(duckdb_disabled_filesystems, ',');
	for (auto &fs : fs_list) {
		std::string trimmed_fs = fs;
		duckdb::StringUtil::Trim(trimmed_fs);
		if (duckdb::StringUtil::CIEquals(trimmed_fs, "LocalFileSystem")) {
			return duckdb_disabled_filesystems;
		}
	}
	return "LocalFileSystem," + std::string(duckdb_disabled_filesystems);
}

void
DuckDBManager::RefreshConnectionState(duckdb::ClientContext &context) {
	std::string disabled_filesystems = DisabledFileSystems();
	if (disabled_filesystems != "") {
		/*
		 * DuckDB does not allow us to disable this setting on the
		 * database after the DuckDB connection is created for a non
		 * superuser, any further connections will inherit this
		 * restriction. This means that once a non-superuser used a
		 * DuckDB connection in a Postgres backend, any other
		 * connection will inherit these same filesystem restrictions.
		 * This shouldn't be a problem in practice.
		 */
		pgduckdb::DuckDBQueryOrThrow(context, "SET disabled_filesystems=" +
		                                          duckdb::KeywordHelper::WriteQuoted(disabled_filesystems));
	}

	if (strlen(duckdb_azure_transport_option_type) > 0) {
		pgduckdb::DuckDBQueryOrThrow(context,
		                             "SET azure_transport_option_type=" +
		                                 duckdb::KeywordHelper::WriteQuoted(duckdb_azure_transport_option_type));
	}

	const auto extensions_table_last_seq = GetSeqLastValue("extensions_table_seq");
	if (IsExtensionsSeqLessThan(extensions_table_last_seq)) {
		LoadExtensions(context);
		UpdateExtensionsSeq(extensions_table_last_seq);
	}

	if (!secrets_valid) {
		DropSecrets(context);
		LoadSecrets(context);
		secrets_valid = true;
	}
}

/*
 * Creates a new connection to the global DuckDB instance. This should only be
 * used in some rare cases, where a temporary new connection is needed instead
 * of the global cached connection that is returned by GetConnection.
 */
duckdb::unique_ptr<duckdb::Connection>
DuckDBManager::CreateConnection() {
	pgduckdb::RequireDuckdbExecution();

	auto &instance = Get();
	auto connection = duckdb::make_uniq<duckdb::Connection>(*instance.database);
	auto &context = *connection->context;

	instance.RefreshConnectionState(context);

	return connection;
}

/* Returns the cached connection to the global DuckDB instance. */
duckdb::Connection *
DuckDBManager::GetConnection(bool force_transaction) {
	pgduckdb::RequireDuckdbExecution();

	auto &instance = Get();
	auto &context = *instance.connection->context;

	if (!context.transaction.HasActiveTransaction()) {
		if (IsSubTransaction()) {
			throw duckdb::NotImplementedException("SAVEPOINT and subtransactions are not supported in DuckDB");
		}

		if (force_transaction || pg::IsInTransactionBlock()) {
			/*
			 * We only want to open a new DuckDB transaction if we're already
			 * in a Postgres transaction block. Always opening a transaction
			 * incurs a significant performance penalty for single statement
			 * queries on MotherDuck. This is because a second round-trip is
			 * needed to send the COMMIT to MotherDuck when Postgres its
			 * transaction finishes. So we only want to do this when actually
			 * necessary.
			 */
			instance.connection->BeginTransaction();
		}
	}

	instance.RefreshConnectionState(context);

	return instance.connection.get();
}

/*
 * Returns the cached connection to the global DuckDB instance, but does not do
 * any checks required to correctly initialize the DuckDB transaction nor
 * refreshes the secrets/extensions/etc. Only use this in rare cases where you
 * know for sure that the connection is already initialized correctly for the
 * current query, and you just want a pointer to it.
 */
duckdb::Connection *
DuckDBManager::GetConnectionUnsafe() {
	auto &instance = Get();
	return instance.connection.get();
}

} // namespace pgduckdb
