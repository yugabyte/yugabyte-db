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

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/tablet/deltafile.h"
#include "kudu/tablet/delta_compaction.h"
#include "kudu/tablet/delta_iterator_merger.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/gutil/algorithm.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"
#include "kudu/util/env.h"
#include "kudu/util/env_util.h"
#include "kudu/util/path_util.h"
#include "kudu/util/status.h"
#include "kudu/util/auto_release_pool.h"

DEFINE_int32(num_rows, 2100, "the first row to update");
DEFINE_int32(num_delta_files, 3, "number of delta files");

using std::is_sorted;
using std::shared_ptr;
using std::string;
using std::vector;

namespace kudu {
namespace tablet {

using fs::ReadableBlock;
using fs::WritableBlock;

class TestDeltaCompaction : public KuduTest {
 public:
  TestDeltaCompaction()
      : deltafile_idx_(0),
        schema_(CreateSchema()) {
  }

  static Schema CreateSchema() {
    SchemaBuilder builder;
    CHECK_OK(builder.AddColumn("val", UINT32));
    return builder.Build();
  }

  Status GetDeltaFileWriter(gscoped_ptr<DeltaFileWriter>* dfw,
                            BlockId* block_id) const {
    gscoped_ptr<WritableBlock> block;
    RETURN_NOT_OK(fs_manager_->CreateNewBlock(&block));
    *block_id = block->id();
    dfw->reset(new DeltaFileWriter(block.Pass()));
    RETURN_NOT_OK((*dfw)->Start());
    return Status::OK();
  }

  Status GetDeltaFileReader(const BlockId& block_id,
                            shared_ptr<DeltaFileReader>* dfr) const {
    gscoped_ptr<ReadableBlock> block;
    RETURN_NOT_OK(fs_manager_->OpenBlock(block_id, &block));
    shared_ptr<DeltaFileReader> delta_reader;
    return DeltaFileReader::Open(block.Pass(), block_id, dfr, REDO);
  }

  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();
    SeedRandom();
    fs_manager_.reset(new FsManager(env_.get(), GetTestPath("fs_root")));
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_->Open());
  }

 protected:
  int64_t deltafile_idx_;
  Schema schema_;
  gscoped_ptr<FsManager> fs_manager_;
};

TEST_F(TestDeltaCompaction, TestMergeMultipleSchemas) {
  vector<Schema> schemas;
  SchemaBuilder builder(schema_);
  schemas.push_back(builder.Build());

  // Add an int column with default
  uint32_t default_c2 = 10;
  ASSERT_OK(builder.AddColumn("c2", UINT32, false, &default_c2, &default_c2));
  schemas.push_back(builder.Build());

  // add a string column with default
  Slice default_c3("Hello World");
  ASSERT_OK(builder.AddColumn("c3", STRING, false, &default_c3, &default_c3));
  schemas.push_back(builder.Build());

  vector<shared_ptr<DeltaStore> > inputs;

  faststring buf;
  int row_id = 0;
  int curr_timestamp = 0;
  int deltafile_idx = 0;
  for (const Schema& schema : schemas) {
    // Write the Deltas
    BlockId block_id;
    gscoped_ptr<DeltaFileWriter> dfw;
    ASSERT_OK(GetDeltaFileWriter(&dfw, &block_id));

    // Generate N updates with the new schema, some of them are on existing
    // rows others are on new rows (see kNumUpdates and kNumMultipleUpdates).
    // Each column will be updated with value composed by delta file id
    // and update number (see update_value assignment).
    size_t kNumUpdates = 10;
    size_t kNumMultipleUpdates = kNumUpdates / 2;
    DeltaStats stats;
    for (size_t i = 0; i < kNumUpdates; ++i) {
      buf.clear();
      RowChangeListEncoder update(&buf);
      for (size_t col_idx = schema.num_key_columns(); col_idx < schema.num_columns(); ++col_idx) {
        ColumnId col_id = schema.column_id(col_idx);
        DCHECK_GE(col_id, 0);

        stats.IncrUpdateCount(col_id, 1);
        const ColumnSchema& col_schema = schema.column(col_idx);
        int update_value = deltafile_idx * 100 + i;
        switch (col_schema.type_info()->physical_type()) {
          case UINT32:
            {
              uint32_t u32_val = update_value;
              update.AddColumnUpdate(col_schema, col_id, &u32_val);
            }
            break;
          case BINARY:
            {
              string s = boost::lexical_cast<string>(update_value);
              Slice str_val(s);
              update.AddColumnUpdate(col_schema, col_id, &str_val);
            }
            break;
          default:
            FAIL() << "Type " << DataType_Name(col_schema.type_info()->type()) << " Not Supported";
            break;
        }
      }

      // To simulate multiple updates on the same row, the first N updates
      // of this new schema will always be on rows [0, 1, 2, ...] while the
      // others will be on new rows. (N is tunable by changing kNumMultipleUpdates)
      DeltaKey key((i < kNumMultipleUpdates) ? i : row_id, Timestamp(curr_timestamp));
      RowChangeList row_changes = update.as_changelist();
      ASSERT_OK(dfw->AppendDelta<REDO>(key, row_changes));
      ASSERT_OK(stats.UpdateStats(key.timestamp(), row_changes));
      curr_timestamp++;
      row_id++;
    }

    ASSERT_OK(dfw->WriteDeltaStats(stats));
    ASSERT_OK(dfw->Finish());
    shared_ptr<DeltaFileReader> dfr;
    ASSERT_OK(GetDeltaFileReader(block_id, &dfr));
    inputs.push_back(dfr);
    deltafile_idx++;
  }

  // Merge
  MvccSnapshot snap(MvccSnapshot::CreateSnapshotIncludingAllTransactions());
  const Schema& merge_schema = schemas.back();
  shared_ptr<DeltaIterator> merge_iter;
  ASSERT_OK(DeltaIteratorMerger::Create(inputs, &merge_schema,
                                        snap, &merge_iter));
  gscoped_ptr<DeltaFileWriter> dfw;
  BlockId block_id;
  ASSERT_OK(GetDeltaFileWriter(&dfw, &block_id));
  ASSERT_OK(WriteDeltaIteratorToFile<REDO>(merge_iter.get(),
                                           ITERATE_OVER_ALL_ROWS,
                                           dfw.get()));
  ASSERT_OK(dfw->Finish());

  shared_ptr<DeltaFileReader> dfr;
  ASSERT_OK(GetDeltaFileReader(block_id, &dfr));
  DeltaIterator* raw_iter;
  ASSERT_OK(dfr->NewDeltaIterator(&merge_schema, snap, &raw_iter));
  gscoped_ptr<DeltaIterator> scoped_iter(raw_iter);

  vector<string> results;
  ASSERT_OK(DebugDumpDeltaIterator(REDO, scoped_iter.get(), merge_schema,
                                          ITERATE_OVER_ALL_ROWS, &results));
  for (const string &str : results) {
    VLOG(1) << str;
  }
  ASSERT_TRUE(is_sorted(results.begin(), results.end()));
}

} // namespace tablet
} // namespace kudu
