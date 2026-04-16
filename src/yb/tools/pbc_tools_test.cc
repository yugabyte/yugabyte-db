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

#include <future>

#include "google/protobuf/text_format.h"

#include "yb/client/schema.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"

#include "yb/common/value.pb.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/env.h"
#include "yb/util/pb_util.h"
#include "yb/util/random_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"

DECLARE_int32(heartbeat_interval_ms);

using namespace std::chrono_literals;

namespace yb::pb_util {

class PBToolsTest : public YBTest {
};

struct ProcessOutput {
  std::string stdout;
  std::string stderr;
};

Result<ProcessOutput> CallPBDump(
    std::string_view pb_proto_file_path, std::string_view key_id = "",
    std::string_view key_file_path = "");
Result<ProcessOutput> CallPBWrite(
    std::string_view input_file, std::string_view output_file, std::string_view key_id = "",
    std::string_view key_file_path = "");

template <typename ProtoT>
Result<ProtoT> ParsePBDumpOutput(const std::string& s);

TEST_F(PBToolsTest, BinaryToTextToBinary) {
  tablet::RaftGroupReplicaSuperBlockPB superblock;
  superblock.set_primary_table_id("primary_table_id");
  superblock.set_raft_group_id("my_group_id");
  auto path = GetTestPath("proto_binary");
  auto* env = Env::Default();
  ASSERT_OK(WritePBContainerToPath(env, path, superblock, OVERWRITE, SYNC));

  // Validate the dump tool that converts a binary proto file to text.
  auto output = ASSERT_RESULT(CallPBDump(path));
  auto parsed_dump =
      ASSERT_RESULT(ParsePBDumpOutput<tablet::RaftGroupReplicaSuperBlockPB>(output.stdout));
  std::string diff_s;
  ASSERT_TRUE(ArePBsEqual(superblock, parsed_dump, &diff_s)) << "Msg difference string: " << diff_s;

  // Validate the write tool that converts a text proto file to a binary proto file.
  // Modify the text proto dump.
  auto pos = output.stdout.find("my_group_id");
  output.stdout[pos] = 'M';
  auto modified_text_proto_path = GetTestPath("proto_text_modified.txt");
  ASSERT_OK(WriteStringToFile(env, output.stdout, modified_text_proto_path));
  auto modified_proto_path = GetTestPath("modified_proto_binary");
  ASSERT_RESULT(CallPBWrite(modified_text_proto_path, modified_proto_path));
  tablet::RaftGroupReplicaSuperBlockPB modified_superblock;
  ASSERT_OK(ReadPBContainerFromPath(env, modified_proto_path, &modified_superblock));
  tablet::RaftGroupReplicaSuperBlockPB expected_modified_superblock{superblock};
  expected_modified_superblock.set_raft_group_id("My_group_id");
  ASSERT_TRUE(ArePBsEqual(modified_superblock, expected_modified_superblock, &diff_s))
      << "Msg difference string: " << diff_s;
}

class PBToolsTestWithMiniCluster : public YBMiniClusterTestBase<ExternalMiniCluster> {
 protected:
  void SetUp() override;
};

Status EnableEncryption(
    tools::ClusterAdminClient& client, const std::string& key_id, const std::vector<uint8_t>& key);

Result<master::GetTableLocationsResponsePB> GetTableLocations(
    client::YBClient& client, const TableId& table_id, MonoDelta timeout);

Status CreateYCQLTable(client::YBClient& client, const client::YBTableName& table_name);

TEST_F(PBToolsTestWithMiniCluster, EncryptedBinaryToTextToEncryptedBinary) {
  auto admin_client =
      tools::ClusterAdminClient(cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(5));
  ASSERT_OK(admin_client.Init());
  auto key_id = RandomHumanReadableString(16);
  auto key = RandomBytes(32);
  ASSERT_OK(EnableEncryption(admin_client, key_id, key));

  // Wait for the tserver to get the encryption key.
  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_heartbeat_interval_ms));

  auto client = ASSERT_RESULT(cluster_->CreateClient());
  std::string kTableNameS{"foobarbaz"};
  const client::YBTableName kTableName{YQL_DATABASE_CQL, "yugabyte", kTableNameS};
  ASSERT_OK(CreateYCQLTable(*client, kTableName));

  auto yb_table_info = ASSERT_RESULT(client->GetYBTableInfo(kTableName));
  auto table_locations = ASSERT_RESULT(GetTableLocations(*client, yb_table_info.table_id, 5s));
  ASSERT_EQ(table_locations.tablet_locations_size(), 1);
  auto ts = cluster_->tablet_server(0);
  auto tablet_md_path = JoinPathSegments(
      ts->GetRootDir(), "yb-data", "tserver", "tablet-meta",
      table_locations.tablet_locations(0).tablet_id());

  // Write encryption file.
  auto* env = Env::Default();
  yb::Slice key_slice{key.data(), key.size()};
  auto key_file_path = GetTestPath("key_file");
  ASSERT_OK(yb::WriteStringToFile(env, key_slice, key_file_path));

  auto output = ASSERT_RESULT(CallPBDump(tablet_md_path, key_id, key_file_path));

  // Shut down tserver while we modify its tablet metadata file.
  ts->Shutdown(SafeShutdown::kTrue);

  // Modify proto text.
  auto pos = output.stdout.find(kTableNameS);
  ASSERT_NE(pos, std::string::npos) << "Couldn't find table name in proto dump, dump value:\n"
                                    << output.stdout;
  output.stdout[pos] = 'F';

  // Write proto text to an encrypted file.
  auto modified_text_proto_path = GetTestPath("proto_text_modified.txt");
  ASSERT_OK(WriteStringToFile(env, output.stdout, modified_text_proto_path));
  ASSERT_RESULT(CallPBWrite(modified_text_proto_path, tablet_md_path, key_id, key_file_path));

  // Sanity check the written file cannot be read without passing the encryption key.
  auto result = CallPBDump(tablet_md_path);
  ASSERT_FALSE(result.ok()) << "Expected reading the proto to fail without encryption key";
  ASSERT_RESULT(CallPBDump(tablet_md_path, key_id, key_file_path));
  // Sanity check the restarted tserver can read the written tablet metadata file.
  ASSERT_OK(ts->Start());
  {
    tserver::GetTabletStatusRequestPB status_req;
    tserver::GetTabletStatusResponsePB status_resp;
    status_req.set_tablet_id(table_locations.tablet_locations(0).tablet_id());
    auto proxy = cluster_->GetProxy<tserver::TabletServerServiceProxy>(ts);
    rpc::RpcController controller;
    ASSERT_OK(proxy.GetTabletStatus(status_req, &status_resp, &controller));
    ASSERT_EQ(status_resp.tablet_status().table_name(), "Foobarbaz");
  }
}

