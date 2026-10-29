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
#include <set>

#include "google/protobuf/text_format.h"

#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_protocol_util.h"
#include "yb/common/value.pb.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet_bootstrap_state_manager.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/pb_util.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"

DECLARE_int32(heartbeat_interval_ms);

using namespace yb::size_literals;

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

// yb-pbc-dump takes the schema from the file itself, so it checks the container headers of the
// files that a cluster with a table writes.
TEST_F(PBToolsTestWithMiniCluster, DumpContainerFiles) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  const client::YBTableName table_name{YQL_DATABASE_CQL, "yugabyte", "t"};
  ASSERT_OK(CreateYCQLTable(*client, table_name));
  auto table_info = ASSERT_RESULT(client->GetYBTableInfo(table_name));
  auto table_locations = ASSERT_RESULT(GetTableLocations(*client, table_info.table_id, 5s));
  ASSERT_EQ(table_locations.tablet_locations_size(), 1);
  const auto& tablet_id = table_locations.tablet_locations(0).tablet_id();

  // A log rollover writes the bootstrap state file when it has a write to save.
  client::TableHandle table;
  ASSERT_OK(table.Open(table_name, client.get()));
  auto session = client->NewSession(10s * kTimeMultiplier);
  auto op = table.NewInsertOp(session->arena());
  QLAddInt32HashValue(op->mutable_request(), 1);
  ASSERT_OK(session->TEST_ApplyAndFlush(op));
  ASSERT_OK(cluster_->LogGCOnSingleTServer(0, {tablet_id}, /* rollover= */ true));
  const auto bootstrap_state_path = tablet::TabletBootstrapStateManager::FilePath(JoinPathSegments(
      cluster_->tablet_server(0)->GetRootDir(), "yb-data", "tserver", "wals",
      "table-" + table_info.table_id, "tablet-" + tablet_id));
  ASSERT_OK(WaitFor(
      [&bootstrap_state_path] { return Env::Default()->FileExists(bootstrap_state_path); },
      30s * kTimeMultiplier, "Bootstrap state file"));

  // Otherwise the walk could see a container file that is still being written.
  cluster_->Shutdown();

  std::set<std::string> types;
  // Walk does not return the callback's status.
  std::vector<std::string> failures;
  for (auto* daemon : std::initializer_list<ExternalDaemon*>{
           cluster_->master(0), cluster_->tablet_server(0)}) {
    ASSERT_OK(Env::Default()->Walk(
        daemon->GetRootDir(), Env::PRE_ORDER,
        [&types, &failures](
            Env::FileType type, const std::string& dir, const std::string& name) -> Status {
          if (type != Env::FILE_TYPE) {
            return Status::OK();
          }
          const auto path = JoinPathSegments(dir, name);
          auto status = [&types, &path]() -> Status {
            if (!VERIFY_RESULT(IsPBContainerFile(Env::Default(), path))) {
              return Status::OK();
            }
            auto output = VERIFY_RESULT(CallPBDump(path));
            types.insert(output.stdout.substr(0, output.stdout.find(' ')));
            return Status::OK();
          }();
          if (!status.ok()) {
            failures.push_back(Format("$0: $1", path, status));
          }
          return Status::OK();
        }));
  }
  ASSERT_TRUE(failures.empty()) << AsString(failures);
  for (const auto* type : {
           "yb.AutoFlagsConfigPB", "yb.InstanceMetadataPB", "yb.consensus.ConsensusMetadataPB",
           "yb.consensus.TabletBootstrapStatePB", "yb.tablet.RaftGroupReplicaSuperBlockPB"}) {
    ASSERT_TRUE(types.contains(type)) << type << " is not among dumped types: " << AsString(types);
  }
}

// Every tablet bootstrap reads these files, so a field type from a heavy proto file must not grow
// their container headers back to tens of KB of descriptors.
TEST_F(PBToolsTest, PerTabletContainerHeaderSize) {
  auto header_size = [this](const google::protobuf::Message& msg) -> Result<uint64_t> {
    const auto path = GetTestPath(msg.GetTypeName());
    std::unique_ptr<WritableFile> file;
    RETURN_NOT_OK(Env::Default()->NewWritableFile(path, &file));
    WritablePBContainerFile writer(std::move(file));
    RETURN_NOT_OK(writer.Init(msg));
    RETURN_NOT_OK(writer.Close());
    return Env::Default()->GetFileSize(path);
  };
  ASSERT_LT(ASSERT_RESULT(header_size(tablet::RaftGroupReplicaSuperBlockPB())), 16_KB);
  ASSERT_LT(ASSERT_RESULT(header_size(consensus::ConsensusMetadataPB())), 2_KB);
  ASSERT_LT(ASSERT_RESULT(header_size(consensus::TabletBootstrapStatePB())), 1_KB);
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
