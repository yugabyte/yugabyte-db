// Copyright (c) YugaByte, Inc.
//
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

#include "yb/client/client-test-util.h"
#include "yb/client/table_info.h"
#include "yb/client/ql-dml-test-base.h"

#include "yb/master/master_client.pb.h"

#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/util/backoff_waiter.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_ddl_atomicity_test_base.h"

using namespace std::chrono_literals;
using namespace std::literals;

DECLARE_int32(TEST_partitioning_version);
DECLARE_bool(ysql_yb_enable_replica_identity);

namespace {

const auto kDefaultTimeout = 30s;

template <size_t N>
  std::string bytes_to_str(const char (&bytes)[N]) {
    // Correctly instantiates std::string if an array conains a zero char and handles the case with
    // null-terminated string ignoring the last element.
    return std::string(bytes, N - (bytes[N - 1] == '\0' ? 1 : 0));
  }

}  // anonymous namespace

namespace yb {
namespace tools {

class YBBackupTestNumTablets : public YBBackupTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);

    // For convenience, rather than create a subclass for tablet splitting tests, add tablet split
    // flags here since they shouldn't really affect non-tablet splitting tests.
    options->extra_master_flags.push_back("--enable_automatic_tablet_splitting=false");
    options->extra_tserver_flags.push_back("--db_filter_block_size_bytes=2048");
    options->extra_tserver_flags.push_back("--db_index_block_size_bytes=2048");
    options->extra_tserver_flags.push_back("--db_block_size_bytes=1024");
    options->extra_tserver_flags.push_back("--ycql_num_tablets=3");
    options->extra_tserver_flags.push_back("--ysql_num_tablets=3");
    options->extra_tserver_flags.push_back("--cleanup_split_tablets_interval_sec=1");
  }

  string default_db_ = "yugabyte";
};

// Test backup/restore on table with UNIQUE constraint when default number of tablets differs. When
// creating the table, the default is 3; when restoring, the default is 2. Restore should restore
// the unique constraint index as 3 tablets since the tablet snapshot files are already split into 3
// tablets.
//
// For debugging, run ./yb_build.sh with extra flags:
// - --extra-daemon-flags "--vmodule=client=1,table_creator=1"
// - --test-args "--verbose_yb_backup"
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLChangeDefaultNumTablets),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";
  const string index_name = table_name + "_v_key";

  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT, UNIQUE (v))", table_name)));

  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 3);

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table (and index) so that, on restore, running the ysql_dump file recreates the table
  // (and index).
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // When restore runs the CREATE TABLE, make it run in an environment where the default number of
  // tablets is different. Namely, run it with new default 2 (previously 3). This won't affect the
  // table since the table is generated with SPLIT clause specifying 3, but it will change the way
  // the unique index is created because the unique index has no corresponding grammar to specify
  // number of splits in ysql_dump file.
  for (auto ts : cluster_->tserver_daemons()) {
    ts->Shutdown();
    ts->mutable_flags()->push_back("--ysql_num_tablets=2");
    ASSERT_OK(ts->Restart());
  }

  // Check that --ysql_num_tablets=2 is working as intended by
  // 1. running the CREATE TABLE that is expected to be found in the ysql_dump file and
  // 2. finding 2 index tablets
  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT, UNIQUE (v))", table_name)));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 2);
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore should notice that the index it creates from ysql_dump file (2 tablets) differs from
  // the external snapshot (3 tablets), so it should adjust to match the snapshot (3 tablets).
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 3);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test delete_snapshot operation, where one of the tablets involved in the delete snapshot
// operation is already deleted (For example: due to tablet splitting).
// 1. create a table with 3 pre-split tablets.
// 2. create a database snapshot
// 3. split one of the tablets to make 4 tablets
// 4. delete the snapshot created at 2
TEST_F(YBBackupTest, DeleteSnapshotAfterTabletSplitting) {
  const string table_name = "mytbl";
  const string default_db = "yugabyte";
  LOG(INFO) << Format("Create table '$0'", table_name);
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT, v INT)", table_name)));
  LOG(INFO) << "Insert values";
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 (k,v) SELECT i,i FROM generate_series(1,10000) AS i", table_name),
      10000));
  // Verify tablets count and get table_id
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 3);
  constexpr int middle_index = 1;
  TableId table_id = tablets[0].table_id();
  LOG(INFO) << "Create snapshot";
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_id));
  LOG(INFO) << "Split one tablet Manually and wait for parent tablet to be deleted.";
  string tablet_id = tablets[middle_index].tablet_id();
  // Split it && Wait for split to complete.
  constexpr int num_tablets = 4;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db, table_name, /* wait_for_parent_deletion */ true, tablet_id));
  LOG(INFO) << "Finish tablet splitting";
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  // Delete the snapshot after the parent tablet has been deleted
  LOG(INFO) << "Delete snapshot";
  ASSERT_OK(snapshot_util_->DeleteSnapshot(snapshot_id));
  // Make sure the snapshot has been deleted
  LOG(INFO) << "Wait snapshot to be Deleted";
  ASSERT_OK(snapshot_util_->WaitAllSnapshotsDeleted());
}

// Test fixture class for tests that need multiple masters (ex: master failover scenarios)
class YBBackupTestMultipleMasters : public YBBackupTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    options->num_masters = 3;
  }
};

// Test delete_snapshot operation completes successfully when one of the tablets involved in the
// delete snapshot operation is already deleted (For example: due to tablet splitting) and a master
// failover happens after that.
// 1. create a table with 3 pre-split tablets.
// 2. create a database snapshot
// 3. split one of the tablets to make 4 tablets and wait for the parent tablet to be deleted
// 4. perform a master leader failover - the new master will not have any state for this deleted
// parent tablet
// 5. delete the snapshot created at 2
TEST_F_EX(
    YBBackupTest, DeleteSnapshotAfterTabletSplittingAndMasterFailover,
    YBBackupTestMultipleMasters) {
  const string table_name = "mytbl";
  const string default_db = "yugabyte";
  LOG(INFO) << Format("Create table '$0'", table_name);
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT, v INT)", table_name)));
  LOG(INFO) << "Insert values";
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 (k,v) SELECT i,i FROM generate_series(1,1000) AS i", table_name),
      1000));
  // Verify tablets count and get table_id
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 3);
  TableId table_id = tablets[0].table_id();
  LOG(INFO) << "Create snapshot";
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_id));
  LOG(INFO) << "Split one tablet Manually and wait for parent tablet to be deleted.";
  constexpr int middle_index = 1;
  string tablet_id = tablets[middle_index].tablet_id();
  // Split it && Wait for split to complete.
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db, table_name, /* wait_for_parent_deletion */ true, tablet_id));
  LOG(INFO) << "Finish tablet splitting";
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db, table_name));
  LogTabletsInfo(tablets);
  constexpr int num_tablets = 4;
  ASSERT_EQ(tablets.size(), num_tablets);
  // Perform a master failover. the new master will not have any state for this deleted parent
  // tablet.
  LOG(INFO) << "Fail over the master leader";
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
  // Delete the snapshot after the parent tablet has been deleted
  LOG(INFO) << "Delete snapshot";
  ASSERT_OK(snapshot_util_->DeleteSnapshot(snapshot_id));
  // Make sure the snapshot has been deleted
  LOG(INFO) << "Wait snapshot to be Deleted";
  ASSERT_OK(snapshot_util_->WaitAllSnapshotsDeleted());
}

