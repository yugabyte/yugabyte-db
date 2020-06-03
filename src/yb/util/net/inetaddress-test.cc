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

#include "yb/util/net/inetaddress.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/net/net_fwd.h"

namespace yb {

class InetAddressTest : public YBTest {
 protected:
  void RunRoundTrip(const std::string& strval) {
    InetAddress addr_orig;
    ASSERT_OK(addr_orig.FromString(strval));
    std::string bytes;
    ASSERT_OK(addr_orig.ToBytes(&bytes));
    InetAddress addr_new;
    ASSERT_OK(addr_new.FromBytes(bytes));
    std::string strval_new;
    ASSERT_OK(addr_new.ToString(&strval_new));
    ASSERT_EQ(strval, strval_new);
    ASSERT_EQ(addr_orig, addr_new);
  }
};

TEST_F(InetAddressTest, TestRoundTrip) {
  for (const std::string& strval : {
      "1.2.3.4",
      "2001:db8:a0b:12f0::1",
      "0.0.0.0",
      "2607:f0d0:1002:51::4",
      "::1",
      "255.255.255.255"}) {
    RunRoundTrip(strval);
  }
}

TEST_F(InetAddressTest, TestOperators) {
  // Assignment.
  InetAddress addr1;
  ASSERT_OK(addr1.FromString("1.2.3.4"));
  InetAddress addr2 = addr1;
  std::string strval;
  ASSERT_OK(addr2.ToString(&strval));
  ASSERT_EQ("1.2.3.4", strval);

  // InEquality.
  ASSERT_OK(addr1.FromString("1.2.3.4"));
  ASSERT_OK(addr2.FromString("1.2.3.5"));
  ASSERT_NE(addr1, addr2);

  // Comparison.
  ASSERT_OK(addr1.FromString("1.2.3.4"));
  ASSERT_OK(addr2.FromString("2001:db8:a0b:12f0::1"));

  // v4 < v6
  ASSERT_LT(addr1, addr2);
  ASSERT_GT(addr2, addr1);

  ASSERT_OK(addr1.FromString("1.2.3.4"));
  ASSERT_OK(addr2.FromString("1.2.3.5"));
  ASSERT_LT(addr1, addr2);
  ASSERT_LE(addr1, addr2);

  ASSERT_OK(addr1.FromString("1.2.3.4"));
  ASSERT_OK(addr2.FromString("1.2.3.4"));
  ASSERT_LE(addr1, addr2);
  ASSERT_GE(addr1, addr2);
}

TEST_F(InetAddressTest, TestErrors) {
  InetAddress addr;
  ASSERT_FALSE(addr.FromString("1.2.3.256").ok());
  ASSERT_FALSE(addr.FromString("1:2:3:f").ok());
  ASSERT_FALSE(addr.FromString("2607:g0d0:1002:51::4").ok());

  std::string bytes;
  ASSERT_FALSE(addr.FromBytes(bytes).ok());
  bytes = "0";
  ASSERT_FALSE(addr.FromBytes(bytes).ok());
  bytes = "012345";
  ASSERT_FALSE(addr.FromBytes(bytes).ok());
  bytes = "111111111111111111"; // 17 bytes.
  ASSERT_FALSE(addr.FromBytes(bytes).ok());
}

TEST_F(InetAddressTest, TestHostName) {
  InetAddress addr;
  ASSERT_OK(addr.FromString("localhost"));
  ASSERT_OK(addr.FromString("1.2.3")); // boost seems to convert this to 1.2.0.3.
  ASSERT_EQ("1.2.0.3", addr.ToString());
  ASSERT_OK(addr.FromString("1.2")); // boost seems to convert this to 1.0.0.2.
  ASSERT_EQ("1.0.0.2", addr.ToString());
  ASSERT_OK(addr.FromString("1000"));
  ASSERT_EQ("0.0.3.232", addr.ToString());
  ASSERT_OK(addr.FromString("0xC00002EB"));
  ASSERT_EQ("192.0.2.235", addr.ToString());
}

TEST_F(InetAddressTest, FilterAddresses) {

  // Create a list of ipv6 and ipv4 addresses
  const vector<string> address_strs = {
    "::1",                                    "10.150.0.148",
    "2600:1f18:1094:c832:36e6:43b9:e6c8:f02", "127.0.0.1",
    "127.0.0.2",                              "0.0.0.0",
    "::",                                     "fe80::4001:aff:fe96:94",
    "fe80::2%lo"
  };
  vector<IpAddress> addresses;
  for (const auto &address_str : address_strs) {
    LOG(INFO) << address_str;
    addresses.push_back(IpAddress::from_string(address_str));
  }
  LOG(INFO) << "Starting test";
  auto test_addresses = addresses;
  FilterAddresses("ipv4_all", &test_addresses);
  ASSERT_EQ(test_addresses.size(), 4);

  test_addresses = addresses;
  FilterAddresses("ipv6_all", &test_addresses);
  ASSERT_EQ(test_addresses.size(), 5);

  test_addresses = addresses;
  FilterAddresses("ipv6_all,ipv4_all", &test_addresses);
  ASSERT_EQ(test_addresses.size(), 9);
  ASSERT_TRUE(test_addresses[0].is_v6());
  ASSERT_TRUE(test_addresses[test_addresses.size() - 1].is_v4());

  test_addresses = addresses;
  FilterAddresses("ipv6_external", &test_addresses);
  ASSERT_EQ(test_addresses.size(), 1);

  test_addresses = addresses;
  FilterAddresses("ipv6_non_link_local", &test_addresses);
  ASSERT_EQ(test_addresses.size(), 2);

  test_addresses = addresses;
  FilterAddresses("ipv4_external", &test_addresses);
  ASSERT_EQ(test_addresses.size(), 1);

  test_addresses = addresses;
  FilterAddresses("ipv4_external,ipv6_external", &test_addresses);
  ASSERT_EQ(test_addresses.size(), 2);
  ASSERT_TRUE(test_addresses[0].is_v4());
  ASSERT_TRUE(test_addresses[test_addresses.size() - 1].is_v6());

  test_addresses = addresses;
  FilterAddresses("ipv6_external,ipv4_external", &test_addresses);
  ASSERT_EQ(test_addresses.size(), 2);
  ASSERT_TRUE(test_addresses[0].is_v6());
  ASSERT_TRUE(test_addresses[test_addresses.size() - 1].is_v4());
}

} // namespace yb
