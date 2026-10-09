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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <atomic>

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

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/write_controller.h"

#include "yb/dockv/reader_projection.h"

#include "yb/util/cast.h"
#include "yb/util/enums.h"
#include "yb/util/scope_exit.h"
#include "yb/util/slice.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"
#include "yb/util/flags.h"

DECLARE_bool(TEST_skip_write_stop_check_in_should_apply_write);

using std::string;
using std::vector;
using namespace std::literals;

namespace yb {
namespace tablet {

DEFINE_NON_RUNTIME_int32(testiterator_num_inserts, 1000,
             "Number of rows inserted in TestRowIterator/TestInsert");

static_assert(static_cast<int>(std::to_underlying(TableType::YQL_TABLE_TYPE)) ==
                  std::to_underlying(client::YBTableType::YQL_TABLE_TYPE),
              "Numeric code for YQL_TABLE_TYPE table type must be consistent");
static_assert(static_cast<int>(std::to_underlying(TableType::REDIS_TABLE_TYPE)) ==
                  std::to_underlying(client::YBTableType::REDIS_TABLE_TYPE),
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
  ASSERT_OK(tablet->Flush(FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
  OpId id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  const int64_t start_index = id.index;

  this->InsertTestRows(1, kCount, 555);
  id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  ASSERT_EQ(id.index, start_index);

  ASSERT_OK(tablet->Flush(FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
  id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  ASSERT_EQ(id.index, start_index + kCount);

  this->InsertTestRows(1, kCount, 777);
  id = ASSERT_RESULT(tablet->MaxPersistentOpId()).regular;
  ASSERT_EQ(id.index, start_index + kCount);

  ASSERT_OK(tablet->Flush(FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
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

TYPED_TEST(TestTablet, ShouldApplyWriteRespectsWriteStop) {
  auto* db = down_cast<rocksdb::DBImpl*>(this->tablet()->regular_db());
  auto& write_controller = db->TEST_write_controller();

  ASSERT_TRUE(this->tablet()->ShouldApplyWrite());

  {
    auto stop_token = write_controller.GetStopToken();
    ASSERT_TRUE(write_controller.IsStopped());
    ASSERT_FALSE(write_controller.NeedsDelay());
    ASSERT_FALSE(this->tablet()->ShouldApplyWrite());

    auto stop_token_2 = write_controller.GetStopToken();
    ASSERT_FALSE(this->tablet()->ShouldApplyWrite());

    stop_token.reset();
    ASSERT_TRUE(write_controller.IsStopped());
    ASSERT_FALSE(this->tablet()->ShouldApplyWrite());
  }
  ASSERT_FALSE(write_controller.IsStopped());
  ASSERT_TRUE(this->tablet()->ShouldApplyWrite());
}

TYPED_TEST(TestTablet, ShouldApplyWriteRespectsWriteDelay) {
  auto* db = down_cast<rocksdb::DBImpl*>(this->tablet()->regular_db());
  auto& write_controller = db->TEST_write_controller();

  ASSERT_TRUE(this->tablet()->ShouldApplyWrite());

  {
    auto delay_token = write_controller.GetDelayToken(1024);
    ASSERT_FALSE(write_controller.IsStopped());
    ASSERT_TRUE(write_controller.NeedsDelay());
    ASSERT_FALSE(this->tablet()->ShouldApplyWrite());
  }
  ASSERT_TRUE(this->tablet()->ShouldApplyWrite());
}

// Demonstrates that without the AreWritesStopped() check (controlled via
// TEST_skip_write_stop_check_in_should_apply_write), ShouldApplyWrite() does
// not detect kStop-type stalls triggered by db_max_flushing_bytes. See #30728.
TYPED_TEST(TestTablet, ShouldApplyWriteWithoutStopCheckIgnoresWriteStop) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_write_stop_check_in_should_apply_write) = true;
  auto se = ScopeExit([&] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_write_stop_check_in_should_apply_write) = false;
  });

  auto* db = down_cast<rocksdb::DBImpl*>(this->tablet()->regular_db());
  auto& write_controller = db->TEST_write_controller();

  // NeedsDelay() only checks total_delayed_, not total_stopped_.
  // So a stop token is invisible when the AreWritesStopped() check is skipped.
  auto stop_token = write_controller.GetStopToken();
  ASSERT_TRUE(write_controller.IsStopped());
  ASSERT_TRUE(this->tablet()->ShouldApplyWrite());

  stop_token.reset();

  // Delay tokens use total_delayed_ and are still detected by NeedsDelay().
  auto delay_token = write_controller.GetDelayToken(1024);
  ASSERT_FALSE(this->tablet()->ShouldApplyWrite());
  delay_token.reset();
}

TYPED_TEST(TestTablet, ShouldApplyWriteRespectsStopAndDelaySimultaneously) {
  auto* db = down_cast<rocksdb::DBImpl*>(this->tablet()->regular_db());
  auto& write_controller = db->TEST_write_controller();

  auto stop_token = write_controller.GetStopToken();
  auto delay_token = write_controller.GetDelayToken(1024);
  ASSERT_TRUE(write_controller.IsStopped());
  ASSERT_TRUE(write_controller.NeedsDelay());
  ASSERT_FALSE(this->tablet()->ShouldApplyWrite());

  stop_token.reset();
  ASSERT_FALSE(write_controller.IsStopped());
  ASSERT_TRUE(write_controller.NeedsDelay());
  ASSERT_FALSE(this->tablet()->ShouldApplyWrite());

  delay_token.reset();
  ASSERT_TRUE(this->tablet()->ShouldApplyWrite());
}

// Tablet::AreWritesStopped() is the lightweight check used by the early rejection
// in RaftConsensus::Update() before acquiring update_mutex_. It only checks for hard
// stops (WriteController::IsStopped()), not delays. See #30728.
TYPED_TEST(TestTablet, AreWritesStoppedDetectsHardStop) {
  auto* db = down_cast<rocksdb::DBImpl*>(this->tablet()->regular_db());
  auto& write_controller = db->TEST_write_controller();

  ASSERT_FALSE(this->tablet()->AreWritesStopped());

  {
    auto stop_token = write_controller.GetStopToken();
    ASSERT_TRUE(this->tablet()->AreWritesStopped());

    auto stop_token_2 = write_controller.GetStopToken();
    ASSERT_TRUE(this->tablet()->AreWritesStopped());

    stop_token.reset();
    ASSERT_TRUE(this->tablet()->AreWritesStopped());
  }
  ASSERT_FALSE(this->tablet()->AreWritesStopped());
}

// AreWritesStopped() does not react to delay tokens -- only hard stops.
// This is intentional: the early rejection in RaftConsensus::Update() should only
// fire for hard stops where DelayWrite() would block indefinitely on bg_cv_.Wait().
// Delay tokens cause a bounded sleep, not an indefinite block.
TYPED_TEST(TestTablet, AreWritesStoppedIgnoresDelay) {
  auto* db = down_cast<rocksdb::DBImpl*>(this->tablet()->regular_db());
  auto& write_controller = db->TEST_write_controller();

  auto delay_token = write_controller.GetDelayToken(1024);
  ASSERT_TRUE(write_controller.NeedsDelay());
  ASSERT_FALSE(this->tablet()->AreWritesStopped());
}

class TabletIteratorShutdownTest : public TabletTestBase<StringKeyTestSetup> {};

// An iterator from Tablet::NewRowIterator owns the ScopedRWOperation that blocks RocksDB shutdown,
// and a tablet shutdown parked on that operation resumes the instant it is released. Releasing it
// before the iterator's RocksDB iterators are torn down therefore lets the shutdown destroy the DBs
// underneath CleanupIteratorState, which crashes in DBImpl::PurgeObsoleteFiles. See issue #33496.
TEST_F(TabletIteratorShutdownTest, ShutdownWaitsForIteratorTeardown) {
  LocalTabletWriter writer(tablet());
  ASSERT_OK(InsertTestRow(&writer, 0, 0));

  dockv::ReaderProjection projection(schema_);
  auto iter = ASSERT_RESULT(tablet()->NewRowIterator(projection));
  ASSERT_OK(iter->FetchNext(nullptr));

  std::atomic<bool> destroying_iter{false};
  std::atomic<bool> checked{false};
  std::atomic<bool> dbs_destroyed{false};
  std::atomic<bool> destroyed_during_teardown{false};

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("Tablet::CompleteShutdownStorages:Start", [&dbs_destroyed](void*) {
    dbs_destroyed.store(true, std::memory_order_release);
  });
  sync_point->SetCallBack("IntentAwareIterator::~IntentAwareIterator", [&](void*) {
    if (!destroying_iter.load(std::memory_order_acquire)) {
      return;
    }
    bool expected = false;
    if (!checked.compare_exchange_strong(expected, true)) {
      return;
    }
    // Give a shutdown that has already been let go a chance to reach the DB teardown. While the
    // pending operation is still held -- the ordering this test asserts -- this always times out.
    const auto deadline = CoarseMonoClock::now() + 2s * kTimeMultiplier;
    while (CoarseMonoClock::now() < deadline && !dbs_destroyed.load(std::memory_order_acquire)) {
      SleepFor(10ms);
    }
    if (dbs_destroyed.load(std::memory_order_acquire)) {
      destroyed_during_teardown.store(true, std::memory_order_release);
    }
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  std::atomic<bool> shutdown_done{false};
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &shutdown_done] {
    tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
    tablet()->CompleteShutdown();
    shutdown_done.store(true, std::memory_order_release);
  });

  // The live iterator holds the operation StartShutdownStorages waits on, so shutdown parks.
  SleepFor(500ms * kTimeMultiplier);
  ASSERT_FALSE(shutdown_done.load(std::memory_order_acquire));
  ASSERT_FALSE(dbs_destroyed.load(std::memory_order_acquire));

  destroying_iter.store(true, std::memory_order_release);
  iter.reset();

  thread_holder.JoinAll();
  ASSERT_TRUE(shutdown_done.load(std::memory_order_acquire));
  // Without this the test passes vacuously if the sync point never fires, e.g. if it is switched
  // to DEBUG_ONLY_TEST_SYNC_POINT or sync point processing is disabled.
  ASSERT_TRUE(checked.load(std::memory_order_acquire))
      << "the ~IntentAwareIterator sync point never fired, so the ordering was never checked";
  ASSERT_FALSE(destroyed_during_teardown.load(std::memory_order_acquire))
      << "RocksDB was destroyed while the iterator's RocksDB iterators were still being torn down";
}

} // namespace tablet
} // namespace yb
