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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/util/random_util.h"

#include <cmath>
#include <cstdlib>
#include <random>

#include "yb/util/random.h"

namespace yb {

namespace {

const std::string kHumanReadableCharacters =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

}

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
  std::random_device device;
  return device();
}

std::vector<uint8_t> RandomBytes(size_t len, std::mt19937_64* rng) {
  std::vector<uint8_t> data(len);
  std::generate(data.begin(), data.end(), [=] {
    return RandomUniformInt<uint8_t>(0, std::numeric_limits<uint8_t>::max(), rng);
  });
  return data;
}

std::string RandomString(size_t len, std::mt19937_64* rng) {
  std::string str;
  str.reserve(len);
  while (len > 0) {
    str += static_cast<char>(
        RandomUniformInt<uint8_t>(0, std::numeric_limits<uint8_t>::max(), rng));
    len--;
  }
  return str;
}

std::string RandomHumanReadableString(size_t len, Random* rnd) {
  // TODO: https://yugabyte.atlassian.net/browse/ENG-1508: Avoid code duplication in yb::Random and
  // rocksdb::Random. Currently this does not allow to reuse the same function in both code bases.
  std::string ret;
  ret.resize(len);
  for (size_t i = 0; i < len; ++i) {
    ret[i] = static_cast<char>('a' + rnd->Uniform(26));
  }
  return ret;
}

std::string RandomHumanReadableString(size_t len, std::mt19937_64* rng) {
  std::string ret(len, 0);
  for (size_t i = 0; i != len; ++i) {
    ret[i] = RandomElement(kHumanReadableCharacters, rng);
  }
  return ret;
}

namespace {

thread_local std::unique_ptr<std::mt19937_64> thread_local_random_ptr;
thread_local bool random_initializing = false;

} // namespace

std::mt19937_64& ThreadLocalRandom() {
  auto* result = thread_local_random_ptr.get();
  if (result) {
    return *result;
  }

  random_initializing = true;
  thread_local_random_ptr.reset(result = new std::mt19937_64);
  random_initializing = false;

  Seed(result);
  return *result;
}

bool RandomUniformBool(std::mt19937_64* rng) {
  return RandomUniformInt(0, 1, rng) != 0;
}

bool IsRandomInitializingInThisThread() {
  return random_initializing;
}

} // namespace yb
