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

#include <string>
#include "yb/util/status.h"

namespace yb {

// Async operations have 3 outcomes:
// 1. Not Done: Operation still in progress
// 2. Done failed: Operation completed unsuccessfully.
// 3. Done succeeded: Operation completed successfully.
// This can be wrapped by Result to indicate when the outcome could not be determined due to other
// errors.
// Ex:
//   Result<IsOperationDoneResult> IsCreateTableDone();
class IsOperationDoneResult {
 public:
  static IsOperationDoneResult NotDone() { return IsOperationDoneResult(false, Status::OK()); }

  static IsOperationDoneResult Done(Status status = {}) {
    return IsOperationDoneResult(true, std::move(status));
  }

  // Has the operation completed?
  bool done() const { return done_; }
  // The status of a completed operation.
  const Status& status() { return status_; }

  // Operation completed and it was successful.
  explicit operator bool() const { return done_ && status_.ok(); }

  std::string ToString() const { return YB_STRUCT_TO_STRING(done_, status_); }

 private:
  IsOperationDoneResult(bool done, Status status) : done_(done), status_(std::move(status)) {}

  bool done_;
  Status status_;
};

inline std::ostream& operator<<(std::ostream& out, const IsOperationDoneResult& result) {
  return out << result.ToString();
}

}  // namespace yb
