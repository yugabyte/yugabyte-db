// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <optional>
#include <utility>

#include "yb/gutil/port.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb::dockv {

// Compile-time choice of how key decoders report a failure.
// DecodePolicy<true>: Status / Result<T> describing the input, for callers which return the status
// to the client or log it.
// DecodePolicy<false>: ProbeStatus / std::optional<T>, nothing is built on failure. For callers
// which treat a non-DocKey input as a normal outcome, e.g. the bloom filter key transformer.
// Functions using the DOCKV_DECODE_* macros below declare `using Policy = DecodePolicy<...>;`.
struct NODISCARD_CLASS ProbeStatus {
  constexpr explicit ProbeStatus(bool ok) : ok_(ok) {}

  bool ok() const { return ok_; }

  bool ok_;
};

// Converts to the failure value of every DecodePolicy<false> return type.
struct ProbeFailure {
  operator ProbeStatus() const { return ProbeStatus{false}; }  // NOLINT

  template <class T>
  operator std::optional<T>() const { return std::nullopt; }  // NOLINT
};

template <bool kDescribeErrors>
struct DecodePolicy;

template <>
struct DecodePolicy<true> {
  using StatusType = Status;
  template <class T>
  using ResultType = Result<T>;

  static Status Ok() { return Status::OK(); }

  static bool IsOk(const Status& status) { return status.ok(); }
  template <class T>
  static bool IsOk(const Result<T>& result) { return result.ok(); }

  static Status Failure(Status&& status) { return std::move(status); }
  template <class T>
  static Status Failure(Result<T>&& result) { return std::move(result.status()); }

  template <class T>
  static T&& Value(Result<T>&& result) { return std::move(*result); }

  // Calls make_status, which builds the described status.
  template <class MakeStatus>
  static Status Error(const MakeStatus& make_status) { return make_status(); }
};

template <>
struct DecodePolicy<false> {
  using StatusType = ProbeStatus;
  template <class T>
  using ResultType = std::optional<T>;

  static ProbeStatus Ok() { return ProbeStatus{true}; }

  static bool IsOk(ProbeStatus status) { return status.ok(); }
  static bool IsOk(const Status& status) { return status.ok(); }
  template <class T>
  static bool IsOk(const std::optional<T>& result) { return result.has_value(); }
  template <class T>
  static bool IsOk(const Result<T>& result) { return result.ok(); }

  template <class T>
  static ProbeFailure Failure(T&&) { return ProbeFailure(); }

  template <class T>
  static T&& Value(std::optional<T>&& result) { return std::move(*result); }
  template <class T>
  static T&& Value(Result<T>&& result) { return std::move(*result); }

  // Does not call make_status, so the description is never built.
  template <class MakeStatus>
  static ProbeFailure Error(const MakeStatus&) { return ProbeFailure(); }
};

// Counterparts of RETURN_NOT_OK, VERIFY_RESULT and STATUS_FORMAT(Corruption, ...) for functions
// returning Policy::StatusType or Policy::ResultType<T>. Also accept Status and Result<T> from
// decoders outside of dockv.
#define DOCKV_DECODE_RETURN_NOT_OK(...) \
  do { \
    auto&& _status = (__VA_ARGS__); \
    if (PREDICT_FALSE(!Policy::IsOk(_status))) { \
      return Policy::Failure(std::move(_status)); \
    } \
  } while (false)

#define DOCKV_DECODE_VERIFY_RESULT(...) \
  __extension__ ({ \
    auto&& _result = (__VA_ARGS__); \
    if (PREDICT_FALSE(!Policy::IsOk(_result))) { \
      return Policy::Failure(std::move(_result)); \
    } \
    Policy::Value(std::move(_result)); \
  })

#define DOCKV_DECODE_ERROR(...) \
  Policy::Error([&]() -> ::yb::Status { return STATUS_FORMAT(Corruption, __VA_ARGS__); })

}  // namespace yb::dockv