// Test backup/restore when a hash-partitioned table undergoes manual tablet splitting.  Most
// often, if tablets are split after creation, the partition boundaries will not be evenly spaced.
// This then differs from the boundaries of a hash table that is pre-split with the same number of
// tablets.  Restoring snapshots to a table with differing partition boundaries should be detected
// and handled by repartitioning the table, even if the number of partitions are equal.  This test
// exercises that:
// 1. start with 3 pre-split tablets
// 2. split one of them to make 4 tablets
// 3. backup
// 4. drop table
// 5. restore, which will initially create 4 pre-split tablets then realize the partition boundaries
//    differ
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLManualTabletSplit),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";

  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name)));

  // Insert rows that hash to each possible partition range for both manual split and even split.
  //
  // part range    | k  | hash   | manual split part num | even split part num | interesting
  //       -0x3fff | 1  | 0x1210 | 1                     | 1                   | N
  // 0x3fff-0x5555 | 6  | 0x4e58 | 1                     | 2                   | Y
  // 0x5555-0x7ffe | 9  | 0x5d60 | 2                     | 2                   | N
  // 0x7ffe-0xa6e8 | 23 | 0x986c | 2                     | 3                   | Y
  // 0xa6e8-0xaaaa | 4  | 0x9eaf | 3                     | 3                   | N
  // 0xaaaa-0xbffd | 27 | 0xbd51 | 4                     | 3                   | Y
  // 0xbffd-       | 2  | 0xc0c4 | 4                     | 4                   | N
  //
  // Split ranges are further discused in comments below.
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (generate_series(1, 100))", table_name), 100));
  string select_query = Format("SELECT k, to_hex(yb_hash_code(k)) AS hash FROM $0"
                               " WHERE k IN (1, 2, 4, 6, 9, 23, 27) ORDER BY hash",
                               table_name);
  string select_output = R"#(
                            k  | hash
                           ----+------
                             1 | 1210
                             6 | 4e58
                             9 | 5d60
                            23 | 986c
                             4 | 9eaf
                            27 | bd51
                             2 | c0c4
                           (7 rows)
                         )#";
  ASSERT_NO_FATALS(RunPsqlCommand(select_query, select_output));

  // It has three tablets because of --ysql_num_tablets=3.
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 3);
  const std::vector<std::string> initial_split_points = {"\x55\x55", "\xaa\xaa"};
  ASSERT_EQ(ASSERT_RESULT(GetSplitPoints(tablets)), initial_split_points);

  // Choose the middle tablet among
  // -       -0x5555
  // - 0x5555-0xaaaa
  // - 0xaaaa-
  constexpr int middle_index = 1;
  ASSERT_EQ(tablets[middle_index].partition().partition_key_start(), "\x55\x55");
  string tablet_id = tablets[middle_index].tablet_id();

  // Flush table because it is necessary for manual tablet split.
  auto table_id = ASSERT_RESULT(GetTableId(table_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({table_id}, false, 30, false));

  // Split it && Wait for split to complete.
  constexpr int num_tablets = 4;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ false, tablet_id));

  // Verify that it has these four tablets:
  // -       -0x5555
  // - 0x5555-?
  // - ?-0xaaaa
  // - 0xaaaa-
  // Tablet splitting should choose the split point based on the existing data. Don't verify that
  // it chose the right split point: that is out of scope of this test. Just trust what it chose.
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  auto three_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(three_split_points[0], initial_split_points[0]);
  ASSERT_EQ(three_split_points[2], initial_split_points[1]);


  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table so that, on restore, running the ysql_dump file recreates the table.  ysql_dump
  // should specify SPLIT INTO 4 TABLETS because the table in snapshot has 4 tablets.
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Before performing restore, demonstrate that the table that would be created by the ysql_dump
  // file will have different splits.
  ASSERT_NO_FATALS(CreateTable(
      Format("CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 4 TABLETS", table_name)));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), 4);
  auto default_table_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_NE(three_split_points, default_table_split_points);
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore should notice that the table it creates from ysql_dump file has different partition
  // boundaries from the one in the external snapshot EVEN THOUGH the number of partitions is four
  // in both, so it should recreate partitions to match the splits in the snapshot.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Validate.
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), 4);
  auto post_restore_splits = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(post_restore_splits, three_split_points);
  ASSERT_NO_FATALS(RunPsqlCommand(select_query, select_output));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test backup/restore of a range-partitioned table
// without tablet splitting happening.
// Note: make sure there is no CatalogManager::RepartitionTable's log.
// This indicates the success of backup/restore of range-partitioned tables
// without the help RepartitionTable.
// This test exercises that:
// 1. start with a table with 3 pre-split tablets
// 2. insert data
// 3. backup
// 4. drop table
// 5. restore
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestPreSplitYSQLRangeSplitTable),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";

  // Create table
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT, v TEXT, PRIMARY KEY(k ASC, v ASC))"
                                      " SPLIT AT VALUES ((10, 'f'), (20, 'n'))", table_name)));

  // Verify table has 3 tablets
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 3);
  // 'H' represents the KeyEntryType for Int32.
  // \x80\x00\x00\x0a = 0x80000000 ^ 10
  // \x80\x00\x00\x14 = 0x80000000 ^ 20
  // 'S' represents the KeyEntryType for String.
  // "f" and "n" are the split point values of column v
  // two '\x00' terminate a string value.
  // '!' indicates the end of the range group of a key.
  ASSERT_TRUE(CheckPartitions(tablets, {"H\x80\x00\x00\x0aSf\x00\x00!"s,
                                        "H\x80\x00\x00\x14Sn\x00\x00!"s}));

  // Insert data
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (1,'a'), (11,'h'), (21,'o')", table_name), 3));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY v", table_name),
      R"#(
         k  | v
        ----+---
          1 | a
         11 | h
         21 | o
        (3 rows)
      )#"
  ));

  // Backup
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), 3);
  ASSERT_TRUE(CheckPartitions(tablets, {"H\x80\x00\x00\x0aSf\x00\x00!"s,
                                        "H\x80\x00\x00\x14Sn\x00\x00!"s}));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY v", table_name),
      R"#(
         k  | v
        ----+---
          1 | a
         11 | h
         21 | o
        (3 rows)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test backup/restore of a range-partitioned table with a range-partitioned index
// without tablet splitting happening.
// Note: make sure there is no CatalogManager::RepartitionTable's log.
// This indicates the success of backup/restore of range-partitioned tables
// without the help RepartitionTable.
// This test exercises that:
// 1. start with a table with 3 pre-split tablets
// 2. create an index with 3 pre-split tablets
// 3. insert data
// 4. backup
// 5. drop table
// 6. restore
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestPreSplitYSQLRangeSplitTableAndIndex),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";
  const string index_name = "myidx";

  // Create table and index
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT, v TEXT, PRIMARY KEY(k ASC))"
                                      " SPLIT AT VALUES ((10), (20))", table_name)));
  ASSERT_NO_FATALS(CreateIndex(Format("CREATE INDEX $0 ON $1 (v ASC)"
                                      " SPLIT AT VALUES (('f'), ('n'))", index_name, table_name)));

  // Verify both table and index have 3 tablets
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 3);
  // 'H' represents the KeyEntryType for Int32.
  // \x80\x00\x00\x0a = 0x80000000 ^ 10
  // \x80\x00\x00\x14 = 0x80000000 ^ 20
  // '!' indicates the end of the range group of a key.
  ASSERT_TRUE(CheckPartitions(tablets, {"H\x80\x00\x00\x0a!"s,
                                        "H\x80\x00\x00\x14!"s}));

  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 3);
  // 'S' represents the KeyEntryType for String.
  // "f" and "n" are the split point values.
  // one '\0' is the default split point value of an index's hidden column.
  // two '\0' terminate a string value.
  // '!' indicates the end of the range group of a key.
  ASSERT_TRUE(CheckPartitions(tablets, {"Sf\0\0\0!"s, "Sn\0\0\0!"s}));

  // Insert data
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (1,'a'), (11,'h'), (21,'o')", table_name), 3));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY v", table_name),
      R"#(
         k  | v
        ----+---
          1 | a
         11 | h
         21 | o
        (3 rows)
      )#"
  ));

  // Backup
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), 3);
  ASSERT_TRUE(CheckPartitions(tablets, {"H\x80\x00\x00\x0a!"s,
                                        "H\x80\x00\x00\x14!"s}));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 3);
  ASSERT_TRUE(CheckPartitions(tablets, {"Sf\0\0\0!"s, "Sn\0\0\0!"s}));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY v", table_name),
      R"#(
         k  | v
        ----+---
          1 | a
         11 | h
         21 | o
        (3 rows)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test backup/restore when a range-partitioned table undergoes automatic tablet splitting.
