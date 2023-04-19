//
// Copyright (c) Yugabyte, Inc.
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
//

#pragma once

#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"

namespace yb {

struct CAPABILITY("thread role") ThreadRole {};

template<class T>
class [[nodiscard]] SCOPED_CAPABILITY CapabilityGuard {  // NOLINT
 public:
  CapabilityGuard() ACQUIRE(T::Alias()) {};
  ~CapabilityGuard() RELEASE(T::Alias()) {};

 private:
  DISALLOW_COPY_AND_ASSIGN(CapabilityGuard);
};

} // namespace yb
