//
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
//
#pragma once

#include "yb/rocksdb/status.h"

namespace rocksdb {

inline constexpr const char kBackgroundErrorRestartHint[] =
    "Requires a process restart to clear the error from cache.";

class BackgroundError {
 public:
  BackgroundError() = default;

  bool ok() const { return status_.ok(); }

  operator Status() const { return status_; }

  bool TrySet(const Status& err) {
    if (!status_.ok() || err.ok()) {
      return false;
    }
    status_ = err.CloneAndAppend(kBackgroundErrorRestartHint);
    return true;
  }

 private:
  Status status_;
};

}  // namespace rocksdb
