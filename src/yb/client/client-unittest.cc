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
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/master_client.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_test_util.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/tcp_stream.h"

#include "yb/server/clock.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/flags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/test_macros.h"

DECLARE_string(use_node_to_node_plaintext);

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

CloudInfoPB Placement(const std::string& zone) {
  CloudInfoPB result;
  result.set_placement_cloud("cloud");
  result.set_placement_region("region");
  result.set_placement_zone(zone);
  return result;
}

master::TSInfoPB TabletServerIn(const std::string& host, const std::string& zone) {
  master::TSInfoPB result;
  result.set_permanent_uuid(host);
  auto* address = result.add_private_rpc_addresses();
  address->set_host(host);
  address->set_port(9100);
  *result.mutable_cloud_info() = Placement(zone);
  return result;
}

} // namespace

// A client reaches a tablet server in plaintext only when it applies use_node_to_node_plaintext.
// One that does not, like the client XClusterRemoteClientHolder builds for another universe, stays
// on the messenger's default transport even toward a server whose placement names match its own.
TEST(ClientUnitTest, NodeToNodePlaintextAppliesOnlyToClientsThatApplyIt) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_plaintext) = "zone";

  // A messenger holding the secure transport, keys generated in process as rpc's SecureTest
  // builds one.
  rpc::SecureContext secure_context(
      rpc::RequireClientCertificate::kFalse, rpc::UseClientCertificate::kFalse);
  ASSERT_OK(secure_context.TEST_GenerateKeys(2048, "127.0.0.1", rpc::MatchingCertKeyPair::kTrue));
  rpc::MessengerBuilder builder("test");
  builder.UseDefaultConnectionContextFactory();
  builder.SetListenProtocol(rpc::SecureStreamProtocol());
  builder.SetUncompressedProtocol(rpc::SecureStreamProtocol());
  builder.AddStreamFactory(
      rpc::SecureStreamProtocol(),
      rpc::SecureStreamFactory(
          rpc::TcpStream::Factory(), MemTracker::GetRootTracker(), &secure_context));
  auto messenger = rpc::CreateAutoShutdownMessengerHolder(ASSERT_RESULT(builder.Build()));
  const auto* encrypted = &messenger->ProtocolFor(rpc::Encrypted::kTrue);
  const auto* unencrypted = &messenger->ProtocolFor(rpc::Encrypted::kFalse);
  ASSERT_NE(encrypted, unencrypted);

  for (bool apply : {false, true}) {
    SCOPED_TRACE(apply ? "applies the flag" : "does not apply the flag");
    auto client = ASSERT_RESULT(YBClientBuilder()
        .add_master_server_addr("127.0.0.1")
        .set_skip_master_leader_resolution(true)
        .set_cloud_info_pb(Placement("zone1"))
        .apply_node_to_node_plaintext(apply)
        .Build(messenger.get()));

    internal::RemoteTabletServer same_zone(TabletServerIn("ts-same-zone", "zone1"));
    internal::RemoteTabletServer other_zone(TabletServerIn("ts-other-zone", "zone2"));
    ASSERT_EQ(
        ASSERT_RESULT(same_zone.ObtainProxy(*client))->proxy().TEST_protocol(),
        apply ? unencrypted : encrypted);
    ASSERT_EQ(
        ASSERT_RESULT(other_zone.ObtainProxy(*client))->proxy().TEST_protocol(), encrypted);
  }
}

} // namespace client
} // namespace yb
