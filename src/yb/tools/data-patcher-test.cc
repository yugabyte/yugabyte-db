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

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/mini_master.h"

#include "yb/server/skewed_clock.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/physical_time.h"
#include "yb/util/range.h"
#include "yb/util/subprocess.h"
#include "yb/util/string_util.h"
#include "yb/util/timestamp.h"

using namespace std::literals;

DECLARE_bool(fail_on_out_of_range_clock_skew);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_string(time_source);

namespace yb {
namespace tools {

class DataPatcherTest : public CqlTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    server::SkewedClock::Register();
    FLAGS_time_source = server::SkewedClock::kName;
    CqlTestBase<MiniCluster>::SetUp();
  }

  template <class... Args>
  Result<std::string> RunDataPatcher(Args&&... args) {
    auto command = ToStringVector(GetToolPath("data-patcher"), std::forward<Args>(args)...);
    std::string result;
    LOG(INFO) << "Run tool: " << AsString(command);
    RETURN_NOT_OK(Subprocess::Call(command, &result));
    return result;
  }
};

Result<std::pair<int64_t, int64_t>> CheckValues(CassandraSession* session, int total_values) {
  auto result = VERIFY_RESULT(session->ExecuteWithResult("SELECT i, writetime(j) FROM t"));
  auto iterator = result.CreateIterator();
  std::vector<int32_t> values;
  int64_t now = VERIFY_RESULT(WallClock()->Now()).time_point;
  std::pair<int64_t, int64_t> min_max(
      std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::min());
  std::pair<int32_t, int32_t> min_max_key;
  while (iterator.Next()) {
    auto key = iterator.Row().Value(0).As<int32_t>();
    values.push_back(key);
    auto time = iterator.Row().Value(1).As<int64_t>();
    auto delta = now - time;
    if (delta < min_max.first) {
      min_max.first = delta;
      min_max_key.first = key;
    }
    if (delta > min_max.second) {
      min_max.second = delta;
      min_max_key.second = key;
    }
  }
  std::sort(values.begin(), values.end());
  if (AsString(values) != AsString(Range(total_values))) {
    EXPECT_EQ(AsString(values), AsString(Range(total_values)));
    return STATUS(IllegalState, "Fetched values mismatch");
  }
  LOG(INFO) << "Indexes: " << AsString(min_max_key);
  return min_max;
}

template <class Range>
CHECKED_STATUS InsertValues(CassandraSession* session, const Range& range) {
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

auto NextValueRange(int group_size, int* last_value) {
  *last_value += group_size;
  return Range(*last_value);
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
  constexpr int kValueGroupSize = 10;
  const MonoDelta kMonoDelta(60min * 24 * 365 * 80);

  FLAGS_fail_on_out_of_range_clock_skew = false;
  FLAGS_TEST_transaction_ignore_applying_probability = 0.8;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (i INT PRIMARY KEY, j INT) WITH transactions = { 'enabled' : true }"));
  int last_value = 0;

  ASSERT_OK(InsertValues(&session, NextValueRange(kValueGroupSize, &last_value)));
  ASSERT_OK(CheckValues(&session, last_value));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  auto delta_changers = JumpClocks(cluster_.get(), kMonoDelta.ToChronoMilliseconds());
  ASSERT_OK(InsertValues(&session, NextValueRange(kValueGroupSize, &last_value)));
  auto min_max = ASSERT_RESULT(CheckValues(&session, last_value));
  LOG(INFO) << "Time range: " << AsString(min_max);
  ASSERT_LT(min_max.second, 0);

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  std::vector<FsManager*> fs_managers;
  for (int i = 0; i != cluster_->num_masters(); ++i) {
    fs_managers.push_back(&cluster_->mini_master(i)->fs_manager());
  }
  for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
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
  ShutdownCluster();
  ASSERT_OK(RunDataPatcher(
      "sub-time", "--delta", Format("$0s", static_cast<int64_t>(kMonoDelta.ToSeconds())),
      "--bound-time", time.ToFormattedString(),
      "--data-dirs", data_root_dirs, "--wal-dirs", wal_root_dirs));
  delta_changers.clear();
  ASSERT_OK(RunDataPatcher(
      "apply-patch", "--data-dirs", data_root_dirs, "--wal-dirs", wal_root_dirs));
  ASSERT_OK(StartCluster());

  session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(InsertValues(&session, NextValueRange(kValueGroupSize, &last_value)));

  // Check that we have all values and all values are written before now.
  auto new_min_max = ASSERT_RESULT(CheckValues(&session, last_value));
  LOG(INFO) << "New time range: " << AsString(new_min_max);
  ASSERT_GT(new_min_max.first, 0);
}

} // namespace tools
} // namespace yb
