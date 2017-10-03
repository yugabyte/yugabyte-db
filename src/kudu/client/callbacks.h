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
#ifndef KUDU_CLIENT_CALLBACKS_H
#define KUDU_CLIENT_CALLBACKS_H

#ifdef KUDU_HEADERS_NO_STUBS
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#else
#include "kudu/client/stubs.h"
#endif
#include "kudu/util/kudu_export.h"

namespace kudu {

class Status;

namespace client {

// All possible log levels.
enum KuduLogSeverity {
  SEVERITY_INFO,
  SEVERITY_WARNING,
  SEVERITY_ERROR,
  SEVERITY_FATAL
};

// Interface for all logging callbacks.
class KUDU_EXPORT KuduLoggingCallback {
 public:
  KuduLoggingCallback() {
  }

  virtual ~KuduLoggingCallback() {
  }

  // 'message' is NOT terminated with an endline.
  virtual void Run(KuduLogSeverity severity,
                   const char* filename,
                   int line_number,
                   const struct ::tm* time,
                   const char* message,
                   size_t message_len) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(KuduLoggingCallback);
};

// Logging callback that invokes a member function pointer.
template <typename T>
class KUDU_EXPORT KuduLoggingMemberCallback : public KuduLoggingCallback {
 public:
  typedef void (T::*MemberType)(
      KuduLogSeverity severity,
      const char* filename,
      int line_number,
      const struct ::tm* time,
      const char* message,
      size_t message_len);

  KuduLoggingMemberCallback(T* object, MemberType member)
    : object_(object),
      member_(member) {
  }

  virtual void Run(KuduLogSeverity severity,
                   const char* filename,
                   int line_number,
                   const struct ::tm* time,
                   const char* message,
                   size_t message_len) OVERRIDE {
    (object_->*member_)(severity, filename, line_number, time,
        message, message_len);
  }

 private:
  T* object_;
  MemberType member_;
};

// Logging callback that invokes a function pointer with a single argument.
template <typename T>
class KUDU_EXPORT KuduLoggingFunctionCallback : public KuduLoggingCallback {
 public:
  typedef void (*FunctionType)(T arg,
      KuduLogSeverity severity,
      const char* filename,
      int line_number,
      const struct ::tm* time,
      const char* message,
      size_t message_len);

  KuduLoggingFunctionCallback(FunctionType function, T arg)
    : function_(function),
      arg_(arg) {
  }

  virtual void Run(KuduLogSeverity severity,
                   const char* filename,
                   int line_number,
                   const struct ::tm* time,
                   const char* message,
                   size_t message_len) OVERRIDE {
    function_(arg_, severity, filename, line_number, time,
              message, message_len);
  }

 private:
  FunctionType function_;
  T arg_;
};

// Interface for all status callbacks.
class KUDU_EXPORT KuduStatusCallback {
 public:
  KuduStatusCallback() {
  }

  virtual ~KuduStatusCallback() {
  }

  virtual void Run(const Status& s) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(KuduStatusCallback);
};

// Status callback that invokes a member function pointer.
template <typename T>
class KUDU_EXPORT KuduStatusMemberCallback : public KuduStatusCallback {
 public:
  typedef void (T::*MemberType)(const Status& s);

  KuduStatusMemberCallback(T* object, MemberType member)
    : object_(object),
      member_(member) {
  }

  virtual void Run(const Status& s) OVERRIDE {
    (object_->*member_)(s);
  }

 private:
  T* object_;
  MemberType member_;
};

// Status callback that invokes a function pointer with a single argument.
template <typename T>
class KUDU_EXPORT KuduStatusFunctionCallback : public KuduStatusCallback {
 public:
  typedef void (*FunctionType)(T arg, const Status& s);

  KuduStatusFunctionCallback(FunctionType function, T arg)
    : function_(function),
      arg_(arg) {
  }

  virtual void Run(const Status& s) OVERRIDE {
    function_(arg_, s);
  }

 private:
  FunctionType function_;
  T arg_;
};

} // namespace client
} // namespace kudu

#endif
