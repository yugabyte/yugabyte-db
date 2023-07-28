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
// A Status encapsulates the result of an operation.  It may indicate success,
// or it may indicate an error with an associated error message.
//
// Multiple threads can invoke const methods on a Status without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Status must use
// external synchronization.

#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include <boost/intrusive_ptr.hpp>

#include "yb/gutil/thread_annotations.h"

#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_bool.h"

// Return the given status if it is not OK.
#define YB_RETURN_NOT_OK(s) do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return MoveStatus(std::move(_s)); \
  } while (false)

// Return the given status if it is not OK, but first clone it and prepend the given message.
#define YB_RETURN_NOT_OK_PREPEND(s, msg) do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return MoveStatus(_s).CloneAndPrepend(msg); \
  } while (0);

// Return 'to_return' if 'to_call' returns a bad status.  The substitution for 'to_return' may
// reference the variable 's' for the bad status.
#define YB_RETURN_NOT_OK_RET(to_call, to_return) do { \
    ::yb::Status s = (to_call); \
    if (PREDICT_FALSE(!s.ok())) return (to_return);  \
  } while (0);

// Return the given status by adding the error code if it is not OK.
#define YB_RETURN_NOT_OK_SET_CODE(s, code) do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return MoveStatus(_s).CloneAndAddErrorCode(code); \
  } while (false)

#define RETURN_NOT_OK           YB_RETURN_NOT_OK
#define RETURN_NOT_OK_PREPEND   YB_RETURN_NOT_OK_PREPEND
#define RETURN_NOT_OK_RET       YB_RETURN_NOT_OK_RET
#define RETURN_NOT_OK_SET_CODE  YB_RETURN_NOT_OK_SET_CODE

extern "C" {

struct YBCStatusStruct;

}

namespace yb {

class Slice;

YB_STRONGLY_TYPED_BOOL(AddRef);

class StatusErrorCode;
struct StatusCategoryDescription;

class NODISCARD_CLASS Status;

// This class is unsafe version of Status that could be used in low level performance critical
// libraries to transfer status to the caller.
// Each single call to Status::UnsafeRelease should end up with single Status instantiation from
// UnsafeStatus.
class UnsafeStatus {
 private:
  friend class Status;
  void* state_ = nullptr;
};

class Status {
 public:
  // Wrapper class for OK status to forbid creation of Result from Status::OK in compile time
  class OK {
   public:
    operator Status() const {
      return Status();
    }
  };
  // Create a success status.
  Status() {}

  explicit Status(UnsafeStatus source)
      : state_(static_cast<State*>(source.state_), false) {}

  UnsafeStatus UnsafeRelease() {
    UnsafeStatus result;
    result.state_ = state_.detach();
    return result;
  }

  // Returns true if the status indicates success.
  MUST_USE_RESULT bool ok() const { return state_ == nullptr; }

  // Declares set of Is* functions

  #define YB_STATUS_CODE(name, pb_name, value, message) \
      bool BOOST_PP_CAT(Is, name)() const { \
        return code() == BOOST_PP_CAT(k, name); \
      }
  #include "yb/util/status_codes.h"
  #undef YB_STATUS_CODE

  // Returns a text message of this status to be reported to users.
  // Returns empty string for success.
  std::string ToUserMessage(bool include_code = false) const {
    return ToString(false /* include_file_and_line */, include_code);
  }

  // Return a string representation of this status suitable for printing.
  // Returns the string "OK" for success.
  std::string ToString(bool include_file_and_line = true, bool include_code = true) const;

  // Return a string representation of the status code, without the message
  // text or posix code information.
  std::string CodeAsString() const;

  // Returned string has unlimited lifetime, and should NOT be released by the caller.
  const char* CodeAsCString() const;

  // Return the message portion of the Status. This is similar to ToString,
  // except that it does not include the stringified error code or posix code.
  //
  // For OK statuses, this returns an empty string.
  //
  // The returned Slice is only valid as long as this Status object remains
  // live and unchanged.
  Slice message() const;

  const uint8_t* ErrorData(uint8_t category) const;
  Slice ErrorCodesSlice() const;

