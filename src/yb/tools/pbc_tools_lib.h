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

#include <variant>

#include "yb/encryption/universe_key_manager.h"

#include "yb/util/env.h"
#include "yb/util/result.h"

namespace yb {

namespace encryption {

class UniverseKeyManager;

}  // namespace encryption

namespace pb_util {

class PBToolEnv {
 public:
  static Result<PBToolEnv> Create(const std::string& key_id, const std::string& key_file_path);
  Env& env();

 private:
  PBToolEnv() = default;
  std::variant<Env*, std::unique_ptr<Env>> env_;
  std::unique_ptr<encryption::UniverseKeyManager> key_manager_;
};

}  // namespace pb_util
}  // namespace yb
