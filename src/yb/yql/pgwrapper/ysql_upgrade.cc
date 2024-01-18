//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgwrapper/ysql_upgrade.h"

#include <regex>

#include <boost/algorithm/string.hpp>

#include "server/catalog/pg_yb_migration_d.h"

#include "yb/util/env_util.h"
#include "yb/util/format.h"
#include "yb/util/path_util.h"
#include "yb/util/pg_util.h"

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
  return VERIFY_RESULT(pgconn->FetchValue<PGUint64>(query_str));
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
    const std::string query_str(
        "SELECT major, minor FROM pg_catalog.pg_yb_migration"
        "  ORDER BY major DESC, minor DESC"
        "  LIMIT 1");
    pgwrapper::PGResultPtr res = VERIFY_RESULT(pgconn->Fetch(query_str));
    if (PQntuples(res.get()) == 1) {
      int major_version = VERIFY_RESULT(pgwrapper::GetInt32(res.get(), 0, 0));
      int minor_version = VERIFY_RESULT(pgwrapper::GetInt32(res.get(), 0, 1));
      Version ver(major_version, minor_version);
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
  // Note that the plain password in the connection string will be sent over the wire, but since it
  // only goes over a unix-domain socket, there should be no eavesdropping/tampering issues.
  auto builder = PGConnBuilder({
    .host = PgDeriveSocketDir(ysql_proxy_addr_),
    .port = ysql_proxy_addr_.port(),
    .dbname = database_name,
    .user = "postgres",
    .password = UInt64ToString(ysql_auth_key_),
  });

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

Status YsqlUpgradeHelper::Upgrade() {
  RETURN_NOT_OK(AnalyzeMigrationFiles());
  LOG(INFO) << "Latest version defined in migrations is " << latest_version_;

  std::vector<std::unique_ptr<DatabaseEntry>> databases;

  // Place template databases to be processed first.
  std::vector<std::string> db_names{"template1", "template0"};

  {
    auto& t1_entry = databases.emplace_back(VERIFY_RESULT(MakeDatabaseEntry("template1")));
    auto  t1_conn  = VERIFY_RESULT(t1_entry->GetConnection());

    // Fetch databases list
    {
      const std::string query_str("SELECT datname FROM pg_database"
                                  "  WHERE datname NOT IN ('template0', 'template1');");
      pgwrapper::PGResultPtr res = VERIFY_RESULT(t1_conn->Fetch(query_str));
      for (int i = 0; i < PQntuples(res.get()); i++) {
        db_names.emplace_back(VERIFY_RESULT(pgwrapper::GetString(res.get(), i, 0)));
      }
    }
  }

  for (const auto& db : db_names) {
    LOG(INFO) << "Determining a YSQL version for DB " << db;
    auto& entry = db == "template1"
        ? databases[0]
        : databases.emplace_back(VERIFY_RESULT(MakeDatabaseEntry(db)));

    auto conn = VERIFY_RESULT(entry->GetConnection());

    const auto current_version = VERIFY_RESULT(DetermineAndSetVersion(&*conn));
    entry->version_ = current_version;

    if (current_version.first >= kCatalogVersionMigrationNumber) {
      catalog_version_migration_applied_ = true;
    }
  }

  while (true) {
    DatabaseEntry* min_version_entry =
        &**std::min_element(databases.begin(), databases.end(),
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
  for (auto& entry : databases) {
    auto conn = VERIFY_RESULT(entry->GetConnection());
    if (!VERIFY_RESULT(FunctionExists(conn.get(), "yb_catalog_version"))) {
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

  auto pgconn = VERIFY_RESULT(db_entry->GetConnection());

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
  if (pg_global_heartbeat_wait_) {
    LOG(INFO) << "Found pg_global in migration file " << next_migration_filename
              << " when applying to " << db_name;
  }

  // Note that underlying PQexec executes mutiple statements transactionally, where our usual ACID
  // guarantees apply.
  // Migrations may override that using BEGIN/COMMIT statements - this will split a singular
  // implicit transaction onto several explicit ones.
  RETURN_NOT_OK_PREPEND(pgconn->Execute(migration_content.ToString(),
                                       false /* show_query_in_error */),
                        Format("Failed to apply migration '$0' to a database $1",
                               next_migration_filename,
                               db_name));

  // Wait for the new Catalog Version to be propagated to tserver through heartbeat.
  // This can only happen once, when the table is introduced in the first migration.
  // Sleep here isn't guaranteed to work (see #6238), failure to propagate a catalog version
  // would lead to Catalog Version Mismatch error fixed by retrial.
  if (!catalog_version_migration_applied_) {
    SleepFor(MonoDelta::FromMilliseconds(2 * heartbeat_interval_ms_));
    catalog_version_migration_applied_ = true;
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