void PBToolsTestWithMiniCluster::SetUp() {
  YBMiniClusterTestBase::SetUp();
  ExternalMiniClusterOptions options;
  options.replication_factor = 1;
  cluster_ = std::make_unique<ExternalMiniCluster>(options);
  ASSERT_OK(cluster_->Start());
  // Skip verification.
  verify_cluster_before_next_tear_down_ = false;
}

Result<ProcessOutput> CallTool(const std::vector<std::string>& argv) {
  ProcessOutput output;
  RETURN_NOT_OK_PREPEND(
      Subprocess::Call(argv, &output.stdout, &output.stderr),
      Format("Failed to call tool $0, stderr:\n$1", argv[0], output.stderr));
  return output;
}

Result<ProcessOutput> CallPBDump(
    std::string_view pb_proto_file_path, std::string_view key_id, std::string_view key_file_path) {
  auto tool_path = GetToolPath("yb-pbc-dump");
  std::vector<std::string> argv{tool_path, std::string{pb_proto_file_path}};
  if (!key_id.empty() && !key_file_path.empty()) {
    argv.emplace_back(key_file_path);
    argv.emplace_back(key_id);
  }
  return CallTool(argv);
}

Result<ProcessOutput> CallPBWrite(
    std::string_view input_file, std::string_view output_file, std::string_view key_id,
    std::string_view key_file_path) {
  auto tool_path = GetToolPath("yb-pbc-write");
  std::vector<std::string> argv{tool_path, std::string{input_file}, std::string{output_file}};
  if (!key_id.empty() && !key_file_path.empty()) {
    argv.emplace_back(key_file_path);
    argv.emplace_back(key_id);
  }
  return CallTool(argv);
}

template <typename ProtoT>
Result<ProtoT> ParsePBDumpOutput(const std::string& s) {
  // Skip the header, which is the first two lines of `s`. Our caller gives us the expected proto
  // type at compile time through the template parameter.
  auto pos = s.find('\n');
  if (pos != std::string::npos) {
    auto second_pos = s.find('\n', pos + 1);
    if (second_pos != std::string::npos) {
      auto trimmed_s = s.substr(second_pos + 1, s.size() - (second_pos + 1));
      ProtoT proto;
      if (google::protobuf::TextFormat::ParseFromString(trimmed_s, &proto)) {
        return proto;
      } else {
        return STATUS_FORMAT(
            InvalidArgument, "Couldn't parse $0 as proto of type $1", s, proto.GetTypeName());
      }
    }
  }
  return STATUS_FORMAT(InvalidArgument, "Input string does not have 2 line header:\n$0", s);
}

Status EnableEncryption(
    tools::ClusterAdminClient& client, const std::string& key_id, const std::vector<uint8_t>& key) {
  RETURN_NOT_OK(client.AddUniverseKeyToAllMasters(key_id, std::string{key.begin(), key.end()}));
  // need to do a wait loop here?
  RETURN_NOT_OK(client.AllMastersHaveUniverseKeyInMemory(key_id));
  RETURN_NOT_OK(client.RotateUniverseKeyInMemory(key_id));
  return client.IsEncryptionEnabled();
}

Result<master::GetTableLocationsResponsePB> GetTableLocations(
    client::YBClient& client, const TableId& table_id, MonoDelta timeout) {
  auto promise = std::make_shared<std::promise<Result<master::GetTableLocationsResponsePB>>>();
  client.GetTableLocations(
      table_id, 100, RequireTabletsRunning::kTrue, PartitionsOnly::kFalse,
      [&promise](const Result<master::GetTableLocationsResponsePB*>& result) {
        if (result.ok()) {
          promise->set_value(**result);
        } else {
          promise->set_value(result.status());
        }
      });
  auto future = promise->get_future();
  if (future.wait_for(timeout.ToSteadyDuration()) != std::future_status::ready) {
    return STATUS(TimedOut, "Timed out waiting for response");
  }
  return future.get();
}

Status CreateYCQLTable(client::YBClient& client, const client::YBTableName& table_name) {
  auto table_creator = client.NewTableCreator();
  RETURN_NOT_OK(
      client.CreateNamespaceIfNotExists(table_name.namespace_name(), table_name.namespace_type()));
  client::YBSchema schema{Schema{{ColumnSchema{"i", DataType::INT32, ColumnKind::HASH}}}};
  return table_creator->table_name(table_name).schema(&schema).num_tablets(1).wait(true).Create();
}

}  // namespace yb::pb_util
