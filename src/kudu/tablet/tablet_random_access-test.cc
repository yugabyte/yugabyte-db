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

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <string>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/gutil/casts.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet-test-base.h"
#include "kudu/util/stopwatch.h"

DEFINE_int32(keyspace_size, 3000, "number of unique row keys to insert/mutate");
DEFINE_int32(runtime_seconds, 1, "number of seconds to run the test");
DEFINE_int32(sleep_between_background_ops_ms, 100,
             "number of milliseconds to sleep between flushing or compacting");
DEFINE_int32(update_delete_ratio, 4, "ratio of update:delete when mutating existing rows");

DECLARE_int32(deltafile_default_block_size);

using std::string;
using std::vector;

enum TestOp {
  TEST_INSERT,
  TEST_UPDATE,
  TEST_DELETE,
  TEST_FLUSH_OPS,
  TEST_FLUSH_TABLET,
  TEST_FLUSH_DELTAS,
  TEST_MINOR_COMPACT_DELTAS,
  TEST_MAJOR_COMPACT_DELTAS,
  TEST_COMPACT_TABLET,
  TEST_NUM_OP_TYPES // max value for enum
};
MAKE_ENUM_LIMITS(TestOp, TEST_INSERT, TEST_NUM_OP_TYPES);

namespace kudu {
namespace tablet {

const char* TestOp_names[] = {
  "TEST_INSERT",
  "TEST_UPDATE",
  "TEST_DELETE",
  "TEST_FLUSH_OPS",
  "TEST_FLUSH_TABLET",
  "TEST_FLUSH_DELTAS",
  "TEST_MINOR_COMPACT_DELTAS",
  "TEST_MAJOR_COMPACT_DELTAS",
  "TEST_COMPACT_TABLET"
};

// Test which does only random operations against a tablet, including update and random
// get (ie scans with equal lower and upper bounds).
//
// The test maintains an in-memory copy of the expected state of the tablet, and uses only
// a single thread, so that it's easy to verify that the tablet always matches the expected
// state.
class TestRandomAccess : public KuduTabletTest {
 public:
  TestRandomAccess()
    : KuduTabletTest(Schema({ ColumnSchema("key", INT32),
                              ColumnSchema("val", INT32, true) }, 1)),
      done_(1) {
    OverrideFlagForSlowTests("keyspace_size", "30000");
    OverrideFlagForSlowTests("runtime_seconds", "10");
    OverrideFlagForSlowTests("sleep_between_background_ops_ms", "1000");

    // Set a small block size to increase chances that a single update will span
    // multiple delta blocks.
    FLAGS_deltafile_default_block_size = 1024;
    expected_tablet_state_.resize(FLAGS_keyspace_size);
  }

  virtual void SetUp() OVERRIDE {
    KuduTabletTest::SetUp();
    writer_.reset(new LocalTabletWriter(tablet().get(), &client_schema_));
  }

  // Pick a random row of the table, verify its current state, and then
  // modify it in some way. The modifications may include multiple mutations
  // to the same row in a single batch (eg insert/update/delete).
  //
  // The mutations are always valid. For example:
  // - inserting if it doesn't exist yet
  // - perform an update or delete the row if it does exist.
  //
  // TODO: should add a version of this test which also tries invalid operations
  // and validates the correct errors.
  void DoRandomBatch() {
    int key = rand() % expected_tablet_state_.size();
    string& cur_val = expected_tablet_state_[key];

    // Check that a read yields what we expect.
    string val_in_table = GetRow(key);
    ASSERT_EQ("(" + cur_val + ")", val_in_table);

    vector<LocalTabletWriter::Op> pending;
    for (int i = 0; i < 3; i++) {
      int new_val = rand();
      if (cur_val.empty()) {
        // If there is no row, then insert one.
        cur_val = InsertRow(key, new_val, &pending);
      } else {
        if (new_val % (FLAGS_update_delete_ratio + 1) == 0) {
          cur_val = DeleteRow(key, &pending);
        } else {
          cur_val = MutateRow(key, new_val, &pending);
        }
      }
    }
    CHECK_OK(writer_->WriteBatch(pending));
    for (LocalTabletWriter::Op op : pending) {
      delete op.row;
    }
  }

  void DoRandomBatches() {
    int op_count = 0;
    Stopwatch s;
    s.start();
    while (s.elapsed().wall_seconds() < FLAGS_runtime_seconds) {
      for (int i = 0; i < 100; i++) {
        ASSERT_NO_FATAL_FAILURE(DoRandomBatch());
        op_count++;
      }
    }
    LOG(INFO) << "Ran " << op_count << " ops "
              << "(" << (op_count / s.elapsed().wall_seconds()) << " ops/sec)";
  }

