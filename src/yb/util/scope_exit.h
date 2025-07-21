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
#include <utility>

#include "yb/gutil/macros.h"

#include "yb/util/concepts.h"

namespace yb {

// Execute the lambda when object goes out of scope.
// The class has the Cancel method to dismiss execution the lambda and release the resources held
// by it immediately.
// The class is useful to perform cleanup due to unexpected exits, such as
// VERIFY_RESULT and RETURN_NOT_OK. Whereas in the success case, the cleanup is not needed.
//
// Ex:
//  Status DoStuff(const Identifier& id) {
//    std::lock_guard l(mutex_);
//    AddToMap(id);
//    auto se = CancelableScopeExit([&id]{ RemoveFromMap(id); });
//
//    RETURN_NOT_OK(Commit(VERIFY_RESULT(DoInternalStuff(id))));
//
//    se.Cancel();
//    return Status::OK();
//  }
template <InvocableAs<void()> F>
class [[nodiscard]] CancelableScopeExit { // NOLINT
 public:
  explicit CancelableScopeExit(const F& f) : f_(f) {}
  explicit CancelableScopeExit(F&& f) : f_(std::move(f)) {}
  explicit CancelableScopeExit(CancelableScopeExit<F>&& rhs) : f_(std::move(rhs.f_)) {
    rhs.f_.reset();
  }

  ~CancelableScopeExit() {
    if (f_) {
      (*f_)();
    }
  }

  void Cancel() { f_.reset(); }

 private:
  std::optional<F> f_;

  DISALLOW_COPY_AND_ASSIGN(CancelableScopeExit);
};

// Execute the lambda when object goes out of scope.
// Unlike CancelableScopeExit execution can't be dismissed (no Cancel method)
template <InvocableAs<void()> F>
class [[nodiscard]] ScopeExit { // NOLINT
 public:
  explicit ScopeExit(const F& f) : impl_(f) {}
  explicit ScopeExit(F&& f) : impl_(std::move(f)) {}
  explicit ScopeExit(ScopeExit<F>&&) = default;

 private:
  CancelableScopeExit<F> impl_;
};

template <InvocableAs<void()> F>
auto MakeOptionalScopeExit(F&& f) {
  return std::optional<ScopeExit<std::decay_t<F>>>{std::forward<F>(f)};
}

} // namespace yb
