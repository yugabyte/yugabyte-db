// Copyright (c) YugabyteDB, Inc.
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

#include "yb/ash/wait_state.h"

#include "yb/common/common.pb.h"

#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

namespace yb {

using yb::ash::AshAuxInfo;
using yb::ash::AshMetadata;

class WaitStateTest : public YBTest {};

HostPort RandomHostPort() {
  return HostPort(
      Format("host-$0", RandomUniformInt<uint16_t>(0, 10)), RandomUniformInt<uint16_t>());
}

AshMetadata GenerateRandomMetadata() {
  return AshMetadata{
      .root_request_id{Uuid::Generate()},
      .yql_endpoint_tserver_uuid{Uuid::Generate()},
      .query_id = RandomUniformInt<uint64_t>(),
      .session_id = RandomUniformInt<uint64_t>(),
      .rpc_request_id = RandomUniformInt<int64_t>(),
      .client_host_port = RandomHostPort()};
}

void testToAndFromPB(bool use_hex) {
  // If using Hex, we need to use 2 hex characters to represent 1 byte of uuid.
  const size_t kInflationFactor = (use_hex ? 2 : 1);
  AshMetadata meta1 = GenerateRandomMetadata();
  AshMetadataPB pb;
  meta1.ToPB(&pb, use_hex);
  ASSERT_EQ(pb.root_request_id().size(), kInflationFactor * kUuidSize);
  ASSERT_EQ(pb.yql_endpoint_tserver_uuid().size(), kInflationFactor * kUuidSize);
  AshMetadata meta2 = AshMetadata::FromPB(pb, use_hex);
  ASSERT_EQ(meta1.root_request_id, meta2.root_request_id);
  ASSERT_EQ(meta1.yql_endpoint_tserver_uuid, meta2.yql_endpoint_tserver_uuid);
  ASSERT_EQ(meta1.query_id, meta2.query_id);
  ASSERT_EQ(meta1.session_id, meta2.session_id);
  ASSERT_EQ(meta1.rpc_request_id, meta2.rpc_request_id);
  ASSERT_EQ(meta1.client_host_port, meta2.client_host_port);
}

TEST(WaitStateTest, TestToAndFromPB) {
  testToAndFromPB(/* use_hex */ false);
}

TEST(WaitStateTest, TestToAndFromPBHex) {
  testToAndFromPB(/* use_hex */ true);
}

TEST(WaitStateTest, TestUpdate) {
  AshMetadata meta1 = GenerateRandomMetadata();
  const AshMetadata meta1_copy = meta1;
  // Update 4 fields, rest unset.
  AshMetadataPB pb1;
  auto pb1_root_request_id = Uuid::Generate();
  pb1_root_request_id.ToBytes(pb1.mutable_root_request_id());
  pb1.set_query_id(RandomUniformInt<uint64_t>());
  pb1.set_session_id(RandomUniformInt<uint64_t>());
  HostPortToPB(RandomHostPort(), pb1.mutable_client_host_port());
  meta1.UpdateFrom(AshMetadata::FromPB(pb1, /* use_hex */ false));
  ASSERT_EQ(meta1.root_request_id, pb1_root_request_id);
  ASSERT_EQ(meta1.yql_endpoint_tserver_uuid, meta1_copy.yql_endpoint_tserver_uuid);
  ASSERT_EQ(meta1.query_id, pb1.query_id());
  ASSERT_EQ(meta1.session_id, pb1.session_id());
  ASSERT_EQ(meta1.rpc_request_id, meta1_copy.rpc_request_id);
  ASSERT_EQ(meta1.client_host_port, HostPortFromPB(pb1.client_host_port()));

  meta1 = meta1_copy;
  // Update 2 other fields, rest unset.
  AshMetadataPB pb2;
  auto pb2_yql_endpoint_tserver_uuid = Uuid::Generate();
  pb2_yql_endpoint_tserver_uuid.ToBytes(pb2.mutable_yql_endpoint_tserver_uuid());
  pb2.set_rpc_request_id(RandomUniformInt<int64_t>());
  meta1.UpdateFrom(AshMetadata::FromPB(pb2, /* use_hex */ false));
  ASSERT_EQ(meta1.root_request_id, meta1_copy.root_request_id);
  ASSERT_EQ(meta1.yql_endpoint_tserver_uuid, pb2_yql_endpoint_tserver_uuid);
  ASSERT_EQ(meta1.query_id, meta1_copy.query_id);
  ASSERT_EQ(meta1.session_id, meta1_copy.session_id);
  ASSERT_EQ(meta1.rpc_request_id, pb2.rpc_request_id());
  ASSERT_EQ(meta1.client_host_port, meta1_copy.client_host_port);
}

}  // namespace yb
