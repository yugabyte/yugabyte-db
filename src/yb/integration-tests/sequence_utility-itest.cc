// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include <gmock/gmock.h>

#include "yb/master/master_ddl.pb.h"
#include "yb/master/ysql_sequence_util.h"

#include "yb/tablet/tablet.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb::master {

const NamespaceName kNamespaceName = "db1";

class SequencesUtilTest : public pgwrapper::PgMiniTestBase {
 public:
  SequencesUtilTest() = default;

  void SetUp() override {
    pgwrapper::PgMiniTestBase::SetUp();
    google::SetVLOGLevel("sequence_util*", 4);
    namespace_id_ = ASSERT_RESULT(CreateYsqlNamespace(kNamespaceName));
    namespace_oid_ = ASSERT_RESULT(GetPgsqlDatabaseOid(namespace_id_));
  }

  Result<NamespaceId> CreateYsqlNamespace(const NamespaceName& namespace_name) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0", namespace_name));

    master::GetNamespaceInfoResponsePB resp;
    RETURN_NOT_OK(client_->GetNamespaceInfo(
        {} /* namespace_id */, namespace_name, YQL_DATABASE_PGSQL, &resp));
    return resp.namespace_().id();
  }

  Status CreateSampleSequences() {
    auto conn = VERIFY_RESULT(ConnectToDB(kNamespaceName));
    RETURN_NOT_OK(conn.Execute("CREATE SEQUENCE basic_sequence START WITH 1"));

    RETURN_NOT_OK(conn.Execute("CREATE SEQUENCE bumped_sequence START WITH 10"));
    RETURN_NOT_OK(conn.Fetch("SELECT nextval('bumped_sequence')"));

    RETURN_NOT_OK(conn.Execute("CREATE SEQUENCE altered_sequence START WITH 20"));
    // Altering a sequence is a breaking change for PG catalog, incrementing the breaking PG
    // catalog version (for this DB).
    RETURN_NOT_OK(conn.Execute("ALTER SEQUENCE altered_sequence RESTART WITH 21"));

    RETURN_NOT_OK(conn.Execute("CREATE SEQUENCE set_sequence START WITH 30"));
    // Below sets last_value=31, is_called=true.
    RETURN_NOT_OK(conn.Fetch("SELECT pg_catalog.setval('set_sequence', 31, true)"));

    RETURN_NOT_OK(conn.Execute("CREATE SEQUENCE dropped_sequence START WITH 40"));
    RETURN_NOT_OK(conn.Execute("DROP SEQUENCE dropped_sequence"));

    RETURN_NOT_OK(conn.Execute("CREATE SEQUENCE extra_sequence START WITH 50"));

    const NamespaceName unscanned_namespace_name = "unscanned_database";
    RETURN_NOT_OK(CreateYsqlNamespace(unscanned_namespace_name));
    auto conn_2 = VERIFY_RESULT(ConnectToDB(unscanned_namespace_name));
    RETURN_NOT_OK(conn_2.Execute("CREATE SEQUENCE sequence_in_a_different_database START 99999"));

    return Status::OK();
  }

  // Using Postgres, compute the results we expect to see from a scan of database kNamespaceName.
  //
  // Returns one string for each expected row formatted as "<oid>, <last_value>, <is_called>",
  // with <is_called> being 0 or 1.
  Result<std::vector<std::string>> GetSequencesData(
      std::optional<Timestamp> read_time = std::nullopt) {
    std::vector<std::string> results;
    auto conn = VERIFY_RESULT(ConnectToDB(kNamespaceName));
    if (read_time) {
      RETURN_NOT_OK(conn.ExecuteFormat("SET yb_read_time TO $0", read_time->ToInt64()));
    }
    auto sequence_names =
        VERIFY_RESULT(conn.FetchRows<std::string>("SELECT sequencename FROM pg_sequences"));
    for (const auto& sequence_name : sequence_names) {
      auto result = VERIFY_RESULT(conn.FetchRowAsString(Format(
          "SELECT pg_class.oid, last_value, is_called FROM $0 "
          "JOIN pg_class ON relname='$0'",
          sequence_name)));
      LOG(INFO) << "Expected scan row for " << sequence_name << ": " << result;
      results.push_back(result);
    }
    return results;
  }

  void VerifyScan(std::vector<std::string> expected, std::vector<YsqlSequenceInfo> actual) {
    std::vector<std::string> actual_as_strings;
    for (const auto& a : actual) {
      actual_as_strings.push_back(Format("$0, $1, $2", a.sequence_oid, a.last_value, a.is_called));
    }
    sort(expected.begin(), expected.end());
    EXPECT_THAT(actual_as_strings, testing::WhenSorted(testing::ContainerEq(expected)));
  }

  Result<Timestamp> GetCurrentTime() {
    auto time = Timestamp(VERIFY_RESULT(WallClock()->Now()).time_point);
    LOG(INFO) << "Current time: " << time.ToHumanReadableTime();
    return time;
  }

  NamespaceId namespace_id_;
  uint32_t namespace_oid_;
};

