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

#include "yb/util/one_time_bool.h"

namespace yb {

bool OneTimeBool::Set() {
  bool expected = false;
  return value_.compare_exchange_strong(expected, true);
}

OneTimeBool::operator bool() const {
  return value_.load();
}

}  // namespace yb