// Note: after #12631 is fixed, make sure there is no CatalogManager::RepartitionTable's log.
// This indicates the success of backup/restore of range-partitioned tables
// without the help RepartitionTable.
// This test exercises that:
// 1. start with a table with 2 pre-split tablets
// 2. insert data into the table to trigger automatic tablet splitting
// 3. backup
// 4. drop table
// 5. restore
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLAutomaticTabletSplitRangeTable),
          YBBackupTestNumTablets) {
  constexpr int expected_num_tablets = 4;
  ASSERT_OK(cluster_->SetFlagOnMasters("tablet_split_low_phase_size_threshold_bytes", "2500"));
  // Enable automatic tablet splitting (overriden by YBBackupTestNumTablets).
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_automatic_tablet_splitting", "true"));
  ASSERT_OK(cluster_->SetFlagOnMasters("tablet_split_limit_per_table",
                                       IntToString(expected_num_tablets)));
  // Setting low phase shards count per node explicitly to guarantee a table can split at least
  // up to expected_num_tablets per table within the low phase.
  const auto low_phase_shard_count_per_node = std::ceil(
      static_cast<double>(expected_num_tablets) / GetNumTabletServers());
  ASSERT_OK(cluster_->SetFlagOnMasters("tablet_split_low_phase_shard_count_per_node",
                                       IntToString(low_phase_shard_count_per_node)));

  const string table_name = "mytbl";

  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k TEXT, PRIMARY KEY(k ASC))"
                                      " SPLIT AT VALUES (('4a'))", table_name)));

  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 2);

  // 'S' represents the KeyEntryType for String.
  // "4a" is the split point value.
  // two '\0' terminate a string value.
  // '!' indicates the end of the range group of a key.
  ASSERT_TRUE(CheckPartitions(tablets, {"S4a\0\0!"s}));

  // Insert data.
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 SELECT i||'a' FROM generate_series(101, 150) i", table_name), 50));

  // Flush table so SST file size is accurate.
  auto table_id = ASSERT_RESULT(GetTableId(table_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({table_id}, false, 30, false));

  // Wait for automatic split to complete.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto res = VERIFY_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
        return res.size() == expected_num_tablets;
      }, 30s * kTimeMultiplier, Format("Waiting for tablet count: $0", expected_num_tablets)));

  // Backup.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table.
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Validate number of tablets after restore.
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), expected_num_tablets);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test backup/restore when a range-partitioned table undergoes manual tablet splitting.
// Note: after #12631 is fixed, make sure there is no CatalogManager::RepartitionTable's log.
// This indicates the success of backup/restore of range-partitioned tables
// without the help of RepartitionTable.
// This test exercises that:
// 1. start with 2 pre-split tablets
// 2. split one of them (3 tablets)
// 3. further split one of newly created tablets (4 tablets)
// 4. backup
// 5. drop table
// 6. restore
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLManualTabletSplitRangeTable),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";

  // Create table
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k TEXT, PRIMARY KEY(k ASC))"
                                      " SPLIT AT VALUES (('4a'))", table_name)));

  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 2);

  // 'S' represents the KeyEntryType for String.
  // "4a" is the split point value.
  // two '\0' terminate a string value.
  // '!' indicates the end of the range group of a key.
  const std::string initial_split_point = "S4a\0\0!"s;
  ASSERT_TRUE(CheckPartitions(tablets, {initial_split_point}));

  // Insert data
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 SELECT i||'a' FROM generate_series(101, 150) i", table_name), 50));

  // Flush table
  auto table_id = ASSERT_RESULT(GetTableId(table_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({table_id}, false, 30, false));

  // Split at split depth 0
  // Choose the first tablet among tablets: "" --- "4a" and "4a" --- ""
  int split_index = 0;
  ASSERT_EQ(tablets[split_index].partition().partition_key_start(), "");
  ASSERT_EQ(tablets[split_index].partition().partition_key_end(), initial_split_point);
  string tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  int num_tablets = 3;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ true, tablet_id));

  // Verify that it has these three tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  auto two_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(two_split_points[1], initial_split_point);

  // Further split at split depth 1
  split_index = 0;
  tablet_id = tablets[split_index].tablet_id();

  // Split it && wait for split to complete.
  num_tablets = 4;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ false, tablet_id));

  // Verify that it has these four tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  auto three_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  // Verify the 3 split points contain as suffix the earlier 2 split points.
  ASSERT_TRUE(std::equal(three_split_points.begin() + 1,
                         three_split_points.end(),
                         two_split_points.begin(),
                         two_split_points.end()));

  // Backup
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), 4);
  auto post_restore_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(post_restore_split_points,
            three_split_points);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test backup/restore when a range-partitioned unique index undergoes manual tablet splitting
// on its hidden column: ybuniqueidxkeysuffix.
// This test exercises that:
// 1. create a table and a unique index
// 2. insert data into the table
// 3. split the index on its hidden column into 3 tablets
// 4. backup
// 5. drop table
// 6. restore
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLTabletSplitRangeUniqueIndexOnHiddenColumn),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";
  const string index_name = "myidx";

  // Create table and index
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", table_name)));
  ASSERT_NO_FATALS(CreateIndex(Format("CREATE UNIQUE INDEX $0 ON $1 (v ASC)",
                                      index_name, table_name)));

  // Verify the index has only one tablet
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);
  // Use this function for side effects. It validates the begin and end partitions are empty.
  ASSERT_RESULT(GetSplitPoints(tablets));

  // Insert data
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 SELECT *, null FROM generate_series(1, 100)", table_name), 100));

  // Flush index
  auto index_id = ASSERT_RESULT(GetTableId(index_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({index_id}, false, 30, false));

  // Split the unique index into three tablets on its hidden column:
  // tablet-1 boundaries: [ "", (null, <ybctid-1>) )
  // tablet-2 boundaries: [ (null, <ybctid-1>), (null, <ybctid-2>) )
  // tablet-3 boundaries: [ (null, <ybctid-2>), "" )
  // Split at split depth 0
  int split_index = 0;
  ASSERT_EQ(tablets[split_index].partition().partition_key_start(), "");
  ASSERT_EQ(tablets[split_index].partition().partition_key_end(), "");
  string tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  int num_tablets = 2;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, index_name, /* wait_for_parent_deletion */ true, tablet_id));


  // Verify that it has these two tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  auto one_split_point = ASSERT_RESULT(GetSplitPoints(tablets));

  // Further split at split depth 1
  split_index = 0;
  tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  num_tablets = 3;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, index_name, /* wait_for_parent_deletion */ true, tablet_id));

  // Verify that it has these three tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  auto two_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(two_split_points[1], one_split_point[0]);

  // Verify yb_get_range_split_clause returns an empty string because null values are present
  // in split points.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT yb_get_range_split_clause('$0'::regclass)", index_name),
      R"#(
         yb_get_range_split_clause
        ---------------------------

        (1 row)
      )#"
  ));

  // Backup
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 3);
  auto post_restore_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(post_restore_split_points, two_split_points);

  // Verify yb_get_range_split_clause returns an empty string because null values are present
  // in split points.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT yb_get_range_split_clause('$0'::regclass)", index_name),
      R"#(
         yb_get_range_split_clause
        ---------------------------

        (1 row)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test backup/restore when a range-partitioned regular index undergoes manual tablet splitting
// on its hidden column: ybidxbasectid.
// This test exercises that:
// 1. create a table and a regular index
// 2. insert data into the table
// 3. split the index on its hidden column into 3 tablets
// 4. backup
// 5. drop table
// 6. restore
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLTabletSplitRangeIndexOnHiddenColumn),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";
  const string index_name = "myidx";

  // Create table and index
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", table_name)));
  ASSERT_NO_FATALS(CreateIndex(Format("CREATE INDEX $0 ON $1 (v ASC)", index_name, table_name)));

  // Verify the index has only one tablet
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);
  // Use this function for side effects. It validates the begin and end partitions are empty.
  ASSERT_RESULT(GetSplitPoints(tablets));

  // Insert data
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 SELECT *, 200 FROM generate_series(1, 100)", table_name), 100));

  // Flush index
  auto index_id = ASSERT_RESULT(GetTableId(index_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({index_id}, false, 30, false));

  // Split the index into three tablets on its hidden column:
  // tablet-1 boundaries: [ "", (200, <ybctid-1>) )
  // tablet-2 boundaries: [ (200, <ybctid-1>), (200, <ybctid-2>) )
  // tablet-3 boundaries: [ (200, <ybctid-2>), "" )
  // Split at split depth 0
  int split_index = 0;
  ASSERT_EQ(tablets[split_index].partition().partition_key_start(), "");
  ASSERT_EQ(tablets[split_index].partition().partition_key_end(), "");
  string tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  int num_tablets = 2;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, index_name, /* wait_for_parent_deletion */ true, tablet_id));

  // Verify that it has these two tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  auto one_split_point = ASSERT_RESULT(GetSplitPoints(tablets));

  // Further split at split depth 1
  split_index = 0;
  tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  num_tablets = 3;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, index_name, /* wait_for_parent_deletion */ false, tablet_id));

  // Verify that it has these three tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  auto two_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(two_split_points[1], one_split_point[0]);

  // Backup
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 3);
  auto post_restore_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(two_split_points,
            post_restore_split_points);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test backup/restore when a range-partitioned GIN index undergoes manual tablet splitting and
