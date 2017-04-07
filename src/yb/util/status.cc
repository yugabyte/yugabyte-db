// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/util/status.h"

#include <stdio.h>
#include <stdint.h>

#include "yb/gutil/strings/fastmem.h"
#include "yb/util/malloc.h"
#include "yb/util/debug-util.h"

namespace yb {

const char* Status::CopyState(const char* state) {
  uint32_t size;
  strings::memcpy_inlined(&size, state, sizeof(size));
  auto result = new char[size + 13];
  strings::memcpy_inlined(result, state, size + 13);
  return result;
}

Status::Status(Code code,
               const Slice& msg,
               const Slice& msg2,
               int64_t error_code,
               const char* file_name,
               int line_number)
    : file_name_(file_name),
      line_number_(line_number) {
  assert(code != kOk);
  const uint32_t len1 = msg.size();
  const uint32_t len2 = msg2.size();
  const uint32_t size = len1 + (len2 ? (2 + len2) : 0);
  auto result = new char[size + kMsgPos];
  memcpy(result, &size, sizeof(size));
  result[kCodePos] = static_cast<char>(code);
  memcpy(result + kErrorCodePos, &error_code, sizeof(error_code));
  memcpy(result + kMsgPos, msg.data(), len1);
  if (len2) {
    result[kMsgPos + len1] = ':';
    result[kMsgPos + len1 + 1] = ' ';
    memcpy(result + kMsgPos + 2 + len1, msg2.data(), len2);
  }
  state_ = result;
}

std::string Status::CodeAsString() const {
  if (state_ == nullptr) {
    return "OK";
  }

  const char* type;
  const Code status_code = code();
  switch (status_code) {
    case kOk:
      type = "OK";
      break;
    case kNotFound:
      type = "Not found";
      break;
    case kCorruption:
      type = "Corruption";
      break;
    case kNotSupported:
      type = "Not implemented";
      break;
    case kInvalidArgument:
      type = "Invalid argument";
      break;
    case kIOError:
      type = "IO error";
      break;
    case kAlreadyPresent:
      type = "Already present";
      break;
    case kRuntimeError:
      type = "Runtime error";
      break;
    case kNetworkError:
      type = "Network error";
      break;
    case kIllegalState:
      type = "Illegal state";
      break;
    case kNotAuthorized:
      type = "Not authorized";
      break;
    case kAborted:
      type = "Aborted";
      break;
    case kRemoteError:
      type = "Remote error";
      break;
    case kServiceUnavailable:
      type = "Service unavailable";
      break;
    case kTimedOut:
      type = "Timed out";
      break;
    case kUninitialized:
      type = "Uninitialized";
      break;
    case kConfigurationError:
      type = "Configuration error";
      break;
    case kIncomplete:
      type = "Incomplete";
      break;
    case kEndOfFile:
      type = "End of file";
      break;
    case kInvalidCommand:
      type = "Invalid command";
      break;
    case kSqlError:
      type = "SQL error";
      break;
    default:
      return "Incorrect status code " + std::to_string(status_code);
  }
  return std::string(type);
}

std::string Status::ToString(bool include_file_and_line) const {
  std::string result(CodeAsString());
  if (state_ == nullptr) {
    return result;
  }

  if (include_file_and_line && file_name_ != nullptr && line_number_ != 0) {
    result.append(" (");

    // Try to only include file path starting from source root directory. We are assuming that all
    // C++ code is located in $YB_SRC_ROOT/src, where $YB_SRC_ROOT is the repository root. Note that
    // this will break if the repository itself is located in a parent directory named "src".
    // However, neither Jenkins, nor our standard code location on a developer workstation
    // (~/code/yugabyte) should have that problem.
    const char* src_subpath = strstr(file_name_, "/src/");
    result.append(src_subpath != nullptr ? src_subpath + 5 : file_name_);

    result.append(":");
    result.append(std::to_string(line_number_));
    result.append(")");
  }
  result.append(": ");
  Slice msg = message();
  result.append(reinterpret_cast<const char*>(msg.data()), msg.size());
  int64_t error = GetErrorCode();
  if (error != -1) {
    char buf[64];
    snprintf(buf, sizeof(buf), " (error %lld)", error);
    result.append(buf);
  }
  return result;
}

Slice Status::message() const {
  if (state_ == nullptr) {
    return Slice();
  }

  uint32_t length;
  memcpy(&length, state_, sizeof(length));
  return Slice(state_ + kMsgPos, length);
}

int64_t Status::GetErrorCode() const {
  if (state_ == nullptr) {
    return 0;
  }
  int64_t error_code;
  memcpy(&error_code, state_ + kErrorCodePos, sizeof(error_code));
  return error_code;
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
  return Status(code(), msg, message(), posix_code(), file_name_, line_number_);
}

Status Status::CloneAndAppend(const Slice& msg) const {
  return Status(code(), message(), msg, posix_code(), file_name_, line_number_);
}

size_t Status::memory_footprint_excluding_this() const {
  return state_ ? yb_malloc_usable_size(state_) : 0;
}

size_t Status::memory_footprint_including_this() const {
  return yb_malloc_usable_size(this) + memory_footprint_excluding_this();
}
}  // namespace yb
