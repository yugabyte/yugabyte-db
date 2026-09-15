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

#include "yb/util/keyed_fingerprint.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/util/logging.h"

namespace yb {

namespace {

constexpr size_t kFingerprintKeyBytes = 32;
constexpr size_t kScopeTagBytes = 4;
constexpr size_t kDigestBytes = 8;
// Fixed label whose HMAC identifies the key (and thus the scope) itself.
const char kScopeTagLabel[] = "yb-fingerprint-scope-tag";

std::string HmacSha256HexPrefix(Slice key, Slice data, size_t out_bytes) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  unsigned int digest_len = 0;
  // HMAC() can realistically fail only on allocation failure; make the assumption explicit.
  CHECK_NOTNULL(HMAC(
      EVP_sha256(), key.data(), narrow_cast<int>(key.size()), data.data(), data.size(), digest,
      &digest_len));
  DCHECK_GE(digest_len, out_bytes);
  return strings::b2a_hex(pointer_cast<const char*>(digest), narrow_cast<int>(out_bytes));
}

}  // namespace

std::string GenerateFingerprintKey() {
  std::string key(kFingerprintKeyBytes, '\0');
  CHECK_EQ(1, RAND_bytes(pointer_cast<unsigned char*>(key.data()), kFingerprintKeyBytes));
  return key;
}

std::string KeyedFingerprintToken(Slice key, Slice data) {
  return HmacSha256HexPrefix(key, Slice(kScopeTagLabel), kScopeTagBytes) + "-" +
         HmacSha256HexPrefix(key, data, kDigestBytes);
}

}  // namespace yb
