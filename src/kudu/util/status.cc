// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "kudu/util/status.h"

#include <stdio.h>
#include <stdint.h>

#include "kudu/gutil/strings/fastmem.h"
#include "kudu/util/malloc.h"

namespace kudu {

const char* Status::CopyState(const char* state) {
  uint32_t size;
  strings::memcpy_inlined(&size, state, sizeof(size));
  auto result = new char[size + 7];
  strings::memcpy_inlined(result, state, size + 7);
  return result;
}

Status::Status(Code code, const Slice& msg, const Slice& msg2,
               int16_t posix_code) {
  assert(code != kOk);
  const uint32_t len1 = msg.size();
  const uint32_t len2 = msg2.size();
  const uint32_t size = len1 + (len2 ? (2 + len2) : 0);
  auto result = new char[size + 7];
  memcpy(result, &size, sizeof(size));
  result[4] = static_cast<char>(code);
  memcpy(result + 5, &posix_code, sizeof(posix_code));
  memcpy(result + 7, msg.data(), len1);
  if (len2) {
    result[7 + len1] = ':';
    result[8 + len1] = ' ';
    memcpy(result + 9 + len1, msg2.data(), len2);
  }
  state_ = result;
}

std::string Status::CodeAsString() const {
  if (state_ == nullptr) {
    return "OK";
  }

  const char* type;
  switch (code()) {
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
  }
  return std::string(type);
}

std::string Status::ToString() const {
  std::string result(CodeAsString());
  if (state_ == nullptr) {
    return result;
  }

  result.append(": ");
  Slice msg = message();
  result.append(reinterpret_cast<const char*>(msg.data()), msg.size());
  int16_t posix = posix_code();
  if (posix != -1) {
    char buf[64];
    snprintf(buf, sizeof(buf), " (error %d)", posix);
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
  return Slice(state_ + 7, length);
}

int16_t Status::posix_code() const {
  if (state_ == nullptr) {
    return 0;
  }
  int16_t posix_code;
  memcpy(&posix_code, state_ + 5, sizeof(posix_code));
  return posix_code;
}

Status Status::CloneAndPrepend(const Slice& msg) const {
  return Status(code(), msg, message(), posix_code());
}

Status Status::CloneAndAppend(const Slice& msg) const {
  return Status(code(), message(), msg, posix_code());
}

size_t Status::memory_footprint_excluding_this() const {
  return state_ ? kudu_malloc_usable_size(state_) : 0;
}

size_t Status::memory_footprint_including_this() const {
  return kudu_malloc_usable_size(this) + memory_footprint_excluding_this();
}
}  // namespace kudu
