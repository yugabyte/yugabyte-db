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

#include <string>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/encryption/encryption_util.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/gutil/endian.h"

#include "yb/tserver/universe_key_test_util.h"

#include "yb/util/status.h"
#include "yb/util/test_util.h"

using std::string;

using yb::tserver::GenerateTestUniverseKeyManager;

namespace yb {

class TestHeaderManagerImpl : public YBTest {};

TEST_F(TestHeaderManagerImpl, FileOps) {
  auto universe_key_manger = GenerateTestUniverseKeyManager();
  auto header_manager =
      DefaultHeaderManager(universe_key_manger.get());
  auto params = encryption::EncryptionParams::NewEncryptionParams();
  string header = ASSERT_RESULT(header_manager->SerializeEncryptionParams(*params.get()));
  auto start_idx = header_manager->GetEncryptionMetadataStartIndex();
  auto status = ASSERT_RESULT(header_manager->GetFileEncryptionStatusFromPrefix(
      Slice(header.c_str(), start_idx)));
  ASSERT_TRUE(status.is_encrypted);
  Slice s = Slice(header.c_str() + start_idx, status.header_size);
  auto new_params = ASSERT_RESULT(header_manager->DecodeEncryptionParamsFromEncryptionMetadata(s));
  ASSERT_TRUE(params->Equals(*new_params.get()));
}

} // namespace yb
