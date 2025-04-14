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

#include <optional>

namespace yb {

template<class T>
class ObjectProvider {
 public:
  explicit ObjectProvider(T* value) :
      value_(value) {
    if (!value_) {
      holder_.emplace();
      value_ = &*holder_;
    }
  }

  T* operator->() const { return get(); }

  T& operator*() const { return *get(); }

  T* get() const { return value_; }

  bool is_owner() const { return holder_.has_value(); }

 private:
  T* value_;
  std::optional<T> holder_;
};

} // namespace yb
