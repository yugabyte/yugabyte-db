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

#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol.pb.h"

#include "yb/util/errno.h"
#include "yb/util/flags.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

DECLARE_bool(node_to_node_encryption_required_on_broadcast);
DECLARE_string(node_to_node_encryption_scope);
DECLARE_string(use_private_ip);

namespace yb {

class WireProtocolTest : public YBTest {
 public:
  WireProtocolTest()
    : schema_({ ColumnSchema("col1", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                ColumnSchema("col2", DataType::STRING),
                ColumnSchema("col3", DataType::UINT32, ColumnKind::VALUE, Nullable::kTrue) }) {
  }

 protected:
  Schema schema_;
};

TEST_F(WireProtocolTest, TestOKStatus) {
  Status s = Status::OK();
  AppStatusPB pb;
  StatusToPB(s, &pb);
  EXPECT_EQ(AppStatusPB::OK, pb.code());
  EXPECT_FALSE(pb.has_message());
  EXPECT_FALSE(pb.has_posix_code());

  Status s2 = StatusFromPB(pb);
  ASSERT_OK(s2);
}

TEST_F(WireProtocolTest, TestBadStatus) {
  Status s = STATUS(NotFound, "foo", "bar");
  AppStatusPB pb;
  StatusToPB(s, &pb);
  EXPECT_EQ(AppStatusPB::NOT_FOUND, pb.code());
  EXPECT_TRUE(pb.has_message());
  EXPECT_EQ("foo: bar", pb.message());
  EXPECT_FALSE(pb.has_posix_code());

  Status s2 = StatusFromPB(pb);
  EXPECT_TRUE(s2.IsNotFound());
  EXPECT_EQ(s.ToString(/* no file/line */ false), s2.ToString(/* no file/line */ false));
}

TEST_F(WireProtocolTest, TestBadStatusWithPosixCode) {
  Status s = STATUS(NotFound, "foo", "bar", Errno(1234));
  AppStatusPB pb;
  StatusToPB(s, &pb);
  EXPECT_EQ(AppStatusPB::NOT_FOUND, pb.code());
  EXPECT_TRUE(pb.has_message());
  EXPECT_EQ("foo: bar", pb.message());
  EXPECT_TRUE(pb.has_posix_code());
  EXPECT_EQ(1234, pb.posix_code());

  Status s2 = StatusFromPB(pb);
  EXPECT_TRUE(s2.IsNotFound());
  EXPECT_EQ(1234, Errno(s2));
  EXPECT_EQ(s.ToString(/* no file/line */ false), s2.ToString(/* no file/line */ false));
}

TEST_F(WireProtocolTest, TestSchemaRoundTrip) {
  google::protobuf::RepeatedPtrField<ColumnSchemaPB> pbs;

  SchemaToColumnPBs(schema_, &pbs);
  ASSERT_EQ(3, pbs.size());

  // Column 0.
  EXPECT_TRUE(pbs.Get(0).is_key());
  EXPECT_EQ("col1", pbs.Get(0).name());
  EXPECT_EQ(PersistentDataType::STRING, pbs.Get(0).type().main());
  EXPECT_FALSE(pbs.Get(0).is_nullable());

  // Column 1.
  EXPECT_FALSE(pbs.Get(1).is_key());
  EXPECT_EQ("col2", pbs.Get(1).name());
  EXPECT_EQ(PersistentDataType::STRING, pbs.Get(1).type().main());
  EXPECT_FALSE(pbs.Get(1).is_nullable());

  // Column 2.
  EXPECT_FALSE(pbs.Get(2).is_key());
  EXPECT_EQ("col3", pbs.Get(2).name());
  EXPECT_EQ(PersistentDataType::UINT32, pbs.Get(2).type().main());
  EXPECT_TRUE(pbs.Get(2).is_nullable());

  // Convert back to a Schema object and verify they're identical.
  Schema schema2;
  ASSERT_OK(ColumnPBsToSchema(pbs, &schema2));
  EXPECT_EQ(schema_.ToString(), schema2.ToString());
  EXPECT_EQ(schema_.num_key_columns(), schema2.num_key_columns());
}

// Test that, when non-contiguous key columns are passed, an error Status
// is returned.
TEST_F(WireProtocolTest, TestBadSchema_NonContiguousKey) {
  google::protobuf::RepeatedPtrField<ColumnSchemaPB> pbs;

  // Column 0: key
  ColumnSchemaPB* col_pb = pbs.Add();
  col_pb->set_name("c0");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(true);

  // Column 1: not a key
  col_pb = pbs.Add();
  col_pb->set_name("c1");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(false);

  // Column 2: marked as key. This is an error.
  col_pb = pbs.Add();
  col_pb->set_name("c2");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(true);

  Schema schema;
  Status s = ColumnPBsToSchema(pbs, &schema);
  ASSERT_STR_CONTAINS(s.ToString(), "Got out-of-order key column");
}

// Test that, when multiple columns with the same name are passed, an
// error Status is returned.
TEST_F(WireProtocolTest, TestBadSchema_DuplicateColumnName) {
  google::protobuf::RepeatedPtrField<ColumnSchemaPB> pbs;

  // Column 0:
  ColumnSchemaPB* col_pb = pbs.Add();
  col_pb->set_name("c0");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(true);

  // Column 1:
  col_pb = pbs.Add();
  col_pb->set_name("c1");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(false);

  // Column 2: same name as column 0
  col_pb = pbs.Add();
  col_pb->set_name("c0");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(false);

  Schema schema;
  Status s = ColumnPBsToSchema(pbs, &schema);
  ASSERT_EQ("Invalid argument: Duplicate column name: c0",
            s.ToString(/* no file/line */ false));
}

namespace {

CloudInfoPB CloudInfo(const std::string& cloud, const std::string& region,
                      const std::string& zone) {
  CloudInfoPB result;
  result.set_placement_cloud(cloud);
  result.set_placement_region(region);
  result.set_placement_zone(zone);
  return result;
}

} // namespace

TEST_F(WireProtocolTest, NodeToNodeEncryptionScope) {
  const auto here = CloudInfo("cloud1", "region1", "zone1");
  const auto same_zone = CloudInfo("cloud1", "region1", "zone1");
  const auto same_region = CloudInfo("cloud1", "region1", "zone2");
  const auto same_cloud = CloudInfo("cloud1", "region2", "zone3");
  const auto elsewhere = CloudInfo("cloud2", "region3", "zone4");

  // Each scope names the boundary within which a destination is exempt. Anything at or
  // beyond that boundary is outside the scope.
  struct {
    const char* scope;
    bool zone, region, cloud, other;
  } cases[] = {
    // scope     same_zone  same_region  same_cloud  elsewhere
    {"zone",     false,     true,        true,       true},
    {"region",   false,     false,       true,       true},
    {"cloud",    false,     false,       false,      true},
    {"never",    true,      true,        true,       true},
  };

  for (const auto& c : cases) {
    google::FlagSaver flag_saver;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_scope) = c.scope;
    SCOPED_TRACE(c.scope);
    EXPECT_EQ(c.zone, UseEncryption(AddressKind::kPrivate, same_zone, here));
    EXPECT_EQ(c.region, UseEncryption(AddressKind::kPrivate, same_region, here));
    EXPECT_EQ(c.cloud, UseEncryption(AddressKind::kPrivate, same_cloud, here));
    EXPECT_EQ(c.other, UseEncryption(AddressKind::kPrivate, elsewhere, here));
  }
}

TEST_F(WireProtocolTest, NodeToNodeEncryptionScopeUnknownPlacement) {
  const auto here = CloudInfo("cloud1", "region1", "zone1");

  // A destination whose placement was never reported matches no node's, so it is outside
  // every scope and its connection stays encrypted.
  for (const auto* scope : {"zone", "region", "cloud", "never"}) {
    google::FlagSaver flag_saver;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_scope) = scope;
    SCOPED_TRACE(scope);
    EXPECT_TRUE(UseEncryption(AddressKind::kPrivate, CloudInfoPB(), here));
  }
}

