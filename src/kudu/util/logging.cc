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

#include "kudu/util/logging.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <sstream>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <glog/logging.h>

#include "kudu/gutil/callback.h"
#include "kudu/gutil/spinlock.h"
#include "kudu/util/flag_tags.h"

DEFINE_string(log_filename, "",
    "Prefix of log filename - "
    "full path is <log_dir>/<log_filename>.[INFO|WARN|ERROR|FATAL]");
TAG_FLAG(log_filename, stable);

#define PROJ_NAME "kudu"

bool logging_initialized = false;

using namespace std; // NOLINT(*)
using namespace boost::uuids; // NOLINT(*)

using base::SpinLock;
using base::SpinLockHolder;

namespace kudu {

namespace {

class SimpleSink : public google::LogSink {
 public:
  explicit SimpleSink(LoggingCallback cb) : cb_(std::move(cb)) {}

  virtual ~SimpleSink() OVERRIDE {
  }

  virtual void send(google::LogSeverity severity, const char* full_filename,
                    const char* base_filename, int line,
                    const struct ::tm* tm_time,
                    const char* message, size_t message_len) OVERRIDE {
    LogSeverity kudu_severity;
    switch (severity) {
      case google::INFO:
        kudu_severity = SEVERITY_INFO;
        break;
      case google::WARNING:
        kudu_severity = SEVERITY_WARNING;
        break;
      case google::ERROR:
        kudu_severity = SEVERITY_ERROR;
        break;
      case google::FATAL:
        kudu_severity = SEVERITY_FATAL;
        break;
      default:
        LOG(FATAL) << "Unknown glog severity: " << severity;
    }
    cb_.Run(kudu_severity, full_filename, line, tm_time, message, message_len);
  }

 private:

  LoggingCallback cb_;
};

SpinLock logging_mutex(base::LINKER_INITIALIZED);

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

} // anonymous namespace

void InitGoogleLoggingSafe(const char* arg) {
  SpinLockHolder l(&logging_mutex);
  if (logging_initialized) return;

  google::InstallFailureSignalHandler();

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
    ss << FLAGS_log_dir << "/" << PROJ_NAME "_test_log." << uuid_generator();
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

  google::InitGoogleLogging(arg);

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

  google::InitGoogleLogging(arg);

  // This also disables file-based logging.
  google::LogToStderr();

  // File logging: off.
  // Stderr logging threshold: INFO.
  // Sink logging: off.
  initial_stderr_severity = google::INFO;
  logging_initialized = true;
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

} // namespace kudu
