// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "yb/util/random_util.h"

#include <sys/types.h>
#include <unistd.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "yb/util/env.h"
#include "yb/util/random.h"
#include "yb/gutil/walltime.h"

namespace yb {

void RandomString(void* dest, size_t n, Random* rng) {
  size_t i = 0;
  uint32_t random = rng->Next();
  char* cdest = static_cast<char*>(dest);
  static const size_t sz = sizeof(random);
  if (n >= sz) {
    for (i = 0; i <= n - sz; i += sz) {
      memcpy(&cdest[i], &random, sizeof(random));
      random = rng->Next();
    }
  }
  memcpy(cdest + i, &random, n - i);
}

uint32_t GetRandomSeed32() {
  uint32_t seed = static_cast<uint32_t>(GetCurrentTimeMicros());
  seed *= getpid();
  seed *= Env::Default()->gettid();
  return seed;
}

std::string RandomHumanReadableString(int len, Random* rnd) {
  // TODO: https://yugabyte.atlassian.net/browse/ENG-1508: Avoid code duplication in yb::Random and
  // rocksdb::Random. Currently this does not allow to reuse the same function in both code bases.
  std::string ret;
  ret.resize(len);
  for (int i = 0; i < len; ++i) {
    ret[i] = static_cast<char>('a' + rnd->Uniform(26));
  }
  return ret;
}

} // namespace yb
