//
// Copyright (c) YugaByte, Inc.
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
//

#pragma once

#include <concepts>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/status.h"
#include "yb/util/tostring.h"

namespace yb {
namespace result::internal {

template<class TValue>
struct ResultTraits {
  using Stored = TValue;
  using Pointer = TValue*;

  static const Stored& ToStored(const TValue& value) { return value; }
  static Stored&& ToStored(TValue&& value) { return std::move(value); }
  static void Destroy(Stored* value) { value->~TValue(); }
  static TValue* GetPtr(Stored* value) { return value; }
  static const TValue* GetPtr(const Stored* value) { return value; }
};

template<class TValue>
struct ResultTraits<TValue&> {
  using Stored = TValue*;
  using Pointer = TValue*;

  static Stored ToStored(TValue& value) { return &value; }
  static void Destroy(Stored* value) {}
  static TValue* GetPtr(const Stored* value) { return *value; }
};

template<class R>
using ResultValueType = typename std::remove_cvref_t<R>::ValueType;

template<class T, class V>
concept ConvertibleValue =
    (std::is_reference_v<V> && std::convertible_to<T, V>) ||
    (!std::is_reference_v<V> && std::convertible_to<std::remove_cvref_t<T>, V>);

template<class R>
concept ResultType = std::same_as<std::remove_cvref_t<R>, Result<ResultValueType<R>>>;

template<class S>
concept StatusType = std::same_as<std::remove_cvref_t<S>, Status>;

template<class R, class TValue>
concept ConvertibleResultValueType = std::convertible_to<ResultValueType<R>, TValue>;

} // namespace result::internal

void StatusCheck(bool);

template<class TValue>
class [[nodiscard]] Result { // NOLINT
  using Traits = result::internal::ResultTraits<TValue>;

 public:
  using ValueType = TValue;

  // Forbid creation from Status::OK as value must be explicitly specified in case status is OK
  Result(const Status::OK&) = delete;
  Result(Status::OK&&) = delete;

  Result(Result&& rhs) : Result(std::move(rhs), {}) {}

  Result(const Result& rhs) : Result(rhs, {}) {}

  template<result::internal::ResultType R>
  requires(result::internal::ConvertibleResultValueType<R, TValue>)
  Result(R&& rhs) : Result(std::forward<R>(rhs), {}) {}

  template<result::internal::StatusType S>
  Result(S&& status) : success_(false), status_(std::forward<S>(status)) {
    StatusCheck(!status_.ok());
  }

  template<class UValue>
  requires(result::internal::ConvertibleValue<UValue, TValue>)
  Result(UValue&& value)
      : success_(true), value_(Traits::ToStored(std::forward<UValue>(value))) {}

  Result& operator=(const Result& rhs) {
    return &rhs == this ? *this : ReInit(rhs);
  }

  Result& operator=(Result&& rhs) {
    return &rhs == this ? *this : ReInit(std::move(rhs));
  }

  template<class T>
  requires(
      result::internal::StatusType<T> ||
      result::internal::ConvertibleValue<T, TValue> ||
      (result::internal::ResultType<T> &&
       result::internal::ConvertibleResultValueType<T, TValue>))
  Result& operator=(T&& t) {
    return ReInit(std::forward<T>(t));
  }

  MUST_USE_RESULT explicit operator bool() const {
    return ok();
  }

  MUST_USE_RESULT bool operator!() const {
    return !ok();
  }

  MUST_USE_RESULT bool ok() const {
#ifndef NDEBUG
    ANNOTATE_IGNORE_WRITES_BEGIN();
    success_checked_ = true;
    ANNOTATE_IGNORE_WRITES_END();
#endif
    return success_;
  }

  const Status& status() const& {
#ifndef NDEBUG
    StatusCheck(ANNOTATE_UNPROTECTED_READ(success_checked_));
#endif
    StatusCheck(!success_);
    return status_;
  }

  Status& status() & {
#ifndef NDEBUG
    StatusCheck(ANNOTATE_UNPROTECTED_READ(success_checked_));
#endif
    StatusCheck(!success_);
    return status_;
  }

