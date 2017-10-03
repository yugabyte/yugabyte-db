// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A Status encapsulates the result of an operation.  It may indicate success,
// or it may indicate an error with an associated error message.
//
// Multiple threads can invoke const methods on a Status without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Status must use
// external synchronization.

#ifndef KUDU_UTIL_STATUS_H_
#define KUDU_UTIL_STATUS_H_

#include <stdint.h>
#include <string>

#ifdef KUDU_HEADERS_NO_STUBS
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#else
#include "kudu/client/stubs.h"
#endif

#include "kudu/util/kudu_export.h"
#include "kudu/util/slice.h"

// Return the given status if it is not OK.
#define KUDU_RETURN_NOT_OK(s) do { \
    ::kudu::Status _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return _s;     \
  } while (0);

// Return the given status if it is not OK, but first clone it and
// prepend the given message.
#define KUDU_RETURN_NOT_OK_PREPEND(s, msg) do { \
    ::kudu::Status _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return _s.CloneAndPrepend(msg); \
  } while (0);

// Return 'to_return' if 'to_call' returns a bad status.
// The substitution for 'to_return' may reference the variable
// 's' for the bad status.
#define KUDU_RETURN_NOT_OK_RET(to_call, to_return) do { \
    ::kudu::Status s = (to_call); \
    if (PREDICT_FALSE(!s.ok())) return (to_return);  \
  } while (0);

// Emit a warning if 'to_call' returns a bad status.
#define KUDU_WARN_NOT_OK(to_call, warning_prefix) do { \
    ::kudu::Status _s = (to_call); \
    if (PREDICT_FALSE(!_s.ok())) { \
      KUDU_LOG(WARNING) << (warning_prefix) << ": " << _s.ToString();  \
    } \
  } while (0);

// Log the given status and return immediately.
#define KUDU_LOG_AND_RETURN(level, status) do { \
    ::kudu::Status _s = (status); \
    KUDU_LOG(level) << _s.ToString(); \
    return _s; \
  } while (0);

// If 'to_call' returns a bad status, CHECK immediately with a logged message
// of 'msg' followed by the status.
#define KUDU_CHECK_OK_PREPEND(to_call, msg) do { \
  ::kudu::Status _s = (to_call); \
  KUDU_CHECK(_s.ok()) << (msg) << ": " << _s.ToString(); \
  } while (0);

// If the status is bad, CHECK immediately, appending the status to the
// logged message.
#define KUDU_CHECK_OK(s) KUDU_CHECK_OK_PREPEND(s, "Bad status")

// This header is used in both the Kudu build as well as in builds of
// applications that use the Kudu C++ client. In the latter we need to be
// careful to "namespace" our macros, to avoid colliding or overriding with
// similarly named macros belonging to the application.
//
// KUDU_HEADERS_USE_SHORT_STATUS_MACROS handles this behavioral change. When
// defined, we're building Kudu and:
// 1. Non-namespaced macros are allowed and mapped to the namespaced versions
//    defined above.
// 2. Namespaced versions of glog macros are mapped to the real glog macros
//    (otherwise the macros are defined in the C++ client stubs).
#ifdef KUDU_HEADERS_USE_SHORT_STATUS_MACROS
#define RETURN_NOT_OK         KUDU_RETURN_NOT_OK
#define RETURN_NOT_OK_PREPEND KUDU_RETURN_NOT_OK_PREPEND
#define RETURN_NOT_OK_RET     KUDU_RETURN_NOT_OK_RET
#define WARN_NOT_OK           KUDU_WARN_NOT_OK
#define LOG_AND_RETURN        KUDU_LOG_AND_RETURN
#define CHECK_OK_PREPEND      KUDU_CHECK_OK_PREPEND
#define CHECK_OK              KUDU_CHECK_OK

// These are standard glog macros.
#define KUDU_LOG              LOG
#define KUDU_CHECK            CHECK
#endif

namespace kudu {

class KUDU_EXPORT Status {
 public:
  // Create a success status.
  Status() : state_(NULL) { }
  ~Status() { delete[] state_; }

  // Copy the specified status.
  Status(const Status& s);
  void operator=(const Status& s);

#if __cplusplus >= 201103L
  // Move the specified status.
  Status(Status&& s);
  void operator=(Status&& s);
#endif

  // Return a success status.
  static Status OK() { return Status(); }

