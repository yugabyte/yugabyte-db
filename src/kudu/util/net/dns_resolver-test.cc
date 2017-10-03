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

#include "kudu/util/net/dns_resolver.h"

#include <boost/bind.hpp>
#include <gtest/gtest.h>
#include <vector>

#include "kudu/gutil/strings/util.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/net/net_util.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/test_util.h"

using std::vector;

namespace kudu {

class DnsResolverTest : public KuduTest {
 protected:
  DnsResolver resolver_;
};

TEST_F(DnsResolverTest, TestResolution) {
  vector<Sockaddr> addrs;
  Synchronizer s;
  {
    HostPort hp("localhost", 12345);
    resolver_.ResolveAddresses(hp, &addrs, s.AsStatusCallback());
  }
  ASSERT_OK(s.Wait());
  ASSERT_TRUE(!addrs.empty());
  for (const Sockaddr& addr : addrs) {
    LOG(INFO) << "Address: " << addr.ToString();
    EXPECT_TRUE(HasPrefixString(addr.ToString(), "127."));
    EXPECT_TRUE(HasSuffixString(addr.ToString(), ":12345"));
  }
}

} // namespace kudu
