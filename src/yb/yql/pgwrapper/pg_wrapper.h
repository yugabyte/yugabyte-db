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

#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "yb/gutil/ref_counted.h"
#include "yb/util/flags.h"
#include "yb/util/subprocess.h"
#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/process_wrapper/process_wrapper.h"

namespace yb {

class Thread;

namespace tserver {

class TabletServerIf;

} // namespace tserver

namespace pgwrapper {

// Returns the root directory of our PostgreSQL installation.
std::string GetPostgresInstallRoot();

// Configuration for an external PostgreSQL server.
struct PgProcessConf : public ProcessWrapperCommonConfig {
  static constexpr uint16_t kDefaultPort = 5433;

  static Result<PgProcessConf> CreateValidateAndRunInitDb(
      const std::string& bind_addresses,
      const std::string& data_dir,
      const int tserver_shm_fd);

  std::string ToString();

  std::string data_dir;
  uint16_t pg_port = kDefaultPort;
  std::string listen_addresses = "0.0.0.0";
  std::string master_addresses;

  // File descriptor of the local tserver's shared memory.
  int tserver_shm_fd = -1;

  // If this is true, we will not log to the file, even if the log file is specified.
  bool force_disable_log_file = false;

  // Start the PostgreSQL server in binary upgrade mode.
  bool run_in_binary_upgrade = false;
};

// Invokes a PostgreSQL child process once. Also allows invoking initdb. Not thread-safe.
class PgWrapper : public ProcessWrapper {
 public:
  explicit PgWrapper(PgProcessConf conf);

  // Checks if we have a valid configuration in order to be able to run PostgreSQL.
  Status PreflightCheck() override;

  Status Start() override;

  Status ReloadConfig() override;
  Status UpdateAndReloadConfig() override;

  // Calls initdb if the data directory does not exist, or if the tablet server is transitioning
  // from one major PG version to another. (The data directory's version must match the PG major
  // version.) This function is intended to be used during tablet server initialization.
  Status InitDbLocalOnlyIfNeeded();

  // Run initdb in a mode that sets up the required metadata in the YB cluster. This is done
  // only once after the cluster has started up. tmp_dir_base is used as a base directory to
  // create a temporary PostgreSQL directory that is later deleted.
  static Status InitDbForYSQL(
      const std::string& master_addresses, const std::string& tmp_dir_base, int tserver_shm_fd,
      std::vector<std::pair<std::string, YBCPgOid>> db_to_oid);

  Status SetYsqlConnManagerStatsShmKey(key_t statsshmkey);

  struct PgUpgradeParams {
    std::string data_dir;
    std::string old_version_pg_address;
    uint16_t old_version_pg_port;
    std::string new_version_pg_address;
    uint16_t new_version_pg_port;
  };

  static Status RunPgUpgrade(const PgUpgradeParams& param);

 private:
  struct LocalInitdbParams {
    std::string versioned_data_dir;
  };
  struct GlobalInitdbParams {
    std::vector<std::pair<std::string, YBCPgOid>> db_to_oid;
  };
  using InitdbParams = std::variant<LocalInitdbParams, GlobalInitdbParams>;

  // Calls PostgreSQL's initdb program for initial database initialization.
  //
  // initdb_params - initdb is run in one of two modes: 1) local initdb, which initializes a
  //                 PostgreSQL data directory and does not access the YB cluster; or 2) global
  //                 cluster initdb, which does initialization of catalogs and system objects
  //                 against the YugabyteDB cluster.
  //
  //                 In local initdb mode, the caller must pass the data directory to use, which
  //                 is expected to be a directory that's versioned to support online upgrade.
  //
  //                 In global cluster initdb mode, the caller must pass a mapping from database
  //                 name to database OID. If there is already an existing cluster and it's going
  //                 through an online upgrade, the caller must pass entries for all existing
  //                 system-generated databases other than template1 (typically template0,
  //                 postgres, yugabyte, and system platform), so that such OIDs can be reused in
  //                 the new version of PostgreSQL. The template1 database is special because it's
  //                 created by the bootstrap phase of initdb (see file comment for initdb.c for
  //                 more details). The template1 database always has OID 1.
  //
  //                 For a new cluster, the caller must pass an empty map, which indicates that
  //                 default OIDs are to be used.
  Status InitDb(InitdbParams initdb_params);

  // Creates a directory name "<conf_.data_dir>_<version>".
  std::string MakeVersionedDataDir(int32_t version);

  static std::string GetPostgresExecutablePath();
  static std::string GetPostgresSuppressionsPath();
  static std::string GetPostgresLibPath();
  static std::string GetPostgresThirdPartyLibPath();
  static std::string GetInitDbExecutablePath();

  // Set common environment for a child process (initdb or postgres itself).
  void SetCommonEnv(Subprocess* proc, bool yb_enabled);
  PgProcessConf conf_;
  key_t ysql_conn_mgr_stats_shmem_key_;
};

// Keeps a PostgreSQL process running in the background, and restarts it in case it crashes.
// Starts a separate thread to monitor the child process.
class PgSupervisor : public ProcessSupervisor {
 public:
  explicit PgSupervisor(PgProcessConf conf, tserver::TabletServerIf* tserver);
  ~PgSupervisor();

  const PgProcessConf& conf() const {
    return conf_;
  }

  Status ReloadConfig();
  Status UpdateAndReloadConfig();
  std::shared_ptr<ProcessWrapper> CreateProcessWrapper() override;

  // Get the shared memory key to be used by ysql connection manager to publish stats
  key_t GetYsqlConnManagerStatsShmkey();

 private:
  Status CleanupOldServerUnlocked();
  Status RegisterPgFlagChangeNotifications() REQUIRES(mtx_);
  Status RegisterReloadPgConfigCallback(const void* flag_ptr) REQUIRES(mtx_);
  void DeregisterPgFlagChangeNotifications() REQUIRES(mtx_);

  PgProcessConf conf_;
  std::vector<FlagCallbackRegistration> flag_callbacks_ GUARDED_BY(mtx_);
  void PrepareForStop() REQUIRES(mtx_) override;
  Status PrepareForStart() REQUIRES(mtx_) override;
  key_t ysql_conn_mgr_stats_shmem_key_ = 0;

  std::string GetProcessName() override {
    return "PostgreSQL";
  }
};

}  // namespace pgwrapper
}  // namespace yb
