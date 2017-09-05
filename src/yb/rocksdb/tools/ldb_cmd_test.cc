//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
#ifndef ROCKSDB_LITE

#include "yb/rocksdb/tools/ldb_cmd.h"
#include "yb/rocksdb/util/testharness.h"

class LdbCmdTest : public testing::Test {};

TEST_F(LdbCmdTest, HexToString) {
  // map input to expected outputs.
  map<string, vector<int>> inputMap = {
      {"0x7", {7}},         {"0x5050", {80, 80}}, {"0xFF", {-1}},
      {"0x1234", {18, 52}}, {"0xaa", {-86}}, {"0x123", {18, 3}},
  };

  for (const auto& inPair : inputMap) {
    auto actual = rocksdb::LDBCommand::HexToString(inPair.first);
    auto expected = inPair.second;
    for (unsigned int i = 0; i < actual.length(); i++) {
      ASSERT_EQ(expected[i], static_cast<int>(actual[i]));
    }
  }
}

TEST_F(LdbCmdTest, HexToStringBadInputs) {
  const vector<string> badInputs = {
      "0xZZ", "123", "0xx5", "0x11G", "Ox12", "0xT", "0x1Q1",
  };
  for (const auto badInput : badInputs) {
    try {
      rocksdb::LDBCommand::HexToString(badInput);
      std::cerr << "Should fail on bad hex value: " << badInput << "\n";
      FAIL();
    } catch (...) {
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#else
#include <stdio.h>

int main(int argc, char** argv) {
  fprintf(stderr, "SKIPPED as LDBCommand is not supported in ROCKSDB_LITE\n");
  return 0;
}

#endif  // ROCKSDB_LITE
