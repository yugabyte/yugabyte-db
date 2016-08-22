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

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"
#include "yb/tools/data_gen_util.h"
#include "yb/tools/ysck_remote.h"
#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/test_util.h"

DECLARE_int32(heartbeat_interval_ms);

namespace yb {
namespace tools {

using client::YBColumnSchema;
using client::YBInsert;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTable;
using client::YBTableCreator;
using client::sp::shared_ptr;
using std::static_pointer_cast;
using std::string;
using std::vector;
using strings::Substitute;

static const char *kTableName = "ysck-test-table";

class RemoteYsckTest : public YBTest {
 public:
  RemoteYsckTest()
    : random_(SeedRandom()) {
    YBSchemaBuilder b;
    b.AddColumn("key")->Type(YBColumnSchema::INT32)->NotNull()->PrimaryKey();
    b.AddColumn("int_val")->Type(YBColumnSchema::INT32)->NotNull();
    CHECK_OK(b.Build(&schema_));
  }

  virtual void SetUp() OVERRIDE {
    YBTest::SetUp();

    // Speed up testing, saves about 700ms per TEST_F.
    FLAGS_heartbeat_interval_ms = 10;

    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    mini_cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(mini_cluster_->Start());

    master_rpc_addr_ = mini_cluster_->mini_master()->bound_rpc_addr();

    // Connect to the cluster.
    ASSERT_OK(client::YBClientBuilder()
                     .add_master_server_addr(master_rpc_addr_.ToString())
                     .Build(&client_));

    // Create one table.
    gscoped_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(kTableName)
                     .schema(&schema_)
                     .num_replicas(3)
                     .split_rows(GenerateSplitRows())
                     .Create());
    // Make sure we can open the table.
    ASSERT_OK(client_->OpenTable(kTableName, &client_table_));

    ASSERT_OK(RemoteYsckMaster::Build(master_rpc_addr_, &master_));
    cluster_.reset(new YsckCluster(master_));
    ysck_.reset(new Ysck(cluster_));
  }

  virtual void TearDown() OVERRIDE {
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
    Status status;
    status = client_->OpenTable(kTableName, &table);
    if (!status.ok()) {
      promise->Set(status);
    }
    shared_ptr<YBSession> session(client_->NewSession());
    session->SetTimeoutMillis(10000);
    status = session->SetFlushMode(YBSession::MANUAL_FLUSH);
    if (!status.ok()) {
      promise->Set(status);
    }

    for (uint64_t i = 0; continue_writing.Load(); i++) {
      gscoped_ptr<YBInsert> insert(table->NewInsert());
      GenerateDataForRow(table->schema(), i, &random_, insert->mutable_row());
      status = session->Apply(insert.release());
      if (!status.ok()) {
        promise->Set(status);
      }
      status = session->Flush();
      if (!status.ok()) {
        promise->Set(status);
      }
      started_writing->CountDown(1);
    }
    promise->Set(Status::OK());
  }

 protected:
  // Generate a set of split rows for tablets used in this test.
  vector<const YBPartialRow*> GenerateSplitRows() {
    vector<const YBPartialRow*> split_rows;
    vector<int> split_nums = { 33, 66 };
    for (int i : split_nums) {
      YBPartialRow* row = schema_.NewRow();
      CHECK_OK(row->SetInt32(0, i));
      split_rows.push_back(row);
    }
    return split_rows;
  }

  Status GenerateRowWrites(uint64_t num_rows) {
    shared_ptr<YBTable> table;
    RETURN_NOT_OK(client_->OpenTable(kTableName, &table));
    shared_ptr<YBSession> session(client_->NewSession());
    session->SetTimeoutMillis(10000);
    RETURN_NOT_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    for (uint64_t i = 0; i < num_rows; i++) {
      VLOG(1) << "Generating write for row id " << i;
      gscoped_ptr<YBInsert> insert(table->NewInsert());
      GenerateDataForRow(table->schema(), i, &random_, insert->mutable_row());
      RETURN_NOT_OK(session->Apply(insert.release()));

      if (i > 0 && i % 1000 == 0) {
        RETURN_NOT_OK(session->Flush());
      }
    }
    RETURN_NOT_OK(session->Flush());
    return Status::OK();
  }

