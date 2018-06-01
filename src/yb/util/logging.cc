// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "yb/util/logging.h"

#include <signal.h>
#include <stdio.h>

#include <sstream>
#include <iostream>
#include <fstream>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include <glog/logging.h>

#include "yb/gutil/callback.h"
#include "yb/gutil/spinlock.h"

#include "yb/util/debug-util.h"
#include "yb/util/flag_tags.h"

DEFINE_string(log_filename, "",
    "Prefix of log filename - "
    "full path is <log_dir>/<log_filename>.[INFO|WARN|ERROR|FATAL]");
TAG_FLAG(log_filename, stable);

DEFINE_string(fatal_details_path_prefix, "",
              "A prefix to use for the path of a file to save fatal failure stack trace and "
              "other details to.");
DEFINE_string(minicluster_daemon_id, "",
              "A human-readable 'daemon id', e.g. 'm-1' or 'ts-2', used in tests.");

const char* kProjName = "yb";

bool logging_initialized = false;

using namespace std; // NOLINT(*)
using namespace boost::uuids; // NOLINT(*)

using base::SpinLockHolder;

namespace yb {

// Sink which implements special handling for LOG(FATAL) and CHECK failures, such as disabling
// core dumps and printing the failure stack trace into a separate file.
unique_ptr<LogFatalHandlerSink> log_fatal_handler_sink;

namespace {

class SimpleSink : public google::LogSink {
 public:
  explicit SimpleSink(LoggingCallback cb) : cb_(std::move(cb)) {}

  ~SimpleSink() override {
  }

  virtual void send(google::LogSeverity severity, const char* full_filename,
                    const char* base_filename, int line,
                    const struct ::tm* tm_time,
                    const char* message, size_t message_len) override {
    LogSeverity yb_severity;
    switch (severity) {
      case google::INFO:
        yb_severity = SEVERITY_INFO;
        break;
      case google::WARNING:
        yb_severity = SEVERITY_WARNING;
        break;
      case google::ERROR:
        yb_severity = SEVERITY_ERROR;
        break;
      case google::FATAL:
        yb_severity = SEVERITY_FATAL;
        break;
      default:
        LOG(FATAL) << "Unknown glog severity: " << severity;
    }
    cb_.Run(yb_severity, full_filename, line, tm_time, message, message_len);
  }

 private:

