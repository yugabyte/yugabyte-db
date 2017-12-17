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
#ifndef YB_TABLET_TABLET_TEST_UTIL_H
#define YB_TABLET_TABLET_TEST_UTIL_H

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <gflags/gflags.h>

#include "yb/common/iterator.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/strings/join.h"
#include "yb/tablet/row_op.h"
#include "yb/tablet/tablet-harness.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/operations/alter_schema_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

DECLARE_bool(enable_data_block_fsync);

namespace yb {
namespace tablet {

using consensus::RaftConfigPB;
using std::string;
using std::vector;

class YBTabletTest : public YBTest {
 public:
  explicit YBTabletTest(const Schema& schema, TableType table_type = TableType::YQL_TABLE_TYPE)
    : schema_(schema.CopyWithColumnIds()),
      client_schema_(schema),
      table_type_(table_type) {
    // Keep unit tests fast, but only if no one has set the flag explicitly.
    if (google::GetCommandLineFlagInfoOrDie("enable_data_block_fsync").is_default) {
      FLAGS_enable_data_block_fsync = false;
    }
  }

  virtual void SetUp() override {
    YBTest::SetUp();

    SetUpTestTablet();
  }

  void CreateTestTablet(const string& root_dir = "") {
    string dir = root_dir.empty() ? GetTestPath("fs_root") : root_dir;
    TabletHarness::Options opts(dir);
    opts.enable_metrics = true;
    opts.table_type = table_type_;
    bool first_time = harness_ == NULL;
    harness_.reset(new TabletHarness(schema_, opts));
    CHECK_OK(harness_->Create(first_time));
  }

  void SetUpTestTablet(const string& root_dir = "") {
    CreateTestTablet(root_dir);
    CHECK_OK(harness_->Open());
  }

  void TabletReOpen(const string& root_dir = "") {
    SetUpTestTablet(root_dir);
  }

  const Schema &schema() const {
    return schema_;
  }

  const Schema &client_schema() const {
    return client_schema_;
  }

  server::Clock* clock() {
    return harness_->clock();
  }

  FsManager* fs_manager() {
    return harness_->fs_manager();
  }

  void AlterSchema(const Schema& schema) {
    tserver::AlterSchemaRequestPB req;
    req.set_schema_version(tablet()->metadata()->schema_version() + 1);

    AlterSchemaOperationState operation_state(nullptr, &req);
    ASSERT_OK(tablet()->CreatePreparedAlterSchema(&operation_state, &schema));
    ASSERT_OK(tablet()->AlterSchema(&operation_state));
    operation_state.Finish();
  }

  const std::shared_ptr<TabletClass>& tablet() const {
    return harness_->tablet();
  }

  TabletHarness* harness() {
    return harness_.get();
  }

 protected:
  const Schema schema_;
  const Schema client_schema_;
  TableType table_type_;

  gscoped_ptr<TabletHarness> harness_;
};

static inline CHECKED_STATUS IterateToStringList(RowwiseIterator *iter,
                                         vector<string> *out,
                                         int limit = INT_MAX) {
  out->clear();
  Schema schema = iter->schema();
  Arena arena(1024, 1024);
  RowBlock block(schema, 100, &arena);
  int fetched = 0;
  std::vector<std::pair<std::string, std::string>> temp;
  while (iter->HasNext() && fetched < limit) {
    RETURN_NOT_OK(iter->NextBlock(&block));
    for (size_t i = 0; i < block.nrows() && fetched < limit; i++) {
      auto row = block.row(i);
      std::string key(static_cast<const char*>(row.cell(0).ptr()), row.cell(0).size());
      temp.emplace_back(key, schema.DebugRow(row));
      fetched++;
    }
  }
  auto type_info = schema.column(0).type_info();
  std::sort(temp.begin(), temp.end(), [type_info](const auto& lhs, const auto& rhs) {
    return type_info->Compare(lhs.first.c_str(), rhs.first.c_str()) < 0;
  });
  for (auto& p : temp) {
    out->push_back(std::move(p.second));
  }
  return Status::OK();
}

// Performs snapshot reads, under each of the snapshots in 'snaps', and stores
// the results in 'collected_rows'.
static inline void CollectRowsForSnapshots(Tablet* tablet,
                                           const Schema& schema,
                                           const vector<MvccSnapshot>& snaps,
                                           vector<vector<string>* >* collected_rows) {
  for (const MvccSnapshot& snapshot : snaps) {
    DVLOG(1) << "Snapshot: " <<  snapshot.ToString();
    gscoped_ptr<RowwiseIterator> iter;
    ASSERT_OK(tablet->NewRowIterator(schema,
                                     snapshot,
                                     Tablet::UNORDERED,
                                     boost::none,
                                     &iter));
    ScanSpec scan_spec;
    ASSERT_OK(iter->Init(&scan_spec));
    auto collector = new vector<string>();
    ASSERT_OK(IterateToStringList(iter.get(), collector));
    for (const auto& mrs : *collector) {
      DVLOG(1) << "Got from MRS: " << mrs;
    }
    collected_rows->push_back(collector);
  }
}

// Performs snapshot reads, under each of the snapshots in 'snaps', and verifies that
// the results match the ones in 'expected_rows'.
static inline void VerifySnapshotsHaveSameResult(Tablet* tablet,
                                                 const Schema& schema,
                                                 const vector<MvccSnapshot>& snaps,
                                                 const vector<vector<string>* >& expected_rows) {
  int idx = 0;
  // Now iterate again and make sure we get the same thing.
  for (const MvccSnapshot& snapshot : snaps) {
    DVLOG(1) << "Snapshot: " <<  snapshot.ToString();
    gscoped_ptr<RowwiseIterator> iter;
    ASSERT_OK(tablet->NewRowIterator(schema,
                                     snapshot,
                                     Tablet::UNORDERED,
                                     boost::none,
                                     &iter));
    ScanSpec scan_spec;
    ASSERT_OK(iter->Init(&scan_spec));
    vector<string> collector;
    ASSERT_OK(IterateToStringList(iter.get(), &collector));
    ASSERT_EQ(collector.size(), expected_rows[idx]->size());

    for (int i = 0; i < expected_rows[idx]->size(); i++) {
      DVLOG(1) << "Got from DRS: " << collector[i];
      DVLOG(1) << "Expected: " << (*expected_rows[idx])[i];
      ASSERT_EQ((*expected_rows[idx])[i], collector[i]);
    }
    idx++;
  }
}

// Take an un-initialized iterator, Init() it, and iterate through all of its rows.
// The resulting string contains a line per entry.
static inline string InitAndDumpIterator(gscoped_ptr<RowwiseIterator> iter) {
  ScanSpec scan_spec;
  CHECK_OK(iter->Init(&scan_spec));

  vector<string> out;
  CHECK_OK(IterateToStringList(iter.get(), &out));
  return JoinStrings(out, "\n");
}

// Dump all of the rows of the tablet into the given vector.
static inline CHECKED_STATUS DumpTablet(const Tablet& tablet,
                                        const Schema& projection,
                                        vector<string>* out) {
  gscoped_ptr<RowwiseIterator> iter;
  RETURN_NOT_OK(tablet.NewRowIterator(projection, boost::none, &iter));
  ScanSpec scan_spec;
  RETURN_NOT_OK(iter->Init(&scan_spec));
  std::vector<string> rows;
  RETURN_NOT_OK(IterateToStringList(iter.get(), &rows));
  std::sort(rows.begin(), rows.end());
  out->swap(rows);
  return Status::OK();
}

} // namespace tablet
} // namespace yb
#endif // YB_TABLET_TABLET_TEST_UTIL_H
