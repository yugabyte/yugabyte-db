// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// Portions Copyright (c) YugaByte, Inc.

#include "yb/util/status.h"

#include <stdio.h>
#include <stdint.h>

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

}

Status::StatePtr Status::CopyState() const {
  if (!state_)
    return nullptr;
  auto state = state_.get();
  size_t size = state->message_len + kHeaderSize;
  StatePtr result(static_cast<State*>(malloc(size)));
  memcpy(result.get(), state, size);
  return result;
}

Status::Status(Code code,
               const char* file_name,
               int line_number,
               const Slice& msg,
               const Slice& msg2,
               int64_t error_code) {
  assert(code != kOk);
  const size_t len1 = msg.size();
  const size_t len2 = msg2.size();
  const size_t size = len1 + (len2 ? (2 + len2) : 0);
  state_.reset(static_cast<State*>(malloc(size + kHeaderSize)));
  state_->message_len = static_cast<uint32_t>(size);
  state_->code = static_cast<uint8_t>(code);
  state_->error_code = error_code;
  state_->file_name = file_name;
  state_->line_number = line_number;
  memcpy(state_->message, msg.data(), len1);
  if (len2) {
    state_->message[len1] = ':';
    state_->message[len1 + 1] = ' ';
    memcpy(state_->message + 2 + len1, msg2.data(), len2);
  }
#ifndef NDEBUG
  static const bool print_stack_trace = getenv("YB_STACK_TRACE_ON_ERROR_STATUS") != nullptr;
  if (print_stack_trace) {
    // We skip a couple of top frames like these:
    //    ~/code/yugabyte/src/yb/util/status.cc:53:
    //        @ yb::Status::Status(yb::Status::Code, yb::Slice const&, yb::Slice const&, long,
    //                             char const*, int)
    //    ~/code/yugabyte/src/yb/util/status.h:137:
    //        @ yb::STATUS(Corruption, char const*, int, yb::Slice const&, yb::Slice const&, short)
    //    ~/code/yugabyte/src/yb/common/doc_hybrid_time.cc:94:
    //        @ yb::DocHybridTime::DecodeFrom(yb::Slice*)
    LOG(WARNING) << "Non-OK status generated: " << ToString() << ", stack trace:\n"
                 << GetStackTrace(StackTraceLineFormat::CLION_CLICKABLE, /* skip frames: */ 2);
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

#define YB_STATUS_RETURN_MESSAGE(name, value, message) \
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
  int64_t error = GetErrorCode();
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

int16_t Status::posix_code() const {
  int64_t error_code = !IsSqlError() ? GetErrorCode() : 0;
  CHECK_LE(error_code, INT16_MAX) << "invalid posix_code";
  return static_cast<int16_t>(error_code);
}

int64_t Status::sql_error_code() const {
  int64_t error_code = IsSqlError() ? GetErrorCode() : 0;
  return error_code;
}

Status Status::CloneAndPrepend(const Slice& msg) const {
  return Status(code(), state_->file_name, state_->line_number, msg, message(), posix_code());
}

Status Status::CloneAndAppend(const Slice& msg) const {
  return Status(code(), state_->file_name, state_->line_number, message(), msg, posix_code());
}

size_t Status::memory_footprint_excluding_this() const {
  return state_ ? malloc_usable_size(state_.get()) : 0;
}

size_t Status::memory_footprint_including_this() const {
  return malloc_usable_size(this) + memory_footprint_excluding_this();
}
}  // namespace yb