  LoggingCallback cb_;
};

base::SpinLock logging_mutex(base::LINKER_INITIALIZED);

// There can only be a single instance of a SimpleSink.
//
// Protected by 'logging_mutex'.
SimpleSink* registered_sink = nullptr;

// Records the logging severity after the first call to
// InitGoogleLoggingSafe{Basic}. Calls to UnregisterLoggingCallback()
// will restore stderr logging back to this severity level.
//
// Protected by 'logging_mutex'.
int initial_stderr_severity;

void UnregisterLoggingCallbackUnlocked() {
  CHECK(logging_mutex.IsHeld());
  CHECK(registered_sink);

  // Restore logging to stderr, then remove our sink. This ordering ensures
  // that no log messages are missed.
  google::SetStderrLogging(initial_stderr_severity);
  google::RemoveLogSink(registered_sink);
  delete registered_sink;
  registered_sink = nullptr;
}

void DumpStackTraceAndExit() {
  const auto stack_trace = GetStackTrace();
  if (write(STDERR_FILENO, stack_trace.c_str(), stack_trace.length()) < 0) {
    // Ignore errors.
  }

  // Set the default signal handler for SIGABRT, to avoid invoking our
  // own signal handler installed by InstallFailedSignalHandler().
  struct sigaction sig_action;
  memset(&sig_action, 0, sizeof(sig_action));
  sigemptyset(&sig_action.sa_mask);
  sig_action.sa_handler = SIG_DFL;
  sigaction(SIGABRT, &sig_action, NULL);

  abort();
}

} // anonymous namespace

void InitializeGoogleLogging(const char *arg) {
  google::InitGoogleLogging(arg);

  google::InstallFailureFunction(DumpStackTraceAndExit);

  log_fatal_handler_sink = std::make_unique<LogFatalHandlerSink>();
}

void InitGoogleLoggingSafe(const char* arg) {
  SpinLockHolder l(&logging_mutex);
  if (logging_initialized) return;

  google::InstallFailureSignalHandler();

  // Set the logbuflevel to -1 so that all logs are printed out in unbuffered.
  FLAGS_logbuflevel = -1;

  if (!FLAGS_log_filename.empty()) {
    for (int severity = google::INFO; severity <= google::FATAL; ++severity) {
      google::SetLogSymlink(severity, FLAGS_log_filename.c_str());
    }
  }

  // This forces our logging to use /tmp rather than looking for a
  // temporary directory if none is specified. This is done so that we
  // can reliably construct the log file name without duplicating the
  // complex logic that glog uses to guess at a temporary dir.
  if (FLAGS_log_dir.empty()) {
    FLAGS_log_dir = "/tmp";
  }

  if (!FLAGS_logtostderr) {
    // Verify that a log file can be created in log_dir by creating a tmp file.
    stringstream ss;
    random_generator uuid_generator;
    ss << FLAGS_log_dir << "/" << kProjName << "_test_log." << uuid_generator();
    const string file_name = ss.str();
    ofstream test_file(file_name.c_str());
    if (!test_file.is_open()) {
      stringstream error_msg;
      error_msg << "Could not open file in log_dir " << FLAGS_log_dir;
      perror(error_msg.str().c_str());
      // Unlock the mutex before exiting the program to avoid mutex d'tor assert.
      logging_mutex.Unlock();
      exit(1);
    }
    remove(file_name.c_str());
  }

  InitializeGoogleLogging(arg);

  // Needs to be done after InitGoogleLogging
  if (FLAGS_log_filename.empty()) {
    CHECK_STRNE(google::ProgramInvocationShortName(), "UNKNOWN")
        << ": must initialize gflags before glog";
    FLAGS_log_filename = google::ProgramInvocationShortName();
  }

  // File logging: on.
  // Stderr logging threshold: FLAGS_stderrthreshold.
  // Sink logging: off.
  initial_stderr_severity = FLAGS_stderrthreshold;
  logging_initialized = true;
}

void InitGoogleLoggingSafeBasic(const char* arg) {
  SpinLockHolder l(&logging_mutex);
  if (logging_initialized) return;

  InitializeGoogleLogging(arg);

  // This also disables file-based logging.
  google::LogToStderr();

  // File logging: off.
  // Stderr logging threshold: INFO.
  // Sink logging: off.
  initial_stderr_severity = google::INFO;
  logging_initialized = true;
}

bool IsLoggingInitialized() {
  SpinLockHolder l(&logging_mutex);
  return logging_initialized;
}

void RegisterLoggingCallback(const LoggingCallback& cb) {
  SpinLockHolder l(&logging_mutex);
  CHECK(logging_initialized);

  if (registered_sink) {
    LOG(WARNING) << "Cannot register logging callback: one already registered";
    return;
  }

  // AddLogSink() claims to take ownership of the sink, but it doesn't
  // really; it actually expects it to remain valid until
  // google::ShutdownGoogleLogging() is called.
  registered_sink = new SimpleSink(cb);
  google::AddLogSink(registered_sink);

  // Even when stderr logging is ostensibly off, it's still emitting
  // ERROR-level stuff. This is the default.
  google::SetStderrLogging(google::ERROR);

  // File logging: yes, if InitGoogleLoggingSafe() was called earlier.
  // Stderr logging threshold: ERROR.
  // Sink logging: on.
}

void UnregisterLoggingCallback() {
  SpinLockHolder l(&logging_mutex);
  CHECK(logging_initialized);

  if (!registered_sink) {
    LOG(WARNING) << "Cannot unregister logging callback: none registered";
    return;
  }

  UnregisterLoggingCallbackUnlocked();
  // File logging: yes, if InitGoogleLoggingSafe() was called earlier.
  // Stderr logging threshold: initial_stderr_severity.
  // Sink logging: off.
}

void GetFullLogFilename(google::LogSeverity severity, string* filename) {
  stringstream ss;
  ss << FLAGS_log_dir << "/" << FLAGS_log_filename << "."
     << google::GetLogSeverityName(severity);
  *filename = ss.str();
}

void ShutdownLoggingSafe() {
  SpinLockHolder l(&logging_mutex);
  if (!logging_initialized) return;

  if (registered_sink) {
    UnregisterLoggingCallbackUnlocked();
  }

  google::ShutdownGoogleLogging();

  logging_initialized = false;
}

void LogCommandLineFlags() {
  LOG(INFO) << "Flags (see also /varz are on debug webserver):" << endl
            << google::CommandlineFlagsIntoString();
}

// Support for the special THROTTLE_MSG token in a log message stream.
ostream& operator<<(ostream &os, const PRIVATE_ThrottleMsg&) {
  using google::LogMessage;
#ifdef DISABLE_RTTI
  LogMessage::LogStream *log = static_cast<LogMessage::LogStream*>(&os);
#else
  LogMessage::LogStream *log = dynamic_cast<LogMessage::LogStream*>(&os);
#endif
  CHECK(log && log == log->self())
      << "You must not use COUNTER with non-glog ostream";
  int ctr = log->ctr();
  if (ctr > 0) {
    os << " [suppressed " << ctr << " similar messages]";
  }
  return os;
}

void DisableCoreDumps() {
  struct rlimit lim;
  PCHECK(getrlimit(RLIMIT_CORE, &lim) == 0);
  lim.rlim_cur = 0;
  PCHECK(setrlimit(RLIMIT_CORE, &lim) == 0);

  // Set coredump_filter to not dump any parts of the address space.  Although the above disables
  // core dumps to files, if core_pattern is set to a pipe rather than a file, it's not sufficient.
  // Setting this pattern results in piping a very minimal dump into the core processor (eg abrtd),
  // thus speeding up the crash.
  int f = open("/proc/self/coredump_filter", O_WRONLY);
  if (f >= 0) {
    auto ret = write(f, "00000000", 8);
    if (ret != 8) {
      LOG(WARNING) << "Error writing to /proc/self/coredump_filter: " << strerror(errno);
    }
    close(f);
  }
}

string GetFatalDetailsPathPrefix() {
  if (!FLAGS_fatal_details_path_prefix.empty())
    return FLAGS_fatal_details_path_prefix;

  const char* fatal_details_path_prefix_env_var = getenv("YB_FATAL_DETAILS_PATH_PREFIX");
  if (fatal_details_path_prefix_env_var) {
    return fatal_details_path_prefix_env_var;
  }

  string fatal_log_path;
  GetFullLogFilename(LogSeverity::SEVERITY_FATAL, &fatal_log_path);
  if (!FLAGS_minicluster_daemon_id.empty()) {
    fatal_log_path += "." + FLAGS_minicluster_daemon_id;
  }
  return fatal_log_path + ".details";
}

// ------------------------------------------------------------------------------------------------
// LogFatalHandlerSink
// ------------------------------------------------------------------------------------------------

LogFatalHandlerSink::LogFatalHandlerSink() {
  AddLogSink(this);
}

LogFatalHandlerSink::~LogFatalHandlerSink() {
  RemoveLogSink(this);
}

void LogFatalHandlerSink::send(
    google::LogSeverity severity, const char* full_filename, const char* base_filename,
    int line_number, const struct tm* tm_time, const char* message, size_t message_len) {
  if (severity == LogSeverity::SEVERITY_FATAL) {
    DisableCoreDumps();
    string timestamp_for_filename;
    StringAppendStrftime(&timestamp_for_filename, "%Y-%m-%dT%H_%M_%S", tm_time);
    const string output_path = Format(
        "$0.$1.pid$2.txt", GetFatalDetailsPathPrefix(), timestamp_for_filename, getpid());
    // Use a line format similar to glog with a couple of slight differences:
    // - Report full file path.
    // - Time has no microsecond component.
    string output_str = "F";
    StringAppendStrftime(&output_str, "%Y%m%d %H:%M:%S", tm_time);
    // TODO: append thread id if we need to.
    StringAppendF(&output_str, " %s:%d] ", full_filename, line_number);
    output_str += std::string(message, message_len);
    output_str += "\n";
    output_str += GetStackTrace();

    ofstream out_f(output_path);
    if (out_f) {
      out_f << output_str << endl;
    }
    if (out_f.bad()) {
      cerr << "Failed to write fatal failure details to " << output_path << endl;
    } else {
      cerr << "Fatal failure details written to " << output_path << endl;
    }
    // Also output fatal failure details to stderr so make sure we have a properly symbolized stack
    // trace in the context of a test.
    cerr << output_str << endl;
  }
}

} // namespace yb
