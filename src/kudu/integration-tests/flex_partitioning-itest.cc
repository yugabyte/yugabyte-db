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
// Integration test for flexible partitioning (eg buckets, range partitioning
// of PK subsets, etc).

#include <algorithm>
#include <glog/stl_logging.h>
#include <map>
#include <memory>
#include <vector>

#include "kudu/client/client-test-util.h"
#include "kudu/common/partial_row.h"
#include "kudu/integration-tests/external_mini_cluster.h"
#include "kudu/integration-tests/cluster_itest_util.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/tools/data_gen_util.h"
#include "kudu/util/random.h"
#include "kudu/util/random_util.h"
#include "kudu/util/test_util.h"
#include "kudu/gutil/strings/escaping.h"

namespace kudu {
namespace itest {

using client::KuduClient;
using client::KuduClientBuilder;
using client::KuduColumnSchema;
using client::KuduInsert;
using client::KuduPredicate;
using client::KuduScanner;
using client::KuduSchema;
using client::KuduSchemaBuilder;
using client::KuduSession;
using client::KuduTable;
using client::KuduTableCreator;
using client::KuduValue;
using client::sp::shared_ptr;
using std::unordered_map;
using std::vector;
using strings::Substitute;

static const char* const kTableName = "test-table";
static const int kNumRows = 1000;

class FlexPartitioningITest : public KuduTest {
 public:
  FlexPartitioningITest()
    : random_(GetRandomSeed32()) {
  }
  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();

    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    // This test produces lots of tablets. With container and log preallocation,
    // we end up using quite a bit of disk space. So, we disable them.
    opts.extra_tserver_flags.push_back("--log_container_preallocate_bytes=0");
    opts.extra_tserver_flags.push_back("--log_preallocate_segments=false");
    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    KuduClientBuilder builder;
    ASSERT_OK(cluster_->CreateClient(builder, &client_));

    ASSERT_OK(itest::CreateTabletServerMap(cluster_->master_proxy().get(),
                                           cluster_->messenger(),
                                           &ts_map_));
  }

  virtual void TearDown() OVERRIDE {
    cluster_->Shutdown();
    KuduTest::TearDown();
    STLDeleteValues(&ts_map_);
    STLDeleteElements(&inserted_rows_);
  }

 protected:
  void CreateTable(int num_columns,
                   const vector<string>& bucket_a, int num_buckets_a,
                   const vector<string>& bucket_b, int num_buckets_b,
                   const vector<string>& range_cols,
                   int num_splits) {
    // Set up the actual PK columns based on num_columns. The PK is made up
    // of all the columns.
    KuduSchemaBuilder b;
    vector<string> pk;
    for (int i = 0; i < num_columns; i++) {
      string name = Substitute("c$0", i);
      b.AddColumn(name)->Type(KuduColumnSchema::INT32)->NotNull();
      pk.push_back(name);
    }
    b.SetPrimaryKey(pk);
    KuduSchema schema;
    ASSERT_OK(b.Build(&schema));

    gscoped_ptr<KuduTableCreator> table_creator(client_->NewTableCreator());
    table_creator->table_name(kTableName)
        .schema(&schema)
        .num_replicas(1);

    // Set up partitioning.
    if (!bucket_a.empty()) {
      table_creator->add_hash_partitions(bucket_a, num_buckets_a);
    }
    if (!bucket_b.empty()) {
      table_creator->add_hash_partitions(bucket_b, num_buckets_b);
    }
    table_creator->set_range_partition_columns(range_cols);

    // Compute split points.
    vector<const KuduPartialRow*> split_rows;
    int increment = kNumRows / num_splits;
    for (int i = 1; i < num_splits; i++) {
      KuduPartialRow* row = schema.NewRow();
      for (int j = 0; j < range_cols.size(); j++) {
        const string& range_col = range_cols[j];
        if (j == 0) {
          // Set the first component of the range to a set increment.
          ASSERT_OK(row->SetInt32(range_col, increment * i));
        } else {
          ASSERT_OK(row->SetInt32(range_col, random_.Next32()));
        }
      }
      split_rows.push_back(row);
    }
    table_creator->split_rows(split_rows);

    ASSERT_OK(table_creator->Create());

    ASSERT_OK(client_->OpenTable(kTableName, &table_));
  }

