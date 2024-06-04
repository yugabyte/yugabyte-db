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

#include "yb/integration-tests/external_daemon.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/algorithm/string.hpp>

#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"

#include "yb/gutil/singleton.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/util.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/master/master_rpc.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/jsonreader.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using std::atomic;
using std::lock_guard;
using std::mutex;
using std::ostream;
using std::string;
using std::thread;
using std::unique_ptr;
using std::vector;

using rapidjson::Value;
using yb::rpc::RpcController;
using yb::server::ServerStatusPB;

DECLARE_string(vmodule);
DECLARE_bool(mem_tracker_logging);
DECLARE_bool(mem_tracker_log_stack_trace);
DECLARE_bool(use_libbacktrace);

DEFINE_NON_RUNTIME_string(
    external_daemon_heap_profile_prefix, "",
    "If this is not empty, tcmalloc's HEAPPROFILE is set this, followed by a unique "
    "suffix for external mini-cluster daemons.");

DEFINE_NON_RUNTIME_int64(
    external_mini_cluster_max_log_bytes, 50_MB * 100,
    "Max total size of log bytes produced by all external mini-cluster daemons. "
    "The test is shut down if this limit is exceeded.");

namespace yb {

static double kProcessStartTimeoutSeconds = 60.0;
static MonoDelta kTabletServerRegistrationTimeout = 60s;

static const int kHeapProfileSignal = SIGUSR1;

// Global state to manage all log tailer threads. This state is managed using Singleton from gutil
// and is never deallocated.
struct GlobalLogTailerState {
  std::mutex logging_mutex;
  std::atomic<int> next_log_tailer_id{0};

  // We need some references to these heap-allocated atomic booleans so that ASAN would not consider
  // them memory leaks.
  std::mutex id_to_stopped_flag_mutex;
  std::map<int, std::atomic<bool>*> id_to_stopped_flag;

