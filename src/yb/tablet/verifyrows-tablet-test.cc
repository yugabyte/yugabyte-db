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

#include <boost/ptr_container/ptr_vector.hpp>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/common/ql_protocol_util.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-base.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/test_graph.h"
#include "yb/util/thread.h"

DEFINE_int32(num_counter_threads, 8, "Number of counting threads to launch");
DEFINE_int32(num_summer_threads, 1, "Number of summing threads to launch");
DEFINE_int32(num_slowreader_threads, 1, "Number of 'slow' reader threads to launch");
DEFINE_int64(inserts_per_thread, 1000, "Number of rows inserted by the inserter thread");

using std::shared_ptr;

namespace yb {
namespace tablet {

// We use only one thread for now as each thread picks an OpId via a WriteTnxState for write and
// could reach rocksdb::MemTable::SetLastOpId() async'ly causing out of order assertion.
// There could be multiple threads, as long as they get OpId's assigned in order.
const int kNumInsertThreads = 1;

template<class SETUP>
class VerifyRowsTabletTest : public TabletTestBase<SETUP> {
  // Import some names from superclass, since C++ is stingy about
  // letting us refer to the members otherwise.
  typedef TabletTestBase<SETUP> superclass;
  using superclass::schema_;
  using superclass::client_schema_;
  using superclass::tablet;
  using superclass::setup_;
 public:
  virtual void SetUp() {
    superclass::SetUp();

    // Warm up code cache with all the projections we'll be using.
    gscoped_ptr<RowwiseIterator> iter;
    CHECK_OK(tablet()->NewRowIterator(client_schema_, boost::none, &iter));
    const Schema* schema = tablet()->schema();
    ColumnSchema valcol = schema->column(schema->find_column("val"));
    valcol_projection_ = Schema({ valcol }, 0);
    CHECK_OK(tablet()->NewRowIterator(valcol_projection_, boost::none, &iter));

    ts_collector_.StartDumperThread();
  }

  VerifyRowsTabletTest()
      : running_insert_count_(kNumInsertThreads),
        ts_collector_(::testing::UnitTest::GetInstance()->current_test_info()->test_case_name()) {
  }

  void InsertThread(int tid) {
    CountDownOnScopeExit dec_count(&running_insert_count_);
    shared_ptr<TimeSeries> inserts = ts_collector_.GetTimeSeries("inserted");

    uint64_t max_rows = this->ClampRowCount(FLAGS_inserts_per_thread * kNumInsertThreads)
        / kNumInsertThreads;

    if (max_rows < FLAGS_inserts_per_thread) {
      LOG(WARNING) << "Clamping the inserts per thread to " << max_rows << " to prevent overflow";
    }

    this->InsertTestRows(tid * max_rows, max_rows, 0, inserts.get());
  }

  void UpdateThread(int tid) {
    const Schema &schema = schema_;

    shared_ptr<TimeSeries> updates = ts_collector_.GetTimeSeries("updated");

    LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);

    Arena tmp_arena(1024, 1024);
    RowBlock block(schema_, 1, &tmp_arena);
    faststring update_buf;

    uint64_t updates_since_last_report = 0;
    int col_idx = schema.num_key_columns() == 1 ? 2 : 3;
    LOG(INFO) << "Update thread using schema: " << schema.ToString();

    YBPartialRow row(&client_schema_);

