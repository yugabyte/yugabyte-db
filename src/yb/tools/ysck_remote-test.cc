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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tools/data_gen_util.h"
#include "yb/tools/ysck_remote.h"

#include "yb/util/monotime.h"
#include "yb/util/promise.h"
#include "yb/util/random.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using namespace std::literals;

DECLARE_int32(heartbeat_interval_ms);

namespace yb {
namespace tools {

using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableName;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;

static const YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "ysck-test-table");

class RemoteYsckTest : public YBTest {
 public:
  RemoteYsckTest()
    : random_(SeedRandom()) {
    YBSchemaBuilder b;
    b.AddColumn("key")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    b.AddColumn("int_val")->Type(DataType::INT32)->NotNull();
    CHECK_OK(b.Build(&schema_));
  }

  void SetUp() override {
    YBTest::SetUp();

    // Speed up testing, saves about 700ms per TEST_F.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 10;

    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    mini_cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(mini_cluster_->Start());

    master_rpc_addr_ = ASSERT_RESULT(mini_cluster_->GetLeaderMasterBoundRpcAddr());

    // Connect to the cluster.
    client_ = ASSERT_RESULT(mini_cluster_->CreateClient());

    // Create one table.
    ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                  kTableName.namespace_type()));
    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(kTableName)
                     .schema(&schema_)
                     .num_tablets(3)
                     .Create());
    // Make sure we can open the table.
    ASSERT_OK(client_->OpenTable(kTableName, &client_table_));

    ASSERT_OK(RemoteYsckMaster::Build(HostPort(master_rpc_addr_), &master_));
    cluster_.reset(new YsckCluster(master_));
    ysck_.reset(new Ysck(cluster_));
  }

  void TearDown() override {
    client_.reset();
    if (mini_cluster_) {
      mini_cluster_->Shutdown();
      mini_cluster_.reset();
    }
    YBTest::TearDown();
  }

  // Writes rows to the table until the continue_writing flag is set to false.
  //
  // Public for use with std::bind.
  void GenerateRowWritesLoop(CountDownLatch* started_writing,
                             const AtomicBool& continue_writing,
                             Promise<Status>* promise) {
    shared_ptr<YBTable> table;
    Status status = client_->OpenTable(kTableName, &table);
    if (!status.ok()) {
      promise->Set(status);
      return;
    }
    auto session = client_->NewSession(10s);

    for (uint64_t i = 0; continue_writing.Load(); i++) {
      std::shared_ptr<client::YBqlWriteOp> insert(table->NewQLInsert());
      GenerateDataForRow(table->schema(), i, &random_, insert->mutable_request());
      status = session->TEST_ApplyAndFlush(insert);
      if (!status.ok()) {
        promise->Set(status);
        return;
      }
      started_writing->CountDown(1);
    }
    promise->Set(Status::OK());
  }

 protected:
  // Generate a set of split rows for tablets used in this test.
  Status GenerateRowWrites(uint64_t num_rows) {
    shared_ptr<YBTable> table;
    RETURN_NOT_OK(client_->OpenTable(kTableName, &table));
    auto session = client_->NewSession(10s);
    for (uint64_t i = 0; i < num_rows; i++) {
      VLOG(1) << "Generating write for row id " << i;
      std::shared_ptr<client::YBqlWriteOp> insert(table->NewQLInsert());
      GenerateDataForRow(table->schema(), i, &random_, insert->mutable_request());
      session->Apply(insert);

      if (i > 0 && i % 1000 == 0) {
        RETURN_NOT_OK(session->TEST_Flush());
      }
    }
    RETURN_NOT_OK(session->TEST_Flush());
    return Status::OK();
  }

  std::shared_ptr<Ysck> ysck_;
  std::unique_ptr<client::YBClient> client_;

 private:
  HostPort master_rpc_addr_;
  std::shared_ptr<MiniCluster> mini_cluster_;
  client::YBSchema schema_;
  shared_ptr<client::YBTable> client_table_;
  std::shared_ptr<YsckMaster> master_;
  std::shared_ptr<YsckCluster> cluster_;
  Random random_;
};

TEST_F(RemoteYsckTest, TestMasterOk) {
  ASSERT_OK(ysck_->CheckMasterRunning());
}

TEST_F(RemoteYsckTest, TestTabletServersOk) {
  LOG(INFO) << "Fetching table and tablet info...";
  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  LOG(INFO) << "Checking tablet servers are running...";
  ASSERT_OK(ysck_->CheckTabletServersRunning());
}

