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

#include <algorithm>
#include <string>
#include <vector>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/benchmarks/tpch/rpc_line_item_dao.h"
#include "yb/benchmarks/tpch/tpch-schemas.h"
#include "yb/common/partial_row.h"
#include "yb/gutil/stringprintf.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

namespace yb {

using client::YBRowResult;
using client::YBSchema;
using client::YBTableName;
using std::string;
using std::vector;
using namespace std::placeholders;

class RpcLineItemDAOTest : public YBMiniClusterTestBase<MiniCluster> {

 public:
  RpcLineItemDAOTest() {}

  virtual void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    // Start minicluster
    cluster_.reset(new MiniCluster(env_.get(), MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());

    YBTableName kTableName("tpch1");

    // Create the table and Connect to it.
    string master_address(cluster_->mini_master()->bound_rpc_addr_str());
    dao_.reset(new yb::RpcLineItemDAO(master_address, kTableName, 5));
    dao_->Init();
  }

  virtual void DoTearDown() override {
    cluster_->Shutdown();
    YBMiniClusterTestBase::DoTearDown();
  }

 protected:
  gscoped_ptr<RpcLineItemDAO> dao_;

  // Builds a test row to be inserted into the lineitem table.
  // The row's ship_date is set such that it matches the TPCH Q1 predicate.
  static void BuildTestRow(int order, int line, YBPartialRow* row) {
    CHECK_OK(row->SetInt64(tpch::kOrderKeyColIdx, order));
    CHECK_OK(row->SetInt32(tpch::kLineNumberColIdx, line));
    CHECK_OK(row->SetInt32(tpch::kPartKeyColIdx, 12345));
    CHECK_OK(row->SetInt32(tpch::kSuppKeyColIdx, 12345));
    CHECK_OK(row->SetInt32(tpch::kQuantityColIdx, 12345));
    CHECK_OK(row->SetDouble(tpch::kExtendedPriceColIdx, 123.45));
    CHECK_OK(row->SetDouble(tpch::kDiscountColIdx, 123.45));
    CHECK_OK(row->SetDouble(tpch::kTaxColIdx, 123.45));
    CHECK_OK(row->SetStringCopy(tpch::kReturnFlagColIdx, StringPrintf("hello %d", line)));
    CHECK_OK(row->SetStringCopy(tpch::kLineStatusColIdx, StringPrintf("hello %d", line)));
    CHECK_OK(row->SetStringCopy(tpch::kShipDateColIdx, Slice("1985-07-15")));
    CHECK_OK(row->SetStringCopy(tpch::kCommitDateColIdx, Slice("1985-11-13")));
    CHECK_OK(row->SetStringCopy(tpch::kReceiptDateColIdx, Slice("1985-11-13")));
    CHECK_OK(row->SetStringCopy(tpch::kShipInstructColIdx, StringPrintf("hello %d", line)));
    CHECK_OK(row->SetStringCopy(tpch::kShipModeColIdx, StringPrintf("hello %d", line)));
    CHECK_OK(row->SetStringCopy(tpch::kCommentColIdx, StringPrintf("hello %d", line)));
  }

  static void UpdateTestRow(int key, int line_number, int quantity, YBPartialRow* row) {
    CHECK_OK(row->SetInt64(tpch::kOrderKeyColIdx, key));
    CHECK_OK(row->SetInt32(tpch::kLineNumberColIdx, line_number));
    CHECK_OK(row->SetInt32(tpch::kQuantityColIdx, quantity));
  }

  int CountRows() {
    gscoped_ptr<RpcLineItemDAO::Scanner> scanner;
    dao_->OpenScanner(vector<string>(), &scanner);
    vector<YBRowResult> rows;
    int count = 0;
    while (scanner->HasMore()) {
      scanner->GetNext(&rows);
      count += rows.size();
    }
    return count;
  }

  void ScanTpch1RangeToStrings(int64_t min_orderkey, int64_t max_orderkey,
                          vector<string>* str_rows) {
    str_rows->clear();
    gscoped_ptr<RpcLineItemDAO::Scanner> scanner;
    dao_->OpenTpch1ScannerForOrderKeyRange(min_orderkey, max_orderkey,
                                          &scanner);
    vector<YBRowResult> rows;
    while (scanner->HasMore()) {
      scanner->GetNext(&rows);
      for (const YBRowResult& row : rows) {
        str_rows->push_back(row.ToString());
      }
    }
    std::sort(str_rows->begin(), str_rows->end());
  }
}; // class RpcLineItemDAOTest

TEST_F(RpcLineItemDAOTest, TestInsert) {
  dao_->WriteLine(std::bind(BuildTestRow, 1, 1, _1));
  dao_->FinishWriting();
  ASSERT_EQ(1, CountRows());
  for (int i = 2; i < 10; i++) {
    for (int y = 0; y < 5; y++) {
      dao_->WriteLine(std::bind(BuildTestRow, i, y, _1));
    }
  }
  dao_->FinishWriting();
  ASSERT_EQ(41, CountRows());

  vector<string> rows;
  ScanTpch1RangeToStrings(7, 7, &rows);
  ASSERT_EQ(5, rows.size());
  ScanTpch1RangeToStrings(5, 7, &rows);
  ASSERT_EQ(15, rows.size());
}

TEST_F(RpcLineItemDAOTest, TestUpdate) {
  dao_->WriteLine(std::bind(BuildTestRow, 1, 1, _1));
  dao_->FinishWriting();
  ASSERT_EQ(1, CountRows());

  dao_->MutateLine(std::bind(UpdateTestRow, 1, 1, 12345, _1));
  dao_->FinishWriting();
  gscoped_ptr<RpcLineItemDAO::Scanner> scanner;
  dao_->OpenScanner({ tpch::kQuantityColName }, &scanner);
  vector<YBRowResult> rows;
  while (scanner->HasMore()) {
    scanner->GetNext(&rows);
    for (const YBRowResult& row : rows) {
      int32_t l_quantity;
      ASSERT_OK(row.GetInt32(0, &l_quantity));
      ASSERT_EQ(12345, l_quantity);
    }
  }
}

} // namespace yb
