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

#include "yb/tools/pbc_tools_lib.h"

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/util/status.h"

namespace yb::pb_util {

Result<PBToolEnv> PBToolEnv::Create(const std::string& key_id, const std::string& key_file_path) {
  PBToolEnv tool_env;
  if (key_id.empty() || key_file_path.empty()) {
    tool_env.env_ = Env::Default();
    return tool_env;
  }
  faststring key_file_content;
  RETURN_NOT_OK_PREPEND(
      ReadFileToString(Env::Default(), key_file_path, &key_file_content),
      Format("Could not read key file at path $0", key_file_path));
  auto res = encryption::UniverseKeyManager::FromKey(key_id, yb::Slice(key_file_content));
  if (!res.ok()) {
    RETURN_NOT_OK_PREPEND(res, "Could not create universe key manager")
  }
  tool_env.key_manager_ = std::move(*res);
  tool_env.env_ = yb::encryption::NewEncryptedEnv(
      yb::encryption::DefaultHeaderManager(tool_env.key_manager_.get()));
  return tool_env;
}

Env& PBToolEnv::env() {
  if (auto** env = std::get_if<Env*>(&env_)) {
    return **env;
  } else if (auto* unique_env = std::get_if<std::unique_ptr<Env>>(&env_)) {
    return **unique_env;
  }
  CHECK(false) << "env_ is not initialized.";
}

}  // namespace yb::pb_util
