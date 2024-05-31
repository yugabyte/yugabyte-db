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
#pragma once

#include <memory>
#include <string>

#include "yb/encryption/header_manager.h"

#include "yb/util/status.h"

namespace yb {
namespace encryption {

class HeaderManagerMockImpl : public HeaderManager {
 public:
  HeaderManagerMockImpl();
  void SetFileEncryption(bool file_encrypted);

  Result<std::string> SerializeEncryptionParams(
      const EncryptionParams& encryption_info) override;

  Result<EncryptionParamsPtr> DecodeEncryptionParamsFromEncryptionMetadata(
      const Slice& s, std::string* universe_key_id_output) override;

  uint32_t GetEncryptionMetadataStartIndex() override;
  Result<FileEncryptionStatus> GetFileEncryptionStatusFromPrefix(const Slice& s) override;
  Result<std::string> GetLatestUniverseKeyId() override;
  Result<bool> IsEncryptionEnabled() override;

 private:
  EncryptionParamsPtr encryption_params_;
  bool file_encrypted_ = false;
  const std::string universe_key_id_ = "0";
};

std::unique_ptr<HeaderManager> GetMockHeaderManager();

} // namespace encryption
} // namespace yb
