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

namespace yb {

template <typename T>
class ObjectProvider {
 public:
  // Creates an empty ObjectProvider in an empty state (with no value).
  ObjectProvider() = default;

  explicit ObjectProvider(T* value) {
    reset(value);
  }

  // Resets the ObjectProvider to a owned state if value is nullptr.
  // Otherwise, resets to a borrowed state if the value is not equal to the current value.
  void reset(T* value = nullptr) {
    // Owned state, re-initializing the holder.
    if (!value) {
      holder_.emplace();
      value_ = &*holder_;
      return;
    }

    // No need to do anything if the same value is being borrowed or owned. This check should
    // be done after the owned state check to be able to move from the empty state.
    if (value_ == value) {
      return;
    }

    // Borrowed state.
    holder_.reset();
    value_ = value;
  }


  T* operator->() const { return get(); }

  T& operator*() const { return *get(); }

  T* get() const { return value_; }

  bool has_value() const { return value_ != nullptr; }

  bool is_owner() const { return holder_.has_value(); }

 private:
  T* value_ = nullptr;
  std::optional<T> holder_;
};

} // namespace yb
