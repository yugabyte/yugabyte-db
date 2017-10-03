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
#ifndef KUDU_UTIL_LOGGING_CALLBACK_H
#define KUDU_UTIL_LOGGING_CALLBACK_H

#include <ctime>
#include <string>

#include "kudu/gutil/callback_forward.h"

namespace kudu {

enum LogSeverity {
  SEVERITY_INFO,
  SEVERITY_WARNING,
  SEVERITY_ERROR,
  SEVERITY_FATAL
};

// Callback for simple logging.
//
// 'message' is NOT terminated with an endline.
typedef Callback<void(LogSeverity severity,
                      const char* filename,
                      int line_number,
                      const struct ::tm* time,
                      const char* message,
                      size_t message_len)> LoggingCallback;

} // namespace kudu

#endif
