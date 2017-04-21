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
#ifndef YB_CLIENT_CALLBACKS_H_
#define YB_CLIENT_CALLBACKS_H_

#ifdef YB_HEADERS_NO_STUBS
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#include "yb/gutil/walltime.h"
#else
#include "yb/client/stubs.h"
#endif
#include "yb/util/yb_export.h"

namespace yb {

class Status;

namespace client {

// All possible log levels.
enum YBLogSeverity {
  SEVERITY_INFO,
  SEVERITY_WARNING,
  SEVERITY_ERROR,
  SEVERITY_FATAL
};

// Interface for all logging callbacks.
class YB_EXPORT YBLoggingCallback {
 public:
  YBLoggingCallback() {
  }

  virtual ~YBLoggingCallback() {
  }

  // 'message' is NOT terminated with an endline.
  virtual void Run(YBLogSeverity severity,
                   const char* filename,
                   int line_number,
                   const struct ::tm* time,
                   const char* message,
                   size_t message_len) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(YBLoggingCallback);
};

// Logging callback that invokes a member function pointer.
template <typename T>
class YB_EXPORT YBLoggingMemberCallback : public YBLoggingCallback {
 public:
  typedef void (T::*MemberType)(
      YBLogSeverity severity,
      const char* filename,
      int line_number,
      const struct ::tm* time,
      const char* message,
      size_t message_len);

  YBLoggingMemberCallback(T* object, MemberType member)
    : object_(object),
      member_(member) {
  }

  virtual void Run(YBLogSeverity severity,
                   const char* filename,
                   int line_number,
                   const struct ::tm* time,
                   const char* message,
                   size_t message_len) override {
    (object_->*member_)(severity, filename, line_number, time,
        message, message_len);
  }

 private:
  T* object_;
  MemberType member_;
};

// Logging callback that invokes a function pointer with a single argument.
template <typename T>
class YB_EXPORT YBLoggingFunctionCallback : public YBLoggingCallback {
 public:
  typedef void (*FunctionType)(T arg,
      YBLogSeverity severity,
      const char* filename,
      int line_number,
      const struct ::tm* time,
      const char* message,
      size_t message_len);

  YBLoggingFunctionCallback(FunctionType function, T arg)
    : function_(function),
      arg_(arg) {
  }

  virtual void Run(YBLogSeverity severity,
                   const char* filename,
                   int line_number,
                   const struct ::tm* time,
                   const char* message,
                   size_t message_len) override {
    function_(arg_, severity, filename, line_number, time,
              message, message_len);
  }

 private:
  FunctionType function_;
  T arg_;
};

// Interface for all status callbacks.
class YB_EXPORT YBStatusCallback {
 public:
  YBStatusCallback() {
  }

  virtual ~YBStatusCallback() {
  }

  virtual void Run(const Status& s) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(YBStatusCallback);
};

// Status callback that invokes a member function pointer.
template <typename T>
class YB_EXPORT YBStatusMemberCallback : public YBStatusCallback {
 public:
  typedef void (T::*MemberType)(const Status& s);

  YBStatusMemberCallback(T* object, MemberType member)
    : object_(object),
      member_(member) {
  }

  virtual void Run(const Status& s) override {
    (object_->*member_)(s);
  }

 private:
  T* object_;
  MemberType member_;
};

// Status callback that invokes a function pointer with a single argument.
template <typename T>
class YB_EXPORT YBStatusFunctionCallback : public YBStatusCallback {
 public:
  typedef void (*FunctionType)(T arg, const Status& s);

  YBStatusFunctionCallback(FunctionType function, T arg)
    : function_(function),
      arg_(arg) {
  }

  virtual void Run(const Status& s) override {
    function_(arg_, s);
  }

 private:
  FunctionType function_;
  T arg_;
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_CALLBACKS_H_
