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

#include <time.h>

#include <glog/logging.h>

#include "yb/common/iterator.h"
#include "yb/common/row.h"
#include "yb/common/scan_spec.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet-test-base.h"
#include "yb/util/slice.h"
#include "yb/util/test_macros.h"

// Include client header so we can access YBTableType.
#include "yb/client/client.h"

#include "yb/util/enums.h"

using std::shared_ptr;
using std::unordered_set;

namespace yb {
namespace tablet {

using fs::ReadableBlock;
using yb::util::to_underlying;

DEFINE_int32(testiterator_num_inserts, 1000,
             "Number of rows inserted in TestRowIterator/TestInsert");

static_assert(to_underlying(TableType::YQL_TABLE_TYPE) ==
                  to_underlying(client::YBTableType::YQL_TABLE_TYPE),
              "Numeric code for YQL_TABLE_TYPE table type must be consistent");
static_assert(to_underlying(TableType::REDIS_TABLE_TYPE) ==
                  to_underlying(client::YBTableType::REDIS_TABLE_TYPE),
              "Numeric code for REDIS_TABLE_TYPE table type must be consistent");

template<class SETUP>
class TestTablet : public TabletTestBase<SETUP> {
  typedef SETUP Type;

 public:
  // Verify that iteration doesn't fail
  void CheckCanIterate() {
    vector<string> out_rows;
    ASSERT_OK(this->IterateToStringList(&out_rows));
  }
};
TYPED_TEST_CASE(TestTablet, TabletTestHelperTypes);

// Test that historical data for a row is maintained even after the row
// is flushed.
TYPED_TEST(TestTablet, TestInsertsAndMutationsAreUndoneWithMVCCAfterFlush) {
  // Insert 5 rows, after the first one, each time we insert a new row we mutate
  // the previous one.

  // Take snapshots after each operation
  vector<MvccSnapshot> snaps;
  snaps.push_back(MvccSnapshot(*this->tablet()->mvcc_manager()));

  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  for (int i = 0; i < 5; i++) {
    this->InsertTestRows(i, 1, 0);
    DVLOG(1) << "Inserted row=" << i << ", row_idx=" << i << ", val=0";
    MvccSnapshot ins_snaphsot(*this->tablet()->mvcc_manager());
    snaps.push_back(ins_snaphsot);
    LOG(INFO) << "After Insert Snapshot: " <<  ins_snaphsot.ToString();
    if (i > 0) {
      ASSERT_OK(this->UpdateTestRow(&writer, i - 1, i));
      DVLOG(1) << "Mutated row=" << i - 1 << ", row_idx=" << i - 1 << ", val=" << i;
      MvccSnapshot mut_snaphsot(*this->tablet()->mvcc_manager());
      snaps.push_back(mut_snaphsot);
      DVLOG(1) << "After Mutate Snapshot: " <<  mut_snaphsot.ToString();
    }
  }

  // Collect the expected rows from the snapshots.
  vector<vector<string>*> expected_rows;
  CollectRowsForSnapshots(this->tablet().get(), this->client_schema_,
                          snaps, &expected_rows);

  // Now verify that we get the same thing.
  VerifySnapshotsHaveSameResult(this->tablet().get(), this->client_schema_,
                                snaps, expected_rows);

  // Take a snapshot and mutate the rows again.
  snaps.push_back(MvccSnapshot(*this->tablet()->mvcc_manager()));

  for (int i = 0; i < 4; i++) {
    ASSERT_OK(this->UpdateTestRow(&writer, i, i + 10));
    DVLOG(1) << "Mutated row=" << i << ", row_idx=" << i << ", val=" << i + 10;
    MvccSnapshot mut_snaphsot(*this->tablet()->mvcc_manager());
    snaps.push_back(mut_snaphsot);
    DVLOG(1) << "After Mutate Snapshot: " <<  mut_snaphsot.ToString();
  }

  // also throw a delete in there.
  ASSERT_OK(this->DeleteTestRow(&writer, 4));
  MvccSnapshot delete_snaphsot(*this->tablet()->mvcc_manager());
  snaps.push_back(delete_snaphsot);
  DVLOG(1) << "After Delete Snapshot: " <<  delete_snaphsot.ToString();

  // Collect the expected rows now that we have undos and redos
  STLDeleteElements(&expected_rows);
  CollectRowsForSnapshots(this->tablet().get(), this->client_schema_,
                          snaps, &expected_rows);

  // Now verify that with undos and redos we get the same thing.
  VerifySnapshotsHaveSameResult(this->tablet().get(), this->client_schema_,
                                snaps, expected_rows);

  STLDeleteElements(&expected_rows);
}

// Test that inserting a row which already exists causes an AlreadyPresent
// error
TYPED_TEST(TestTablet, TestInsertDuplicateKey) {
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);

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
  uint64_t max_rows = this->ClampRowCount(FLAGS_testiterator_num_inserts);

  // Put a row in (insert and flush).
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
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

  // Now iterate over the tablet and make sure the rows show up.
  gscoped_ptr<RowwiseIterator> iter;
  const Schema& schema = this->client_schema_;
  ASSERT_OK(this->tablet()->NewRowIterator(schema, boost::none, &iter));
  ScanSpec scan_spec;
  ASSERT_OK(iter->Init(&scan_spec));
  LOG(INFO) << "Created iter: " << iter->ToString();

  // Collect the expected rows.
  vector<string> rows;
  ASSERT_OK(yb::tablet::DumpTablet(*this->tablet(), this->client_schema_, &rows));
  ASSERT_EQ(max_rows, rows.size());
}

// Test that when a row has been updated many times, it always yields
// the most recent value.
TYPED_TEST(TestTablet, TestMultipleUpdates) {
  // Insert and update same row several times.
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
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
  ASSERT_OK(registry->WriteAsJson(&writer, { "*" }, MetricJsonOptions()));
  // Open tablet, should still work. Need a new writer though, as we should not overwrite an already
  // existing root.
  ASSERT_OK(this->harness()->Open());
  JsonWriter new_writer(&out, JsonWriter::PRETTY);
  ASSERT_OK(registry->WriteAsJson(&new_writer, { "*" }, MetricJsonOptions()));
}

} // namespace tablet
} // namespace yb
