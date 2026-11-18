// Copyright (c) YugabyteDB, Inc.
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

#include <unistd.h>

#include "yb/client/client.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/fs/fs_manager.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/errno.h"
#include "yb/util/path_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using namespace std::literals;

DECLARE_bool(TEST_simulate_abrupt_server_restart);

DECLARE_bool(allow_encryption_at_rest);

DECLARE_bool(TEST_disable_flush_on_shutdown);

DECLARE_bool(log_enable_background_sync);

DECLARE_int64(reuse_unclosed_segment_threshold_bytes);

DECLARE_bool(enable_flush_retryable_requests);

DECLARE_bool(TEST_asyncrpc_finished_set_timedout);

DECLARE_int32(retryable_request_timeout_secs);

DECLARE_int32(interval_durable_wal_write_ms);

DECLARE_int32(bytes_durable_wal_write_mb);

namespace yb {
namespace integration_tests {

class RestartTest : public YBTableTestBase {
 protected:

  bool use_external_mini_cluster() override { return false; }

  size_t num_tablet_servers() override { return 3; }

  int num_tablets() override { return 1; }

  void GetTablet(const client::YBTableName& table_name, string* tablet_id) {
    std::vector<std::string> ranges;
    std::vector<TabletId> tablet_ids;
    ASSERT_OK(client_->GetTablets(table_name, 0 /* max_tablets */, &tablet_ids, &ranges));
    ASSERT_EQ(tablet_ids.size(), 1);
    *tablet_id = tablet_ids[0];
  }

  void ShutdownTabletPeer(const std::shared_ptr<tablet::TabletPeer> &tablet_peer) {
    ASSERT_OK(tablet_peer->TEST_Shutdown(tablet::ShouldAbortActiveTransactions::kTrue,
                                    tablet::DisableFlushOnShutdown::kFalse));
  }

  void CheckSampleKeysValues(int start, int end) {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    ASSERT_EQ(end - start + 1, result_kvs.size());

    for(int i = start ; i <= end ; i++) {
      std::string num_str = std::to_string(i);
      ASSERT_EQ("key_" + num_str, result_kvs[i - start].first);
      ASSERT_EQ("value_" + num_str, result_kvs[i - start].second);
    }
  }

  void CheckKeyValue(int key, int value) {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    for (size_t i = 0; i < result_kvs.size(); i++) {
      std::string key_str = "key_" + std::to_string(key);
      if (result_kvs[i].first == key_str) {
        ASSERT_EQ("value_" + std::to_string(value), result_kvs[i].second);
      }
    }
  }
};

class LogSyncTest : public RestartTest {
 protected:
  void BeforeStartCluster() override {
    // setting the flag immaterial of the default value
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_enable_background_sync) = true;
  }
};

TEST_F(RestartTest, WalFooterProperlyInitialized) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = true;
  // Disable reuse unclosed segment feature to prevent log from reusing
  // the last segment and skipping building footer.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reuse_unclosed_segment_threshold_bytes) = -1;
  string tablet_id;
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  auto timestamp_before_write = GetCurrentTimeMicros();
  {
    auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
    PutKeyValue("key", "value");
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          return tablet_peer->log()->GetLatestEntryOpId().index == 2;
        },
        5s * kTimeMultiplier, "the write is synced"));
  }
  auto timestamp_after_write = GetCurrentTimeMicros();

  ASSERT_OK(tablet_server->Restart());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = false;

  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  ASSERT_OK(tablet_server->WaitStarted());
  log::SegmentSequence segments;
  auto log_reader = ASSERT_RESULT(tablet_peer->log()->GetLogReader());
  // The second segment is started by the first append after restart.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    RETURN_NOT_OK(log_reader->GetSegmentsSnapshot(&segments));
    return segments.size() == 2;
  }, 10s * kTimeMultiplier, "Second segment"));
  log::ReadableLogSegmentPtr segment = ASSERT_RESULT(segments.front());
  ASSERT_TRUE(segment->HasFooter());
  ASSERT_TRUE(segment->footer().has_close_timestamp_micros());
  ASSERT_TRUE(segment->footer().close_timestamp_micros() > timestamp_before_write &&
              segment->footer().close_timestamp_micros() < timestamp_after_write);
}

