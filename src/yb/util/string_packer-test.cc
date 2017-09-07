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

#include "yb/util/string_packer.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using std::replace;

namespace yb {
namespace util {

TEST(BytesFormatterTest, PackingSimple) {
  vector<string> pieces = {"ab", "bca"};
  string expected_packed = "ab00bca00";
  replace(expected_packed.begin(), expected_packed.end(), '0', '\x00');

  string result = PackZeroEncoded(pieces);

  ASSERT_EQ(9, result.length());
  ASSERT_EQ(expected_packed, result);
}

TEST(BytesFormatterTest, PackingComplex) {
  vector<string> pieces = {"ab", "", "", "a"};
  pieces[2].push_back('\x00');
  pieces[3].push_back('\x00');
  pieces[3].push_back('b');
  string expected_packed =  "ab00000100a01b00";
  replace(expected_packed.begin(), expected_packed.end(), '0', '\x00');
  replace(expected_packed.begin(), expected_packed.end(), '1', '\x01');

  string result = PackZeroEncoded(pieces);

  ASSERT_EQ(16, result.length());
  ASSERT_EQ(result, expected_packed);
}

TEST(BytesFormatterTest, UnpackingSimple) {
  vector<string> expected_pieces = {"ab", "bca"};
  string packed = "ab00bca00";
  replace(packed.begin(), packed.end(), '0', '\x00');

  vector<string> result = UnpackZeroEncoded(packed);

  ASSERT_EQ(expected_pieces, result);
}

TEST(BytesFormatterTest, UnpackingComplex) {
  vector<string> expected_pieces = {"ab", "", "", "a"};
  expected_pieces[2].push_back('\x00');
  expected_pieces[3].push_back('\x00');
  expected_pieces[3].push_back('b');
  string packed =  "ab00000100a01b00";
  replace(packed.begin(), packed.end(), '0', '\x00');
  replace(packed.begin(), packed.end(), '1', '\x01');

  vector<string> result = UnpackZeroEncoded(packed);

  ASSERT_EQ(expected_pieces, result);
}

TEST(BytesFormatterTest, RandomizedPAckUnpackTest) {

}

}
}