// GinNullItem become part of its tablets' partition bounds.
// Any kind of GinNull (GinNullKey, GinEmptyItem, and GinNullItem) can be used for this test.
// In the case where GinNull is part of one partition bound of a GIN index, during backup,
// yb_get_range_split_clause fails to decode the partition bound, and YSQL_DUMP doesn't dump the
// SPLIT AT clause of the index.
// During restore, restoring snapshot to the index with different partition boundaries
// should be detected and handled by repartitioning the index. See CatalogManager::RepartitionTable.
// This test exercises that:
// 1. create a table and a GIN index
// 2. insert NULL data into the table
// 3. split the GIN index into 2 tablets to make GinNull become part of partition bounds
// 4. backup
// 5. drop table
// 6. restore
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLTabletSplitGINIndexWithGinNullInPartitionBounds),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";
  const string index_name = "my_gin_idx";

  // Create table and index
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (v tsvector)", table_name)));
  ASSERT_NO_FATALS(CreateIndex(Format("CREATE INDEX $0 ON $1 USING ybgin(v)",
                                      index_name, table_name)));

  // Verify the index has only one tablet
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);
  // Use this function for side effects. It validates the begin and end partitions are empty.
  ASSERT_OK(GetSplitPoints(tablets));

  // Insert data
  const string insert_null_sql = Format(R"#(
    DO $$$$
    BEGIN
      FOR i in 1..1000 LOOP
        INSERT INTO $0 VALUES (NULL);
      END LOOP;
    END $$$$;
  )#", table_name);
  ASSERT_NO_FATALS(RunPsqlCommand(insert_null_sql, "DO"));

  // Flush index
  auto index_id = ASSERT_RESULT(GetTableId(index_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({index_id}, false, 30, false));

  // Split the GIN index into two tablets and wait for its split to complete.
  // The splits make GinNull become part of its tablets' partition bounds:
  // tablet-1 boundaries: [ "", (GinNullItem, <ybctid>) )
  // tablet-2 boundaries: [ (GinNullItem, <ybctid>), "" )
  const auto num_tablets = 2;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, index_name, /* wait_for_parent_deletion */ true, tablets[0].tablet_id()));

  // Verify that it has two tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);

  // Verify GinNull is in the split point.
  // 'v' represents the KeyEntryType for kGinNull.
  auto split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  ASSERT_EQ(split_points.size(), num_tablets - 1);
  ASSERT_EQ(split_points[0][0], 'v');

  // Backup
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 2);
  auto post_restore_split_points = ASSERT_RESULT(GetSplitPoints(tablets));
  // Rely on CatalogManager::RepartitionTable to repartition the GIN index with correct partition
  // boundaries. Validate if repartition works correctly.
  ASSERT_EQ(split_points,
            post_restore_split_points);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

class YBBackupPartitioningVersionTest : public YBBackupTest {
 protected:
  Result<uint32_t> GetTablePartitioningVersion(const client::YBTableName& yb_table_name) {
    const auto table_info = VERIFY_RESULT(client_->GetYBTableInfo(yb_table_name));
    return table_info.schema.table_properties().partitioning_version();
  }

  Result<uint32_t> GetTablePartitioningVersion(const std::string& table_name,
      const std::string& log_prefix, const std::string& ns = std::string()) {
    const auto yb_table_name = VERIFY_RESULT(GetTableName(table_name, log_prefix, ns));
    return GetTablePartitioningVersion(yb_table_name);
  }

  Status ForceSetPartitioningVersion(const int32_t version) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = version;
    for (auto ms : cluster_->master_daemons()) {
      ms->Shutdown();
      ms->mutable_flags()->push_back(Format("--TEST_partitioning_version=$0", version));
      RETURN_NOT_OK(ms->Restart());
    }
    for (auto ts : cluster_->tserver_daemons()) {
      ts->Shutdown();
      ts->mutable_flags()->push_back(Format("--TEST_partitioning_version=$0", version));
      RETURN_NOT_OK(ts->Restart());
    }
    return cluster_->WaitForTabletServerCount(GetNumTabletServers(), kDefaultTimeout);
  }

  string default_db_ = "yugabyte";
};

TEST_F_EX(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYCQLPartitioningVersion),
    YBBackupPartitioningVersionTest) {
  // The test checks that partitioning_version is restored correctly for the tables backuped before
  // the next increment of the partitioning_version.
  constexpr auto kKeyspace0 = "keyspace0";
  constexpr auto kKeyspace1 = "keyspace1";

  // 1) Create a table with partitioning_version == 0.
  ASSERT_OK(ForceSetPartitioningVersion(0));
  const client::YBTableName kTableNameV0(YQL_DATABASE_CQL, kKeyspace0, "mytbl0");
  client::TableHandle table_0;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_0, kTableNameV0);
  auto partitioning_version = ASSERT_RESULT(GetTablePartitioningVersion(kTableNameV0));
  ASSERT_EQ(0, partitioning_version);

  // 2) Force backuping.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", kKeyspace0, "create"}));

  // 3) Simulate cluster upgrade with new partitioning_version and restore.
  ASSERT_OK(ForceSetPartitioningVersion(1));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", kKeyspace1, "restore"}));

  // 4) Make sure new table is created with a new patitioninig version.
  const client::YBTableName kTableNameV1(YQL_DATABASE_CQL, kKeyspace1, "mytbl1");
  client::TableHandle table_1;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_1, kTableNameV1);
  partitioning_version = ASSERT_RESULT(GetTablePartitioningVersion(kTableNameV1));
  ASSERT_EQ(1, partitioning_version);

  // 5) Make sure old table has been restored with the old patitioninig version.
  const client::YBTableName kTableNameV0_Restored(YQL_DATABASE_CQL, kKeyspace1, "mytbl0");
  partitioning_version = ASSERT_RESULT(GetTablePartitioningVersion(kTableNameV0_Restored));
  ASSERT_EQ(0, partitioning_version);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F_EX(
    YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLPartitioningVersion),
    YBBackupPartitioningVersionTest) {
  // The test checks that range partitions are restored correctly depending on partitioning_version.

  // 1) Create a table with partitioning_version == 0.
  ASSERT_OK(ForceSetPartitioningVersion(0));
  const std::vector<std::string> expected_splits_tblv0 = {
      bytes_to_str("\x48\x80\x00\x00\x64\x21"), /* 100 */
      bytes_to_str("\x48\x80\x00\x00\xc8\x21") /* 200 */};
  const std::vector<std::string> expected_splits_idx1v0 = {
      bytes_to_str("\x61\x86\xff\xff\x21"), /* 'y' */
      bytes_to_str("\x61\x8f\xff\xff\x21"), /* 'p' */
      bytes_to_str("\x61\x9a\xff\xff\x21") /* 'e' */};
  const std::vector<std::string> expected_splits_idx2v0 = {
      bytes_to_str("\x53\x78\x79\x7a\x00\x00\x21") /* 'xyz' */};

  // 1.1) Create regular tablets and check partitoning verison and structure.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE tblr0 (k INT, v TEXT, PRIMARY KEY (k ASC))")));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("tblr0", "pre-backup")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "tblr0")), {}));
  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE tblv0 (k INT, v TEXT, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((100), (200))")));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("tblv0", "pre-backup")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "tblv0")),
      expected_splits_tblv0));

  // 1.2) Create indexes and check partitoning verison and structure.
  ASSERT_NO_FATALS(CreateIndex(
      Format("CREATE INDEX idx1v0 ON tblv0 (v DESC) SPLIT AT VALUES (('y'), ('p'), ('e'))")));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("idx1v0", "pre-backup")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "idx1v0")),
      expected_splits_idx1v0));
  ASSERT_NO_FATALS(
      CreateIndex(Format("CREATE UNIQUE INDEX idx2v0 ON tblv0 (v ASC) SPLIT AT VALUES (('xyz'))")));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("idx2v0", "pre-backup")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "idx2v0")),
      expected_splits_idx2v0));

  // 2) Force backuping.
  const std::string backup_dir = GetTempDir("backup");
  ASSERT_OK(
      RunBackupCommand({"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // 3) Drop table to be able to restore in the same keyspace.
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE tblv0"), "DROP TABLE"));

  // 4) Simulate cluster upgrade with new partitioning_version and restore. Index tables with
  //    partitioning version above 0 should contain additional `null` value for a hidden columns
  //    `ybuniqueidxkeysuffix` or `ybidxbasectid`.
  ASSERT_OK(ForceSetPartitioningVersion(1));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // 5) Make sure new tables are created with a new patitioning version.
  // 5.1) Create regular tablet and check partitioning verison and structure.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE tblr1 (k INT, v TEXT, PRIMARY KEY (k ASC))")));
  ASSERT_EQ(1, ASSERT_RESULT(GetTablePartitioningVersion("tblr1", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "tblr1")), {}));
  ASSERT_NO_FATALS(CreateTable(
      Format("CREATE TABLE tblv1 (k INT, v TEXT, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((10))")));
  ASSERT_EQ(1, ASSERT_RESULT(GetTablePartitioningVersion("tblv1", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "tblv1")),
      {bytes_to_str("\x48\x80\x00\x00\x0a\x21"), /* 10 */}));

  // 5.2) Create indexes and check partitoning verison and structure.
  ASSERT_NO_FATALS(
      CreateIndex(Format("CREATE INDEX idx1v1 ON tblv1 (v ASC) SPLIT AT VALUES (('de'), ('op'))")));
  ASSERT_EQ(1, ASSERT_RESULT(GetTablePartitioningVersion("idx1v1", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "idx1v1")),
      {bytes_to_str("\x53\x64\x65\x00\x00\x00\x21"), /* 'de', -Inf */
       bytes_to_str("\x53\x6f\x70\x00\x00\x00\x21"),
       /* 'op', -Inf */}));
  ASSERT_NO_FATALS(CreateIndex(
      Format("CREATE UNIQUE INDEX idx2v1 ON tblv1 (v DESC) SPLIT AT VALUES (('pp'), ('cc'))")));
  ASSERT_EQ(1, ASSERT_RESULT(GetTablePartitioningVersion("idx2v1", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "idx2v1")),
      {bytes_to_str("\x61\x8f\x8f\xff\xff\x00\x21"), /* 'pp', -Inf */
       bytes_to_str("\x61\x9c\x9c\xff\xff\x00\x21"),
       /* 'cc', -Inf */}));

  // 6) Make sure old tables have been restored with the old patitioning version and structure
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("tblr0", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "tblr0")), {}));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("tblv0", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "tblv0")),
      expected_splits_tblv0));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("idx1v0", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "idx1v0")),
      expected_splits_idx1v0));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("idx2v0", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, "idx2v0")),
      expected_splits_idx2v0));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