// Verify that when a tserver is abruptly restarted before its memtable has been
// flushed, tablet bootstrap correctly recovers all writes from the WAL.
//
// Setup:
//   1. Disable flush on shutdown so writes stay only in memtable + WAL.
//   2. Simulate abrupt restart so the WAL segment is not closed with a footer.
//   3. Insert N rows.
//   4. Restart all tservers.
//
// Expectations after bootstrap:
//   - All N rows are visible (replayed from WAL).
//   - The previously-active WAL segment has a footer reconstructed by tail-scan.
//   - Subsequent writes work normally.
TEST_F(RestartTest, BootstrapReplaysUnflushedWritesFromWAL) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = true;
  // Disable reuse-unclosed-segment so bootstrap exercises the footer-rebuild path.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reuse_unclosed_segment_threshold_bytes) = -1;

  string tablet_id;
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));

  // Verifies all expected key/value pairs are present using set membership
  // (independent of scan order, which is lexicographic in YCQL).
  auto check_all_keys_present = [this](int n) {
    auto result_kvs = GetScanResults(client::TableRange(table_));
    ASSERT_EQ(n, result_kvs.size());
    std::map<std::string, std::string> got(result_kvs.begin(), result_kvs.end());
    for (int i = 0; i < n; ++i) {
      auto k = "key_" + std::to_string(i);
      auto v = "value_" + std::to_string(i);
      auto it = got.find(k);
      ASSERT_NE(it, got.end()) << "missing key " << k;
      ASSERT_EQ(it->second, v) << "wrong value for key " << k;
    }
  };

  constexpr int kNumRows = 50;
  for (int i = 0; i < kNumRows; ++i) {
    PutKeyValue("key_" + std::to_string(i), "value_" + std::to_string(i));
  }

  ASSERT_NO_FATALS(check_all_keys_present(kNumRows));

  for (size_t i = 0; i < mini_cluster()->num_tablet_servers(); ++i) {
    ASSERT_OK(mini_cluster()->mini_tablet_server(i)->Restart());
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = false;

  for (size_t i = 0; i < mini_cluster()->num_tablet_servers(); ++i) {
    ASSERT_OK(mini_cluster()->mini_tablet_server(i)->WaitStarted());
  }

  ASSERT_NO_FATALS(check_all_keys_present(kNumRows));

  // The previously-active segment should now have a footer rebuilt by tail-scan during bootstrap.
  // With reuse_unclosed_segment_threshold_bytes = -1 and an abrupt prior shutdown, bootstrap does
  // not reuse the prior segment. A new one is started by the first append after the restart, which
  // this replica may receive after the scan.
  {
    auto* ts = mini_cluster()->mini_tablet_server(0);
    auto peer = ASSERT_RESULT(ts->server()->tablet_manager()->GetServingTablet(tablet_id));
    log::SegmentSequence segments;
    auto log_reader = ASSERT_RESULT(peer->log()->GetLogReader());
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      RETURN_NOT_OK(log_reader->GetSegmentsSnapshot(&segments));
      return segments.size() >= 2;
    }, 10s * kTimeMultiplier, "Second segment"));
    log::ReadableLogSegmentPtr first = ASSERT_RESULT(segments.front());
    ASSERT_TRUE(first->HasFooter()) << "first segment missing footer after bootstrap";
  }

  PutKeyValue("post_restart_key", "post_restart_value");
  auto result_kvs = GetScanResults(client::TableRange(table_));
  ASSERT_EQ(kNumRows + 1, result_kvs.size());
}