  Status&& status() && {
#ifndef NDEBUG
    StatusCheck(ANNOTATE_UNPROTECTED_READ(success_checked_));
#endif
    StatusCheck(!success_);
    return std::move(status_);
  }

  auto& get() const { return *get_ptr(); }
  auto& operator*() const& { return *get_ptr(); }
  auto& operator*() & { return *get_ptr(); }

  TValue&& operator*() && {
#ifndef NDEBUG
    StatusCheck(ANNOTATE_UNPROTECTED_READ(success_checked_));
#endif
    StatusCheck(success_);
    return value_;
  }

  auto operator->() const { return get_ptr(); }
  auto operator->() { return get_ptr(); }

  auto get_ptr() const {
#ifndef NDEBUG
    StatusCheck(ANNOTATE_UNPROTECTED_READ(success_checked_));
#endif
    StatusCheck(success_);
    return Traits::GetPtr(&value_);
  }

  auto get_ptr() {
#ifndef NDEBUG
    StatusCheck(ANNOTATE_UNPROTECTED_READ(success_checked_));
#endif
    StatusCheck(success_);
    return Traits::GetPtr(&value_);
  }

  Status MoveTo(typename Traits::Pointer value) {
    if (!ok()) {
      return status();
    }
    *value = std::move(**this);
    return Status::OK();
  }

  std::string ToString() const {
    return ok() ? AsString(**this) : status().ToString();
  }

  ~Result() {
    if (success_) {
      Traits::Destroy(&value_);
    } else {
      status_.~Status();
    }
  }

 private:
  struct ConstructorRoutingTag {};

  template<result::internal::ResultType R>
  Result(R&& rhs, ConstructorRoutingTag) : success_(rhs.success_) {
    if (success_) {
      new (&value_) typename Traits::Stored(std::forward<R>(rhs).value_);
    } else {
      new (&status_) Status(std::forward<R>(rhs).status_);
    }
  }

  template<class T>
  Result& ReInit(T&& t) {
    this->~Result();
    return *new (this) Result(std::forward<T>(t));
  }

  const bool success_;
#ifndef NDEBUG
  mutable bool success_checked_ = false;
#endif
  union {
    Status status_;
    typename Traits::Stored value_;
  };

  template<class UValue> friend class Result;
};

// Specify Result<bool> to avoid confusion with operator bool and operator!.
template<>
Result<bool>::operator bool() const = delete;
template<>
bool Result<bool>::operator!() const = delete;

template<class TValue>
Status&& MoveStatus(Result<TValue>&& result) {
  return std::move(result.status());
}

template<class TValue>
const Status& MoveStatus(const Result<TValue>& result) {
  return result.status();
}

template<class TValue>
inline std::string StatusToString(const Result<TValue>& result) {
  return result.status().ToString();
}

template<class TValue>
std::ostream& operator<<(std::ostream& out, const Result<TValue>& result) {
  return result.ok() ? out << *result : out << result.status();
}

template<class Functor>
class ResultToStatusAdaptor {
 public:
  explicit ResultToStatusAdaptor(const Functor& functor) : functor_(functor) {}