  int CountTablets() {
    vector<tserver::ListTabletsResponsePB_StatusAndSchemaPB> tablets;
    CHECK_OK(ListTablets(ts_map_.begin()->second, MonoDelta::FromSeconds(10), &tablets));
    return tablets.size();
  }

  // Insert 'kNumRows' rows into the given table. The first column 'c0' is ascending,
  // but the rest are random int32s.
  Status InsertRandomRows();

  // Perform a scan with a predicate on 'col_name' BETWEEN 'lower' AND 'upper'.
  // Verifies that the results match up with applying the same scan against our
  // in-memory copy 'inserted_rows_'.
  void CheckScanWithColumnPredicate(Slice col_name, int lower, int upper);

  // Like the above, but uses the primary key range scan API in the client to
  // scan between 'inserted_rows_[lower]' (inclusive) and 'inserted_rows_[upper]'
  // (exclusive).
  void CheckPKRangeScan(int lower, int upper);
  void CheckPartitionKeyRangeScanWithPKRange(int lower, int upper);

  // Performs a series of scans, each over a single tablet in the table, and
  // verifies that the aggregated results match up with 'inserted_rows_'.
  void CheckPartitionKeyRangeScan();

  // Inserts data into the table, then performs a number of scans to verify that
  // the data can be retrieved.
  void InsertAndVerifyScans();

  Random random_;

  gscoped_ptr<ExternalMiniCluster> cluster_;
  unordered_map<string, TServerDetails*> ts_map_;