TEST_F(LogSyncTest, BackgroundSync) {

  // triggers log background sync threadpool
  PutKeyValue("key_0", "value_0");
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  string tablet_id;
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  CheckSampleKeysValues(0, 0);

  ASSERT_OK(tablet_server->Restart());
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));
  ASSERT_OK(tablet_server->WaitStarted());

  // shutting down tablet_peer resets the BG sync threapool token maintained in the Log.
  PutKeyValue("key_1", "value_1");
  CheckSampleKeysValues(0, 1);
  ShutdownTabletPeer(tablet_peer);
}

class PersistRetryableRequestsTest : public RestartTest {
 protected:
  void BeforeStartCluster() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 10;
  }

  size_t num_tablet_servers() override { return 1; }

  void TestRetryableWrite(bool wait_file_to_expire);

  Status RollLog(tablet::TabletPeerPtr peer) {
    // Rollover the log to persist retryable requests.
    RETURN_NOT_OK(peer->log()->AllocateSegmentAndRollOver());
    if (!FLAGS_enable_flush_retryable_requests) {
      return Status::OK();
    }
    return WaitFor([&] {
      return peer->TEST_HasBootstrapStateOnDisk();
    }, 10s, "retryable requests flushed to disk");
  }
};

// Test for scenario:
// 1. Replicated an WriteOp on tablet 1 and tserver crashed before replying to client.
// 2. Restart the tserver.
// 3. The client gets TimedOut and retry the same write.
// 4. Tablet 1 should reject the duplicate write.
void PersistRetryableRequestsTest::TestRetryableWrite(bool wait_file_to_expire) {
  auto* tablet_server = mini_cluster()->mini_tablet_server(0);
  string tablet_id;
  ASSERT_NO_FATALS(GetTablet(table_.name(), &tablet_id));
  auto tablet_peer = ASSERT_RESULT(
      tablet_server->server()->tablet_manager()->GetServingTablet(tablet_id));

  int64_t new_index = 1;
  PutKeyValue("key_1", "value_1");
  new_index++;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index ==
               new_index;
      },
      10s,
      Format("the write $0 is replicated", new_index)));

  if (wait_file_to_expire) {
    ASSERT_OK(RollLog(tablet_peer));

    // Don't flush newer versions to disk.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = false;
    PutKeyValue("key_1", "value_1");
    new_index++;
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          return VERIFY_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index ==
                 new_index;
        },
        10s,
        Format("the write $0 is replicated", new_index)));
    ASSERT_OK(RollLog(tablet_peer));

    // Sleep for enough time to make the persisted file old enough.
    // it shouldn't replay from the op id that is covered by the persisted file.
    SleepFor((FLAGS_retryable_request_timeout_secs + 1) * 1s);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          return VERIFY_RESULT(tablet_peer->GetRaftConsensus())->MinRetryableRequestOpId() ==
                 OpId::Max();
        },
        10s,
        "retryable requests get GCed"));
  }

#ifndef NDEBUG
  SyncPoint::GetInstance()->LoadDependency({
      {"AsyncRpc::Finished:SetTimedOut:1",
       "RestartTest::TestRetryableWriteAfterRestart:WaitForSetTimedOut"},
      {"RestartTest::TestRetryableWriteAfterRestart:RowUpdated",
       "AsyncRpc::Finished:SetTimedOut:2"}
  });

  SyncPoint::GetInstance()->EnableProcessing();
