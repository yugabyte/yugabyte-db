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

#include "yb/rocksutil/rocksdb_encrypted_file_factory.h"

#include "yb/util/status.h"
#include "yb/util/test_util.h"
#include "yb/util/header_manager.h"
#include "yb/util/header_manager_mock_impl.h"
#include "yb/util/encryption_test_util.h"
#include "yb/util/cipher_stream.h"

#include "yb/util/random_util.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace yb {
namespace enterprise {

class TestCipherStream : public YBTest {};

TEST_F(TestCipherStream, ConcurrentEncryption) {
  InitOpenSSL();

  constexpr int kBufSize = 10000;
  constexpr int kNumRuns = 10000;
  auto plaintext_bytes = RandomBytes(kBufSize);
  uint8_t encrypted_bytes[kBufSize];
  auto encryption_params = EncryptionParams::NewEncryptionParams();
  auto cipher_stream  = ASSERT_RESULT(BlockAccessCipherStream::FromEncryptionParams(
      std::move(encryption_params)));
  ASSERT_OK(cipher_stream->Encrypt(0, Slice(plaintext_bytes.data(), kBufSize), encrypted_bytes));

  std::function<void(bool)> f = [&](bool encrypt) {
    uint8_t result[kBufSize];
    int start = RandomUniformInt(0, kBufSize);
    int size = RandomUniformInt(0, kBufSize - start);

    uint8_t* start_data = encrypt ? plaintext_bytes.data() : encrypted_bytes;
    uint8_t* verify_data = encrypt ? encrypted_bytes : plaintext_bytes.data();


    ASSERT_OK(cipher_stream->Encrypt(start, Slice(start_data + start, size), result));
    ASSERT_EQ(Slice(verify_data + start, size), Slice(result, result + size));
  };

  std::vector<std::thread> threads;
  // Create 20 encrypt threads
  for (int i = 0; i < 20; i++) {
    threads.emplace_back([&]() {
      for (int i = 0; i < kNumRuns; i++) {
        // Do this to trigger use of openssl RAND_bytes
        EncryptionParams::NewEncryptionParams();
        ASSERT_NO_FATALS(f(true /* encrypt */));
      }
    });
  }

  // Create 20 decrypt threads
  for (int i = 0; i < 20; i++) {
    threads.emplace_back([&]() {
      for (int i = 0; i < kNumRuns; i++) {
        EncryptionParams::NewEncryptionParams();
        ASSERT_NO_FATALS(f(false /* encrypt */));
      }
    });
  }

  for (int i = 0; i < threads.size(); i++) {
    threads[i].join();
  }
}

} // namespace enterprise
} // namespace yb
