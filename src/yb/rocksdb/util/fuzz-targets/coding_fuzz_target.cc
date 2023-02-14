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

#include "yb/rocksdb/util/coding.h"

#include <string>

// LibFuzzer tutorial:
// https://github.com/google/fuzzing/blob/master/tutorial/libFuzzerTutorial.md
// LibFuzzer examples:
// https://www.programcreek.com/cpp/?CodeExample=llvm+fuzzer+test+one+input

namespace rocksdb {

class Coding { };

// Fuzz target example created from gtest
// rocksdb/util/coding_test.cc:TEST(Coding, Fixed32)
// Entry point for LibFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < sizeof(uint32_t))
    return 0;

  std::vector<uint32_t> buf(size/sizeof(uint32_t));
  memcpy(buf.data(), data, buf.size() * sizeof(uint32_t));

  std::string s;
  s.reserve(buf.size() * sizeof(uint32_t));
  for (auto v : buf) {
    PutFixed32(&s, v);
  }

  const char* p = s.data();
  for (auto v : buf) {
    uint32_t actual = DecodeFixed32(p);
    CHECK_EQ(v, actual);
    p += sizeof(uint32_t);
  }

  return 0;
}

}  // namespace rocksdb
