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






// Include client header so we can access YBTableType.

#include <time.h>

#include "yb/util/logging.h"

#include "yb/client/table.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"

#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-base.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_bootstrap_if.h"

#include "yb/util/enums.h"
#include "yb/util/slice.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/flags.h"

using std::string;
using std::vector;

namespace yb {
namespace tablet {

DEFINE_NON_RUNTIME_int32(testiterator_num_inserts, 1000,
             "Number of rows inserted in TestRowIterator/TestInsert");

static_assert(static_cast<int>(to_underlying(TableType::YQL_TABLE_TYPE)) ==
                  to_underlying(client::YBTableType::YQL_TABLE_TYPE),
              "Numeric code for YQL_TABLE_TYPE table type must be consistent");
static_assert(static_cast<int>(to_underlying(TableType::REDIS_TABLE_TYPE)) ==
                  to_underlying(client::YBTableType::REDIS_TABLE_TYPE),
              "Numeric code for REDIS_TABLE_TYPE table type must be consistent");

template<class SETUP>
class TestTablet : public TabletTestBase<SETUP> {
  typedef SETUP Type;
};
TYPED_TEST_CASE(TestTablet, TabletTestHelperTypes);

// Test that inserting a row which already exists causes an AlreadyPresent
// error
TYPED_TEST(TestTablet, TestInsertDuplicateKey) {
  LocalTabletWriter writer(this->tablet());

  CHECK_OK(this->InsertTestRow(&writer, 12345, 0));

  // Insert again, should not fail!
  Status s = this->InsertTestRow(&writer, 12345, 0);
  ASSERT_OK(s);

  s = this->InsertTestRow(&writer, 12345, 0);
  ASSERT_OK(s);
}


template<class SETUP>
bool TestSetupExpectsNulls(int32_t key_idx) {
  return false;
}

template<>
bool TestSetupExpectsNulls<NullableValueTestSetup>(int32_t key_idx) {
  // If it's a row that the test updates, then we should expect null
  // based on whether it updated to NULL or away from NULL.
  bool should_update = (key_idx % 2 == 1);
  if (should_update) {
    return (key_idx % 10 == 1);
  }

  // Otherwise, expect whatever was inserted.
  return NullableValueTestSetup::ShouldInsertAsNull(key_idx);
}

// Test iterating over a tablet after updates to many of the existing rows.
TYPED_TEST(TestTablet, TestRowIteratorComplex) {
  int32_t max_rows = this->ClampRowCount(FLAGS_testiterator_num_inserts);

  // Put a row in (insert and flush).
  LocalTabletWriter writer(this->tablet());
  for (int32_t i = 0; i < max_rows; i++) {
    ASSERT_OK_FAST(this->InsertTestRow(&writer, i, 0));
  }
  LOG(INFO) << "Successfully inserted " << max_rows << " rows";

  // Update a subset of the rows
  for (int32_t i = 0; i < max_rows; i++) {
    bool should_update = (i % 2 == 1);
    if (!should_update) continue;

    bool set_to_null = TestSetupExpectsNulls<TypeParam>(i);
    if (set_to_null) {
      ASSERT_OK_FAST(this->UpdateTestRowToNull(&writer, i));
    } else {
      ASSERT_OK_FAST(this->UpdateTestRow(&writer, i, i));
    }
  }

  // Collect the expected rows.
  vector<string> rows;
  ASSERT_OK(yb::tablet::DumpTablet(*this->tablet(), &rows));
  ASSERT_EQ(max_rows, rows.size());
}

// Test that when a row has been updated many times, it always yields
// the most recent value.
TYPED_TEST(TestTablet, TestMultipleUpdates) {
  // Insert and update same row several times.
  LocalTabletWriter writer(this->tablet());
  ASSERT_OK(this->InsertTestRow(&writer, 0, 0));
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 1));
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 2));
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 3));

  // Should see most recent value.
  vector<string> out_rows;
  ASSERT_OK(this->IterateToStringList(&out_rows));
  ASSERT_EQ(1, out_rows.size());

  // Update the row a few times.
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 4));
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 5));
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 6));

  // Should still see most recent value.
  ASSERT_OK(this->IterateToStringList(&out_rows));
  ASSERT_EQ(1, out_rows.size());

  CHECK_OK(this->InsertTestRow(&writer, 1, 0));

  // Should still see most recent value.
  ASSERT_OK(this->IterateToStringList(&out_rows));
  ASSERT_EQ(2, out_rows.size());
  ASSERT_EQ(this->setup_.FormatDebugRow(0, 6, false), out_rows[0]);
  ASSERT_EQ(this->setup_.FormatDebugRow(1, 0, false), out_rows[1]);
}

