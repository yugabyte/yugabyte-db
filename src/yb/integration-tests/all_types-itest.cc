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
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/optional/optional_fwd.hpp>
#include <gtest/gtest.h>

#include "yb/client/client_fwd.h"
#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/type_traits.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"

#include "yb/util/format.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_int32(num_rows_per_tablet, 100,
    "The number of rows to be inserted into each tablet");

using std::vector;

namespace yb {
namespace client {

using std::shared_ptr;
using std::string;

static const int kNumTabletServers = 3;
static const int kNumTablets = 3;

// Wrap the actual DataType so that we can have the setup structs be friends of other classes
// without leaking DataType.
template<DataType KeyType>
struct KeyTypeWrapper {
  static const DataType type = KeyType;
};

template<class Value>
struct TypeDescriptor;

template<>
struct TypeDescriptor<KeyTypeWrapper<yb::DataType::BINARY>> {
  static void AddHashValue(QLWriteRequestPB* req, int value) {
    QLAddBinaryHashValue(req, StringPrintf("%016x", value));
  }

  static int64_t GetIntVal(const QLValue& value) {
    return std::stoll(value.binary_value(), nullptr, 16);
  }
};

template<>
struct TypeDescriptor<KeyTypeWrapper<yb::DataType::STRING>> {
  static void AddHashValue(QLWriteRequestPB* req, int value) {
    QLAddStringHashValue(req, StringPrintf("%016x", value));
  }

  static int64_t GetIntVal(const QLValue& value) {
    return std::stoll(value.string_value(), nullptr, 16);
  }
};

template<>
struct TypeDescriptor<int64_t> {
  static void AddHashValue(QLWriteRequestPB* req, int64_t value) {
    QLAddInt64HashValue(req, value);
  }

  static int64_t GetIntVal(const QLValue& value) {
    return value.int64_value();
  }
};

template<>
struct TypeDescriptor<int32_t> {
  static void AddHashValue(QLWriteRequestPB* req, int32_t value) {
    QLAddInt32HashValue(req, value);
  }

  static int64_t GetIntVal(const QLValue& value) {
    return value.int32_value();
  }
};

template<>
struct TypeDescriptor<int16_t> {
  static void AddHashValue(QLWriteRequestPB* req, int16_t value) {
    QLAddInt16HashValue(req, value);
  }

  static int64_t GetIntVal(const QLValue& value) {
    return value.int16_value();
  }
};

template<>
struct TypeDescriptor<int8_t> {
  static void AddHashValue(QLWriteRequestPB* req, int8_t value) {
    QLAddInt8HashValue(req, value);
  }

  static int64_t GetIntVal(const QLValue& value) {
    return value.int8_value();
  }
};

template<typename KeyTypeWrapper>
struct SliceKeysTestSetup {

  SliceKeysTestSetup()
    : max_rows_(MathLimits<int>::kMax),
      rows_per_tablet_(std::min(max_rows_/ kNumTablets, FLAGS_num_rows_per_tablet)),
      increment_(static_cast<int>(MathLimits<int>::kMax / kNumTablets)) {
  }

  void AddKeyColumnsToSchema(YBSchemaBuilder* builder) const {
    builder->AddColumn("key")->Type(KeyTypeWrapper::type)->NotNull()->HashPrimaryKey();
  }

  void GenerateRowKey(YBqlWriteOp* insert, int split_idx, int row_idx) const {
    int row_key_num = (split_idx * increment_) + row_idx;
    TypeDescriptor<KeyTypeWrapper>::AddHashValue(insert->mutable_request(), row_key_num);
  }

  int64_t KeyIntVal(const qlexpr::QLRow& row) const {
    return TypeDescriptor<KeyTypeWrapper>::GetIntVal(row.column(0));
  }

  int GetRowsPerTablet() const {
    return rows_per_tablet_;
  }

  int GetMaxRows() const {
    return max_rows_;
  }

