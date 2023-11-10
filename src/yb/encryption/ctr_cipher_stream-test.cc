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
#include <thread>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/encryption/cipher_stream.h"
#include "yb/encryption/encryption_util.h"
#include "yb/encryption/header_manager.h"

#include "yb/rpc/secure_stream.h"

#include "yb/util/random_util.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

DECLARE_bool(TEST_encryption_use_openssl_compatible_counter_overflow);

namespace yb {
namespace encryption {

constexpr int kDataSize = 1024;
constexpr int kNumRuns = 1000;

class TestCipherStream : public YBTest {
 public:
  Status TestOverFlowWithKeyType(bool use_openssl_compatible_counter_overflow) {
    // Create a cipher stream on an iv about to overflow.
    FLAGS_TEST_encryption_use_openssl_compatible_counter_overflow =
        use_openssl_compatible_counter_overflow;
    auto params = EncryptionParams::NewEncryptionParams();
    // Initialize the nonce to be about to overflow for each position.
    uint8_t nonce[12];
    for (size_t i = 0; i < sizeof(nonce); i++) {
      nonce[i] = 0xFF;
    }
    params->counter = 0xFFFFFFF0;
    memcpy(params->nonce, nonce, 12);
    auto cipher_stream  = VERIFY_RESULT(BlockAccessCipherStream::FromEncryptionParams(
        std::move(params)));

    // Encrypt data such that part of the message is before the overflow and part is after.
    auto plaintext_bytes = RandomBytes(kDataSize);
    uint8_t encrypted_bytes[kDataSize];
    RETURN_NOT_OK(cipher_stream->Encrypt(
        0, Slice(plaintext_bytes.data(), kDataSize), encrypted_bytes));

    uint8_t decrypted_bytes[kDataSize];
    for (int i = 0; i < kNumRuns; i++) {
      memset(decrypted_bytes, 0, kDataSize);
      int start_idx = RandomUniformInt(0, kDataSize);
      int size = RandomUniformInt(0, kDataSize - start_idx);
      RETURN_NOT_OK(cipher_stream->Decrypt(
          start_idx, Slice(encrypted_bytes + start_idx, size), decrypted_bytes));
      if (Slice(decrypted_bytes, size) != Slice(plaintext_bytes.data() + start_idx, size)) {
        return STATUS(Corruption, Format("Corrupted bytes starting at $0 with size $1",
                                         start_idx, size));
      }
    }
    return Status::OK();
  }
};

TEST_F(TestCipherStream, ConcurrentEncryption) {
  rpc::InitOpenSSL();

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

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(TestCipherStream, Overflow) {
  // Create a cipher stream on a iv about to overflow.
  ASSERT_OK(TestOverFlowWithKeyType(true /* use_openssl_compatible_counter_overflow */ ));
  Status s = TestOverFlowWithKeyType(false /* use_openssl_compatible_counter_overflow */);
  ASSERT_NOK(s);
  ASSERT_EQ(s.CodeAsString(), "Corruption");
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "Corrupted bytes starting");
}

} // namespace encryption
} // namespace yb
