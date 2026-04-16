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

#include <functional>
#include <memory>
#include <optional>

// Helper utilities for working with reference wrappers.
//
// Many STL containers cannot hold basic references. The std::reference_wrapper type overcomes this
// limitation, behaving like a reference and supporting STL containers.

namespace yb {

template <typename T>
using ConstRefWrap = std::reference_wrapper<const T>;

template <typename T>
std::optional<ConstRefWrap<T>> opt_cref(const std::shared_ptr<T>& ptr) {
  if (ptr) {
    return std::optional(std::cref(*ptr));
  }
  return std::nullopt;
}

}  // namespace yb
