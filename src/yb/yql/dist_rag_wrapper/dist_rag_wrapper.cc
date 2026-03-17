// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "yb/yql/dist_rag_wrapper/dist_rag_wrapper.h"

#include <string>
#include <vector>
#include <utility>

#include "yb/gutil/strings/strip.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"
#include "yb/util/pg_util.h"
#include "yb/util/result.h"
#include "yb/util/subprocess.h"
#include "yb/util/status_format.h"

DEFINE_RUNTIME_string(pg_dist_rag_conf_csv, "",
    "Comma-separated list of Distributed RAG service configuration parameters. "
    "Parameters should be of format: <key>=<value>. "
    "Whitespace is insignificant (except within a quoted value) and blank parameters are ignored. "
    "If the parameter contains a comma (,) or double-quote (\") then the entire parameter must be "
    "quoted with double-quote (\"): \"<key>=<value>\". "
    "Two double-quotes (\"\") in a double-quoted parameter represents a single double-quote (\"). "
    "Supported keys: AWS_S3_BUCKET_NAME, AWS_REGION, AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, "
    "OPENAI_API_KEY, SCRIPT_PATH. "
    "Note: Database connection string is automatically constructed from local PostgreSQL instance. "
    "Python environment setup (venv creation, pip install) must be done by yugabyted "
    "before starting. "
    "Example: --pg_dist_rag_conf_csv=AWS_S3_BUCKET_NAME=bucket,OPENAI_API_KEY=sk-proj-example");

namespace yb {

static std::string BuildLocalYsqlConnectionString(const HostPort& local_pg_host_port) {
  const auto unix_socket_dir = PgDeriveSocketDir(local_pg_host_port);
  // Connect via Unix socket with yugabyte user (no password needed for local connection).
  // Format: postgresql:///dbname?user=yugabyte&host=/socket/dir&port=5433
  return Format(
      "postgresql:///yugabyte?user=yugabyte&host=$0&port=$1",
      unix_socket_dir, local_pg_host_port.port());
}

struct DistRagServiceConf {
  std::string s3_bucket;
  std::string aws_region;
  std::string aws_access_key_id;
  std::string aws_secret_access_key;
  std::string openai_api_key;
  std::string script_path;

  std::string ToString() const;
  static Result<DistRagServiceConf> LoadFromCSV(const std::string& csv_config);
};

class DistRagServiceWrapper : public ProcessWrapper {
 public:
  explicit DistRagServiceWrapper(tserver::TabletServer& tablet_server);

  virtual ~DistRagServiceWrapper() = default;

  Status PreflightCheck() override;
  Status Start() override;
  Status ReloadConfig() override;
  Status UpdateAndReloadConfig() override;

 private:
  Status LoadConfig();
  std::string GetScriptPath();
  std::string GetLocalYsqlConnectionString();
  void SetEnvironmentVariables(Subprocess& proc);

  DistRagServiceConf conf_;
  tserver::TabletServer& tablet_server_;
  std::unique_ptr<LogTailerThread> stdout_logger_;
  std::unique_ptr<LogTailerThread> stderr_logger_;
};

}  // namespace yb

static bool ValidateDistRagConfCsv(const char* flag_name, const std::string& value) {
  if (value.empty()) {
    return true;
  }

  auto status = yb::DistRagServiceConf::LoadFromCSV(value);
  if (!status.ok()) {
    LOG_FLAG_VALIDATION_ERROR(flag_name, value) << status.status();
    return false;
  }
  return true;
}

DEFINE_validator(pg_dist_rag_conf_csv, &ValidateDistRagConfCsv);

