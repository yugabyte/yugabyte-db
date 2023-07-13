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

#include <string>
#include <thread>

#include <boost/algorithm/string.hpp>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/jsonb.h"
#include "yb/dockv/partition.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_client.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tools/bulk_load_utils.h"
#include "yb/tools/yb-generate_partitions.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/path_util.h"
#include "yb/util/random.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/subprocess.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/cql/ql/util/statement_result.h"

using std::string;
using std::vector;

DECLARE_uint64(initial_seqno);
DECLARE_uint64(bulk_load_num_files_per_tablet);
DECLARE_bool(enable_load_balancing);
DECLARE_int32(replication_factor);

using namespace std::literals;

namespace yb {
namespace tools {

using client::YBClient;
using client::YBClientBuilder;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTableCreator;
using client::YBTableName;
using client::YBTable;
using common::Jsonb;

static const char* const kPartitionToolName = "yb-generate_partitions_main";
static const char* const kBulkLoadToolName = "yb-bulk_load";
static const char* const kNamespace = "bulk_load_test_namespace";
static const char* const kTableName = "my_table";
// Lower number of runs for tsan due to low perf.
static constexpr int32_t kNumIterations = NonTsanVsTsan(10000, 30);
static constexpr int32_t kNumTablets = NonTsanVsTsan(3, 3);
static constexpr int32_t kNumTabletServers = 1;
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

    opts.num_tablet_servers = kNumTabletServers;

    // Use a high enough initial sequence number.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_initial_seqno) = 1 << 20;

    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    client_ = ASSERT_RESULT(YBClientBuilder()
        .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
        .Build());

    YBSchemaBuilder b;
    b.AddColumn("hash_key")->Type(DataType::INT64)->NotNull()->HashPrimaryKey();
    b.AddColumn("hash_key_timestamp")->Type(DataType::TIMESTAMP)->NotNull()->HashPrimaryKey();
    b.AddColumn("hash_key_string")->Type(DataType::STRING)->NotNull()->HashPrimaryKey();
    b.AddColumn("range_key")->Type(DataType::TIMESTAMP)->NotNull()->PrimaryKey();
    b.AddColumn("v1")->Type(DataType::STRING)->NotNull();
    b.AddColumn("v2")->Type(DataType::INT32)->NotNull();
    b.AddColumn("v3")->Type(DataType::FLOAT)->NotNull();
    b.AddColumn("v4")->Type(DataType::DOUBLE)->NotNull();
    b.AddColumn("v5")->Type(DataType::JSONB)->Nullable();
    CHECK_OK(b.Build(&schema_));

    client_ = ASSERT_RESULT(cluster_->CreateClient());
    client_messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("Client").Build());
    rpc::ProxyCache proxy_cache(client_messenger_.get());
    proxy_ = std::make_unique<master::MasterClientProxy>(
        &proxy_cache, ASSERT_RESULT(cluster_->GetLeaderMasterBoundRpcAddr()));

    // Create the namespace.
    ASSERT_OK(client_->CreateNamespace(kNamespace));

