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
      .top_level_node_id{Uuid::Generate()},
      .query_id = RandomUniformInt<uint64_t>(),
      .pid = RandomUniformInt<pid_t>(),
      .database_id = RandomUniformInt<uint32_t>(),
      .rpc_request_id = RandomUniformInt<int64_t>(),
      .client_host_port = RandomHostPort()};
}

void testToAndFromPB() {
  AshMetadata meta1 = GenerateRandomMetadata();
  AshMetadataPB pb;
  meta1.ToPB(&pb);
  ASSERT_EQ(pb.root_request_id().size(), kUuidSize);
  ASSERT_EQ(pb.top_level_node_id().size(), kUuidSize);
  AshMetadata meta2 = AshMetadata::FromPB(pb);
  ASSERT_EQ(meta1.root_request_id, meta2.root_request_id);
  ASSERT_EQ(meta1.top_level_node_id, meta2.top_level_node_id);
  ASSERT_EQ(meta1.query_id, meta2.query_id);
  ASSERT_EQ(meta1.pid, meta2.pid);
  ASSERT_EQ(meta1.database_id, meta2.database_id);
  ASSERT_EQ(meta1.rpc_request_id, meta2.rpc_request_id);
  ASSERT_EQ(meta1.client_host_port, meta2.client_host_port);
}

TEST(WaitStateTest, TestToAndFromPB) {
  testToAndFromPB();
}

TEST(WaitStateTest, TestUpdate) {
  AshMetadata meta1 = GenerateRandomMetadata();
  const AshMetadata meta1_copy = meta1;
  // Update 4 fields, rest unset.
  AshMetadataPB pb1;
  auto pb1_root_request_id = Uuid::Generate();
  pb1_root_request_id.ToBytes(pb1.mutable_root_request_id());
  pb1.set_query_id(RandomUniformInt<uint64_t>());
  pb1.set_pid(RandomUniformInt<pid_t>());
  HostPortToPB(RandomHostPort(), pb1.mutable_client_host_port());
  meta1.UpdateFrom(AshMetadata::FromPB(pb1));
  ASSERT_EQ(meta1.root_request_id, pb1_root_request_id);
  ASSERT_EQ(meta1.top_level_node_id, meta1_copy.top_level_node_id);
  ASSERT_EQ(meta1.query_id, pb1.query_id());
  ASSERT_EQ(meta1.pid, pb1.pid());
  ASSERT_EQ(meta1.database_id, meta1_copy.database_id);
  ASSERT_EQ(meta1.rpc_request_id, meta1_copy.rpc_request_id);
  ASSERT_EQ(meta1.client_host_port, HostPortFromPB(pb1.client_host_port()));

  meta1 = meta1_copy;
  // Update 2 other fields, rest unset.
  AshMetadataPB pb2;
  auto pb2_top_level_node_id = Uuid::Generate();
  pb2_top_level_node_id.ToBytes(pb2.mutable_top_level_node_id());
  pb2.set_rpc_request_id(RandomUniformInt<int64_t>());
  meta1.UpdateFrom(AshMetadata::FromPB(pb2));
  ASSERT_EQ(meta1.root_request_id, meta1_copy.root_request_id);
  ASSERT_EQ(meta1.top_level_node_id, pb2_top_level_node_id);
  ASSERT_EQ(meta1.query_id, meta1_copy.query_id);
  ASSERT_EQ(meta1.pid, meta1_copy.pid);
  ASSERT_EQ(meta1.rpc_request_id, pb2.rpc_request_id());
  ASSERT_EQ(meta1.client_host_port, meta1_copy.client_host_port);
}

}  // namespace yb
