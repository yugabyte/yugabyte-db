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

#include <gtest/gtest.h>

#include "yb/qlexpr/ql_expr.h"
#include "yb/common/ql_protocol_util.h"

#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/dockv/reader_projection.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-base.h"
#include "yb/tablet/tablet.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/status_log.h"
#include "yb/util/test_graph.h"
#include "yb/util/thread.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_int32(num_counter_threads, 8, "Number of counting threads to launch");
DEFINE_NON_RUNTIME_int32(num_summer_threads, 1, "Number of summing threads to launch");
DEFINE_NON_RUNTIME_int32(num_slowreader_threads, 1, "Number of 'slow' reader threads to launch");
DEFINE_NON_RUNTIME_int32(inserts_per_thread, 1000,
                         "Number of rows inserted by the inserter thread");

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
    dockv::ReaderProjection projection(client_schema_);
    auto iter = ASSERT_RESULT(tablet()->NewRowIterator(projection));
    ASSERT_OK(iter->FetchNext(nullptr));
    const SchemaPtr schema = tablet()->schema();
    auto valcol = ASSERT_RESULT(schema->ColumnIdByName("val"));
    valcol_projection_.Init(*schema, {valcol});

    iter = ASSERT_RESULT(tablet()->NewRowIterator(valcol_projection_));
    ASSERT_OK(iter->FetchNext(nullptr));

    ts_collector_.StartDumperThread();
  }

  VerifyRowsTabletTest()
      : running_insert_count_(kNumInsertThreads),
        ts_collector_(::testing::UnitTest::GetInstance()->current_test_info()->test_case_name()) {
  }

  void InsertThread(int tid) {
    CountDownOnScopeExit dec_count(&running_insert_count_);
    shared_ptr<TimeSeries> inserts = ts_collector_.GetTimeSeries("inserted");

    int32_t max_rows = this->ClampRowCount(FLAGS_inserts_per_thread * kNumInsertThreads)
        / kNumInsertThreads;

    if (max_rows < FLAGS_inserts_per_thread) {
      LOG(WARNING) << "Clamping the inserts per thread to " << max_rows << " to prevent overflow";
    }

    this->InsertTestRows(tid * max_rows, max_rows, 0, inserts.get());
  }

  void UpdateThread(int tid) {
    const Schema &schema = schema_;

    shared_ptr<TimeSeries> updates = ts_collector_.GetTimeSeries("updated");

    LocalTabletWriter writer(this->tablet(), &this->client_schema_);

    faststring update_buf;

    uint64_t updates_since_last_report = 0;
    int col_idx = schema.num_key_columns() == 1 ? 2 : 3;
    LOG(INFO) << "Update thread using schema: " << schema.ToString();

    dockv::YBPartialRow row(&client_schema_);

    qlexpr::QLTableRow value_map;

    while (running_insert_count_.count() > 0) {
      auto iter = tablet()->NewRowIterator(client_schema_);
      CHECK_OK(iter);

      while (ASSERT_RESULT((**iter).FetchNext(&value_map)) && running_insert_count_.count() > 0) {
        unsigned int seed = 1234;
        if (rand_r(&seed) % 10 == 7) {
          // Increment the "val"

          QLValue value;
          CHECK_OK(value_map.GetValue(schema.column_id(col_idx), &value));
          int32_t new_val = value.int32_value() + 1;

          // Rebuild the key by extracting the cells from the row
          QLWriteRequestPB req;
          setup_.BuildRowKeyFromExistingRow(&req, value_map);
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
    qlexpr::QLTableRow row;

    auto max_rows = this->ClampRowCount(FLAGS_inserts_per_thread * kNumInsertThreads)
            / kNumInsertThreads;

    int max_iters = kNumInsertThreads * max_rows / 10;

    while (running_insert_count_.count() > 0) {
      dockv::ReaderProjection projection(client_schema_);
      auto iter = ASSERT_RESULT(tablet()->NewRowIterator(projection));

      for (int i = 0; i < max_iters && ASSERT_RESULT(iter->FetchNext(&row)); i++) {
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
    uint64_t count_since_report = 0;

    uint64_t sum = 0;

    auto iter = tablet()->NewRowIterator(valcol_projection_);
    CHECK_OK(iter);

    qlexpr::QLTableRow row;
    while (CHECK_RESULT((**iter).FetchNext(&row))) {
      QLValue value;
      CHECK_OK(row.GetValue(schema_.column_id(2), &value));
      if (!value.IsNull()) {
        CHECK(value.value().has_int32_value()) << "Row: " << row.ToString();
        sum += value.int32_value();
      }
      count_since_report += 1;

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
    LocalTabletWriter writer(this->tablet());

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
    LocalTabletWriter writer(this->tablet());
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
  dockv::ReaderProjection valcol_projection_;

  TimeSeriesCollector ts_collector_;
};

TYPED_TEST_CASE(VerifyRowsTabletTest, TabletTestHelperTypes);

TYPED_TEST(VerifyRowsTabletTest, DoTestAllAtOnce) {
  if (1000 == FLAGS_inserts_per_thread) {
    if (AllowSlowTests()) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_inserts_per_thread) = 50000;
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

  auto max_rows = this->ClampRowCount(FLAGS_inserts_per_thread * kNumInsertThreads)
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