class YBBackupTestOneTablet : public YBBackupTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--enable_automatic_tablet_splitting=false");
    options->extra_tserver_flags.push_back("--ycql_num_tablets=1");
    options->extra_tserver_flags.push_back("--ysql_num_tablets=1");
  }

  string default_db_ = "yugabyte";
};

// Test that backups taken after a tablet has been split but before the child tablets are compacted
// don't expose the extra data in the child tablets when queried.
TEST_F_EX(
    YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestScanSplitTableAfterRestore),
    YBBackupTestOneTablet) {
  const string table_name = "mytbl";

  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "true"));

  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", table_name)));

  int row_count = 200;
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 SELECT i, i FROM generate_series(1, $1) AS i", table_name, row_count),
      row_count));
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(10s));

  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);

  // Flush table because it is necessary for manual tablet split.
  auto table_id = ASSERT_RESULT(GetTableId(table_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({table_id}, false, 30, false));

  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ false, tablets[0].tablet_id()));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 2);

  // Create backup, unset skip flag, and restore to a new db.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(
      RunBackupCommand({"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "false"));
  std::string db_name = "yugabyte_new";
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", db_name), "restore"}));

  // Sanity check the tablet count.
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(db_name, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 2);
  SetDbName(db_name);
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT count(*) FROM $0", table_name),
      R"#(
           count
          -------
             200
          (1 row)
      )#"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 WHERE v = 2", table_name),
      R"#(
           k | v
          ---+---
           2 | 2
          (1 row)
      )#"));
}

TEST_F_EX(
    YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestRestoreUncompactedChildTabletAndSplit),
    YBBackupTestOneTablet) {
  const string table_name = "mytbl";

  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "true"));
  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", table_name)));
  int row_count = 200;
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 SELECT i, i FROM generate_series(1, $1) AS i", table_name, row_count),
      row_count));

  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);

  // Wait for intents and flush table because it is necessary for manual tablet split.
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(10s));
  auto table_id = ASSERT_RESULT(GetTableId(table_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({table_id}, false, 30, false));
  constexpr bool kWaitForParentDeletion = false;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ kWaitForParentDeletion,
      tablets[0].tablet_id()));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), /* expected_num_tablets = */ 2);

  // Create backup, unset skip flag, and restore to a new db.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(
      RunBackupCommand({"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "false"));
  std::string db_name = "yugabyte_new";
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", db_name), "restore"}));

  // Sanity check the tablet count.
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(db_name, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 2);

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablets[0].tablet_id()));
  // Wait for compaction to complete.
  ASSERT_OK(WaitForTabletPostSplitCompacted(leader_idx, tablets[0].tablet_id()));
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ kWaitForParentDeletion,
      tablets[0].tablet_id()));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(db_name, table_name));
  ASSERT_EQ(tablets.size(), /* expected_num_tablets = */ 3);
}

TEST_F_EX(
    YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestReplicaIdentityAfterRestore),
    YBBackupTestOneTablet) {
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_enable_replica_identity", "true"));

  ASSERT_OK(cluster_->SetFlagOnMasters("ysql_yb_enable_replica_identity", "true"));

  const string table_name = "mytbl";

  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", table_name)));

  ASSERT_RESULT(RunPsqlCommand("ALTER TABLE mytbl REPLICA IDENTITY FULL"));
  ASSERT_NO_FATALS(RunPsqlCommand("SELECT relreplident FROM pg_class WHERE oid = 'mytbl'::regclass",
  R"#(
    relreplident
   --------------
    f
   (1 row)
  )#"));

  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(
      RunBackupCommand({"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));
  std::string db_name = "yugabyte_new";
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", db_name), "restore"}));

  // Sanity check the tablet count.
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(db_name, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);
  SetDbName(db_name);

  ASSERT_NO_FATALS(RunPsqlCommand("SELECT relreplident FROM pg_class WHERE oid = 'mytbl'::regclass",
  R"#(
    relreplident
   --------------
    f
   (1 row)
  )#"));
}

class YBLegacyColocatedDBBackupTest : public YBBackupTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=true");
  }
};

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestColocationDuplication)) {
  TestColocatedDBBackupRestore();
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F_EX(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestLegacyColocatedDBColocationDuplication),
          YBLegacyColocatedDBBackupTest) {
  TestColocatedDBBackupRestore();
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestBackupChecksumsDisabled)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'foo')"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (101, 'bar')"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (102, 'cab')"));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "--disable_checksums",
       "create"}));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (999, 'foo')"));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "--disable_checksums",
       "restore"}));

  SetDbName("yugabyte_new");  // Connecting to the second DB from the moment.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | foo
         101 | bar
         102 | cab
        (3 rows)
      )#"));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

YB_STRONGLY_TYPED_BOOL(SourceDatabaseIsColocated);
YB_STRONGLY_TYPED_BOOL(PackedRowsEnabled);

