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
#include <vector>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/client/row_result.h"
#include "kudu/common/wire_protocol-test-util.h"
#include "kudu/integration-tests/cluster_verifier.h"
#include "kudu/integration-tests/ts_itest-base.h"

DEFINE_int32(num_rows_per_tablet, 100, "The number of rows to be inserted into each tablet");

using std::vector;

namespace kudu {
namespace client {

using sp::shared_ptr;

static const int kNumTabletServers = 3;
static const int kNumTablets = 3;
static const int KMaxBatchSize = 8 * 1024 * 1024;

template<typename KeyTypeWrapper>
struct SliceKeysTestSetup {

  SliceKeysTestSetup()
   : max_rows_(MathLimits<int>::kMax),
     rows_per_tablet_(std::min(max_rows_/ kNumTablets, FLAGS_num_rows_per_tablet)),
     increment_(static_cast<int>(MathLimits<int>::kMax / kNumTablets)) {
  }

  void AddKeyColumnsToSchema(KuduSchemaBuilder* builder) const {
    builder->AddColumn("key")->Type(
        client::FromInternalDataType(KeyTypeWrapper::type))->NotNull()->PrimaryKey();
  }

  // Split points are calculated by equally partitioning the int64_t key space and then
  // using the stringified hexadecimal representation to create the split keys (with
  // zero padding).
  vector<const KuduPartialRow*> GenerateSplitRows(const KuduSchema& schema) const {
    vector<string> splits;
    splits.reserve(kNumTablets - 1);
    for (int i = 1; i < kNumTablets; i++) {
      int split = i * increment_;
      splits.push_back(StringPrintf("%08x", split));
    }
    vector<const KuduPartialRow*> rows;
    for (string val : splits) {
      Slice slice(val);
      KuduPartialRow* row = schema.NewRow();
      CHECK_OK(row->SetSliceCopy<TypeTraits<KeyTypeWrapper::type> >(0, slice));
      rows.push_back(row);
    }
    return rows;
  }

  Status GenerateRowKey(KuduInsert* insert, int split_idx, int row_idx) const {
    int row_key_num = (split_idx * increment_) + row_idx;
    string row_key = StringPrintf("%08x", row_key_num);
    Slice row_key_slice(row_key);
    return insert->mutable_row()->SetSliceCopy<TypeTraits<KeyTypeWrapper::type> >(0,
                                                                                  row_key_slice);
  }

  Status VerifyRowKey(const KuduRowResult& result, int split_idx, int row_idx) const {
    int expected_row_key_num = (split_idx * increment_) + row_idx;
    string expected_row_key = StringPrintf("%08x", expected_row_key_num);
    Slice expected_row_key_slice(expected_row_key);
    Slice row_key;
    RETURN_NOT_OK(result.Get<TypeTraits<KeyTypeWrapper::type> >(0, &row_key));
    if (expected_row_key_slice.compare(row_key) != 0) {
      return Status::Corruption(strings::Substitute("Keys didn't match. Expected: $0 Got: $1",
                                                    expected_row_key_slice.ToDebugString(),
                                                    row_key.ToDebugString()));
    }

    return Status::OK();
  }

  int GetRowsPerTablet() const {
    return rows_per_tablet_;
  }

  int GetMaxRows() const {
    return max_rows_;
  }

  vector<string> GetKeyColumns() const {
    vector<string> key_col;
    key_col.push_back("key");
    return key_col;
  }

  int max_rows_;
  int rows_per_tablet_;
  int increment_;
};

template<typename KeyTypeWrapper>
struct IntKeysTestSetup {
  typedef typename TypeTraits<KeyTypeWrapper::type>::cpp_type CppType;

  IntKeysTestSetup()
     // If CppType is actually bigger than int (e.g. int64_t) casting the max to int
     // returns -1, so we make sure in that case we get max from int directly.
   : max_rows_(static_cast<int>(MathLimits<CppType>::kMax) != -1 ?
       static_cast<int>(MathLimits<CppType>::kMax) : MathLimits<int>::kMax),
     increment_(max_rows_ / kNumTablets),
     rows_per_tablet_(std::min(increment_, FLAGS_num_rows_per_tablet)) {
    DCHECK(base::is_integral<CppType>::value);
  }

  void AddKeyColumnsToSchema(KuduSchemaBuilder* builder) const {
    builder->AddColumn("key")->Type(
        client::FromInternalDataType(KeyTypeWrapper::type))->NotNull()->PrimaryKey();
  }

