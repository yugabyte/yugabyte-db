//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgwrapper/ysql_upgrade.h"

#include <regex>

#include <boost/algorithm/string.hpp>

#include "server/catalog/pg_yb_migration_d.h"

#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_error.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/path_util.h"
#include "yb/util/pg_util.h"
#include "yb/util/status_format.h"

using namespace std::literals;

DEFINE_RUNTIME_int32(ysql_upgrade_migration_max_retries, 10,
    "Maximum number of times YSQL upgrade re-runs a migration script that failed with a "
    "serialization failure (SQLSTATE 40001), such as a read restart or a conflict caused by "
    "concurrent DDL. 0 disables retries.");

DEFINE_test_flag(int32, ysql_upgrade_fail_migration_attempts, 0,
    "Make the first N attempts of each migration script fail: after the script's own statements "
    "have committed, a new transaction block raises a read restart error and is left aborted.");

namespace yb {
namespace pgwrapper {

namespace {

const int kCatalogVersionMigrationNumber = 1;

const char* kStaticDataParentDir = "share";
const char* kMigrationsDir = "ysql_migrations";

std::ostream& operator<<(std::ostream& os, const Version& v) {
  os << v.first << "." << v.second;
  return os;
}

Result<int64_t> SelectCountStar(PGConn* pgconn,
                                const std::string& table_name,
                                const std::string& where_clause = "") {
  auto query_str = Format("SELECT COUNT(*) FROM $0$1",
                          table_name,
                          where_clause == "" ? "" : Format(" WHERE $0", where_clause));
  return VERIFY_RESULT(pgconn->FetchRow<PGUint64>(query_str));
}

Result<bool> SystemTableExists(PGConn* pgconn, const std::string& table_name) {
  auto where_clause = Format("relname = '$0' AND relnamespace = 'pg_catalog'::regnamespace",
                             table_name);
  return VERIFY_RESULT(SelectCountStar(pgconn, "pg_class", where_clause)) == 1;
}

// Verify that system table exists and is not empty.
Result<bool> SystemTableHasRows(PGConn* pgconn, const std::string& table_name) {
  if (!VERIFY_RESULT(SystemTableExists(pgconn, table_name)))
    return false;
  return VERIFY_RESULT(SelectCountStar(pgconn, table_name)) > 0;
}

Result<bool> FunctionExists(PGConn* pgconn, const std::string& function_name) {
  auto where_clause = Format("proname = '$0'", function_name);
  return VERIFY_RESULT(SelectCountStar(pgconn, "pg_proc", where_clause)) == 1;
}

// Return the last breaking version of template1.
// In global catalog version mode, this is used to detect that a migration
// script contains a breaking DDL statement if the last breaking version is
// incremented after execution of the script. We introduce an extra delay to
// allow the new catalog version to be propagated to tserver through heartbeat.
// In per-database catalog version mode, there are two kinds of breaking DDLs:
// (1) per-database breaking DDL
// (2) global-impact breaking DDL
// For (1), if a breaking DDL is executed on DB1 connection, because we execute
// migration script from one DB connection to another and there are multiple
// databases, by the time we come back to DB1 connection to execute the next
// migration script, chances are that the DB1 breaking version has already
// propagated so we don't need to add an extra delay which slows down the
// upgrade process.
// For (2), we need to introduce an extra delay. To simplify the query to detect
// global-impact breaking DDL we also just check the last breaking version of
// template1.
// Note that this means for case (1) we unnecessarily introduce an extra delay
// for template1.
Result<uint64_t> GetBreakingCatalogVersion(PGConn* pgconn) {
  return pgconn->FetchRow<PGUint64>(
      Format("SELECT last_breaking_version FROM pg_yb_catalog_version WHERE "
             "db_oid = $0", kTemplate1Oid));
}

std::string WrapSystemDml(const std::string& query) {
  return "SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;\n" + query;
}

// Analyze pg_catalog state of a database to determine a current major version of a catalog state
// by checking presence of catalog changing features released before the migrations feature landed.
// 0 means that no migrations were applied yet.
Result<int> GetMajorVersionFromSystemCatalogState(PGConn* pgconn) {
  int major_version = 0;

  // Helper macro removing boilerplate.
#define INCREMENT_VERSION_OR_RETURN_IT(oneliner_with_result) \
    if (VERIFY_RESULT(oneliner_with_result)) { \
      ++major_version; \
    } else { \
      return major_version; \
    }

  // V1: #3979 introducing pg_yb_catalog_version table.
  INCREMENT_VERSION_OR_RETURN_IT(SystemTableHasRows(pgconn, "pg_yb_catalog_version"))

  // V2: #4525 which creates pg_tablegroup.
  INCREMENT_VERSION_OR_RETURN_IT(SystemTableExists(pgconn, "pg_tablegroup"))

  // V3: #5478 installing pg_stat_statements.
  INCREMENT_VERSION_OR_RETURN_IT(SystemTableExists(pgconn, "pg_stat_statements"))

  // V4: #5408 introducing a bunch of JSONB functions.
  INCREMENT_VERSION_OR_RETURN_IT(FunctionExists(pgconn, "jsonb_path_query"))

  // V5: #6509 introducing yb_getrusage and yb_mem_usage* functions.
  INCREMENT_VERSION_OR_RETURN_IT(FunctionExists(pgconn, "yb_getrusage"))

  // V6: #7879 introducing yb_servers function.
  INCREMENT_VERSION_OR_RETURN_IT(FunctionExists(pgconn, "yb_servers"))

  // V7: #8719 introducing yb_hash_code function.
  INCREMENT_VERSION_OR_RETURN_IT(FunctionExists(pgconn, "yb_hash_code"))

  // V8: #7850 introducing ybgin access method.
  INCREMENT_VERSION_OR_RETURN_IT(FunctionExists(pgconn, "ybginhandler"))

  return major_version;
}

// Create a pg_yb_migration if it doesn't exist yet.
// Returns true if the table was created or false if it was present already.
Result<bool> CreateMigrationTableIfNotExist(PGConn* pgconn) {
  if (VERIFY_RESULT(SystemTableExists(pgconn, "pg_yb_migration"))) {
    LOG(INFO) << "pg_yb_migration table is present";
    return false;
  }

  const std::string query_str(
      "CREATE TABLE pg_catalog.pg_yb_migration ("
      "  major        int    NOT NULL,"
      "  minor        int    NOT NULL,"
      "  name         name   NOT NULL,"
      "  time_applied bigint"
      ") WITH (table_oid = $0, row_type_oid = $1);");
  RETURN_NOT_OK(pgconn->ExecuteFormat(query_str,
                                      YBMigrationRelationId,
                                      YBMigrationRelation_Rowtype_Id));
  LOG(INFO) << "pg_yb_migration table was created";
  return true;
}

// Determine a YSQL version of a given database and make sure it's recorded in pg_yb_migration.
// Creates a pg_yb_migration if it doesn't yet exist.
Result<Version> DetermineAndSetVersion(PGConn* pgconn) {
  bool table_created = VERIFY_RESULT(CreateMigrationTableIfNotExist(pgconn));

  // If pg_yb_migration was present before and has values, that's our version.
  if (!table_created) {
    const auto query_str(
        "SELECT major, minor FROM pg_catalog.pg_yb_migration"
        "  ORDER BY major DESC, minor DESC"
        "  LIMIT 1");
    const auto rows = VERIFY_RESULT((pgconn->FetchRows<int32_t, int32_t>(query_str)));
    if (!rows.empty()) {
      const auto& [major, minor] = rows.front();
      Version ver(major, minor);
      LOG(INFO) << "Version is " << ver;
      return ver;
    }
  }

  int major_version = VERIFY_RESULT(GetMajorVersionFromSystemCatalogState(pgconn));
  const std::string query_str(
      "INSERT INTO pg_catalog.pg_yb_migration (major, minor, name, time_applied)"
      "  VALUES ($0, 0, '<baseline>', NULL);");
  RETURN_NOT_OK(pgconn->ExecuteFormat(WrapSystemDml(query_str), major_version));

  Version ver(major_version, 0);
  LOG(INFO) << "Inserted a version " << ver;
  return ver;
}

bool IsNonSqlFile(const std::string& filename) {
  return !boost::algorithm::iends_with(filename, ".sql");
}

// Returns the highest OID in [FirstUnpinnedObjectId, FirstNormalObjectId) over all catalogs with an
// oid column in the connected database, or nullopt if there is none. These are the OIDs that
// initdb and YSQL upgrade mode assign to system objects. YbGetMaxAllocatedSystemOid computes the
// same value in the backend.
Result<std::optional<Oid>> GetMaxAllocatedSystemOid(PGConn* pgconn) {
  // FirstUnpinnedObjectId, hardcoded in transam.h.
  constexpr Oid kPgFirstUnpinnedObjectId = 12000;

  const auto catalogs = VERIFY_RESULT(pgconn->FetchRows<std::string>(
      "SELECT quote_ident(c.relname) FROM pg_catalog.pg_class c"
      "  JOIN pg_catalog.pg_attribute a ON a.attrelid = c.oid"
      "  WHERE c.relnamespace = 'pg_catalog'::regnamespace AND c.relkind = 'r'"
      "    AND a.attname = 'oid'"));
  SCHECK(!catalogs.empty(), IllegalState, "No catalog with an oid column found");
  std::vector<std::string> selects;
  for (const auto& catalog : catalogs) {
    selects.push_back(Format(
        "SELECT max(oid) AS oid FROM pg_catalog.$0 WHERE oid >= $1 AND oid < $2",
        catalog, kPgFirstUnpinnedObjectId, kPgFirstNormalObjectId));
  }
  return pgconn->FetchRow<std::optional<PGOid>>(Format(
      "SELECT max(oid) FROM ($0) AS o", boost::algorithm::join(selects, " UNION ALL ")));
}

// A migration can lose a read restart or a conflict to concurrent DDL in the same database, and
// the query layer can't retry it when the script is a multi-statement query or has already sent
// a notice to the client. Notices are flushed immediately and count as data sent; e.g. ALTER
// EXTENSION emits "'extension' is a beta feature!" unless ysql_beta_feature_extension is set.
// Migrations are required to be idempotent (see ysql_migrations/README.md), so re-run the whole
// script.
Status ExecuteMigration(
    PGConn* pgconn, const std::string& migration_content, const std::string& migration_name) {
  CoarseBackoffWaiter waiter(CoarseTimePoint::max(), 2s /* max_wait */);
  for (int attempt = 0;; ++attempt) {
    auto script = migration_content;
    if (attempt < FLAGS_TEST_ysql_upgrade_fail_migration_attempts) {
      // COMMIT ends the script's trailing implicit block; otherwise BEGIN would absorb it and the
      // failure would roll it back. YB002 is the internal read restart code, which the client
      // sees as 40001 only while yb_enable_extended_sql_codes is off.
      script += "\nCOMMIT;\nBEGIN;\n"
                "DO $$ BEGIN RAISE EXCEPTION 'Injected read restart' "
                "USING ERRCODE = 'YB002'; END $$;\n";
    }
    auto status = pgconn->Execute(
        script, /*show_query_in_error=*/false, /*ignore_empty_query=*/true);
    if (status.ok() ||
        PgsqlError(status) != YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE ||
        attempt >= FLAGS_ysql_upgrade_migration_max_retries) {
      return status;
    }
    LOG(WARNING) << "Retrying migration '" << migration_name << "' after attempt " << attempt + 1
                 << " failed: " << status;
    // A failure inside a script's explicit BEGIN block leaves the session in an aborted
    // transaction.
    if (auto rollback_status = pgconn->RollbackTransaction(); !rollback_status.ok()) {
      return status.CloneAndAppend(
          Format("; ROLLBACK before retrying failed: $0", rollback_status));
    }
    waiter.Wait();
  }
}

} // anonymous namespace

class YsqlUpgradeHelper::DatabaseEntry {
 public:
  DatabaseEntry(std::string database_name,
                PGConnBuilder conn_builder)
      : database_name_(database_name),
        conn_builder_(conn_builder) {
  }

