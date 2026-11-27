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

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "yb/encryption/encryption_util.h"

#include "yb/util/enums.h"
#include "yb/util/status_fwd.h"

namespace yb {

class Env;
class JsonWriter;

namespace encryption {

class HeaderManager;
class UniverseKeyManager;

// Where a file sits in the tserver layout. Snapshot files are hard links to SST files, so their
// bytes are also counted under kData until the live SST file is compacted away.
YB_DEFINE_ENUM(EncryptionKeyReportFileCategory, (kData)(kWal)(kSnapshot)(kMetadata));

// RocksDB MANIFEST files are only replaced when the DB is reopened or the file outgrows
// max_manifest_file_size, so compaction alone does not move them to new keys.
YB_DEFINE_ENUM(EncryptionKeyReportFileType, (kSst)(kManifest)(kWalSegment)(kOther));

// Groups on-disk files by the key sizes protecting them, so an operator can tell how much data is
// still under AES-128 data keys or universe keys.
struct EncryptionKeyReport {
  struct Key {
    EncryptionKeyReportFileCategory category;
    EncryptionKeyReportFileType file_type;
    bool encrypted;
    // Zero for unencrypted and undecodable files.
    uint32_t data_key_bytes;
    UniverseKeyId universe_key_id;
    // Zero for unencrypted and undecodable files.
    uint32_t universe_key_bytes;

    auto operator<=>(const Key&) const = default;
  };

  struct Totals {
    uint64_t files = 0;
    uint64_t bytes = 0;
  };

  std::map<Key, Totals> groups;
  // Files that could not be read, e.g. deleted during the walk.
  uint64_t unreadable_files = 0;
  // Encrypted files whose universe key is not in the registry, so their data key cannot be
  // decrypted. They are grouped under that universe key with zero key sizes and, since their key
  // sizes are unknown, are not counted in aes128_files.
  uint64_t undecodable_files = 0;
  // Encrypted files under a 16-byte data key or universe key. Always counted; paths are kept only
  // up to the requested limit, so aes128_paths.size() can be less than aes128_files.
  uint64_t aes128_files = 0;
  std::vector<std::string> aes128_paths;

  void ToJson(JsonWriter* jw) const;
};

// Reads the encryption header of every regular file under root_dirs. raw_env must not decrypt.
Result<EncryptionKeyReport> CollectEncryptionKeyReport(
    const std::vector<std::string>& root_dirs, Env* raw_env, HeaderManager* header_manager,
    UniverseKeyManager* universe_key_manager, size_t max_aes128_paths = 0);

} // namespace encryption
} // namespace yb
