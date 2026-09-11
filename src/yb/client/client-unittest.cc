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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
// Tests for the client which are true unit tests and don't require a cluster, etc.

#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client-internal.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"

#include "yb/master/master_client.pb.h"

namespace yb {
namespace client {

using std::string;
using std::vector;

using namespace std::literals;
using namespace std::placeholders;

const std::string kNoPrimaryKeyMessage = "Invalid argument: No primary key specified";

TEST(ClientUnitTest, TestSchemaBuilder_EmptySchema) {
  YBSchema s;
  YBSchemaBuilder b;
  ASSERT_EQ(kNoPrimaryKeyMessage, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_KeyNotSpecified) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->NotNull();
  b.AddColumn("b")->Type(DataType::INT32)->NotNull();
  ASSERT_EQ(kNoPrimaryKeyMessage, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_DuplicateColumn) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("x")->Type(DataType::INT32);
  b.AddColumn("x")->Type(DataType::INT32);
  ASSERT_EQ("Invalid argument: Duplicate column name: x",
            b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_WrongPrimaryKeyOrder) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(DataType::INT32);
  b.AddColumn("x")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("x")->Type(DataType::INT32);
  const char *expected_status =
    "Invalid argument: Primary key column 'x' should be before regular column 'key'";
  ASSERT_EQ(expected_status, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_WrongHashKeyOrder) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->PrimaryKey();
  b.AddColumn("b")->Type(DataType::INT32)->HashPrimaryKey();
  const char *expected_status =
    "Invalid argument: Hash primary key column 'b' should be before primary key 'a'";
  ASSERT_EQ(expected_status, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_SingleKey_GoodSchema) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("b")->Type(DataType::INT32);
  b.AddColumn("c")->Type(DataType::INT32)->NotNull();
  ASSERT_EQ("OK", b.Build(&s).ToString());
}

namespace {

void AddHostPort(
    google::protobuf::RepeatedPtrField<HostPortPB>* list, const std::string& host) {
  auto* hp = list->Add();
  hp->set_host(host);
  hp->set_port(9100);
}

master::TSInfoPB MakeTSInfo(
    const std::vector<std::string>& private_hosts,
    const std::vector<std::string>& broadcast_hosts,
    const std::string& zone) {
  master::TSInfoPB result;
  result.set_permanent_uuid("ts-uuid");
  for (const auto& host : private_hosts) {
    AddHostPort(result.mutable_private_rpc_addresses(), host);
  }
  for (const auto& host : broadcast_hosts) {
    AddHostPort(result.mutable_broadcast_addresses(), host);
  }
  result.mutable_cloud_info()->set_placement_cloud("cloud1");
  result.mutable_cloud_info()->set_placement_region("region1");
  result.mutable_cloud_info()->set_placement_zone(zone);
  return result;
}

CloudInfoPB MakeCloudInfo(const std::string& zone) {
  CloudInfoPB result;
  result.set_placement_cloud("cloud1");
  result.set_placement_region("region1");
  result.set_placement_zone(zone);
  return result;
}

std::vector<std::string> Hosts(const std::vector<HostPort>& host_ports) {
  std::vector<std::string> result;
  for (const auto& hp : host_ports) {
    result.push_back(hp.host());
  }
  return result;
}

} // namespace

// A server is reached at the address use_private_ip selects, and the remaining ways it
// advertised itself are kept so that one unreachable address does not condemn it. The order
// after the first is private before broadcast, which is the direction GetHostPort already
// falls back in.
TEST(ClientUnitTest, RemoteTabletServerCandidateOrder) {
  const auto here = MakeCloudInfo("zone1");

  {
    // Same zone, so use_private_ip=zone would prefer the private address. The default is
    // never, meaning the broadcast address is preferred whenever one was advertised.
    internal::RemoteTabletServer ts(MakeTSInfo({"private1"}, {"broadcast1"}, "zone1"));
    EXPECT_EQ((std::vector<std::string>{"broadcast1", "private1"}),
              Hosts(ts.TEST_Candidates(here)));
  }

  {
    // Nothing was broadcast, so the private address is both the preference and the only
    // candidate, and it must not appear twice.
    internal::RemoteTabletServer ts(MakeTSInfo({"private1"}, {}, "zone1"));
    EXPECT_EQ((std::vector<std::string>{"private1"}), Hosts(ts.TEST_Candidates(here)));
  }

  {
    // Several of each: every advertised address is a candidate, the preferred one first and
    // not repeated, then the rest private before broadcast.
    internal::RemoteTabletServer ts(
        MakeTSInfo({"private1", "private2"}, {"broadcast1", "broadcast2"}, "zone1"));
    EXPECT_EQ(
        (std::vector<std::string>{"broadcast1", "private1", "private2", "broadcast2"}),
        Hosts(ts.TEST_Candidates(here)));
  }
}

} // namespace client
} // namespace yb