  vector<const KuduPartialRow*> GenerateSplitRows(const KuduSchema& schema) const {
    vector<CppType> splits;
    splits.reserve(kNumTablets - 1);
    for (int64_t i = 1; i < kNumTablets; i++) {
      splits.push_back(i * increment_);
    }
    vector<const KuduPartialRow*> rows;
    for (CppType val : splits) {
      KuduPartialRow* row = schema.NewRow();
      CHECK_OK(row->Set<TypeTraits<KeyTypeWrapper::type> >(0, val));
      rows.push_back(row);
    }
    return rows;
  }

  Status GenerateRowKey(KuduInsert* insert, int split_idx, int row_idx) const {
    CppType val = (split_idx * increment_) + row_idx;
    return insert->mutable_row()->Set<TypeTraits<KeyTypeWrapper::type> >(0, val);
  }

  Status VerifyRowKey(const KuduRowResult& result, int split_idx, int row_idx) const {
    CppType val;
    RETURN_NOT_OK(result.Get<TypeTraits<KeyTypeWrapper::type> >(0, &val));
    int expected = (split_idx * increment_) + row_idx;
    if (val != expected) {
      return Status::Corruption(strings::Substitute("Keys didn't match. Expected: $0 Got: $1",
                                                    expected, val));
    }
    return Status::OK();
  }

  int GetRowsPerTablet() const {
    return rows_per_tablet_;
  }

  int GetMaxRows() const {
    return max_rows_;
  }

  vector<string> GetKeyColumns() const {
    vector<string> key_col;
    key_col.push_back("key");
    return key_col;
  }

  int max_rows_;
  int increment_;
  int rows_per_tablet_;
};

// Integration that writes, scans and verifies all types.
template <class TestSetup>
class AllTypesItest : public KuduTest {
 public:
  AllTypesItest() {
    if (AllowSlowTests()) {
      FLAGS_num_rows_per_tablet = 10000;
    }
    setup_ = TestSetup();
  }

  // Builds a schema that includes all (frontend) supported types.
  // The key is templated so that we can try different key types.
  void CreateAllTypesSchema() {
    KuduSchemaBuilder builder;
    setup_.AddKeyColumnsToSchema(&builder);
    builder.AddColumn("int8_val")->Type(KuduColumnSchema::INT8);
    builder.AddColumn("int16_val")->Type(KuduColumnSchema::INT16);
    builder.AddColumn("int32_val")->Type(KuduColumnSchema::INT32);
    builder.AddColumn("int64_val")->Type(KuduColumnSchema::INT64);
    builder.AddColumn("timestamp_val")->Type(KuduColumnSchema::TIMESTAMP);
    builder.AddColumn("string_val")->Type(KuduColumnSchema::STRING);
    builder.AddColumn("bool_val")->Type(KuduColumnSchema::BOOL);
    builder.AddColumn("float_val")->Type(KuduColumnSchema::FLOAT);
    builder.AddColumn("double_val")->Type(KuduColumnSchema::DOUBLE);
    builder.AddColumn("binary_val")->Type(KuduColumnSchema::BINARY);
    CHECK_OK(builder.Build(&schema_));
  }

  Status CreateCluster() {
    vector<string> ts_flags;
    // Set the flush threshold low so that we have flushes and test the on-disk formats.
    ts_flags.push_back("--flush_threshold_mb=1");
    // Set the major delta compaction ratio low enough that we trigger a lot of them.
    ts_flags.push_back("--tablet_delta_store_major_compact_min_ratio=0.001");

    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = kNumTabletServers;

    for (const std::string& flag : ts_flags) {
      opts.extra_tserver_flags.push_back(flag);
    }

    cluster_.reset(new ExternalMiniCluster(opts));
    RETURN_NOT_OK(cluster_->Start());
    KuduClientBuilder builder;
    return cluster_->CreateClient(builder, &client_);
  }

  Status CreateTable() {
    CreateAllTypesSchema();
    vector<const KuduPartialRow*> split_rows = setup_.GenerateSplitRows(schema_);
    gscoped_ptr<client::KuduTableCreator> table_creator(client_->NewTableCreator());

    for (const KuduPartialRow* row : split_rows) {
      split_rows_.push_back(*row);
    }

    RETURN_NOT_OK(table_creator->table_name("all-types-table")
                  .schema(&schema_)
                  .split_rows(split_rows)
                  .num_replicas(kNumTabletServers)
                  .Create());
    return client_->OpenTable("all-types-table", &table_);
  }