TEST_F(RemoteYsckTest, TestTableConsistency) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(MonoDelta::FromSeconds(30));
  Status s;
  while (MonoTime::Now().ComesBefore(deadline)) {
    ASSERT_OK(ysck_->FetchTableAndTabletInfo());
    s = ysck_->CheckTablesConsistency();
    if (s.ok()) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  ASSERT_OK(s);
}

TEST_F(RemoteYsckTest, TestChecksum) {
  uint64_t num_writes = 100;
  LOG(INFO) << "Generating row writes...";
  ASSERT_OK(GenerateRowWrites(num_writes));

  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(MonoDelta::FromSeconds(30));
  Status s;
  while (MonoTime::Now().ComesBefore(deadline)) {
    ASSERT_OK(ysck_->FetchTableAndTabletInfo());
    s = ysck_->ChecksumData(vector<string>(),
                            vector<string>(),
                            ChecksumOptions(MonoDelta::FromSeconds(1), 16));
    if (s.ok()) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  ASSERT_OK(s);
}

TEST_F(RemoteYsckTest, TestChecksumTimeout) {
  uint64_t num_writes = 10000;
  LOG(INFO) << "Generating row writes...";
  ASSERT_OK(GenerateRowWrites(num_writes));
  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  // Use an impossibly low timeout value of zero!
  Status s = ysck_->ChecksumData(vector<string>(),
                                 vector<string>(),
                                 ChecksumOptions(MonoDelta::FromNanoseconds(0), 16));
  ASSERT_TRUE(s.IsTimedOut()) << "Expected TimedOut Status, got: " << s.ToString();
}

TEST_F(RemoteYsckTest, TestChecksumSnapshot) {
  CountDownLatch started_writing(1);
  AtomicBool continue_writing(true);
  Promise<Status> promise;
  scoped_refptr<Thread> writer_thread;

  CHECK_OK(
      Thread::Create("RemoteYsckTest",
                     "TestChecksumSnapshot",
                     &RemoteYsckTest::GenerateRowWritesLoop,
                     this,
                     &started_writing,
                     std::cref(continue_writing),
                     &promise,
                     &writer_thread));
  CHECK(started_writing.WaitFor(MonoDelta::FromSeconds(30)));

  uint64_t ts = client_->GetLatestObservedHybridTime();
  MonoTime start(MonoTime::Now());
  MonoTime deadline = start;
  deadline.AddDelta(MonoDelta::FromSeconds(30));
  Status s;
  // TODO: We need to loop here because safe time is not yet implemented.
  // Remove this loop when that is done. See KUDU-1056.
  while (true) {
    ASSERT_OK(ysck_->FetchTableAndTabletInfo());
    Status s = ysck_->ChecksumData(vector<string>(), vector<string>(),
                                   ChecksumOptions(MonoDelta::FromSeconds(10), 16));
    if (s.ok()) break;
    if (deadline.ComesBefore(MonoTime::Now())) break;
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  if (!s.ok()) {
    LOG(WARNING) << Substitute("Timed out after $0 waiting for ysck to become consistent on TS $1. "
                               "Status: $2",
                               MonoTime::Now().GetDeltaSince(start).ToString(),
                               ts, s.ToString());
    EXPECT_OK(s); // To avoid ASAN complaints due to thread reading the CountDownLatch.
  }
  continue_writing.Store(false);
  ASSERT_OK(promise.Get());
  writer_thread->Join();
}

// Test that followers & leader wait until safe time to respond to a snapshot
// scan at current hybrid_time. TODO: Safe time not yet implemented. See KUDU-1056.
TEST_F(RemoteYsckTest, DISABLED_TestChecksumSnapshotCurrentHybridTime) {
  CountDownLatch started_writing(1);
  AtomicBool continue_writing(true);
  Promise<Status> promise;
  scoped_refptr<Thread> writer_thread;

  CHECK_OK(
      Thread::Create("RemoteYsckTest",
                     "TestChecksumSnapshot",
                     &RemoteYsckTest::GenerateRowWritesLoop,
                     this,
                     &started_writing,
                     std::cref(continue_writing),
                     &promise,
                     &writer_thread));
  CHECK(started_writing.WaitFor(MonoDelta::FromSeconds(30)));

  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  ASSERT_OK(ysck_->ChecksumData(vector<string>(), vector<string>(),
                                ChecksumOptions(MonoDelta::FromSeconds(10), 16)));
  continue_writing.Store(false);
  ASSERT_OK(promise.Get());
  writer_thread->Join();
}

}  // namespace tools
}  // namespace yb
