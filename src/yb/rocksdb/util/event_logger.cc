//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include "yb/rocksdb/util/event_logger.h"

#include <inttypes.h>
#include <cassert>
#include <sstream>
#include <string>


namespace rocksdb {


EventLoggerStream::EventLoggerStream(Logger* logger)
    : logger_(logger), log_buffer_(nullptr), json_writer_(nullptr) {}

EventLoggerStream::EventLoggerStream(LogBuffer* log_buffer)
    : logger_(nullptr), log_buffer_(log_buffer), json_writer_(nullptr) {}

EventLoggerStream::~EventLoggerStream() {
  if (json_writer_) {
    json_writer_->EndObject();
#ifdef ROCKSDB_PRINT_EVENTS_TO_STDOUT
    printf("%s\n", json_writer_->Get().c_str());
#else
    if (logger_) {
      EventLogger::Log(logger_, *json_writer_);
    } else if (log_buffer_) {
      EventLogger::LogToBuffer(log_buffer_, *json_writer_);
    }
#endif
    delete json_writer_;
  }
}

void EventLogger::Log(const JSONWriter& jwriter) {
  Log(logger_, jwriter);
}

void EventLogger::Log(Logger* logger, const JSONWriter& jwriter) {
#ifdef ROCKSDB_PRINT_EVENTS_TO_STDOUT
  printf("%s\n", jwriter.Get().c_str());
#else
  rocksdb::RLOG(logger, "%s %s", Prefix(), jwriter.Get().c_str());
#endif
}

void EventLogger::LogToBuffer(
    LogBuffer* log_buffer, const JSONWriter& jwriter) {
#ifdef ROCKSDB_PRINT_EVENTS_TO_STDOUT
  printf("%s\n", jwriter.Get().c_str());
#else
  assert(log_buffer);
  rocksdb::LOG_TO_BUFFER(log_buffer, "%s %s", Prefix(), jwriter.Get().c_str());
#endif
}

}  // namespace rocksdb