// Test that metrics behave properly during tablet initialization
TYPED_TEST(TestTablet, TestMetricsInit) {
  // Create a tablet, but do not open it
  this->CreateTestTablet();
  MetricRegistry* registry = this->harness()->metrics_registry();
  std::stringstream out;
  JsonWriter writer(&out, JsonWriter::PRETTY);
  ASSERT_OK(registry->WriteAsJson(&writer, MetricJsonOptions()));
  // Open tablet, should still work. Need a new writer though, as we should not overwrite an already
  // existing root.
  ASSERT_OK(this->harness()->Open());
  JsonWriter new_writer(&out, JsonWriter::PRETTY);
  ASSERT_OK(registry->WriteAsJson(&new_writer, MetricJsonOptions()));
}

TYPED_TEST(TestTablet, TestFlushedOpId) {
  auto tablet = this->tablet();
  LocalTabletWriter writer(tablet);
  const int64_t kCount = 1000;

  // Insert & flush one row to start index counting.
  ASSERT_OK(this->InsertTestRow(&writer, 0, 333));
  ASSERT_OK(tablet->Flush(FlushMode::kSync));
  OpId id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  const int64_t start_index = id.index;

  this->InsertTestRows(1, kCount, 555);
  id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  ASSERT_EQ(id.index, start_index);

  ASSERT_OK(tablet->Flush(FlushMode::kSync));
  id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  ASSERT_EQ(id.index, start_index + kCount);

  this->InsertTestRows(1, kCount, 777);
  id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  ASSERT_EQ(id.index, start_index + kCount);

  ASSERT_OK(tablet->Flush(FlushMode::kSync));
  id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  ASSERT_EQ(id.index, start_index + 2*kCount);
}

TYPED_TEST(TestTablet, TestDocKeyMetrics) {
  auto metrics = this->harness()->tablet()->metrics();

  ASSERT_EQ(metrics->Get(TabletCounters::kDocDBKeysFound), 0);
  ASSERT_EQ(metrics->Get(TabletCounters::kDocDBObsoleteKeysFound), 0);

  LocalTabletWriter writer(this->tablet());
  ASSERT_OK(this->InsertTestRow(&writer, 0, 0));
  ASSERT_OK(this->InsertTestRow(&writer, 1, 0));
  ASSERT_OK(this->InsertTestRow(&writer, 2, 0));
  ASSERT_OK(this->InsertTestRow(&writer, 3, 0));
  ASSERT_OK(this->InsertTestRow(&writer, 4, 0));

  this->VerifyTestRows(0, 5);

  ASSERT_EQ(metrics->Get(TabletCounters::kDocDBKeysFound), 5);
  ASSERT_EQ(metrics->Get(TabletCounters::kDocDBObsoleteKeysFound), 0);
  auto prev_total_keys = metrics->Get(TabletCounters::kDocDBKeysFound);

  ASSERT_OK(this->DeleteTestRow(&writer, 0));
  std::vector<std::string> str;
  ASSERT_OK(this->IterateToStringList(&str));

  ASSERT_EQ(metrics->Get(TabletCounters::kDocDBKeysFound) - prev_total_keys, 5);
  ASSERT_EQ(metrics->Get(TabletCounters::kDocDBObsoleteKeysFound), 1);
}

} // namespace tablet
} // namespace yb