TEST_F(WireProtocolTest, NodeToNodeEncryptionScopeInvalid) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_scope) = "continent";

  ASSERT_NOK(GetNodeToNodeEncryptionScope());

  // A value that names no scope leaves every destination outside it, so a typo encrypts
  // rather than exposing traffic.
  const auto here = CloudInfo("cloud1", "region1", "zone1");
  EXPECT_TRUE(UseEncryption(AddressKind::kPrivate, here, here));
}

// use_private_ip names a boundary of its own, and a destination inside the encryption scope
// can still sit outside that one, which is what sends the connection out on a broadcast
// address. The exemption is withheld there so narrowing the scope cannot leave a connection
// unencrypted once it has left the destination's private address.
TEST_F(WireProtocolTest, NodeToNodeEncryptionRequiredOnBroadcast) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_scope) = "zone";
  const auto here = CloudInfo("cloud1", "region1", "zone1");

  for (auto kind : {AddressKind::kBroadcast, AddressKind::kConfigured}) {
    SCOPED_TRACE(ToString(kind));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_required_on_broadcast) = true;
    EXPECT_TRUE(UseEncryption(kind, here, here));

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_required_on_broadcast) = false;
    EXPECT_FALSE(UseEncryption(kind, here, here));
  }

  // A connection that stayed on the private address is the scope's to decide either way.
  for (auto required : {true, false}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_required_on_broadcast) = required;
    EXPECT_FALSE(UseEncryption(AddressKind::kPrivate, here, here));
  }
}