  int Increment() const {
    return increment_;
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

  void AddKeyColumnsToSchema(YBSchemaBuilder* builder) const {
    builder->AddColumn("key")->Type(KeyTypeWrapper::type)->NotNull()->HashPrimaryKey();
  }

  void GenerateRowKey(YBqlWriteOp* insert, int split_idx, int row_idx) const {
    CppType val = (split_idx * increment_) + row_idx;
    TypeDescriptor<CppType>::AddHashValue(insert->mutable_request(), val);
  }

  int64_t KeyIntVal(const qlexpr::QLRow& row) const {
    return TypeDescriptor<CppType>::GetIntVal(row.column(0));
  }

  int GetRowsPerTablet() const {
    return rows_per_tablet_;
  }

  int GetMaxRows() const {
    return max_rows_;
  }

  int Increment() const {
    return increment_;
  }

  std::vector<std::string> GetKeyColumns() const {
    return {"key"};
  }

  int max_rows_;
  int increment_;
  int rows_per_tablet_;
};

// Integration that writes, scans and verifies all types.
template <class TestSetup>
class AllTypesItest : public YBTest {
 public:
  AllTypesItest() {
    if (AllowSlowTests()) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_rows_per_tablet) = 10000;
    }
    setup_ = TestSetup();
  }

  // Builds a schema that includes all (frontend) supported types.
  // The key is templated so that we can try different key types.
  void CreateAllTypesSchema() {
    YBSchemaBuilder builder;
    setup_.AddKeyColumnsToSchema(&builder);
    builder.AddColumn("int8_val")->Type(DataType::INT8);
    builder.AddColumn("int16_val")->Type(DataType::INT16);
    builder.AddColumn("int32_val")->Type(DataType::INT32);
    builder.AddColumn("int64_val")->Type(DataType::INT64);
    builder.AddColumn("string_val")->Type(DataType::STRING);
    builder.AddColumn("bool_val")->Type(DataType::BOOL);
    builder.AddColumn("binary_val")->Type(DataType::BINARY);
    CHECK_OK(builder.Build(&schema_));
  }

  Status CreateCluster() {
    vector<string> ts_flags;

    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = kNumTabletServers;

    for (const std::string& flag : ts_flags) {
      opts.extra_tserver_flags.push_back(flag);
    }

    cluster_.reset(new ExternalMiniCluster(opts));
    RETURN_NOT_OK(cluster_->Start());
    client_ = VERIFY_RESULT(cluster_->CreateClient());
    return Status::OK();
  }

  Status CreateTable() {
    CreateAllTypesSchema();
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());