  Status GenerateRow(KuduSession* session, int split_idx, int row_idx) {
    KuduInsert* insert = table_->NewInsert();
    RETURN_NOT_OK(setup_.GenerateRowKey(insert, split_idx, row_idx));
    int int_val = (split_idx * setup_.GetRowsPerTablet()) + row_idx;
    KuduPartialRow* row = insert->mutable_row();
    RETURN_NOT_OK(row->SetInt8("int8_val", int_val));
    RETURN_NOT_OK(row->SetInt16("int16_val", int_val));
    RETURN_NOT_OK(row->SetInt32("int32_val", int_val));
    RETURN_NOT_OK(row->SetInt64("int64_val", int_val));
    RETURN_NOT_OK(row->SetTimestamp("timestamp_val", int_val));
    string content = strings::Substitute("hello $0", int_val);
    Slice slice_val(content);
    RETURN_NOT_OK(row->SetStringCopy("string_val", slice_val));
    RETURN_NOT_OK(row->SetBinaryCopy("binary_val", slice_val));
    double double_val = int_val;
    RETURN_NOT_OK(row->SetDouble("double_val", double_val));
    RETURN_NOT_OK(row->SetFloat("float_val", double_val));
    RETURN_NOT_OK(row->SetBool("bool_val", int_val % 2));
    VLOG(1) << "Inserting row[" << split_idx << "," << row_idx << "]" << insert->ToString();
    RETURN_NOT_OK(session->Apply(insert));
    return Status::OK();
  }

  // This inserts kNumRowsPerTablet in each of the tablets. In the end we should have
  // perfectly partitioned table, if the encoding of the keys was correct and the rows
  // ended up in the right place.
  Status InsertRows() {
    shared_ptr<KuduSession> session = client_->NewSession();
    RETURN_NOT_OK(session->SetFlushMode(KuduSession::MANUAL_FLUSH));
    int max_rows_per_tablet = setup_.GetRowsPerTablet();
    for (int i = 0; i < kNumTablets; ++i) {
      for (int j = 0; j < max_rows_per_tablet; ++j) {
        RETURN_NOT_OK(GenerateRow(session.get(), i, j));
        if (j % 1000 == 0) {
          RETURN_NOT_OK(session->Flush());
        }
      }
      RETURN_NOT_OK(session->Flush());
    }
    return Status::OK();
  }

  void SetupProjection(vector<string>* projection) {
    vector<string> keys = setup_.GetKeyColumns();
    for (const string& key : keys) {
      projection->push_back(key);
    }
    projection->push_back("int8_val");
    projection->push_back("int16_val");
    projection->push_back("int32_val");
    projection->push_back("int64_val");
    projection->push_back("timestamp_val");
    projection->push_back("string_val");
    projection->push_back("binary_val");
    projection->push_back("double_val");
    projection->push_back("float_val");
    projection->push_back("bool_val");
  }

  void VerifyRow(const KuduRowResult& row, int split_idx, int row_idx) {
    ASSERT_OK(setup_.VerifyRowKey(row, split_idx, row_idx));

    int64_t expected_int_val = (split_idx * setup_.GetRowsPerTablet()) + row_idx;
    int8_t int8_val;
    ASSERT_OK(row.GetInt8("int8_val", &int8_val));
    ASSERT_EQ(int8_val, static_cast<int8_t>(expected_int_val));
    int16_t int16_val;
    ASSERT_OK(row.GetInt16("int16_val", &int16_val));
    ASSERT_EQ(int16_val, static_cast<int16_t>(expected_int_val));
    int32_t int32_val;
    ASSERT_OK(row.GetInt32("int32_val", &int32_val));
    ASSERT_EQ(int32_val, static_cast<int32_t>(expected_int_val));
    int64_t int64_val;
    ASSERT_OK(row.GetInt64("int64_val", &int64_val));
    ASSERT_EQ(int64_val, expected_int_val);
    int64_t timestamp_val;
    ASSERT_OK(row.GetTimestamp("timestamp_val", &timestamp_val));
    ASSERT_EQ(timestamp_val, expected_int_val);

    string content = strings::Substitute("hello $0", expected_int_val);
    Slice expected_slice_val(content);
    Slice string_val;
    ASSERT_OK(row.GetString("string_val", &string_val));
    ASSERT_EQ(string_val, expected_slice_val);
    Slice binary_val;
    ASSERT_OK(row.GetBinary("binary_val", &binary_val));
    ASSERT_EQ(binary_val, expected_slice_val);

    bool expected_bool_val = expected_int_val % 2;
    bool bool_val;
    ASSERT_OK(row.GetBool("bool_val", &bool_val));
    ASSERT_EQ(bool_val, expected_bool_val);

    double expected_double_val = expected_int_val;
    double double_val;
    ASSERT_OK(row.GetDouble("double_val", &double_val));
    ASSERT_EQ(double_val, expected_double_val);
    float float_val;
    ASSERT_OK(row.GetFloat("float_val", &float_val));
    ASSERT_EQ(float_val, static_cast<float>(double_val));
  }

