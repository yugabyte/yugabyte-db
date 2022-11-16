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

namespace yb {

template <class T>
class ValueChanger {
 public:
  ValueChanger() : value_(nullptr) {}

  ValueChanger(T new_value, T* value) {
    Init(new_value, value);
  }

  ValueChanger(const ValueChanger&) = delete;
  void operator=(const ValueChanger&) = delete;

  ~ValueChanger() {
    Reset();
  }

  void Init(T new_value, T* value) {
    value_ = value;
    original_value_ = *value;
    *value = new_value;
  }

  void Reset() {
    if (value_) {
      *value_ = original_value_;
      value_ = nullptr;
    }
  }

 private:
  T* value_;
  T original_value_;
};

} // namespace yb