TEST_F(SequencesUtilTest, ScanWhenSequencesDataTableNonexistentGivesNotFound) {
  // Expect failure because sequences_data table has not been created yet.
  auto result = master::ScanSequencesDataTable(*client_.get(), namespace_oid_);
  ASSERT_NOK(result);
  ASSERT_TRUE(result.status().IsNotFound());
  ASSERT_NOK_STR_CONTAINS(
      result.status(), "Table with identifier 0000ffff00003000800000000000ffff not found");
}

TEST_F(SequencesUtilTest, ScanReturnsNothingForNoSequences) {
  // Create sequences_data table but leave no sequences existing.
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("CREATE SEQUENCE foo"));
  ASSERT_OK(conn.Execute("DROP SEQUENCE foo"));
  {
    auto results = master::ScanSequencesDataTable(*client_.get(), namespace_oid_);
    ASSERT_OK(results);
    ASSERT_EQ(0, results->size());
  }

  // Create sequences but scan an empty database.
  ASSERT_OK(CreateSampleSequences());
  auto empty_namespace_id = ASSERT_RESULT(CreateYsqlNamespace("empty_database"));
  auto empty_namespace_oid = ASSERT_RESULT(GetPgsqlDatabaseOid(empty_namespace_id));
  {
    auto results = master::ScanSequencesDataTable(*client_.get(), empty_namespace_oid);
    ASSERT_OK(results);
    ASSERT_EQ(0, results->size());
  }

  // Same but instead scan a nonexistent database OID.
  {
    auto results =
        master::ScanSequencesDataTable(*client_.get(), /*nonexistent database OID*/ 666666);
    ASSERT_OK(results);
    ASSERT_EQ(0, results->size());
  }
}

TEST_F(SequencesUtilTest, ScanSampleSequences) {
  ASSERT_OK(CreateSampleSequences());
  auto expected = ASSERT_RESULT(GetSequencesData());
  auto actual = ASSERT_RESULT(master::ScanSequencesDataTable(*client_.get(), namespace_oid_));
  VerifyScan(expected, actual);
}

TEST_F(SequencesUtilTest, ScanWithPaging) {
  ASSERT_OK(CreateSampleSequences());
  auto expected = ASSERT_RESULT(GetSequencesData());
  {
    auto actual = ASSERT_RESULT(
        master::ScanSequencesDataTable(*client_.get(), namespace_oid_, /*max_rows_per_read=*/1));
    VerifyScan(expected, actual);
  }
  {
    auto actual = ASSERT_RESULT(
        master::ScanSequencesDataTable(*client_.get(), namespace_oid_, /*max_rows_per_read=*/2));
    VerifyScan(expected, actual);
  }
}

TEST_F(SequencesUtilTest, ScanWithReadFailure) {
  ASSERT_OK(CreateSampleSequences());
  auto result = master::ScanSequencesDataTable(
      *client_.get(), namespace_oid_, /*max_rows_per_read=*/1000, /*TEST_fail_read=*/true);
  ASSERT_NOK(result);
  LOG(INFO) << "return status is " << result.status();
}

