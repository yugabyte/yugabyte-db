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

#ifndef YB_UTIL_STATUS_H_
#define YB_UTIL_STATUS_H_

#include <stdint.h>
#include <string>

#ifdef YB_HEADERS_NO_STUBS
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#else
#include "yb/client/stubs.h"
#endif

#include "yb/util/yb_export.h"
#include "yb/util/slice.h"

// We can't include strings/substitute.h because of client_samples-test: adding this here pulls a
// lot of dependencies into the client. Therefore, we require that any user of STATUS_SUBSTITUTE
// include this header on their own.
// #include "yb/gutil/strings/substitute.h"

// Return the given status if it is not OK.
#define YB_RETURN_NOT_OK(s) do { \
    ::yb::Status _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return _s;     \
  } while (0);

// Return the given status if it is not OK, but first clone it and
// prepend the given message.
#define YB_RETURN_NOT_OK_PREPEND(s, msg) do { \
    ::yb::Status _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) return _s.CloneAndPrepend(msg); \
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
  const auto _s = (to_call); \
  YB_CHECK(_s.ok()) << (msg) << ": " << _s.ToString(); \
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

class YB_EXPORT Status {
 public:
  Status() {}

  // Create a success status.
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
  static Status NotFound(const char* file_name,
                         int line_number,
                         const Slice& msg,
                         const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kNotFound, msg, msg2, posix_code, file_name, line_number);
  }
  static Status Corruption(const char* file_name,
                           int line_number,
                           const Slice& msg,
                           const Slice& msg2 = Slice(),
                           int16_t posix_code = -1) {
    return Status(kCorruption, msg, msg2, posix_code, file_name, line_number);
  }
  static Status NotSupported(const char* file_name,
                             int line_number,
                             const Slice& msg,
                             const Slice& msg2 = Slice(),
                             int16_t posix_code = -1) {
    return Status(kNotSupported, msg, msg2, posix_code, file_name, line_number);
  }
  static Status InvalidArgument(const char* file_name,
                                int line_number,
                                const Slice& msg,
                                const Slice& msg2 = Slice(),
                                int16_t posix_code = -1) {
    return Status(kInvalidArgument, msg, msg2, posix_code, file_name, line_number);
  }
  static Status IOError(const char* file_name,
                        int line_number,
                        const Slice& msg,
                        const Slice& msg2 = Slice(),
                        int16_t posix_code = -1) {
    return Status(kIOError, msg, msg2, posix_code, file_name, line_number);
  }
  static Status AlreadyPresent(const char* file_name,
                               int line_number,
                               const Slice& msg,
                               const Slice& msg2 = Slice(),
                               int16_t posix_code = -1) {
    return Status(kAlreadyPresent, msg, msg2, posix_code, file_name, line_number);
  }
  static Status RuntimeError(const char* file_name,
                             int line_number,
                             const Slice& msg,
                             const Slice& msg2 = Slice(),
                             int16_t posix_code = -1) {
    return Status(kRuntimeError, msg, msg2, posix_code, file_name, line_number);
  }
  static Status NetworkError(const char* file_name,
                             int line_number,
                             const Slice& msg,
                             const Slice& msg2 = Slice(),
                             int16_t posix_code = -1) {
    return Status(kNetworkError, msg, msg2, posix_code, file_name, line_number);
  }
  static Status IllegalState(const char* file_name,
                             int line_number,
                             const Slice& msg,
                             const Slice& msg2 = Slice(),
                             int16_t posix_code = -1) {
    return Status(kIllegalState, msg, msg2, posix_code, file_name, line_number);
  }
  static Status NotAuthorized(const char* file_name,
                              int line_number,
                              const Slice& msg,
                              const Slice& msg2 = Slice(),
                              int16_t posix_code = -1) {
    return Status(kNotAuthorized, msg, msg2, posix_code, file_name, line_number);
  }
  static Status Aborted(const char* file_name,
                        int line_number,
                        const Slice& msg,
                        const Slice& msg2 = Slice(),
                        int16_t posix_code = -1) {
    return Status(kAborted, msg, msg2, posix_code, file_name, line_number);
  }
  static Status RemoteError(const char* file_name,
                            int line_number,
                            const Slice& msg,
                            const Slice& msg2 = Slice(),
                            int16_t posix_code = -1) {
    return Status(kRemoteError, msg, msg2, posix_code, file_name, line_number);
  }
  static Status ServiceUnavailable(const char* file_name,
                                   int line_number,
                                   const Slice& msg,
                                   const Slice& msg2 = Slice(),
                                   int16_t posix_code = -1) {
    return Status(kServiceUnavailable, msg, msg2, posix_code, file_name, line_number);
  }
  static Status TimedOut(const char* file_name,
                         int line_number,
                         const Slice& msg,
                         const Slice& msg2 = Slice(),
                         int16_t posix_code = -1) {
    return Status(kTimedOut, msg, msg2, posix_code, file_name, line_number);
  }
  static Status Uninitialized(const char* file_name,
                              int line_number,
                              const Slice& msg,
                              const Slice& msg2 = Slice(),
                              int16_t posix_code = -1) {
    return Status(kUninitialized, msg, msg2, posix_code, file_name, line_number);
  }
  static Status ConfigurationError(const char* file_name,
                                   int line_number,
                                   const Slice& msg,
                                   const Slice& msg2 = Slice(),
                                   int16_t posix_code = -1) {
    return Status(kConfigurationError, msg, msg2, posix_code, file_name, line_number);
  }
  static Status Incomplete(const char* file_name,
                           int line_number,
                           const Slice& msg,
                           const Slice& msg2 = Slice(),
                           int64_t posix_code = -1) {
    return Status(kIncomplete, msg, msg2, posix_code, file_name, line_number);
  }
  static Status EndOfFile(const char* file_name,
                          int line_number,
                          const Slice& msg,
                          const Slice& msg2 = Slice(),
                          int64_t posix_code = -1) {
    return Status(kEndOfFile, msg, msg2, posix_code, file_name, line_number);
  }
  static Status InvalidCommand(const char* file_name,
                                int line_number,
                                const Slice& msg,
                                const Slice& msg2 = Slice(),
                                int16_t posix_code = -1) {
    return Status(kInvalidCommand, msg, msg2, posix_code, file_name, line_number);
  }

  // Returns true iff the status indicates success.
  bool ok() const { return (state_ == nullptr); }

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

  // Returns true iff the status indicates an InvalidCommand error
  bool IsInvalidCommand() const { return code() == kInvalidCommand; }

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
  const char* state_ = nullptr;

  // This must always be a pointer to a constant string. The status object does not own this string.
  const char* file_name_ = nullptr;
  int line_number_ = 0;

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
    kInvalidCommand = 19,
    // NOTE: Remember to duplicate these constants into wire_protocol.proto and
    // and to add StatusTo/FromPB ser/deser cases in wire_protocol.cc !
    //
    // TODO: Move error codes into an error_code.proto or something similar.
  };
  COMPILE_ASSERT(sizeof(Code) == 4, code_enum_size_is_part_of_abi);

  Code code() const {
    return (state_ == NULL) ? kOk : static_cast<Code>(state_[4]);
  }

  Status(Code code,
         const Slice& msg,
         const Slice& msg2,
         int16_t posix_code,
         const char* file_name,
         int line_number);
  static const char* CopyState(const char* s);
};

inline Status::Status(const Status& s)
    : file_name_(s.file_name_),
      line_number_(s.line_number_) {
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
inline Status::Status(Status&& s)
    : state_(s.state_),
      file_name_(s.file_name_),
      line_number_(s.line_number_)  {
  s.state_ = nullptr;
}

inline void Status::operator=(Status&& s) {
  if (state_ != s.state_) {
    delete[] state_;
    state_ = s.state_;
    s.state_ = nullptr;
  }
  file_name_ = s.file_name_;
  line_number_ = s.line_number_;
}
#endif

}  // namespace yb

#define STATUS(status_type, ...) (Status::status_type(__FILE__, __LINE__, __VA_ARGS__))
#define STATUS_SUBSTITUTE(status_type, ...) \
    (Status::status_type(__FILE__, __LINE__, strings::Substitute(__VA_ARGS__)))

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

#define CHECKED_STATUS MUST_USE_RESULT yb::Status

#endif  // YB_UTIL_STATUS_H_
