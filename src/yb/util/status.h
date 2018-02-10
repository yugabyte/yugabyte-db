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

#ifndef YB_UTIL_STATUS_H_
#define YB_UTIL_STATUS_H_

#include <atomic>
#include <memory>
#include <string>

#include <boost/intrusive_ptr.hpp>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

#ifdef YB_HEADERS_NO_STUBS
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#else
#include "yb/client/stubs.h"
#endif


#include "yb/util/slice.h"
#include "yb/util/format.h"
#include "yb/gutil/strings/substitute.h"

// Return the given status if it is not OK.
#define YB_RETURN_NOT_OK(s) do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return MoveStatus(std::move(_s)); \
  } while (false)

// Return the given status if it is not OK, but first clone it and
// prepend the given message.
#define YB_RETURN_NOT_OK_PREPEND(s, msg) do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return MoveStatus(_s).CloneAndPrepend(msg); \
  } while (0);

// Return 'to_return' if 'to_call' returns a bad status.
// The substitution for 'to_return' may reference the variable
// 's' for the bad status.
#define YB_RETURN_NOT_OK_RET(to_call, to_return) do { \
    ::yb::Status s = (to_call); \
    if (PREDICT_FALSE(!s.ok())) return (to_return);  \
  } while (0);

// Emit a warning if 'to_call' returns a bad status.
#define YB_WARN_NOT_OK(to_call, warning_prefix) do { \
    ::yb::Status _s = (to_call); \
    if (PREDICT_FALSE(!_s.ok())) { \
      YB_LOG(WARNING) << (warning_prefix) << ": " << _s.ToString();  \
    } \
  } while (0);

// Log the given status and return immediately.
#define YB_LOG_AND_RETURN(level, status) do { \
    ::yb::Status _s = (status); \
    YB_LOG(level) << _s.ToString(); \
    return _s; \
  } while (0);

// If 'to_call' returns a bad status, CHECK immediately with a logged message
// of 'msg' followed by the status.
#define YB_CHECK_OK_PREPEND(to_call, msg) do { \
  auto&& _s = (to_call); \
  YB_CHECK(_s.ok()) << (msg) << ": " << StatusToString(_s); \
  } while (0);

// If the status is bad, CHECK immediately, appending the status to the
// logged message.
#define YB_CHECK_OK(s) YB_CHECK_OK_PREPEND(s, "Bad status")

// This header is used in both the YB build as well as in builds of
// applications that use the YB C++ client. In the latter we need to be
// careful to "namespace" our macros, to avoid colliding or overriding with
// similarly named macros belonging to the application.
//
// YB_HEADERS_USE_SHORT_STATUS_MACROS handles this behavioral change. When
// defined, we're building YB and:
// 1. Non-namespaced macros are allowed and mapped to the namespaced versions
//    defined above.
// 2. Namespaced versions of glog macros are mapped to the real glog macros
//    (otherwise the macros are defined in the C++ client stubs).
#ifdef YB_HEADERS_USE_SHORT_STATUS_MACROS
#define RETURN_NOT_OK         YB_RETURN_NOT_OK
#define RETURN_NOT_OK_PREPEND YB_RETURN_NOT_OK_PREPEND
#define RETURN_NOT_OK_RET     YB_RETURN_NOT_OK_RET
#define WARN_NOT_OK           YB_WARN_NOT_OK
#define LOG_AND_RETURN        YB_LOG_AND_RETURN
#define CHECK_OK_PREPEND      YB_CHECK_OK_PREPEND
#define CHECK_OK              YB_CHECK_OK

// These are standard glog macros.
#define YB_LOG              LOG
#define YB_CHECK            CHECK
#endif

namespace yb {

#define YB_STATUS_CODES \
    ((Ok, 0, "OK")) \
    ((NotFound, 1, "Not found")) \
    ((Corruption, 2, "Corruption")) \
    ((NotSupported, 3, "Not implemented")) \
    ((InvalidArgument, 4, "Invalid argument")) \
    ((IOError, 5, "IO error")) \
    ((AlreadyPresent, 6, "Already present")) \
    ((RuntimeError, 7, "Runtime error")) \
    ((NetworkError, 8, "Network error")) \
    ((IllegalState, 9, "Illegal state")) \
    ((NotAuthorized, 10, "Not authorized")) \
    ((Aborted, 11, "Aborted")) \
    ((RemoteError, 12, "Remote error")) \
    ((ServiceUnavailable, 13, "Service unavailable")) \
    ((TimedOut, 14, "Timed out")) \
    ((Uninitialized, 15, "Uninitialized")) \
    ((ConfigurationError, 16, "Configuration error")) \
    ((Incomplete, 17, "Incomplete")) \
    ((EndOfFile, 18, "End of file")) \
    ((InvalidCommand, 19, "Invalid command")) \
    ((QLError, 20, "SQL error")) \
    ((InternalError, 21, "Internal error")) \
    ((ShutdownInProgress, 22, "Shutdown in progress")) \
    ((MergeInProgress, 23, "Merge in progress")) \
    ((Busy, 24, "Resource busy")) \
    ((Expired, 25, "Operation expired")) \
    ((TryAgain, 26, "Operation failed. Try again.")) \
    ((LeaderNotReadyToServe, 27, "Leader not ready to serve requests.")) \
    ((LeaderHasNoLease, 28, "Leader does not have a valid lease.")) \
    ((Combined, 29, "Combined status representing multiple status failures.")) \
    /**/

#define YB_STATUS_CODE_DECLARE(name, value, message) \
    BOOST_PP_CAT(k, name) = value,

#define YB_STATUS_CODE_IS_FUNC(name, value, message) \
    bool BOOST_PP_CAT(Is, name)() const { \
      return code() == BOOST_PP_CAT(k, name); \
    } \
    /**/

#define YB_STATUS_FORWARD_MACRO(r, data, tuple) data tuple

enum class TimeoutError {
  kMutexTimeout = 1,
  kLockTimeout = 2,
  kLockLimit = 3,
};

class Status {
 public:
  // Create a success status.
  Status() {}

