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

#ifndef YB_UTIL_RESULT_H
#define YB_UTIL_RESULT_H

#include <type_traits>

#include "yb/util/status.h"

namespace yb {

template<class TValue>
struct ResultTraits {
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
  typedef TValue* Stored;
  typedef const TValue* ConstPointer;
  typedef TValue* Pointer;
  typedef const TValue& ConstReference;
  typedef Pointer&& RValueReference;

  static TValue* ToStored(TValue& value) { return &value; } // NOLINT
  static void Destroy(Stored* value) {}
  static TValue* GetPtr(const Stored* value) { return *value; }
};

#ifdef __clang__
#define NODISCARD_CLASS [[nodiscard]]
#else
#define NODISCARD_CLASS
#endif

template<class TValue>
class NODISCARD_CLASS Result {
 public:
  typedef ResultTraits<TValue> Traits;

  Result(const Result& rhs) : success_(rhs.success_) {
    if (success_) {
      new (&value_) typename Traits::Stored(rhs.value_);
    } else {
      new (&status_) Status(rhs.status_);
    }
  }

  template<class UValue,
           typename = typename std::enable_if<std::is_convertible<UValue, TValue>::value>::type>
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

  Result(const Status& status) : success_(false), status_(status) { // NOLINT
    CHECK(!status_.ok());
  }

  Result(Status&& status) : success_(false), status_(std::move(status)) { // NOLINT
    CHECK(!status_.ok());
  }

  Result(const TValue& value) : success_(true), value_(Traits::ToStored(value)) {} // NOLINT

  template <class UValue,
            typename = typename std::enable_if<
                std::is_convertible<const UValue&, const TValue&>::value>::type>
  Result(const UValue& value) // NOLINT
      : success_(true), value_(Traits::ToStored(value)) {}

  Result(typename Traits::RValueReference value) // NOLINT
      : success_(true), value_(std::move(value)) {}

  template <class UValue,
            typename = typename std::enable_if<
                std::is_convertible<UValue&&, TValue&&>::value>::type>
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

  template<class UValue,
           typename = typename std::enable_if<std::is_convertible<UValue, TValue>::value>::type>
  Result& operator=(const Result<UValue>& rhs) {
    this->~Result();
    return *new (this) Result(rhs);
  }

  Result& operator=(const Status& status) {
    CHECK(!status.ok());
    this->~Result();
    return *new (this) Result(status);
  }

  Result& operator=(Status&& status) {
    CHECK(!status.ok());
    this->~Result();
    return *new (this) Result(std::move(status));
  }

  Result& operator=(const TValue& value) {
    this->~Result();
    return *new (this) Result(value);
  }

  template <class UValue,
            typename = typename std::enable_if<
                std::is_convertible<UValue&&, TValue&&>::value>::type>
  Result& operator=(UValue&& value) {
    this->~Result();
    return *new (this) Result(std::move(value));
  }

  explicit operator bool() const {
    return ok();
  }

  bool operator!() const {
    return !ok();
  }

  bool ok() const {
#ifndef NDEBUG
    success_checked_ = true;
#endif
    return success_;
  }

  const Status& status() const& {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(!success_);
    return status_;
  }

  Status& status() & {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(!success_);
    return status_;
  }

  Status&& status() && {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(!success_);
    return status_;
  }

  auto& operator*() const& { return *get_ptr(); }
  auto& operator*() & { return *get_ptr(); }

  TValue&& operator*() && {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return value_;
  }

  auto operator->() const { return get_ptr(); }
  auto operator->() { return get_ptr(); }

  auto get_ptr() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return Traits::GetPtr(&value_);
  }

  auto get_ptr() {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return Traits::GetPtr(&value_);
  }

  CHECKED_STATUS MoveTo(typename Traits::Pointer value) {
    if (!ok()) {
      return status();
    }
    *value = std::move(**this);
    return Status::OK();
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
class Result<bool> {
 public:
  Result(const Status& status) : state_(State::kFailed), status_(status) {} // NOLINT
  Result(Status&& status) : state_(State::kFailed), status_(std::move(status)) {} // NOLINT
  Result(bool value) : state_(value ? State::kTrue : State::kFalse) {} // NOLINT

  bool ok() const {
#ifndef NDEBUG
    success_checked_ = true;
#endif
    return state_ != State::kFailed;
  }

  const Status& status() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(state_ == State::kFailed);
    return status_;
  }

  Status& status() {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(state_ == State::kFailed);
    return status_;
  }

  bool get() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(state_ != State::kFailed);
    return state_ != State::kFalse;
  }

  bool operator*() const { return get(); }

 private:
  enum class State : uint8_t {
    kFalse,
    kTrue,
    kFailed,
  };

  State state_;
#ifndef NDEBUG
  mutable bool success_checked_ = false;
#endif
  Status status_; // Don't use Result, because default Status is cheap.
};

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

inline std::ostream& operator<<(std::ostream& out, const Result<bool>& result) {
  return result.ok() ? out << result.get() : out << result.status();
}

template <class Functor>
class ResultToStatusAdaptor {
 public:
  explicit ResultToStatusAdaptor(const Functor& functor) : functor_(functor) {}

  template <class Output, class... Args>
  CHECKED_STATUS operator()(Output* output, Args&&... args) {
    auto result = functor_(std::forward<Args>(args)...);
    RETURN_NOT_OK(result);
    *output = std::move(*result);
    return Status::OK();
  }
 private:
  Functor functor_;
};

template <class Functor>
ResultToStatusAdaptor<Functor> ResultToStatus(const Functor& functor) {
  return ResultToStatusAdaptor<Functor>(functor);
}

template<class TValue>
CHECKED_STATUS ResultToStatus(const Result<TValue>& result) {
  return result.ok() ? Status::OK() : result.status();
}

// Checks that result is ok, extracts result value is case of success.
#define CHECK_RESULT(expr) \
  __extension__ ({ auto&& __result = (expr); CHECK_OK(__result); std::move(*__result); })

// Returns if result is not ok, extracts result value is case of success.
#define VERIFY_RESULT(expr) \
  __extension__ ({ auto&& __result = (expr); RETURN_NOT_OK(__result); std::move(*__result); })

// Returns if result is not ok, prepending status with provided message,
// extracts result value is case of success.
#define VERIFY_RESULT_PREPEND(expr, message) \
  __extension__ ({ \
    auto&& __result = (expr); RETURN_NOT_OK_PREPEND(__result, message); std::move(*__result); })

// Asserts that result is ok, extracts result value is case of success.
#define ASSERT_RESULT(expr) \
  __extension__ ({ auto&& __result = (expr); ASSERT_OK(__result); std::move(*__result); })

// Asserts that result is ok, extracts result value is case of success.
#define EXPECT_RESULT(expr) \
  __extension__ ({ auto&& __result = (expr); EXPECT_OK(__result); std::move(*__result); })

// Asserts that result is ok, extracts result value is case of success.
#define ASSERT_RESULT_FAST(expr) \
  __extension__ ({ auto&& __result = (expr); ASSERT_OK_FAST(__result); std::move(*__result); })

} // namespace yb

#endif // YB_UTIL_RESULT_H
