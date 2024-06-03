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

#include <memory>
#include <unordered_map>

#include <boost/lexical_cast.hpp>
#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/gutil/callback.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/tools/ysck.h"

#include "yb/util/test_util.h"

namespace yb {
namespace tools {

using std::shared_ptr;
using std::static_pointer_cast;
using std::string;
using std::unordered_map;
using std::vector;
using client::YBTableName;

static const YBTableName kTableName(YQL_DATABASE_CQL, "test");

class MockYsckTabletServer : public YsckTabletServer {
 public:
  explicit MockYsckTabletServer(const string& uuid)
      : YsckTabletServer(uuid),
        connect_status_(Status::OK()),
        address_("<mock>") {
  }

  Status Connect() const override {
    return connect_status_;
  }

  virtual void RunTabletChecksumScanAsync(
      const std::string& tablet_id,
      const Schema& schema,
      const ChecksumOptions& options,
      const ReportResultCallback& callback) override {
    callback.Run(Status::OK(), 0);
  }

  Status CurrentHybridTime(uint64_t* hybrid_time) const override {
    *hybrid_time = 0;
    return Status::OK();
  }

  const std::string& address() const override {
    return address_;
  }

  // Public because the unit tests mutate this variable directly.
  Status connect_status_;

 private:
  const string address_;
};

class MockYsckMaster : public YsckMaster {
 public:
  MockYsckMaster()
      : connect_status_(Status::OK()) {
  }

  Status Connect() const override {
    return connect_status_;
  }

  Status RetrieveTabletServers(TSMap* tablet_servers) override {
    *tablet_servers = tablet_servers_;
    return Status::OK();
  }

  Status RetrieveTablesList(vector<shared_ptr<YsckTable>>* tables) override {
    tables->assign(tables_.begin(), tables_.end());
    return Status::OK();
  }

  Status RetrieveTabletsList(const shared_ptr<YsckTable>& table) override {
    return Status::OK();
  }

  // Public because the unit tests mutate these variables directly.
  Status connect_status_;
  TSMap tablet_servers_;
  vector<shared_ptr<YsckTable>> tables_;
};

class YsckTest : public YBTest {
 public:
  YsckTest()
      : master_(new MockYsckMaster()),
        cluster_(new YsckCluster(static_pointer_cast<YsckMaster>(master_))),
        ysck_(new Ysck(cluster_)) {
    unordered_map<string, shared_ptr<YsckTabletServer>> tablet_servers;
    for (int i = 0; i < 3; i++) {
      string name = strings::Substitute("$0", i);
      shared_ptr<MockYsckTabletServer> ts(new MockYsckTabletServer(name));
      InsertOrDie(&tablet_servers, ts->uuid(), ts);
    }
    master_->tablet_servers_.swap(tablet_servers);
  }

 protected:
  void CreateDefaultAssignmentPlan(int tablets_count) {
    while (tablets_count > 0) {
      for (const YsckMaster::TSMap::value_type& entry : master_->tablet_servers_) {
        if (tablets_count-- == 0) return;
        assignment_plan_.push_back(entry.second->uuid());
      }
    }
  }

  void CreateOneTableOneTablet() {
    CreateDefaultAssignmentPlan(1);

    shared_ptr<YsckTablet> tablet(new YsckTablet("1"));
    CreateAndFillTablet(tablet, 1, true);

    CreateAndAddTable({tablet}, kTableName, 1);
  }

  void CreateOneSmallReplicatedTable() {
    int num_replicas = 3;
    int num_tablets = 3;
    vector<shared_ptr<YsckTablet>> tablets;
    CreateDefaultAssignmentPlan(num_replicas * num_tablets);
    for (int i = 0; i < num_tablets; i++) {
      shared_ptr<YsckTablet> tablet(new YsckTablet(boost::lexical_cast<string>(i)));
      CreateAndFillTablet(tablet, num_replicas, true);
      tablets.push_back(tablet);
    }

    CreateAndAddTable(tablets, kTableName, num_replicas);
  }

  void CreateOneOneTabletReplicatedBrokenTable() {
    // We're placing only two tablets, the 3rd goes nowhere.
    CreateDefaultAssignmentPlan(2);

    shared_ptr<YsckTablet> tablet(new YsckTablet("1"));
    CreateAndFillTablet(tablet, 2, false);

    CreateAndAddTable({tablet}, kTableName, 3);
  }