    const YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "all-types-table");
    RETURN_NOT_OK(client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                      table_name.namespace_type()));
    RETURN_NOT_OK(table_creator->table_name(table_name)
                  .schema(&schema_)
                  .num_tablets(kNumTablets)
                  .Create());
    return table_.Open(table_name, client_.get());
  }

  Status GenerateRow(YBSession* session, int split_idx, int row_idx) {
    auto insert = table_.NewInsertOp();
    setup_.GenerateRowKey(insert.get(), split_idx, row_idx);
    int int_val = (split_idx * setup_.GetRowsPerTablet()) + row_idx;
    auto req = insert->mutable_request();
    table_.AddInt8ColumnValue(req, "int8_val", int_val);
    table_.AddInt16ColumnValue(req, "int16_val", int_val);
    table_.AddInt32ColumnValue(req, "int32_val", int_val);
    table_.AddInt64ColumnValue(req, "int64_val", int_val);
    std::string content = StringPrintf("hello %010x", int_val);
    table_.AddStringColumnValue(req, "string_val", content);
    table_.AddBoolColumnValue(req, "bool_val", int_val % 2);
    table_.AddBinaryColumnValue(req, "binary_val", content);
    VLOG(1) << "Inserting row[" << split_idx << "," << row_idx << "]" << insert->ToString();
    session->Apply(insert);
    return Status::OK();
  }

  // This inserts kNumRowsPerTablet in each of the tablets. In the end we should have
  // perfectly partitioned table, if the encoding of the keys was correct and the rows
  // ended up in the right place.
  Status InsertRows() {
    shared_ptr<YBSession> session = client_->NewSession(MonoDelta::FromSeconds(60));
    int max_rows_per_tablet = setup_.GetRowsPerTablet();
    for (int i = 0; i < kNumTablets; ++i) {
      for (int j = 0; j < max_rows_per_tablet; ++j) {
        RETURN_NOT_OK(GenerateRow(session.get(), i, j));
        if (j % 1000 == 0) {
          RETURN_NOT_OK(session->TEST_Flush());
        }
      }
      RETURN_NOT_OK(session->TEST_Flush());
    }
    return Status::OK();
  }

  void SetupProjection(vector<string>* projection) {
    *projection = setup_.GetKeyColumns();
    projection->push_back("int8_val");
    projection->push_back("int16_val");
    projection->push_back("int32_val");
    projection->push_back("int64_val");
    projection->push_back("string_val");
    projection->push_back("binary_val");
    projection->push_back("bool_val");
  }

  void VerifyRow(const qlexpr::QLRow& row) {
    int64_t key_int_val = setup_.KeyIntVal(row);
    int64_t row_idx = key_int_val % setup_.Increment();;
    int64_t split_idx = key_int_val / setup_.Increment();
    int64_t expected_int_val = (split_idx * setup_.GetRowsPerTablet()) + row_idx;

    ASSERT_EQ(static_cast<int8_t>(expected_int_val), row.column(1).int8_value());
    ASSERT_EQ(static_cast<int16_t>(expected_int_val), row.column(2).int16_value());
    ASSERT_EQ(static_cast<int32_t>(expected_int_val), row.column(3).int32_value());
    ASSERT_EQ(expected_int_val, row.column(4).int64_value());
    string content = StringPrintf("hello %010" PRIx64, expected_int_val);
    ASSERT_EQ(content, row.column(5).string_value());
    ASSERT_EQ(content, row.column(6).binary_value());
    ASSERT_EQ(expected_int_val % 2, row.column(7).bool_value());
  }

  void VerifyRows() {
    std::vector<std::string> projection;
    SetupProjection(&projection);

    TableIteratorOptions options;
    options.columns = projection;
    auto count = 0;
    for (const auto& row : TableRange(table_, options)) {
      VerifyRow(row);
      ++count;
    }
    ASSERT_EQ(setup_.GetRowsPerTablet() * kNumTablets, count);
  }

  void RunTest() {
    ASSERT_OK(CreateCluster());
    ASSERT_OK(CreateTable());
    ASSERT_OK(InsertRows());
    // Check that all of the replicas agree on the inserted data. This retries until
    // all replicas are up-to-date, which is important to ensure that the following
    // Verify always passes.
    ASSERT_NO_FATALS(ClusterVerifier(cluster_.get()).CheckCluster());
    // Check that the inserted data matches what we thought we inserted.
    VerifyRows();
  }

  void TearDown() override {
    client_.reset();
    cluster_->AssertNoCrashes();
    cluster_->Shutdown();
  }

 protected:
  TestSetup setup_;
  YBSchema schema_;
  std::unique_ptr<YBClient> client_;
  std::unique_ptr<ExternalMiniCluster> cluster_;
  TableHandle table_;
};

typedef ::testing::Types<IntKeysTestSetup<KeyTypeWrapper<DataType::INT8>>,
                         IntKeysTestSetup<KeyTypeWrapper<DataType::INT16>>,
                         IntKeysTestSetup<KeyTypeWrapper<DataType::INT32>>,
                         IntKeysTestSetup<KeyTypeWrapper<DataType::INT64>>,
                         SliceKeysTestSetup<KeyTypeWrapper<DataType::STRING>>,
                         SliceKeysTestSetup<KeyTypeWrapper<DataType::BINARY>>
                         > KeyTypes;

TYPED_TEST_CASE(AllTypesItest, KeyTypes);

TYPED_TEST(AllTypesItest, TestAllKeyTypes) {
  this->RunTest();
}

}  // namespace client
}  // namespace yb
