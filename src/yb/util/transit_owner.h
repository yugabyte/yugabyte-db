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
#include <utility>

#include "yb/gutil/macros.h"

#include "yb/util/debug-util.h"
#include "yb/util/logging.h"

namespace yb {

// Helper that makes conditional ownership transfer explicit and checkable.
//
// Usage pattern:
//
//   bool DoFunc(TransitOwner<Obj>& o);
//
//   void func(Obj&& o) {
//     TransitOwner<Obj> owner{std::move(o)};
//     if (DoFunc(owner)) {
//       DCHECK(!owner.Owns())
//           << "DoFunc is expected to take ownership, object is not accessible anymore";
//     } else {
//       DCHECK(owner.Owns()) << "Ownership expected to be preserved, object is accessible";
//     }
//   }
template <class T>
class TransitOwner {
 public:
  explicit TransitOwner(T&& t) : holder_(std::move(t)) {}

  template <typename Self>
  [[nodiscard]] auto* operator->(this Self& self) {
    return self.ValuePtr();
  }

  template <typename Self>
  [[nodiscard]] auto& operator*(this Self& self) {
    return *self.ValuePtr();
  }

  [[nodiscard]] T Release() {
    T result{std::move(*ValuePtr())};
    holder_.reset();
    return result;
  }

  [[nodiscard]] constexpr bool Owns() const {
    return holder_.has_value();
  }

 private:
  template <typename Self>
  [[nodiscard]] auto ValuePtr(this Self& self) -> decltype(self.holder_.operator->()) {
    if (PREDICT_TRUE(self.Owns())) {
      return self.holder_.operator->();
    }
    LOG(DFATAL) << "Not the owner anymore" << GetStackTrace();
    return nullptr;
  }

  std::optional<T> holder_;

  DISALLOW_COPY_AND_ASSIGN(TransitOwner);
};

} // namespace yb