#endif

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = true;
  new_index++;
  // Start a new thread for writing a new row.
  std::thread th([&] {
    PutKeyValue("key_0", "value_0");
  });

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(tablet_peer->GetRaftConsensus())->GetLastCommittedOpId().index ==
               new_index;
      },
      10s,
      Format("the write $0 is replicated", new_index)));

  if (!wait_file_to_expire) {
    ASSERT_OK(RollLog(tablet_peer));
  }

  TEST_SYNC_POINT("RestartTest::TestRetryableWriteAfterRestart:WaitForSetTimedOut");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = false;

  // Write a new op to the newly allocated segment.
  PutKeyValue("key_2", "value_2");
  ASSERT_OK(ASSERT_RESULT(tablet_peer->shared_tablet())
                ->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  // Restart tserver.
  ASSERT_OK(tablet_server->Restart());
  ASSERT_OK(tablet_server->WaitStarted());

  PutKeyValue("key_0", "value_1");

  TEST_SYNC_POINT("RestartTest::TestRetryableWriteAfterRestart:RowUpdated");

  th.join();

  CheckKeyValue(/* key = */ 0, /* value = */ 1);

#ifndef NDEBUG
  SyncPoint::GetInstance()->DisableProcessing();
  SyncPoint::GetInstance()->ClearTrace();
#endif // NDEBUG
}


TEST_F(PersistRetryableRequestsTest, TestRetryableWriteAfterRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
  return TestRetryableWrite(/* wait_file_to_expire */ false);
}

TEST_F(PersistRetryableRequestsTest, TestRetryableWriteWithoutPersistence) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = false;
  return TestRetryableWrite(/* wait_file_to_expire */ false);
}

TEST_F(PersistRetryableRequestsTest, TestRetryableRequestsFileTooOld) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
  return TestRetryableWrite(/* wait_file_to_expire */ true);
}

// Tracks how much of each written file was fsynced, so WAL segments can be cut back to that size
// as a power loss would.
class PowerLossEnv : public EnvWrapper {
 public:
  PowerLossEnv() : EnvWrapper(Env::Default()) {}

  Status NewWritableFile(
      const WritableFileOptions& opts, const std::string& fname,
      std::unique_ptr<WritableFile>* result) override {
    RETURN_NOT_OK(EnvWrapper::NewWritableFile(opts, fname, result));
    Track(opts, fname, result);
    return Status::OK();
  }

  Status NewTempWritableFile(
      const WritableFileOptions& opts, const std::string& name_template,
      std::string* created_filename, std::unique_ptr<WritableFile>* result) override {
    RETURN_NOT_OK(EnvWrapper::NewTempWritableFile(opts, name_template, created_filename, result));
    Track(opts, *created_filename, result);
    return Status::OK();
  }

  // Bootstrap fsyncs an unclosed WAL segment it does not reuse through RWFile.
  Status NewRWFile(
      const RWFileOptions& opts, const std::string& fname,
      std::unique_ptr<RWFile>* result) override {
    RETURN_NOT_OK(EnvWrapper::NewRWFile(opts, fname, result));
    std::lock_guard lock(mutex_);
    auto it = files_.find(fname);
    if (it != files_.end()) {
      auto target = std::move(*result);
      *result = std::make_unique<SyncTrackingRWFile>(std::move(target), it->second);
    }
    return Status::OK();
  }

  // WAL segments are created under a temporary name and renamed once their header is synced.
  Status RenameFile(const std::string& src, const std::string& target) override {
    RETURN_NOT_OK(EnvWrapper::RenameFile(src, target));
    std::lock_guard lock(mutex_);
    auto it = files_.find(src);
    if (it != files_.end()) {
      auto state = std::move(it->second);
      files_.erase(it);
      files_[target] = std::move(state);
    }
    return Status::OK();
  }

  uint64_t UnsyncedWalBytes() {
    uint64_t result = 0;
    std::lock_guard lock(mutex_);
    for (const auto& [path, state] : files_) {
      if (IsWalSegment(path)) {
        result += state->written_size.load() - state->synced_size.load();
      }
    }
    return result;
  }

