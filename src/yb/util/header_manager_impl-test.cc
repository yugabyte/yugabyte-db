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

#include <sys/types.h>
#include <openssl/rand.h>

#include <string>

#include "yb/util/header_manager_impl.h"

#include "yb/util/encryption_util.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"
#include "yb/util/universe_key_manager.h"
#include "yb/util/random_util.h"

#include "yb/gutil/endian.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace yb {
namespace enterprise {

class TestHeaderManagerImpl : public YBTest {};

std::unique_ptr<UniverseKeyManager> GenerateUniverseKeyManager() {
  auto universe_key_manager = std::make_unique<UniverseKeyManager>();
  UniverseKeyRegistryPB registry;
  auto encryption_params = EncryptionParams::NewEncryptionParams();
  EncryptionParamsPB params_pb;
  encryption_params->ToEncryptionParamsPB(&params_pb);
  auto version_id = RandomHumanReadableString(16);
  (*registry.mutable_universe_keys())[version_id] = params_pb;
  registry.set_encryption_enabled(true);
  registry.set_latest_version_id(version_id);
  universe_key_manager->SetUniverseKeyRegistry(registry);
  return universe_key_manager;
}

TEST_F(TestHeaderManagerImpl, FileOps) {
  auto universe_key_manger = GenerateUniverseKeyManager();
  std::unique_ptr<HeaderManager> header_manager =
      DefaultHeaderManager(universe_key_manger.get());
  auto params = EncryptionParams::NewEncryptionParams();
  string header = ASSERT_RESULT(header_manager->SerializeEncryptionParams(*params.get()));
  auto start_idx = header_manager->GetEncryptionMetadataStartIndex();
  auto status = ASSERT_RESULT(header_manager->GetFileEncryptionStatusFromPrefix(
      Slice(header.c_str(), start_idx)));
  ASSERT_TRUE(status.is_encrypted);
  Slice s = Slice(header.c_str() + start_idx, status.header_size);
  auto new_params = ASSERT_RESULT(header_manager->DecodeEncryptionParamsFromEncryptionMetadata(s));
  ASSERT_TRUE(params->Equals(*new_params.get()));
}

} // namespace enterprise
} // namespace yb
