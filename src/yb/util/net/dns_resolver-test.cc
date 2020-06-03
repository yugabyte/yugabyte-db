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

#include "yb/util/net/dns_resolver.h"

#include <vector>

#include <gtest/gtest.h>

#include "yb/gutil/strings/util.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/test_util.h"

using std::vector;

namespace yb {

class DnsResolverTest : public YBTest {
 protected:
  DnsResolver resolver_;
};

TEST_F(DnsResolverTest, TestResolution) {
  vector<Endpoint> addrs;
  Synchronizer s;
  {
    HostPort hp("localhost", 12345);
    resolver_.ResolveAddresses(hp, &addrs, s.AsStatusCallback());
  }
  ASSERT_OK(s.Wait());
  ASSERT_TRUE(!addrs.empty());
  for (const auto& addr : addrs) {
    LOG(INFO) << "Address: " << addr;
    if (addr.address().is_v4()) {
      EXPECT_TRUE(HasPrefixString(ToString(addr), "127."));
    } else if (addr.address().is_v6()) {
      EXPECT_TRUE(HasPrefixString(ToString(addr), "[::1]"));
    }
    EXPECT_TRUE(HasSuffixString(ToString(addr), ":12345"));
  }
}

} // namespace yb
