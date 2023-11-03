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

#include "yb/encryption/encrypted_file_factory.h"
#include "yb/encryption/encryption_test_util.h"
#include "yb/encryption/encryption_util.h"
#include "yb/encryption/header_manager_mock_impl.h"

#include "yb/gutil/casts.h"

#include "yb/util/random_util.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;

namespace yb {
namespace encryption {

constexpr uint32_t kDataSize = 1000;

class TestEncryptedEnv : public YBTest {};

TEST_F(TestEncryptedEnv, FileOps) {
  auto header_manager = GetMockHeaderManager();
  HeaderManager* hm_ptr = header_manager.get();

  auto env = NewEncryptedEnv(std::move(header_manager));
  auto fname_template = "test-fileXXXXXX";
  auto bytes = RandomBytes(kDataSize*2);
  Slice first_half_data(bytes.data(), kDataSize);
  Slice second_half_data(bytes.data()+kDataSize, kDataSize);
  Slice total_data(bytes.data(), kDataSize*2);

  for (bool encrypted : {false, true}) {
    down_cast<HeaderManagerMockImpl*>(hm_ptr)->SetFileEncryption(encrypted);

    string fname;
    std::unique_ptr<WritableFile> writable_file;
    ASSERT_OK(env->NewTempWritableFile(
        WritableFileOptions(), fname_template, &fname, &writable_file));
    TestWrites(writable_file.get(), first_half_data);

    std::unique_ptr<yb::RandomAccessFile> ra_file;
    ASSERT_OK(env->NewRandomAccessFile(fname, &ra_file));
    TestRandomAccessReads<uint8_t>(ra_file.get(), first_half_data);

    auto opt = WritableFileOptions();
    opt.mode = EnvWrapper::OPEN_EXISTING;
    opt.initial_offset = kDataSize;
    ASSERT_OK(env->NewWritableFile(opt, fname, &writable_file));
    ASSERT_OK(writable_file->Append(second_half_data));

    ASSERT_OK(env->NewRandomAccessFile(fname, &ra_file));
    TestRandomAccessReads<uint8_t>(ra_file.get(), total_data);

    ASSERT_OK(env->DeleteFile(fname));
  }
}

} // namespace encryption
} // namespace yb
