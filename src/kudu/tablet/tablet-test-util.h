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
#ifndef KUDU_TABLET_TABLET_TEST_UTIL_H
#define KUDU_TABLET_TABLET_TEST_UTIL_H

#include <algorithm>
#include <gflags/gflags.h>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/iterator.h"
#include "kudu/gutil/casts.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/tablet/row_op.h"
#include "kudu/tablet/tablet-harness.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/transactions/alter_schema_transaction.h"
#include "kudu/tablet/transactions/write_transaction.h"
#include "kudu/util/metrics.h"
#include "kudu/util/test_util.h"

DECLARE_bool(enable_data_block_fsync);

namespace kudu {
namespace tablet {

using consensus::RaftConfigPB;
using std::string;
using std::vector;

class KuduTabletTest : public KuduTest {
 public:
  explicit KuduTabletTest(const Schema& schema)
    : schema_(schema.CopyWithColumnIds()),
      client_schema_(schema) {
    // Keep unit tests fast, but only if no one has set the flag explicitly.
    if (google::GetCommandLineFlagInfoOrDie("enable_data_block_fsync").is_default) {
      FLAGS_enable_data_block_fsync = false;
    }
  }

  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();

    SetUpTestTablet();
  }

  void CreateTestTablet(const string& root_dir = "") {
    string dir = root_dir.empty() ? GetTestPath("fs_root") : root_dir;
    TabletHarness::Options opts(dir);
    opts.enable_metrics = true;
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

    AlterSchemaTransactionState tx_state(NULL, &req, NULL);
    ASSERT_OK(tablet()->CreatePreparedAlterSchema(&tx_state, &schema));
    ASSERT_OK(tablet()->AlterSchema(&tx_state));
    tx_state.Finish();
  }

  const std::shared_ptr<Tablet>& tablet() const {
    return harness_->tablet();
  }

  TabletHarness* harness() {
    return harness_.get();
  }

 protected:
  const Schema schema_;
  const Schema client_schema_;

  gscoped_ptr<TabletHarness> harness_;
};

class KuduRowSetTest : public KuduTabletTest {
 public:
  explicit KuduRowSetTest(const Schema& schema)
    : KuduTabletTest(schema) {
  }

  virtual void SetUp() OVERRIDE {
    KuduTabletTest::SetUp();
    ASSERT_OK(tablet()->metadata()->CreateRowSet(&rowset_meta_,
                                                       SchemaBuilder(schema_).Build()));
  }

  Status FlushMetadata() {
    return tablet()->metadata()->Flush();
  }

 protected:
  std::shared_ptr<RowSetMetadata> rowset_meta_;
};

static inline Status IterateToStringList(RowwiseIterator *iter,
                                         vector<string> *out,
                                         int limit = INT_MAX) {
  out->clear();
  Schema schema = iter->schema();
  Arena arena(1024, 1024);
  RowBlock block(schema, 100, &arena);
  int fetched = 0;
  while (iter->HasNext() && fetched < limit) {
    RETURN_NOT_OK(iter->NextBlock(&block));
    for (size_t i = 0; i < block.nrows() && fetched < limit; i++) {
      if (block.selection_vector()->IsRowSelected(i)) {
        out->push_back(schema.DebugRow(block.row(i)));
        fetched++;
      }
    }
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
                                            &iter));
    ASSERT_OK(iter->Init(NULL));
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
                                            &iter));
    ASSERT_OK(iter->Init(NULL));
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

// Construct a new iterator from the given rowset, and dump
// all of its results into 'out'. The previous contents
// of 'out' are cleared.
static inline Status DumpRowSet(const RowSet &rs,
                                const Schema &projection,
                                const MvccSnapshot &snap,
                                vector<string> *out,
                                int limit = INT_MAX) {
  gscoped_ptr<RowwiseIterator> iter;
  RETURN_NOT_OK(rs.NewRowIterator(&projection, snap, &iter));
  RETURN_NOT_OK(iter->Init(NULL));
  RETURN_NOT_OK(IterateToStringList(iter.get(), out, limit));
  return Status::OK();
}

// Take an un-initialized iterator, Init() it, and iterate through all of its rows.
// The resulting string contains a line per entry.
static inline string InitAndDumpIterator(gscoped_ptr<RowwiseIterator> iter) {
  CHECK_OK(iter->Init(NULL));

  vector<string> out;
  CHECK_OK(IterateToStringList(iter.get(), &out));
  return JoinStrings(out, "\n");
}

// Dump all of the rows of the tablet into the given vector.
static inline Status DumpTablet(const Tablet& tablet,
                         const Schema& projection,
                         vector<string>* out) {
  gscoped_ptr<RowwiseIterator> iter;
  RETURN_NOT_OK(tablet.NewRowIterator(projection, &iter));
  RETURN_NOT_OK(iter->Init(NULL));
  std::vector<string> rows;
  RETURN_NOT_OK(IterateToStringList(iter.get(), &rows));
  std::sort(rows.begin(), rows.end());
  out->swap(rows);
  return Status::OK();
}

// Write a single row to the given RowSetWriter (which may be of the rolling
// or non-rolling variety).
template<class RowSetWriterClass>
static Status WriteRow(const Slice &row_slice, RowSetWriterClass *writer) {
  const Schema &schema = writer->schema();
  DCHECK_EQ(row_slice.size(), schema.byte_size());

  RowBlock block(schema, 1, NULL);
  ConstContiguousRow row(&schema, row_slice.data());
  RowBlockRow dst_row = block.row(0);
  RETURN_NOT_OK(CopyRow(row, &dst_row, reinterpret_cast<Arena*>(NULL)));

  return writer->AppendBlock(block);
}

} // namespace tablet
} // namespace kudu
#endif
