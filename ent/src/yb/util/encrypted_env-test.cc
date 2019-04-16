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

#include <string>

#include "yb/util/encrypted_file_factory.h"
#include "yb/util/encryption_util.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"
#include "yb/util/header_manager_mock_impl.h"
#include "yb/util/encryption_test_util.h"
#include "yb/util/random_util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

DECLARE_bool(enable_encryption);

namespace yb {
namespace enterprise {

constexpr uint32_t kDataSize = 1000;

class TestEncryptedEnv : public YBTest {};

TEST_F(TestEncryptedEnv, FileOps) {
  auto header_manager = GetMockHeaderManager();
  HeaderManager* hm_ptr = header_manager.get();
  auto env = yb::enterprise::NewEncryptedEnv(std::move(header_manager));
  auto fname_template = "test-fileXXXXXX";
  auto bytes = RandomBytes(kDataSize);
  Slice data(bytes.data(), kDataSize);

  for (bool encrypted : {false, true}) {
    FLAGS_enable_encryption = encrypted;
    down_cast<HeaderManagerMockImpl*>(hm_ptr)->SetFileEncryption(encrypted);

    string fname;
    gscoped_ptr<WritableFile> writable_file;
    ASSERT_OK(env->NewTempWritableFile(
        WritableFileOptions(), fname_template, &fname, &writable_file));
    TestWrites(writable_file.get(), data);

    gscoped_ptr<RandomAccessFile> ra_file;
    ASSERT_OK(env->NewRandomAccessFile(RandomAccessFileOptions(), fname, &ra_file));
    TestRandomAccessReads<RandomAccessFile, uint8_t>(ra_file.get(), data);

    ASSERT_OK(env->DeleteFile(fname));
  }
}

} // namespace enterprise
} // namespace yb
