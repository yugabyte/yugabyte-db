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

#include <gtest/gtest.h>

#include "yb/gutil/strings/join.h"

#include "yb/util/path_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using std::string;

namespace yb {

ExternalYbController::ExternalYbController(
    const ExternalYbControllerOptions& options)
    : options_(options) {}

Status ExternalYbController::Start() {
  CHECK(!process_);

  std::vector<std::string> argv;
  // First the exe for argv[0].
  auto exe_path = GetYbcToolPath("yb-controller-server");
  argv.push_back(BaseName(exe_path));

  argv.push_back("--log_dir=" + options_.log_dir);
  argv.push_back("--tmp_dir=" + options_.tmp_dir);
  argv.push_back("--server_address=" + options_.server_address);
  argv.push_back("--yb_tserver_address=" + options_.yb_tserver_address);
  argv.push_back("--yb_admin=" + GetToolPath("yb-admin"));
  argv.push_back("--yb_ctl=" + GetToolPath("../../../bin", "yb-ctl"));
  argv.push_back("--ycqlsh=" + GetToolPath("../../../bin", "ycqlsh"));
  argv.push_back("--ysql_dump=" + GetPgToolPath("ysql_dump"));
  argv.push_back("--ysql_dumpall=" + GetPgToolPath("ysql_dumpall"));
  argv.push_back("--ysqlsh=" + GetPgToolPath("ysqlsh"));
  argv.push_back("--logtostderr");
  argv.push_back("--v=1");
  argv.push_back(Format("--server_port=$0", options_.server_port));
  argv.push_back(Format("--yb_master_webserver_port=$0", options_.yb_master_webserver_port));
  argv.push_back(Format("--yb_tserver_webserver_port=$0", options_.yb_tserver_webserver_port));
  argv.insert(argv.end(), options_.extra_flags.begin(), options_.extra_flags.end());

  auto p = std::make_unique<Subprocess>(exe_path, argv);
  p->PipeParentStdout();
  p->PipeParentStderr();

  LOG(INFO) << "Starting YB Controller with args: " << JoinStrings(argv, "\n");

  RETURN_NOT_OK_PREPEND(p->Start(), Format("Failed to start subprocess $0", exe_path));

  bool pingSuccess = false;
  int retries = 0;
  while (!pingSuccess && retries++ < 20) {
    auto status = ping();
    if (status.ok()) {
      pingSuccess = true;
      break;
    }
    // Allow some time for server initialisation.
    SleepFor(MonoDelta::FromMilliseconds(500));
  }

  if (!pingSuccess) {
    return STATUS_FORMAT(InternalError, "Failed to ping YB Controller server!");
  }

  std::string existing_prefix = TEST_GetThreadUnformattedLogPrefix();
  if (!existing_prefix.empty()) {
    existing_prefix += "-";
  }
  auto stdout_prefix = Format("[$0yb-controller-$1 stdout]", existing_prefix, options_.idx);
  auto stderr_prefix = Format("[$0yb-controller-$1]", existing_prefix, options_.idx);
  auto* listener = stdout_tailer_thread_ ? stdout_tailer_thread_->listener() : nullptr;
  stdout_tailer_thread_ = std::make_unique<ExternalDaemon::LogTailerThread>(
      stdout_prefix, p->ReleaseChildStdoutFd(), &std::cout);
  if (listener) {
    stdout_tailer_thread_->SetListener(listener);
  }

  listener = stderr_tailer_thread_ ? stderr_tailer_thread_->listener() : nullptr;
  // We will mostly see stderr output from the child process (because of --logtostderr), so we'll
  // assume that by default in the output prefix.
  stderr_tailer_thread_ = std::make_unique<ExternalDaemon::LogTailerThread>(
      stderr_prefix, p->ReleaseChildStderrFd(), &std::cerr);
  if (listener) {
    stderr_tailer_thread_->SetListener(listener);
  }

  process_.swap(p);

  return Status::OK();
}

Status ExternalYbController::ping() {
  std::vector<string> argv;

  // First the path to YB Controller CLI tool.
  argv.push_back(GetYbcToolPath("yb-controller-cli"));
  // Command to ping YB Controller server.
  argv.push_back("ping");
  argv.push_back("--tserver_ip=" + options_.server_address);
  argv.push_back(Format("--server_port=$0", options_.server_port));
  argv.insert(argv.end(), options_.extra_flags.begin(), options_.extra_flags.end());

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
  argv.push_back("--tserver_ip=" + options_.server_address);
  argv.push_back(Format("--server_port=$0", options_.server_port));
  argv.push_back("--wait");
  argv.insert(argv.end(), options_.extra_flags.begin(), options_.extra_flags.end());

  LOG(INFO) << "Run YB Controller CLI: " << AsString(argv);
  string output;
  auto status = Subprocess::Call(argv, &output);

  LOG(INFO) << "YB Controller shutdown result: " << status << ": " << output;

  if (!status.ok()) {
    LOG(INFO) << "Killing YB Controller process with SIGKILL";
    WARN_NOT_OK(process_->Kill(SIGKILL), "Killing YB Controller process failed");
  }

  // Manually cleanup left behind subprocesses if any.
  std::string cmd =
      "ps -ef | grep -v grep | grep " + options_.tmp_dir + " | awk '{print $2}' | xargs kill -9";
  std::vector<string> argvc = { "bash", "-c", cmd };
  string results;
  LOG(INFO) << "Killing YB Controller subprocesses";
  Status s = Subprocess::Call(argvc, &results);
  if (!s.ok()) {
    LOG(INFO) << "No subprocesses to kill! " << s.ToString();
  }
  LOG(INFO) << results;

  // Delete the tmp directory if present.
  status = (DeleteIfExists(options_.tmp_dir, Env::Default()));
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
  argv.insert(argv.end(), options_.extra_flags.begin(), options_.extra_flags.end());
  argv.push_back("--tserver_ip=" + options_.server_address);
  argv.push_back(Format("--server_port=$0", options_.server_port));
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
