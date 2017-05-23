//
// Copyright (c) YugaByte, Inc.
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

  const Status& status() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(!success_);
    return status_;
  }

  Status& status() {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(!success_);
    return status_;
  }

  const TValue& operator*() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(success_);
    return value_;
  }

  TValue& operator*() {
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
  Result(const Status& status) : state_(State::FAILED), status_(status) {} // NOLINT
  Result(Status&& status) : state_(State::FAILED), status_(std::move(status)) {} // NOLINT
  Result(bool value) : state_(value ? State::TRUE : State::FALSE) {} // NOLINT

  bool ok() const {
#ifndef NDEBUG
    success_checked_ = true;
#endif
    return state_ != State::FAILED;
  }

  const Status& status() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(state_ == State::FAILED);
    return status_;
  }

  Status& status() {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(state_ == State::FAILED);
    return status_;
  }

  bool get() const {
#ifndef NDEBUG
    CHECK(success_checked_);
#endif
    CHECK(state_ != State::FAILED);
    return state_ != State::FALSE;
  }

 private:
  enum class State : uint8_t {
    FALSE,
    TRUE,
    FAILED
  };

  State state_;
#ifndef NDEBUG
  mutable bool success_checked_ = false;
#endif
  Status status_; // Don't use optional, because default Status is cheap.
};

template<class TValue>
Status&& MoveStatus(Result<TValue>* result) {
  return std::move(result->status());
}

} // namespace yb

#endif // YB_UTIL_RESULT_H