class YBBackupTestWithPackedRowsAndColocation :
    public YBBackupTest,
    public ::testing::WithParamInterface<std::tuple<PackedRowsEnabled, SourceDatabaseIsColocated>> {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    if (std::get<0>(GetParam())) {
      // Add flags to enable packed row feature.
      options->extra_master_flags.push_back("--enable_automatic_tablet_splitting=false");
      options->extra_tserver_flags.push_back("--ysql_enable_packed_row=true");
      options->extra_tserver_flags.push_back("--ysql_enable_packed_row_for_colocated_table=true");
    } else {
      options->extra_tserver_flags.push_back("--ysql_enable_packed_row=false");
      options->extra_tserver_flags.push_back("--ysql_enable_packed_row_for_colocated_table=false");
    }
  }

  void SetUp() override {
    YBBackupTest::SetUp();
    if (std::get<1>(GetParam())) {
      ASSERT_NO_FATALS(RunPsqlCommand(
          Format("CREATE DATABASE $0 WITH COLOCATION=TRUE", backup_db_name), "CREATE DATABASE"));
      SetDbName(backup_db_name);
    }
  }

  const std::string backup_db_name = std::get<1>(GetParam()) ? "colo_db" : "yugabyte";
  const std::string restore_db_name = "restored_db";
};

TEST_P(
    YBBackupTestWithPackedRowsAndColocation,
    YB_DISABLE_TEST_IN_SANITIZERS(YSQLSchemaPackingWithSnapshotGreaterVersionThanRestore)) {
  const std::string table_name = "test1";
  // Create a table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0(a INT)", table_name)));

  ASSERT_NO_FATALS(InsertRows(Format("INSERT INTO $0 VALUES (1), (2), (3)", table_name), 3));

  // Perform a series of Alters.
  ASSERT_NO_FATALS(
      RunPsqlCommand(Format("ALTER TABLE $0 ADD COLUMN b INT", table_name), "ALTER TABLE"));
  ASSERT_NO_FATALS(InsertRows(Format("INSERT INTO $0 VALUES (4,4), (5,5), (6,6)", table_name), 3));

  ASSERT_NO_FATALS(
      RunPsqlCommand(Format("ALTER TABLE $0 ADD COLUMN c INT", table_name), "ALTER TABLE"));
  ASSERT_NO_FATALS(
      InsertRows(Format("INSERT INTO $0 VALUES (7,7,7), (8,8,8), (9,9,9)", table_name), 3));

  ASSERT_NO_FATALS(
      RunPsqlCommand(Format("ALTER TABLE $0 RENAME COLUMN b TO d", table_name), "ALTER TABLE"));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY a", table_name),
      R"#(
         a | d | c
        ---+---+---
         1 |   |
         2 |   |
         3 |   |
         4 | 4 |
         5 | 5 |
         6 | 6 |
         7 | 7 | 7
         8 | 8 | 8
         9 | 9 | 9
        (9 rows)
      )#"));

  // Create some additional tables to exercise the colocation logic when mapping tables in the
  // snapshot to tables in the restored db. Only significant when the database is colocated.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE extra_table1 (k TEXT, v TEXT)", table_name)));
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE extra_table2 (k TEXT, v TEXT)", table_name)));

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", backup_db_name),
       "create"}));

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", restore_db_name),
       "restore"}));

  SetDbName(restore_db_name);

  ASSERT_NO_FATALS(InsertRows(Format("INSERT INTO $0 VALUES (0,0,0)", table_name), 1));

  // Check the data.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY a", table_name),
      R"#(
         a | d | c
        ---+---+---
         0 | 0 | 0
         1 |   |
         2 |   |
         3 |   |
         4 | 4 |
         5 | 5 |
         6 | 6 |
         7 | 7 | 7
         8 | 8 | 8
         9 | 9 | 9
        (10 rows)
      )#"));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_P(
    YBBackupTestWithPackedRowsAndColocation,
    YB_DISABLE_TEST_IN_SANITIZERS(YSQLSchemaPackingWithSnapshotLowerVersionThanRestore)) {
  const std::string table_name = "test1";
  // Create a table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0(a INT)", table_name)));

  ASSERT_NO_FATALS(InsertRows(Format("INSERT INTO $0 VALUES (1), (2), (3)", table_name), 3));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY a", table_name),
      R"#(
         a
        ---
         1
         2
         3
        (3 rows)
      )#"));

  // Create some additional tables to exercise the colocation logic when mapping tables in the
  // snapshot to tables in the restored db. Only significant when the database is colocated.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE extra_table1 (k TEXT, v TEXT)", table_name)));
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE extra_table2 (k TEXT, v TEXT)", table_name)));

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", backup_db_name),
       "create"}));

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", restore_db_name),
       "restore"}));

  SetDbName(restore_db_name);

  ASSERT_NO_FATALS(InsertRows(Format("INSERT INTO $0 VALUES (4), (5), (6)", table_name), 3));

  // Check the data.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY a", table_name),
      R"#(
         a
        ---
         1
         2
         3
         4
         5
         6
        (6 rows)
      )#"));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

INSTANTIATE_TEST_CASE_P(
    PackedRows, YBBackupTestWithPackedRowsAndColocation,
    ::testing::Values(std::make_tuple(PackedRowsEnabled::kTrue, SourceDatabaseIsColocated::kFalse),
        std::make_tuple(PackedRowsEnabled::kTrue, SourceDatabaseIsColocated::kTrue)));

class YBBackupCrossColocation : public YBBackupTestWithPackedRowsAndColocation {};

TEST_P(YBBackupCrossColocation, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLRestoreWithInvalidIndex)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE t1 (id INT NOT NULL, c1 INT, PRIMARY KEY (id))"));
  for (int i = 0; i < 3; ++i) {
    ASSERT_NO_FATALS(InsertOneRow(Format("INSERT INTO t1 (id, c1) VALUES ($0, $0)", i)));
  }
  ASSERT_NO_FATALS(CreateIndex("CREATE INDEX t1_c1_idx ON t1(c1)"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "UPDATE pg_index SET indisvalid='f' WHERE indexrelid = 't1_c1_idx'::regclass", "UPDATE 1"));
  const std::string backup_dir = GetTempDir("backup");
  const auto backup_keyspace = Format("ysql.$0", backup_db_name);
  const auto restore_keyspace = Format("ysql.$0", restore_db_name);
  ASSERT_OK(
      RunBackupCommand({"--backup_location", backup_dir, "--keyspace", backup_keyspace, "create"}));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", restore_keyspace, "restore"}));

  SetDbName(restore_db_name);

  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT * from t1 ORDER BY id;",
      R"#(
                 id | c1
                ----+----
                  0 |  0
                  1 |  1
                  2 |  2
                (3 rows)
      )#"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT indexname from pg_indexes where schemaname = 'public'",
      R"#(
                indexname
                -----------
                 t1_pkey
                (1 row)
      )#"));
  // Try to recreate the index to ensure import_snapshot didn't import any metadata for the invalid
  // index.
  ASSERT_NO_FATALS(CreateIndex("CREATE INDEX t1_c1_idx ON t1(c1)"));
}

INSTANTIATE_TEST_CASE_P(
    CrossColocationTests, YBBackupCrossColocation,
    ::testing::Values(
        std::make_tuple(PackedRowsEnabled::kFalse, SourceDatabaseIsColocated::kFalse),
        std::make_tuple(PackedRowsEnabled::kFalse, SourceDatabaseIsColocated::kTrue)));

TEST_P(
    YBBackupTestWithPackedRowsAndColocation,
    YB_DISABLE_TEST_IN_SANITIZERS(RestoreBackupAfterOldSchemaGC)) {
  const std::string table_name = "test1";
  // Create a table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0(a INT)", table_name)));

  ASSERT_NO_FATALS(InsertRows(Format("INSERT INTO $0 VALUES (1), (2), (3)", table_name), 3));

  // Perform a series of Alters.
  ASSERT_NO_FATALS(
      RunPsqlCommand(Format("ALTER TABLE $0 ADD COLUMN b INT", table_name), "ALTER TABLE"));
  ASSERT_NO_FATALS(InsertRows(Format("INSERT INTO $0 VALUES (4,4), (5,5), (6,6)", table_name), 3));

  ASSERT_NO_FATALS(
      RunPsqlCommand(Format("ALTER TABLE $0 ADD COLUMN c INT", table_name), "ALTER TABLE"));
  ASSERT_NO_FATALS(
      InsertRows(Format("INSERT INTO $0 VALUES (7,7,7), (8,8,8), (9,9,9)", table_name), 3));

  ASSERT_NO_FATALS(
      RunPsqlCommand(Format("ALTER TABLE $0 RENAME COLUMN b TO d", table_name), "ALTER TABLE"));
  ASSERT_NO_FATALS(
      InsertRows(Format("INSERT INTO $0 VALUES (9,9,9), (10,10,10), (11,11,11)", table_name), 3));

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", backup_db_name),
       "create"}));

  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(0), {},
      tserver::FlushTabletsRequestPB::COMPACT));
  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(1), {},
      tserver::FlushTabletsRequestPB::COMPACT));
  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(2), {},
      tserver::FlushTabletsRequestPB::COMPACT));

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", backup_db_name),
       "restore"}));

  SetDbName(backup_db_name);

  ASSERT_NO_FATALS(
      InsertRows(Format("INSERT INTO $0 VALUES (9,9,9), (10,10,10), (11,11,11)", table_name), 3));

  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(0), {},
      tserver::FlushTabletsRequestPB::COMPACT));
  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(1), {},
      tserver::FlushTabletsRequestPB::COMPACT));
  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(2), {},
      tserver::FlushTabletsRequestPB::COMPACT));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

