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

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_op.h"

#include "yb/common/column_id.h"
#include "yb/common/ql_protocol.pb.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

// RepeatedPtrField would NOT call dtor if class is not defined, so need this include below.
#include "yb/master/master_client.pb.h" // for TabletLocationsPB

#include "yb/rpc/messenger.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/random.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"

DECLARE_uint64(max_clock_sync_error_usec);
DECLARE_bool(disable_clock_sync_error);
DECLARE_int32(ht_lease_duration_ms);
DECLARE_string(time_source);

namespace yb {

const std::string kMock = "mock";

class ClockSynchronizationTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  ClockSynchronizationTest() : random_(0) {
    server::HybridClock::RegisterProvider(kMock, mock_clock_.AsProvider());
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ht_lease_duration_ms) = 0;
    SetupFlags();

    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;

    opts.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    client::YBSchemaBuilder b;
    b.AddColumn("key")->Type(DataType::INT64)->NotNull()->HashPrimaryKey();
    b.AddColumn("value")->Type(DataType::INT64)->NotNull();
    CHECK_OK(b.Build(&schema_));

    client_ = ASSERT_RESULT(cluster_->CreateClient());
  }

  void DoTearDown() override {
    client_.reset();
    cluster_->Shutdown();
  }

  void CreateTable() {

    ASSERT_OK(client_->CreateNamespace(kNamespace));

    // Create the table.
    table_name_.reset(new client::YBTableName(YQL_DATABASE_CQL, kNamespace, kTableName));
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(*table_name_.get())
                  .table_type(client::YBTableType::YQL_TABLE_TYPE)
                  .schema(&schema_)
                  .num_tablets(10)
                  .wait(true)
                  .Create());
    ASSERT_OK(client_->OpenTable(*table_name_, &table_));
  }

  void PerformOps(int num_writes_per_tserver) {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(
        client_->GetTablets(*table_name_, 0, &tablets, /* partition_list_version =*/ nullptr));
    auto session = client_->NewSession(MonoDelta::FromSeconds(60));
    for (int i = 0; i < num_writes_per_tserver; i++) {
      auto ql_write = std::make_shared<client::YBqlWriteOp>(table_);
      auto *const req = ql_write->mutable_request();
      req->set_client(QLClient::YQL_CLIENT_CQL);
      req->set_type(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT);
      QLExpressionPB *hash_column = req->add_hashed_column_values();
      int64_t val = random_.Next64();
      hash_column->mutable_value()->set_int64_value(val);

      QLColumnValuePB *column = req->add_column_values();
      column->set_column_id(kFirstColumnId + 1);
      column->mutable_expr()->mutable_value()->set_int64_value(val);
      EXPECT_OK(session->TEST_ApplyAndFlush(ql_write));
      EXPECT_EQ(QLResponsePB::YQL_STATUS_OK, ql_write->response().status());
    }
  }

  virtual void SetupFlags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = "mock";
  }

  std::unique_ptr<client::YBClient> client_;
  client::YBSchema schema_;
  std::unique_ptr<client::YBTableName> table_name_;
  std::shared_ptr<client::YBTable> table_;
  std::unique_ptr<rpc::Messenger> client_messenger_;
  Random random_;
  MockClock mock_clock_;
  constexpr static const char* const kNamespace = "my_namespace";
  constexpr static const char* const kTableName = "my_table";
};

TEST_F(ClockSynchronizationTest, TestClockSkewError) {
  mock_clock_.Set({0, FLAGS_max_clock_sync_error_usec + 1});

  CreateTable();
  PerformOps(100);
}

} // namespace yb
