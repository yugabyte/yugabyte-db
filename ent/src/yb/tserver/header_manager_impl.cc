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

#include "yb/tserver/header_manager_impl.h"

#include "yb/util/env.h"
#include "yb/util/encryption_util.h"
#include "yb/util/memory/memory.h"
#include "yb/util/encryption_header.pb.h"
#include "yb/util/header_manager.h"
#include "yb/util/pb_util.h"

#include "yb/gutil/endian.h"

static const string kEncryptionMagic = "encrypt!";

namespace yb {
namespace tserver {
namespace enterprise {

class HeaderManagerImpl : public yb::enterprise::HeaderManager {
  Result<string> SerializeEncryptionParams(
      const yb::enterprise::EncryptionParams& encryption_info) override {

    EncryptionHeaderPB encryption_header;
    encryption_info.ToEncryptionHeaderPB(&encryption_header);

    string encryption_header_str;
    encryption_header.SerializeToString(&encryption_header_str);
    char header_size[sizeof(uint32_t)];
    BigEndian::Store32(&header_size, encryption_header_str.size());

    return kEncryptionMagic + string(header_size, sizeof(header_size)) +
        encryption_header_str;
  }

  Result<std::unique_ptr<yb::enterprise::EncryptionParams>>
  DecodeEncryptionParamsFromEncryptionMetadata(const Slice& s) override {
    auto encryption_params = std::make_unique<yb::enterprise::EncryptionParams>();
    EncryptionHeaderPB encryption_header;
    RETURN_NOT_OK(pb_util::ParseFromArray(&encryption_header, s.data(), s.size()));
    encryption_header.ParseFromString(s.ToBuffer());
    encryption_params->FromEncryptionHeaderPB(encryption_header);
    return encryption_params;
  }

  uint32_t GetEncryptionMetadataStartIndex() override {
    return kEncryptionMagic.size() + sizeof(uint32_t);
  }

  Result<yb::enterprise::FileEncryptionStatus> GetFileEncryptionStatusFromPrefix(
      const Slice& s) override {
    yb::enterprise::FileEncryptionStatus status;
    status.is_encrypted = s.compare_prefix(Slice(kEncryptionMagic)) == 0;
    if (status.is_encrypted) {
      status.header_size = BigEndian::Load32(s.data() + kEncryptionMagic.size());
    }
    return status;
  }
};

std::unique_ptr<yb::enterprise::HeaderManager> DefaultHeaderManager() {
  return std::make_unique<HeaderManagerImpl>();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