  // Wakes up periodically to perform a flush or compaction.
  void BackgroundOpThread() {
    int n_flushes = 0;
    while (!done_.WaitFor(MonoDelta::FromMilliseconds(FLAGS_sleep_between_background_ops_ms))) {
      CHECK_OK(tablet()->Flush());
      ++n_flushes;
      switch (n_flushes % 3) {
        case 0:
          CHECK_OK(tablet()->Compact(Tablet::FORCE_COMPACT_ALL));
          break;
        case 1:
          CHECK_OK(tablet()->CompactWorstDeltas(RowSet::MAJOR_DELTA_COMPACTION));
          break;
        case 2:
          CHECK_OK(tablet()->CompactWorstDeltas(RowSet::MINOR_DELTA_COMPACTION));
          break;
      }
    }
  }

  // Adds an insert for the given key/value pair to 'ops', returning the new stringified
  // value of the row.
  string InsertRow(int key, int val, vector<LocalTabletWriter::Op>* ops) {
    gscoped_ptr<KuduPartialRow> row(new KuduPartialRow(&client_schema_));
    CHECK_OK(row->SetInt32(0, key));
    if (val & 1) {
      CHECK_OK(row->SetNull(1));
    } else {
      CHECK_OK(row->SetInt32(1, val));
    }
    string ret = row->ToString();
    ops->push_back(LocalTabletWriter::Op(RowOperationsPB::INSERT, row.release()));
    return ret;
  }

  // Adds an update of the given key/value pair to 'ops', returning the new stringified
  // value of the row.
  string MutateRow(int key, uint32_t new_val, vector<LocalTabletWriter::Op>* ops) {
    gscoped_ptr<KuduPartialRow> row(new KuduPartialRow(&client_schema_));
    CHECK_OK(row->SetInt32(0, key));
    if (new_val & 1) {
      CHECK_OK(row->SetNull(1));
    } else {
      CHECK_OK(row->SetInt32(1, new_val));
    }
    string ret = row->ToString();
    ops->push_back(LocalTabletWriter::Op(RowOperationsPB::UPDATE, row.release()));
    return ret;
  }

  // Adds a delete of the given row to 'ops', returning an empty string (indicating that
  // the row no longer exists).
  string DeleteRow(int key, vector<LocalTabletWriter::Op>* ops) {
    gscoped_ptr<KuduPartialRow> row(new KuduPartialRow(&client_schema_));
    CHECK_OK(row->SetInt32(0, key));
    ops->push_back(LocalTabletWriter::Op(RowOperationsPB::DELETE, row.release()));
    return "";
  }

  // Random-read the given row, returning its current value.
  // If the row doesn't exist, returns "()".
  string GetRow(int key) {
    ScanSpec spec;
    const Schema& schema = this->client_schema_;
    gscoped_ptr<RowwiseIterator> iter;
    CHECK_OK(this->tablet()->NewRowIterator(schema, &iter));
    ColumnRangePredicate pred_one(schema.column(0), &key, &key);
    spec.AddPredicate(pred_one);
    CHECK_OK(iter->Init(&spec));

    string ret = "()";
    int n_results = 0;

    Arena arena(1024, 4*1024*1024);
    RowBlock block(schema, 100, &arena);
    while (iter->HasNext()) {
      arena.Reset();
      CHECK_OK(iter->NextBlock(&block));
      for (int i = 0; i < block.nrows(); i++) {
        if (!block.selection_vector()->IsRowSelected(i)) {
          continue;
        }
        // We expect to only get exactly one result per read.
        CHECK_EQ(n_results, 0)
          << "Already got result when looking up row "
          << key << ": " << ret
          << " and now have new matching row: "
          << schema.DebugRow(block.row(i))
          << "  iterator: " << iter->ToString();
        ret = schema.DebugRow(block.row(i));
        n_results++;
      }
    }
    return ret;
  }

 protected:
  void RunFuzzCase(const vector<TestOp>& ops,
                   int update_multiplier);

  // The current expected state of the tablet.
  vector<string> expected_tablet_state_;

  // Latch triggered when the main thread is finished performing
  // operations. This stops the compact/flush thread.
  CountDownLatch done_;

