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
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <iosfwd>
#include <string>
#include <type_traits>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/gutil/hash/hash.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/port.h"
#include "yb/gutil/type_traits.h"
#include "yb/util/format.h"
#include "yb/util/hash_util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/pg_util.h"

namespace yb {

// Test Murmur2 Hash64 returns the expected values for inputs. These tests are
// duplicated on the Java side to ensure that hash computations are stable
// across both platforms.
TEST(HashUtilTest, TestMurmur2Hash64) {
  uint64_t hash;

  hash = HashUtil::MurmurHash2_64("ab", 2, 0);
  ASSERT_EQ(7115271465109541368, hash);

  hash = HashUtil::MurmurHash2_64("abcdefg", 7, 0);
  ASSERT_EQ(2601573339036254301, hash);

  hash = HashUtil::MurmurHash2_64("quick brown fox", 15, 42);
  ASSERT_EQ(3575930248840144026, hash);
}

TEST(HashUtilTest, PgSocketDerivation) {
  LOG(INFO) << "Test IP addresses";
  ASSERT_EQ("/tmp/.yb.127.0.0.1:5433", PgDeriveSocketDir(HostPort("127.0.0.1", 5433)));
  ASSERT_EQ("/tmp/.yb.127.255.255.254:65535",
            PgDeriveSocketDir(HostPort("127.255.255.254", 65535)));

  LOG(INFO) << "Test names";
  constexpr auto kHostPrefix = "aaaaaaaaa.bbbbbbbbb.ccccccccc.ddddddddd.eeeeeeeee";
  constexpr auto kPort = 18008;
  // 63-char name
  ASSERT_EQ(
      Format("/tmp/.yb.$0.fffffffff.ggg:$1", kHostPrefix, kPort),
      PgDeriveSocketDir(HostPort(Format("$0.fffffffff.ggg", kHostPrefix), kPort)));
  // 64-char name
  ASSERT_EQ(
      Format("/tmp/.yb.$0.fffffffff.gggg:$1", kHostPrefix, kPort),
      PgDeriveSocketDir(HostPort(Format("$0.fffffffff.gggg", kHostPrefix), kPort)));
  // 77-char name
  ASSERT_EQ(
      Format("/tmp/.yb.$0.fffffffff.ggggggggg.hhhhhhh:$1", kHostPrefix, kPort),
      PgDeriveSocketDir(HostPort(Format("$0.fffffffff.ggggggggg.hhhhhhh", kHostPrefix), kPort)));
  // 78-char name
  ASSERT_EQ(
      Format("/tmp/.yb.$0.ffffff#9194157326238941401:$1", kHostPrefix, kPort),
      PgDeriveSocketDir(HostPort(Format("$0.fffffffff.ggggggggg.hhhhhhhh", kHostPrefix), kPort)));
  // 99-char name
  ASSERT_EQ(
      Format("/tmp/.yb.$0.ffffff#17919586771964798778:$1", kHostPrefix, kPort),
      PgDeriveSocketDir(HostPort(Format("$0.fffffffff.ggggggggg.hhhhhhhhh.iiiiiiiii.jjjjjjjjj",
                                        kHostPrefix),
                                 kPort)));
  // 255-char name
  ASSERT_EQ(
      Format("/tmp/.yb.$0.ffffff#10320903717037216904:$1", kHostPrefix, kPort),
      PgDeriveSocketDir(HostPort(Format("$0.fffffffff.ggggggggg.hhhhhhhhh.iiiiiiiii.jjjjjjjjj."
                                        "kkkkkkkkk.lllllllll.mmmmmmmmm.nnnnnnnnn.ooooooooo."
                                        "ppppppppp.qqqqqqqqq.rrrrrrrrr.sssssssss.ttttttttt."
                                        "uuuuuuuuu.vvvvvvvvv.wwwwwwwww.xxxxxxxxx.yyyyyyyyy.zzzzz",
                                        kHostPrefix),
                                 kPort)));
}

} // namespace yb