  template<class Output, class... Args>
  Status operator()(Output* output, Args&&... args) {
    auto result = functor_(std::forward<Args>(args)...);
    RETURN_NOT_OK(result);
    *output = std::move(*result);
    return Status::OK();
  }
 private:
  Functor functor_;
};

template<class Functor>
ResultToStatusAdaptor<Functor> ResultToStatus(const Functor& functor) {
  return ResultToStatusAdaptor<Functor>(functor);
}

template<class TValue>
Status ResultToStatus(const Result<TValue>& result) {
  return result.ok() ? Status::OK() : result.status();
}

template<class TValue>
TValue ResultToValue(const Result<TValue>& result, const TValue& value_for_error) {
  return result.ok() ? *result : value_for_error;
}

template<class TValue>
TValue ResultToValue(Result<TValue>&& result, const TValue& value_for_error) {
  return result.ok() ? std::move(*result) : value_for_error;
}

template<class TValue>
TValue ResultToValue(const Result<TValue>& result, TValue&& value_for_error) {
  return result.ok() ? *result : std::move(value_for_error);
}

template<class TValue>
TValue ResultToValue(Result<TValue>&& result, TValue&& value_for_error) {
  return result.ok() ? std::move(*result) : std::move(value_for_error);
}

/*
 * GNU statement expression extension forces to return value and not rvalue reference.
 * As a result VERIFY_RESULT or similar helpers will call move or copy constructor of T even
 * for Result<T&>/Result<const T&>
 * To avoid this undesirable behavior for Result<T&>/Result<const T&>, the std::reference_wrapper<T>
 * is returned from statement.
 * The following functions help implement this strategy.
 */
template<class T>
T&& WrapMove(Result<T>&& result) {
  return std::move(*result);
}

template<class T>
std::reference_wrapper<T> WrapMove(Result<T&>&& result) {
  return std::reference_wrapper<T>(*result);
}

template<class T>
Result<T> Copy(const Result<T>& src) {
  return src;
}

template<class T>
struct IsNonConstResultRvalue : std::false_type {};

template<class T>
struct IsNonConstResultRvalue<Result<T>&&> : std::true_type {};

#define RESULT_CHECKER_HELPER(expr, checker) \
  __extension__ ({ \
    auto&& __result = (expr); \
    static_assert(yb::IsNonConstResultRvalue<decltype(__result)>::value, \
                  "only non-const Result<T> rvalue reference is allowed"); \
    checker; \
    WrapMove(std::move(__result)); })

// Returns if result is not ok, extracts result value in case of success.
#define VERIFY_RESULT(expr) \
  RESULT_CHECKER_HELPER(expr, RETURN_NOT_OK(__result))

// Returns if result is not ok, extracts result value in case of success.
#define VERIFY_RESULT_OR_SET_CODE(expr, code) \
  RESULT_CHECKER_HELPER(expr, RETURN_NOT_OK_SET_CODE(__result, code))

// Helper version of VERIFY_RESULT which returns reference instead of std::reference_wrapper.
#define VERIFY_RESULT_REF(expr) \
  VERIFY_RESULT(expr).get()

// If expr's result is not ok, returns the error status prepended with provided message.
// If expr's result is ok returns wrapped value.
#define VERIFY_RESULT_PREPEND(expr, message) \
  RESULT_CHECKER_HELPER(expr, RETURN_NOT_OK_PREPEND(__result, message))

// If expr's result is not ok, returns the error status prepended with current the caller name.
// If expr's result is ok returns wrapped value.
// This macro helps to identify the exact place of failure for the widely used expression.
#define VERIFY_RESULT_PREPEND_FUNC(expr) \
  VERIFY_RESULT_PREPEND(expr, __func__)

template<class T>
T&& OptionalWrapMove(Result<T>&& result) {
  return std::move(*result);
}

template<class T>
T&& OptionalWrapMove(T&& result) {
  return std::move(result);
}

template<class T>
bool OptionalResultIsOk(const Result<T>& result) {
  return result.ok();
}

template<class T>
constexpr bool OptionalResultIsOk(const T& result) {
  return true;
}

template<class TValue>
Status&& OptionalMoveStatus(Result<TValue>&& result) {
  return std::move(result.status());
}

template<class TValue>
Status OptionalMoveStatus(TValue&& value) {
  return Status::OK();
}

template <typename T>
std::string AsDebugHexString(const Result<T>& value) {
  if (!value.ok()) {
    return value.status().ToString();
  }
  return AsDebugHexString(value.get());
}

// When expr type is Result, then it works as VERIFY_RESULT. Otherwise, just returns expr.
// Could be used in templates to work with functions that returns Result in some instantiations,
// and plain value in others.
#define OPTIONAL_VERIFY_RESULT(expr) \
  __extension__ ({ \
    auto&& __result = (expr); \
    if (!::yb::OptionalResultIsOk(__result)) { \
      return ::yb::OptionalMoveStatus(std::move(__result)); \
    } \
    ::yb::OptionalWrapMove(std::move(__result)); })

} // namespace yb
