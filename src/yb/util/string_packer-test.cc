// Copyright (c) YugaByte, Inc.

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
