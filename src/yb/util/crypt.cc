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

#include "yb/util/crypt.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "crypt_blowfish/cpp-ow-crypt.h"

namespace yb {
namespace util {

static constexpr uint16_t kBcryptRandomSize = 16;
static constexpr uint16_t kBcryptDefaultWorkFactor = 12;

static int try_close(int fd) {
  int ret;
  do {
    ret = close(fd);
  } while (ret == -1 && errno == EINTR);
  return ret;
}

static int try_read(int fd, char* out, size_t count) {
  size_t total = 0;
  ssize_t partial = 0;

  while (total < count) {
    do {
      partial = read(fd, out + total, count - total);
    } while (partial == -1 && errno == EINTR);

    if (partial < 1) {
      return -1;
    }

    total += partial;
  }

  return 0;
}

int bcrypt_gensalt(int workfactor, char salt[kBcryptHashSize]) {
  int fd;
  char input[kBcryptRandomSize];
  int workf;
  char* aux;

  fd = open("/dev/urandom", O_RDONLY);
  if (fd == -1) {
    return -1;
  }

  if (try_read(fd, input, kBcryptRandomSize) != 0) {
    try_close(fd);
    return -1;
  }

  if (try_close(fd) != 0) {
    return -1;
  }

  workf = (workfactor < 4 || workfactor > 31) ? 12 : workfactor;
  aux = crypt_gensalt_rn("$2a$", workf, input, kBcryptRandomSize, salt, kBcryptHashSize);
  return (aux == NULL) ? -1 : 0;
}

int bcrypt_hashpw(
    const char* passwd, const char salt[kBcryptHashSize], char hash[kBcryptHashSize]) {
  char* aux;
  aux = crypt_rn(passwd, salt, hash, kBcryptHashSize);
  return (aux == NULL) ? -1 : 0;
}

int bcrypt_hashpw(const char* passwd, char hash[kBcryptHashSize]) {
  char salt[kBcryptHashSize];
  int ret = bcrypt_gensalt(kBcryptDefaultWorkFactor, salt);
  if (ret != 0) {
    return ret;
  }
  return bcrypt_hashpw(passwd, salt, hash);
}

int bcrypt_checkpw(const char* passwd, const char hash[kBcryptHashSize]) {
  int ret;
  char outhash[kBcryptHashSize];

  ret = bcrypt_hashpw(passwd, hash, outhash);
  if (ret != 0) {
    return ret;
  }

  return strcmp(hash, outhash);
}

} // namespace util
} // namespace yb
