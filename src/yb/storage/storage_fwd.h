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

#include "yb/util/clone_ptr.h"

namespace yb::storage {

class UserFrontier;
class UserFrontiers;

// Frontier should be copyable, but should still preserve its polymorphic nature. We cannot use
// shared_ptr here, because we are planning to modify the copied value. If we used shared_ptr and
// modified the copied value, the original value would also change.
using UserFrontierPtr = yb::clone_ptr<UserFrontier>;
using UserFrontiersPtr = std::unique_ptr<UserFrontiers>;

} // namespace yb::storage
