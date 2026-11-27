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

#include "yb/encryption/encryption_key_report.h"

#include <ranges>
#include <string_view>

#include "yb/encryption/cipher_stream.h"
#include "yb/encryption/header_manager.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/util/env.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"

namespace yb {
namespace encryption {

namespace {

constexpr std::string_view kSnapshotsDirSuffix = ".snapshots";
constexpr std::string_view kWalsDirName = "wals";
constexpr std::string_view kTabletMetadataDirName = "tablet-meta";
constexpr std::string_view kConsensusMetadataDirName = "consensus-meta";

EncryptionKeyReportFileCategory Categorize(const std::string& dirname) {
  for (const auto& part : std::views::split(dirname, '/')) {
    std::string_view component(part.begin(), part.end());
    if (component.ends_with(kSnapshotsDirSuffix)) {
      return EncryptionKeyReportFileCategory::kSnapshot;
    }
    if (component == kWalsDirName) {
      return EncryptionKeyReportFileCategory::kWal;
    }
    if (component == kTabletMetadataDirName || component == kConsensusMetadataDirName) {
      return EncryptionKeyReportFileCategory::kMetadata;
    }
  }
  return EncryptionKeyReportFileCategory::kData;
}

EncryptionKeyReportFileType FileType(const std::string& basename) {
  if (basename.ends_with(".sst") || basename.find(".sblock.") != std::string::npos) {
    return EncryptionKeyReportFileType::kSst;
  }
  if (basename.starts_with("MANIFEST-")) {
    return EncryptionKeyReportFileType::kManifest;
  }
  if (basename.starts_with("wal-")) {
    return EncryptionKeyReportFileType::kWalSegment;
  }
  return EncryptionKeyReportFileType::kOther;
}

Status AddFile(
    const std::string& path, EncryptionKeyReportFileCategory category, Env* raw_env,
    HeaderManager* header_manager, UniverseKeyManager* universe_key_manager,
    size_t max_aes128_paths, EncryptionKeyReport* report) {
  const auto bytes = VERIFY_RESULT(raw_env->GetFileSize(path));
  std::unique_ptr<yb::RandomAccessFile> file;
  RETURN_NOT_OK(raw_env->NewRandomAccessFile(path, &file));

  EncryptionKeyReport::Key key{
      .category = category,
      .file_type = FileType(BaseName(path)),
      .encrypted = false,
      .data_key_bytes = 0,
      .universe_key_id = {},
      .universe_key_bytes = 0};
  std::unique_ptr<BlockAccessCipherStream> stream;
  uint32_t header_size;
  bool undecodable = false;
  // Empty and very short files cannot carry an encryption header.
  if (bytes >= header_manager->GetEncryptionMetadataStartIndex()) {
    auto encrypted = GetEncryptionInfoFromFile<uint8_t>(
        header_manager, file.get(), &stream, &header_size, &key.universe_key_id);
    if (encrypted.ok()) {
      key.encrypted = *encrypted;
    } else if (!key.universe_key_id.empty()) {
      // The header names its universe key, which is parsed before the data key is decrypted, so
      // this file is encrypted under a universe key that is missing from the registry.
      VLOG(1) << "Cannot decrypt the data key of " << path << ": " << encrypted.status();
      key.encrypted = true;
      undecodable = true;
    } else {
      return encrypted.status();
    }
  }
  if (undecodable) {
    ++report->undecodable_files;
  } else if (key.encrypted) {
    key.data_key_bytes = stream->key_size();
    auto universe_params = universe_key_manager->GetUniverseParamsWithVersion(key.universe_key_id);
    if (universe_params.ok()) {
      key.universe_key_bytes = (*universe_params)->key_size;
    }
  }

  if (key.encrypted && (key.data_key_bytes == 16 || key.universe_key_bytes == 16)) {
    ++report->aes128_files;
    if (report->aes128_paths.size() < max_aes128_paths) {
      report->aes128_paths.push_back(path);
    }
  }

  auto& totals = report->groups[key];
  ++totals.files;
  totals.bytes += bytes;
  return Status::OK();
}

} // namespace

Result<EncryptionKeyReport> CollectEncryptionKeyReport(
    const std::vector<std::string>& root_dirs, Env* raw_env, HeaderManager* header_manager,
    UniverseKeyManager* universe_key_manager, size_t max_aes128_paths) {
  EncryptionKeyReport report;
  for (const auto& root : root_dirs) {
    // Errors from individual files are counted rather than returned: files are routinely deleted
    // by compaction and log GC while the walk is in progress.
    RETURN_NOT_OK(raw_env->Walk(
        root, Env::PRE_ORDER,
        [&](Env::FileType type, const std::string& dirname, const std::string& basename) {
          if (type != Env::FILE_TYPE) {
            return Status::OK();
          }
          auto s = AddFile(
              JoinPathSegments(dirname, basename), Categorize(dirname), raw_env, header_manager,
              universe_key_manager, max_aes128_paths, &report);
          if (!s.ok()) {
            VLOG(1) << "Skipping " << JoinPathSegments(dirname, basename) << ": " << s;
            ++report.unreadable_files;
          }
          return Status::OK();
        }));
  }
  return report;
}

void EncryptionKeyReport::ToJson(JsonWriter* jw) const {
  jw->StartObject();
  jw->String("groups");
  jw->StartArray();
  for (const auto& [key, totals] : groups) {
    jw->StartObject();
    jw->String("category");
    jw->String(ToString(key.category).substr(1));
    jw->String("file_type");
    jw->String(ToString(key.file_type).substr(1));
    jw->String("encrypted");
    jw->Bool(key.encrypted);
    if (key.encrypted) {
      jw->String("data_key_bits");
      jw->Uint(key.data_key_bytes * 8);
      jw->String("universe_key_id");
      jw->String(key.universe_key_id);
      jw->String("universe_key_bits");
      jw->Uint(key.universe_key_bytes * 8);
    }
    jw->String("files");
    jw->Uint64(totals.files);
    jw->String("bytes");
    jw->Uint64(totals.bytes);
    jw->EndObject();
  }
  jw->EndArray();
  jw->String("unreadable_files");
  jw->Uint64(unreadable_files);
  jw->String("undecodable_files");
  jw->Uint64(undecodable_files);
  jw->String("aes128_files");
  jw->Uint64(aes128_files);
  if (!aes128_paths.empty()) {
    jw->String("aes128_paths_truncated");
    jw->Bool(aes128_paths.size() < aes128_files);
    jw->String("aes128_paths");
    jw->StartArray();
    for (const auto& path : aes128_paths) {
      jw->String(path);
    }
    jw->EndArray();
  }
  jw->EndObject();
}

} // namespace encryption
} // namespace yb
