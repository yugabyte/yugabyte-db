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
#include "yb/util/test_util.h"

using std::string;

namespace yb {

ExternalYbController::ExternalYbController(
    const string& log_dir, const string& tmp_dir, const string& yb_tserver_address,
    const string& yb_admin, const string& yb_ctl, const string& ycqlsh, const string& ysql_dump,
    const string& ysql_dumpall, const string& ysqlsh, uint16_t server_port,
    uint16_t yb_master_webserver_port, uint16_t yb_tserver_webserver_port,
    const string& server_address, const string& exe, const std::vector<string>& extra_flags)
    : exe_(exe),
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
  // First the exe for argv[0]
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
  argv.push_back(Format("--server_port=$0", server_port_));
  argv.push_back(Format("--yb_master_webserver_port=$0", yb_master_webserver_port_));
  argv.push_back(Format("--yb_tserver_webserver_port=$0", yb_tserver_webserver_port_));
  argv.insert(argv.end(), extra_flags_.begin(), extra_flags_.end());

  std::unique_ptr<Subprocess> p(new Subprocess(exe_, argv));
  p->PipeParentStdout();
  p->PipeParentStderr();

  LOG(INFO) << "Starting YBC with args " << JoinStrings(argv, "\n");

  RETURN_NOT_OK_PREPEND(p->Start(), Format("Failed to start subprocess $0", exe_));

  // allow some time for server initialisation
  SleepFor(MonoDelta::FromMilliseconds(500));

  // make sure we can ping the server
  CHECK_OK(ping());

  process_.swap(p);

  return Status::OK();
}

Status ExternalYbController::ping() {
  std::vector<string> argv;

  // first the path to yb controller cli tool
  argv.push_back(GetYbcToolPath("yb-controller-cli"));
  // command to ping yb controller server
  argv.push_back("ping");
  argv.push_back("--tserver_ip=" + server_address_);
  argv.push_back(Format("--server_port=$0", server_port_));
  argv.insert(argv.end(), extra_flags_.begin(), extra_flags_.end());

  LOG(INFO) << "Run tool: " << AsString(argv);
  string output;
  RETURN_NOT_OK(Subprocess::Call(argv, &output));

  LOG(INFO) << "YBC ping result: " << output;
  CHECK(output.find("Ping successful!") != string::npos)
      << "Pinging YB Controller server unsuccessful";

  return Status::OK();
}

Status ExternalYbController::Restart() {
  Status status = ping();
  // Shutdown if server is running
  if (status.ok()) {
    Shutdown();
    // wait some time after shutdown
    SleepFor(MonoDelta::FromMilliseconds(500));
  }
  return Start();
}

void ExternalYbController::Shutdown() {
  if (!process_) {
    return;
  }

  LOG(INFO) << "Killing YB Controller process with SIGKILL";
  WARN_NOT_OK(process_->Kill(SIGKILL), "Killing YB Controller process failed");

  process_ = nullptr;
}

bool ExternalYbController::IsShutdown() const {
  return process_.get() == nullptr;
}

ExternalYbController::~ExternalYbController() {
  Shutdown();
}

}  // namespace yb
