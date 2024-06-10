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

#include "yb/gutil/casts.h"

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

#include "yb/encryption/encryption_test_util.h"
#include "yb/encryption/header_manager.h"
#include "yb/encryption/header_manager_mock_impl.h"

#include "yb/util/random_util.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

namespace yb {

constexpr uint32_t kDataSize = 1000;

class TestRocksDBEncryptedEnv : public YBTest {};

TEST_F(TestRocksDBEncryptedEnv, FileOps) {
  auto header_manager = encryption::GetMockHeaderManager();
  encryption::HeaderManager* hm_ptr = header_manager.get();
  auto env = yb::NewRocksDBEncryptedEnv(std::move(header_manager));
  auto fname = "test-file";
  auto bytes = RandomBytes(kDataSize);
  Slice data(bytes.data(), bytes.size());

  for (bool encrypted : {false, true}) {
    down_cast<encryption::HeaderManagerMockImpl*>(hm_ptr)->SetFileEncryption(encrypted);

    std::unique_ptr<rocksdb::WritableFile> writable_file;
    ASSERT_OK(env->NewWritableFile(fname, &writable_file, rocksdb::EnvOptions()));
    encryption::TestWrites(writable_file.get(), data);

    std::unique_ptr<rocksdb::RandomAccessFile> ra_file;
    ASSERT_OK(env->NewRandomAccessFile(fname, &ra_file, rocksdb::EnvOptions()));
    encryption::TestRandomAccessReads<uint8_t>(ra_file.get(), data);

    std::unique_ptr<rocksdb::SequentialFile> s_file;
    ASSERT_OK(env->NewSequentialFile(fname, &s_file, rocksdb::EnvOptions()));
    encryption::TestSequentialReads<uint8_t>(s_file.get(), data);

    ASSERT_OK(env->DeleteFile(fname));
  }
}

} // namespace yb