  // Return error status of an appropriate type.
  static Status NotFound(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kNotFound, msg, msg2, posix_code);
  }
  static Status Corruption(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kCorruption, msg, msg2, posix_code);
  }
  static Status NotSupported(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kNotSupported, msg, msg2, posix_code);
  }
  static Status InvalidArgument(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kInvalidArgument, msg, msg2, posix_code);
  }
  static Status IOError(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kIOError, msg, msg2, posix_code);
  }
  static Status AlreadyPresent(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kAlreadyPresent, msg, msg2, posix_code);
  }
  static Status RuntimeError(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kRuntimeError, msg, msg2, posix_code);
  }
  static Status NetworkError(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kNetworkError, msg, msg2, posix_code);
  }
  static Status IllegalState(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kIllegalState, msg, msg2, posix_code);
  }
  static Status NotAuthorized(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kNotAuthorized, msg, msg2, posix_code);
  }
  static Status Aborted(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kAborted, msg, msg2, posix_code);
  }
  static Status RemoteError(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kRemoteError, msg, msg2, posix_code);
  }
  static Status ServiceUnavailable(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kServiceUnavailable, msg, msg2, posix_code);
  }
  static Status TimedOut(const Slice& msg, const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kTimedOut, msg, msg2, posix_code);
  }
  static Status Uninitialized(const Slice& msg, const Slice& msg2 = Slice(),
                              int16_t posix_code = -1) {
    return Status(kUninitialized, msg, msg2, posix_code);
  }
  static Status ConfigurationError(const Slice& msg, const Slice& msg2 = Slice(),
                                   int16_t posix_code = -1) {
    return Status(kConfigurationError, msg, msg2, posix_code);
  }
  static Status Incomplete(const Slice& msg, const Slice& msg2 = Slice(),
                           int64_t posix_code = -1) {
    return Status(kIncomplete, msg, msg2, posix_code);
  }
  static Status EndOfFile(const Slice& msg, const Slice& msg2 = Slice(),
                          int64_t posix_code = -1) {
    return Status(kEndOfFile, msg, msg2, posix_code);
  }

  // Returns true iff the status indicates success.
  bool ok() const { return (state_ == NULL); }

  // Returns true iff the status indicates a NotFound error.
  bool IsNotFound() const { return code() == kNotFound; }

  // Returns true iff the status indicates a Corruption error.
  bool IsCorruption() const { return code() == kCorruption; }

  // Returns true iff the status indicates a NotSupported error.
  bool IsNotSupported() const { return code() == kNotSupported; }

  // Returns true iff the status indicates an IOError.
  bool IsIOError() const { return code() == kIOError; }

  // Returns true iff the status indicates an InvalidArgument error
  bool IsInvalidArgument() const { return code() == kInvalidArgument; }

  // Returns true iff the status indicates an AlreadyPresent error
  bool IsAlreadyPresent() const { return code() == kAlreadyPresent; }

  // Returns true iff the status indicates a RuntimeError.
  bool IsRuntimeError() const { return code() == kRuntimeError; }

  // Returns true iff the status indicates a NetworkError.
  bool IsNetworkError() const { return code() == kNetworkError; }

  // Returns true iff the status indicates a IllegalState.
  bool IsIllegalState() const { return code() == kIllegalState; }

  // Returns true iff the status indicates a NotAuthorized.
  bool IsNotAuthorized() const { return code() == kNotAuthorized; }

  // Returns true iff the status indicates Aborted.
  bool IsAborted() const { return code() == kAborted; }

  // Returns true iff the status indicates RemoteError.
  bool IsRemoteError() const { return code() == kRemoteError; }

  // Returns true iff the status indicates ServiceUnavailable.
  bool IsServiceUnavailable() const { return code() == kServiceUnavailable; }

  // Returns true iff the status indicates TimedOut.
  bool IsTimedOut() const { return code() == kTimedOut; }

  // Returns true iff the status indicates Uninitialized.
  bool IsUninitialized() const { return code() == kUninitialized; }

  // Returns true iff the status indicates Configuration error.
  bool IsConfigurationError() const { return code() == kConfigurationError; }

  // Returns true iff the status indicates Incomplete.
  bool IsIncomplete() const { return code() == kIncomplete; }

  // Returns true iff the status indicates end of file.
  bool IsEndOfFile() const { return code() == kEndOfFile; }

  // Return a string representation of this status suitable for printing.
  // Returns the string "OK" for success.
  std::string ToString() const;

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

  // Get the POSIX code associated with this Status, or -1 if there is none.
  int16_t posix_code() const;

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

 private:
  // OK status has a NULL state_.  Otherwise, state_ is a new[] array
  // of the following form:
  //    state_[0..3] == length of message
  //    state_[4]    == code
  //    state_[5..6] == posix_code
  //    state_[7..]  == message
  const char* state_;

  enum Code {
    kOk = 0,
    kNotFound = 1,
    kCorruption = 2,
    kNotSupported = 3,
    kInvalidArgument = 4,
    kIOError = 5,
    kAlreadyPresent = 6,
    kRuntimeError = 7,
    kNetworkError = 8,
    kIllegalState = 9,
    kNotAuthorized = 10,
    kAborted = 11,
    kRemoteError = 12,
    kServiceUnavailable = 13,
    kTimedOut = 14,
    kUninitialized = 15,
    kConfigurationError = 16,
    kIncomplete = 17,
    kEndOfFile = 18,
    // NOTE: Remember to duplicate these constants into wire_protocol.proto and
    // and to add StatusTo/FromPB ser/deser cases in wire_protocol.cc !
    //
    // TODO: Move error codes into an error_code.proto or something similar.
  };
  COMPILE_ASSERT(sizeof(Code) == 4, code_enum_size_is_part_of_abi);

  Code code() const {
    return (state_ == NULL) ? kOk : static_cast<Code>(state_[4]);
  }

  Status(Code code, const Slice& msg, const Slice& msg2, int16_t posix_code);
  static const char* CopyState(const char* s);
};

inline Status::Status(const Status& s) {
  state_ = (s.state_ == NULL) ? NULL : CopyState(s.state_);
}
inline void Status::operator=(const Status& s) {
  // The following condition catches both aliasing (when this == &s),
  // and the common case where both s and *this are ok.
  if (state_ != s.state_) {
    delete[] state_;
    state_ = (s.state_ == NULL) ? NULL : CopyState(s.state_);
  }
}

#if __cplusplus >= 201103L
inline Status::Status(Status&& s) : state_(s.state_) {
  s.state_ = nullptr;
}

inline void Status::operator=(Status&& s) {
  if (state_ != s.state_) {
    delete[] state_;
    state_ = s.state_;
    s.state_ = nullptr;
  }
}
#endif

}  // namespace kudu

#endif  // KUDU_UTIL_STATUS_H_
