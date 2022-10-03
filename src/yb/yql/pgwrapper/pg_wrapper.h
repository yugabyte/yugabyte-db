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

#ifndef YB_YQL_PGWRAPPER_PG_WRAPPER_H
#define YB_YQL_PGWRAPPER_PG_WRAPPER_H

#include <string>
#include <atomic>

#include <boost/optional.hpp>

#include "yb/gutil/ref_counted.h"
#include "yb/util/subprocess.h"
#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"

namespace yb {

class Thread;

namespace tserver {

class TabletServerIf;

} // namespace tserver

namespace pgwrapper {

// Returns the root directory of our PostgreSQL installation.
std::string GetPostgresInstallRoot();

// Configuration for an external PostgreSQL server.
struct PgProcessConf {
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
  std::string certs_dir;
  std::string certs_for_client_dir;
  std::string cert_base_name;
  bool enable_tls = false;

  // File descriptor of the local tserver's shared memory.
  int tserver_shm_fd = -1;

  // If this is true, we will not log to the file, even if the log file is specified.
  bool force_disable_log_file = false;
};

// Invokes a PostgreSQL child process once. Also allows invoking initdb. Not thread-safe.
class PgWrapper {
 public:
  explicit PgWrapper(PgProcessConf conf);

  // Checks if we have a valid configuration in order to be able to run PostgreSQL.
  Status PreflightCheck();

  Status Start();

  Status ReloadConfig();

  Status UpdateAndReloadConfig();

  void Kill();

  // Calls initdb if the data directory does not exist. This is intended to use during tablet server
  // initialization.
  Status InitDbLocalOnlyIfNeeded();

  // Calls PostgreSQL's initdb program for initial database initialization.
  // yb_enabled - whether initdb should be talking to YugaByte cluster, or just initialize a
  //              PostgreSQL data directory. The former is only done once from outside of the YB
  //              cluster, and the latter is done on every tablet server startup.
  Status InitDb(bool yb_enabled);

  // Waits for the running PostgreSQL process to complete. Returns the exit code or an error.
  // Non-zero exit codes are considered non-error cases for the purpose of this function.
  Result<int> Wait();

  // Run initdb in a mode that sets up the required metadata in the YB cluster. This is done
  // only once after the cluster has started up. tmp_dir_base is used as a base directory to
  // create a temporary PostgreSQL directory that is later deleted.
  static Status InitDbForYSQL(
      const std::string& master_addresses, const std::string& tmp_dir_base, int tserver_shm_fd);

 private:
  static std::string GetPostgresExecutablePath();
  static std::string GetPostgresLibPath();
  static std::string GetPostgresThirdPartyLibPath();
  static std::string GetInitDbExecutablePath();
  static Status CheckExecutableValid(const std::string& executable_path);

  // Set common environment for a child process (initdb or postgres itself).
  void SetCommonEnv(Subprocess* proc, bool yb_enabled);

  PgProcessConf conf_;
  boost::optional<Subprocess> pg_proc_;
};

YB_DEFINE_ENUM(PgProcessState,
    (kNotStarted)
    (kRunning)
    (kStopping)
    (kStopped));

// Keeps a PostgreSQL process running in the background, and restarts in case it crashes.
// Starts a separate thread to monitor the child process.
class PgSupervisor {
 public:
  explicit PgSupervisor(PgProcessConf conf, tserver::TabletServerIf* tserver);
  ~PgSupervisor();

  Status Start();
  void Stop();
  PgProcessState GetState();

  const PgProcessConf& conf() const {
    return conf_;
  }

  Status ReloadConfig();

  Status UpdateAndReloadConfig();

 private:
  Status ExpectStateUnlocked(PgProcessState state);
  Status StartServerUnlocked();
  void RunThread();
  Status CleanupOldServerUnlocked();

  PgProcessConf conf_;
  boost::optional<PgWrapper> pg_wrapper_;
  PgProcessState state_ = PgProcessState::kNotStarted;
  scoped_refptr<Thread> supervisor_thread_;
  std::mutex mtx_;
};

}  // namespace pgwrapper
}  // namespace yb

#endif  // YB_YQL_PGWRAPPER_PG_WRAPPER_H
