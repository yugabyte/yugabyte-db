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

#ifndef YB_ENCRYPTION_ENCRYPTED_FILE_FACTORY_H
#define YB_ENCRYPTION_ENCRYPTED_FILE_FACTORY_H

#include <memory>

#include "yb/encryption/encryption_fwd.h"

namespace yb {

class Env;

namespace encryption {

std::unique_ptr<yb::Env> NewEncryptedEnv(std::unique_ptr<HeaderManager> header_manager);

} // namespace encryption
} // namespace yb

#endif // YB_ENCRYPTION_ENCRYPTED_FILE_FACTORY_H