  // Return a success status.
  static Status OK() { return Status(); }

  // Returns true if the status indicates success.
  bool ok() const { return state_ == nullptr; }

  // Declares set of Is* functions
  BOOST_PP_SEQ_FOR_EACH(YB_STATUS_FORWARD_MACRO, YB_STATUS_CODE_IS_FUNC, YB_STATUS_CODES)

  // Return a string representation of this status suitable for printing.
  // Returns the string "OK" for success.
  std::string ToString(bool include_file_and_line = true) const;

  // Return a string representation of the status code, without the message
  // text or posix code information.
  std::string CodeAsString() const;

  // Return the message portion of the Status. This is similar to ToString,
  // except that it does not include the stringified error code or posix code.
  //
  // For OK statuses, this returns an empty string.
  //
  // The returned Slice is only valid as long as this Status object remains
  // live and unchanged.
  Slice message() const;

  int64_t error_code() const { return state_ ? state_->error_code : 0; }

  // Return a new Status object with the same state plus an additional leading message.
  Status CloneAndPrepend(const Slice& msg) const;

  // Same as CloneAndPrepend, but appends to the message instead.
  Status CloneAndAppend(const Slice& msg) const;

  // Returns the memory usage of this object without the object itself. Should
  // be used when embedded inside another object.
  size_t memory_footprint_excluding_this() const;

  // Returns the memory usage of this object including the object itself.
  // Should be used when allocated on the heap.
  size_t memory_footprint_including_this() const;

  enum Code {
    BOOST_PP_SEQ_FOR_EACH(YB_STATUS_FORWARD_MACRO, YB_STATUS_CODE_DECLARE, YB_STATUS_CODES)

    // NOTE: Remember to duplicate these constants into wire_protocol.proto and
    // and to add StatusTo/FromPB ser/deser cases in wire_protocol.cc !
    //
    // TODO: Move error codes into an error_code.proto or something similar.
  };

  Status(Code code,
         const char* file_name,
         int line_number,
         const Slice& msg,
         const Slice& msg2 = Slice(),
         int64_t error_code = -1);

  Status(Code code,
         const char* file_name,
         int line_number,
         TimeoutError error_code);

  Code code() const {
    return (state_ == nullptr) ? kOk : static_cast<Code>(state_->code);
  }
 private:
  struct State {
    std::atomic<size_t> counter;
    uint32_t message_len;
    uint8_t code;
    int64_t error_code;
    // This must always be a pointer to a constant string.
    // The status object does not own this string.
    const char* file_name;
    int line_number;
    char message[1];
  };

  typedef boost::intrusive_ptr<State> StatePtr;

  friend inline void intrusive_ptr_release(State* state) {
    if (state->counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      free(state);
    }
  }

  friend inline void intrusive_ptr_add_ref(State* state) {
    state->counter.fetch_add(1, std::memory_order_relaxed);
  }

  StatePtr state_;
  static constexpr size_t kHeaderSize = offsetof(State, message);

  static_assert(sizeof(Code) == 4, "Code enum size is part of abi");
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

}  // namespace yb

#define STATUS(status_type, ...) \
    (Status(Status::BOOST_PP_CAT(k, status_type), __FILE__, __LINE__, __VA_ARGS__))
#define STATUS_SUBSTITUTE(status_type, ...) \
    (Status(Status::BOOST_PP_CAT(k, status_type), \
            __FILE__, \
            __LINE__, \
            strings::Substitute(__VA_ARGS__)))

#define STATUS_FORMAT(status_type, ...) \
    (::yb::Status(::yb::Status::BOOST_PP_CAT(k, status_type), \
            __FILE__, \
            __LINE__, \
            ::yb::Format(__VA_ARGS__)))

// Utility macros to perform the appropriate check. If the check fails,
// returns the specified (error) Status, with the given message.
#define SCHECK_OP(var1, op, var2, type, msg)                        \
  do {                                                              \
    if (PREDICT_FALSE(!((var1)op(var2)))) return STATUS(type, msg); \
  } while (0)
#define SCHECK(expr, type, msg) SCHECK_OP(expr, ==, true, type, msg)
#define SCHECK_EQ(var1, var2, type, msg) SCHECK_OP(var1, ==, var2, type, msg)
#define SCHECK_NE(var1, var2, type, msg) SCHECK_OP(var1, !=, var2, type, msg)
#define SCHECK_GT(var1, var2, type, msg) SCHECK_OP(var1, >, var2, type, msg)  // NOLINT.
#define SCHECK_GE(var1, var2, type, msg) SCHECK_OP(var1, >=, var2, type, msg)
#define SCHECK_LT(var1, var2, type, msg) SCHECK_OP(var1, <, var2, type, msg)  // NOLINT.
#define SCHECK_LE(var1, var2, type, msg) SCHECK_OP(var1, <=, var2, type, msg)
#define SCHECK_BOUNDS(var1, lbound, rbound, type, msg) \
    do { \
      SCHECK_GE(var1, lbound, type, msg); \
      SCHECK_LE(var1, rbound, type, msg); \
    } while(false)

#ifdef YB_HEADERS_NO_STUBS
#define CHECKED_STATUS MUST_USE_RESULT ::yb::Status
#else
// Only for the build using client headers. MUST_USE_RESULT is undefined in that case.
#define CHECKED_STATUS ::yb::Status
#endif

#endif  // YB_UTIL_STATUS_H_
