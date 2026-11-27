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

#include <string>

#include <gtest/gtest.h>

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/encryption/encryption_key_report.h"
#include "yb/encryption/header_manager_impl.h"
#include "yb/encryption/universe_key_manager.h"

#include "yb/tserver/universe_key_test_util.h"

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/path_util.h"
#include "yb/util/test_util.h"

DECLARE_string(encryption_algorithm_override);

namespace yb::encryption {

using Category = EncryptionKeyReportFileCategory;
using FileType = EncryptionKeyReportFileType;
constexpr auto kAes128 = ENCRYPTION_ALGORITHM_AES_128_CTR;
constexpr auto kAes256 = ENCRYPTION_ALGORITHM_AES_256_CTR;

class EncryptionKeyReportTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    ASSERT_OK(SET_FLAG(encryption_algorithm_override, "AES_256_CTR"));
    universe_key_manager_ = tserver::GenerateTestUniverseKeyManager();
    encrypted_env_ = NewEncryptedEnv(DefaultHeaderManager(universe_key_manager_.get()));
    root_ = GetTestPath("root");
  }

  void WriteFile(Env* env, const std::string& relative_path, EncryptionAlgorithmPB algorithm) {
    ASSERT_OK(SET_FLAG(encryption_algorithm_override, EncryptionAlgorithmName(algorithm)));
    auto path = JoinPathSegments(root_, relative_path);
    ASSERT_OK(env_->CreateDirs(DirName(path)));
    // The encrypted Env only encrypts new files created through NewTempWritableFile.
    std::unique_ptr<WritableFile> file;
    std::string tmp_path;
    ASSERT_OK(env->NewTempWritableFile(
        WritableFileOptions(), JoinPathSegments(root_, "tmpXXXXXX"), &tmp_path, &file));
    ASSERT_OK(file->Append("0123456789"));
    ASSERT_OK(file->Close());
    ASSERT_OK(env_->RenameFile(tmp_path, path));
  }

  std::unique_ptr<UniverseKeyManager> universe_key_manager_;
  std::unique_ptr<Env> encrypted_env_;
  std::string root_;
};

TEST_F(EncryptionKeyReportTest, GroupsByKeySizeAndCategory) {
  ASSERT_NO_FATALS(WriteFile(encrypted_env_.get(), "data/rocksdb/tablet-1/000010.sst", kAes128));
  ASSERT_NO_FATALS(WriteFile(encrypted_env_.get(), "data/rocksdb/tablet-1/000011.sst", kAes256));
  ASSERT_NO_FATALS(WriteFile(encrypted_env_.get(), "data/rocksdb/tablet-1/000012.sst", kAes256));
  ASSERT_NO_FATALS(
      WriteFile(encrypted_env_.get(), "data/rocksdb/tablet-1/MANIFEST-000005", kAes128));
  ASSERT_NO_FATALS(WriteFile(
      encrypted_env_.get(), "data/rocksdb/tablet-1.snapshots/s1/000010.sst", kAes128));
  ASSERT_NO_FATALS(WriteFile(encrypted_env_.get(), "wals/table-1/tablet-1/wal-000001", kAes128));
  ASSERT_NO_FATALS(WriteFile(encrypted_env_.get(), "tablet-meta/tablet-1", kAes256));
  ASSERT_NO_FATALS(WriteFile(env_.get(), "plain", kAes128));
  ASSERT_OK(WriteStringToFile(env_.get(), Slice(), JoinPathSegments(root_, "empty")));

  auto universe_key_id = ASSERT_RESULT(universe_key_manager_->GetLatestUniverseParams()).version_id;
  auto report = ASSERT_RESULT(CollectEncryptionKeyReport(
      {root_}, env_.get(), DefaultHeaderManager(universe_key_manager_.get()).get(),
      universe_key_manager_.get()));

  std::stringstream json;
  JsonWriter jw(&json, JsonWriter::PRETTY);
  report.ToJson(&jw);
  LOG(INFO) << "Report: " << json.str();

  auto files = [&](Category category, FileType file_type, bool encrypted,
                   uint32_t data_key_bytes) -> uint64_t {
    EncryptionKeyReport::Key key{
        .category = category, .file_type = file_type, .encrypted = encrypted,
        .data_key_bytes = encrypted ? data_key_bytes : 0,
        .universe_key_id = encrypted ? universe_key_id : "",
        .universe_key_bytes = encrypted ? 32U : 0};
    auto it = report.groups.find(key);
    return it == report.groups.end() ? 0 : it->second.files;
  };
  ASSERT_EQ(files(Category::kData, FileType::kSst, true, 16), 1);
  ASSERT_EQ(files(Category::kData, FileType::kSst, true, 32), 2);
  ASSERT_EQ(files(Category::kData, FileType::kManifest, true, 16), 1);
  ASSERT_EQ(files(Category::kSnapshot, FileType::kSst, true, 16), 1);
  ASSERT_EQ(files(Category::kWal, FileType::kWalSegment, true, 16), 1);
  ASSERT_EQ(files(Category::kData, FileType::kOther, false, 0), 2);
  ASSERT_EQ(files(Category::kMetadata, FileType::kOther, true, 32), 1);
  ASSERT_EQ(report.groups.size(), 7);
  ASSERT_EQ(report.unreadable_files, 0);
  ASSERT_EQ(report.undecodable_files, 0);
  // 000010.sst, MANIFEST, the snapshot SST and the WAL segment use 16-byte data keys.
  ASSERT_EQ(report.aes128_files, 4);
  ASSERT_TRUE(report.aes128_paths.empty());

  auto limited_report = ASSERT_RESULT(CollectEncryptionKeyReport(
      {root_}, env_.get(), DefaultHeaderManager(universe_key_manager_.get()).get(),
      universe_key_manager_.get(), /* max_aes128_paths= */ 2));
  ASSERT_EQ(limited_report.aes128_files, 4);
  ASSERT_EQ(limited_report.aes128_paths.size(), 2);
}

// A file under a universe key missing from the registry is reported under that key, with unknown
// key sizes, rather than dropped from the report.
TEST_F(EncryptionKeyReportTest, MissingUniverseKey) {
  auto other_key_manager = tserver::GenerateTestUniverseKeyManager();
  auto other_env = NewEncryptedEnv(DefaultHeaderManager(other_key_manager.get()));
  ASSERT_NO_FATALS(WriteFile(other_env.get(), "data/rocksdb/tablet-1/000010.sst", kAes128));
  auto other_key_id = ASSERT_RESULT(other_key_manager->GetLatestUniverseParams()).version_id;

  auto report = ASSERT_RESULT(CollectEncryptionKeyReport(
      {root_}, env_.get(), DefaultHeaderManager(universe_key_manager_.get()).get(),
      universe_key_manager_.get()));
  EncryptionKeyReport::Key key{
      .category = Category::kData, .file_type = FileType::kSst, .encrypted = true,
      .data_key_bytes = 0, .universe_key_id = other_key_id, .universe_key_bytes = 0};
  ASSERT_EQ(report.groups.size(), 1);
  ASSERT_EQ(report.groups.begin()->first, key);
  ASSERT_EQ(report.undecodable_files, 1);
  ASSERT_EQ(report.unreadable_files, 0);
  ASSERT_EQ(report.aes128_files, 0);
}

} // namespace yb::encryption