  // Truncates WAL segments to the size they had at their last fsync.
  Status DropUnsyncedWalData() {
    std::lock_guard lock(mutex_);
    for (const auto& [path, state] : files_) {
      if (!IsWalSegment(path) || !FileExists(path)) {
        continue;
      }
      auto synced_size = state->synced_size.load();
      auto size = VERIFY_RESULT(GetFileSize(path));
      if (size <= synced_size) {
        continue;
      }
      LOG(INFO) << "Truncating " << path << " from " << size << " to " << synced_size;
      RETURN_NOT_OK(STATUS_FROM_ERRNO_IF_NONZERO_RV(path, truncate(path.c_str(), synced_size)));
    }
    return Status::OK();
  }

 private:
  struct FileState {
    explicit FileState(uint64_t size) : written_size(size), synced_size(size) {}

    std::atomic<uint64_t> written_size;
    std::atomic<uint64_t> synced_size;
  };
  using FileStatePtr = std::shared_ptr<FileState>;

  class File : public WritableFileWrapper {
   public:
    File(std::unique_ptr<WritableFile> target, FileStatePtr state, bool sync_on_close)
        : WritableFileWrapper(std::move(target)), state_(std::move(state)),
          sync_on_close_(sync_on_close) {}

    Status Append(const Slice& data) override {
      RETURN_NOT_OK(WritableFileWrapper::Append(data));
      state_->written_size += data.size();
      return Status::OK();
    }

    Status AppendSlices(const Slice* slices, size_t num) override {
      RETURN_NOT_OK(WritableFileWrapper::AppendSlices(slices, num));
      for (size_t i = 0; i != num; ++i) {
        state_->written_size += slices[i].size();
      }
      return Status::OK();
    }

    Status Sync() override {
      return SyncAndTrack([this] { return WritableFileWrapper::Sync(); });
    }

    Status Fsync() override {
      return SyncAndTrack([this] { return WritableFileWrapper::Fsync(); });
    }

    Status Close() override {
      if (sync_on_close_) {
        return SyncAndTrack([this] { return WritableFileWrapper::Close(); });
      }
      return WritableFileWrapper::Close();
    }

   private:
    template <class F>
    Status SyncAndTrack(const F& sync) {
      // Only bytes appended before the sync started are guaranteed to be durable.
      auto size = state_->written_size.load();
      RETURN_NOT_OK(sync());
      UpdateAtomicMax(&state_->synced_size, size);
      return Status::OK();
    }

    const FileStatePtr state_;
    const bool sync_on_close_;
  };

  // WAL segments are written through WritableFile, so RWFile only has to track syncs.
  class SyncTrackingRWFile : public RWFile {
   public:
    SyncTrackingRWFile(std::unique_ptr<RWFile> target, FileStatePtr state)
        : target_(std::move(target)), state_(std::move(state)) {}

    Status Read(uint64_t offset, size_t length, Slice* result, uint8_t* scratch) const override {
      return target_->Read(offset, length, result, scratch);
    }

    Status Write(uint64_t offset, const Slice& data) override {
      return target_->Write(offset, data);
    }

    Status PreAllocate(uint64_t offset, size_t length) override {
      return target_->PreAllocate(offset, length);
    }

    Status PunchHole(uint64_t offset, size_t length) override {
      return target_->PunchHole(offset, length);
    }

    Status Flush(FlushMode mode, uint64_t offset, size_t length) override {
      return target_->Flush(mode, offset, length);
    }

    Status Sync() override {
      auto size = state_->written_size.load();
      RETURN_NOT_OK(target_->Sync());
      UpdateAtomicMax(&state_->synced_size, size);
      return Status::OK();
    }

    Status Close() override {
      return target_->Close();
    }

    Result<uint64_t> Size() const override {
      return target_->Size();
    }

    const std::string& filename() const override {
      return target_->filename();
    }

   private:
    const std::unique_ptr<RWFile> target_;
    const FileStatePtr state_;
  };

