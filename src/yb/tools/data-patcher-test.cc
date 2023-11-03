// Copyright (c) YugaByte, Inc.
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

#include <boost/algorithm/string/join.hpp>

#include "yb/docdb/docdb_test_util.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/split.h"

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/mini_master.h"

#include "yb/server/skewed_clock.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/atomic.h"
#include "yb/util/physical_time.h"
#include "yb/util/range.h"
#include "yb/util/subprocess.h"
#include "yb/util/string_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/timestamp.h"

using namespace std::literals;

DECLARE_bool(fail_on_out_of_range_clock_skew);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_string(time_source);
DECLARE_uint64(clock_skew_force_crash_bound_usec);

namespace yb {
namespace tools {

class DataPatcherTest : public CqlTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    server::SkewedClock::Register();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    CqlTestBase<MiniCluster>::SetUp();
  }

  Result<std::string> RunDataPatcher(const std::vector<std::string>& args) {
    std::vector<std::string> command{GetToolPath("data-patcher")};
    for (const auto& arg : args) {
      command.push_back(arg);
    }
    std::string result;
    LOG(INFO) << "Run tool: " << JoinStrings(command, " ");
    RETURN_NOT_OK(Subprocess::Call(command, &result));
    std::vector<std::string> output_lines;
    SplitStringUsing(result, "\n", &output_lines);
    LOG(INFO) << "Standard output from tool:";
    for (const auto& line : output_lines) {
      LOG(INFO) << line;
    }
    return result;
  }
};

// Checks that the values in the table as as expected, and return a vector of their write times.
void CheckAndGetWriteTimes(
    CassandraSession* session, size_t total_values, std::vector<int64_t>* write_times,
    const char* step_description) {
  auto result = ASSERT_RESULT(session->ExecuteWithResult("SELECT i, writetime(j) FROM t"));
  auto iterator = result.CreateIterator();
  std::vector<int32_t> values;
  write_times->clear();
  write_times->resize(total_values);
  std::map<int32_t, int64_t> write_time_by_key;
  while (iterator.Next()) {
    auto key = iterator.Row().Value(0).As<int32_t>();
    values.push_back(key);
    auto time = iterator.Row().Value(1).As<int64_t>();
    ASSERT_GE(key, 0);
    ASSERT_LT(key, total_values);
    if (0 <= key && key < narrow_cast<int32_t>(total_values)) {
      (*write_times)[key] = time;
    }
  }
  for (size_t k = 0; k < total_values; ++k) {
    if ((*write_times)[k] == 0) {
      LOG(WARNING) << "Missing value (" << step_description << "): " << k;
    }
  }

  std::sort(values.begin(), values.end());
  ASSERT_EQ(AsString(values), AsString(Range(total_values)));
  ASSERT_EQ(total_values, write_times->size());
}

template <class Range>
Status InsertValues(CassandraSession* session, const Range& range) {
  std::vector<CassandraFuture> futures;
  for (auto i : range) {
    auto expr = Format("INSERT INTO t (i, j) VALUES ($0, $0);", i);
    if (i & 1) {
      expr = "BEGIN TRANSACTION " + expr + " END TRANSACTION;";
    }
    futures.push_back(session->ExecuteGetFuture(expr));
  }
  for (auto& future : futures) {
    RETURN_NOT_OK(future.Wait());
  }
  return Status::OK();
}

auto NextValueRange(int group_size, size_t* last_value) {
  *last_value += group_size;
  return Range(*last_value);
}

std::vector<int32_t> GetKeyOrder(const std::vector<int64_t>& write_times) {
  std::vector<int32_t> key_order;
  key_order.reserve(write_times.size());
  for (size_t i = 0; i < write_times.size(); ++i) {
    key_order.push_back(narrow_cast<int32_t>(i));
  }
  std::sort(
      key_order.begin(), key_order.end(), [&write_times](int32_t k1, int32_t k2) {
        return write_times[k1] < write_times[k2];
      });
  return key_order;
}

