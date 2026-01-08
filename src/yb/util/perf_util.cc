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

#include "yb/util/perf_util.h"

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/path_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/tostring.h"

DEFINE_RUNTIME_int32(perf_record_timeout_sec, 120,
                     "Timeout for perf record in seconds to prevent runaway recordings.");

namespace yb {

namespace {
// Runs a command and returns the stdout. Logs the command to INFO logs.
Result<std::string> LogAndRunCommand(const std::vector<std::string>& cmd) {
  std::string out, err;
  auto status = Subprocess::Call(cmd, &out, &err);
  LOG(INFO) << "Command: " << AsString(cmd) << " output: " << out << " error: " << err;
  RETURN_NOT_OK_PREPEND(status, err);
  return out;
}
} // namespace

PerfProfiler::~PerfProfiler() {
  if (perf_proc_) {
    WARN_NOT_OK(perf_proc_->Kill(SIGKILL), "Failed to kill perf process");
  }
}

Status PerfProfiler::Start(int freq, const std::string& storage_dir) {
  if (perf_proc_) {
    return STATUS(IllegalState, "Perf profiling is already running");
  }

  storage_dir_ = storage_dir;
  const pid_t target_pid = getpid();
  const auto perf_record_path = Format("$0/profile.data", storage_dir);

  // Test to see if perf is installed.
  auto status = LogAndRunCommand({"perf", "--version"});
  if (!status.ok()) {
    return STATUS(NotFound,
      "Failed to get perf version. Consider running: sudo yum install perf && "
      "sudo yum install perl-open.noarch && sudo sysctl kernel.perf_event_paranoid=0 && "
      "sudo sysctl kernel.kptr_restrict=0");
  }

  // Record profile.
  // We use -p to attach to the current process.
  // We don't provide a command to run, so it will record until interrupted.
  std::vector<std::string> record_cmd = {
      "perf", "record",
      "-F", std::to_string(freq),
      "-p", std::to_string(target_pid),
      "-g",
      "-o", perf_record_path,
      "--", "sleep", std::to_string(FLAGS_perf_record_timeout_sec)
  };

  perf_proc_ = std::make_unique<Subprocess>("perf", record_cmd);
  RETURN_NOT_OK(perf_proc_->Start());

  return Status::OK();
}

Result<PerfProfilerStopResult> PerfProfiler::Stop() {
  if (!perf_proc_) {
    return STATUS(IllegalState, "Perf profiling is not running");
  }

  // Send SIGINT to stop recording
  RETURN_NOT_OK(perf_proc_->Kill(SIGINT));
  int ret_code;
  RETURN_NOT_OK(perf_proc_->Wait(&ret_code));
  perf_proc_.reset();

  if (ret_code != 0) {
    LOG(WARNING) << "Perf record exited with code: " << ret_code;
  }

  const auto perf_record_path = Format("$0/profile.data", storage_dir_);
  const auto perf_script_path = Format("$0/profile-script.txt", storage_dir_);
  const auto collapsed_stacks_name = "profile-collapsed.txt";
  const auto collapsed_stacks_path = Format("$0/$1", storage_dir_, collapsed_stacks_name);

  // Run perf script to generate stacks.
  std::vector<std::string> script_cmd = {"perf", "script", "-i", perf_record_path};
  auto script_out = VERIFY_RESULT(LogAndRunCommand(script_cmd));
  RETURN_NOT_OK_PREPEND(WriteStringToFile(Env::Default(), script_out, perf_script_path),
      Format("Failed to write perf script output to '$0'", perf_script_path));

  // Collapse stacks.
  std::vector<std::string> collapse_cmd =
      {VERIFY_RESULT(path_utils::GetToolPath("stackcollapse-perf.pl")), perf_script_path};
  auto collapsed_out = VERIFY_RESULT(LogAndRunCommand(collapse_cmd));
  RETURN_NOT_OK_PREPEND(WriteStringToFile(Env::Default(), collapsed_out, collapsed_stacks_path),
      "Failed to write collapsed stacks output to '" + collapsed_stacks_path + "'");

  std::vector<std::string> flamegraph_cmd =
      {VERIFY_RESULT(path_utils::GetToolPath("flamegraph.pl")), collapsed_stacks_path};
  return PerfProfilerStopResult{
    .collapsed_stacks_name = collapsed_stacks_name,
    .flamegraph = VERIFY_RESULT(LogAndRunCommand(flamegraph_cmd))
  };
}

} // namespace yb