namespace yb {

std::string DistRagServiceConf::ToString() const {
  return YB_STRUCT_TO_STRING(s3_bucket, aws_region, script_path);
}

Result<DistRagServiceConf> DistRagServiceConf::LoadFromCSV(
    const std::string& csv_config) {
  SCHECK(!csv_config.empty(), InvalidArgument, "CSV configuration cannot be empty");

  DistRagServiceConf conf;
  std::vector<std::string> config_entries;
  RETURN_NOT_OK(ReadCSVValues(csv_config, &config_entries));

  for (const std::string& config_entry : config_entries) {
    auto eq_pos = config_entry.find('=');
    SCHECK_FORMAT(
        eq_pos != std::string::npos, InvalidArgument,
        "Invalid config entry: $0 (expected key=value)", config_entry);

    auto key = config_entry.substr(0, eq_pos);
    auto value = config_entry.substr(eq_pos + 1);
    StripWhiteSpace(&key);
    StripWhiteSpace(&value);

    if (key == "AWS_S3_BUCKET_NAME") {
      conf.s3_bucket = value;
    } else if (key == "AWS_REGION") {
      conf.aws_region = value;
    } else if (key == "AWS_ACCESS_KEY_ID") {
      conf.aws_access_key_id = value;
    } else if (key == "AWS_SECRET_ACCESS_KEY") {
      conf.aws_secret_access_key = value;
    } else if (key == "OPENAI_API_KEY") {
      conf.openai_api_key = value;
    } else if (key == "SCRIPT_PATH") {
      conf.script_path = value;
    } else {
      LOG(WARNING) << "Unknown config key: " << key;
    }
  }

  // The RAG agent is located at python/ai/rag_agent/start_rag_agent.py
  // (same location for both development builds and release tarballs)
  if (conf.script_path.empty()) {
    const auto yb_root = yb::env_util::GetRootDir("python");
    conf.script_path = JoinPathSegments(yb_root, "python", "ai", "rag_agent", "start_rag_agent.py");
    VLOG(1) << "Using script_path: " << conf.script_path;
  }
  SCHECK_FORMAT(
      Env::Default()->FileExists(conf.script_path), NotFound,
      "DistRag service script not found: $0", conf.script_path);

  VLOG(1) << "Loaded RAG config";
  return conf;
}

DistRagServiceWrapper::DistRagServiceWrapper(
    tserver::TabletServer& tablet_server)
    : tablet_server_(tablet_server) {
}

Status DistRagServiceWrapper::PreflightCheck() {
  VLOG(1) << "Running preflight checks for DistRag service...";
  RETURN_NOT_OK(LoadConfig());

  auto script_path = GetScriptPath();
  SCHECK_FORMAT(
      Env::Default()->FileExists(script_path), NotFound,
      "DistRag service script not found: $0. "
      "Make sure run_server.py exists in share/dist_rag/",
      script_path);
  VLOG(1) << "Script found: " << script_path;

  VLOG(1) << "All preflight checks passed";
  return Status::OK();
}

Status DistRagServiceWrapper::Start() {
  RETURN_NOT_OK(LoadConfig());
  auto script_path = GetScriptPath();
  auto script_dir = DirName(script_path);

  auto venv_dir = JoinPathSegments(script_dir, "venv");
  auto python_exe = JoinPathSegments(venv_dir, "bin", "python");

  SCHECK_FORMAT(
      Env::Default()->FileExists(python_exe), NotFound,
      "Python executable not found at $0. "
      "Ensure python virtual environment is set up.", python_exe);

  VLOG(1) << "Using Python from virtual environment: " << python_exe;

  LOG(INFO) << "Starting DistRag Service...";
  VLOG(1) << "  Python: " << python_exe;
  VLOG(1) << "  Script: " << script_path;
  VLOG(1) << "  S3 Bucket: " << conf_.s3_bucket;

  auto service_cmd = Format("cd $0 && $1 $2", script_dir, python_exe, script_path);

  const std::vector<std::string> argv{
    "bash", "-c", service_cmd
  };

  VLOG(1) << "Command: " << service_cmd;

  proc_.emplace(argv[0], argv);
  SetEnvironmentVariables(proc_.value());
  proc_->PipeParentStdout();
  proc_->PipeParentStderr();

  RETURN_NOT_OK(proc_->Start());

  const auto process_name = "pg_dist_rag_service";
  stdout_logger_ = std::make_unique<LogTailerThread>(
      process_name, proc_->ReleaseChildStdoutFd(), google::GLOG_INFO);
  stderr_logger_ = std::make_unique<LogTailerThread>(
      process_name, proc_->ReleaseChildStderrFd(), google::GLOG_WARNING);

  LOG(INFO) << "Distributed RAG Service started successfully!";
  VLOG(1) << "  PID: " << proc_->pid();

  return Status::OK();
}

Status DistRagServiceWrapper::ReloadConfig() {
  if (!proc_) {
    return Status::OK();
  }
  VLOG(1) << "Reloading DistRag service configuration (SIGHUP)";
  Kill(SIGHUP);
  VLOG(1) << "SIGHUP sent to PID: " << proc_->pid();
  return Status::OK();
}

Status DistRagServiceWrapper::UpdateAndReloadConfig() {
  RETURN_NOT_OK(LoadConfig());
  return ReloadConfig();
}

Status DistRagServiceWrapper::LoadConfig() {
  auto new_conf = VERIFY_RESULT(DistRagServiceConf::LoadFromCSV(FLAGS_pg_dist_rag_conf_csv));
  std::swap(conf_, new_conf);
  return Status::OK();
}

std::string DistRagServiceWrapper::GetScriptPath() {
  return conf_.script_path;
}

std::string DistRagServiceWrapper::GetLocalYsqlConnectionString() {
  return BuildLocalYsqlConnectionString(tablet_server_.pgsql_proxy_bind_address());
}

void DistRagServiceWrapper::SetEnvironmentVariables(Subprocess& proc) {
  proc.SetEnv("YUGABYTEDB_CONNECTION_STRING", GetLocalYsqlConnectionString());
  proc.SetEnv("AWS_S3_BUCKET_NAME", conf_.s3_bucket);
  proc.SetEnv("AWS_REGION", conf_.aws_region);
  proc.SetEnv("AWS_ACCESS_KEY_ID", conf_.aws_access_key_id);
  proc.SetEnv("AWS_SECRET_ACCESS_KEY", conf_.aws_secret_access_key);
  proc.SetEnv("OPENAI_API_KEY", conf_.openai_api_key);

  VLOG(1) << "Environment variables set for RAG Service";
}

DistRagServiceSupervisor::DistRagServiceSupervisor(
    tserver::TabletServer& tablet_server)
    : tablet_server_(tablet_server) {
}

DistRagServiceSupervisor::~DistRagServiceSupervisor() {
  Stop();
}

std::shared_ptr<ProcessWrapper> DistRagServiceSupervisor::CreateProcessWrapper() {
  return std::make_shared<DistRagServiceWrapper>(tablet_server_);
}

Status DistRagServiceSupervisor::PrepareForStart() {
  VLOG(1) << "Preparing to start DistRag service...";
  return Status::OK();
}

}  // namespace yb