void CheckWriteTimeConsistency(
    const std::vector<int64_t>& old_write_times,
    const std::vector<int64_t>& new_write_times) {
  ASSERT_EQ(old_write_times.size(), new_write_times.size());
  const size_t n = old_write_times.size();

  // Check that old and new keys, when ordered by their write times, are in the same order.
  const auto old_order = GetKeyOrder(old_write_times);
  const auto new_order = GetKeyOrder(old_write_times);
  ASSERT_VECTORS_EQ(old_order, new_order);
  const auto& key_order = old_order;

  size_t k;
  for (k = 0; k < n && old_write_times[key_order[k]] == new_write_times[key_order[k]]; ++k) {}
  LOG(INFO) << "Out of " << n << " keys, the first " << k << " write times were preserved";
}

// Write values with normal time.
// Jump time to a long delta in future.
// Write another set of values.
// Stop cluster.
// Patch SST files to subtract time delta.
// Start cluster.
// Write more values.
// Check that all write times are before current time.
TEST_F(DataPatcherTest, AddTimeDelta) {
  docdb::DisableYcqlPackedRow();

  constexpr int kValueGroupSize = 10;
  constexpr int kDataPatcherConcurrency = 2;
  const MonoDelta kMonoDelta(60min * 24 * 365 * 80);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fail_on_out_of_range_clock_skew) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0.8;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (i INT PRIMARY KEY, j INT) WITH transactions = { 'enabled' : true }"));
  size_t last_value = 0;

  ASSERT_OK(InsertValues(&session, NextValueRange(kValueGroupSize, &last_value)));

  std::vector<int64_t> original_write_times;
  ASSERT_NO_FATALS(CheckAndGetWriteTimes(
      &session, last_value, &original_write_times,
      "after inserting initial rows without clock skew"));
  for (size_t k = 0; k < original_write_times.size(); ++k) {
    LOG(INFO) << "Write time before clock jump for " << k << ": " << original_write_times[k];
  }

  // Cassandra writetime and PhysicalTime are both in microseconds.
  const Timestamp time_before_clock_jump(ASSERT_RESULT(WallClock()->Now()).time_point);
  for (int64_t write_time : original_write_times) {
    ASSERT_LT(write_time, time_before_clock_jump.value());
  }

  const auto jump_clocks = [&]() {
    return JumpClocks(cluster_.get(), kMonoDelta.ToChronoMilliseconds());
  };

  auto delta_changers = jump_clocks();
  ASSERT_OK(InsertValues(&session, NextValueRange(kValueGroupSize, &last_value)));

  ASSERT_OK(InsertValues(&session, NextValueRange(kValueGroupSize, &last_value)));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  // Insert more values after flushing / compacting data, to make sure there is some data that is
  // only available in WAL files, not in SSTables.
  ASSERT_OK(InsertValues(&session, NextValueRange(kValueGroupSize, &last_value)));

  // When we undo the clock jump, all times should be less than this.
  const Timestamp time_after_clock_jump(ASSERT_RESULT(WallClock()->Now()).time_point);

  std::vector<int64_t> write_times_after_jump;
  ASSERT_NO_FATALS(CheckAndGetWriteTimes(
      &session, last_value, &write_times_after_jump, "after inserting some rows with clock skew"));
  for (size_t k = 0; k < write_times_after_jump.size(); ++k) {
    LOG(INFO) << "Write time after clock jump for " << k << ": " << write_times_after_jump[k];
  }

  std::vector<FsManager*> fs_managers;
  for (size_t i = 0; i != cluster_->num_masters(); ++i) {
    fs_managers.push_back(&cluster_->mini_master(i)->fs_manager());
  }
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    fs_managers.push_back(&cluster_->mini_tablet_server(i)->fs_manager());
  }

  std::vector<std::string> data_root_dirs_vec;
  std::vector<std::string> wal_root_dirs_vec;
  for (auto* fs_manager : fs_managers) {
    auto dirs = fs_manager->GetDataRootDirs();
    data_root_dirs_vec.insert(data_root_dirs_vec.end(), dirs.begin(), dirs.end());
    dirs = fs_manager->GetWalRootDirs();
    wal_root_dirs_vec.insert(wal_root_dirs_vec.end(), dirs.begin(), dirs.end());
  }
  auto data_root_dirs = boost::join(data_root_dirs_vec, ",");
  auto wal_root_dirs = boost::join(wal_root_dirs_vec, ",");
  LOG(INFO) << "Data dirs: " << data_root_dirs;
  LOG(INFO) << "WAL dirs: " << wal_root_dirs;

  LOG(INFO) << "Running data-patcher sub-time before shutting down the cluster, without WAL dirs";
  std::vector<std::string> args{
      "sub-time", "--delta", Format("$0s", static_cast<int64_t>(kMonoDelta.ToSeconds())),
      "--bound-time", time_before_clock_jump.ToFormattedString(),
      "--data-dirs", data_root_dirs,
      "--concurrency", Format("$0", kDataPatcherConcurrency),
      "--debug"
  };
  ASSERT_OK(RunDataPatcher(args));

  LOG(INFO) << "Shutting down the cluster";
  ShutdownCluster();

  args.push_back("--wal-dirs");
  args.push_back(wal_root_dirs);
  LOG(INFO) << "Running data-patcher sub-time after shutting down the cluster, with WAL dirs";
  ASSERT_OK(RunDataPatcher(args));

  delta_changers.clear();
  LOG(INFO) << "Running data-patcher apply-patch with --dry-run";
  args = {"apply-patch", "--data-dirs", data_root_dirs, "--wal-dirs", wal_root_dirs, "--dry-run"};
  ASSERT_OK(RunDataPatcher(args));

  args.resize(args.size() - 1);
  LOG(INFO) << "Running data-patcher apply-patch without --dry-run";
  ASSERT_OK(RunDataPatcher(args));

  const auto start_cluster = [&]() {
    ASSERT_OK(StartCluster());
    session = ASSERT_RESULT(EstablishSession(driver_.get()));
  };
  start_cluster();

  // Check that we have the same values as before and their timestamps are in the right order.
  std::vector<int64_t> patched_write_times;
  ASSERT_NO_FATALS(CheckAndGetWriteTimes(
      &session, last_value, &patched_write_times, "after using data-patcher to undo clock skew"));
  ASSERT_NO_FATALS(CheckWriteTimeConsistency(write_times_after_jump, patched_write_times));
  for (auto write_time : patched_write_times) {
    ASSERT_LT(write_time, time_after_clock_jump.value());
  }

  const auto original_last_value = last_value;
  ASSERT_OK(InsertValues(&session, NextValueRange(kValueGroupSize, &last_value)));
  std::vector<int64_t> patched_write_times_with_extra_rows;
  ASSERT_NO_FATALS(CheckAndGetWriteTimes(
      &session, last_value, &patched_write_times_with_extra_rows,
      "after undoing clock skew and inserting more rows"));

  LOG(INFO) << "Shutting down cluster before reverting the data-patcher changes";
  ShutdownCluster();

  LOG(INFO) << "Running revert with --dry-run";
  args.push_back("--revert");
  args.push_back("--dry-run");
  ASSERT_OK(RunDataPatcher(args));

  LOG(INFO) << "Running revert without --dry-run";
  args.resize(args.size() - 1);
  ASSERT_OK(RunDataPatcher(args));

  LOG(INFO) << "Turning off clock skew checking and restarting the cluster";
  SetAtomicFlag(0, &FLAGS_clock_skew_force_crash_bound_usec);
  SetAtomicFlag(false, &FLAGS_fail_on_out_of_range_clock_skew);
  start_cluster();
  delta_changers = jump_clocks();

  std::vector<int64_t> write_times_after_revert;
  ASSERT_NO_FATALS(CheckAndGetWriteTimes(
      &session, original_last_value, &write_times_after_revert,
      "after reverting the effect of data-patcher, going back to clock skew"));
  ASSERT_VECTORS_EQ(write_times_after_jump, write_times_after_revert);
}

} // namespace tools
} // namespace yb
