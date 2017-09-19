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
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/rpc/messenger.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/random.h"

DECLARE_uint64(max_clock_sync_error_usec);
DECLARE_bool(use_mock_wall_clock);
DECLARE_bool(disable_clock_sync_error);

namespace yb {

class ClockSynchronizationTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  ClockSynchronizationTest() : random_(0) {
  }

  void SetUp() override {
    FLAGS_use_mock_wall_clock = true;
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;

    opts.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    client::YBSchemaBuilder b;
    b.AddColumn("key")->Type(INT64)->NotNull()->HashPrimaryKey();
    b.AddColumn("value")->Type(INT64)->NotNull();
    CHECK_OK(b.Build(&schema_));

    rpc::MessengerBuilder bld("Client");
    ASSERT_OK(bld.Build(&client_messenger_));
    client::YBClientBuilder builder;
    ASSERT_OK(cluster_->CreateClient(&builder, &client_));
  }

  void DoTearDown() override {
    cluster_->Shutdown();
  }

  void CreateTable() {

    ASSERT_OK(client_->CreateNamespace(kNamespace));

    // Create the table.
    table_name_.reset(new client::YBTableName(kNamespace, kTableName));
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(*table_name_.get())
                  .table_type(client::YBTableType::YQL_TABLE_TYPE)
                  .schema(&schema_)
                  .num_replicas(cluster_->num_tablet_servers())
                  .num_tablets(10)
                  .wait(true)
                  .Create());
    ASSERT_OK(client_->OpenTable(*table_name_, &table_));
  }

  void PerformOps(int num_writes_per_tserver) {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(client_->GetTablets(*table_name_, 0, &tablets));
    std::shared_ptr<client::YBSession> session =  client_->NewSession();
    for (int i = 0; i < num_writes_per_tserver; i++) {
      auto ql_write = std::make_shared<client::YBqlWriteOp>(table_);
      auto *const req = ql_write->mutable_request();
      req->set_client(QLClient::YQL_CLIENT_CQL);
      req->set_type(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT);
      YBPartialRow *prow = ql_write->mutable_row();
      QLColumnValuePB *hash_column = req->add_hashed_column_values();
      int64_t val = random_.Next64();
      hash_column->set_column_id(kFirstColumnId);
      hash_column->mutable_expr()->mutable_value()->set_int64_value(val);

      QLColumnValuePB *column = req->add_column_values();
      column->set_column_id(kFirstColumnId + 1);
      column->mutable_expr()->mutable_value()->set_int64_value(val);
      EXPECT_OK(prow->SetInt64(0, val));
      EXPECT_OK(session->Apply(ql_write));
      EXPECT_EQ(ql_write->response().status(), QLResponsePB::YQL_STATUS_OK);
    }
  }

  std::shared_ptr<client::YBClient> client_;
  client::YBSchema schema_;
  std::unique_ptr<client::YBTableName> table_name_;
  std::shared_ptr<client::YBTable> table_;
  std::shared_ptr<rpc::Messenger> client_messenger_;
  Random random_;
  constexpr static const char* const kNamespace = "my_namespace";
  constexpr static const char* const kTableName = "my_table";
};

#if !defined(__APPLE__)
class MockHybridClockNtpErrors : public server::HybridClock {
 public:

  int NtpAdjtime(timex* timex) {
    ntp_adjtime(timex);
    // Always return error.
    return TIME_ERROR;
  }

  int NtpGettime(ntptimeval* timeval) {
    ntp_gettime(timeval);
    // Always return error.
    return TIME_ERROR;
  }

};
#endif // !defined(__APPLE__)

TEST_F(ClockSynchronizationTest, TestClockSkewError) {
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    server::HybridClock* clock =
        down_cast<server::HybridClock*>(cluster_->mini_tablet_server(i)->server()->Clock());
    clock->SetMockMaxClockErrorForTests(FLAGS_max_clock_sync_error_usec + 1);
  }

  CreateTable();
  PerformOps(100);
}

#if !defined(__APPLE__)
TEST_F(ClockSynchronizationTest, TestNtpErrors) {
  using ::testing::AtLeast;
  using ::testing::Return;
  using ::testing::_;

  FLAGS_use_mock_wall_clock = false;
  for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
    auto* mock_clock = new MockHybridClockNtpErrors();
    EXPECT_OK(mock_clock->Init());
    cluster_->mini_tablet_server(i)->server()->SetClockForTests(mock_clock);
  }

  CreateTable();
  PerformOps(100);
}
#endif // !defined(__APPLE__)

} // namespace yb