class YBAddColumnDefaultBackupTest : public YBBackupTestWithPackedRowsAndColocation {};

TEST_P(YBAddColumnDefaultBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLDefaultMissingValues)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE t1 (id INT)"));
  ASSERT_NO_FATALS(InsertRows("INSERT INTO t1 VALUES (generate_series(1, 3))", 3));
  ASSERT_NO_FATALS(
      RunPsqlCommand("ALTER TABLE t1 ADD COLUMN c1 TEXT DEFAULT 'default'", "ALTER TABLE"));
  ASSERT_NO_FATALS(InsertRows("INSERT INTO t1 VALUES (generate_series(4, 6), null)", 3));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO t1 VALUES (7, 'not default')"));
  ASSERT_NO_FATALS(RunPsqlCommand("UPDATE t1 SET c1 = null WHERE id = 1", "UPDATE 1"));
  const std::string backup_dir = GetTempDir("backup");
  const auto backup_keyspace = Format("ysql.$0", backup_db_name);
  const auto restore_keyspace = Format("ysql.$0", restore_db_name);
  ASSERT_OK(
      RunBackupCommand({"--backup_location", backup_dir, "--keyspace", backup_keyspace, "create"}));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", restore_keyspace, "restore"}));

  SetDbName(restore_db_name);

  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT * from t1 ORDER BY id;",
      R"#(
                 id |     c1
                ----+-------------
                  1 |
                  2 | default
                  3 | default
                  4 |
                  5 |
                  6 |
                  7 | not default
                (7 rows)
      )#"));
}

INSTANTIATE_TEST_CASE_P(
    AddColumnDefault, YBAddColumnDefaultBackupTest,
    ::testing::Values(std::make_tuple(PackedRowsEnabled::kTrue, SourceDatabaseIsColocated::kFalse),
        std::make_tuple(PackedRowsEnabled::kTrue, SourceDatabaseIsColocated::kTrue),
        std::make_tuple(PackedRowsEnabled::kFalse, SourceDatabaseIsColocated::kFalse),
        std::make_tuple(PackedRowsEnabled::kTrue, SourceDatabaseIsColocated::kFalse)));

class YBDdlAtomicityBackupTest : public YBBackupTestBase, public pgwrapper::PgDdlAtomicityTestBase {
 public:
  Status RunDdlAtomicityTest(pgwrapper::DdlErrorInjection inject_error);
};

Status YBDdlAtomicityBackupTest::RunDdlAtomicityTest(pgwrapper::DdlErrorInjection inject_error) {
  // Start Yb Controllers for backup/restore.
  if (UseYbController()) {
    CHECK_OK(cluster_->StartYbControllerServers());
  }

  // Setup required tables.
  auto conn = VERIFY_RESULT(Connect());
  const int num_rows = 5;
  RETURN_NOT_OK(SetupTablesForAllDdls(&conn, num_rows));

  auto client = VERIFY_RESULT(cluster_->CreateClient());

  RETURN_NOT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "true"));

  if (inject_error) {
    // Run one failed DDL after pausing DDL rollback.
    RETURN_NOT_OK(RunOneDdlWithErrorInjection(&conn));
  } else {
    // Run all DDLs after pausing DDL rollback.
    RETURN_NOT_OK(RunAllDdls(&conn));
  }

  // Run backup and verify that it fails because DDL rollback is paused.
  const auto kDatabase = "yugabyte";
  const auto backup_keyspace = Format("ysql.$0", kDatabase);
  const std::string backup_dir = GetTempDir("backup");
  Status s = RunBackupCommand({"--backup_location", backup_dir, "--keyspace", backup_keyspace,
                               "create"}, cluster_.get());
  if (s.ok()) {
    return STATUS(IllegalState, "Backup should have failed because DDL in progress");
  }

  // Re-enable DDL rollback, wait for rollback to finish and run backup again.
  RETURN_NOT_OK(cluster_->SetFlagOnMasters("TEST_pause_ddl_rollback", "false"));

  if (inject_error) {
    RETURN_NOT_OK(WaitForDdlVerificationAfterDdlFailure(client.get(), kDatabase));
  } else {
    RETURN_NOT_OK(WaitForDdlVerificationAfterSuccessfulDdl(client.get(), kDatabase));
  }

  RETURN_NOT_OK(RunBackupCommand({"--backup_location", backup_dir, "--keyspace", backup_keyspace,
                                  "create"}, cluster_.get()));

  // Restore the backup.
  const auto restore_db = "restored_db";
  const auto restore_keyspace = Format("ysql.$0", restore_db);
  RETURN_NOT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", restore_keyspace, "restore"},
      cluster_.get()));

  // Verify that the tables in the restored database have the expected schema and data.
  auto restore_conn = VERIFY_RESULT(ConnectToDB(restore_db));

  if (inject_error) {
    RETURN_NOT_OK(VerifyAllFailingDdlsRolledBack(&restore_conn, client.get(), restore_db));
    return VerifyRowsAfterDdlErrorInjection(&restore_conn, num_rows);
  }
  RETURN_NOT_OK(VerifyAllSuccessfulDdls(&restore_conn, client.get(), restore_db));
  return VerifyRowsAfterDdlSuccess(&restore_conn, num_rows);
}

TEST_F(YBDdlAtomicityBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(SuccessfulDdlAtomicityTest)) {
  ASSERT_OK(RunDdlAtomicityTest(pgwrapper::DdlErrorInjection::kFalse));
}

TEST_F(YBDdlAtomicityBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(DdlRollbackAtomicityTest)) {
  ASSERT_OK(RunDdlAtomicityTest(pgwrapper::DdlErrorInjection::kTrue));
}

// 1. Create table
// 2. Create index on table
// 3. Insert 123 -> 456
// 4. Backup
// 5. Drop table and index.
// 5. Restore, validate 123 -> 456, index isn't restored
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYCQLKeyspaceBackupWithoutIndexes)) {
  // need to disable the test since yb controller doesn't support skipping indexes
  if (UseYbController()) {
    return;
  }
  // Create table and index.
  auto session = client_->NewSession(120s);
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(cluster_->num_tablet_servers()), client_.get(),
      &table_);

  client::TableHandle index_table;
  client::kv_table_test::CreateIndex(
      yb::client::Transactional::kFalse, 1, false, table_, client_.get(), &index_table);

  // Refresh table_ variable.
  ASSERT_OK(table_.Reopen());

  // Insert into table.
  const int32_t key = 123;
  const int32_t old_val = 456;
  ASSERT_OK(client::kv_table_test::WriteRow(&table_, session, key, old_val));

  // Backup.
  const string& keyspace = table_.name().namespace_name();
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", keyspace, "create", "--skip_indexes"}));

  // Drop table and index.
  ASSERT_OK(client_->DeleteTable(table_.name()));
  ASSERT_FALSE(ASSERT_RESULT(client_->TableExists(table_.name())));
  ASSERT_FALSE(ASSERT_RESULT(client_->TableExists(index_table.name())));

  // Restore.
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Create new YBTableNames since the old ones' id are outdated.
  const client::YBTableName table_name(YQL_DATABASE_CQL, keyspace, table_.name().table_name());
  const client::YBTableName index_table_name(
      YQL_DATABASE_CQL, keyspace, index_table.name().table_name());

  // Refresh table variable to the one newly created by restore.
  ASSERT_OK(table_.Open(table_name, client_.get()));

  // Verify nothing changed.
  auto rows = ASSERT_RESULT(client::kv_table_test::SelectAllRows(&table_, session));
  ASSERT_EQ(rows.size(), 1);
  ASSERT_EQ(rows[key], old_val);
  // Verify index table is not restored.
  ASSERT_FALSE(ASSERT_RESULT(client_->TableExists(index_table_name)));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