    while (running_insert_count_.count() > 0) {
      gscoped_ptr<RowwiseIterator> iter;
      CHECK_OK(tablet()->NewRowIterator(client_schema_, boost::none, &iter));
      ScanSpec scan_spec;
      CHECK_OK(iter->Init(&scan_spec));

      while (iter->HasNext() && running_insert_count_.count() > 0) {
        tmp_arena.Reset();
        CHECK_OK(iter->NextBlock(&block));
        CHECK_EQ(block.nrows(), 1);

        if (!block.selection_vector()->IsRowSelected(0)) {
          // Don't try to update rows which aren't visible yet --
          // this will crash, since the data in row_slice isn't even copied.
          continue;
        }

        RowBlockRow rb_row = block.row(0);
        unsigned int seed = 1234;
        if (rand_r(&seed) % 10 == 7) {
          // Increment the "val"
          const int32_t *old_val = schema.ExtractColumnFromRow<INT32>(rb_row, col_idx);
          // Issue an update. In the NullableValue setup, many of the rows start with
          // NULL here, so we have to check for it.
          int32_t new_val;
          if (old_val != nullptr) {
            new_val = *old_val + 1;
          } else {
            new_val = 0;
          }

          // Rebuild the key by extracting the cells from the row
          QLWriteRequestPB req;
          setup_.BuildRowKeyFromExistingRow(&req, rb_row);
          QLAddInt32ColumnValue(&req, kFirstColumnId + col_idx, new_val);
          CHECK_OK(writer.Write(&req));

          if (++updates_since_last_report >= 10) {
            updates->AddValue(updates_since_last_report);
            updates_since_last_report = 0;
          }
        }
      }
    }
  }

  // Thread which iterates slowly over the first 10% of the data.
  // This is meant to test that outstanding iterators don't end up
  // trying to reference already-freed memory.
  void SlowReaderThread(int tid) {
    Arena arena(32*1024, 256*1024);
    RowBlock block(schema_, 1, &arena);

    uint64_t max_rows = this->ClampRowCount(FLAGS_inserts_per_thread * kNumInsertThreads)
            / kNumInsertThreads;

    int max_iters = kNumInsertThreads * max_rows / 10;

    while (running_insert_count_.count() > 0) {
      gscoped_ptr<RowwiseIterator> iter;
      CHECK_OK(tablet()->NewRowIterator(client_schema_, boost::none, &iter));
      ScanSpec scan_spec;
      CHECK_OK(iter->Init(&scan_spec));

      for (int i = 0; i < max_iters && iter->HasNext(); i++) {
        CHECK_OK(iter->NextBlock(&block));

        if (running_insert_count_.WaitFor(MonoDelta::FromMilliseconds(1))) {
          return;
        }
      }
    }
  }

  void SummerThread(int tid) {
    shared_ptr<TimeSeries> scanned_ts = ts_collector_.GetTimeSeries("scanned");

    while (running_insert_count_.count() > 0) {
      CountSum(scanned_ts);
    }
  }

  uint64_t CountSum(const shared_ptr<TimeSeries> &scanned_ts) {
    Arena arena(1024, 1024); // unused, just scanning ints

    static const int kBufInts = 1024*1024 / 8;
    RowBlock block(valcol_projection_, kBufInts, &arena);
    ColumnBlock column = block.column_block(0);

    uint64_t count_since_report = 0;

    uint64_t sum = 0;

    gscoped_ptr<RowwiseIterator> iter;
    CHECK_OK(tablet()->NewRowIterator(valcol_projection_, boost::none, &iter));
    ScanSpec scan_spec;
    CHECK_OK(iter->Init(&scan_spec));

    while (iter->HasNext()) {
      arena.Reset();
      CHECK_OK(iter->NextBlock(&block));

      for (size_t j = 0; j < block.nrows(); j++) {
        sum += *reinterpret_cast<const int32_t *>(column.cell_ptr(j));
      }
      count_since_report += block.nrows();

      // Report metrics if enough time has passed
      if (count_since_report > 100) {
        if (scanned_ts.get()) {
          scanned_ts->AddValue(count_since_report);
        }
        count_since_report = 0;
      }
    }

    if (scanned_ts.get()) {
      scanned_ts->AddValue(count_since_report);
    }

    return sum;
  }

  // Thread which cycles between inserting and deleting a test row, each time
  // with a different value.
  void DeleteAndReinsertCycleThread(int tid) {
    int32_t iteration = 0;
    LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);

