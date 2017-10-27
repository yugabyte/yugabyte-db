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

#include <algorithm>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "yb/common/schema.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet-test-base.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace tablet {

class TabletPushdownTest : public YBTabletTest {
 public:
  TabletPushdownTest()
    : YBTabletTest(Schema({ ColumnSchema("key", INT32),
                              ColumnSchema("int_val", INT32),
                              ColumnSchema("string_val", STRING) }, 1)) {
  }

  void SetUp() override {
    YBTabletTest::SetUp();

    FillTestTablet();
  }

  void FillTestTablet() {
    RowBuilder rb(client_schema_);

    nrows_ = 2100;
    if (AllowSlowTests()) {
      nrows_ = 100000;
    }

    LocalTabletWriter writer(tablet().get(), &client_schema_);
    YBPartialRow row(&client_schema_);
    for (int64_t i = 0; i < nrows_; i++) {
      CHECK_OK(row.SetInt32(0, i));
      CHECK_OK(row.SetInt32(1, i * 10));
      CHECK_OK(row.SetStringCopy(2, StringPrintf("%08" PRId64, i)));
      ASSERT_OK_FAST(writer.Insert(row));
    }
  }

  // The predicates tested in the various test cases all yield
  // the same set of rows. Run the scan and verify that the
  // expected rows are returned.
  void TestScanYieldsExpectedResults(ScanSpec spec) {
    gscoped_ptr<RowwiseIterator> iter;
    ASSERT_OK(tablet()->NewRowIterator(client_schema_, boost::none, &iter));
    ASSERT_OK(iter->Init(&spec));
    ASSERT_TRUE(spec.predicates().empty()) << "Should have accepted all predicates";

    vector<string> results;
    LOG_TIMING(INFO, "Filtering by int value") {
      ASSERT_OK(IterateToStringList(iter.get(), &results));
    }
    std::sort(results.begin(), results.end());
    for (const string &str : results) {
      LOG(INFO) << str;
    }
    ASSERT_EQ(11, results.size());
    ASSERT_EQ("(int32 key=200, int32 int_val=2000, string string_val=00000200)", results[0]);
    ASSERT_EQ("(int32 key=210, int32 int_val=2100, string string_val=00000210)", results[10]);
  }

  // Test that a scan with an empty projection and the given spec
  // returns the expected number of rows. The rows themselves
  // should be empty.
  void TestCountOnlyScanYieldsExpectedResults(ScanSpec spec) {
    Schema empty_schema(std::vector<ColumnSchema>(), 0);
    gscoped_ptr<RowwiseIterator> iter;
    ASSERT_OK(tablet()->NewRowIterator(empty_schema, boost::none, &iter));
    ASSERT_OK(iter->Init(&spec));
    ASSERT_TRUE(spec.predicates().empty()) << "Should have accepted all predicates";

    vector<string> results;
    ASSERT_OK(IterateToStringList(iter.get(), &results));
    ASSERT_EQ(11, results.size());
    for (const string& result : results) {
      ASSERT_EQ("()", result);
    }
  }
 private:
  uint64_t nrows_;
};

TEST_F(TabletPushdownTest, TestPushdownIntKeyRange) {
  ScanSpec spec;
  int32_t lower = 200;
  int32_t upper = 210;
  ColumnRangePredicate pred0(schema_.column(0), &lower, &upper);
  spec.AddPredicate(pred0);

  TestScanYieldsExpectedResults(spec);
  TestCountOnlyScanYieldsExpectedResults(spec);
}

// TODO: Value range scan is not working yet, it returns 2100 rows.
TEST_F(TabletPushdownTest, DISABLED_TestPushdownIntValueRange) {
  // Push down a double-ended range on the integer value column.

  ScanSpec spec;
  int32_t lower = 2000;
  int32_t upper = 2100;
  ColumnRangePredicate pred1(schema_.column(1), &lower, &upper);
  spec.AddPredicate(pred1);

  TestScanYieldsExpectedResults(spec);
}

} // namespace tablet
} // namespace yb
