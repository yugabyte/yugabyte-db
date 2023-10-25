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

DECLARE_string(tmp_dir);

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

TEST(HashUtilTest, PgSocketDerivationWithCustomPath) {
  constexpr auto port = 65535;

  // Smallest flag path.
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tmp_dir) = "/";

    // Largest possible host without trimming.
    // tmp path len = 1, hostname len = 80, port len = 5.
    constexpr auto largest_hostname_without_trimming =
        "aaaaaaaaa.bbbbbbbbb.ccccccccc.ddddddddd.eeeeeeeee.fffffffff.ggggggggg.hhhhhhhhh.";
    ASSERT_EQ(
        Format("$0/.yb.$1:$2", FLAGS_tmp_dir, largest_hostname_without_trimming, port),
        PgDeriveSocketDir(HostPort(largest_hostname_without_trimming, port)));

    // Above host with 1 more character which will lead to trimming.
    // tmp path len = 1, hostname len = 81, port len = 5.
    auto smallest_hostname_with_trimming = Format("$0$1", largest_hostname_without_trimming, "i");
    ASSERT_EQ(
        Format(
            "$0/.yb.$1:$2",
            FLAGS_tmp_dir,
            "aaaaaaaaa.bbbbbbbbb.ccccccccc.ddddddddd.eeeeeeeee.fffffffff#13688723505865877493",
            port),
        PgDeriveSocketDir(HostPort(smallest_hostname_with_trimming, port)));
  }

  // Largest flag path that doesn't involve fallback to /tmp.
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tmp_dir) =
        "/aaaaa/bbb/ccc/dddddddddddddddddddddddddddddddddddddddddd/ee";

    constexpr auto largest_hostname_without_trimming = "aaaaaaaaa.bbbbbbbbb.c";
    // tmp path len = 60, hostname len = 21, port len = 5.
    ASSERT_EQ(
        Format("$0/.yb.$1:$2", FLAGS_tmp_dir, largest_hostname_without_trimming, port),
        PgDeriveSocketDir(HostPort(largest_hostname_without_trimming, port)));

    // Above host with 1 more character which will lead to trimming.
    // tmp path len = 60, hostname len = 22, port len = 5.
    auto smallest_hostname_with_trimming = Format("$0$1", largest_hostname_without_trimming, "c");
    ASSERT_EQ(
        Format("$0/.yb.$1:$2", FLAGS_tmp_dir, "#12324495471980671580", port),
        PgDeriveSocketDir(HostPort(smallest_hostname_with_trimming, port)));
  }

  // Smallest flag path which involves fallback to /tmp.
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tmp_dir) =
        "/aaaaa/bbb/ccc/dddddddddddddddddddddddddddddddddddddddddd/eee";
    constexpr auto fallback_tmp_path = "/tmp";

    // Largest possible host without trimming.
    // tmp path len = 61, which leads to fallback to "/tmp".
    // So effective tmp path len = 4, hostname len = 77, port len = 5.
    constexpr auto largest_hostname_without_trimming =
        "aaaaaaaaa.bbbbbbbbb.ccccccccc.ddddddddd.eeeeeeeee.fffffffff.ggggggg.hhhhhhh.i";
    ASSERT_EQ(
        Format("$0/.yb.$1:$2", fallback_tmp_path, largest_hostname_without_trimming, port),
        PgDeriveSocketDir(HostPort(largest_hostname_without_trimming, port)));

    // Above host with 1 more character which will lead to trimming.
    // tmp path len = 61, which leads to fallback to "/tmp".
    // So effective tmp path len = 4, hostname len = 78, port len = 5.
    auto smallest_hostname_with_trimming = Format("$0$1", largest_hostname_without_trimming, "i");
    ASSERT_EQ(
        Format(
            "$0/.yb.$1:$2",
            fallback_tmp_path,
            "aaaaaaaaa.bbbbbbbbb.ccccccccc.ddddddddd.eeeeeeeee.ffffff#17028242530413137375",
            port),
        PgDeriveSocketDir(HostPort(smallest_hostname_with_trimming, port)));
  }
}

} // namespace yb
