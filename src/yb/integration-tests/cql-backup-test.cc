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
DECLARE_bool(TEST_import_snapshot_failed);

namespace yb {

YB_DEFINE_ENUM(ObjectOp, (kKeep)(kDrop));
typedef ObjectOp UDTypeOp;

const string kDefaultTableName = "test_tbl";

class CqlBackupTest : public CqlTestBase<MiniCluster> {
 public:
  virtual ~CqlBackupTest() = default;

  void SetUp() override {
    // Provide correct '--fs_data_dirs' via TS Web UI.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_mini_cluster_mode) = true;
    CqlTestBase<MiniCluster>::SetUp();

    backup_dir_ = GetTempDir("backup");
    session_ = make_unique<CassandraSession>(
        ASSERT_RESULT(EstablishSession(driver_.get())));
  }

  void cql(const string& query) {
    ASSERT_OK(session_->ExecuteQuery(query));
  }

  int64 getRowCount(const string& table_name = kDefaultTableName) {
    auto result = EXPECT_RESULT(session_->ExecuteWithResult("SELECT count(*) FROM " + table_name));
    auto iterator = result.CreateIterator();
    EXPECT_TRUE(iterator.Next());
    return iterator.Row().Value(0).As<int64>();
  }

  void createTestTable(const string& table_name = kDefaultTableName) {
    cql("CREATE TABLE " + table_name + "(userid INT PRIMARY KEY, fullname TEXT)");
    cql("INSERT INTO " + table_name + " (userid, fullname) values (1, 'yb');");
    ASSERT_EQ(1, getRowCount(table_name));
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
  void DoTestRestoreUDT(const string& new_ks, UDTypeOp udtOp);

  template <typename Fn1, typename Fn2> // void fn()
  void DoTestImportSnapshotFailure(Fn1 fnBeforeRestore, Fn2 fnAfterRestore);

 protected:
  string backup_dir_;
  unique_ptr<CassandraSession> session_;
};

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestBackupWithoutTSWebUI)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  createTestTable();

  // A thread that starts the stopped WebServer after 130 sec. as retry round = 110 sec.
  yb::ThreadPtr thread = stopWebServerAndStartAfter(0, 130);

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir_, "--keyspace", kCqlTestKeyspace, "create"}));

  thread->Join();
  cql("DROP TABLE " + kDefaultTableName);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestBackupRestoreWithoutTSWebUI)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
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

void CqlBackupTest::DoTestRestoreUDT(const string& new_ks, UDTypeOp udtOp) {
  cql("CREATE TYPE udt1 (a int, b int)");
  cql("CREATE TYPE udt2 (a int, b frozen<udt1>)");
  cql("CREATE TABLE " + kDefaultTableName + " (id INT PRIMARY KEY, v udt2)");
  cql("INSERT INTO " + kDefaultTableName + " (id, v) values (1, {a:2, b:{a:3, b:4}});");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir_, "--keyspace", kCqlTestKeyspace, "create"}));

  cql("DROP TABLE " + kDefaultTableName);
  // There are 3 different test-cases in the code below.
  //
  // Test-case 1 (no type dropping - reuse existing types in keyspace 'test'):
  //     DROP TABLE test.test_tbl
  //   Backup-restore:
  //     CREATE TABLE test.test_tbl
  //
  // Test-case 2 (all operations are in keyspace 'test'):
  //     DROP TABLE test.test_tbl
  //     DROP TYPE test.udt2
  //     DROP TYPE test.udt1
  //   Backup-restore:
  //     CREATE TYPE test.udt1
  //     CREATE TYPE test.udt2
  //     CREATE TABLE test.test_tbl
  //
  // Test-case 3 (restoring into keyspace 'ks2'):
  //     DROP TABLE test.test_tbl
  //   Backup-restore:
  //     CREATE TYPE ks2.udt1
  //     CREATE TYPE ks2.udt2
  //     # Possible error: creating test.udt1 & test.udt2 here (wrong keyspace in UDT).
  //     # That's why the types test.udt1/udt2 were not dropped before to catch
  //     # the case via getting 'AlreadyExists' error in the case.
  //     CREATE TABLE ks2.test_tbl
  //     # Possible error: ks2.test_tbl can reuse test.udt1/udt2
  //                       (wrong keyspace in the table schema).
  //     # To catch the case via the error "UDT is used in table"- let's drop test.udt1/udt2 types:
  //     DROP TYPE test.udt2
  //     DROP TYPE test.udt1

  if (new_ks == kCqlTestKeyspace && udtOp == UDTypeOp::kDrop) {
    cql("DROP TYPE udt2");
    cql("DROP TYPE udt1");
  }

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir_, "--keyspace", new_ks, "restore"}));

  if (new_ks != kCqlTestKeyspace) {
    if (udtOp == UDTypeOp::kDrop) {
      // Restored table must use types: ks2.udt1 & ks2.udt2. Delete UDT: test.udt1 & test.udt2.
      cql("DROP TYPE udt2");
      cql("DROP TYPE udt1");
    }

    cql("USE ks2");
  }
  EXPECT_EQ(1, getRowCount(kDefaultTableName));

  // Ensure the user-defined types are available in the current keyspace.
  cql("CREATE TABLE " + kDefaultTableName + "2" + " (id INT PRIMARY KEY, v1 udt1, v2 udt2)");

  // Drop the tables to unblock the UD types dropping below.
  cql("DROP TABLE " + kDefaultTableName);
  cql("DROP TABLE " + kDefaultTableName + "2");
  // Test the types dropping. Ensure that nothing blocks dropping of the types.
  cql("DROP TYPE udt2");
  cql("DROP TYPE udt1");
}

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestRestoreUDT)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  DoTestRestoreUDT(kCqlTestKeyspace, UDTypeOp::kDrop);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestRestoreReuseExistingUDT)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  DoTestRestoreUDT(kCqlTestKeyspace, UDTypeOp::kKeep);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestRestoreUDTIntoNewKS)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  DoTestRestoreUDT("ks2", UDTypeOp::kDrop);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