  virtual ~DatabaseEntry() = default;

  // Establish a connection to this database, or reuse an existing one if
  // not in single-connection mode.
  virtual Result<std::shared_ptr<pgwrapper::PGConn>> GetConnection() = 0;

 protected:
  Result<std::shared_ptr<pgwrapper::PGConn>> MakeConnection() {
    auto pgconn = std::make_shared<pgwrapper::PGConn>(VERIFY_RESULT(conn_builder_.Connect()));
    RETURN_NOT_OK(pgconn->Execute("SET ysql_upgrade_mode TO true;"));
    // ExecuteMigration retries on 40001. A database or role setting that enables extended codes
    // would turn read restarts and conflicts into YB00x codes and silently disable that retry.
    RETURN_NOT_OK(pgconn->Execute("SET yb_enable_extended_sql_codes TO false"));
    // Use snapshot isolation mode during YSQL upgrade. This is needed as a simple work
    // around so that we do not start subtransactions during YSQL upgrade. Otherwise the
    // COMMIT will only capture the invalidation messages generated by the last DML statement
    // preceding the COMMIT statement.
    RETURN_NOT_OK(pgconn->Execute("SET DEFAULT_TRANSACTION_ISOLATION TO \"REPEATABLE READ\""));
    return pgconn;
  }

