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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <string>

#include <gtest/gtest.h>

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

static std::string IKey(const std::string& user_key,
                        uint64_t seq,
                        ValueType vt) {
  std::string encoded;
  AppendInternalKey(&encoded, ParsedInternalKey(user_key, seq, vt));
  return encoded;
}

static std::string Shorten(const std::string& s, const std::string& l) {
  std::string result = s;
  InternalKeyComparator(BytewiseComparator()).FindShortestSeparator(&result, l);
  return result;
}

auto ShortenUserKeys(const char* s, const char* l) {
  return Shorten(IKey(s, 100, kTypeValue), IKey(l, 200, kTypeValue));
}

#define TEST_SHORTEN_USER_KEYS(expected, start, limit) \
  ASSERT_EQ(IKey((expected), kMaxSequenceNumber, kValueTypeForSeek), ShortenUserKeys(start, limit))

static std::string ShortSuccessor(const std::string& s) {
  std::string result = s;
  InternalKeyComparator(BytewiseComparator()).FindShortSuccessor(&result);
  return result;
}

static void TestKey(const std::string& key,
                    uint64_t seq,
                    ValueType vt) {
  std::string encoded = IKey(key, seq, vt);

  Slice in(encoded);
  ParsedInternalKey decoded("", 0, kTypeValue);

  ASSERT_TRUE(ParseInternalKey(in, &decoded));
  ASSERT_EQ(key, decoded.user_key.ToString());
  ASSERT_EQ(seq, decoded.sequence);
  ASSERT_EQ(vt, decoded.type);

  ASSERT_TRUE(!ParseInternalKey(Slice("bar"), &decoded));
}

class FormatTest : public RocksDBTest {};

TEST_F(FormatTest, InternalKey_EncodeDecode) {
  const char* keys[] = { "", "k", "hello", "longggggggggggggggggggggg" };
  const uint64_t seq[] = {
    1, 2, 3,
    (1ull << 8) - 1, 1ull << 8, (1ull << 8) + 1,
    (1ull << 16) - 1, 1ull << 16, (1ull << 16) + 1,
    (1ull << 32) - 1, 1ull << 32, (1ull << 32) + 1
  };
  for (unsigned int k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
    for (unsigned int s = 0; s < sizeof(seq) / sizeof(seq[0]); s++) {
      TestKey(keys[k], seq[s], kTypeValue);
      TestKey("hello", 1, kTypeDeletion);
    }
  }
}

TEST_F(FormatTest, InternalKeyShortSeparator) {
  // When user keys are same
  ASSERT_EQ(IKey("foo", 100, kTypeValue),
            Shorten(IKey("foo", 100, kTypeValue),
                    IKey("foo", 99, kTypeValue)));
  ASSERT_EQ(IKey("foo", 100, kTypeValue),
            Shorten(IKey("foo", 100, kTypeValue),
                    IKey("foo", 101, kTypeValue)));
  ASSERT_EQ(IKey("foo", 100, kTypeValue),
            Shorten(IKey("foo", 100, kTypeValue),
                    IKey("foo", 100, kTypeValue)));
  ASSERT_EQ(IKey("foo", 100, kTypeValue),
            Shorten(IKey("foo", 100, kTypeValue),
                    IKey("foo", 100, kTypeDeletion)));

  // When user keys are misordered
  ASSERT_EQ(IKey("foo", 100, kTypeValue),
            Shorten(IKey("foo", 100, kTypeValue),
                    IKey("bar", 99, kTypeValue)));
  ASSERT_EQ(IKey("ABC\xffZ123", 100, kTypeValue), ShortenUserKeys("ABC\xffZ123", "ABC\x01"));

  // When user keys are different, but correctly ordered
  TEST_SHORTEN_USER_KEYS("g", "foo", "hello");
  TEST_SHORTEN_USER_KEYS("ABC2", "ABC1AAAAA", "ABC2ABB");
  TEST_SHORTEN_USER_KEYS("ABC1B", "ABC1AAA", "ABC2");
  TEST_SHORTEN_USER_KEYS("ABC1\xffH", "ABC1\xffG123", "ABC2");
  TEST_SHORTEN_USER_KEYS("AAA2", "AAA1AAA", "AAA2AA");
  TEST_SHORTEN_USER_KEYS("AAA2", "AAA1AAA", "AAA4");
  TEST_SHORTEN_USER_KEYS("AAA1B", "AAA1AAA", "AAA2");
  TEST_SHORTEN_USER_KEYS("AAA2", "AAA1AAA", "AAA2A");
  ASSERT_EQ(IKey("AAA1", 100, kTypeValue), ShortenUserKeys("AAA1", "AAA2"));

  // When start user key is prefix of limit user key
  ASSERT_EQ(IKey("foo", 100, kTypeValue), ShortenUserKeys("foo", "foobar"));

  // When limit user key is prefix of start user key
  ASSERT_EQ(IKey("foobar", 100, kTypeValue), ShortenUserKeys("foobar", "foo"));
}

TEST_F(FormatTest, InternalKeyShortestSuccessor) {
  ASSERT_EQ(IKey("g", kMaxSequenceNumber, kValueTypeForSeek),
            ShortSuccessor(IKey("foo", 100, kTypeValue)));
  ASSERT_EQ(IKey("\xff\xff", 100, kTypeValue),
            ShortSuccessor(IKey("\xff\xff", 100, kTypeValue)));
}

TEST_F(FormatTest, IterKeyOperation) {
  IterKey k;
  const char p[] = "abcdefghijklmnopqrstuvwxyz";
  const char q[] = "0123456789";

  ASSERT_EQ(k.GetKey(), "");

  k.TrimAppend(0, p, 3);
  ASSERT_EQ(k.GetKey(), "abc");

  k.TrimAppend(1, p, 3);
  ASSERT_EQ(k.GetKey(), "aabc");

  k.TrimAppend(0, p, 26);
  ASSERT_EQ(k.GetKey(), "abcdefghijklmnopqrstuvwxyz");

  k.TrimAppend(26, q, 10);
  ASSERT_EQ(k.GetKey(), "abcdefghijklmnopqrstuvwxyz0123456789");

  k.TrimAppend(36, q, 1);
  ASSERT_EQ(k.GetKey(), "abcdefghijklmnopqrstuvwxyz01234567890");

  k.TrimAppend(26, q, 1);
  ASSERT_EQ(k.GetKey(), "abcdefghijklmnopqrstuvwxyz0");

  // Size going up, memory allocation is triggered
  k.TrimAppend(27, p, 26);
  ASSERT_EQ(k.GetKey(), "abcdefghijklmnopqrstuvwxyz0abcdefghijklmnopqrstuvwxyz");
}

TEST_F(FormatTest, UpdateInternalKey) {
  std::string user_key("abcdefghijklmnopqrstuvwxyz");
  uint64_t new_seq = 0x123456;
  ValueType new_val_type = kTypeDeletion;

  std::string ikey;
  AppendInternalKey(&ikey, ParsedInternalKey(user_key, 100U, kTypeValue));
  size_t ikey_size = ikey.size();
  UpdateInternalKey(&ikey, new_seq, new_val_type);
  ASSERT_EQ(ikey_size, ikey.size());

  Slice in(ikey);
  ParsedInternalKey decoded;
  ASSERT_TRUE(ParseInternalKey(in, &decoded));
  ASSERT_EQ(user_key, decoded.user_key);
  ASSERT_EQ(new_seq, decoded.sequence);
  ASSERT_EQ(new_val_type, decoded.type);
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