    // Create the table.
    table_name_.reset(new YBTableName(YQL_DATABASE_CQL, kNamespace, kTableName));
    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(*table_name_.get())
          .table_type(client::YBTableType::YQL_TABLE_TYPE)
          .schema(&schema_)
          .num_tablets(kNumTablets)
          .wait(true)
          .Create());

    ASSERT_OK(client_->OpenTable(*table_name_, &table_));

    for (size_t i = 0; i < cluster_->num_masters(); i++) {
      const string& master_address = cluster_->mini_master(i)->bound_rpc_addr_str();
      master_addresses_.push_back(master_address);
    }

    master_addresses_comma_separated_ = boost::algorithm::join(master_addresses_, ",");
    partition_generator_.reset(new YBPartitionGenerator(*table_name_, master_addresses_));
    ASSERT_OK(partition_generator_->Init());
  }

  void DoTearDown() override {
    client_messenger_->Shutdown();
    client_.reset();
    cluster_->Shutdown();
  }

  Status StartProcessAndGetStreams(string exe_path, vector<string> argv, FILE** out,
                                   FILE** in, std::unique_ptr<Subprocess>* process) {
    process->reset(new Subprocess(exe_path, argv));
    (*process)->PipeParentStdout();
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

  Status CreateQLReadRequest(const string& row, QLReadRequestPB* req) {
    req->set_client(YQL_CLIENT_CQL);
    string tablet_id;
    string partition_key;
    CsvTokenizer tokenizer = Tokenize(row);
    RETURN_NOT_OK(partition_generator_->LookupTabletIdWithTokenizer(
        tokenizer, {}, &tablet_id, &partition_key));
    uint16_t hash_code = dockv::PartitionSchema::DecodeMultiColumnHashValue(partition_key);
    req->set_hash_code(hash_code);
    req->set_max_hash_code(hash_code);

    auto it = tokenizer.begin();
    // Set hash columns.
    // hash_key .
    QLExpressionPB* hashed_column = req->add_hashed_column_values();
    hashed_column->mutable_value()->set_int64_value(std::stol(*it++));

    // hash_key_timestamp.
    auto ts = TimestampFromString(*it++);
    RETURN_NOT_OK(ts);
    hashed_column = req->add_hashed_column_values();
    hashed_column->mutable_value()->set_timestamp_value(ts->ToInt64());

    // hash_key_string.
    hashed_column = req->add_hashed_column_values();
    hashed_column->mutable_value()->set_string_value(*it++);

    // Set range column.
    QLConditionPB* condition = req->mutable_where_expr()->mutable_condition();
    condition->set_op(QLOperator::QL_OP_EQUAL);
    condition->add_operands()->set_column_id(kFirstColumnId + 3);
    RETURN_NOT_OK(ts = TimestampFromString(*it++));
    condition->add_operands()->mutable_value()->set_timestamp_value(ts->ToInt64());

    // Set all column ids.
    QLRSRowDescPB *rsrow_desc = req->mutable_rsrow_desc();
    for (size_t i = 0; i < table_->InternalSchema().num_columns(); i++) {
      req->mutable_column_refs()->add_ids(narrow_cast<int32_t>(kFirstColumnId + i));
      req->add_selected_exprs()->set_column_id(narrow_cast<int32_t>(kFirstColumnId + i));

      const ColumnSchema& col = table_->InternalSchema().column(i);
      QLRSColDescPB *rscol_desc = rsrow_desc->add_rscol_descs();
      rscol_desc->set_name(col.name());
      col.type()->ToQLTypePB(rscol_desc->mutable_ql_type());
    }
    return Status::OK();
  }


  void ValidateRow(const string& row, const qlexpr::QLRow& ql_row) {
    // Get individual columns.
    CsvTokenizer tokenizer = Tokenize(row);
    auto it = tokenizer.begin();
    ASSERT_EQ(std::stol(*it++), ql_row.column(0).int64_value());
    auto ts = TimestampFromString(*it++);
    ASSERT_OK(ts);
    ASSERT_EQ(*ts, ql_row.column(1).timestamp_value());
    ASSERT_EQ(*it++, ql_row.column(2).string_value());
    ASSERT_OK(ts = TimestampFromString(*it++));
    ASSERT_EQ(*ts, ql_row.column(3).timestamp_value());
    ASSERT_EQ(*it++, ql_row.column(4).string_value());
    ASSERT_EQ(std::stoi(*it++), ql_row.column(5).int32_value());
    ASSERT_FLOAT_EQ(std::stof(*it++), ql_row.column(6).float_value());
    ASSERT_DOUBLE_EQ(std::stold(*it++), ql_row.column(7).double_value());
    string token_str = *it++;
    if (IsNull(token_str)) {
      ASSERT_TRUE(ql_row.column(8).IsNull());
    } else {
      Jsonb jsonb_from_token;
      CHECK_OK(jsonb_from_token.FromString(token_str));
      ASSERT_EQ(jsonb_from_token.SerializedJsonb(), ql_row.column(8).jsonb_value());
    }
  }

 protected:

  class LoadGenerator {
   public:
    LoadGenerator(const string& tablet_id, const client::TableHandle* table,
                  tserver::TabletServerServiceProxy* tserver_proxy)
      : tablet_id_(tablet_id),
        table_(table),
        tserver_proxy_(tserver_proxy) {
    }

    void RunLoad() {
      while (running_.load()) {
        ASSERT_NO_FATALS(WriteRow(tablet_id_));
      }
    }

    void StopLoad() {
      running_.store(false);
    }

    void WriteRow(const string& tablet_id) {
      tserver::WriteRequestPB req;
      tserver::WriteResponsePB resp;
      rpc::RpcController controller;
      controller.set_timeout(15s);

      req.set_tablet_id(tablet_id);
      QLWriteRequestPB* ql_write = req.add_ql_write_batch();
      QLAddInt64HashValue(ql_write, random_.Next64());
      QLAddTimestampHashValue(ql_write, random_.Next32());
      QLAddStringHashValue(ql_write, std::to_string(random_.Next32()));
      QLSetHashCode(ql_write);

      QLAddTimestampRangeValue(ql_write, random_.Next64());

      table_->AddStringColumnValue(ql_write, "v1", "");
      table_->AddInt32ColumnValue(ql_write, "v2", 0);
      table_->AddFloatColumnValue(ql_write, "v3", 0);
      table_->AddDoubleColumnValue(ql_write, "v4", 0);
      table_->AddJsonbColumnValue(ql_write, "v5", "{ \"a\" : \"foo\" , \"b\" : \"bar\" }");

      auto status = tserver_proxy_->Write(req, &resp, &controller);
      ASSERT_TRUE(status.ok() || status.IsTimedOut()) << "Bad status: " << status;
      ASSERT_FALSE(resp.has_error()) << "Resp: " << resp.ShortDebugString();
    }

   private:
    string tablet_id_;
    const client::TableHandle* table_;
    tserver::TabletServerServiceProxy* tserver_proxy_;

    std::atomic<bool> running_{true};
    Random random_{0};
  };

  string GenerateRow(int index) {
    // Build the row and lookup table_id
    string timestamp_string;
    string json;
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
      json = "\"{\\\"a\\\":\\\"foo\\\",\\\"b\\\":\\\"bar\\\"}\"";
    } else {
      timestamp_string = std::to_string(static_cast<int64_t>(random_.Next32()));
      json = "\\\\n"; // represents null value.
    }

    string row = strings::Substitute(
        "$0,$1,$2,2017-06-17 14:47:00,\"abc,xyz\",$3,3.14,4.1,$4",
        static_cast<int64_t>(random_.Next32()), timestamp_string, random_.Next32(), kV2Value, json);
    VLOG(1) << "Generated row: " << row;
    return row;
  }

  void VerifyTabletId(const string& tablet_id, master::TabletLocationsPB* tablet_location) {
    // Verify we have the appropriate tablet_id.
    master::GetTabletLocationsRequestPB req;
    req.add_tablet_ids(tablet_id);
    master::GetTabletLocationsResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(60s);
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
                   std::unique_ptr<qlexpr::QLRowBlock>* rowblock) {
    tserver::ReadResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(15s);
    ASSERT_OK(tserver_proxy->Read(req, &resp, &controller));
    ASSERT_FALSE(resp.has_error());
    ASSERT_EQ(1, resp.ql_batch_size());
    QLResponsePB ql_resp = resp.ql_batch(0);
    ASSERT_EQ(QLResponsePB_QLStatus_YQL_STATUS_OK, ql_resp.status())
        << "Response: " << ql_resp.ShortDebugString();
    ASSERT_TRUE(ql_resp.has_rows_data_sidecar());

    // Retrieve row.
    ASSERT_TRUE(controller.finished());
    auto rows_data = ASSERT_RESULT(controller.ExtractSidecar(ql_resp.rows_data_sidecar()));
    auto columns = std::make_shared<std::vector<ColumnSchema>>(schema_.columns());
    ql::RowsResult rowsResult(*table_name_, columns, rows_data);
    *rowblock = rowsResult.GetRowBlock();
  }

  std::unique_ptr<YBClient> client_;
  YBSchema schema_;
  std::unique_ptr<YBTableName> table_name_;
  std::shared_ptr<YBTable> table_;
  std::unique_ptr<master::MasterClientProxy> proxy_;
  std::unique_ptr<rpc::Messenger> client_messenger_;
  std::unique_ptr<YBPartitionGenerator> partition_generator_;
  std::vector<std::string> master_addresses_;
  std::string master_addresses_comma_separated_;
  Random random_;
};