TEST_F(SequencesUtilTest, EnsureSequenceUpdatesAreInWalWhenNoChanges) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_log_write_batches) = true;
  ASSERT_OK(CreateSampleSequences());
  auto expected = ASSERT_RESULT(GetSequencesData());
  auto sequences = ASSERT_RESULT(master::ScanSequencesDataTable(*client_.get(), namespace_oid_));
  auto updates =
      ASSERT_RESULT(EnsureSequenceUpdatesAreInWal(*client_.get(), namespace_oid_, sequences));

  // For this test we assume there were no changes since the scan used to call
  // EnsureSequenceUpdatesAreInWal was done.  Thus EnsureSequenceUpdatesAreInWal should have made
  // one update for each sequence.
  ASSERT_EQ(updates, expected.size());

  // Ensure the updates it made are nops.
  {
    auto actual = ASSERT_RESULT(master::ScanSequencesDataTable(*client_.get(), namespace_oid_));
    VerifyScan(expected, actual);
  }
}

TEST_F(SequencesUtilTest, EnsureSequenceUpdatesAreInWalWithConcurrentChanges) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_log_write_batches) = true;
  ASSERT_OK(CreateSampleSequences());
  auto sequences = ASSERT_RESULT(master::ScanSequencesDataTable(*client_.get(), namespace_oid_));

  // Here we are testing changes that occur between the scan used to get the sequence information
  // and the call to EnsureSequenceUpdatesAreInWal.  This should make EnsureSequenceUpdatesAreInWal
  // do fewer updates because it should not update changes since the scan was done.

  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.Execute("ALTER SEQUENCE altered_sequence RESTART WITH 22"));
  // Change just is_called.
  ASSERT_OK(conn.Fetch("SELECT pg_catalog.setval('set_sequence', 31, false)"));
  ASSERT_OK(conn.Execute("DROP SEQUENCE extra_sequence"));

  auto updates =
      ASSERT_RESULT(EnsureSequenceUpdatesAreInWal(*client_.get(), namespace_oid_, sequences));
  ASSERT_EQ(updates, sequences.size() - /*number of sequences changed above*/ 3);

  // Ensure the updates it did make are nops.
  {
    auto expected = ASSERT_RESULT(GetSequencesData());
    auto actual = ASSERT_RESULT(master::ScanSequencesDataTable(*client_.get(), namespace_oid_));
    VerifyScan(expected, actual);
  }
}

TEST_F(SequencesUtilTest, ReadSequencesAsOfTime) {
  auto kSequenceName = "seq_1";
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn.ExecuteFormat("CREATE SEQUENCE $0 START WITH 1", kSequenceName));
  auto time1 = ASSERT_RESULT(GetCurrentTime());
  auto ground_truth_seq_time1 = ASSERT_RESULT(GetSequencesData());
  ASSERT_EQ(ground_truth_seq_time1.size(), 1);
  // Fetch next value which bumps the last_value of the sequence by FLAGS_ysql_sequence_cache_minval
  // and sets is_called to true
  ASSERT_OK(conn.FetchFormat("SELECT nextval('$0')", kSequenceName));
  auto time2 = ASSERT_RESULT(GetCurrentTime());
  auto ground_truth_seq_time2 = ASSERT_RESULT(GetSequencesData());
  // Bump the sequence last_value again by getting nextval from a new session. Asking current
  // session for nextval won't bump the last_value until the cache is exhausted.
  auto conn2 = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  ASSERT_OK(conn2.FetchFormat("SELECT nextval('$0')", kSequenceName));
  auto ground_truth_seq_cur_time = ASSERT_RESULT(GetSequencesData());

  // Check that the sequence info as of time1 is equal to the ground truth at time1
  auto expected_seq_time1 = ASSERT_RESULT(GetSequencesData(time1));
  ASSERT_EQ(expected_seq_time1.size(), 1);
  ASSERT_STR_EQ(expected_seq_time1[0], ground_truth_seq_time1[0]);
  // Check for time2
  auto expected_seq_time2 = ASSERT_RESULT(GetSequencesData(time2));
  ASSERT_EQ(expected_seq_time2.size(), 1);
  ASSERT_STR_EQ(expected_seq_time2[0], ground_truth_seq_time2[0]);
  // Check that resetting yb_read_time to 0 returns the current state of the sequence
  auto expected_seq_cur_time = ASSERT_RESULT(GetSequencesData());
  ASSERT_EQ(expected_seq_cur_time.size(), 1);
  ASSERT_STR_EQ(expected_seq_cur_time[0], ground_truth_seq_cur_time[0]);
}

}  // namespace yb::master
