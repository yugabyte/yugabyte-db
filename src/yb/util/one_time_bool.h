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

#include <atomic>

namespace yb {

// A thread-safe bool that starts as false and can be set to true exactly once.
class OneTimeBool {
 public:
  OneTimeBool() = default;
  // Set the value to true.
  // Returns true if this call set the value to true.
  // Otherwise if the value was already true when called, returns false.
  bool Set();

  operator bool() const;
 private:
  std::atomic_bool value_{false};
};

}  // namespace yb
