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

#include <type_traits>
#include <utility>

#include "yb/util/status_fwd.h"

namespace yb {

template <class F>
class NODISCARD_CLASS ScopeExitLambda {
 public:
  ScopeExitLambda(const ScopeExitLambda&) = delete;
  void operator=(const ScopeExitLambda&) = delete;

  ScopeExitLambda(ScopeExitLambda&& rhs) : f_(std::move(rhs.f_)), moved_(false) {
    rhs.moved_ = true;
  }

  explicit ScopeExitLambda(const F& f) : f_(f), moved_(false) {}
  explicit ScopeExitLambda(F&& f) : f_(std::move(f)), moved_(false) {}

  ~ScopeExitLambda() {
    if (!moved_) {
      f_();
    }
  }
 private:
  F f_;
  bool moved_;
};

template <class F>
ScopeExitLambda<F> ScopeExit(const F& f) {
  return ScopeExitLambda<F>(f);
}

template <class F>
ScopeExitLambda<typename std::remove_reference<F>::type> ScopeExit(F&& f) {
  return ScopeExitLambda<typename std::remove_reference<F>::type>(std::forward<F>(f));
}

} // namespace yb