class YBBulkLoadTestWithoutRebalancing : public YBBulkLoadTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    YBBulkLoadTest::SetUp();
  }
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

TEST_F(YBBulkLoadTest, VerifyPartitionsWithIgnoredColumns) {
  const std::set<int> skipped_cols = tools::SkippedColumns("0,9");
  for (int i = 0; i < kNumIterations; i++) {
    string tablet_id;
    string partition_key;
    string row = GenerateRow(i);
    ASSERT_OK(partition_generator_->LookupTabletId(row, &tablet_id, &partition_key));
    VLOG(1) << "Got tablet id: " << tablet_id << ", partition key: " << partition_key;

    VerifyTabletIdPartitionKey(tablet_id, partition_key);

    string tablet_id2;
    string partition_key2;
    string row_with_extras = "foo," + row + ",bar";
    ASSERT_OK(partition_generator_->LookupTabletId(
        row_with_extras, skipped_cols, &tablet_id2, &partition_key2));
    ASSERT_EQ(tablet_id, tablet_id2);
    ASSERT_EQ(partition_key, partition_key2);
  }
}

TEST_F(YBBulkLoadTest, TestTokenizer) {
  {
    // JSON needs to be enclosed in quotes, so that the internal commas are not treated as different
    // columns. Need to escape the quotes within to ensure that they are not eaten up.
    string str ="1,2017-06-17 14:47:00,\"abc,;xyz\","
                 "\"{\\\"a\\\":\\\"foo\\\",\\\"b\\\":\\\"bar\\\"}\"";
    CsvTokenizer tokenizer = Tokenize(str);
    auto it = tokenizer.begin();
    ASSERT_EQ(*it++, "1");
    ASSERT_EQ(*it++, "2017-06-17 14:47:00");
    ASSERT_EQ(*it++, "abc,;xyz");
    ASSERT_EQ(*it++, "{\"a\":\"foo\",\"b\":\"bar\"}");
    ASSERT_EQ(it, tokenizer.end());
  }

  {
    // Separating fields with ';'. No need to enclose JSON in quotes. Internal quotes still need
    // to be escapted to prevent being consumed.
    string str ="1;2017-06-17 14:47:00;\"abc,;xyz\";"
                "{\\\"a\\\":\\\"foo\\\",\\\"b\\\":\\\"bar\\\"}";
    CsvTokenizer tokenizer = Tokenize(str, ';', '\"');
    auto it = tokenizer.begin();
    ASSERT_EQ(*it++, "1");
    ASSERT_EQ(*it++, "2017-06-17 14:47:00");
    ASSERT_EQ(*it++, "abc,;xyz");
    ASSERT_EQ(*it++, "{\"a\":\"foo\",\"b\":\"bar\"}");
    ASSERT_EQ(it, tokenizer.end());
  }

  {
    string str ="1,2017-06-17 14:47:00,'abc,;xyz',"
                 "'{\"a\":\"foo\",\"b\":\"bar\"}'";
    // No need to escape quotes because the quote character is \'
    CsvTokenizer tokenizer = Tokenize(str, ',', '\'');
    auto it = tokenizer.begin();
    ASSERT_EQ(*it++, "1");
    ASSERT_EQ(*it++, "2017-06-17 14:47:00");
    ASSERT_EQ(*it++, "abc,;xyz");
    ASSERT_EQ(*it++, "{\"a\":\"foo\",\"b\":\"bar\"}");
    ASSERT_EQ(it, tokenizer.end());
  }
  {
    string str ="1,2017-06-17 14:47:00,'abc,;xyz',"
                "\\\\n";
    // No need to escape quotes because the quote character is \'
    CsvTokenizer tokenizer = Tokenize(str, ',', '\'');
    auto it = tokenizer.begin();
    ASSERT_EQ(*it++, "1");
    ASSERT_EQ(*it++, "2017-06-17 14:47:00");
    ASSERT_EQ(*it++, "abc,;xyz");
    ASSERT_EQ(*it++, "\\n");
    ASSERT_EQ(it, tokenizer.end());
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

TEST_F_EX(YBBulkLoadTest, TestCLITool, YBBulkLoadTestWithoutRebalancing) {
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
    ASSERT_EQ(generated_rows[i], tokens[1]);
    ASSERT_EQ(tokens[1][tokens[1].length() -1], '\n');
    boost::trim_right(tokens[1]); // remove the trailing '\n'
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
      "-flush_batch_for_tests",
      "-never_fsync", "true"
  };

  std::unique_ptr<Subprocess> bulk_load_process;
  ASSERT_OK(StartProcessAndGetStreams(bulk_load_exec, bulk_load_argv, &out, &in,
                &bulk_load_process));

  for (size_t i = 0; i < mapper_output.size(); i++) {
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
  client::TableHandle table;
  ASSERT_OK(table.Open(*table_name_, client_.get()));

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

    HostPort leader_tserver;
    for (const master::TabletLocationsPB::ReplicaPB& replica : tablet_location.replicas()) {
      if (replica.role() == PeerRole::LEADER) {
        leader_tserver = HostPortFromPB(replica.ts_info().private_rpc_addresses(0));
        break;
      }
    }

    rpc::ProxyCache proxy_cache(client_messenger_.get());
    auto tserver_proxy = std::make_unique<tserver::TabletServerServiceProxy>(&proxy_cache,
                                                                             leader_tserver);

    // Start the load generator to ensure we can import files with running load.
    LoadGenerator load_generator(tablet_id, &table, tserver_proxy.get());
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
      QLReadRequestPB* ql_req = req.mutable_ql_batch()->Add();
      ASSERT_OK(CreateQLReadRequest(row, ql_req));

      std::unique_ptr<qlexpr::QLRowBlock> rowblock;
      PerformRead(req, tserver_proxy.get(), &rowblock);

      // Validate row.
      ASSERT_EQ(1, rowblock->row_count());
      const auto& ql_row = rowblock->row(0);
      ASSERT_EQ(schema_.num_columns(), ql_row.column_count());
      ValidateRow(row, ql_row);
    }

    // Perform a SELECT * and verify the number of rows present in the tablet is what we expected.
    tserver::ReadRequestPB req;
    tserver::ReadResponsePB resp;
    req.set_tablet_id(tablet_id);
    QLReadRequestPB* ql_req = req.mutable_ql_batch()->Add();
    QLConditionPB* condition = ql_req->mutable_where_expr()->mutable_condition();
    condition->set_op(QLOperator::QL_OP_EQUAL);
    condition->add_operands()->set_column_id(kFirstColumnId + kV2Index);
    // kV2Value is common across all rows in the tablet and hence we use that value to verify the
    // expected number of rows. Note that since we have a parallel load tester running, we can't
    // validate the total number of rows in the DB.
    condition->add_operands()->mutable_value()->set_int32_value(kV2Value);

    // Set all column ids.
    QLRSRowDescPB *rsrow_desc = ql_req->mutable_rsrow_desc();
    for (size_t i = 0; i < table_->InternalSchema().num_columns(); i++) {
      ql_req->mutable_column_refs()->add_ids(narrow_cast<int32_t>(kFirstColumnId + i));
      ql_req->add_selected_exprs()->set_column_id(narrow_cast<int32_t>(kFirstColumnId + i));

      const ColumnSchema& col = table_->InternalSchema().column(i);
      QLRSColDescPB *rscol_desc = rsrow_desc->add_rscol_descs();
      rscol_desc->set_name(col.name());
      col.type()->ToQLTypePB(rscol_desc->mutable_ql_type());
    }

    std::unique_ptr<qlexpr::QLRowBlock> rowblock;
    PerformRead(req, tserver_proxy.get(), &rowblock);
    ASSERT_EQ(tabletid_to_line[tablet_id].size(), rowblock->row_count());

    // Stop and join load generator.
    load_generator.StopLoad();
    load_thread.join();
  }
}

} // namespace tools
} // namespace yb
