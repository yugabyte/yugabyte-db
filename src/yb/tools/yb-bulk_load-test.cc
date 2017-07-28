// Copyright (c) YugaByte, Inc.

#include <string>
#include <thread>
#include <gtest/gtest.h>
#include <boost/algorithm/string.hpp>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/partition.h"
#include "yb/common/wire_protocol.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/yql_rocksdb_storage.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/messenger.h"
#include "yb/sql/util/statement_result.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tools/bulk_load_utils.h"
#include "yb/tools/yb-generate_partitions.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/util/date_time.h"
#include "yb/util/path_util.h"
#include "yb/util/random.h"
#include "yb/util/subprocess.h"

DECLARE_uint64(initial_seqno);
DECLARE_uint64(bulk_load_num_files_per_tablet);

namespace yb {
namespace tools {

using client::YBClient;
using client::YBClientBuilder;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTableCreator;
using client::YBTableName;
using client::YBTable;

static const char* const kPartitionToolName = "yb-generate_partitions_main";
static const char* const kBulkLoadToolName = "yb-bulk_load";
static const char* const kNamespace = "bulk_load_test_namespace";
static const char* const kTableName = "my_table";
static constexpr int32_t kNumTablets = 32;
static constexpr int32_t kNumIterations = 10000;
static constexpr int32_t kV2Value = 12345;
static constexpr size_t kV2Index = 5;
static constexpr uint64_t kNumFilesPerTablet = 5;

class YBBulkLoadTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  YBBulkLoadTest() : random_(0) {
  }

  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;

    // Use 3 tservers to test a more realistic scenario.
    opts.num_tablet_servers = 3;

    // Use a high enough initial sequence number.
    FLAGS_initial_seqno = 1 << 20;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(YBClientBuilder()
              .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
              .Build(&client_));

    YBSchemaBuilder b;
    b.AddColumn("hash_key")->Type(INT64)->NotNull()->HashPrimaryKey();
    b.AddColumn("hash_key_timestamp")->Type(TIMESTAMP)->NotNull()->HashPrimaryKey();
    b.AddColumn("hash_key_string")->Type(STRING)->NotNull()->HashPrimaryKey();
    b.AddColumn("range_key")->Type(TIMESTAMP)->NotNull()->PrimaryKey();
    b.AddColumn("v1")->Type(STRING)->NotNull();
    b.AddColumn("v2")->Type(INT32)->NotNull();
    b.AddColumn("v3")->Type(FLOAT)->NotNull();
    b.AddColumn("v4")->Type(DOUBLE)->NotNull();
    CHECK_OK(b.Build(&schema_));

    YBClientBuilder builder;
    ASSERT_OK(cluster_->CreateClient(&builder, &client_));
    rpc::MessengerBuilder bld("Client");
    ASSERT_OK(bld.Build(&client_messenger_));
    proxy_.reset(new master::MasterServiceProxy(client_messenger_,
                                                cluster_->leader_mini_master()->bound_rpc_addr()));

    // Create the namespace.
    ASSERT_OK(client_->CreateNamespace(kNamespace));

    // Create the table.
    table_name_.reset(new YBTableName(kNamespace, kTableName));
    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(*table_name_.get())
          .table_type(client::YBTableType::YQL_TABLE_TYPE)
          .schema(&schema_)
          .num_replicas(1)
          .num_tablets(kNumTablets)
          .wait(true)
          .Create());

    ASSERT_OK(client_->OpenTable(*table_name_, &table_));

    for (int i = 0; i < cluster_->num_masters(); i++) {
      const string& master_address = cluster_->mini_master(i)->bound_rpc_addr_str();
      master_addresses_.push_back(master_address);
    }