  shared_ptr<KuduClient> client_;
  shared_ptr<KuduTable> table_;
  vector<KuduPartialRow*> inserted_rows_;
};

Status FlexPartitioningITest::InsertRandomRows() {
  CHECK(inserted_rows_.empty());

  shared_ptr<KuduSession> session(client_->NewSession());
  session->SetTimeoutMillis(10000);
  RETURN_NOT_OK(session->SetFlushMode(KuduSession::MANUAL_FLUSH));
  for (uint64_t i = 0; i < kNumRows; i++) {
    gscoped_ptr<KuduInsert> insert(table_->NewInsert());
    tools::GenerateDataForRow(table_->schema(), i, &random_, insert->mutable_row());
    inserted_rows_.push_back(new KuduPartialRow(*insert->mutable_row()));
    RETURN_NOT_OK(session->Apply(insert.release()));

    if (i > 0 && i % 1000 == 0) {
      RETURN_NOT_OK(session->Flush());
    }
  }
  RETURN_NOT_OK(session->Flush());
  return Status::OK();
}

void FlexPartitioningITest::CheckScanWithColumnPredicate(Slice col_name, int lower, int upper) {
  KuduScanner scanner(table_.get());
  scanner.SetTimeoutMillis(60000);
  CHECK_OK(scanner.AddConjunctPredicate(table_->NewComparisonPredicate(
      col_name, KuduPredicate::GREATER_EQUAL, KuduValue::FromInt(lower))));
  CHECK_OK(scanner.AddConjunctPredicate(table_->NewComparisonPredicate(
      col_name, KuduPredicate::LESS_EQUAL, KuduValue::FromInt(upper))));

  vector<string> rows;
  ScanToStrings(&scanner, &rows);
  std::sort(rows.begin(), rows.end());

  // Manually evaluate the predicate against the data we think we inserted.
  vector<string> expected_rows;
  for (const KuduPartialRow* row : inserted_rows_) {
    int32_t val;
    CHECK_OK(row->GetInt32(col_name, &val));
    if (val >= lower && val <= upper) {
      expected_rows.push_back("(" + row->ToString() + ")");
    }
  }
  std::sort(expected_rows.begin(), expected_rows.end());

  ASSERT_EQ(expected_rows.size(), rows.size());
  ASSERT_EQ(expected_rows, rows);
}

void FlexPartitioningITest::CheckPKRangeScan(int lower, int upper) {
  KuduScanner scanner(table_.get());
  scanner.SetTimeoutMillis(60000);
  ASSERT_OK(scanner.AddLowerBound(*inserted_rows_[lower]));
  ASSERT_OK(scanner.AddExclusiveUpperBound(*inserted_rows_[upper]));
  vector<string> rows;
  ScanToStrings(&scanner, &rows);
  std::sort(rows.begin(), rows.end());

  vector<string> expected_rows;
  for (int i = lower; i < upper; i++) {
    expected_rows.push_back("(" + inserted_rows_[i]->ToString() + ")");
  }
  std::sort(expected_rows.begin(), expected_rows.end());

  ASSERT_EQ(rows.size(), expected_rows.size());
  ASSERT_EQ(rows, expected_rows);
}

void FlexPartitioningITest::CheckPartitionKeyRangeScan() {
  master::GetTableLocationsResponsePB table_locations;
  ASSERT_OK(GetTableLocations(cluster_->master_proxy(),
                    table_->name(),
                    MonoDelta::FromSeconds(32),
                    &table_locations));

  vector<string> rows;

  for (const master::TabletLocationsPB& tablet_locations :
                table_locations.tablet_locations()) {

    string partition_key_start = tablet_locations.partition().partition_key_start();
    string partition_key_end = tablet_locations.partition().partition_key_end();

    KuduScanner scanner(table_.get());
    scanner.SetTimeoutMillis(60000);
    ASSERT_OK(scanner.AddLowerBoundPartitionKeyRaw(partition_key_start));
    ASSERT_OK(scanner.AddExclusiveUpperBoundPartitionKeyRaw(partition_key_end));
    ScanToStrings(&scanner, &rows);
  }
  std::sort(rows.begin(), rows.end());

  vector<string> expected_rows;
  for (KuduPartialRow* row : inserted_rows_) {
    expected_rows.push_back("(" + row->ToString() + ")");
  }
  std::sort(expected_rows.begin(), expected_rows.end());

  ASSERT_EQ(rows.size(), expected_rows.size());
  ASSERT_EQ(rows, expected_rows);
}

void FlexPartitioningITest::CheckPartitionKeyRangeScanWithPKRange(int lower, int upper) {
  master::GetTableLocationsResponsePB table_locations;
  ASSERT_OK(GetTableLocations(cluster_->master_proxy(),
                    table_->name(),
                    MonoDelta::FromSeconds(32),
                    &table_locations));

  vector<string> rows;

  for (const master::TabletLocationsPB& tablet_locations :
                table_locations.tablet_locations()) {

    string partition_key_start = tablet_locations.partition().partition_key_start();
    string partition_key_end = tablet_locations.partition().partition_key_end();

    KuduScanner scanner(table_.get());
    scanner.SetTimeoutMillis(60000);
    ASSERT_OK(scanner.AddLowerBoundPartitionKeyRaw(partition_key_start));
    ASSERT_OK(scanner.AddExclusiveUpperBoundPartitionKeyRaw(partition_key_end));
    ASSERT_OK(scanner.AddLowerBound(*inserted_rows_[lower]));
    ASSERT_OK(scanner.AddExclusiveUpperBound(*inserted_rows_[upper]));
    ScanToStrings(&scanner, &rows);
  }
  std::sort(rows.begin(), rows.end());

  vector<string> expected_rows;
  for (int i = lower; i < upper; i++) {
    expected_rows.push_back("(" + inserted_rows_[i]->ToString() + ")");
  }
  std::sort(expected_rows.begin(), expected_rows.end());

  ASSERT_EQ(rows.size(), expected_rows.size());
  ASSERT_EQ(rows, expected_rows);
}

void FlexPartitioningITest::InsertAndVerifyScans() {
  ASSERT_OK(InsertRandomRows());

  // First, ensure that we get back the same number we put in.
  {
    vector<string> rows;
    ScanTableToStrings(table_.get(), &rows);
    std::sort(rows.begin(), rows.end());
    ASSERT_EQ(kNumRows, rows.size());
  }

  // Perform some scans with predicates.

  // 1) Various predicates on 'c0', which has non-random data.
  // We concentrate around the value '500' since there is a split point
  // there.
  NO_FATALS(CheckScanWithColumnPredicate("c0", 100, 120));
  NO_FATALS(CheckScanWithColumnPredicate("c0", 490, 610));
  NO_FATALS(CheckScanWithColumnPredicate("c0", 499, 499));
  NO_FATALS(CheckScanWithColumnPredicate("c0", 500, 500));
  NO_FATALS(CheckScanWithColumnPredicate("c0", 501, 501));
  NO_FATALS(CheckScanWithColumnPredicate("c0", 499, 501));
  NO_FATALS(CheckScanWithColumnPredicate("c0", 499, 500));
  NO_FATALS(CheckScanWithColumnPredicate("c0", 500, 501));

  // 2) Random range predicates on the other columns, which are random ints.
  for (int col_idx = 1; col_idx < table_->schema().num_columns(); col_idx++) {
    SCOPED_TRACE(col_idx);
    for (int i = 0; i < 10; i++) {
      int32_t lower = random_.Next32();
      int32_t upper = random_.Next32();
      if (upper < lower) {
        std::swap(lower, upper);
      }

      NO_FATALS(CheckScanWithColumnPredicate(table_->schema().Column(col_idx).name(),
                                             lower, upper));
    }
  }

  // 3) Use the "primary key range" API.
  {
    NO_FATALS(CheckPKRangeScan(100, 120));
    NO_FATALS(CheckPKRangeScan(490, 610));
    NO_FATALS(CheckPKRangeScan(499, 499));
    NO_FATALS(CheckPKRangeScan(500, 500));
    NO_FATALS(CheckPKRangeScan(501, 501));
    NO_FATALS(CheckPKRangeScan(499, 501));
    NO_FATALS(CheckPKRangeScan(499, 500));
    NO_FATALS(CheckPKRangeScan(500, 501));
  }

  // 4) Use the Per-tablet "partition key range" API.
  {
    NO_FATALS(CheckPartitionKeyRangeScan());
  }

  // 5) Use the Per-tablet "partition key range" API with primary key range.
  {
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(100, 120));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(200, 400));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(490, 610));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(499, 499));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(500, 500));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(501, 501));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(499, 501));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(499, 500));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(500, 501));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(650, 700));
    NO_FATALS(CheckPartitionKeyRangeScanWithPKRange(700, 800));
  }
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   RANGE PARTITION BY (c0, c1),
// );
TEST_F(FlexPartitioningITest, TestSimplePartitioning) {
  NO_FATALS(CreateTable(1, // 2 columns
                        vector<string>(), 0, // No hash buckets
                        vector<string>(), 0, // No hash buckets
                        { "c0" }, // no range partitioning
                        2)); // 1 split;
  ASSERT_EQ(2, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32 PRIMARY KEY,
//   BUCKET BY (c0) INTO 3 BUCKETS
// );
TEST_F(FlexPartitioningITest, TestSinglePKBucketed) {
  NO_FATALS(CreateTable(1, // 1 column
                        { "c0" }, 3, // bucket by "c0" in 3 buckets
                        vector<string>(), 0, // no other buckets
                        { "c0" }, // default range
                        2)); // one split
  ASSERT_EQ(6, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   BUCKET BY (c1) INTO 3 BUCKETS
// );
TEST_F(FlexPartitioningITest, TestCompositePK_BucketOnSecondColumn) {
  NO_FATALS(CreateTable(2, // 2 columns
                        { "c1" }, 3, // bucket by "c0" in 3 buckets
                        vector<string>(), 0, // no other buckets
                        { "c0", "c1" }, // default range
                        1)); // no splits;
  ASSERT_EQ(3, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   RANGE PARTITION BY (c1, c0)
// );
TEST_F(FlexPartitioningITest, TestCompositePK_RangePartitionByReversedPK) {
  NO_FATALS(CreateTable(2, // 2 columns
                        vector<string>(), 0, // no buckets
                        vector<string>(), 0, // no buckets
                        { "c1", "c0" }, // range partition by reversed PK
                        2)); // one split
  ASSERT_EQ(2, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   RANGE PARTITION BY (c0)
// );
TEST_F(FlexPartitioningITest, TestCompositePK_RangePartitionByPKPrefix) {
  NO_FATALS(CreateTable(2, // 2 columns
                        vector<string>(), 0, // no buckets
                        vector<string>(), 0, // no buckets
                        { "c0" }, // range partition by c0
                        2)); // one split
  ASSERT_EQ(2, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   RANGE PARTITION BY (c1)
// );
TEST_F(FlexPartitioningITest, TestCompositePK_RangePartitionByPKSuffix) {
  NO_FATALS(CreateTable(2, // 2 columns
                        vector<string>(), 0, // no buckets
                        vector<string>(), 0, // no buckets
                        { "c1" }, // range partition by c1
                        2)); // one split
  ASSERT_EQ(2, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   RANGE PARTITION BY (c0),
//   BUCKET BY (c1) INTO 4 BUCKETS
// );
TEST_F(FlexPartitioningITest, TestCompositePK_RangeAndBucket) {
  NO_FATALS(CreateTable(2, // 2 columns
                        { "c1" }, 4, // BUCKET BY c1 INTO 4 BUCKETS
                        vector<string>(), 0, // no buckets
                        { "c0" }, // range partition by c0
                        2)); // 1 split;
  ASSERT_EQ(8, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   BUCKET BY (c1) INTO 4 BUCKETS,
//   BUCKET BY (c0) INTO 3 BUCKETS
// );
TEST_F(FlexPartitioningITest, TestCompositePK_MultipleBucketings) {
  NO_FATALS(CreateTable(2, // 2 columns
                        { "c1" }, 4, // BUCKET BY c1 INTO 4 BUCKETS
                        { "c0" }, 3, // BUCKET BY c0 INTO 3 BUCKETS
                        { "c0", "c1" }, // default range partitioning
                        2)); // 1 split;
  ASSERT_EQ(4 * 3 * 2, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   RANGE PARTITION BY (),
//   BUCKET BY (c0) INTO 4 BUCKETS,
// );
TEST_F(FlexPartitioningITest, TestCompositePK_SingleBucketNoRange) {
  NO_FATALS(CreateTable(2, // 2 columns
                        { "c0" }, 4, // BUCKET BY c0 INTO 4 BUCKETS
                        vector<string>(), 0, // no buckets
                        vector<string>(), // no range partitioning
                        1)); // 0 splits;
  ASSERT_EQ(4, CountTablets());

  InsertAndVerifyScans();
}

// CREATE TABLE t (
//   c0 INT32,
//   c1 INT32,
//   PRIMARY KEY (c0, c1)
//   RANGE PARTITION BY (),
//   BUCKET BY (c0) INTO 4 BUCKETS,
//   BUCKET BY (c1) INTO 5 BUCKETS,
// );
TEST_F(FlexPartitioningITest, TestCompositePK_MultipleBucketingsNoRange) {
  NO_FATALS(CreateTable(2, // 2 columns
                        { "c0" }, 4, // BUCKET BY c0 INTO 4 BUCKETS
                        { "c1" }, 5, // BUCKET BY c1 INTO 5 BUCKETS
                        vector<string>(), // no range partitioning
                        1)); // 0 splits;
  ASSERT_EQ(20, CountTablets());

  InsertAndVerifyScans();
}

} // namespace itest
} // namespace kudu