  void Track(
      const WritableFileOptions& opts, const std::string& fname,
      std::unique_ptr<WritableFile>* file) {
    auto state = std::make_shared<FileState>((*file)->Size());
    auto target = std::move(*file);
    *file = std::make_unique<File>(std::move(target), state, opts.sync_on_close);
    std::lock_guard lock(mutex_);
    files_[fname] = std::move(state);
  }

  static bool IsWalSegment(const std::string& path) {
    return FsManager::IsWalSegmentFileName(BaseName(path));
  }

  std::mutex mutex_;
  std::map<std::string, FileStatePtr> files_ GUARDED_BY(mutex_);
};

class PowerLossTest : public RestartTest {
 protected:
  void SetUp() override {
    auto env = std::make_unique<PowerLossEnv>();
    power_loss_env_ = env.get();
    ts_env_ = std::move(env);
    RestartTest::SetUp();
  }

  void BeforeStartCluster() override {
    // Otherwise tablets use an encrypted env that bypasses ts_env_.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_encryption_at_rest) = false;
    // Fsync WAL only when forced to: segment allocation, rollover or close.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_interval_durable_wal_write_ms) = 3600 * 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_bytes_durable_wal_write_mb) = 1024;
  }

  size_t num_tablet_servers() override { return 1; }

  using Rows = std::vector<std::pair<std::string, std::string>>;

  // Returns the written rows sorted.
  Rows WriteRows(int num_rows) {
    Rows result;
    for (int i = 0; i != num_rows; ++i) {
      result.emplace_back(Format("key_$0", i), Format("value_$0", i));
      PutKeyValue(result.back().first, result.back().second);
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  void CheckRows(const Rows& expected) {
    auto rows = GetScanResults(client::TableRange(table_));
    std::sort(rows.begin(), rows.end());
    ASSERT_EQ(rows, expected);
  }

  // Shuts down without closing the WAL, as a crash would.
  void ShutdownAbruptly(tserver::MiniTabletServer* ts) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = true;
    ts->Shutdown();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = false;
  }

  PowerLossEnv* power_loss_env_ = nullptr;
};

// Power loss drops the WAL tail that was not fsynced. A memtable flush done before the power loss
// must not have persisted operations from that tail, otherwise the flushed op id is ahead of the
// WAL and bootstrap fails with "WAL files missing".
// https://github.com/yugabyte/yugabyte-db/issues/34197
TEST_F(PowerLossTest, FlushAheadOfUnsyncedWal) {
  auto expected_rows = WriteRows(10);

  // Otherwise the WAL tail is already durable and the scenario is not exercised.
  ASSERT_GT(power_loss_env_->UnsyncedWalBytes(), 0U);

  auto* ts = mini_cluster()->mini_tablet_server(0);
  ASSERT_OK(ts->FlushTablets());

  ShutdownAbruptly(ts);
  ASSERT_OK(power_loss_env_->DropUnsyncedWalData());

  ASSERT_OK(ts->Start());
  ASSERT_NO_FATALS(CheckRows(expected_rows));
}

// A process crash leaves the WAL tail unsynced. When bootstrap does not reuse that segment, it must
// fsync it, since flushes may then persist the entries replayed from it.
TEST_F(PowerLossTest, FlushReplayedFromUnreusedSegment) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reuse_unclosed_segment_threshold_bytes) = -1;
  auto expected_rows = WriteRows(10);
  ASSERT_GT(power_loss_env_->UnsyncedWalBytes(), 0U);

  // Process crash: neither the shutdown flush nor the WAL close syncs the WAL.
  auto* ts = mini_cluster()->mini_tablet_server(0);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = true;
  ShutdownAbruptly(ts);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = false;

  ASSERT_OK(ts->Start());
  ASSERT_OK(ts->FlushTablets());

  ShutdownAbruptly(ts);
  ASSERT_OK(power_loss_env_->DropUnsyncedWalData());

  ASSERT_OK(ts->Start());
  ASSERT_NO_FATALS(CheckRows(expected_rows));
}

} // namespace integration_tests
} // namespace yb