class YBBackupTestWithTableRewrite : public YBBackupTestWithPackedRowsAndColocation {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTestWithPackedRowsAndColocation::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--ysql_enable_reindex=true");
  }

  void SetUpTestData(const bool failedRewrite) {
    constexpr auto kRowCount = 5;
    for (auto table_name : {kTable, kTable2}) {
      // Create the table.
      ASSERT_NO_FATALS(CreateTable(
        Format("CREATE TABLE $0 (a int, b int, PRIMARY KEY (a ASC))", table_name)));
      // Insert some data.
      ASSERT_NO_FATALS(InsertRows(Format(
          "INSERT INTO $0 (a, b) VALUES (generate_series(1, $1), generate_series(1, $1))",
          table_name, kRowCount), kRowCount));
      // Create an index.
      ASSERT_NO_FATALS(CreateIndex(Format("CREATE INDEX $0 ON $1 (b DESC)",
          table_name == kTable ? kIndex : "idx2", kTable)));
    }
    // Create materialized view.
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("CREATE MATERIALIZED VIEW $0 AS SELECT * FROM $1", kMaterializedView, kTable),
        "SELECT 5"));
    // Insert some more data on the materialized view's base table.
    ASSERT_NO_FATALS(InsertOneRow(Format("INSERT INTO $0 (a, b) VALUES (6, 6)", kTable)));
    const auto setFailRewriteGuc = "SET yb_test_fail_table_rewrite_after_creation=true;";

    // Perform a rewrite operations on a table.
    ASSERT_NO_FATALS(RunPsqlCommand(Format(
        "$0 ALTER TABLE $1 ADD COLUMN c SERIAL", failedRewrite ? setFailRewriteGuc : "", kTable),
        failedRewrite ? "SET" : "ALTER TABLE"));
    // Perform a truncate operation.
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("$0 TRUNCATE TABLE $1", failedRewrite ? setFailRewriteGuc : "", kTable2),
        failedRewrite ? "SET" : "TRUNCATE TABLE"));
    // Perform a reindex operation on an index.
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("UPDATE pg_index SET indisvalid='f' WHERE indexrelid = '$0'::regclass", kIndex),
        "UPDATE 1"));
    ASSERT_NO_FATALS(RunPsqlCommand(Format(
        "$0 REINDEX INDEX $1", failedRewrite ? setFailRewriteGuc : "", kIndex),
        failedRewrite ? "SET" : "REINDEX"));
    // Refresh the materialized view.
    ASSERT_NO_FATALS(RunPsqlCommand(Format(
        "$0 REFRESH MATERIALIZED VIEW $1", failedRewrite ? setFailRewriteGuc : "",
            kMaterializedView),
        failedRewrite ? "SET" : "REFRESH MATERIALIZED VIEW"));
  }

  void BackupAndRestore() {
    // Take a backup.
    const auto backup_dir = GetTempDir("backup");
    ASSERT_OK(RunBackupCommand(
        {"--backup_location", backup_dir, "--keyspace", "ysql." + backup_db_name, "create"}));
    // Restore.
    ASSERT_OK(RunBackupCommand(
        {"--backup_location", backup_dir, "--keyspace", "ysql." + restore_db_name, "restore"}));
    SetDbName(restore_db_name); // Connecting to the second DB.
  }

  const std::string kTable = "t1";
  const std::string kTable2 = "t2";
  const std::string kIndex = "idx1";
  const std::string kMaterializedView = "mv";
};

INSTANTIATE_TEST_CASE_P(
    TableRewriteTests, YBBackupTestWithTableRewrite,
    ::testing::Values(
        std::make_tuple(PackedRowsEnabled::kFalse, SourceDatabaseIsColocated::kFalse),
        std::make_tuple(PackedRowsEnabled::kFalse, SourceDatabaseIsColocated::kTrue)));

// Test that backup and restore succeed after successful rewrite operations are executed
// on tables, indexes and materialized views.
TEST_P(YBBackupTestWithTableRewrite,
    YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLBackupAndRestoreAfterRewrite)) {
  SetUpTestData(false /* failedRewrite */);
  BackupAndRestore();
  // Verify that we restored everything correctly.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0", kTable),
      R"#(
         a | b | c
        ---+---+---
         1 | 1 | 1
         2 | 2 | 2
         3 | 3 | 3
         4 | 4 | 4
         5 | 5 | 5
         6 | 6 | 6
        (6 rows)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 WHERE b = 2", kTable),
      R"#(
         a | b | c
        ---+---+---
         2 | 2 | 2
        (1 row)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY a", kMaterializedView),
      R"#(
         a | b
        ---+---
         1 | 1
         2 | 2
         3 | 3
         4 | 4
         5 | 5
         6 | 6
        (6 rows)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0", kTable2),
      R"#(
         a | b
        ---+---
        (0 rows)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 WHERE b = 2", kTable2),
      R"#(
         a | b
        ---+---
        (0 rows)
      )#"
  ));
}

// Test that backup and restore succeed after unsuccessful rewrite operations are executed
// on tables, indexes and materialized views.
TEST_P(YBBackupTestWithTableRewrite,
    YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLBackupAndRestoreAfterFailedRewrite)) {
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_transactional_ddl_gc", "false"));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_ddl_rollback_enabled", "false"));
  SetUpTestData(true /* failedRewrite */);

  // Verify that the orphaned DocDB tables created still exist.
  vector<client::YBTableName> tables;
  for (auto table_name : {kTable, kTable2, kIndex, kMaterializedView}) {
    ASSERT_EQ(ASSERT_RESULT(client_->ListTables(table_name)).size(), 2);
  }

  BackupAndRestore();
  // Verify that we restored everything correctly.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0", kTable),
      R"#(
         a | b
        ---+---
         1 | 1
         2 | 2
         3 | 3
         4 | 4
         5 | 5
         6 | 6
        (6 rows)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 WHERE b = 2", kTable),
      R"#(
         a | b
        ---+---
         2 | 2
        (1 row)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY a", kMaterializedView),
      R"#(
         a | b
        ---+---
         1 | 1
         2 | 2
         3 | 3
         4 | 4
         5 | 5
        (5 rows)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0", kTable2),
      R"#(
         a | b
        ---+---
         1 | 1
         2 | 2
         3 | 3
         4 | 4
         5 | 5
        (5 rows)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 WHERE b = 2", kTable2),
      R"#(
         a | b
        ---+---
         2 | 2
        (1 row)
      )#"
  ));
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestBackupWithFailedLegacyRewrite)) {
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_enable_alter_table_rewrite", "false"));
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_transactional_ddl_gc", "false"));
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_ddl_rollback_enabled", "false"));

  const auto table_name = "t1";
  // Create the table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (a int, b int)", table_name)));
  // Insert some data.
  ASSERT_NO_FATALS(InsertRows(Format(
      "INSERT INTO $0 (a, b) VALUES (generate_series(1, 5), generate_series(1, 5))", table_name),
      5));
  // Create an index.
  ASSERT_NO_FATALS(CreateIndex(Format("CREATE INDEX idx1 ON $0 (b DESC)", table_name)));
  // Perform a failed ADD PKEY operation.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SET yb_test_fail_next_ddl = true;"
             "ALTER TABLE $0 ADD PRIMARY KEY (a ASC)", table_name), "SET"));
  // Verify the original table and the orphaned table exist.
  ASSERT_EQ(ASSERT_RESULT(client_->ListTables(table_name)).size(), 2);

  // Verify backup/restore works correctly.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));

  SetDbName("yugabyte_new");
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY a", table_name),
      R"#(
         a | b
        ---+---
         1 | 1
         2 | 2
         3 | 3
         4 | 4
         5 | 5
        (5 rows)
      )#"));
}
}  // namespace tools
}  // namespace yb
