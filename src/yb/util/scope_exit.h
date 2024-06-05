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
#include <optional>
#include <utility>

#include "yb/gutil/macros.h"
#include "yb/util/status_fwd.h"

namespace yb {

template <class F>
class NODISCARD_CLASS ScopeExitLambda {
 public:
  ScopeExitLambda(ScopeExitLambda&& rhs) : f_(std::move(rhs.f_)) { rhs.Cancel(); }

  explicit ScopeExitLambda(const F& f) : f_(f) {}
  explicit ScopeExitLambda(F&& f) : f_(std::move(f)) {}

  ~ScopeExitLambda() {
    if (f_) {
      (*f_)();
    }
  }

  // Cancel the lambda and release the resources held by it.
  // This is useful when ScopeExit is used to perform cleanup due to unexpected exits, such as
  // VERIFY_RESULT and RETURN_NOT_OK. Whereas in the success case, the cleanup is not needed.
  //
  // Ex:
  //  Status DoStuff(const Identifier& id) {
  //    std::lock_guard l(mutex_);
  //    AddToMap(id);
  //    auto se = CancellableScopeExit([&id](){RemoveFromMap(id);});
  //
  //    auto stuff = VERIFY_RESULT(DoInternalStuff(id));
  //    RETURN_NOT_OK(Commit(stuff));
  //
  //    se.Cancel();
  //    return Status::OK();
  //  }
  void Cancel() { f_ = std::nullopt; }

 private:
  std::optional<F> f_;

  DISALLOW_COPY_AND_ASSIGN(ScopeExitLambda);
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
