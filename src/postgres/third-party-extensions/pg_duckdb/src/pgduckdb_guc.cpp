#include <type_traits>
#include <climits>

#include "pgduckdb/pgduckdb_duckdb.hpp"
#include "pgduckdb/pgduckdb_guc.hpp"
#include "pgduckdb/pgduckdb_metadata_cache.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"
#include "pgduckdb/utility/cpp_wrapper.hpp"

extern "C" {
#include "postgres.h"
#include "access/xact.h"
#include "utils/guc.h"
#include "utils/guc_tables.h"
#include "miscadmin.h" // DataDir
#include "lib/stringinfo.h"
#include "postmaster/bgworker_internals.h"
}

static GucStringAssignHook prev_tz_assign_hook = nullptr;

namespace pgduckdb {

namespace {
char *
MakeDirName(const char *name) {
	StringInfoData buf;
	initStringInfo(&buf);
	appendStringInfo(&buf, "%s/pg_duckdb/%s", DataDir, name);
	return buf.data;
}

template <typename T>
bool
GucCheckDuckDBNotInitdHook(T *, void **, GucSource) {
	if (pgduckdb::DuckDBManager::IsInitialized()) {
		GUC_check_errmsg("Cannot set this variable after DuckDB has been initialized. Reconnect to Postgres or use "
		                 "`duckdb.recycle_ddb()` to reset "
		                 "the DuckDB instance.");
		return false;
	}
	return true;
}

template <typename T>
using GucTypeCheckHook = bool (*)(T *, void **, GucSource);

template <typename T>
using GucTypeAssignHook = void (*)(T, void *);

void
DefineCustomVariable(const char *name, const char *short_desc, bool *var, GucContext context = PGC_USERSET,
                     int flags = 0, GucBoolCheckHook check_hook = NULL, GucBoolAssignHook assign_hook = NULL,
                     GucShowHook show_hook = NULL) {
	DefineCustomBoolVariable(name, gettext_noop(short_desc), NULL, var, *var, context, flags, check_hook, assign_hook,
	                         show_hook);
}

void
DefineCustomVariable(const char *name, const char *short_desc, char **var, GucContext context = PGC_USERSET,
                     int flags = 0, GucStringCheckHook check_hook = NULL, GucStringAssignHook assign_hook = NULL,
                     GucShowHook show_hook = NULL) {
	DefineCustomStringVariable(name, gettext_noop(short_desc), NULL, var, *var, context, flags, check_hook, assign_hook,
	                           show_hook);
}

template <typename T>
void
DefineCustomVariable(const char *name, const char *short_desc, T *var, T min, T max, GucContext context = PGC_USERSET,
                     int flags = 0, GucTypeCheckHook<T> check_hook = NULL, GucTypeAssignHook<T> assign_hook = NULL,
                     GucShowHook show_hook = NULL) {
	/* clang-format off */
	void (*func)(
			const char *name,
			const char *short_desc,
			const char *long_desc,
			T *valueAddr,
			T bootValue,
			T minValue,
			T maxValue,
			GucContext context,
			int flags,
			GucTypeCheckHook<T> check_hook,
			GucTypeAssignHook<T> assign_hook,
			GucShowHook show_hook
	);
	/* clang-format on */
	if constexpr (std::is_integral_v<T>) {
		func = DefineCustomIntVariable;
	} else if constexpr (std::is_floating_point_v<T>) {
		func = DefineCustomRealVariable;
	} else {
		static_assert("Unsupported type");
	}

	func(name, gettext_noop(short_desc), NULL, var, *var, min, max, context, flags, check_hook, assign_hook, show_hook);
}

void
DefineCustomDuckDBVariable(const char *name, const char *short_desc, bool *var, GucContext context = PGC_USERSET,
                           int flags = 0, GucBoolAssignHook assign_hook = NULL, GucShowHook show_hook = NULL) {
	DefineCustomVariable(name, short_desc, var, context, flags, GucCheckDuckDBNotInitdHook, assign_hook, show_hook);
}

void
DefineCustomDuckDBVariable(const char *name, const char *short_desc, char **var, GucContext context = PGC_USERSET,
                           int flags = 0, GucStringAssignHook assign_hook = NULL, GucShowHook show_hook = NULL) {
	DefineCustomVariable(name, short_desc, var, context, flags, GucCheckDuckDBNotInitdHook, assign_hook, show_hook);
}

template <typename T>
void
DefineCustomDuckDBVariable(const char *name, const char *short_desc, T *var, T min, T max,
                           GucContext context = PGC_USERSET, int flags = 0) {
	DefineCustomVariable(name, short_desc, var, min, max, context, flags, GucCheckDuckDBNotInitdHook<T>,
	                     (GucTypeAssignHook<T>)NULL, NULL);
}
} // namespace

bool duckdb_force_execution = false;
bool duckdb_unsafe_allow_execution_inside_functions = false;
bool duckdb_unsafe_allow_mixed_transactions = false;
bool duckdb_convert_unsupported_numeric_to_double = false;
bool duckdb_log_pg_explain = false;
int duckdb_threads_for_postgres_scan = 2;
int duckdb_max_workers_per_postgres_scan = 2;
char *duckdb_motherduck_session_hint = strdup("");
char *duckdb_postgres_role = strdup("");
bool duckdb_force_motherduck_views = false;

int duckdb_maximum_threads = -1;
int duckdb_maximum_memory = 4096; /* 4GB in MB */
char *duckdb_disabled_filesystems = strdup("");
bool duckdb_enable_external_access = true;
bool duckdb_allow_community_extensions = false;
bool duckdb_allow_unsigned_extensions = false;
bool duckdb_autoinstall_known_extensions = true;
bool duckdb_autoload_known_extensions = true;
char *duckdb_temporary_directory = MakeDirName("temp");
char *duckdb_extension_directory = MakeDirName("extensions");
char *duckdb_max_temp_directory_size = strdup("");
char *duckdb_default_collation = strdup("");
char *duckdb_azure_transport_option_type = strdup("");
char *duckdb_custom_user_agent = strdup("");

void
InitGUC() {
	/* pg_duckdb specific GUCs */
	DefineCustomVariable("duckdb.force_execution", "Force queries to use DuckDB execution", &duckdb_force_execution,
	                     PGC_USERSET, GUC_REPORT);

	DefineCustomVariable("duckdb.unsafe_allow_execution_inside_functions", "Allow DuckDB execution inside functions",
	                     &duckdb_unsafe_allow_execution_inside_functions, PGC_SUSET);

	DefineCustomVariable("duckdb.unsafe_allow_mixed_transactions",
	                     "Allow mixed transactions between DuckDB and Postgres",
	                     &duckdb_unsafe_allow_mixed_transactions);

	DefineCustomVariable("duckdb.convert_unsupported_numeric_to_double",
	                     "Convert NUMERIC types of unsupported precision to DOUBLE",
	                     &duckdb_convert_unsupported_numeric_to_double);

	DefineCustomVariable("duckdb.log_pg_explain", "Logs the EXPLAIN plan of a Postgres scan at the NOTICE log level",
	                     &duckdb_log_pg_explain);

	DefineCustomVariable("duckdb.threads_for_postgres_scan",
	                     "Maximum number of DuckDB threads used for a single Postgres scan",
	                     &duckdb_threads_for_postgres_scan, 1, MAX_PARALLEL_WORKER_LIMIT);
	DefineCustomVariable("duckdb.max_workers_per_postgres_scan",
	                     "Maximum number of PostgreSQL workers used for a single Postgres scan",
	                     &duckdb_max_workers_per_postgres_scan, 0, MAX_PARALLEL_WORKER_LIMIT);

	DefineCustomVariable("duckdb.postgres_role",
	                     "Which postgres role should be allowed to use DuckDB execution, use the secrets and create "
	                     "MotherDuck tables. Defaults to superusers only",
	                     &duckdb_postgres_role, PGC_POSTMASTER, GUC_SUPERUSER_ONLY);

	DefineCustomVariable("duckdb.force_motherduck_views",
	                     "Force all views to be created in MotherDuck, even if they don't use MotherDuck tables",
	                     &duckdb_force_motherduck_views);

	/* GUCs acting on DuckDB instance */
	DefineCustomDuckDBVariable("duckdb.enable_external_access", "Allow the DuckDB to access external state.",
	                           &duckdb_enable_external_access, PGC_SUSET);

	DefineCustomDuckDBVariable("duckdb.allow_community_extensions", "Disable installing community extensions",
	                           &duckdb_allow_community_extensions, PGC_SUSET);

	DefineCustomDuckDBVariable("duckdb.allow_unsigned_extensions",
	                           "Allow DuckDB to load extensions with invalid or missing signatures",
	                           &duckdb_allow_unsigned_extensions, PGC_SUSET);

	DefineCustomDuckDBVariable(
	    "duckdb.autoinstall_known_extensions",
	    "Whether known extensions are allowed to be automatically installed when a DuckDB query depends on them",
	    &duckdb_autoinstall_known_extensions, PGC_SUSET);

	DefineCustomDuckDBVariable(
	    "duckdb.autoload_known_extensions",
	    "Whether known extensions are allowed to be automatically loaded when a DuckDB query depends on them",
	    &duckdb_autoload_known_extensions, PGC_SUSET);

	DefineCustomDuckDBVariable("duckdb.max_memory", "The maximum memory DuckDB can use in MB (e.g., 4096 for 4GB)",
	                           &duckdb_maximum_memory, 0, INT_MAX, PGC_SUSET, GUC_UNIT_MB);
	DefineCustomDuckDBVariable(
	    "duckdb.memory_limit",
	    "The maximum memory DuckDB can use in MB (e.g., 4096 for 4GB), alias for duckdb.max_memory",
	    &duckdb_maximum_memory, 0, INT_MAX, PGC_SUSET, GUC_UNIT_MB);

	DefineCustomDuckDBVariable(
	    "duckdb.temporary_directory",
	    "Set the directory to which DuckDB write temp files, alias for duckdb.temporary_directory",
	    &duckdb_temporary_directory, PGC_SUSET);

	DefineCustomDuckDBVariable(
	    "duckdb.max_temp_directory_size",
	    "The maximum amount of data stored inside DuckDB's 'temp_directory' (when set) (e.g., 1GB), "
	    "alias for duckdb.max_temp_directory_size",
	    &duckdb_max_temp_directory_size, PGC_SUSET);

	DefineCustomDuckDBVariable(
	    "duckdb.extension_directory",
	    "Set the directory to where DuckDB stores extensions in, alias for duckdb.extension_directory",
	    &duckdb_extension_directory, PGC_SUSET);

	DefineCustomDuckDBVariable("duckdb.threads", "Maximum number of DuckDB threads per Postgres backend.",
	                           &duckdb_maximum_threads, -1, 1024, PGC_SUSET);
	DefineCustomDuckDBVariable("duckdb.worker_threads",
	                           "Maximum number of DuckDB threads per Postgres backend, alias for duckdb.threads",
	                           &duckdb_maximum_threads, -1, 1024, PGC_SUSET);

	DefineCustomDuckDBVariable("duckdb.default_collation",
	                           "The default collation to use for DuckDB queries, e.g., 'en_us'",
	                           &duckdb_default_collation, PGC_SUSET);

	DefineCustomDuckDBVariable("duckdb.motherduck_session_hint", "The session hint to use for MotherDuck connections",
	                           &duckdb_motherduck_session_hint);

	DefineCustomDuckDBVariable("duckdb.disabled_filesystems",
	                           "Disable specific file systems preventing access (e.g., LocalFileSystem)",
	                           &duckdb_disabled_filesystems, PGC_SUSET);

	DefineCustomDuckDBVariable("duckdb.azure_transport_option_type",
	                           "Set the azure_transport_option_type for DuckDB Azure extension. Can be used to "
	                           "workaround issue #882: https://github.com/duckdb/pg_duckdb/issues/882",
	                           &duckdb_azure_transport_option_type, PGC_SUSET);

	DefineCustomDuckDBVariable("duckdb.custom_user_agent", "Additional user agent string to append to 'pg_duckdb'",
	                           &duckdb_custom_user_agent, PGC_SUSET);
}

#if PG_VERSION_NUM < 160000
static struct config_generic *
find_option(const char *name, bool, bool, int) {
	struct config_generic *gen = nullptr;

	int var_count = GetNumConfigOptions();
	auto *guc_variables = get_guc_variables();

	for (int i = 0; i < var_count; i++) {
		if (pg_strcasecmp(name, guc_variables[i]->name) == 0) {
			gen = (struct config_generic *)guc_variables[i];
			break;
		}
	}

	return gen;
}
#endif

static void
DuckAssignTimezone_Cpp(const char *tz) {
	if (!IsExtensionRegistered()) {
		return;
	}

	if (!DuckDBManager::IsInitialized()) {
		return;
	}

	// Update timezone in DuckDB too.
	// This uses GetConnectionUnsafe because GetConnection requires a
	// transaction context to be active in Postgres to be able to call
	// RefreshConnectionState, and GUCs can be changed outside of transaction
	// blocks (for instance by being reverted due to SET LOCAL or by SIGHUP)
	auto connection = pgduckdb::DuckDBManager::GetConnectionUnsafe();
	pgduckdb::DuckDBQueryOrThrow(*connection, "SET TimeZone =" + duckdb::KeywordHelper::WriteQuoted(tz));
	elog(DEBUG2, "[PGDuckDB] Set DuckDB option: 'TimeZone'=%s", tz);
}

static void
DuckAssignTimezone(const char *newval, void *extra) {
	prev_tz_assign_hook(newval, extra);

	// assign_timezone have not update guc_variables, so we can't use GetConfigOption
	auto *tz = pg_get_timezone_name(*((pg_tz **)extra));
	InvokeCPPFunc(DuckAssignTimezone_Cpp, tz);
}

/*
 * Initialize GUC hooks.
 *  some guc should be set to duckdb instance, such as timezone
 *  is there any other guc should be set to duckdb instance?
 */
void
InitGUCHooks() {
	// timezone
	{
		if (auto *tz = (struct config_string *)find_option("TimeZone", false, false, 0); tz != nullptr) {
			prev_tz_assign_hook = tz->assign_hook;
			tz->assign_hook = DuckAssignTimezone;
		}
	}
}

} // namespace pgduckdb
