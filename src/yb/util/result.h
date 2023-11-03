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

#include <string>
#include <type_traits>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/status.h"
#include "yb/util/tostring.h"

namespace yb {

template<class TValue>
struct ResultTraits {
  typedef TValue ValueType;
  typedef TValue Stored;
  typedef const TValue* ConstPointer;
  typedef TValue* Pointer;
  typedef const TValue& ConstReference;
  typedef TValue&& RValueReference;

  static const TValue& ToStored(const TValue& value) { return value; }
  static void Destroy(Stored* value) { value->~TValue(); }
  static Stored* GetPtr(Stored* value) { return value; }
  static const Stored* GetPtr(const Stored* value) { return value; }
};

template<class TValue>
struct ResultTraits<TValue&> {
  typedef TValue& ValueType;
  typedef TValue* Stored;
  typedef const TValue* ConstPointer;
  typedef TValue* Pointer;
  typedef const TValue& ConstReference;
  typedef Pointer&& RValueReference;

  static TValue* ToStored(TValue& value) { return &value; } // NOLINT
  static void Destroy(Stored* value) {}
  static TValue* GetPtr(const Stored* value) { return *value; }
};

void StatusCheck(bool);

template<class TValue>
class NODISCARD_CLASS Result {
 public:
  using Traits = ResultTraits<TValue>;
  using ValueType = typename Traits::ValueType;

  Result(const Result& rhs) : success_(rhs.success_) {
    if (success_) {
      new (&value_) typename Traits::Stored(rhs.value_);
    } else {
      new (&status_) Status(rhs.status_);
    }
  }

  template<class UValue, class = std::enable_if_t<std::is_convertible<UValue, TValue>::value>>
  Result(const Result<UValue>& rhs) : success_(rhs.success_) {
    if (success_) {
      new (&value_) typename Traits::Stored(rhs.value_);
    } else {
      new (&status_) Status(rhs.status_);
    }
  }

  Result(Result&& rhs) : success_(rhs.success_) {
    if (success_) {
      new (&value_) typename Traits::Stored(std::move(rhs.value_));
    } else {
      new (&status_) Status(std::move(rhs.status_));
    }
  }

  // Forbid creation from Status::OK as value must be explicitly specified in case status is OK
  Result(const Status::OK&) = delete; // NOLINT
  Result(Status::OK&&) = delete; // NOLINT

  Result(const Status& status) : success_(false), status_(status) { // NOLINT
    StatusCheck(!status_.ok());
  }

  Result(Status&& status) : success_(false), status_(std::move(status)) { // NOLINT
    StatusCheck(!status_.ok());
  }

  Result(const TValue& value) : success_(true), value_(Traits::ToStored(value)) {} // NOLINT

  template <class UValue,
            class = std::enable_if_t<std::is_convertible<const UValue&, const TValue&>::value>>
  Result(const UValue& value) // NOLINT
      : success_(true), value_(Traits::ToStored(value)) {}

  Result(typename Traits::RValueReference value) // NOLINT
      : success_(true), value_(std::move(value)) {}

  template <class UValue, class = std::enable_if_t<std::is_convertible<UValue&&, TValue&&>::value>>
  Result(UValue&& value) : success_(true), value_(std::move(value)) {} // NOLINT

  Result& operator=(const Result& rhs) {
    if (&rhs == this) {
      return *this;
    }
    this->~Result();
    return *new (this) Result(rhs);
  }

  Result& operator=(Result&& rhs) {
    if (&rhs == this) {
      return *this;
    }
    this->~Result();
    return *new (this) Result(std::move(rhs));
  }

  template<class UValue, class = std::enable_if_t<std::is_convertible<UValue, TValue>::value>>
  Result& operator=(const Result<UValue>& rhs) {
    this->~Result();
    return *new (this) Result(rhs);
  }

  Result& operator=(const Status& status) {
    StatusCheck(!status.ok());
    this->~Result();
    return *new (this) Result(status);
  }

  Result& operator=(Status&& status) {
    StatusCheck(!status.ok());
    this->~Result();
    return *new (this) Result(std::move(status));
  }

  Result& operator=(const TValue& value) {
    this->~Result();
    return *new (this) Result(value);
  }

  template <class UValue, class = std::enable_if_t<std::is_convertible<UValue&&, TValue&&>::value>>
  Result& operator=(UValue&& value) {
    this->~Result();
    return *new (this) Result(std::move(value));
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
  bool success_;
#ifndef NDEBUG
  mutable bool success_checked_ = false;
#endif
  union {
    Status status_;
    typename Traits::Stored value_;
  };

  template <class UValue> friend class Result;
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

  template <class Output, class... Args>
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