 public:
  const std::string database_name_;
  Version version_{0, 0};

 private:
  PGConnBuilder conn_builder_;
};

class YsqlUpgradeHelper::ReusableConnectionDatabaseEntry
    : public YsqlUpgradeHelper::DatabaseEntry {
 public:
  ReusableConnectionDatabaseEntry(std::string database_name, PGConnBuilder conn_builder)
      : DatabaseEntry(database_name, conn_builder) {
  }

  Result<std::shared_ptr<pgwrapper::PGConn>> GetConnection() override {
    if (pgconn_) {
      return pgconn_;
    }

    pgconn_ = VERIFY_RESULT(MakeConnection());
    return pgconn_;
  }

 private:
  // Connection is wrapped in shared_ptr to rely on its referece counting for destruction.
  std::shared_ptr<pgwrapper::PGConn> pgconn_;
};

class YsqlUpgradeHelper::SingletonConnectionDatabaseEntry
    : public YsqlUpgradeHelper::DatabaseEntry {
 public:
  SingletonConnectionDatabaseEntry(std::string database_name, PGConnBuilder conn_builder)
      : DatabaseEntry(database_name, conn_builder) {
  }

  Result<std::shared_ptr<pgwrapper::PGConn>> GetConnection() override {
    SCHECK(weak_pgconn_.expired(), InternalError,
           "New connection is requested before the old one is released!");

    auto pgconn = VERIFY_RESULT(MakeConnection());
    weak_pgconn_ = std::weak_ptr<pgwrapper::PGConn>(pgconn);
    return pgconn;
  }

 private:
  // Shared connection weak pointer to make sure only one is active at a time.
  inline static std::weak_ptr<pgwrapper::PGConn> weak_pgconn_ {};
};

YsqlUpgradeHelper::YsqlUpgradeHelper(const HostPort& ysql_proxy_addr,
                                     uint64_t ysql_auth_key,
                                     uint32_t heartbeat_interval_ms,
                                     bool use_single_connection)
    : ysql_proxy_addr_(ysql_proxy_addr),
      ysql_auth_key_(ysql_auth_key),
      heartbeat_interval_ms_(heartbeat_interval_ms),
      use_single_connection_(use_single_connection) {
}

YsqlUpgradeHelper::~YsqlUpgradeHelper() = default;

Status YsqlUpgradeHelper::AnalyzeMigrationFiles() {
  const std::string search_for_dir = JoinPathSegments(kStaticDataParentDir, kMigrationsDir);
  const std::string root_dir       = env_util::GetRootDir(search_for_dir);
  SCHECK(root_dir != "", InternalError,
         "Executable path not found");
  migrations_dir_ =
      JoinPathSegments(root_dir, kStaticDataParentDir, kMigrationsDir);
  auto* env = Env::Default();
  SCHECK(env->DirExists(migrations_dir_), InternalError,
         "Migrations directory not found");

  migration_filenames_map_.clear();
  std::vector<std::string> migration_filenames;
  RETURN_NOT_OK(env->GetChildren(migrations_dir_, &migration_filenames));

  // Remove unrelated files.
  migration_filenames.erase(
      std::remove_if(migration_filenames.begin(), migration_filenames.end(), IsNonSqlFile),
      migration_filenames.end());

  SCHECK(migration_filenames.size() > 0, InternalError,
         "No migrations found!");

  // Check that all migrations conform to the naming schema.
  static const std::regex regex("V(\\d+)(\\.(\\d+))?__\\d+__[_0-9A-Za-z]+\\.sql");
  std::smatch version_match;
  for (size_t i = 0; i < migration_filenames.size(); ++i) {
    const auto& filename = migration_filenames[i];
    SCHECK(std::regex_search(filename.begin(), filename.end(), version_match, regex),
           InternalError,
           Format("Migration '$0' does not conform to the filename pattern", filename));
    int major_version = std::stoi(version_match[1]);
    int minor_version = version_match[3].length() > 0 ? std::stoi(version_match[3]) : 0;
    Version version{major_version, minor_version};

    // Make sure another file with the same version wasn't already read.
    SCHECK(migration_filenames_map_.find(version) == migration_filenames_map_.end(),
           InternalError,
           Format("Migration '$0' uses the same version number as another file", filename));
    migration_filenames_map_[version] = filename;
  }

  // Check that there are no gaps in migration version numbers.
  // Good: 1.0, 2.0, 3.0, 3.1, 3.2
  // Bad:  1.0, 2.0, 4.0
  // Bad:  1.0, 2.0, 3.0, 3.2
  // Bad:  1.0, 2.0, 3.0, 4.1
  // Bad:  1.0, 2.0, 3.0, 3.1, 4.0
  // Bad:  1.0, 2.0, 3.0, 3.1, 4.1
  Version prev_version{0, 0};
  bool using_minor_versions = false;
  for (const auto& entry : migration_filenames_map_) {
    const auto& curr_version = entry.first;
    const auto& filename = entry.second;

    DCHECK(curr_version > prev_version)
        << "Expected new version to be greater than previous version: " << curr_version << " vs "
        << prev_version << ", filename: " << entry.second;
    if (using_minor_versions) {
      // Since previous increment was on minor version, expect a minor version increment.
      SCHECK((curr_version.first == prev_version.first &&
              curr_version.second == prev_version.second + 1),
             InternalError,
             Format("Migration '$0' is not exactly one minor version away from previous version $1",
                    filename, prev_version));
    } else {
      // Could be a major or minor version increment.
      SCHECK(((curr_version.first == prev_version.first &&
               curr_version.second == prev_version.second + 1) ||
              (curr_version.first == prev_version.first + 1 &&
               curr_version.second == prev_version.second)),
                InternalError,
                Format("Migration '$0' is not exactly one major or minor version away from previous"
                       " version $1",
                       filename, prev_version));
      if (curr_version.first == prev_version.first) {
        using_minor_versions = true;
      }
    }
    prev_version = curr_version;
  }

  latest_version_ = std::prev(migration_filenames_map_.end())->first;

  return Status::OK();
}

Result<std::unique_ptr<YsqlUpgradeHelper::DatabaseEntry>>
YsqlUpgradeHelper::MakeDatabaseEntry(std::string database_name) {
  // Explicitly using an infinite connect_timeout here.
  auto builder = pgwrapper::CreateInternalPGConnBuilder(
      ysql_proxy_addr_, database_name, pgwrapper::PGConnSettings::kDefaultUser, ysql_auth_key_,
      /* deadline */ std::nullopt);

  std::unique_ptr<DatabaseEntry> entry;
  if (use_single_connection_) {
    entry = std::make_unique<SingletonConnectionDatabaseEntry>(database_name, builder);
  } else {
    entry = std::make_unique<ReusableConnectionDatabaseEntry>(database_name, builder);

    // Eagerly initialize a connection
    RETURN_NOT_OK(entry->GetConnection());
  }

  return entry;
}

Result<bool> YsqlUpgradeHelper::HasYbCatalogVersion(DatabaseEntry* db_entry) {
  auto conn = VERIFY_RESULT(db_entry->GetConnection());
  return FunctionExists(conn.get(), "yb_catalog_version");
}

Result<Oid> YsqlUpgradeHelper::GetMinNextOidOfAllDatabases() {
  Oid max_oid = 0;
  for (auto& entry : databases_) {
    auto conn = VERIFY_RESULT(entry->GetConnection());
    const auto db_max_oid = VERIFY_RESULT(GetMaxAllocatedSystemOid(conn.get()));
    if (db_max_oid && *db_max_oid > max_oid) {
      max_oid = *db_max_oid;
    }
  }
  LOG(INFO) << "Highest system OID over all databases is " << max_oid;
  return max_oid + 1;
}

Status YsqlUpgradeHelper::Upgrade() {
  RETURN_NOT_OK(AnalyzeMigrationFiles());
  LOG(INFO) << "Latest version defined in migrations is " << latest_version_;

  // Place template databases to be processed first.
  std::vector<std::string> db_names{"template1", "template0"};

  {
    auto& t1_entry = databases_.emplace_back(VERIFY_RESULT(MakeDatabaseEntry("template1")));
    auto  t1_conn  = VERIFY_RESULT(t1_entry->GetConnection());

    // Fetch databases list
    {
      const std::string query_str("SELECT datname FROM pg_database"
                                  "  WHERE datname NOT IN ('template0', 'template1');");
      pgwrapper::PGResultPtr res = VERIFY_RESULT(t1_conn->Fetch(query_str));
      for (int i = 0; i < PQntuples(res.get()); i++) {
        db_names.emplace_back(VERIFY_RESULT(GetValue<std::string>(res.get(), i, 0)));
      }
    }
  }

  for (const auto& db : db_names) {
    LOG(INFO) << "Determining a YSQL version for DB " << db;
    auto& entry = db == "template1"
        ? databases_[0]
        : databases_.emplace_back(VERIFY_RESULT(MakeDatabaseEntry(db)));

    auto conn = VERIFY_RESULT(entry->GetConnection());

    const auto current_version = VERIFY_RESULT(DetermineAndSetVersion(&*conn));
    entry->version_ = current_version;

    if (current_version.first >= kCatalogVersionMigrationNumber) {
      catalog_version_migration_applied_ = true;
    }
  }

  while (true) {
    DatabaseEntry* min_version_entry =
        &**std::min_element(databases_.begin(), databases_.end(),
                            [](const auto& db1, const auto& db2) {
                              return db1->version_ < db2->version_;
                            });

    auto& min_version = min_version_entry->version_;
    if (min_version >= latest_version_) {
      LOG(INFO) << "Minimum version is " << min_version
                << " which is latest";
      break;
    }

    LOG(INFO) << "Minimum version is " << min_version
              << " (database " << min_version_entry->database_name_ << ")";

    RETURN_NOT_OK(MigrateOnce(min_version_entry));
    if (pg_global_heartbeat_wait_) {
      SleepFor(MonoDelta::FromMilliseconds(2 * heartbeat_interval_ms_));
    }
  }

  // Fix for https://github.com/yugabyte/yugabyte-db/issues/18507:
  // This bug only shows up when upgrading from 2.4.x or 2.6.x, in these
  // two releases the table pg_yb_catalog_version exists but the function
  // yb_catalog_version does not exist. However we have skipped V1 because
  // SystemTableHasRows(pgconn, "pg_yb_catalog_version") returns true.
  for (auto& entry : databases_) {
    if (!VERIFY_RESULT(HasYbCatalogVersion(entry.get()))) {
      LOG(WARNING) << "Function yb_catalog_version is missing in " << entry->database_name_;
      // Run V1 migration script to introduce function "yb_catalog_version".
      const Version version = {0, 0};
      RETURN_NOT_OK(MigrateOnce(entry.get(), &version));
    } else {
      LOG(INFO) << "Found function yb_catalog_version in " << entry->database_name_;
    }
  }

  return Status::OK();
}

Status YsqlUpgradeHelper::MigrateOnce(DatabaseEntry* db_entry, const Version* historical_version) {
  const auto& db_name = db_entry->database_name_;
  const auto& version = historical_version ? *historical_version : db_entry->version_;

  const auto& next_migration = std::find_if(migration_filenames_map_.begin(),
                                            migration_filenames_map_.end(),
                                            [version](const std::pair<Version, std::string>& e) {
                                              return e.first > version;
                                            });
  SCHECK(next_migration != migration_filenames_map_.end(),
         InternalError,
         Format("Migration following $0.$1 is not found!", version.first, version.second));
  const auto& next_version = next_migration->first;
  const auto& next_migration_filename = next_migration->second;

  faststring migration_content;
  RETURN_NOT_OK_PREPEND(ReadFileToString(Env::Default(),
                                         JoinPathSegments(migrations_dir_, next_migration_filename),
                                         &migration_content),
                        Format("Failed to read migration '$0'", next_migration_filename));

  LOG(INFO) << db_name << ": applying migration '" << next_migration_filename << "'";

  // We use the existence of "pg_global" to indicate that we need to wait.
  // For example, the creation of shared system relation need to be propagated
  // to invalidate its negative cache entry in other Postgres backends.
  pg_global_heartbeat_wait_ =
    db_name == "template1" && boost::icontains(migration_content.ToString(), "pg_global");
  std::optional<Oid> min_next_oid;
  if (pg_global_heartbeat_wait_) {
    LOG(INFO) << "Found pg_global in migration file " << next_migration_filename
              << " when applying to " << db_name;
    // Creating a shared relation inserts some of its catalog rows, such as its array type and
    // constraints, into every database, with OIDs that template1 assigns. Another database can
    // already use such an OID, if a migration that acts only in some databases has assigned
    // OIDs there. So make template1 assign OIDs above the system OIDs of every database.
    // Do this before taking template1's connection: in single connection mode, only one
    // connection can be open at a time.
    min_next_oid = VERIFY_RESULT(GetMinNextOidOfAllDatabases());
  }

  auto pgconn = VERIFY_RESULT(db_entry->GetConnection());

  const auto check_breaking_ddl =
      catalog_version_migration_applied_ && !use_single_connection_;
  uint64_t old_breaking_version = last_breaking_version_;
  if (check_breaking_ddl && old_breaking_version == 0) {
    old_breaking_version = VERIFY_RESULT(GetBreakingCatalogVersion(&*pgconn));
    DCHECK_GE(old_breaking_version, 1UL);
  }

  if (min_next_oid) {
    RETURN_NOT_OK(pgconn->ExecuteFormat(
        "SET yb_ysql_upgrade_min_next_oid TO $0", *min_next_oid));
  }

  // Note that underlying PQexec executes mutiple statements transactionally, where our usual ACID
  // guarantees apply.
  // Migrations may override that using BEGIN/COMMIT statements - this will split a singular
  // implicit transaction onto several explicit ones.
  RETURN_NOT_OK_PREPEND(
      ExecuteMigration(&*pgconn, migration_content.ToString(), next_migration_filename),
      Format("Failed to apply migration '$0' to a database $1", next_migration_filename, db_name));
  if (min_next_oid) {
    RETURN_NOT_OK(pgconn->Execute("RESET yb_ysql_upgrade_min_next_oid"));
  }

  bool has_breaking_ddl = false;
  if (check_breaking_ddl) {
    last_breaking_version_ = VERIFY_RESULT(GetBreakingCatalogVersion(&*pgconn));
    DCHECK_GE(last_breaking_version_, 1UL);
    has_breaking_ddl = old_breaking_version < last_breaking_version_;
  }
  // Wait for the new Catalog Version to be propagated to tserver through heartbeat.
  // (1) catalog_version_migration_applied_: this can only happen once, when the
  // table pg_yb_catalog_version is introduced in the first migration.
  // (2) has_breaking_ddl: last migration contains a breaking DDL as indicated
  // by the last_breaking_version change in the pg_yb_catalog_version table.
  // Sleep here isn't guaranteed to work (see #6238), failure to propagate a catalog version
  // would lead to Catalog Version Mismatch error fixed by retrial.
  if (!catalog_version_migration_applied_ || has_breaking_ddl) {
    SleepFor(MonoDelta::FromMilliseconds(2 * heartbeat_interval_ms_));
    if (!catalog_version_migration_applied_) {
      catalog_version_migration_applied_ = true;
    }
  }

  if (historical_version) {
    LOG(INFO) << db_name << ": migration successfully applied without version bump";
    return Status::OK();
  }

  RETURN_NOT_OK_PREPEND(
      pgconn->ExecuteFormat(
          WrapSystemDml(
              "INSERT INTO pg_catalog.pg_yb_migration (major, minor, name, time_applied) "
              "  VALUES ($0, $1, '$2', ROUND(EXTRACT(EPOCH FROM CURRENT_TIMESTAMP) * 1000));"),
          next_version.first, next_version.second, next_migration_filename),
      Format("Failed to bump pg_yb_migration to $0.$1 in database $2",
             next_version.first, next_version.second, db_name));

  db_entry->version_ = next_version;
  LOG(INFO) << db_name << ": migration successfully applied, version bumped to " << next_version;

  return Status::OK();
}

}  // namespace pgwrapper
}  // namespace yb