    while (running_insert_count_.count() > 0) {
      for (int i = 0; i < 100; i++) {
        CHECK_OK(this->InsertTestRow(&writer, tid, iteration++));
        CHECK_OK(this->DeleteTestRow(&writer, tid));
      }
    }
  }

  // Thread which continuously sends updates at the same row, ignoring any
  // "not found" errors that might come back. This is used simultaneously with
  // DeleteAndReinsertCycleThread to check for races where we might accidentally
  // succeed in UPDATING a ghost row.
  void StubbornlyUpdateSameRowThread(int tid) {
    int32_t iteration = 0;
    LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
    while (running_insert_count_.count() > 0) {
      for (int i = 0; i < 100; i++) {
        Status s = this->UpdateTestRow(&writer, tid, iteration++);
        if (!s.ok() && !s.IsNotFound()) {
          // We expect "not found", but not any other errors.
          CHECK_OK(s);
        }
      }
    }
  }

  template<typename FunctionType>
  void StartThreads(int n_threads, const FunctionType &function) {
    for (int i = 0; i < n_threads; i++) {
      scoped_refptr<yb::Thread> new_thread;
      CHECK_OK(yb::Thread::Create("test", strings::Substitute("test$0", i),
          function, this, i, &new_thread));
      threads_.push_back(new_thread);
    }
  }

  void JoinThreads() {
    for (scoped_refptr<yb::Thread> thr : threads_) {
     CHECK_OK(ThreadJoiner(thr.get()).Join());
    }
  }

  std::vector<scoped_refptr<yb::Thread> > threads_;
  CountDownLatch running_insert_count_;

  // Projection with only an int column.
  // This is provided by both harnesses.
  Schema valcol_projection_;

  TimeSeriesCollector ts_collector_;
};

TYPED_TEST_CASE(VerifyRowsTabletTest, TabletTestHelperTypes);

TYPED_TEST(VerifyRowsTabletTest, DoTestAllAtOnce) {
  if (1000 == FLAGS_inserts_per_thread) {
    if (AllowSlowTests()) {
      FLAGS_inserts_per_thread = 50000;
    }
  }

  // Spawn a bunch of threads, each of which will do updates.
  this->StartThreads(kNumInsertThreads, &TestFixture::InsertThread);
  this->StartThreads(FLAGS_num_summer_threads, &TestFixture::SummerThread);
  this->StartThreads(FLAGS_num_slowreader_threads, &TestFixture::SlowReaderThread);
  this->JoinThreads();
  LOG_TIMING(INFO, "Summing int32 column") {
    uint64_t sum = this->CountSum(shared_ptr<TimeSeries>());
    LOG(INFO) << "Sum = " << sum;
  }

  uint64_t max_rows = this->ClampRowCount(FLAGS_inserts_per_thread * kNumInsertThreads)
          / kNumInsertThreads;

  this->VerifyTestRows(0, max_rows * kNumInsertThreads);

  // Start up a bunch of threads which repeatedly insert and delete the same
  // row, while flushing and compacting. This checks various concurrent handling
  // of DELETE/REINSERT during flushes.
  google::FlagSaver saver;
  this->StartThreads(10, &TestFixture::DeleteAndReinsertCycleThread);
  this->StartThreads(10, &TestFixture::StubbornlyUpdateSameRowThread);

  // Run very quickly in dev builds, longer in slow builds.
  float runtime_seconds = AllowSlowTests() ? 2 : 0.1;
  Stopwatch sw;
  sw.start();
  while (sw.elapsed().wall < runtime_seconds * NANOS_PER_SECOND &&
         !this->HasFatalFailure()) {
    SleepFor(MonoDelta::FromMicroseconds(5000));
  }

  // This is sort of a hack -- the flusher thread stops when it sees this
  // countdown latch go to 0.
  this->running_insert_count_.Reset(0);
  this->JoinThreads();
}

// NOTE: Cannot add another TYPED_TEST here. The opid chosen for the next insert might
// be out of order and will assert in rocksdb::MemTable::SetLastOpId().

} // namespace tablet
} // namespace yb
