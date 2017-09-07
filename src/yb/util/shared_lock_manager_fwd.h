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

#ifndef YB_UTIL_SHARED_LOCK_MANAGER_FWD_H_
#define YB_UTIL_SHARED_LOCK_MANAGER_FWD_H_

namespace yb {
namespace util {

const size_t NUM_LOCK_TYPES = 6;
enum class LockType {
  SR_READ_WEAK = 0,
  SR_READ_STRONG = 1,
  SR_WRITE_WEAK = 2,
  SR_WRITE_STRONG = 3,
  SI_WRITE_WEAK = 4,
  SI_WRITE_STRONG = 5
};

typedef std::map<std::string, LockType> LockBatch;
typedef uint32_t LockState;

class SharedLockManager;

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_SHARED_LOCK_MANAGER_FWD_H_