  gscoped_ptr<LocalTabletWriter> writer_;
};

TEST_F(TestRandomAccess, Test) {
  scoped_refptr<Thread> flush_thread;
  CHECK_OK(Thread::Create("test", "flush",
                          boost::bind(&TestRandomAccess::BackgroundOpThread, this),
                          &flush_thread));

  DoRandomBatches();
  done_.CountDown();
  flush_thread->Join();
}


void GenerateTestCase(vector<TestOp>* ops, int len) {
  bool exists = false;
  bool ops_pending = false;
  bool data_in_mrs = false;
  bool worth_compacting = false;
  bool data_in_dms = false;
  ops->clear();
  while (ops->size() < len) {
    TestOp r = tight_enum_cast<TestOp>(rand() % enum_limits<TestOp>::max_enumerator);
    switch (r) {
      case TEST_INSERT:
        if (exists) continue;
        ops->push_back(TEST_INSERT);
        exists = true;
        ops_pending = true;
        data_in_mrs = true;
        break;
      case TEST_UPDATE:
        if (!exists) continue;
        ops->push_back(TEST_UPDATE);
        ops_pending = true;
        if (!data_in_mrs) {
          data_in_dms = true;
        }
        break;
      case TEST_DELETE:
        if (!exists) continue;
        ops->push_back(TEST_DELETE);
        ops_pending = true;
        exists = false;
        if (!data_in_mrs) {
          data_in_dms = true;
        }
        break;
      case TEST_FLUSH_OPS:
        if (ops_pending) {
          ops->push_back(TEST_FLUSH_OPS);
          ops_pending = false;
        }
        break;
      case TEST_FLUSH_TABLET:
        if (data_in_mrs) {
          if (ops_pending) {
            ops->push_back(TEST_FLUSH_OPS);
            ops_pending = false;
          }
          ops->push_back(TEST_FLUSH_TABLET);
          data_in_mrs = false;
          worth_compacting = true;
        }
        break;
      case TEST_COMPACT_TABLET:
        if (worth_compacting) {
          if (ops_pending) {
            ops->push_back(TEST_FLUSH_OPS);
            ops_pending = false;
          }
          ops->push_back(TEST_COMPACT_TABLET);
          worth_compacting = false;
        }
        break;
      case TEST_FLUSH_DELTAS:
        if (data_in_dms) {
          if (ops_pending) {
            ops->push_back(TEST_FLUSH_OPS);
            ops_pending = false;
          }
          ops->push_back(TEST_FLUSH_DELTAS);
          data_in_dms = false;
        }
        break;
      case TEST_MAJOR_COMPACT_DELTAS:
        ops->push_back(TEST_MAJOR_COMPACT_DELTAS);
        break;
      case TEST_MINOR_COMPACT_DELTAS:
        ops->push_back(TEST_MINOR_COMPACT_DELTAS);
        break;
      default:
        LOG(FATAL);
    }
  }
}

string DumpTestCase(const vector<TestOp>& ops) {
  vector<string> names;
  for (TestOp test_op : ops) {
    names.push_back(TestOp_names[test_op]);
  }
  return JoinStrings(names, ",\n");
}

void TestRandomAccess::RunFuzzCase(const vector<TestOp>& test_ops,
                                   int update_multiplier = 1) {
  LOG(INFO) << "test case: " << DumpTestCase(test_ops);

  LocalTabletWriter writer(tablet().get(), &client_schema_);
  vector<LocalTabletWriter::Op> ops;

  string cur_val = "";
  string pending_val = "";

  int i = 0;
  for (TestOp test_op : test_ops) {
    string val_in_table = GetRow(1);
    ASSERT_EQ("(" + cur_val + ")", val_in_table);

    i++;
    LOG(INFO) << TestOp_names[test_op];
    switch (test_op) {
      case TEST_INSERT:
        pending_val = InsertRow(1, i, &ops);
        break;
      case TEST_UPDATE:
        for (int j = 0; j < update_multiplier; j++) {
          pending_val = MutateRow(1, i, &ops);
        }
        break;
      case TEST_DELETE:
        pending_val = DeleteRow(1, &ops);
        break;
      case TEST_FLUSH_OPS:
        ASSERT_OK(writer.WriteBatch(ops));
        for (LocalTabletWriter::Op op : ops) {
          delete op.row;
        }
        ops.clear();
        cur_val = pending_val;
        break;
      case TEST_FLUSH_TABLET:
        ASSERT_OK(tablet()->Flush());
        break;
      case TEST_FLUSH_DELTAS:
        ASSERT_OK(tablet()->FlushBiggestDMS());
        break;
      case TEST_MAJOR_COMPACT_DELTAS:
        ASSERT_OK(tablet()->CompactWorstDeltas(RowSet::MAJOR_DELTA_COMPACTION));
        break;
      case TEST_MINOR_COMPACT_DELTAS:
        ASSERT_OK(tablet()->CompactWorstDeltas(RowSet::MINOR_DELTA_COMPACTION));
        break;
      case TEST_COMPACT_TABLET:
        ASSERT_OK(tablet()->Compact(Tablet::FORCE_COMPACT_ALL));
        break;
      default:
        LOG(FATAL) << test_op;
    }
  }
  for (LocalTabletWriter::Op op : ops) {
    delete op.row;
  }
}

// Generates a random test sequence and runs it.
// The logs of this test are designed to easily be copy-pasted and create
// more specific test cases like TestFuzz<N> below.
TEST_F(TestRandomAccess, TestFuzz) {
  SeedRandom();
  vector<TestOp> test_ops;
  GenerateTestCase(&test_ops, 500);
  RunFuzzCase(test_ops);
}

// Generates a random test case, but the UPDATEs are all repeated 1000 times.
// This results in very large batches which are likely to span multiple delta blocks
// when flushed.
TEST_F(TestRandomAccess, TestFuzzHugeBatches) {
  SeedRandom();
  vector<TestOp> test_ops;
  GenerateTestCase(&test_ops, AllowSlowTests() ? 1000 : 50);
  RunFuzzCase(test_ops, 1000);
}

// A particular test case which previously failed TestFuzz.
TEST_F(TestRandomAccess, TestFuzz1) {
  TestOp test_ops[] = {
    // Get an inserted row in a DRS.
    TEST_INSERT,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,

    // DELETE in DMS, INSERT in MRS and flush again.
    TEST_DELETE,
    TEST_INSERT,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,

    // State:
    // RowSet RowSet(0):
    //   (int32 key=1, int32 val=NULL) Undos: [@1(DELETE)] Redos (in DMS): [@2 DELETE]
    // RowSet RowSet(1):
    //   (int32 key=1, int32 val=NULL) Undos: [@2(DELETE)] Redos: []

    TEST_COMPACT_TABLET,
  };
  RunFuzzCase(vector<TestOp>(test_ops, test_ops + arraysize(test_ops)));
}

// A particular test case which previously failed TestFuzz.
TEST_F(TestRandomAccess, TestFuzz2) {
  TestOp test_ops[] = {
    TEST_INSERT,
    TEST_DELETE,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,
    // (int32 key=1, int32 val=NULL)
    // Undo Mutations: [@1(DELETE)]
    // Redo Mutations: [@1(DELETE)]

    TEST_INSERT,
    TEST_DELETE,
    TEST_INSERT,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,
    // (int32 key=1, int32 val=NULL)
    // Undo Mutations: [@2(DELETE)]
    // Redo Mutations: []

    TEST_COMPACT_TABLET,
    // Output Row: (int32 key=1, int32 val=NULL)
    // Undo Mutations: [@1(DELETE)]
    // Redo Mutations: [@1(DELETE)]

    TEST_DELETE,
    TEST_FLUSH_OPS,
    TEST_COMPACT_TABLET
  };
  RunFuzzCase(vector<TestOp>(test_ops, test_ops + arraysize(test_ops)));
}

// A particular test case which previously failed TestFuzz.
TEST_F(TestRandomAccess, TestFuzz3) {
  TestOp test_ops[] = {
    TEST_INSERT,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,
    // Output Row: (int32 key=1, int32 val=NULL)
    // Undo Mutations: [@1(DELETE)]
    // Redo Mutations: []

    TEST_DELETE,
    // Adds a @2 DELETE to DMS for above row.

    TEST_INSERT,
    TEST_DELETE,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,
    // (int32 key=1, int32 val=NULL)
    // Undo Mutations: [@2(DELETE)]
    // Redo Mutations: [@2(DELETE)]

    // Compaction input:
    // Row 1: (int32 key=1, int32 val=NULL)
    //   Undo Mutations: [@2(DELETE)]
    //   Redo Mutations: [@2(DELETE)]
    // Row 2: (int32 key=1, int32 val=NULL)
    //  Undo Mutations: [@1(DELETE)]
    //  Redo Mutations: [@2(DELETE)]

    TEST_COMPACT_TABLET,
  };
  RunFuzzCase(vector<TestOp>(test_ops, test_ops + arraysize(test_ops)));
}

// A particular test case which previously failed TestFuzz.
TEST_F(TestRandomAccess, TestFuzz4) {
  TestOp test_ops[] = {
    TEST_INSERT,
    TEST_FLUSH_OPS,
    TEST_COMPACT_TABLET,
    TEST_DELETE,
    TEST_FLUSH_OPS,
    TEST_COMPACT_TABLET,
    TEST_INSERT,
    TEST_UPDATE,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,
    TEST_DELETE,
    TEST_INSERT,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,
    TEST_UPDATE,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,
    TEST_UPDATE,
    TEST_DELETE,
    TEST_INSERT,
    TEST_DELETE,
    TEST_FLUSH_OPS,
    TEST_FLUSH_TABLET,
    TEST_COMPACT_TABLET
  };
  RunFuzzCase(vector<TestOp>(test_ops, test_ops + arraysize(test_ops)));
}

} // namespace tablet
} // namespace kudu
