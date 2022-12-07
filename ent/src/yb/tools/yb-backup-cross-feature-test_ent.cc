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

#include "yb/client/table_info.h"
#include "yb/client/ql-dml-test-base.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_backup.pb.h"

#include "yb/tools/yb-backup-test_base_ent.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/pb_util.h"

using namespace std::chrono_literals;
using namespace std::literals;

DECLARE_int32(TEST_partitioning_version);

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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 3);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
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
  ASSERT_TRUE(CheckPartitions(tablets, {"\x55\x55", "\xaa\xaa"}));

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
  // - 0x5555-0xa6e8
  // - 0xa6e8-0xaaaa
  // - 0xaaaa-
  // 0xa6e8 just happens to be what tablet splitting chooses.  Tablet splitting should choose the
  // split point based on the existing data.  Don't verify that it chose the right split point: that
  // is out of scope of this test.  Just trust what it chose.
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  ASSERT_TRUE(CheckPartitions(tablets, {"\x55\x55", "\xa6\xe8", "\xaa\xaa"}));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table so that, on restore, running the ysql_dump file recreates the table.  ysql_dump
  // should specify SPLIT INTO 4 TABLETS because the table in snapshot has 4 tablets.
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Before performing restore, demonstrate that the table that would be created by the ysql_dump
  // file will have the following even splits:
  // -       -0x3fff
  // - 0x3fff-0x7ffe
  // - 0x7ffe-0xbffd
  // - 0xbffd-
  // Note: If this test starts failing because of this, the default splits probably changed to
  // something more even like -0x4000, 0x4000-0x8000, and so forth.  Simply adjust the test
  // expectation here.
  ASSERT_NO_FATALS(CreateTable(
      Format("CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 4 TABLETS", table_name)));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), 4);
  ASSERT_TRUE(CheckPartitions(tablets, {"\x3f\xff", "\x7f\xfe", "\xbf\xfd"}));
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore should notice that the table it creates from ysql_dump file has different partition
  // boundaries from the one in the external snapshot EVEN THOUGH the number of partitions is four
  // in both, so it should recreate partitions to match the splits in the snapshot.
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Validate.
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), 4);
  ASSERT_TRUE(CheckPartitions(tablets, {"\x55\x55", "\xa6\xe8", "\xaa\xaa"}));
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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

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
  ASSERT_TRUE(CheckPartitions(tablets, {"S4a\0\0!"s}));

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
  ASSERT_EQ(tablets[split_index].partition().partition_key_end(), "S4a\0\0!"s);
  string tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  int num_tablets = 3;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ true, tablet_id));

  // Verify that it has these three tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  ASSERT_TRUE(CheckPartitions(tablets, {"S150a\0\0!"s, "S4a\0\0!"s}));

  // Further split at split depth 1
  // Choose the first tablet among tablets: "" --- "150a", "150a" --- "4a", and "4a" --- ""
  split_index = 0;
  ASSERT_EQ(tablets[split_index].partition().partition_key_start(), "");
  ASSERT_EQ(tablets[split_index].partition().partition_key_end(), "S150a\0\0!"s);
  tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  num_tablets = 4;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ false, tablet_id));

  // Verify that it has these four tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  ASSERT_TRUE(CheckPartitions(tablets, {"S133a\0\0!"s, "S150a\0\0!"s, "S4a\0\0!"s}));

  // Backup
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, table_name));
  ASSERT_EQ(tablets.size(), 4);
  ASSERT_TRUE(CheckPartitions(tablets, {"S133a\0\0!"s, "S150a\0\0!"s, "S4a\0\0!"s}));

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
  ASSERT_TRUE(CheckPartitions(tablets, {}));

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
  // '|' represents kNullHigh
  ASSERT_TRUE(CheckPartitions(tablets, {"|SG\230lH\200\000\001\000\001\027!!\000\000!"s}));

  // Further split at split depth 1
  split_index = 0;
  ASSERT_EQ(tablets[split_index].partition().partition_key_start(), "");
  ASSERT_EQ(tablets[split_index].partition().partition_key_end(),
            "|SG\230lH\200\000\001\000\001\027!!\000\000!"s);
  tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  num_tablets = 3;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, index_name, /* wait_for_parent_deletion */ true, tablet_id));

  // Verify that it has these three tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  ASSERT_TRUE(CheckPartitions(tablets, {"|SGYUH\200\000\001\000\0010!!\000\000!"s,
                                        "|SG\230lH\200\000\001\000\001\027!!\000\000!"s}));

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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 3);
  ASSERT_TRUE(CheckPartitions(tablets, {"|SGYUH\200\000\001\000\0010!!\000\000!"s,
                                        "|SG\230lH\200\000\001\000\001\027!!\000\000!"s}));

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
  ASSERT_TRUE(CheckPartitions(tablets, {}));

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
  // H\200\000\000\310 represents ascending-order integer value 200
  ASSERT_TRUE(CheckPartitions(tablets,
                              {"H\200\000\000\310SG\230lH\200\000\001\000\001\027!!\000\000!"s}));

  // Further split at split depth 1
  split_index = 0;
  ASSERT_EQ(tablets[split_index].partition().partition_key_start(), "");
  ASSERT_EQ(tablets[split_index].partition().partition_key_end(),
            "H\200\000\000\310SG\230lH\200\000\001\000\001\027!!\000\000!"s);
  tablet_id = tablets[split_index].tablet_id();

  // Split it && Wait for split to complete.
  num_tablets = 3;
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, index_name, /* wait_for_parent_deletion */ false, tablet_id));

  // Verify that it has these three tablets:
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), num_tablets);
  ASSERT_TRUE(CheckPartitions(tablets,
                              {"H\200\000\000\310SGq\317H\200\000\001\000\001\n!!\000\000!"s,
                               "H\200\000\000\310SG\230lH\200\000\001\000\001\027!!\000\000!"s}));

  // Backup
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Validate
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(default_db_, index_name));
  ASSERT_EQ(tablets.size(), 3);
  ASSERT_TRUE(CheckPartitions(tablets,
                              {"H\200\000\000\310SGq\317H\200\000\001\000\001\n!!\000\000!"s,
                               "H\200\000\000\310SG\230lH\200\000\001\000\001\027!!\000\000!"s}));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

