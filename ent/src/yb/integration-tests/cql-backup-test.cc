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

#include "yb/integration-tests/cql_test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using std::make_unique;
using std::string;
using std::unique_ptr;

DECLARE_bool(TEST_mini_cluster_mode);

namespace yb {

const string kDefaultTableName = "users";

class CqlBackupTest : public CqlTestBase<MiniCluster> {
 public:
  virtual ~CqlBackupTest() = default;

  void SetUp() override {
    FLAGS_TEST_mini_cluster_mode = true; // Provide correct '--fs_data_dirs' via TS Web UI.
    CqlTestBase<MiniCluster>::SetUp();

    backup_dir_ = GetTempDir("backup");
    session_ = make_unique<CassandraSession>(
        ASSERT_RESULT(EstablishSession(driver_.get())));
  }

  void cql(const string& query) {
    ASSERT_OK(session_->ExecuteQuery(query));
  }

  void createTestTable(const string& table_name = kDefaultTableName) {
    cql("CREATE TABLE " + table_name + "(userid INT PRIMARY KEY, fullname TEXT)");
    cql("INSERT INTO " + table_name + " (userid, fullname) values (1, 'yb');");

    auto result = ASSERT_RESULT(session_->ExecuteWithResult("SELECT count(*) FROM " + table_name));
    auto iterator = result.CreateIterator();
    ASSERT_TRUE(iterator.Next());
    auto count = iterator.Row().Value(0).As<int64>();
    EXPECT_EQ(count, 1);
  }

  yb::ThreadPtr stopWebServerAndStartAfter(size_t tsIdx, double startAfterSec) {
    Webserver* web_server = cluster_->mini_tablet_server(tsIdx)->server()->TEST_web_server();
    CHECK_NOTNULL(web_server)->Stop();
    SleepFor(MonoDelta::FromSeconds(0.5)); // Let the server stop listening.

    // A thread that starts the stopped WebServer after some time.
    yb::ThreadPtr thread;
    EXPECT_OK(yb::Thread::Create(
        CURRENT_TEST_NAME(), "web_server_starter",
        [web_server, startAfterSec]() {
          // Start the server after the specified number of seconds.
          SleepFor(MonoDelta::FromSeconds(startAfterSec));
          EXPECT_OK(web_server->Start());
        }, &thread));
    return thread;
  }

 protected:
  string backup_dir_;
  unique_ptr<CassandraSession> session_;
};

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestBackupWithoutTSWebUI)) {
  createTestTable();

  // A thread that starts the stopped WebServer after 130 sec. as retry round = 110 sec.
  yb::ThreadPtr thread = stopWebServerAndStartAfter(0, 130);

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir_, "--keyspace", kCqlTestKeyspace, "create"}));

  thread->Join();
  cql("DROP TABLE " + kDefaultTableName);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestBackupRestoreWithoutTSWebUI)) {
  createTestTable();

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir_, "--keyspace", kCqlTestKeyspace, "create"}));

  // A thread that starts the stopped WebServer after 110 sec. as retry round = 90 sec.
  yb::ThreadPtr thread = stopWebServerAndStartAfter(0, 110);

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir_, "--keyspace", kCqlTestKeyspace, "restore"}));

  thread->Join();
  cql("DROP TABLE " + kDefaultTableName);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace yb
