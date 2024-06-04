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

#pragma once

#include <type_traits>

namespace yb {

// Wrapper for a factory of some type T, which is only invoked once the wrappers operator<T>() is
// invoked, i.e. when the factory is cast to type T.
template <class Factory>
class LazyFactory {
 public:
  explicit LazyFactory(const Factory& factory) : factory_(factory) {}

  constexpr operator std::invoke_result_t<const Factory&>() const {
    return factory_();
  }

 private:
  Factory factory_;
};

template <class Factory>
auto MakeLazyFactory(const Factory& factory) {
  return LazyFactory<Factory>(factory);
}

}  // namespace yb
