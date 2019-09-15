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

#include "yb/rocksdb/util/timeout_error.h"

using namespace std::literals;

namespace rocksdb {

namespace {

const std::string kTimeoutMsgs[] = {
    "Timeout Acquiring Mutex"s,                           // kMutexTimeout
    "Timeout waiting to lock key"s,                       // kLockTimeout
    "Failed to acquire lock due to max_num_locks limit"s  // kLockLimit
};

static_assert(arraysize(kTimeoutMsgs) == kElementsInTimeoutCode,
              "Wrong number of timeout messages");

} // namespace

const std::string& TimeoutErrorTag::ToMessage(Value value) {
  return kTimeoutMsgs[yb::to_underlying(value)];
}

static const std::string kTimeoutErrorCategoryName = "timeout";

static yb::StatusCategoryRegisterer timeout_error_category_registerer(
    yb::StatusCategoryDescription::Make<TimeoutErrorTag>(&kTimeoutErrorCategoryName));

} // namespace rocksdb
