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

#include "yb/util/status.h"

namespace yb {

template<class TValue>
class Result {
 public:
  Result(const Result& rhs) : success_(rhs.success_) {
    if (success_) {
      new (&value_) TValue(rhs.value_);
    } else {
      new (&status_) Status(rhs.status_);
    }
  }

  Result(Result&& rhs) : success_(rhs.success_) {
    if (success_) {
      new (&value_) TValue(std::move(rhs.value_));
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

  Result(const TValue& value) : success_(true), value_(value) {} // NOLINT
  Result(TValue&& value) : success_(true), value_(std::move(value)) {} // NOLINT

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

  Result& operator=(TValue&& value) {
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

  const TValue& operator*() const& {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return value_;
  }

  TValue& operator*() & {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return value_;
  }

  TValue&& operator*() && {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return value_;
  }

  const TValue* operator->() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return &value_;
  }

  TValue* operator->() {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return &value_;
  }

  const TValue* get_ptr() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return &value_;
  }

  TValue* get_ptr() {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return &value_;
  }

  ~Result() {
    if (success_) {
      value_.~TValue();
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
    TValue value_;
  };
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
  return result.ok() ? out << *result : out << result.status().ToString();
}

} // namespace yb

#endif // YB_UTIL_RESULT_H