  Status VerifyRows() {
    vector<string> projection;
    SetupProjection(&projection);

    int total_rows = 0;
    // Scan a single tablet and make sure it has the rows we expect in the amount we
    // expect.
    for (int i = 0; i < kNumTablets; ++i) {
      KuduScanner scanner(table_.get());
      string low_split;
      string high_split;
      if (i != 0) {
        const KuduPartialRow& split = split_rows_[i - 1];
        RETURN_NOT_OK(scanner.AddLowerBound(split));
        low_split = split.ToString();
      }
      if (i != kNumTablets - 1) {
        const KuduPartialRow& split = split_rows_[i];
        RETURN_NOT_OK(scanner.AddExclusiveUpperBound(split));
        high_split = split.ToString();
      }

      RETURN_NOT_OK(scanner.SetProjectedColumns(projection));
      RETURN_NOT_OK(scanner.SetBatchSizeBytes(KMaxBatchSize));
      RETURN_NOT_OK(scanner.SetFaultTolerant());
      RETURN_NOT_OK(scanner.SetReadMode(KuduScanner::READ_AT_SNAPSHOT));
      RETURN_NOT_OK(scanner.SetTimeoutMillis(5000));
      RETURN_NOT_OK(scanner.Open());
      LOG(INFO) << "Scanning tablet: [" << low_split << ", " << high_split << ")";

      int total_rows_in_tablet = 0;
      while (scanner.HasMoreRows()) {
        vector<KuduRowResult> rows;
        RETURN_NOT_OK(scanner.NextBatch(&rows));

        for (int j = 0; j < rows.size(); ++j) {
          VLOG(1) << "Scanned row: " << rows[j].ToString();
          VerifyRow(rows[j], i, total_rows_in_tablet + j);
        }
        total_rows_in_tablet += rows.size();
      }
      CHECK_EQ(total_rows_in_tablet, setup_.GetRowsPerTablet());
      total_rows += total_rows_in_tablet;
    }
    CHECK_EQ(total_rows, setup_.GetRowsPerTablet() * kNumTablets);
    return Status::OK();
  }

  void RunTest() {
    ASSERT_OK(CreateCluster());
    ASSERT_OK(CreateTable());
    ASSERT_OK(InsertRows());
    // Check that all of the replicas agree on the inserted data. This retries until
    // all replicas are up-to-date, which is important to ensure that the following
    // Verify always passes.
    NO_FATALS(ClusterVerifier(cluster_.get()).CheckCluster());
    // Check that the inserted data matches what we thought we inserted.
    ASSERT_OK(VerifyRows());
  }

  virtual void TearDown() OVERRIDE {
    cluster_->AssertNoCrashes();
    cluster_->Shutdown();
  }

 protected:
  TestSetup setup_;
  KuduSchema schema_;
  vector<KuduPartialRow> split_rows_;
  shared_ptr<KuduClient> client_;
  gscoped_ptr<ExternalMiniCluster> cluster_;
  shared_ptr<KuduTable> table_;
};

// Wrap the actual DataType so that we can have the setup structs be friends of other classes
// without leaking DataType.
template<DataType KeyType>
struct KeyTypeWrapper {
  static const DataType type = KeyType;
};

typedef ::testing::Types<IntKeysTestSetup<KeyTypeWrapper<INT8> >,
                         IntKeysTestSetup<KeyTypeWrapper<INT16> >,
                         IntKeysTestSetup<KeyTypeWrapper<INT32> >,
                         IntKeysTestSetup<KeyTypeWrapper<INT64> >,
                         IntKeysTestSetup<KeyTypeWrapper<TIMESTAMP> >,
                         SliceKeysTestSetup<KeyTypeWrapper<STRING> >,
                         SliceKeysTestSetup<KeyTypeWrapper<BINARY> >
                         > KeyTypes;

TYPED_TEST_CASE(AllTypesItest, KeyTypes);

TYPED_TEST(AllTypesItest, TestAllKeyTypes) {
  this->RunTest();
}

} // namespace client
} // namespace kudu