  std::shared_ptr<Ysck> ysck_;
  shared_ptr<client::YBClient> client_;

 private:
  Sockaddr master_rpc_addr_;
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
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(30));
  Status s;
  while (MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
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

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(30));
  Status s;
  while (MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
    ASSERT_OK(ysck_->FetchTableAndTabletInfo());
    s = ysck_->ChecksumData(vector<string>(),
                            vector<string>(),
                            ChecksumOptions(MonoDelta::FromSeconds(1), 16, false, 0));
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
                                 ChecksumOptions(MonoDelta::FromNanoseconds(0), 16, false, 0));
  ASSERT_TRUE(s.IsTimedOut()) << "Expected TimedOut Status, got: " << s.ToString();
}

TEST_F(RemoteYsckTest, TestChecksumSnapshot) {
  CountDownLatch started_writing(1);
  AtomicBool continue_writing(true);
  Promise<Status> promise;
  scoped_refptr<Thread> writer_thread;

  Thread::Create("RemoteYsckTest", "TestChecksumSnapshot",
                 &RemoteYsckTest::GenerateRowWritesLoop, this,
                 &started_writing, boost::cref(continue_writing), &promise,
                 &writer_thread);
  CHECK(started_writing.WaitFor(MonoDelta::FromSeconds(30)));

  uint64_t ts = client_->GetLatestObservedTimestamp();
  MonoTime start(MonoTime::Now(MonoTime::FINE));
  MonoTime deadline = start;
  deadline.AddDelta(MonoDelta::FromSeconds(30));
  Status s;
  // TODO: We need to loop here because safe time is not yet implemented.
  // Remove this loop when that is done. See KUDU-1056.
  while (true) {
    ASSERT_OK(ysck_->FetchTableAndTabletInfo());
    Status s = ysck_->ChecksumData(vector<string>(), vector<string>(),
                                   ChecksumOptions(MonoDelta::FromSeconds(10), 16, true, ts));
    if (s.ok()) break;
    if (deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) break;
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  if (!s.ok()) {
    LOG(WARNING) << Substitute("Timed out after $0 waiting for ysck to become consistent on TS $1. "
                               "Status: $2",
                               MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).ToString(),
                               ts, s.ToString());
    EXPECT_OK(s); // To avoid ASAN complaints due to thread reading the CountDownLatch.
  }
  continue_writing.Store(false);
  ASSERT_OK(promise.Get());
  writer_thread->Join();
}

// Test that followers & leader wait until safe time to respond to a snapshot
// scan at current timestamp. TODO: Safe time not yet implemented. See KUDU-1056.
TEST_F(RemoteYsckTest, DISABLED_TestChecksumSnapshotCurrentTimestamp) {
  CountDownLatch started_writing(1);
  AtomicBool continue_writing(true);
  Promise<Status> promise;
  scoped_refptr<Thread> writer_thread;

  Thread::Create("RemoteYsckTest", "TestChecksumSnapshot",
                 &RemoteYsckTest::GenerateRowWritesLoop, this,
                 &started_writing, boost::cref(continue_writing), &promise,
                 &writer_thread);
  CHECK(started_writing.WaitFor(MonoDelta::FromSeconds(30)));

  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  ASSERT_OK(ysck_->ChecksumData(vector<string>(), vector<string>(),
                                ChecksumOptions(MonoDelta::FromSeconds(10), 16, true,
                                                ChecksumOptions::kCurrentTimestamp)));
  continue_writing.Store(false);
  ASSERT_OK(promise.Get());
  writer_thread->Join();
}

} // namespace tools
} // namespace yb
