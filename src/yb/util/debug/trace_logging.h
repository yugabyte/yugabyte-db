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
// This header defines the following macro:
//
// VLOG_AND_TRACE(category, vlevel)
//
//   Write a log message to VLOG(vlevel) as well as the current
//   trace event buffer as an "INSTANT" trace event type. If the
//   given vlog level is not enabled, this will still result in a
//   trace buffer entry.
//
//   The provided 'category' should be a trace event category, which
//   allows the users to filter which trace events to enable.
//   For example:
//
//    VLOG_AND_TRACE("my_subsystem", 1) << "This always shows up in trace buffers "
//        << "but only shows up in the log if VLOG(1) level logging is enabled.";
//
//   Most VLOG(1) level log messages are reasonable to use this macro.
//   Note that there is slightly more overhead to this macro as opposed
//   to just using VLOG(1).
//
//   Note that, like VLOG(n), this macro avoids evaluating its arguments unless
//   either trace recording or VLOG(n) is enabled. In the case that both are enabled,
//   the arguments are only evaluated once.
//
#pragma once

#include "yb/util/logging.h"
#include <string>

#include "yb/gutil/macros.h"
#include "yb/util/debug/trace_event.h"

// The inner workings of these macros are a bit arcane:
// - We make use of the fact that a block can be embedded within a ternary expression.
//   This allows us to determine whether the trace event is enabled before we decide
//   to evaluate the arguments.
// - We have to use google::LogMessageVoidify so that we can put 'void(0)' on one side
//   of the ternary expression and the log stream on the other. This technique is
//   cribbed from glog/logging.h.
#define VLOG_AND_TRACE_INTERNAL(category, vlevel) \
  yb::debug::TraceVLog(__FILE__, __LINE__, category, VLOG_IS_ON(vlevel)).stream()
#define VLOG_AND_TRACE(category, vlevel)                              \
  !( {                                                                \
      bool enabled;                                                   \
      TRACE_EVENT_CATEGORY_GROUP_ENABLED(category, &enabled);         \
      enabled || VLOG_IS_ON(vlevel);                                  \
    } ) ? static_cast<void>(0) :                                      \
          google::LogMessageVoidify() & VLOG_AND_TRACE_INTERNAL(category, vlevel)

namespace yb {
namespace debug {

class TraceVLog {
 public:
  TraceVLog(const char* file, int line, const char* category, bool do_vlog)
    : sink_(category),
      google_msg_(file, line, google::GLOG_INFO, &sink_, do_vlog) {
  }

  std::ostream& stream() {
    return google_msg_.stream();
  }

 private:
  class TraceLogSink : public google::LogSink {
   public:
    explicit TraceLogSink(const char* category) : category_(category) {}
    void send(google::LogSeverity severity, const char* full_filename,
              const char* base_filename, int line,
              const struct ::tm* tm_time, const char* message,
              size_t message_len) override {
      // Rather than calling TRACE_EVENT_INSTANT here, we have to do it from
      // the destructor. This is because glog holds its internal mutex while
      // calling send(). So, if we try to use TRACE_EVENT here, and --trace_to_console
      // is enabled, then we'd end up calling back into glog when its lock is already
      // held. glog isn't re-entrant, so that causes a crash.
      //
      // By just storing the string here, and then emitting the trace in the dtor,
      // we defer the tracing until the google::LogMessage has destructed and the
      // glog lock is available again.
      str_ = ToString(severity, base_filename, line,
                      tm_time, message, message_len);
    }
    virtual ~TraceLogSink() {
      TRACE_EVENT_INSTANT1(category_, "vlog", TRACE_EVENT_SCOPE_THREAD,
                           "msg", str_);
    }

   private:
    const char* const category_;
    std::string str_;
  };

  TraceLogSink sink_;
  google::LogMessage google_msg_;
};

} // namespace debug
} // namespace yb