template <typename Fn1, typename Fn2>
void CqlBackupTest::DoTestImportSnapshotFailure(Fn1 fnBeforeRestore, Fn2 fnAfterRestore) {
  cql("CREATE KEYSPACE ks1");
  cql("USE ks1");
  cql("CREATE TYPE udt (a int, b int)");
  cql("CREATE TABLE " + kDefaultTableName + " (id INT PRIMARY KEY, v udt)");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir_, "--keyspace", "ks1", "create"}));

  fnBeforeRestore();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_import_snapshot_failed) = true;
  ASSERT_NOK(RunBackupCommand(
      {"--backup_location", backup_dir_, "--keyspace", "ks2", "restore"}));

  fnAfterRestore();
}

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestFailedImportSnapshot)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  DoTestImportSnapshotFailure(
    []() -> void {}, // Before backup-restore
    [this]() -> void { // After backup-restore
      // Clean-up must delete all created objects: ks2.test_tbl, ks2.udt, ks2.
      // Try to create them manually to ensure that they were deleted.
      cql("CREATE KEYSPACE ks2");
      cql("USE ks2");
      cql("CREATE TYPE udt (a int, b int)");
      cql("CREATE TABLE " + kDefaultTableName + " (id INT PRIMARY KEY, v udt)");
    });

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestFailedImportSnapshotKeepExistingUDT)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  DoTestImportSnapshotFailure(
    [this]() -> void { // Before backup-restore
      cql("CREATE KEYSPACE ks2");
      cql("USE ks2");
      cql("CREATE TYPE udt (a int, b int)");
    },
    [this]() -> void { // After backup-restore
      // Clean-up must delete only just created table: ks2.test_tbl.
      // Try to use available UDT ks2.udt.
      cql("CREATE TABLE " + kDefaultTableName + " (id INT PRIMARY KEY, v udt)");
    });

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(CqlBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestFailedImportSnapshotKeepExistingKS)) {
  if (DisableMiniClusterBackupTests()) {
    return;
  }
  DoTestImportSnapshotFailure(
    [this]() -> void { // Before backup-restore
      cql("CREATE KEYSPACE ks2");
    },
    [this]() -> void { // After backup-restore
      // Clean-up must delete only just created table & udt: ks2.test_tbl, ks2.udt.
      // Try to use available keyspace ks2.
      cql("USE ks2");
      cql("CREATE TYPE udt (a int, b int)");
      cql("CREATE TABLE " + kDefaultTableName + " (id INT PRIMARY KEY, v udt)");
    });

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace yb