class YBBackupAfterFailedMatviewRefresh : public YBBackupTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        "--enable_transactional_ddl_gc=false");
    options->extra_tserver_flags.push_back(
        "--TEST_yb_test_fail_matview_refresh_after_creation=true");
  }
};

// Test that backup and restore succeed when an orphaned table is left behind
// after a failed refresh on a materialized view.
TEST_F_EX(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLBackupAfterFailedMatviewRefresh),
       YBBackupAfterFailedMatviewRefresh) {
  const string kDatabaseName = "yugabyte";
  const string kNewDatabaseName = "yugabyte_new";

  const string base_table = "base";
  const string materialized_view = "mv";

  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (t int)", base_table)));
  ASSERT_NO_FATALS(InsertOneRow(Format("INSERT INTO $0 (t) VALUES (1)", base_table)));
  ASSERT_NO_FATALS(RunPsqlCommand(Format("CREATE MATERIALIZED VIEW $0 AS SELECT * FROM $1",
                                        materialized_view,
                                        base_table),
                                  "SELECT 1"));
  ASSERT_NO_FATALS(InsertOneRow(Format("INSERT INTO $0 (t) VALUES (1)", base_table)));
  ASSERT_NO_FATALS(RunPsqlCommand(Format("REFRESH MATERIALIZED VIEW $0", materialized_view), ""));
  const auto matview_table_id = ASSERT_RESULT(GetTableId(materialized_view, "pre-backup"));
  const auto matview_table_pg_oid = ASSERT_RESULT(GetPgsqlTableOid(TableId(matview_table_id)));
  // Naming convention in PG for the relation created as part of the REFRESH is
  // "pg_temp_<OID of matview>".
  const auto orphaned_mv = "pg_temp_" + std::to_string(matview_table_pg_oid);
  // Verify that the table created as a part of REFRESH still exists.
  ASSERT_TRUE(ASSERT_RESULT(client_->TableExists(
      client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, orphaned_mv))));

  // Take a backup, ensure that this passes despite the state of the orphaned_mv.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql." + kDatabaseName, "create"}));

  // Now try to restore the backup and ensure that only the original materialized view
  // was restored.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql." + kNewDatabaseName, "restore"}));
  SetDbName(kNewDatabaseName); // Connecting to the second DB.

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0", materialized_view),
      R"#(
         t
        ---
         1
        (1 row)
      )#"
  ));

  ASSERT_FALSE(ASSERT_RESULT(client_->TableExists(client::YBTableName(YQL_DATABASE_PGSQL,
      kNewDatabaseName, orphaned_mv))));
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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

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
  ASSERT_OK(WaitForTabletFullyCompacted(leader_idx, tablets[0].tablet_id()));
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      default_db_, table_name, /* wait_for_parent_deletion */ kWaitForParentDeletion,
      tablets[0].tablet_id()));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(db_name, table_name));
  ASSERT_EQ(tablets.size(), /* expected_num_tablets = */ 3);
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestColocationDuplication)) {
  // Create a colocated database.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "CREATE DATABASE demo WITH COLOCATED=TRUE", "CREATE DATABASE"));

  // Set this database for creating tables below.
  SetDbName("demo");

  // Create 10 tables in a loop and insert data.
  vector<string> table_names;
  for (int i = 0; i < 10; ++i) {
    table_names.push_back(Format("mytbl_$0", i));
  }
  for (const auto& table_name : table_names) {
    ASSERT_NO_FATALS(CreateTable(
        Format("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name)));
    ASSERT_NO_FATALS(InsertRows(
        Format("INSERT INTO $0 VALUES (generate_series(1, 100))", table_name), 100));
  }
  LOG(INFO) << "All tables created and data inserted successsfully";

  // Create a backup.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.demo", "create"}));
  LOG(INFO) << "Backup finished";

  // Read the SnapshotInfoPB from the given path.
  master::SnapshotInfoPB snapshot_info;
  ASSERT_OK(pb_util::ReadPBContainerFromPath(
      Env::Default(), JoinPathSegments(backup_dir, "SnapshotInfoPB"), &snapshot_info));
  LOG(INFO) << "SnapshotInfoPB: " << snapshot_info.ShortDebugString();

  // SnapshotInfoPB should contain 1 namespace entry, 1 tablet entry and 11 table entries.
  // 11 table entries comprise of - 10 entries for the tables created and 1 entry for
  // the parent colocated table.
  int32_t num_namespaces = 0, num_tables = 0, num_tablets = 0, num_others = 0;
  for (const auto& entry : snapshot_info.backup_entries()) {
    if (entry.entry().type() == master::SysRowEntryType::NAMESPACE) {
      num_namespaces++;
    } else if (entry.entry().type() == master::SysRowEntryType::TABLE) {
      num_tables++;
    } else if (entry.entry().type() == master::SysRowEntryType::TABLET) {
      num_tablets++;
    } else {
      num_others++;
    }
  }

  ASSERT_EQ(num_namespaces, 1);
  ASSERT_EQ(num_tablets, 1);
  ASSERT_EQ(num_tables, 11);
  ASSERT_EQ(num_others, 0);
  // Snapshot should be complete.
  ASSERT_EQ(snapshot_info.entry().state(),
            master::SysSnapshotEntryPB::State::SysSnapshotEntryPB_State_COMPLETE);
  // We clear all tablet snapshot entries for backup.
  ASSERT_EQ(snapshot_info.entry().tablet_snapshots_size(), 0);
  // We've migrated this field to backup_entries so they are already accounted above.
  ASSERT_EQ(snapshot_info.entry().entries_size(), 0);

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));
  LOG(INFO) << "Restored backup to yugabyte_new keyspace successfully";

  SetDbName("yugabyte_new");

  // Post-restore, we should have all the data.
  for (const auto& table_name : table_names) {
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("SELECT COUNT(*) FROM $0", table_name),
        R"#(
           count
          -------
             100
          (1 row)
        )#"));
  }

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

class YBBackupTestWithPackedRows : public YBBackupTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    // Add flags to enable packed row feature.
    options->extra_master_flags.push_back("--enable_automatic_tablet_splitting=false");
    options->extra_tserver_flags.push_back("--ysql_enable_packed_row=true");
  }
};

