// Copyright (c) Yugabyte, Inc.
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

#include "yb/yql/process_wrapper/process_wrapper.h"

#include "yb/rpc/secure.h"

#include "yb/server/server_base_options.h"

DECLARE_string(certs_dir);
DECLARE_string(certs_for_client_dir);
DECLARE_string(cert_node_filename);
DECLARE_bool(use_client_to_server_encryption);

namespace yb {

// ------------------------------------------------------------------------------------------------
// ProcessWrapper: managing one instance of a child process
// ------------------------------------------------------------------------------------------------
Status ProcessWrapper::CheckExecutableValid(const std::string& executable_path) {
  if (VERIFY_RESULT(Env::Default()->IsExecutableFile(executable_path))) {
    return Status::OK();
  }
  return STATUS_FORMAT(NotFound, "Not an executable file: $0", executable_path);
}

Result<int> ProcessWrapper::Wait() {
  if (!proc_) {
    return STATUS(IllegalState, "Child process has not been started, cannot wait for it to exit");
  }
  return proc_->Wait();
}

void ProcessWrapper::Kill() {
  int signal = SIGINT;
  // TODO(fizaa): Use SIGQUIT in asan build until GH #15168 is fixed.
#ifdef ADDRESS_SANITIZER
  signal = SIGQUIT;
#endif
  WARN_NOT_OK(proc_->Kill(signal), "Kill process failed");
}


// ------------------------------------------------------------------------------------------------
// ProcessWrapper: managing one instance of a child process
// ------------------------------------------------------------------------------------------------
YbSubProcessState ProcessSupervisor::GetState() {
  std::lock_guard lock(mtx_);
  return state_;
}

Status ProcessSupervisor::ExpectStateUnlocked(YbSubProcessState expected_state) {
  SCHECK_EQ(state_, expected_state, IllegalState, "Process is in unexpected state");
  return Status::OK();
}

void ProcessSupervisor::RunThread() {
  std::string process_name = GetProcessName();
  while (true) {
    Result<int> wait_result = process_wrapper_->Wait();
    if (wait_result.ok()) {
      int ret_code = *wait_result;
      if (ret_code == 0) {
        LOG(INFO) << process_name << " exited normally";
      } else {
        util::LogWaitCode(ret_code, process_name);
      }
      process_wrapper_.reset();
    } else {
      LOG(WARNING) << "Failed when waiting for process to exit: " << wait_result.status();

      // Don't continue waiting in the loop if it is IllegalState as this means the process is not
      // currently running at all, perhaps due to failure to start. In this case, the
      // process_wrapper is not initilized. So there isn't a process to wait.
      if (!wait_result.status().IsIllegalState()) {
        LOG(INFO) << "Wait a bit next process_wrapper wait-check for " << process_name;
        SleepFor(std::chrono::seconds(1));
        continue;
      }
    }

    {
      std::lock_guard lock(mtx_);
      if (state_ == YbSubProcessState::kStopping) {
        break;
      }
      LOG(INFO) << "Restarting " << process_name << "process";
      Status start_status = StartProcessUnlocked();
      if (!start_status.ok()) {
        LOG(WARNING) << "Failed trying to start " << process_name
                     << " process: " << start_status << ", waiting a bit";
        SleepFor(std::chrono::seconds(1));
      }
    }
  }
}

Status ProcessSupervisor::StartProcessUnlocked() {
  if (process_wrapper_) {
    RSTATUS_DCHECK(!process_wrapper_, IllegalState, "Expecting 'process_wrapper_' to not be set");
  }
  auto process_wrapper = CreateProcessWrapper();
  RETURN_NOT_OK(process_wrapper->Start());

  process_wrapper_.swap(process_wrapper);
  return Status::OK();
}

Status ProcessSupervisor::Start() {
  std::lock_guard lock(mtx_);
  std::string process_name = GetProcessName();
  RETURN_NOT_OK(ExpectStateUnlocked(YbSubProcessState::kNotStarted));
  RETURN_NOT_OK(PrepareForStart());
  LOG(INFO) << "Starting "  << process_name << " process";

  RETURN_NOT_OK(StartProcessUnlocked());

  std::string thread_name = process_name + " supervisor";
  Status status = Thread::Create(
      thread_name, thread_name, &ProcessSupervisor::RunThread,
      this, &supervisor_thread_);
  if (!status.ok()) {
    supervisor_thread_.reset();
    return status;
  }

  state_ = YbSubProcessState::kRunning;

  return Status::OK();
}

void ProcessSupervisor::Stop() {
  {
    std::lock_guard lock(mtx_);
    state_ = YbSubProcessState::kStopping;
    PrepareForStop();
    if (process_wrapper_) {
      process_wrapper_->Kill();
    }
  }
  supervisor_thread_->Join();
}

Status ProcessWrapperCommonConfig::SetSslConf(
    const server::ServerBaseOptions& options, FsManager& fs_manager) {
  this->certs_dir =
      FLAGS_certs_dir.empty() ? rpc::GetCertsDir(fs_manager.GetDefaultRootDir()) : FLAGS_certs_dir;
  this->certs_for_client_dir =
      FLAGS_certs_for_client_dir.empty() ? this->certs_dir : FLAGS_certs_for_client_dir;
  this->enable_tls = FLAGS_use_client_to_server_encryption;

  // Follow the same logic as elsewhere, check FLAGS_cert_node_filename then
  // server_broadcast_addresses then rpc_bind_addresses.
  if (!FLAGS_cert_node_filename.empty()) {
    this->cert_base_name = FLAGS_cert_node_filename;
  } else {
    const auto server_broadcast_addresses =
        HostPort::ParseStrings(options.server_broadcast_addresses, 0);
    RETURN_NOT_OK(server_broadcast_addresses);
    const auto rpc_bind_addresses = HostPort::ParseStrings(options.rpc_opts.rpc_bind_addresses, 0);
    RETURN_NOT_OK(rpc_bind_addresses);
    this->cert_base_name = !server_broadcast_addresses->empty()
                               ? server_broadcast_addresses->front().host()
                               : rpc_bind_addresses->front().host();
  }

  return Status::OK();
}

}  // namespace yb