// The gate withholds an exemption; it cannot grant one.
TEST_F(WireProtocolTest, NodeToNodeEncryptionRequiredOnBroadcastCannotExempt) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_scope) = "never";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_required_on_broadcast) = false;

  const auto here = CloudInfo("cloud1", "region1", "zone1");
  for (auto kind : {AddressKind::kPrivate, AddressKind::kBroadcast, AddressKind::kConfigured}) {
    SCOPED_TRACE(ToString(kind));
    EXPECT_TRUE(UseEncryption(kind, here, here));
  }
}

// The address and the transport must describe the same connection, so the kind reports which
// list GetHostPort actually returned from, including its fallback when no broadcast address
// was reported.
TEST_F(WireProtocolTest, SelectHostPortReportsTheListItUsed) {
  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_private_ip) = "zone";

  const auto here = CloudInfo("cloud1", "region1", "zone1");
  const auto elsewhere = CloudInfo("cloud2", "region2", "zone2");

  google::protobuf::RepeatedPtrField<HostPortPB> private_addrs;
  auto* p = private_addrs.Add();
  p->set_host("private.example.com");
  p->set_port(9100);

  google::protobuf::RepeatedPtrField<HostPortPB> broadcast_addrs;
  auto* b = broadcast_addrs.Add();
  b->set_host("broadcast.example.com");
  b->set_port(9100);

  auto same_zone = SelectHostPort(broadcast_addrs, private_addrs, here, here);
  EXPECT_EQ(AddressKind::kPrivate, same_zone.kind);
  EXPECT_EQ("private.example.com", same_zone.host_port.host());

  auto other_zone = SelectHostPort(broadcast_addrs, private_addrs, elsewhere, here);
  EXPECT_EQ(AddressKind::kBroadcast, other_zone.kind);
  EXPECT_EQ("broadcast.example.com", other_zone.host_port.host());

  // Outside the scope, but nothing was broadcast, so the private address is what gets used
  // and the kind has to say so or the connection would be encrypted for the wrong reason.
  google::protobuf::RepeatedPtrField<HostPortPB> no_broadcast;
  auto fallback = SelectHostPort(no_broadcast, private_addrs, elsewhere, here);
  EXPECT_EQ(AddressKind::kPrivate, fallback.kind);
  EXPECT_EQ("private.example.com", fallback.host_port.host());
}

} // namespace yb
