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

#ifndef ENT_SRC_YB_UTIL_HEADER_MANAGER_H
#define ENT_SRC_YB_UTIL_HEADER_MANAGER_H

#include "yb/util/status.h"
#include "yb/util/result.h"

namespace yb {
namespace enterprise {

struct EncryptionParams;
struct FileEncryptionStatus;

// Class used by FileFactory to construct header for encrypted files. The header format looks like:
// magic encryption string
// header size
// EncryptionHeaderPB
class HeaderManager {
 public:
  virtual ~HeaderManager() {}

  virtual Result<std::unique_ptr<EncryptionParams>> DecodeEncryptionParamsFromEncryptionMetadata(
      const Slice& s) = 0;
  virtual Result<std::string> SerializeEncryptionParams(
      const EncryptionParams& encryption_info) = 0;
  virtual uint32_t GetEncryptionMetadataStartIndex() = 0;
  virtual Result<FileEncryptionStatus> GetFileEncryptionStatusFromPrefix(const Slice& s) = 0;
};

} // namespace enterprise
} // namespace yb

#endif // ENT_SRC_YB_UTIL_HEADER_MANAGER_H
