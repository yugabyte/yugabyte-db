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

#pragma once

#include <memory>

namespace yb {

// A utility class that owns a cloneable pointer.
//
// This class uniquely owns a single object, so this object is destroyed with the clone_ptr
// instance.  During copying of a clone_ptr it clones the owned object, which should provide a Clone
// function.  This can be used to implement value types that are accessible via an abstract
// interface.
template <class T>
class clone_ptr {
 public:
  clone_ptr() noexcept : t_(nullptr) {}
  clone_ptr(T* t) noexcept : t_(t) {} // NOLINT

  clone_ptr(const clone_ptr& rhs) : t_(rhs.t_ ? rhs.t_->Clone().release() : nullptr) {}

  clone_ptr(clone_ptr&& rhs) noexcept : t_(rhs.release()) {}

  void operator=(const clone_ptr& rhs) noexcept {
    reset();
    if (rhs.t_) {
      t_ = rhs.t_->Clone().release();
    }
  }

  void operator=(clone_ptr&& rhs) noexcept {
    reset();
    t_ = rhs.release();
  }

  template <class U>
  clone_ptr(const clone_ptr<U>& rhs) : t_(rhs.t_ ? rhs.t_->Clone().release() : nullptr) {}

  template <class U>
  clone_ptr(clone_ptr<U>&& rhs) noexcept : t_(rhs.release()) {}

  template <class U>
  clone_ptr(std::unique_ptr<U>&& rhs) noexcept : t_(rhs.release()) { } // NOLINT

  template <class U>
  void operator=(const clone_ptr<U>& rhs) {
    reset();
    if (rhs.t_) {
      t_ = rhs.t_->Clone();
    }
  }

  template <class U>
  void operator=(clone_ptr<U>&& rhs) noexcept {
    reset();
    t_ = rhs.release();
  }

  ~clone_ptr() {
    reset();
  }

  void reset(T* t = nullptr) {
    delete t_;
    t_ = t;
  }

  T* release() {
    T* result = t_;
    t_ = nullptr;
    return result;
  }

  T* get() const {
    return t_;
  }

  T* operator->() const {
    return get();
  }

  T& operator*() const {
    return *get();
  }

  explicit operator bool() const {
    return t_ != nullptr;
  }

 private:
  T* t_;
};

} // namespace yb