  // This is used to limit the total amount of logs produced by external daemons over the lifetime
  // of a test program. Guarded by logging_mutex.
  size_t total_bytes_logged = 0;
};

ExternalDaemon::LogTailerThread::LogTailerThread(
    const std::string& line_prefix, const int child_fd, ostream* const out)
    : id_(global_state()->next_log_tailer_id.fetch_add(1)),
      stopped_(CreateStoppedFlagForId(id_)),
      thread_desc_(Format("log tailer thread for prefix $0", line_prefix)),
      thread_([this, line_prefix, child_fd, out] {
        VLOG(1) << "Starting " << thread_desc_;
        FILE* const fp = fdopen(child_fd, "rb");
        char buf[65536];
        const atomic<bool>* stopped;

        {
          lock_guard<mutex> l(state_lock_);
          stopped = stopped_;
        }

        // Instead of doing a nonblocking read, we detach this thread and allow it to block
        // indefinitely trying to read from a child process's stream where nothing is happening.
        // This is probably OK as long as we are careful to avoid accessing any state that might
        // have been already destructed (e.g. logging, cout/cerr, member fields of this class,
        // etc.) in case we do get unblocked. Instead, we keep a local pointer to the atomic
        // "stopped" flag, and that allows us to safely check if it is OK to print log messages.
        // The "stopped" flag itself is never deallocated.
        bool is_eof = false;
        bool is_fgets_null = false;
        auto& logging_mutex = global_state()->logging_mutex;
        auto& total_bytes_logged = global_state()->total_bytes_logged;
        while (!(is_eof = feof(fp)) &&
               !(is_fgets_null = (fgets(buf, sizeof(buf), fp) == nullptr)) && !stopped->load()) {
          size_t l = strlen(buf);
          const char* maybe_end_of_line = l > 0 && buf[l - 1] == '\n' ? "" : "\n";
          // Synchronize tailing output from all external daemons for simplicity.
          lock_guard<mutex> lock(logging_mutex);
          if (stopped->load()) break;
          // Make sure we always output an end-of-line character.
          *out << line_prefix << " " << buf << maybe_end_of_line;
          if (!stopped->load()) {
            auto listener = listener_.load(std::memory_order_acquire);
            if (!stopped->load() && listener) {
              listener->Handle(GStringPiece(buf, maybe_end_of_line ? l : l - 1));
            }
          }
          total_bytes_logged += strlen(buf) + strlen(maybe_end_of_line);
          // Abort the test if it produces too much log spew.
          CHECK_LE(total_bytes_logged, FLAGS_external_mini_cluster_max_log_bytes);
        }
        fclose(fp);
        if (!stopped->load()) {
          // It might not be safe to log anything if we have already stopped.
          VLOG(1) << "Exiting " << thread_desc_ << ": is_eof=" << is_eof
                  << ", is_fgets_null=" << is_fgets_null << ", stopped=0";
        }
      }) {
  thread_.detach();
}

void ExternalDaemon::LogTailerThread::SetListener(StringListener* listener) {
  listener_ = listener;
}

GlobalLogTailerState* ExternalDaemon::LogTailerThread::global_state() {
  return Singleton<GlobalLogTailerState>::get();
}

void ExternalDaemon::LogTailerThread::RemoveListener(StringListener* listener) {
  listener_.compare_exchange_strong(listener, nullptr);
}

ExternalDaemon::LogTailerThread::~LogTailerThread() {
  VLOG(1) << "Stopping " << thread_desc_;
  lock_guard<mutex> l(state_lock_);
  stopped_->store(true);
  listener_ = nullptr;
}

atomic<bool>* ExternalDaemon::LogTailerThread::CreateStoppedFlagForId(int id) {
  lock_guard<mutex> lock(global_state()->id_to_stopped_flag_mutex);
  // This is never deallocated, but we add this pointer to the id_to_stopped_flag map referenced
  // from the global state singleton, and that apparently makes ASAN no longer consider this to be
  // a memory leak. We don't need to check if the id already exists in the map, because this
  // function is never invoked with a particular id more than once.
  auto* const stopped = new atomic<bool>();
  stopped->store(false);
  global_state()->id_to_stopped_flag[id] = stopped;
  return stopped;
}

ExternalDaemon::ExternalDaemon(
    std::string daemon_id, rpc::Messenger* messenger, rpc::ProxyCache* proxy_cache,
    const string& exe, const string& root_dir, const std::vector<std::string>& data_dirs,
    const vector<string>& extra_flags)
    : daemon_id_(daemon_id),
      messenger_(messenger),
      proxy_cache_(proxy_cache),
      exe_(exe),
      root_dir_(root_dir),
      data_dirs_(data_dirs),
      extra_flags_(extra_flags) {}

ExternalDaemon::~ExternalDaemon() {}

bool ExternalDaemon::ServerInfoPathsExist() {
  return Env::Default()->FileExists(GetServerInfoPath());
}

Status ExternalDaemon::BuildServerStateFromInfoPath() {
  return BuildServerStateFromInfoPath(GetServerInfoPath(), &status_);
}

Status ExternalDaemon::BuildServerStateFromInfoPath(
    const string& info_path, std::unique_ptr<ServerStatusPB>* server_status) {
  server_status->reset(new ServerStatusPB());
  RETURN_NOT_OK_PREPEND(
      pb_util::ReadPBFromPath(Env::Default(), info_path, (*server_status).get()),
      "Failed to read info file from " + info_path);
  return Status::OK();
}

string ExternalDaemon::GetServerInfoPath() {
  return JoinPathSegments(root_dir_, "info.pb");
}

Status ExternalDaemon::DeleteServerInfoPaths() {
  return Env::Default()->DeleteFile(GetServerInfoPath());
}

Status ExternalDaemon::StartProcess(const vector<string>& user_flags) {
  CHECK(!process_);

  vector<string> argv;
  // First the exe for argv[0]
  argv.push_back(BaseName(exe_));

  // Then all the flags coming from the minicluster framework.
  argv.insert(argv.end(), user_flags.begin(), user_flags.end());

  // Disable callhome.
  argv.push_back("--callhome_enabled=false");

  // Disabled due to #4507.
  // TODO: Enable metrics logging after #4507 is fixed.
  //
  // Even though we set -logtostderr down below, metrics logs end up being written
  // based on -log_dir. So, we have to set that too.
  argv.push_back("--metrics_log_interval_ms=0");

  // Force set log_dir to empty value, process will chose default destination inside fs_data_dir
  // In other case log_dir value will be extracted from TEST_TMPDIR env variable but it is
  // inherited from test script
  argv.push_back("--log_dir=");

  // Tell the server to dump its port information so we can pick it up.
  const string info_path = GetServerInfoPath();
  argv.push_back("--server_dump_info_path=" + info_path);
  argv.push_back("--server_dump_info_format=pb");

  // We use ephemeral ports in many tests. They don't work for production, but are OK
  // in unit tests.
  argv.push_back("--rpc_server_allow_ephemeral_ports");

  // A previous instance of the daemon may have run in the same directory. So, remove
  // the previous info file if it's there.
  Status s = DeleteServerInfoPaths();
  if (!s.ok() && !s.IsNotFound()) {
    LOG(WARNING) << "Failed to delete info paths: " << s.ToString();
  }

  // Ensure that logging goes to the test output doesn't get buffered.
  argv.push_back("--logbuflevel=-1");

  // Use the same verbose logging level in the child process as in the test driver.
  if (FLAGS_v != 0) {  // Skip this option if it has its default value (0).
    argv.push_back(Format("-v=$0", FLAGS_v));
  }
  if (!FLAGS_vmodule.empty()) {
    argv.push_back(Format("--vmodule=$0", FLAGS_vmodule));
  }
  if (FLAGS_mem_tracker_logging) {
    argv.push_back("--mem_tracker_logging");
  }
  if (FLAGS_mem_tracker_log_stack_trace) {
    argv.push_back("--mem_tracker_log_stack_trace");
  }
  if (FLAGS_use_libbacktrace) {
    argv.push_back("--use_libbacktrace");
  }

  const char* test_invocation_id = getenv("YB_TEST_INVOCATION_ID");
  if (test_invocation_id) {
    // We use --metric_node_name=... to include a unique "test invocation id" into the command
    // line so we can kill any stray processes later. --metric_node_name is normally how we pass
    // the Universe ID to the cluster. We could use any other flag that is present in yb-master
    // and yb-tserver for this.
    argv.push_back(Format("--metric_node_name=$0", test_invocation_id));
  }

  string fatal_details_path_prefix = GetFatalDetailsPathPrefix();
  argv.push_back(
      Format("--fatal_details_path_prefix=$0.$1", GetFatalDetailsPathPrefix(), daemon_id_));

  argv.push_back(Format("--minicluster_daemon_id=$0", daemon_id_));

  // Finally, extra flags to override.
  // - extra_flags_ is taken from ExternalMiniCluster.opts_, which is often set by test subclasses'
  //   UpdateMiniClusterOptions.
  // - extra daemon flags is supplied by the user, either through environment variable or
  //   yb_build.sh --extra_daemon_flags (or --extra_daemon_args), so it should take highest
  //   precedence.
  argv.insert(argv.end(), extra_flags_.begin(), extra_flags_.end());
  AddExtraFlagsFromEnvVar("YB_EXTRA_DAEMON_FLAGS", &argv);

  auto p = std::make_unique<Subprocess>(exe_, argv);
  p->PipeParentStdout();
  p->PipeParentStderr();
  auto default_output_prefix = Format("[$0]", daemon_id_);
  LOG(INFO) << "Running " << default_output_prefix << ": " << exe_ << "\n"
            << JoinStrings(argv, "\n");
  if (!FLAGS_external_daemon_heap_profile_prefix.empty()) {
    p->SetEnv("HEAPPROFILE", FLAGS_external_daemon_heap_profile_prefix + "_" + daemon_id_);
    p->SetEnv("HEAPPROFILESIGNAL", std::to_string(kHeapProfileSignal));
  }

  RETURN_NOT_OK_PREPEND(p->Start(), Format("Failed to start subprocess $0", exe_));

  auto* listener = stdout_tailer_thread_ ? stdout_tailer_thread_->listener() : nullptr;
  stdout_tailer_thread_ = std::make_unique<LogTailerThread>(
      Format("[$0 stdout]", daemon_id_), p->ReleaseChildStdoutFd(), &std::cout);
  if (listener) {
    stdout_tailer_thread_->SetListener(listener);
  }

  listener = stderr_tailer_thread_ ? stderr_tailer_thread_->listener() : nullptr;
  // We will mostly see stderr output from the child process (because of --logtostderr), so we'll
  // assume that by default in the output prefix.
  stderr_tailer_thread_ = std::make_unique<LogTailerThread>(
      default_output_prefix, p->ReleaseChildStderrFd(), &std::cerr);
  if (listener) {
    stderr_tailer_thread_->SetListener(listener);
  }

  // The process is now starting -- wait for the bound port info to show up.
  Stopwatch sw;
  sw.start();
  bool success = false;
  while (sw.elapsed().wall_seconds() < kProcessStartTimeoutSeconds) {
    if (ServerInfoPathsExist()) {
      success = true;
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
    int rc;
    Status s = p->WaitNoBlock(&rc);
    if (s.IsTimedOut()) {
      // The process is still running.
      continue;
    }
    RETURN_NOT_OK_PREPEND(s, Format("Failed waiting on $0", exe_));
    return STATUS(RuntimeError, Format("Process exited with rc=$0", rc), exe_);
  }

  if (!success) {
    WARN_NOT_OK(p->Kill(SIGKILL), "Killing process failed");
    return STATUS(
        TimedOut, Format(
                      "Timed out after $0s waiting for process ($1) to write info file ($2)",
                      kProcessStartTimeoutSeconds, exe_, info_path));
  }

  RETURN_NOT_OK(BuildServerStateFromInfoPath());
  LOG(INFO) << "Started " << default_output_prefix << " " << exe_ << " as pid " << p->pid();
  VLOG(1) << exe_ << " instance information:\n" << status_->DebugString();

  process_.swap(p);
  return Status::OK();
}

Status ExternalDaemon::Pause() {
  if (!process_) return Status::OK();
  VLOG(1) << "Pausing " << ProcessNameAndPidStr();
  RETURN_NOT_OK(process_->Kill(SIGSTOP));
  is_paused_ = true;
  return Status::OK();
}

Status ExternalDaemon::Resume() {
  if (!process_) return Status::OK();
  VLOG(1) << "Resuming " << ProcessNameAndPidStr();
  RETURN_NOT_OK(process_->Kill(SIGCONT));
  is_paused_ = false;
  return Status::OK();
}

Status ExternalDaemon::Kill(int signal) {
  if (!process_) return Status::OK();
  VLOG(1) << "Kill " << ProcessNameAndPidStr() << " with " << signal;
  return process_->Kill(signal);
}

bool ExternalDaemon::IsShutdown() const {
  return process_.get() == nullptr;
}

bool ExternalDaemon::WasUnsafeShutdown() const {
  return sigkill_used_for_shutdown_;
}

bool ExternalDaemon::IsProcessAlive(RequireExitCode0 require_exit_code_0) const {
  if (IsShutdown()) {
    return false;
  }

  int rc = 0;
  Status s = process_->WaitNoBlock(&rc);

  // Return code will be non-zero if the process crashed.
  if (require_exit_code_0 && rc != 0) {
    LOG(DFATAL) << "Non-zero return code " << rc << " for WaitNoBlock for daemon " << daemon_id_;
  }

  // If the non-blocking Wait "times out", that means the process
  // is running.
  return s.IsTimedOut();
}

bool ExternalDaemon::IsProcessPaused() const {
  return is_paused_;
}

pid_t ExternalDaemon::pid() const {
  return process_->pid();
}

void ExternalDaemon::Shutdown(SafeShutdown safe_shutdown, RequireExitCode0 require_exit_code_0) {
  if (!process_) {
    return;
  }

  // Before we kill the process, store the addresses. If we're told to start again we'll reuse
  // these.
  bound_rpc_ = bound_rpc_hostport();
  bound_http_ = bound_http_hostport();

  LOG_WITH_PREFIX(INFO) << "Starting Shutdown() of daemon with id " << id();

  const auto start_time = CoarseMonoClock::Now();
  auto process_name_and_pid = exe_;
  if (IsProcessAlive(require_exit_code_0)) {
    process_name_and_pid = ProcessNameAndPidStr();
    // In coverage builds, ask the process nicely to flush coverage info
    // before we kill -9 it. Otherwise, we never get any coverage from
    // external clusters.
    FlushCoverage();

    if (!FLAGS_external_daemon_heap_profile_prefix.empty()) {
      // The child process has been configured using the HEAPPROFILESIGNAL environment variable to
      // create a heap profile on receiving kHeapProfileSignal.
      static const int kWaitMs = 100;
      LOG_WITH_PREFIX(INFO) << "Sending signal " << kHeapProfileSignal << " to "
                            << process_name_and_pid << " to capture a heap profile. Waiting for "
                            << kWaitMs << " ms afterwards.";
      WARN_NOT_OK(process_->Kill(kHeapProfileSignal), "Killing process failed");
      std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    }

    if (safe_shutdown) {
      constexpr auto max_graceful_shutdown_wait = 1min * kTimeMultiplier;
      // We put 'SIGTERM' in quotes because an unquoted one would be treated as a test failure
      // by our regular expressions in common-test-env.sh.
      LOG_WITH_PREFIX(INFO) << "Terminating " << process_name_and_pid << " using 'SIGTERM' signal";
      WARN_NOT_OK(process_->Kill(SIGTERM), "Killing process failed");
      CoarseBackoffWaiter waiter(start_time + max_graceful_shutdown_wait, 100ms);
      while (IsProcessAlive(require_exit_code_0)) {
        YB_LOG_EVERY_N_SECS(INFO, 1)
            << LogPrefix() << "Waiting for process termination: " << process_name_and_pid;
        if (!waiter.Wait()) {
          break;
        }
      }

      if (IsProcessAlive(require_exit_code_0)) {
        LOG_WITH_PREFIX(INFO) << "The process " << process_name_and_pid
                              << " is still running after " << CoarseMonoClock::Now() - start_time
                              << " ms, will send SIGKILL";
      }
    }

    if (IsProcessAlive(require_exit_code_0)) {
      LOG_WITH_PREFIX(INFO) << "Killing " << process_name_and_pid << " with SIGKILL";
      sigkill_used_for_shutdown_ = true;
      WARN_NOT_OK(process_->Kill(SIGKILL), "Killing process failed");
    }
  }
  int ret = 0;
  WARN_NOT_OK(process_->Wait(&ret), Format("$0 Waiting on $1", LogPrefix(), process_name_and_pid));
  process_.reset();
  LOG_WITH_PREFIX(INFO) << "Process " << process_name_and_pid << " shutdown completed in "
                        << CoarseMonoClock::Now() - start_time << "ms";
}

void ExternalDaemon::FlushCoverage() {
#ifndef COVERAGE_BUILD_
  return;
#else
  LOG(INFO) << "Attempting to flush coverage for " << exe_ << " pid " << process_->pid();
  server::GenericServiceProxy proxy(messenger_, bound_rpc_addr());

  server::FlushCoverageRequestPB req;
  server::FlushCoverageResponsePB resp;
  RpcController rpc;

  // Set a reasonably short timeout, since some of our tests kill servers which
  // are kill -STOPed.
  rpc.set_timeout(MonoDelta::FromMilliseconds(100));
  Status s = proxy.FlushCoverage(req, &resp, &rpc);
  if (s.ok() && !resp.success()) {
    s = STATUS(RemoteError, "Server does not appear to be running a coverage build");
  }
  WARN_NOT_OK(s, Format("Unable to flush coverage on $0 pid $1", exe_, process_->pid()));
#endif
}

std::string ExternalDaemon::ProcessNameAndPidStr() {
  return Format("$0 with pid $1", exe_, process_->pid());
}

HostPort ExternalDaemon::bound_rpc_hostport() const {
  CHECK(status_);
  CHECK_GE(status_->bound_rpc_addresses_size(), 1);
  return HostPortFromPB(status_->bound_rpc_addresses(0));
}

HostPort ExternalDaemon::bound_rpc_addr() const { return bound_rpc_hostport(); }

HostPort ExternalDaemon::bound_http_hostport() const {
  CHECK(status_);
  CHECK_GE(status_->bound_http_addresses_size(), 1);
  return HostPortFromPB(status_->bound_http_addresses(0));
}

const NodeInstancePB& ExternalDaemon::instance_id() const {
  CHECK(status_);
  return status_->node_instance();
}

const string& ExternalDaemon::uuid() const {
  CHECK(status_);
  return status_->node_instance().permanent_uuid();
}

template <>
Result<int64_t> ExternalDaemon::ExtractMetricValue<int64_t>(
    const JsonReader& r, const Value* metric, const char* value_field) {
  int64_t value;
  RETURN_NOT_OK(r.ExtractInt64(metric, value_field, &value));
  return value;
}

template <>
Result<bool> ExternalDaemon::ExtractMetricValue<bool>(
    const JsonReader& r, const Value* metric, const char* value_field) {
  bool value;
  RETURN_NOT_OK(r.ExtractBool(metric, value_field, &value));
  return value;
}

template <>
Result<uint32_t> ExternalDaemon::ExtractMetricValue<uint32_t>(
    const JsonReader& r, const Value* metric, const char* value_field) {
  uint32_t value;
  RETURN_NOT_OK(r.ExtractUInt32(metric, value_field, &value));
  return value;
}

string ExternalDaemon::LogPrefix() {
  return Format("{ daemon_id: $0 bound_rpc: $1 } ", daemon_id_, bound_rpc_);
}

void ExternalDaemon::SetLogListener(StringListener* listener) {
  stdout_tailer_thread_->SetListener(listener);
  stderr_tailer_thread_->SetListener(listener);
}

void ExternalDaemon::RemoveLogListener(StringListener* listener) {
  stdout_tailer_thread_->RemoveListener(listener);
  stderr_tailer_thread_->RemoveListener(listener);
}

Result<string> ExternalDaemon::GetFlag(const std::string& flag) {
  server::GenericServiceProxy proxy(proxy_cache_, bound_rpc_addr());

  RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(30));
  server::GetFlagRequestPB req;
  server::GetFlagResponsePB resp;
  req.set_flag(flag);
  RETURN_NOT_OK(proxy.GetFlag(req, &resp, &controller));
  if (!resp.valid()) {
    return STATUS_FORMAT(RemoteError, "Failed to get gflag $0 value.", flag);
  }
  return resp.value();
}

Result<HybridTime> ExternalDaemon::GetServerTime() {
  server::GenericServiceProxy proxy(proxy_cache_, bound_rpc_addr());

  RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(30));
  server::ServerClockRequestPB req;
  server::ServerClockResponsePB resp;
  RETURN_NOT_OK(proxy.ServerClock(req, &resp, &controller));
  SCHECK(resp.has_hybrid_time(), IllegalState, "No hybrid time in response");
  HybridTime ht;
  RETURN_NOT_OK(ht.FromUint64(resp.hybrid_time()));

  return ht;
}

void ExternalDaemon::AddExtraFlag(const std::string& flag, const std::string& value) {
  extra_flags_.push_back(Format("--$0=$1", flag, value));
}

size_t ExternalDaemon::RemoveExtraFlag(const std::string& flag) {
  const std::string flag_with_prefix = "--" + flag;
  return std::erase_if(extra_flags_, [&flag_with_prefix](auto&& flag) {
    return HasPrefixString(flag, flag_with_prefix);
  });
}

}  // namespace yb