    master_addresses_comma_separated_ = boost::algorithm::join(master_addresses_, ",");
    partition_generator_.reset(new YBPartitionGenerator(*table_name_, master_addresses_));
    ASSERT_OK(partition_generator_->Init());
  }

  void DoTearDown() override {
    cluster_->Shutdown();
  }

  CHECKED_STATUS StartProcessAndGetStreams(string exe_path, vector<string> argv, FILE** out,
                                           FILE** in, std::unique_ptr<Subprocess>* process) {
    process->reset(new Subprocess(exe_path, argv));
    (*process)->ShareParentStdout(false);
    RETURN_NOT_OK((*process)->Start());

    *out = fdopen((*process)->ReleaseChildStdinFd(), "w");
    PCHECK(out);
    *in = fdopen((*process)->from_child_stdout_fd(), "r");
    PCHECK(in);
    return Status::OK();
  }

  void CloseStreamsAndWaitForProcess(FILE* out, FILE* in, Subprocess* const process) {
    ASSERT_EQ(0, fclose(out));
    ASSERT_EQ(0, fclose(in));

    int wait_status = 0;
    ASSERT_OK(process->Wait(&wait_status));
    ASSERT_TRUE(WIFEXITED(wait_status));
    ASSERT_EQ(0, WEXITSTATUS(wait_status));
  }

  CHECKED_STATUS CreateYQLReadRequest(const string& row, YQLReadRequestPB* req) {
    req->set_client(YQL_CLIENT_CQL);
    string tablet_id;
    string partition_key;
    CsvTokenizer tokenizer = Tokenize(row);
    RETURN_NOT_OK(partition_generator_->LookupTabletIdWithTokenizer(tokenizer, &tablet_id,
                                                                    &partition_key));
    uint16_t hash_code = PartitionSchema::DecodeMultiColumnHashValue(partition_key);
    req->set_hash_code(hash_code);
    req->set_max_hash_code(hash_code);

    auto it = tokenizer.begin();
    // Set hash columns.
    // hash_key .
    YQLColumnValuePB* hashed_column = req->add_hashed_column_values();
    hashed_column->mutable_expr()->mutable_value()->set_int64_value(
        std::stol(*it++));
    hashed_column->set_column_id(kFirstColumnId);

    // hash_key_timestamp.
    Timestamp ts;
    RETURN_NOT_OK(TimestampFromString(*it++, &ts));
    hashed_column = req->add_hashed_column_values();
    hashed_column->mutable_expr()->mutable_value()->set_timestamp_value(
        ts.ToInt64());
    hashed_column->set_column_id(kFirstColumnId + 1);

    // hash_key_string.
    hashed_column = req->add_hashed_column_values();
    hashed_column->mutable_expr()->mutable_value()->set_string_value(*it++);
    hashed_column->set_column_id(kFirstColumnId + 2);

    // Set range column.
    YQLConditionPB* condition = req->mutable_where_expr()->mutable_condition();
    condition->set_op(YQLOperator::YQL_OP_EQUAL);
    condition->add_operands()->set_column_id(kFirstColumnId + 3);
    RETURN_NOT_OK(TimestampFromString(*it++, &ts));
    condition->add_operands()->mutable_value()->set_timestamp_value(ts.ToInt64());

    // Set all column ids.
    for (int i = 0; i < table_->InternalSchema().num_columns(); i++) {
      req->mutable_column_refs()->add_ids(kFirstColumnId + i);
      req->add_column_ids(kFirstColumnId + i);
    }
    return Status::OK();
  }


  void ValidateRow(const string& row, const YQLRow& yql_row) {
    // Get individual columns.
    CsvTokenizer tokenizer = Tokenize(row);
    auto it = tokenizer.begin();
    Timestamp ts;
    ASSERT_EQ(std::stol(*it++), yql_row.column(0).int64_value());
    ASSERT_OK(TimestampFromString(*it++, &ts));
    ASSERT_EQ(ts, yql_row.column(1).timestamp_value());
    ASSERT_EQ(*it++, yql_row.column(2).string_value());
    ASSERT_OK(TimestampFromString(*it++, &ts));
    ASSERT_EQ(ts, yql_row.column(3).timestamp_value());
    ASSERT_EQ(*it++, yql_row.column(4).string_value());
    ASSERT_EQ(std::stoi(*it++), yql_row.column(5).int32_value());
    ASSERT_FLOAT_EQ(std::stof(*it++), yql_row.column(6).float_value());
    ASSERT_DOUBLE_EQ(std::stold(*it++), yql_row.column(7).double_value());
  }

 protected:

  class LoadGenerator {
   public:
    LoadGenerator(const string& tablet_id, tserver::TabletServerServiceProxy* tserver_proxy)
      : tablet_id_(tablet_id),
        running_(true),
        random_(0),
        tserver_proxy_(tserver_proxy) {
    }

    void RunLoad() {
      while (running_.load()) {
        WriteRow(tablet_id_);
      }
    }

    void StopLoad() {
      running_.store(false);
    }

    void WriteRow(const string& tablet_id) {
      tserver::WriteRequestPB req;
      tserver::WriteResponsePB resp;
      rpc::RpcController controller;
      controller.set_timeout(MonoDelta::FromSeconds(3));

      req.set_tablet_id(tablet_id);
      YQLWriteRequestPB* yql_write = req.add_yql_write_batch();
      YQLColumnValuePB* hash_column = yql_write->add_hashed_column_values();

      hash_column->set_column_id(kFirstColumnId);
      hash_column->mutable_expr()->mutable_value()->set_int64_value(random_.Next64());

      hash_column = yql_write->add_hashed_column_values();
      hash_column->set_column_id(kFirstColumnId + 1);
      hash_column->mutable_expr()->mutable_value()->set_timestamp_value(random_.Next32());

      hash_column = yql_write->add_hashed_column_values();
      hash_column->set_column_id(kFirstColumnId + 2);
      hash_column->mutable_expr()->mutable_value()->set_string_value(
          std::to_string(random_.Next32()));

      YQLColumnValuePB* range_column = yql_write->add_range_column_values();
      range_column->set_column_id(kFirstColumnId + 3);
      range_column->mutable_expr()->mutable_value()->set_timestamp_value(random_.Next64());

      ASSERT_OK(tserver_proxy_->Write(req, &resp, &controller));
      ASSERT_FALSE(resp.has_error());
    }

   private:
    string tablet_id_;
    std::atomic<bool> running_;
    Random random_;
    tserver::TabletServerServiceProxy* tserver_proxy_;
  };

  string GenerateRow(int index) {
    // Build the row and lookup table_id
    string timestamp_string;
    if (index % 2 == 0) {
      // Use string format.
      int year = 1970 + random_.Next32() % 2000;
      int month = 1 + random_.Next32() % 12;
      int day = 1 + random_.Next32() % 28;
      int hour = random_.Next32() % 24;
      int minute = random_.Next32() % 60;
      int second = random_.Next32() % 60;
      timestamp_string = strings::Substitute("$0-$1-$2 $3:$4:$5", year, month, day, hour, minute,
                                             second);
    } else {
      timestamp_string = std::to_string(static_cast<int64_t>(random_.Next32()));
    }

    string row = strings::Substitute(
        "$0,$1,$2,2017-06-17 14:47:00,\"abc,xyz\",$3,3.14,4.1",
        static_cast<int64_t>(random_.Next32()), timestamp_string, random_.Next32(), kV2Value);
    VLOG(1) << "Generated row: " << row;
    return row;
  }

  void VerifyTabletId(const string& tablet_id, master::TabletLocationsPB* tablet_location) {
    // Verify we have the appropriate tablet_id.
    master::GetTabletLocationsRequestPB req;
    req.add_tablet_ids(tablet_id);
    master::GetTabletLocationsResponsePB resp;
    rpc::RpcController controller;
    ASSERT_OK(proxy_->GetTabletLocations(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error());
    ASSERT_EQ(1, resp.tablet_locations_size());
    *tablet_location = resp.tablet_locations(0);
    VLOG(1) << "Got tablet info: " << tablet_location->DebugString();
  }

  void VerifyTabletIdPartitionKey(const string& tablet_id, const string& partition_key) {
    master::TabletLocationsPB tablet_location;
    VerifyTabletId(tablet_id, &tablet_location);
    ASSERT_GE(partition_key, tablet_location.partition().partition_key_start());
    auto partition_key_end = tablet_location.partition().partition_key_end();
    if (!partition_key_end.empty()) {
      ASSERT_LT(partition_key, tablet_location.partition().partition_key_end());
    }
  }

  void PerformRead(const tserver::ReadRequestPB& req,
                   tserver::TabletServerServiceProxy* tserver_proxy,
                   std::unique_ptr<YQLRowBlock>* rowblock) {
    tserver::ReadResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(MonoDelta::FromSeconds(3));
    ASSERT_OK(tserver_proxy->Read(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error());
    ASSERT_EQ(1, resp.yql_batch_size());
    YQLResponsePB yql_resp = resp.yql_batch(0);
    ASSERT_EQ(YQLResponsePB_YQLStatus_YQL_STATUS_OK, yql_resp.status());
    ASSERT_TRUE(yql_resp.has_rows_data_sidecar());

    // Retrieve row.
    Slice rows_data;
    ASSERT_TRUE(controller.finished());
    ASSERT_OK(controller.GetSidecar(yql_resp.rows_data_sidecar(), &rows_data));
    yb::sql::RowsResult rowsResult(*table_name_, schema_.columns(), rows_data.ToBuffer());
    *rowblock = rowsResult.GetRowBlock();
  }

  std::shared_ptr<YBClient> client_;
  YBSchema schema_;
  std::unique_ptr<YBTableName> table_name_;
  std::shared_ptr<YBTable> table_;
  std::unique_ptr<master::MasterServiceProxy> proxy_;
  std::shared_ptr<rpc::Messenger> client_messenger_;
  std::unique_ptr<YBPartitionGenerator> partition_generator_;
  std::vector<std::string> master_addresses_;
  std::string master_addresses_comma_separated_;
  Random random_;
};

TEST_F(YBBulkLoadTest, VerifyPartitions) {
  for (int i = 0; i < kNumIterations; i++) {
    string tablet_id;
    string partition_key;
    ASSERT_OK(partition_generator_->LookupTabletId(GenerateRow(i), &tablet_id, &partition_key));
    VLOG(1) << "Got tablet id: " << tablet_id << ", partition key: " << partition_key;

    VerifyTabletIdPartitionKey(tablet_id, partition_key);
  }
}

TEST_F(YBBulkLoadTest, InvalidLines) {
  string tablet_id;
  string partition_key;
  // Not enough hash columns.
  ASSERT_NOK(partition_generator_->LookupTabletId("1", &tablet_id, &partition_key));

  // Null primary keys.
  ASSERT_NOK(partition_generator_->LookupTabletId("1,\\n", &tablet_id, &partition_key));
  ASSERT_NOK(partition_generator_->LookupTabletId("1,null", &tablet_id, &partition_key));
  ASSERT_NOK(partition_generator_->LookupTabletId("1,NULL", &tablet_id, &partition_key));

  // Invalid types.
  ASSERT_NOK(partition_generator_->LookupTabletId("abc,123", &tablet_id, &partition_key));
  ASSERT_NOK(partition_generator_->LookupTabletId("123,abc", &tablet_id, &partition_key));
  ASSERT_NOK(partition_generator_->LookupTabletId("123.1,123", &tablet_id, &partition_key));
  ASSERT_NOK(partition_generator_->LookupTabletId("123,123.2", &tablet_id, &partition_key));
}

TEST_F(YBBulkLoadTest, TestCLITool) {
  string exe_path = GetToolPath(kPartitionToolName);
  vector<string> argv = {kPartitionToolName, "-master_addresses", master_addresses_comma_separated_,
      "-table_name", kTableName, "-namespace_name", kNamespace};
  FILE *out;
  FILE *in;
  std::unique_ptr<Subprocess> partition_process;
  ASSERT_OK(StartProcessAndGetStreams(exe_path, argv, &out, &in, &partition_process));

  // Write multiple lines.
  vector <string> generated_rows;
  vector <string> mapper_output;
  std::map<string, vector<string>> tabletid_to_line;
  for (int i = 0; i < kNumIterations; i++) {
    // Write the input line.
    string row = GenerateRow(i) + "\n";
    generated_rows.push_back(row);
    ASSERT_GT(fputs(row.c_str(), out), 0);
    ASSERT_EQ(0, fflush(out));

    // Read the output line.
    char buf[1024];
    ASSERT_EQ(buf, fgets(buf, sizeof(buf), in));
    mapper_output.push_back(string(buf));

    // Split based on tab.
    vector<string> tokens;
    boost::split(tokens, buf, boost::is_any_of("\t"));
    ASSERT_EQ(2, tokens.size());
    const string& tablet_id = tokens[0];
    const string& line = tokens[1];
    auto it = tabletid_to_line.find(tablet_id);
    if (it != tabletid_to_line.end()) {
      (*it).second.push_back(line);
    } else {
      tabletid_to_line[tablet_id].push_back(line);
    }

    // Verify tablet id and original line.
    master::TabletLocationsPB tablet_location;
    VerifyTabletId(tablet_id, &tablet_location);
    ASSERT_EQ(generated_rows[i], line);
  }

  CloseStreamsAndWaitForProcess(out, in, partition_process.get());

  // Now lets sort the output and pipe it to the bulk load tool.
  std::sort(mapper_output.begin(), mapper_output.end());

  // Start the bulk load tool.
  string test_dir;
  Env* env = Env::Default();
  ASSERT_OK(env->GetTestDirectory(&test_dir));
  string bulk_load_data = JoinPathSegments(test_dir, "bulk_load_data");
  if (env->FileExists(bulk_load_data)) {
    ASSERT_OK(env->DeleteRecursively(bulk_load_data));
  }
  ASSERT_OK(env->CreateDir(bulk_load_data));

  string bulk_load_exec = GetToolPath(kBulkLoadToolName);
  // -row_batch_size and -flush_batch_for_tests used to ensure we have multiple flushed files per
  // tablet which ensures we would compact some files.
  vector<string> bulk_load_argv = {
      kBulkLoadToolName,
      "-master_addresses", master_addresses_comma_separated_,
      "-table_name", kTableName,
      "-namespace_name", kNamespace,
      "-base_dir", bulk_load_data,
      "-initial_seqno", "0",
      "-row_batch_size", std::to_string(kNumIterations/kNumTablets/10),
      "-bulk_load_num_files_per_tablet", std::to_string(kNumFilesPerTablet),
      "-flush_batch_for_tests"
  };

  std::unique_ptr<Subprocess> bulk_load_process;
  ASSERT_OK(StartProcessAndGetStreams(bulk_load_exec, bulk_load_argv, &out, &in,
                &bulk_load_process));

  for (int i = 0; i < mapper_output.size(); i++) {
    // Write the input line.
    ASSERT_GT(fprintf(out, "%s", mapper_output[i].c_str()), 0);
    ASSERT_EQ(0, fflush(out));
  }

  CloseStreamsAndWaitForProcess(out, in, bulk_load_process.get());

  // Verify we have all tablet ids in the bulk load directory.
  master::GetTableLocationsRequestPB req;
  master::GetTableLocationsResponsePB resp;
  rpc::RpcController controller;

  req.mutable_table()->set_table_name(table_name_->table_name());
  req.mutable_table()->mutable_namespace_()->set_name(kNamespace);
  req.set_max_returned_locations(kNumTablets);
  ASSERT_OK(proxy_->GetTableLocations(req, &resp, &controller));
  ASSERT_FALSE(resp.has_error());
  ASSERT_EQ(kNumTablets, resp.tablet_locations_size());

  for (const master::TabletLocationsPB& tablet_location : resp.tablet_locations()) {
    const string& tablet_id = tablet_location.tablet_id();
    string tablet_path = JoinPathSegments(bulk_load_data, tablet_id);
    ASSERT_TRUE(env->FileExists(tablet_path));

    // Verify atmost 'bulk_load_num_files_per_tablet' files.
    vector <string> tablet_files;
    ASSERT_OK(env->GetChildren(tablet_path, &tablet_files));
    size_t num_files = 0;
    for (const string& tablet_file : tablet_files) {
      if (boost::algorithm::ends_with(tablet_file, ".sst")) {
        num_files++;
      }
    }
    ASSERT_GE(kNumFilesPerTablet, num_files);

    Endpoint leader_tserver;
    for (const master::TabletLocationsPB::ReplicaPB& replica : tablet_location.replicas()) {
      if (replica.role() == consensus::RaftPeerPB_Role::RaftPeerPB_Role_LEADER) {
        HostPort host_port;
        ASSERT_OK(EndpointFromHostPortPB(replica.ts_info().rpc_addresses(0), &leader_tserver));
        break;
      }
    }

    auto tserver_proxy = std::make_unique<tserver::TabletServerServiceProxy>(client_messenger_,
                                                                             leader_tserver);

    // Start the load generator to ensure we can import files with running load.
    LoadGenerator load_generator(tablet_id, tserver_proxy.get());
    std::thread load_thread(&LoadGenerator::RunLoad, &load_generator);
    // Wait for load generator to generate some traffic.
    SleepFor(MonoDelta::FromSeconds(5));

    // Import the data into the tserver.
    tserver::ImportDataRequestPB import_req;
    import_req.set_tablet_id(tablet_id);
    import_req.set_source_dir(tablet_path);
    tserver::ImportDataResponsePB import_resp;
    rpc::RpcController controller;
    ASSERT_OK(tserver_proxy->ImportData(import_req, &import_resp, &controller));
    ASSERT_FALSE(import_resp.has_error()) << import_resp.DebugString();

    for (const string& row : tabletid_to_line[tablet_id]) {
      // Build read request.
      tserver::ReadRequestPB req;
      req.set_tablet_id(tablet_id);
      YQLReadRequestPB* yql_req = req.mutable_yql_batch()->Add();
      ASSERT_OK(CreateYQLReadRequest(row, yql_req));

      std::unique_ptr<YQLRowBlock> rowblock;
      PerformRead(req, tserver_proxy.get(), &rowblock);

      // Validate row.
      ASSERT_EQ(1, rowblock->row_count());
      const YQLRow& yql_row = rowblock->row(0);
      ASSERT_EQ(schema_.num_columns(), yql_row.column_count());
      ValidateRow(row, yql_row);
    }

    // Perform a SELECT * and verify the number of rows present in the tablet is what we expected.
    tserver::ReadRequestPB req;
    tserver::ReadResponsePB resp;
    req.set_tablet_id(tablet_id);
    YQLReadRequestPB* yql_req = req.mutable_yql_batch()->Add();
    YQLConditionPB* condition = yql_req->mutable_where_expr()->mutable_condition();
    condition->set_op(YQLOperator::YQL_OP_EQUAL);
    condition->add_operands()->set_column_id(kFirstColumnId + kV2Index);
    // kV2Value is common across all rows in the tablet and hence we use that value to verify the
    // expected number of rows. Note that since we have a parallel load tester running, we can't
    // validate the total number of rows in the DB.
    condition->add_operands()->mutable_value()->set_int32_value(kV2Value);

    // Set all column ids.
    for (int i = 0; i < table_->InternalSchema().num_columns(); i++) {
      yql_req->mutable_column_refs()->add_ids(kFirstColumnId + i);
      yql_req->add_column_ids(kFirstColumnId + i);
    }

    std::unique_ptr<YQLRowBlock> rowblock;
    PerformRead(req, tserver_proxy.get(), &rowblock);
    ASSERT_EQ(tabletid_to_line[tablet_id].size(), rowblock->row_count());

    // Stop and join load generator.
    load_generator.StopLoad();
    load_thread.join();
  }
}

TEST_F(YBBulkLoadTest, TestCheckedStoild) {
  int32_t int_val;
  ASSERT_OK(CheckedStoi("123", &int_val));
  ASSERT_OK(CheckedStoi("-123", &int_val));
  ASSERT_NOK(CheckedStoi("123.1", &int_val));
  ASSERT_NOK(CheckedStoi("123456789011", &int_val));
  ASSERT_NOK(CheckedStoi("123-abc", &int_val));
  ASSERT_NOK(CheckedStoi("123 123", &int_val));

  int64_t long_val;
  ASSERT_OK(CheckedStol("123", &long_val));
  ASSERT_OK(CheckedStol("-123", &long_val));
  ASSERT_NOK(CheckedStol("123.1", &long_val));
  ASSERT_NOK(CheckedStol("123456789123456789123456789", &long_val));
  ASSERT_NOK(CheckedStol("123 123", &long_val));

  double double_val;
  ASSERT_OK(CheckedStold("123", &double_val));
  ASSERT_OK(CheckedStold("-123", &double_val));
  ASSERT_OK(CheckedStold("123.1", &double_val));
  ASSERT_NOK(CheckedStold("123 123", &double_val));
}

} // namespace tools
} // namespace yb
