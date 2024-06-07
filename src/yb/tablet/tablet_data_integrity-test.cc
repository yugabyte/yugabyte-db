// Copyright (c) Yugabyte, Inc.
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

#include <gtest/gtest.h>

#include "yb/qlexpr/ql_expr.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"

#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-base.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/path_util.h"
#include "yb/util/slice.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace tablet {

class TabletDataIntegrityTest : public TabletTestBase<IntKeyTestSetup<DataType::INT32>> {
  typedef TabletTestBase<IntKeyTestSetup<DataType::INT32>> superclass;
 public:
  void SetUp() override {
    superclass::SetUp();

    auto tablet = this->tablet();
    LocalTabletWriter writer(tablet);
    ASSERT_OK(this->InsertTestRow(&writer, 12345, 0));
    ASSERT_OK(tablet->Flush(FlushMode::kSync));
  }

 protected:
  Result<std::string> GetFirstSstFilePath() {
    auto tablet = this->tablet();
    auto dir = tablet->metadata()->rocksdb_dir();
    auto list = VERIFY_RESULT(tablet->metadata()->fs_manager()->ListDir(dir));
    if (std::find(list.begin(), list.end(), "CURRENT") == list.end()) {
      return STATUS(NotFound, "No rocksdb files found at tablet directory");
    }

    for (const auto& file : list) {
      if (file.find(".sst") != std::string::npos) {
        return JoinPathSegments(dir, file);
      }
    }

    return STATUS(NotFound, "No sst files found in rocksdb directory");
  }
};

TEST_F(TabletDataIntegrityTest, TestNoCorruption) {
  auto tablet = this->tablet();
  ASSERT_OK(tablet->VerifyDataIntegrity());
}

TEST_F(TabletDataIntegrityTest, TestDeletedFile) {
  auto tablet = this->tablet();

  auto sst_path = ASSERT_RESULT(GetFirstSstFilePath());
  ASSERT_OK(env_->DeleteFile(sst_path));

  Status s = tablet->VerifyDataIntegrity();
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "No such file");
}

TEST_F(TabletDataIntegrityTest, TestFileTruncate) {
  auto tablet = this->tablet();

  auto sst_path = ASSERT_RESULT(GetFirstSstFilePath());
  faststring data;
  ASSERT_OK(ReadFileToString(env_.get(), sst_path, &data));
  data.resize(1);
  ASSERT_OK(WriteStringToFile(env_.get(), Slice(data), sst_path));

  Status s = tablet->VerifyDataIntegrity();
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "file size mismatch");
}

// Skipping as we currently don't have any block checks in place.
// TODO: enable this test once we add those. (See issue #7904)
TEST_F(TabletDataIntegrityTest, DISABLED_TestFileGarbageOverwrite) {
  auto tablet = this->tablet();

  auto sst_path = ASSERT_RESULT(GetFirstSstFilePath());
  faststring data;
  ASSERT_OK(ReadFileToString(env_.get(), sst_path, &data));

  faststring garbage;
  garbage.resize(data.size());
  ASSERT_OK(WriteStringToFile(env_.get(), Slice(garbage), sst_path));

  Status s = tablet->VerifyDataIntegrity();
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "bad block contents");
}

} // namespace tablet
} // namespace yb
