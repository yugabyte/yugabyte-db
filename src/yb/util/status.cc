// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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

// Portions Copyright (c) YugaByte, Inc.

#include "yb/util/status.h"

#include <stdio.h>
#include <stdint.h>

#include <regex>

#include <boost/optional.hpp>

#include "yb/gutil/strings/fastmem.h"
#include "yb/util/malloc.h"
#include "yb/util/debug-util.h"

namespace yb {

namespace {

const char* kTimeoutErrorMsgs[] = {
    "",                                                  // kNone
    "Timeout Acquiring Mutex",                           // kMutexTimeout
    "Timeout waiting to lock key",                       // kLockTimeout
    "Failed to acquire lock due to max_num_locks limit"  // kLockLimit
};

#ifndef NDEBUG
// This allows to dump stack traces whenever an error status matching a certain regex is generated.
boost::optional<std::regex> StatusStackTraceRegEx() {
  const char* regex_str = getenv("YB_STACK_TRACE_ON_ERROR_STATUS_RE");
  if (!regex_str) {
    return boost::none;
  }
  return std::regex(regex_str);
}
#endif

} // anonymous namespace

Status::Status(Code code,
               const char* file_name,
               int line_number,
               const Slice& msg,
               const Slice& msg2,
               int64_t error_code,
               DupFileName dup_file_name) {
  assert(code != kOk);
  const size_t len1 = msg.size();
  const size_t len2 = msg2.size();
  const size_t size = len1 + (len2 ? (2 + len2) : 0);
  size_t file_name_size = 0;
  if (dup_file_name) {
    file_name_size = strlen(file_name) + 1;
  }
  state_.reset(static_cast<State*>(malloc(size + kHeaderSize + file_name_size)));
  state_->message_len = static_cast<uint32_t>(size);
  state_->code = static_cast<uint8_t>(code);
  state_->error_code = error_code;
  // We aleady assigned intrusive_ptr, so counter should be one.
  state_->counter.store(1, std::memory_order_relaxed);
  state_->line_number = line_number;
  memcpy(state_->message, msg.data(), len1);
  if (len2) {
    state_->message[len1] = ':';
    state_->message[len1 + 1] = ' ';
    memcpy(state_->message + 2 + len1, msg2.data(), len2);
  }
  if (dup_file_name) {
    auto new_file_name = &state_->message[0] + size;
    memcpy(new_file_name, file_name, file_name_size);
    file_name = new_file_name;
  }

  state_->file_name = file_name;

  #ifndef NDEBUG
  static const bool print_stack_trace = getenv("YB_STACK_TRACE_ON_ERROR_STATUS") != nullptr;
  static const boost::optional<std::regex> status_stack_trace_re =
      StatusStackTraceRegEx();

  std::string string_rep;  // To avoid calling ToString() twice.
  if (print_stack_trace ||
      (status_stack_trace_re &&
       std::regex_search(string_rep = ToString(), *status_stack_trace_re))) {
    if (string_rep.empty()) {
      string_rep = ToString();
    }
    // We skip a couple of top frames like these:
    //    ~/code/yugabyte/src/yb/util/status.cc:53:
    //        @ yb::Status::Status(yb::Status::Code, yb::Slice const&, yb::Slice const&, long,
    //                             char const*, int)
    //    ~/code/yugabyte/src/yb/util/status.h:137:
    //        @ yb::STATUS(Corruption, char const*, int, yb::Slice const&, yb::Slice const&, short)
    //    ~/code/yugabyte/src/yb/common/doc_hybrid_time.cc:94:
    //        @ yb::DocHybridTime::DecodeFrom(yb::Slice*)
    LOG(WARNING) << "Non-OK status generated: " << string_rep << ", stack trace:\n"
                 << GetStackTrace(StackTraceLineFormat::DEFAULT, /* skip frames: */ 1);
  }
#endif
}

Status::Status(Code code, const char* file_name, int line_number, TimeoutError error)
    : Status(code,
             file_name,
             line_number,
             kTimeoutErrorMsgs[static_cast<int>(error)],
             "",
             static_cast<int>(error)) {}

namespace {

#define YB_STATUS_RETURN_MESSAGE(name, pb_name, value, message) \
    case Status::BOOST_PP_CAT(k, name): \
      return message;

const char* CodeAsCString(Status::Code code) {
  switch (code) {
    BOOST_PP_SEQ_FOR_EACH(YB_STATUS_FORWARD_MACRO, YB_STATUS_RETURN_MESSAGE, YB_STATUS_CODES)
  }
  return nullptr;
}

} // namespace

std::string Status::CodeAsString() const {
  auto code = this->code();
  auto* cstr = CodeAsCString(code);
  return cstr != nullptr ? cstr : "Incorrect status code " + std::to_string(code);
}

std::string Status::ToUserMessage(bool include_file_and_line) const {
  if (error_code() == 0) {
    // Return empty string for success.
    return "";
  }
  return ToString(include_file_and_line);
}

std::string Status::ToString(bool include_file_and_line) const {
  std::string result(CodeAsString());
  if (state_ == nullptr) {
    return result;
  }

  if (include_file_and_line && state_->file_name != nullptr && state_->line_number) {
    result.append(" (");

    // Try to only include file path starting from source root directory. We are assuming that all
    // C++ code is located in $YB_SRC_ROOT/src, where $YB_SRC_ROOT is the repository root. Note that
    // this will break if the repository itself is located in a parent directory named "src".
    // However, neither Jenkins, nor our standard code location on a developer workstation
    // (~/code/yugabyte) should have that problem.
    const char* src_subpath = strstr(state_->file_name, "/src/");
    result.append(src_subpath != nullptr ? src_subpath + 5 : state_->file_name);

    result.append(":");
    result.append(std::to_string(state_->line_number));
    result.append(")");
  }
  result.append(": ");
  Slice msg = message();
  result.append(reinterpret_cast<const char*>(msg.data()), msg.size());
  int64_t error = error_code();
  if (error != -1) {
    char buf[64];
    snprintf(buf, sizeof(buf), " (error %" PRId64 ")", error);
    result.append(buf);
  }
  return result;
}

Slice Status::message() const {
  if (state_ == nullptr) {
    return Slice();
  }

  return Slice(state_->message, state_->message_len);
}

Status Status::CloneAndPrepend(const Slice& msg) const {
  return Status(code(), state_->file_name, state_->line_number, msg, message(), error_code(),
                DupFileName(file_name_duplicated()));
}

Status Status::CloneAndChangeErrorCode(int64_t error_code) const {
  return Status(code(), state_->file_name, state_->line_number, message(), Slice(), error_code,
                DupFileName(file_name_duplicated()));
}

Status Status::CloneAndAppend(const Slice& msg) const {
  return Status(code(), state_->file_name, state_->line_number, message(), msg, error_code(),
                DupFileName(file_name_duplicated()));
}

size_t Status::memory_footprint_excluding_this() const {
  return state_ ? malloc_usable_size(state_.get()) : 0;
}

size_t Status::memory_footprint_including_this() const {
  return malloc_usable_size(this) + memory_footprint_excluding_this();
}

bool Status::file_name_duplicated() const {
  return state_->file_name == &state_->message[0] + state_->message_len;
}

}  // namespace yb
