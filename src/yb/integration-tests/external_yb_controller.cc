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
//

#include "yb/integration-tests/external_yb_controller.h"

#include "yb/gutil/strings/join.h"

#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/test_util.h"

using std::string;

namespace yb {

ExternalYbController::ExternalYbController(
    const size_t idx, const string& log_dir, const string& tmp_dir,
    const string& yb_tserver_address, const string& yb_admin, const string& yb_ctl,
    const string& ycqlsh, const string& ysql_dump, const string& ysql_dumpall, const string& ysqlsh,
    uint16_t server_port, uint16_t yb_master_webserver_port, uint16_t yb_tserver_webserver_port,
    const string& server_address, const string& exe, const std::vector<string>& extra_flags)
    : idx_(idx),
      exe_(exe),
      log_dir_(log_dir),
      tmp_dir_(tmp_dir),
      server_address_(server_address),
      yb_tserver_address_(yb_tserver_address),
      yb_admin_(yb_admin),
      yb_ctl_(yb_ctl),
      ycqlsh_(ycqlsh),
      ysql_dump_(ysql_dump),
      ysql_dumpall_(ysql_dumpall),
      ysqlsh_(ysqlsh),
      server_port_(server_port),
      yb_master_webserver_port_(yb_master_webserver_port),
      yb_tserver_webserver_port_(yb_tserver_webserver_port),
      extra_flags_(extra_flags) {}

Status ExternalYbController::Start() {
  CHECK(!process_);

  std::vector<string> argv;
  // First the exe for argv[0].
  argv.push_back(BaseName(exe_));

  argv.push_back("--log_dir=" + log_dir_);
  argv.push_back("--tmp_dir=" + tmp_dir_);
  argv.push_back("--server_address=" + server_address_);
  argv.push_back("--yb_tserver_address=" + yb_tserver_address_);
  argv.push_back("--yb_admin=" + yb_admin_);
  argv.push_back("--yb_ctl=" + yb_ctl_);
  argv.push_back("--ycqlsh=" + ycqlsh_);
  argv.push_back("--ysql_dump=" + ysql_dump_);
  argv.push_back("--ysql_dumpall=" + ysql_dumpall_);
  argv.push_back("--ysqlsh=" + ysqlsh_);
  argv.push_back("--logtostderr");
  argv.push_back(Format("--server_port=$0", server_port_));
  argv.push_back(Format("--yb_master_webserver_port=$0", yb_master_webserver_port_));
  argv.push_back(Format("--yb_tserver_webserver_port=$0", yb_tserver_webserver_port_));
  argv.insert(argv.end(), extra_flags_.begin(), extra_flags_.end());

  std::unique_ptr<Subprocess> p(new Subprocess(exe_, argv));
  p->PipeParentStdout();
  p->PipeParentStderr();

  LOG(INFO) << "Starting YB Controller with args: " << JoinStrings(argv, "\n");

  RETURN_NOT_OK_PREPEND(p->Start(), Format("Failed to start subprocess $0", exe_));

  // Allow some time for server initialisation.
  SleepFor(MonoDelta::FromMilliseconds(500));

  // Make sure we can ping the server.
  CHECK_OK(ping());

  process_.swap(p);

  return Status::OK();
}

Status ExternalYbController::ping() {
  std::vector<string> argv;

  // First the path to YB Controller CLI tool.
  argv.push_back(GetYbcToolPath("yb-controller-cli"));
  // Command to ping YB Controller server.
  argv.push_back("ping");
  argv.push_back("--tserver_ip=" + server_address_);
  argv.push_back(Format("--server_port=$0", server_port_));
  argv.insert(argv.end(), extra_flags_.begin(), extra_flags_.end());

  LOG(INFO) << "Run YB Controller CLI: " << AsString(argv);
  string output;
  RETURN_NOT_OK(Subprocess::Call(argv, &output));

  LOG(INFO) << "YB Controller ping result: " << output;
  CHECK(output.find("Ping successful!") != string::npos)
      << "Pinging YB Controller server unsuccessful";

  return Status::OK();
}

Status ExternalYbController::Restart() {
  Status status = ping();
  // Shutdown if server is running.
  if (status.ok()) {
    Shutdown();
    // Wait some time after shutdown.
    SleepFor(MonoDelta::FromMilliseconds(500));
  }
  return Start();
}

void ExternalYbController::Shutdown() {
  if (!process_) {
    return;
  }
  std::vector<string> argv;
  // First the path to YB Controller CLI tool.
  argv.push_back(GetYbcToolPath("yb-controller-cli"));
  // Command to shutdown YB Controller server.
  argv.push_back("shutdown");
  argv.push_back("--tserver_ip=" + server_address_);
  argv.push_back(Format("--server_port=$0", server_port_));
  argv.push_back("--wait");
  argv.insert(argv.end(), extra_flags_.begin(), extra_flags_.end());

  LOG(INFO) << "Run YB Controller CLI: " << AsString(argv);
  string output;
  auto status = Subprocess::Call(argv, &output);

  LOG(INFO) << "YB Controller shutdown result: " << status << ": " << output;

  if (!status.ok()) {
    LOG(INFO) << "Killing YB Controller process with SIGKILL";
    WARN_NOT_OK(process_->Kill(SIGKILL), "Killing YB Controller process failed");
  }

  // Delete the tmp directory if present.
  status = (DeleteIfExists(tmp_dir_, Env::Default()));
  if (!status.ok()) {
    LOG(WARNING) << "Error while deleting YB Controller temp dir: " << status;
  }

  process_ = nullptr;
}

bool ExternalYbController::IsShutdown() const {
  return process_.get() == nullptr;
}

Status ExternalYbController::RunBackupCommand(
    const string& backup_dir, const string& backup_command, const string& ns, const string& ns_type,
    const string& temp_dir, const bool use_tablespaces) {
  std::vector<string> argv;
  string bucket = BaseName(backup_dir);

  // First the path to YB Controller CLI tool.
  argv.push_back(GetYbcToolPath("yb-controller-cli"));
  argv.push_back(backup_command);
  argv.push_back("--bucket=" + bucket);
  argv.push_back("--cloud_dir=yugabyte");
  argv.push_back("--cloud_type=nfs");
  argv.push_back("--ns_type=" + ns_type);
  argv.push_back("--ns=" + ns);
  argv.push_back("--wait");
  argv.insert(argv.end(), extra_flags_.begin(), extra_flags_.end());
  argv.push_back("--tserver_ip=" + server_address_);
  argv.push_back(Format("--server_port=$0", server_port_));
  argv.push_back("--max_timeout_secs=180");

  if (use_tablespaces) {
    argv.push_back("--use_tablespaces");
  }

  LOG(INFO) << "Setting YBC_NFS_DIR as " << temp_dir;
  setenv("YBC_NFS_DIR", temp_dir.c_str(), true);

  if (backup_command == "backup") {
    RETURN_NOT_OK(Env::Default()->CreateDirs(backup_dir));
  }

  LOG(INFO) << "Run YB Controller CLI: " << AsString(argv);

  string output;
  auto status = Subprocess::Call(argv, &output);
  LOG(INFO) << "YB Controller " << backup_command << " status: " << status << " output: " << output;

  if (output.find("Final Status: OK") == string::npos) {
    return STATUS_FORMAT(
        InternalError,
        "YB Controller " + backup_command + " command failed with output: " + output);
  }
  return Status::OK();
}

ExternalYbController::~ExternalYbController() {
  Shutdown();
}

}  // namespace yb