  void CreateAndAddTable(vector<shared_ptr<YsckTablet>> tablets,
                         const YBTableName& name, int num_replicas) {
    shared_ptr<YsckTable> table(new YsckTable(/* id */ "", name, Schema(), num_replicas,
        TableType::YQL_TABLE_TYPE));
    table->set_tablets(tablets);

    vector<shared_ptr<YsckTable>> tables = { table };
    master_->tables_.assign(tables.begin(), tables.end());
  }

  void CreateAndFillTablet(const shared_ptr<YsckTablet>& tablet,
                           int num_replicas,
                           bool has_leader) {
    vector<shared_ptr<YsckTabletReplica>> replicas;
    if (has_leader) {
      CreateReplicaAndAdd(&replicas, true);
      num_replicas--;
    }
    for (int i = 0; i < num_replicas; i++) {
      CreateReplicaAndAdd(&replicas, false);
    }
    tablet->set_replicas(replicas);
  }

  void CreateReplicaAndAdd(vector<shared_ptr<YsckTabletReplica>>* replicas, bool is_leader) {
    shared_ptr<YsckTabletReplica> replica(new YsckTabletReplica(assignment_plan_.back(),
                                                                is_leader, !is_leader));
    assignment_plan_.pop_back();
    replicas->push_back(replica);
  }

  shared_ptr<MockYsckMaster> master_;
  shared_ptr<YsckCluster> cluster_;
  shared_ptr<Ysck> ysck_;
  // This is used as a stack. First the unit test is responsible to create a plan to follow, that
  // is the order in which each replica of each tablet will be assigned, starting from the end.
  // So if you have 2 tablets with num_replicas=3 and 3 tablet servers, then to distribute evenly
  // you should have a list that looks like ts1,ts2,ts3,ts3,ts2,ts1 so that the two LEADERS, which
  // are assigned first, end up on ts1 and ts3.
  vector<string> assignment_plan_;
};

TEST_F(YsckTest, TestMasterOk) {
  ASSERT_OK(ysck_->CheckMasterRunning());
}

TEST_F(YsckTest, TestMasterUnavailable) {
  Status error = STATUS(NetworkError, "Network failure");
  master_->connect_status_ = error;
  ASSERT_TRUE(ysck_->CheckMasterRunning().IsNetworkError());
}

TEST_F(YsckTest, TestTabletServersOk) {
  ASSERT_OK(ysck_->CheckMasterRunning());
  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  ASSERT_OK(ysck_->CheckTabletServersRunning());
}

TEST_F(YsckTest, TestBadTabletServer) {
  ASSERT_OK(ysck_->CheckMasterRunning());
  Status error = STATUS(NetworkError, "Network failure");
  static_pointer_cast<MockYsckTabletServer>(master_->tablet_servers_.begin()->second)
      ->connect_status_ = error;
  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  Status s = ysck_->CheckTabletServersRunning();
  ASSERT_TRUE(s.IsNetworkError()) << "Status returned: " << s.ToString();
}

TEST_F(YsckTest, TestZeroTableCheck) {
  ASSERT_OK(ysck_->CheckMasterRunning());
  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  ASSERT_OK(ysck_->CheckTabletServersRunning());
  ASSERT_OK(ysck_->CheckTablesConsistency());
}

TEST_F(YsckTest, TestOneTableCheck) {
  CreateOneTableOneTablet();
  ASSERT_OK(ysck_->CheckMasterRunning());
  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  ASSERT_OK(ysck_->CheckTabletServersRunning());
  ASSERT_OK(ysck_->CheckTablesConsistency());
}

TEST_F(YsckTest, TestOneSmallReplicatedTable) {
  CreateOneSmallReplicatedTable();
  ASSERT_OK(ysck_->CheckMasterRunning());
  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  ASSERT_OK(ysck_->CheckTabletServersRunning());
  ASSERT_OK(ysck_->CheckTablesConsistency());
}

TEST_F(YsckTest, TestOneOneTabletBrokenTable) {
  CreateOneOneTabletReplicatedBrokenTable();
  ASSERT_OK(ysck_->CheckMasterRunning());
  ASSERT_OK(ysck_->FetchTableAndTabletInfo());
  ASSERT_OK(ysck_->CheckTabletServersRunning());
  ASSERT_TRUE(ysck_->CheckTablesConsistency().IsCorruption());
}

} // namespace tools
} // namespace yb