  const char* file_name() const;
  int line_number() const;

  // Return a new Status object with the same state plus an additional leading message.
  Status CloneAndPrepend(const Slice& msg) const;

  // Same as CloneAndPrepend, but appends to the message instead.
  Status CloneAndAppend(const Slice& msg) const;

  // Same as CloneAndPrepend, but adds new error code to status.
  // If error code of the same category already present, it will be replaced with new one.
  Status CloneAndAddErrorCode(const StatusErrorCode& error_code) const;

  // Returns the memory usage of this object without the object itself. Should
  // be used when embedded inside another object.
  size_t memory_footprint_excluding_this() const;

  // Returns the memory usage of this object including the object itself.
  // Should be used when allocated on the heap.
  size_t memory_footprint_including_this() const;

  enum Code : int32_t {
  #define YB_STATUS_CODE(name, pb_name, value, message) \
      BOOST_PP_CAT(k, name) = value,
  #include "yb/util/status_codes.h" // NOLINT
  #undef YB_STATUS_CODE
  };

  // Return a new Status object with the same state except status code.
  Status CloneAndReplaceCode(Code code) const;

  Status(Code code,
         const char* file_name,
         int line_number,
         const Slice& msg,
         // Error message details. If present - would be combined as "msg: msg2".
         const Slice& msg2 = Slice(),
         const StatusErrorCode* error = nullptr,
         size_t file_name_len = 0);

  Status(Code code,
         const char* file_name,
         int line_number,
         const Slice& msg,
         // Error message details. If present - would be combined as "msg: msg2".
         const Slice& msg2,
         const StatusErrorCode& error,
         size_t file_name_len = 0)
      : Status(code, file_name, line_number, msg, msg2, &error, file_name_len) {
  }

  Status(Code code,
         const char* file_name,
         int line_number,
         const StatusErrorCode& error,
         size_t file_name_len = 0);

  Status(Code code,
         const char* file_name,
         int line_number,
         const Slice& msg,
         const StatusErrorCode& error,
         size_t file_name_len = 0);

  Status(Code code,
         const char* file_name,
         int line_number,
         const Slice& msg,
         const Slice& errors,
         size_t file_name_len);

  Code code() const;

  static void RegisterCategory(const StatusCategoryDescription& description);

  static const std::string& CategoryName(uint8_t category);

  // Adopt status that was previously exported to C interface.
  explicit Status(YBCStatusStruct* state, AddRef add_ref);

  // Increments state ref count and returns pointer that could be used in C interface.
  YBCStatusStruct* RetainStruct() const;

  // Reset state w/o touching ref count. Return detached pointer that could be used in C interface.
  YBCStatusStruct* DetachStruct();

 private:
  struct State;

  bool file_name_duplicated() const;

  size_t file_name_len_for_copy() const;

  typedef boost::intrusive_ptr<State> StatePtr;

  explicit Status(StatePtr state);

  friend void intrusive_ptr_release(State* state);
  friend void intrusive_ptr_add_ref(State* state);

  StatePtr state_;

  static_assert(sizeof(Code) == 4, "Code enum size is part of ABI");
};

inline Status&& MoveStatus(Status&& status) {
  return std::move(status);
}

inline const Status& MoveStatus(const Status& status) {
  return status;
}

inline std::string StatusToString(const Status& status) {
  return status.ToString();
}

inline std::ostream& operator<<(std::ostream& out, const Status& status) {
  return out << status.ToString();
}

class StatusHolder {
 public:
  Status GetStatus() const;
  void SetError(const Status& status);
  void Reset();

 private:
  std::atomic<bool> is_ok_{true};
  mutable std::mutex mutex_;
  Status status_ GUARDED_BY(mutex_);
};

}  // namespace yb

#define STATUS(status_type, ...) \
    (Status(Status::BOOST_PP_CAT(k, status_type), __FILE__, __LINE__, __VA_ARGS__))

#define SCHECK_NOTNULL(expr) do { \
      if ((expr) == nullptr) { \
        return STATUS(IllegalState, BOOST_PP_STRINGIZE(expr) " must not be null"); \
      } \
    } while (0)

#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
