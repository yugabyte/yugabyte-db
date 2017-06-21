// Copyright (c) YugaByte, Inc.

#include <string>
#include <gtest/gtest.h>
#include <boost/algorithm/string.hpp>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/common/partition.h"
#include "yb/common/wire_protocol.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/messenger.h"
#include "yb/tools/yb-generate_partitions.h"
#include "yb/util/date_time.h"
#include "yb/util/path_util.h"
#include "yb/util/random.h"
#include "yb/util/subprocess.h"

namespace yb {
namespace tools {

using client::YBClient;
using client::YBClientBuilder;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTableCreator;
using client::YBTableName;

static const char* const kToolName = "yb-generate_partitions_main";
static const char* const kTableName = "my_table";
static constexpr int32_t kNumTablets = 32;
static constexpr int32_t kNumIterations = 10000;

class YBGeneratePartitionsTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  YBGeneratePartitionsTest() : random_(0) {
  }

  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(YBClientBuilder()
              .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
              .Build(&client_));

    YBSchemaBuilder b;
    b.AddColumn("hash_key")->Type(INT64)->NotNull()->HashPrimaryKey();
    b.AddColumn("hash_key_timestamp")->Type(TIMESTAMP)->NotNull()->HashPrimaryKey();
    b.AddColumn("range_key")->Type(TIMESTAMP)->NotNull()->PrimaryKey();
    b.AddColumn("v1")->Type(STRING)->NotNull();
    b.AddColumn("v2")->Type(INT32)->NotNull();
    b.AddColumn("v3")->Type(FLOAT)->NotNull();
    b.AddColumn("v4")->Type(DOUBLE)->NotNull();
    CHECK_OK(b.Build(&schema_));

    table_name_.reset(new YBTableName(master::kDefaultNamespaceName, kTableName));
    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    CHECK_OK(table_creator->table_name(*table_name_.get())
          .schema(&schema_)
          .num_replicas(1)
          .num_tablets(kNumTablets)
          .wait(true)
          .Create());

    YBClientBuilder builder;
    ASSERT_OK(cluster_->CreateClient(&builder, &client_));
    rpc::MessengerBuilder bld("Client");
    ASSERT_OK(bld.Build(&client_messenger_));
    proxy_.reset(new master::MasterServiceProxy(client_messenger_,
                                                cluster_->leader_mini_master()->bound_rpc_addr()));

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

 protected:
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
        "$0,$1,2017-06-17 14:47:00,abc,12345,3.14,4.1", static_cast<int64_t>(random_.Next32()),
        timestamp_string);
    LOG (INFO) << "Generated row: " << row;
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
    LOG (INFO) << "Got tablet info: " << tablet_location->DebugString();
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

  std::shared_ptr<YBClient> client_;
  YBSchema schema_;
  std::unique_ptr<YBTableName> table_name_;
  std::unique_ptr<master::MasterServiceProxy> proxy_;
  std::shared_ptr<rpc::Messenger> client_messenger_;
  std::unique_ptr<YBPartitionGenerator> partition_generator_;
  std::vector<std::string> master_addresses_;
  std::string master_addresses_comma_separated_;
  Random random_;
};

TEST_F(YBGeneratePartitionsTest, VerifyPartitions) {
  for (int i = 0; i < kNumIterations; i++) {
    string tablet_id;
    string partition_key;
    ASSERT_OK(partition_generator_->LookupTabletId(GenerateRow(i), &tablet_id, &partition_key));
    LOG (INFO) << "Got tablet id: " << tablet_id << ", partition key: " << partition_key;

    VerifyTabletIdPartitionKey(tablet_id, partition_key);
  }
}

TEST_F(YBGeneratePartitionsTest, InvalidLines) {
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

TEST_F(YBGeneratePartitionsTest, TestCLITool) {
  string exe_path = GetToolPath(kToolName);
  vector<string> argv = {kToolName, "-master_addresses", master_addresses_comma_separated_,
      "-table_name", kTableName};
  Subprocess process(exe_path, argv);
  process.ShareParentStdout(false);
  ASSERT_OK(process.Start());

  FILE* out = fdopen(process.ReleaseChildStdinFd(), "w");
  PCHECK(out);
  FILE* in = fdopen(process.from_child_stdout_fd(), "r");
  PCHECK(in);

  // Write multiple lines.
  vector <string> generated_rows;
  for (int i = 0; i < kNumIterations; i++) {
    // Write the input line.
    string row = GenerateRow(i) + "\n";
    generated_rows.push_back(row);
    ASSERT_GT(fprintf(out, "%s", row.c_str()), 0);
    ASSERT_EQ(0, fflush(out));

    // Read the output line.
    char buf[1024];
    ASSERT_EQ(buf, fgets(buf, sizeof(buf), in));

    // Split based on tab.
    vector<string> tokens;
    boost::split(tokens, buf, boost::is_any_of("\t"));
    ASSERT_EQ(2, tokens.size());

    // Verify tablet id and original line.
    master::TabletLocationsPB tablet_location;
    VerifyTabletId(tokens[0], &tablet_location);
    ASSERT_EQ(generated_rows[i], tokens[1]);
  }

  ASSERT_EQ(0, fclose(out));
  ASSERT_EQ(0, fclose(in));

  int wait_status = 0;
  ASSERT_OK(process.Wait(&wait_status));
  ASSERT_TRUE(WIFEXITED(wait_status));
  ASSERT_EQ(0, WEXITSTATUS(wait_status));
}

} // namespace tools
} // namespace yb
