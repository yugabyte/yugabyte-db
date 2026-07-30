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

#include "yb/gutil/macros.h"
#include "ybgate/ybgate_api.h"

namespace yb {

class ScopedSetMemoryContext {
 public:
  explicit ScopedSetMemoryContext(YbgMemoryContext ctx_to_set) {
    ctx_to_restore_ = YbgSetCurrentMemoryContext(ctx_to_set);
  }

  ~ScopedSetMemoryContext() {
    YbgSetCurrentMemoryContext(ctx_to_restore_);
  }

 private:
  YbgMemoryContext ctx_to_restore_;

  DISALLOW_COPY_AND_ASSIGN(ScopedSetMemoryContext);
};

} // namespace yb