TEST_F(YBBackupTestWithPackedRows,
    YB_DISABLE_TEST_IN_SANITIZERS(YSQLSchemaPackingWithSnapshotGreaterVersionThanRestore)) {
  const std::string table_name = "test1";
  // Create a table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0(a INT)", table_name)));

  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (1), (2), (3)", table_name), 3));

  // Perform a series of Alters.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("ALTER TABLE $0 ADD COLUMN b INT", table_name), "ALTER TABLE"));
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (4,4), (5,5), (6,6)", table_name), 3));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("ALTER TABLE $0 ADD COLUMN c INT", table_name), "ALTER TABLE"));
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (7,7,7), (8,8,8), (9,9,9)", table_name), 3));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("ALTER TABLE $0 RENAME COLUMN b TO d", table_name), "ALTER TABLE"));

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
      )#"
  ));

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Restore into new "ysql.yugabyte2" YSQL DB.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte2", "restore"}));

  SetDbName("yugabyte2");

  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (0,0,0)", table_name), 1));

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
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTestWithPackedRows,
    YB_DISABLE_TEST_IN_SANITIZERS(YSQLSchemaPackingWithSnapshotLowerVersionThanRestore)) {
  const std::string table_name = "test1";
  // Create a table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0(a INT)", table_name)));

  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (1), (2), (3)", table_name), 3));

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY a", table_name),
      R"#(
         a
        ---
         1
         2
         3
        (3 rows)
      )#"
  ));

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Restore into new "ysql.yugabyte2" YSQL DB.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte2", "restore"}));

  SetDbName("yugabyte2");

  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (4), (5), (6)", table_name), 3));

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
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace tools
}  // namespace yb
