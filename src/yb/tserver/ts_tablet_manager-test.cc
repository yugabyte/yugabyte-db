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
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/qlexpr/index.h"
#include "yb/dockv/partition.h"
#include "yb/common/schema.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_round.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/docdb/docdb_rocksdb_util.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/bits.h"
#include "yb/gutil/sysinfo.h"

#include "yb/master/master_heartbeat.pb.h"

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/rate_limiter.h"

#include "yb/tablet/tablet-harness.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/full_compaction_manager.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_memory_manager.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

using std::string;

#define ASSERT_REPORT_HAS_UPDATED_TABLET(report, tablet_id) \
  ASSERT_NO_FATALS(AssertReportHasUpdatedTablet(report, tablet_id))

#define ASSERT_MONOTONIC_REPORT_SEQNO(report_seqno, tablet_report) \
  ASSERT_NO_FATALS(AssertMonotonicReportSeqno(report_seqno, tablet_report))

DECLARE_bool(TEST_pretend_memory_exceeded_enforce_flush);
DECLARE_bool(TEST_tserver_disable_heartbeat);
DECLARE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec);
DECLARE_string(rocksdb_compact_flush_rate_limit_sharing_mode);
DECLARE_bool(disable_auto_flags_management);
DECLARE_int32(scheduled_full_compaction_frequency_hours);
DECLARE_int32(scheduled_full_compaction_jitter_factor_percentage);
DECLARE_int32(auto_compact_memory_cleanup_interval_sec);
DECLARE_bool(allow_encryption_at_rest);
DECLARE_int32(db_block_cache_num_shard_bits);
DECLARE_int32(num_cpus);

namespace yb {
namespace tserver {

using consensus::kInvalidOpIdIndex;
using consensus::RaftConfigPB;
using consensus::ConsensusRound;
using consensus::ConsensusRoundPtr;
using docdb::RateLimiterSharingMode;
using master::ReportedTabletPB;
using master::TabletReportPB;
using master::TabletReportUpdatesPB;
using strings::Substitute;
using tablet::TabletPeer;
using gflags::FlagSaver;

static const char* const kTableId = "my-table-id";
static const char* const kTabletId = "my-tablet-id";
static const int kConsensusRunningWaitMs = 10000;
static const int kDrivesNum = 4;

class TsTabletManagerTest : public YBTest {
 public:
  TsTabletManagerTest()
    : schema_({ ColumnSchema("key", DataType::UINT32, ColumnKind::RANGE_ASC_NULL_FIRST) }) {
  }

  string GetDrivePath(int index) {
    return JoinPathSegments(test_data_root_, Substitute("drive-$0", index + 1));
  }

  void CreateMiniTabletServer() {
    auto options_result = TabletServerOptions::CreateTabletServerOptions();
    ASSERT_OK(options_result);
    std::vector<std::string> paths;
    for (int i = 0; i < kDrivesNum; ++i) {
      auto s = GetDrivePath(i);
      ASSERT_OK(env_->CreateDirs(s));
      paths.push_back(s);
    }

    // Disable AutoFlags management as we dont have a master. AutoFlags will be enabled based on
    // FLAGS_TEST_promote_all_auto_flags in test_main.cc.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_auto_flags_management) = true;

    // Disallow encryption at rest as there is no master.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_encryption_at_rest) = false;

    mini_server_ = std::make_unique<MiniTabletServer>(paths, paths, 0, *options_result, 0);
  }

  void SetUp() override {
    YBTest::SetUp();

    // Requred before tserver creation as using of `mini_server_->FailHeartbeats()`
    // does not guarantee the heartbeat events is off immediately and a couple of events
    // may happen until heartbeat's thread sees the effect of `mini_server_->FailHeartbeats()`
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) = 30 * 24;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_jitter_factor_percentage) = 33;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_memory_cleanup_interval_sec) = 3600;

    test_data_root_ = GetTestPath("TsTabletManagerTest-fsroot");
    CreateMiniTabletServer();
    ASSERT_OK(mini_server_->Start(tserver::WaitTabletsBootstrapped::kFalse));
    mini_server_->FailHeartbeats();

    config_ = mini_server_->CreateLocalConfig();

    tablet_manager_ = mini_server_->server()->tablet_manager();
    fs_manager_ = mini_server_->server()->fs_manager();
  }

  void TearDown() override {
    if (mini_server_) {
      mini_server_->Shutdown();
    }
  }

  Status CreateNewTablet(const std::string& table_id,
                         const std::string& tablet_id,
                         const Schema& schema,
                         std::shared_ptr<tablet::TabletPeer>* out_tablet_peer) {
    Schema full_schema = SchemaBuilder(schema).Build();
    auto partition = tablet::CreateDefaultPartition(full_schema);

    auto table_info = std::make_shared<tablet::TableInfo>(
        "TEST: ", tablet::Primary::kTrue, table_id, tablet_id, tablet_id,
        TableType::DEFAULT_TABLE_TYPE, full_schema, qlexpr::IndexMap(),
        boost::none /* index_info */, 0 /* schema_version */, partition.first,
        "" /* pg_table_id */, tablet::SkipTableTombstoneCheck::kFalse);
    auto tablet_peer = VERIFY_RESULT(tablet_manager_->CreateNewTablet(
        table_info, tablet_id, partition.second, config_));
    if (out_tablet_peer) {
      (*out_tablet_peer) = tablet_peer;
    }

    RETURN_NOT_OK(tablet_peer->WaitUntilConsensusRunning(
          MonoDelta::FromMilliseconds(kConsensusRunningWaitMs)));

    return VERIFY_RESULT(tablet_peer->GetConsensus())->EmulateElection();
  }

  void Reload() {
    LOG(INFO) << "Shutting down tablet manager";
    mini_server_->Shutdown();
    LOG(INFO) << "Restarting tablet manager";
    ASSERT_NO_FATAL_FAILURE(CreateMiniTabletServer());
    ASSERT_OK(mini_server_->Start());
    tablet_manager_ = mini_server_->server()->tablet_manager();
  }

  void AddTablets(size_t num, TSTabletManager::TabletPeers* peers = nullptr) {
    // Add series of tablets
    ASSERT_NE(num, 0);
    for (size_t i = 0; i < num; ++i) {
      std::shared_ptr<TabletPeer> peer;
      const auto tid = Format("tablet-$0", peers->size());
      ASSERT_OK(CreateNewTablet(kTableId, tid, schema_, &peer));
      ASSERT_EQ(tid, peer->tablet()->tablet_id());
      if (peers) {
        peers->push_back(peer);
      }
    }
  }

  Result<TSTabletManager::TabletPeers> GetPeers(
      boost::optional<size_t> expected_count = boost::none) {
    auto peers = tablet_manager_->GetTabletPeers(nullptr);
    if (expected_count.has_value()) {
      SCHECK_EQ(*expected_count, peers.size(), IllegalState, "Unexpected number of peers");
    }
    return std::move(peers);
  }

 protected:
  std::unique_ptr<MiniTabletServer> mini_server_;
  FsManager* fs_manager_;
  TSTabletManager* tablet_manager_;

  Schema schema_;
  RaftConfigPB config_;

  string test_data_root_;
};

TEST_F_EX(TsTabletManagerTest, TestDbBlockCacheNumShardBits, YBTest) {
  auto make_pair_same_kv = [](int32_t arg) {
    return std::make_pair(arg, arg);
  };

  auto random_pair_same_kv = [](int32_t range_min, int32_t range_max) {
    const auto rand_num = RandomUniformInt<int32_t>(range_min, range_max);
    return std::make_pair(rand_num, rand_num);
  };

  auto random_pair_log_v = [](int32_t range_min, int32_t range_max) {
    const auto rand_num = RandomUniformInt<int32_t>(range_min, range_max);
    return std::make_pair(rand_num, Bits::Log2Ceiling(rand_num));
  };

  const std::vector<std::pair<int32_t, int32_t>> kNumShardBitsByFlag {
    { 0, 0 }, { 1, 1 }, random_pair_same_kv(2, rocksdb::kSharedLRUCacheMaxNumShardBits - 2),
    make_pair_same_kv(rocksdb::kSharedLRUCacheMaxNumShardBits - 1),
    make_pair_same_kv(rocksdb::kSharedLRUCacheMaxNumShardBits),
    { rocksdb::kSharedLRUCacheMaxNumShardBits + 1, rocksdb::kSharedLRUCacheMaxNumShardBits },
    { RandomUniformInt<int32_t>(rocksdb::kSharedLRUCacheMaxNumShardBits + 2,
                                std::numeric_limits<int32_t>::max()),
      rocksdb::kSharedLRUCacheMaxNumShardBits }
  };

  for (const auto& num_shard_bits_by_flag_info : kNumShardBitsByFlag) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_cache_num_shard_bits) =
        num_shard_bits_by_flag_info.first;
    const auto num_shard_bits = yb::tserver::GetDbBlockCacheNumShardBits();
    ASSERT_EQ(num_shard_bits, num_shard_bits_by_flag_info.second);
  }

  const std::vector<std::pair<int32_t, int32_t>> kNumShardBitsByCpuNum {
    { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 }, { RandomUniformInt<int32_t>(5, 15), 4 }, { 16, 4 },
    { 17, 5 }, { 18, 5 }, { RandomUniformInt<int32_t>(19, 30), 5 }, { 31, 5 }, { 32, 5 },
    { 33, 6 }, { RandomUniformInt<int32_t>(34, 62), 6 }, { 63, 6 }, { 64, 6 }, { 65, 7 },
    { RandomUniformInt<int32_t>(66, 126), 7 }, { 127, 7 }, { 128, 7 }, { 129, 8 },
    { RandomUniformInt<int32_t>(130, 254), 8 }, { 255, 8 }, { 256, 8 }, { 257, 9 },
    random_pair_log_v(258, std::pow(2, rocksdb::kSharedLRUCacheMaxNumShardBits) - 1),
    { RandomUniformInt<int32_t>(std::pow(2, rocksdb::kSharedLRUCacheMaxNumShardBits) + 1,
                                std::pow(2, 30)),
      rocksdb::kSharedLRUCacheMaxNumShardBits }
  };

  const std::vector<int32_t> kNegativeFlagValues {
    -1, RandomUniformInt(std::numeric_limits<int32_t>::min(), -2)
  };
  for (const auto flag_value : kNegativeFlagValues) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_cache_num_shard_bits) = flag_value;
    for (const auto& num_shard_bits_by_cpu_num_info : kNumShardBitsByCpuNum) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = num_shard_bits_by_cpu_num_info.first;
      const auto num_shard_bits = yb::tserver::GetDbBlockCacheNumShardBits();
      ASSERT_EQ(num_shard_bits, num_shard_bits_by_cpu_num_info.second);
    }
  }
}

TEST_F(TsTabletManagerTest, TestCreateTablet) {
  // Create a new tablet.
  std::shared_ptr<TabletPeer> peer;
  ASSERT_OK(CreateNewTablet(kTableId, kTabletId, schema_, &peer));
  ASSERT_EQ(kTabletId, peer->tablet()->tablet_id());
  peer.reset();

  // Re-load the tablet manager from the filesystem.
  LOG(INFO) << "Shutting down tablet manager";
  mini_server_->Shutdown();
  LOG(INFO) << "Restarting tablet manager";
  CreateMiniTabletServer();
  ASSERT_OK(mini_server_->Start());
  tablet_manager_ = mini_server_->server()->tablet_manager();

  // Ensure that the tablet got re-loaded and re-opened off disk.
  peer = ASSERT_RESULT(tablet_manager_->GetTablet(kTabletId));
  ASSERT_EQ(kTabletId, peer->tablet()->tablet_id());
}

TEST_F(TsTabletManagerTest, TestTombstonedTabletsAreUnregistered) {
  const std::string kTableId = "my-table-id";
  const std::string kTabletId1 = "my-tablet-id-1";
  const std::string kTabletId2 = "my-tablet-id-2";

  auto shutdown_tserver_and_reload_tablet_manager = [this]() {
    // Re-load the tablet manager from the filesystem.
    LOG(INFO) << "Shutting down tablet manager";
    mini_server_->Shutdown();
    LOG(INFO) << "Restarting tablet manager";
    CreateMiniTabletServer();
    ASSERT_OK(mini_server_->Start());
    tablet_manager_ = mini_server_->server()->tablet_manager();
  };

  auto count_tablet_in_assignment_map =
      [&kTableId](const TSTabletManager::TableDiskAssignmentMap* table_assignment_map,
                  const std::string& tablet_id) {
        auto table_assignment_iter = table_assignment_map->find(kTableId);
        EXPECT_NE(table_assignment_iter, table_assignment_map->end());
        // the number of data directories for this table should be non-empty.
        EXPECT_GT(table_assignment_iter->second.size(), 0);
        int tablet_count = 0;
        for (const auto& tablet_assignment_iter : table_assignment_iter->second) {
          // directory_map maps a directory name to a set of tablet ids.
          for (const TabletId& tablet : tablet_assignment_iter.second) {
            if (tablet_id == tablet) {
              tablet_count++;
            }
          }
        }
        return tablet_count;
      };

  auto assert_tablet_assignment_count =
      [this, &count_tablet_in_assignment_map](const std::string& tablet_id, int count) {
    ASSERT_EQ(
        count_tablet_in_assignment_map(&tablet_manager_->table_data_assignment_map_, tablet_id),
        count);
    ASSERT_EQ(
        count_tablet_in_assignment_map(&tablet_manager_->table_wal_assignment_map_, tablet_id),
        count);
  };

  // Create a new tablet.
  std::shared_ptr<TabletPeer> peer;
  ASSERT_OK(CreateNewTablet(kTableId, kTabletId1, schema_, &peer));
  ASSERT_EQ(kTabletId1, peer->tablet()->tablet_id());
  peer.reset();
  ASSERT_OK(CreateNewTablet(kTableId, kTabletId2, schema_, &peer));
  ASSERT_EQ(kTabletId2, peer->tablet()->tablet_id());

  assert_tablet_assignment_count(kTabletId1, 1);
  assert_tablet_assignment_count(kTabletId2, 1);

  shutdown_tserver_and_reload_tablet_manager();

  assert_tablet_assignment_count(kTabletId1, 1);
  assert_tablet_assignment_count(kTabletId2, 1);

  boost::optional<int64_t> cas_config_opid_index_less_or_equal;
  boost::optional<TabletServerErrorPB::Code> error_code;
  ASSERT_OK(tablet_manager_->DeleteTablet(kTabletId1,
      tablet::TABLET_DATA_TOMBSTONED,
      tablet::ShouldAbortActiveTransactions::kFalse,
      cas_config_opid_index_less_or_equal,
      false /* hide_only */,
      false /* keep_data */,
      &error_code));

  assert_tablet_assignment_count(kTabletId1, 0);
  assert_tablet_assignment_count(kTabletId2, 1);

  shutdown_tserver_and_reload_tablet_manager();

  assert_tablet_assignment_count(kTabletId1, 0);
  assert_tablet_assignment_count(kTabletId2, 1);

  ASSERT_OK(tablet_manager_->DeleteTablet(kTabletId1,
                                          tablet::TABLET_DATA_DELETED,
                                          tablet::ShouldAbortActiveTransactions::kFalse,
                                          cas_config_opid_index_less_or_equal,
                                          false /* hide_only */,
                                          false /* keep_data */,
                                          &error_code));

  assert_tablet_assignment_count(kTabletId1, 0);
  assert_tablet_assignment_count(kTabletId2, 1);

  shutdown_tserver_and_reload_tablet_manager();

  assert_tablet_assignment_count(kTabletId1, 0);
  assert_tablet_assignment_count(kTabletId2, 1);
}

TEST_F(TsTabletManagerTest, TestProperBackgroundFlushOnStartup) {
  FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pretend_memory_exceeded_enforce_flush) = true;

  const int kNumTablets = 2;
  const int kNumRestarts = 3;

  std::vector<TabletId> tablet_ids;
  std::vector<ConsensusRoundPtr> consensus_rounds;

  for (int i = 0; i < kNumTablets; ++i) {
    std::shared_ptr<TabletPeer> peer;
    const auto tablet_id = Format("my-tablet-$0", i + 1);
    tablet_ids.emplace_back(tablet_id);
    ASSERT_OK(CreateNewTablet(kTableId, tablet_id, schema_, &peer));
    ASSERT_EQ(tablet_id, peer->tablet()->tablet_id());

    auto replicate_ptr = rpc::MakeSharedMessage<consensus::LWReplicateMsg>();
    replicate_ptr->set_op_type(consensus::NO_OP);
    replicate_ptr->set_hybrid_time(peer->clock().Now().ToUint64());
    auto consensus = ASSERT_RESULT(peer->GetConsensus());
    ConsensusRoundPtr round(new ConsensusRound(consensus.get(), std::move(replicate_ptr)));
    consensus_rounds.emplace_back(round);
    round->BindToTerm(ASSERT_RESULT(peer->GetRaftConsensus())->TEST_LeaderTerm());
    round->SetCallback(consensus::MakeNonTrackedRoundCallback(round.get(), [](const Status&){}));
    ASSERT_OK(ASSERT_RESULT(peer->GetConsensus())->TEST_Replicate(round));
  }

  for (int i = 0; i < kNumRestarts; ++i) {
    LOG(INFO) << "Shutting down tablet manager";
    mini_server_->Shutdown();
    LOG(INFO) << "Restarting tablet manager";
    CreateMiniTabletServer();
    ASSERT_OK(mini_server_->Start(tserver::WaitTabletsBootstrapped::kFalse));
    auto* tablet_manager = mini_server_->server()->tablet_manager();
    ASSERT_NE(nullptr, tablet_manager);
    tablet_manager->tablet_memory_manager()->FlushTabletIfLimitExceeded();
    ASSERT_OK(mini_server_->WaitStarted());
    for (auto& tablet_id : tablet_ids) {
      auto peer = ASSERT_RESULT(tablet_manager->GetTablet(tablet_id));
      ASSERT_EQ(tablet_id, peer->tablet()->tablet_id());
    }
  }
}

static void AssertMonotonicReportSeqno(int32_t* report_seqno,
                                       const TabletReportPB& report) {
  ASSERT_LT(*report_seqno, report.sequence_number());
  *report_seqno = report.sequence_number();
}

static void AssertReportHasUpdatedTablet(const TabletReportPB& report,
                                         const string& tablet_id) {
  ASSERT_GE(report.updated_tablets_size(), 0);
  bool found_tablet = false;
  for (ReportedTabletPB reported_tablet : report.updated_tablets()) {
    if (reported_tablet.tablet_id() == tablet_id) {
      found_tablet = true;
      ASSERT_TRUE(reported_tablet.has_committed_consensus_state());
      ASSERT_TRUE(reported_tablet.committed_consensus_state().has_current_term())
          << reported_tablet.ShortDebugString();
      ASSERT_TRUE(reported_tablet.committed_consensus_state().has_leader_uuid())
          << reported_tablet.ShortDebugString();
      ASSERT_TRUE(reported_tablet.committed_consensus_state().has_config());
      const RaftConfigPB& committed_config = reported_tablet.committed_consensus_state().config();
      ASSERT_EQ(kInvalidOpIdIndex, committed_config.opid_index());
      ASSERT_EQ(1, committed_config.peers_size());
      ASSERT_TRUE(committed_config.peers(0).has_permanent_uuid())
          << reported_tablet.ShortDebugString();
      ASSERT_EQ(committed_config.peers(0).permanent_uuid(),
                reported_tablet.committed_consensus_state().leader_uuid())
          << reported_tablet.ShortDebugString();
    }
  }
  ASSERT_TRUE(found_tablet);
}

static void CopyReportToUpdates(const TabletReportPB& req, TabletReportUpdatesPB* resp) {
  resp->Clear();
  for (const auto & tablet : req.updated_tablets()) {
    auto new_tablet = resp->add_tablets();
    new_tablet->set_tablet_id(tablet.tablet_id());
  }
}

TEST_F(TsTabletManagerTest, TestTabletReports) {
  TabletReportPB report;
  TabletReportUpdatesPB updates;
  int32_t seqno = -1;

  // Generate a tablet report before any tablets are loaded. Should be empty.
  tablet_manager_->StartFullTabletReport(&report);
  ASSERT_EQ(0, report.updated_tablets().size());
  ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
  CopyReportToUpdates(report, &updates);
  tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);

  // Another report should now be incremental, but with no changes.
  tablet_manager_->GenerateTabletReport(&report);
  ASSERT_EQ(0, report.updated_tablets().size());
  ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
  CopyReportToUpdates(report, &updates);
  tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);

  // Create a tablet and do another incremental report - should include the tablet.
  ASSERT_OK(CreateNewTablet(kTableId, "tablet-1", schema_, nullptr));
  int updated_tablets = 0;
  while (updated_tablets != 1) {
    tablet_manager_->GenerateTabletReport(&report);
    updated_tablets = report.updated_tablets().size();
    ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
  }

  ASSERT_REPORT_HAS_UPDATED_TABLET(report, "tablet-1");

  // If we don't acknowledge the report, and ask for another incremental report,
  // it should include the tablet again.
  tablet_manager_->GenerateTabletReport(&report);
  ASSERT_EQ(1, report.updated_tablets().size());
  ASSERT_REPORT_HAS_UPDATED_TABLET(report, "tablet-1");
  ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);

  // Now acknowledge the last report, and further incrementals should be empty.
  CopyReportToUpdates(report, &updates);
  tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);
  tablet_manager_->GenerateTabletReport(&report);
  ASSERT_EQ(0, report.updated_tablets().size());
  ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
  CopyReportToUpdates(report, &updates);
  tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);

  // Create a second tablet, and ensure the incremental report shows it.
  ASSERT_OK(CreateNewTablet(kTableId, "tablet-2", schema_, nullptr));

  // Wait up to 10 seconds to get a tablet report from tablet-2.
  // TabletPeer does not mark tablets dirty until after it commits the
  // initial configuration change, so there is also a window for tablet-1 to
  // have been marked dirty since the last report.
  MonoDelta timeout(MonoDelta::FromSeconds(10));
  MonoTime start(MonoTime::Now());
  report.Clear();
  while (true) {
    bool found_tablet_2 = false;
    tablet_manager_->GenerateTabletReport(&report);
    ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report) << report.ShortDebugString();
    for (const ReportedTabletPB& reported_tablet : report.updated_tablets()) {
      if (reported_tablet.tablet_id() == "tablet-2") {
        found_tablet_2  = true;
        break;
      }
    }
    if (found_tablet_2) break;
    MonoDelta elapsed(MonoTime::Now().GetDeltaSince(start));
    ASSERT_TRUE(elapsed.LessThan(timeout)) << "Waited too long for tablet-2 to be marked dirty: "
                                           << elapsed.ToString() << ". "
                                           << "Latest report: " << report.ShortDebugString();
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  CopyReportToUpdates(report, &updates);
  tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);

  // Asking for a full tablet report should re-report both tablets
  tablet_manager_->StartFullTabletReport(&report);
  ASSERT_EQ(2, report.updated_tablets().size());
  ASSERT_REPORT_HAS_UPDATED_TABLET(report, "tablet-1");
  ASSERT_REPORT_HAS_UPDATED_TABLET(report, "tablet-2");
  ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
}

TEST_F(TsTabletManagerTest, TestTabletReportLimit) {
  TabletReportPB report;
  TabletReportUpdatesPB updates;
  int32_t seqno = -1;

  // Generate a tablet report before any tablets are loaded. Should be empty.
  tablet_manager_->StartFullTabletReport(&report);
  ASSERT_EQ(0, report.updated_tablets().size());
  ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
  CopyReportToUpdates(report, &updates);
  tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);

  // Another report should now be incremental, but with no changes.
  tablet_manager_->GenerateTabletReport(&report);
  ASSERT_EQ(0, report.updated_tablets().size());
  ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
  CopyReportToUpdates(report, &updates);
  tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);

  // Set a report limit and create a set of tablets clearly over that limit.
  const int32_t limit = 10, total_tablets = 25;
  tablet_manager_->SetReportLimit(limit);
  std::set<std::string> tablet_ids, tablet_ids_full;
  for (int i = 0; i < total_tablets; ++i) {
    auto id = "tablet-" + std::to_string(i);
    ASSERT_OK(CreateNewTablet(kTableId, id, schema_, nullptr));
    tablet_ids.insert(id);
    tablet_ids_full.insert(id);
    LOG(INFO) << "Adding " << id;
  }

  // Ensure that incremental report requests returns all in batches.
  for (int n = limit, left = total_tablets; left > 0; left -= n, n = std::min(limit, left)) {
    tablet_manager_->GenerateTabletReport(&report);
    ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
    ASSERT_EQ(n, report.updated_tablets().size());
    for (auto& t : report.updated_tablets()) {
      LOG(INFO) << "Erasing " << t.tablet_id();
      ASSERT_EQ(1, tablet_ids.erase(t.tablet_id()));
    }
    CopyReportToUpdates(report, &updates);
    tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);
  }

  // Generate a Full Report and ensure that the same batching occurs.
  tablet_manager_->StartFullTabletReport(&report);
  for (int n = limit, left = total_tablets; left > 0; left -= n, n = std::min(limit, left)) {
    ASSERT_MONOTONIC_REPORT_SEQNO(&seqno, report);
    ASSERT_EQ(n, report.updated_tablets().size());
    for (auto& t : report.updated_tablets()) {
      ASSERT_EQ(1, tablet_ids_full.erase(t.tablet_id()));
    }
    CopyReportToUpdates(report, &updates);
    tablet_manager_->MarkTabletReportAcknowledged(seqno, updates);
    tablet_manager_->GenerateTabletReport(&report);
  }
  ASSERT_EQ(0, report.updated_tablets().size()); // Last incremental report is empty.
}

namespace {

void SetRateLimiterSharingMode(RateLimiterSharingMode mode) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_sharing_mode) = ToString(mode);
}

Result<size_t> CountUniqueLimiters(const TSTabletManager::TabletPeers& peers,
                                   const size_t start_idx = 0) {
  SCHECK_LT(start_idx, peers.size(), IllegalState,
            "Start index must be less than number of peers");
  std::unordered_set<rocksdb::RateLimiter*> unique;
  for (size_t i = start_idx; i < peers.size(); ++i) {
    auto db = peers[i]->tablet()->regular_db();
    SCHECK_NOTNULL(db);
    auto rl = db->GetDBOptions().rate_limiter.get();
    if (rl) {
      unique.insert(rl);
    }
  }
  return unique.size();
}

} // namespace

TEST_F(TsTabletManagerTest, RateLimiterSharing) {
  // The test checks rocksdb::RateLimiter is correctly shared between RocksDB instances
  // depending on the flags `FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec` and
  // `FLAGS_rocksdb_compact_flush_rate_limit_sharing_mode`, inlcuding possible effect
  // of changing flags on-the-fly (emulating forced changed)

  // No tablets exist, reset flags and reload
  size_t peers_num = 0;
  constexpr auto kBPS = 128_MB;
  SetRateLimiterSharingMode(RateLimiterSharingMode::NONE);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = kBPS;
  ASSERT_NO_FATAL_FAILURE(Reload());
  TSTabletManager::TabletPeers peers = ASSERT_RESULT(GetPeers(peers_num));

  // `NONE`: add tablets and make sure they have unique limiters
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(peers_num + 2, ASSERT_RESULT(CountUniqueLimiters(peers)));
  peers_num = peers.size();

  // `NONE`: emulating forced change for bps flag: make sure new unique limiters are created
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = kBPS / 2;
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(peers_num + 2, ASSERT_RESULT(CountUniqueLimiters(peers)));
  peers_num = peers.size();

  // `NONE`: emulating forced reset for bps flag: make sure new tablets are added with no limiters
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = 0;
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(0, ASSERT_RESULT(CountUniqueLimiters(peers, peers_num)));
  peers_num = peers.size();

  // `NONE` + no bps: reload the cluster with bps flag unset to make sure no limiters are created
  ASSERT_NO_FATAL_FAILURE(Reload());
  peers = ASSERT_RESULT(GetPeers(peers_num));
  ASSERT_EQ(0, ASSERT_RESULT(CountUniqueLimiters(peers)));

  // `NONE` + no bps: add tablets and make sure limiters are not created
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(0, ASSERT_RESULT(CountUniqueLimiters(peers)));
  peers_num = peers.size();

  // `NONE`: reload the cluster with bps flag set and make sure all limiter are unique
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = kBPS;
  ASSERT_NO_FATAL_FAILURE(Reload());
  peers = ASSERT_RESULT(GetPeers(peers_num));
  ASSERT_EQ(peers_num, ASSERT_RESULT(CountUniqueLimiters(peers)));

  // `NONE`: emulating forced change for mode flag: should act as if `NONE` is still set
  SetRateLimiterSharingMode(RateLimiterSharingMode::TSERVER);
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(peers_num + 2, ASSERT_RESULT(CountUniqueLimiters(peers)));
  peers_num = peers.size();

  // `TSERVER`: reload the cluster to apply `TSERVER` sharing mode
  // and make sure all tablets share the same rate limiter
  ASSERT_NO_FATAL_FAILURE(Reload());
  peers = ASSERT_RESULT(GetPeers(peers_num));
  ASSERT_EQ(1, ASSERT_RESULT(CountUniqueLimiters(peers)));

  // `TSERVER`: emulating forced change for bps flag: make sure this has no effect on sharing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = kBPS / 2;
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(1, ASSERT_RESULT(CountUniqueLimiters(peers)));
  peers_num = peers.size();

  // `TSERVER`: emulating forced reset for bps flag: make sure this has no effect on sharing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = 0;
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(1, ASSERT_RESULT(CountUniqueLimiters(peers)));
  peers_num = peers.size();

  // `TSERVER`: emulating forced change for mode flag:
  // should act as if `TSERVER` is still set
  SetRateLimiterSharingMode(RateLimiterSharingMode::NONE);
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(1, ASSERT_RESULT(CountUniqueLimiters(peers)));
  peers_num = peers.size();

  // `TSERVER` + no bps: reload the cluster
  //  with bps flag unset to make sure no limiters are created
  SetRateLimiterSharingMode(RateLimiterSharingMode::TSERVER);
  ASSERT_NO_FATAL_FAILURE(Reload());
  peers = ASSERT_RESULT(GetPeers(peers_num));
  ASSERT_EQ(0, ASSERT_RESULT(CountUniqueLimiters(peers)));

  // `TSERVER` + no bps: add tablets and make sure no limiters are created
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(0, ASSERT_RESULT(CountUniqueLimiters(peers)));
  peers_num = peers.size();

  // `TSERVER` + no bps: emulating forced change for both flags:
  // should act as a `NONE` is applied with some bps set
  SetRateLimiterSharingMode(RateLimiterSharingMode::NONE);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = kBPS;
  ASSERT_NO_FATAL_FAILURE(AddTablets(2, &peers));
  ASSERT_EQ(2, ASSERT_RESULT(CountUniqueLimiters(peers, peers_num)));
  peers_num = peers.size();
}

TEST_F(TsTabletManagerTest, DataAndWalFilesLocations) {
  std::string wal;
  std::string data;
  auto drive_path_len = GetDrivePath(0).size();
  for (int i = 0; i < kDrivesNum; ++i) {
    tablet_manager_->GetAndRegisterDataAndWalDir(fs_manager_,
                                                 kTableId,
                                                 Substitute("tablet-$0", i + 1),
                                                 &data,
                                                 &wal);
    ASSERT_EQ(data.substr(0, drive_path_len), wal.substr(0, drive_path_len));
  }
}

TEST_F(TsTabletManagerTest, EvenDriveSelection) {
  std::string wal;
  std::string data;
  auto drive_path_len = GetDrivePath(0).size();
  constexpr size_t kNumTables = 1000;
  constexpr size_t kNumTablets = kDrivesNum / 2;
  std::unordered_map<std::string, size_t> num_tablets_assigned_to_drive;
  for (size_t i = 0; i < kNumTables; ++i) {
    std::string prev_data_drive;
    for (size_t j = 0; j < kNumTablets; ++j) {
      tablet_manager_->GetAndRegisterDataAndWalDir(fs_manager_,
                                                  Substitute("table-$0", i+ 1),
                                                  Substitute("tablet-$0", j + 1),
                                                  &data,
                                                  &wal);
      const auto chosen_data_drive = data.substr(0, drive_path_len);
      const auto chosen_wal_drive = data.substr(0, drive_path_len);
      ASSERT_EQ(chosen_data_drive, chosen_wal_drive);
      ASSERT_NE(chosen_data_drive, prev_data_drive);
      num_tablets_assigned_to_drive[chosen_wal_drive] += 1;
      prev_data_drive = chosen_data_drive;
    }
  }
  LOG(INFO) << "Data drive distribution " << yb::ToString(num_tablets_assigned_to_drive);
  auto comparator = [](std::pair<std::string, size_t> a, std::pair<std::string, size_t> b) -> bool {
    return a.second > b.second;
  };
  size_t max_count =
      std::max_element(
          num_tablets_assigned_to_drive.begin(), num_tablets_assigned_to_drive.end(), comparator)
          ->second;
  ASSERT_LE(max_count, ceil(1.0 * kNumTables * kNumTablets / kDrivesNum));
  size_t min_count =
      std::min_element(
          num_tablets_assigned_to_drive.begin(), num_tablets_assigned_to_drive.end(), comparator)
          ->second;
  ASSERT_GE(min_count, floor(1.0 * kNumTables * kNumTablets / kDrivesNum));
  ASSERT_LE(max_count - min_count, 1);
}

namespace {
  const HybridTime kNoLastCompact = HybridTime(tablet::kNoLastFullCompactionTime);
  // An arbitrary realistic time.
  const HybridTime kTimeRecent = HybridTime(6820217704657506304U);
} // namespace

// Tests the application of jitter to determine the next compaction time.
TEST_F(TsTabletManagerTest, FullCompactionCalculateNextCompaction) {
  struct JitterToTest {
    TabletId tablet_id;
    // Frequency with which full compactions should be scheduled.
    MonoDelta compaction_frequency;
    // Percentage of compaction frequency to be considered for max jitter.
    int32_t jitter_factor_percentage;
    // The last time the tablet was fully compacted. 0 indicates no previous compaction.
    HybridTime last_compact_time;
    // The expected max jitter delta, determined by
    // compaction_frequency * jitter_factor_percentage / 100.
    MonoDelta expected_max_jitter;
  };

  // Recent compaction time is several minutes before now.
  const auto kRecentCompactionTime = kTimeRecent;
  const auto now = kRecentCompactionTime.AddSeconds(1000);

  const MonoDelta kStandardFrequency = MonoDelta::FromDays(30);
  const int32_t kStandardJitterFactor = 33;
  const MonoDelta kStandardMaxJitter = kStandardFrequency * kStandardJitterFactor / 100;
  auto compaction_manager = tablet_manager_->full_compaction_manager();

  std::vector<JitterToTest> jitter_to_test = {
    // 0) Standard compaction frequency and jitter factor, with no last compaction time.
    {kTabletId, kStandardFrequency, kStandardJitterFactor, kNoLastCompact,
        kStandardMaxJitter /* expected_max_jitter */},

    // 1) Standard compaction frequency and jitter factor, with no last compaction time
    //    (same tablet ID, same expected output as #0).
    {kTabletId, kStandardFrequency, kStandardJitterFactor, kNoLastCompact,
        kStandardMaxJitter /* expected_max_jitter */},

    // 2) Standard compaction frequency and jitter factor with no last compaction time,
    //    using a different tablet id (different expected output as #0).
    {TabletId("another-tablet-id"), kStandardFrequency, kStandardJitterFactor, kNoLastCompact,
        kStandardMaxJitter /* expected_max_jitter */},

    // 3) Standard compaction frequency and jitter factor, with a recent compaction
    //    (different expected output as #0).
    {kTabletId, kStandardFrequency, kStandardJitterFactor, kRecentCompactionTime,
        kStandardMaxJitter /* expected_max_jitter */},

    // 4) Standard compaction frequency and jitter factor, with a recent compaction
    //    (different tablet id, different expected output as #3).
    {TabletId("another-tablet-id"), kStandardFrequency, kStandardJitterFactor,
        kRecentCompactionTime, kStandardMaxJitter /* expected_max_jitter */},

    // 5) Invalid jitter factor, will default to kDefaultJitterFactorPercentage
    //    (same expected output as #3).
    {kTabletId, kStandardFrequency, -1, kRecentCompactionTime,
        kStandardMaxJitter /* expected_max_jitter */},

    // 6) Invalid jitter factor, will default to kDefaultJitterFactorPercentage
    //    (same expected output as #3 and #5).
    {kTabletId, kStandardFrequency, 200, kRecentCompactionTime,
        kStandardMaxJitter /* expected_max_jitter */},

    // 7) Longer compaction frequency with recent compaction time and standard jitter.
    {kTabletId, kStandardFrequency * 3, kStandardJitterFactor, kRecentCompactionTime,
        kStandardMaxJitter * 3 /* expected_max_jitter */},

    // 8) Standard compaction frequency with recent compaction time and no jitter
    //    (expected jitter of 0).
    {kTabletId, kStandardFrequency, 0, kRecentCompactionTime,
        MonoDelta::FromNanoseconds(0) /* expected_max_jitter */},

    // 9) Standard compaction frequency with jitter factor of 10%.
    {kTabletId, kStandardFrequency, 10, kRecentCompactionTime,
        kStandardFrequency / 10 /* expected_max_jitter */},

    // 10) Standard compaction frequency with jitter factor of 100%.
    //     (expected jitter should be exactly 10X the expected jitter of #9).
    {kTabletId, kStandardFrequency, 100, kRecentCompactionTime,
        kStandardFrequency /* expected_max_jitter */},

    // 11) Standard compaction frequency with jitter factor of 100% and no previous
    //     full compaction time.
    {kTabletId, kStandardFrequency, 100, kNoLastCompact,
        kStandardFrequency /* expected_max_jitter */},
  };

  std::vector<MonoDelta> jitter_results;

  int i = 0;
  for (auto jtt : jitter_to_test) {
    LOG(INFO) << "Calculating next compaction time for scenario " << i++;
    compaction_manager->ResetFrequencyAndJitterIfNeeded(
        jtt.compaction_frequency, jtt.jitter_factor_percentage);
    auto jitter =
        compaction_manager->CalculateJitter(jtt.tablet_id, jtt.last_compact_time.ToUint64());
    auto next_compact_time =
        compaction_manager->CalculateNextCompactTime(
            jtt.tablet_id, now, jtt.last_compact_time, jitter);

    ASSERT_EQ(jtt.expected_max_jitter, compaction_manager->max_jitter());
    ASSERT_GE(jitter, MonoDelta::FromNanoseconds(0));
    ASSERT_LE(jitter, jtt.expected_max_jitter);
    jitter_results.push_back(jitter);

    // Expected time of next compaction based on the current time, previous compaction
    // time compaction, compaction frequency, and jitter. If the previous compaction
    // time is 0, uses the current time + jitter.
    HybridTime expected_next_compact_time = jtt.last_compact_time.is_special() ?
        now.AddDelta(jitter) :
        jtt.last_compact_time.AddDelta(jtt.compaction_frequency - jitter);
    ASSERT_EQ(next_compact_time, expected_next_compact_time);
  }

  // Check jitter value special cases.
  // Jitter for tests 0 and 1 should match.
  ASSERT_EQ(jitter_results[0], jitter_results[1]);
  // Jitter for tests 0 and 2 should NOT match.
  ASSERT_NE(jitter_results[0], jitter_results[2]);
  // Jitter for tests 0 and 3 should NOT match.
  ASSERT_NE(jitter_results[0], jitter_results[3]);
  // Jitter for tests 3 and 4 should NOT match.
  ASSERT_NE(jitter_results[3], jitter_results[4]);
  // Jitter for tests 3 and 5 should match.
  ASSERT_EQ(jitter_results[3], jitter_results[5]);
  // Jitter for tests 3 and 6 should match.
  ASSERT_EQ(jitter_results[3], jitter_results[6]);
  // Jitter for test 8 should be 0.
  ASSERT_EQ(jitter_results[8], MonoDelta::FromNanoseconds(0));
  // Jitter for test 10 should be 10x jitter of test 9.
  ASSERT_EQ(jitter_results[10], jitter_results[9] * 10);
}

// Tests that scheduled compaction times are roughly evenly spread based on jitter factor.
TEST_F(TsTabletManagerTest, CompactionsEvenlySpreadByJitter) {
  const auto compaction_frequency = MonoDelta::FromDays(10);
  const int jitter_factor = 10;
  const auto now = HybridTime(kTimeRecent);
  // Use an invalid last compact time to force the compaction near now.
  const auto last_compact_time = kNoLastCompact;

  auto compaction_manager = tablet_manager_->full_compaction_manager();
  compaction_manager->ResetFrequencyAndJitterIfNeeded(
      compaction_frequency, jitter_factor);

  const auto max_jitter = compaction_manager->max_jitter();
  const auto max_compact_time = now.AddDelta(max_jitter);
  const int64_t max_compact_to_now = max_compact_time.ToUint64() - now.ToUint64();
  const int num_times_to_check = 100000;
  // Number of cross sections to divide possible outcomes into, in order to determine
  // normal distribution (e.g. 10 cross sections divides into first 10%, second 10%, etc).
  const int num_cross_sections = 10;

  std::unordered_map<uint64_t, int> all_times;
  std::unordered_map<int, int> time_cross_sections;

  // Use the same last compaction time (0), but a different tablet id each iteration.
  for (int i = 0; i < num_times_to_check; i++) {
    std::string tablet_id = kTabletId + std::to_string(i);
    auto jitter = compaction_manager->CalculateJitter(tablet_id, last_compact_time.ToUint64());
    auto time = compaction_manager->CalculateNextCompactTime(
        tablet_id, now, last_compact_time, jitter);
    ASSERT_GE(time, now);
    ASSERT_LT(time, max_compact_time);

    auto time_from_now = time.ToUint64() - now.ToUint64();
    int cross_section =
        static_cast<int>(time_from_now * num_cross_sections / max_compact_to_now);
    time_cross_sections[cross_section]++;
    all_times[time.ToUint64()]++;
  }

  // Check that majority of times are unique.
  ASSERT_GE(all_times.size(), num_times_to_check * 0.95);

  // We expect a roughly equal amount of results for each time cross section,
  // +/- 5% for varience.
  const auto expected_minus_five_percent = num_times_to_check / num_cross_sections * 0.95;
  const auto expected_plus_five_percent = num_times_to_check / num_cross_sections * 1.05;
  for (auto& it : time_cross_sections) {
    ASSERT_GE(it.second, expected_minus_five_percent);
    ASSERT_LE(it.second, expected_plus_five_percent);
  }
}

// Tests that the cleanup function works as expected.
TEST_F(TsTabletManagerTest, FullCompactionManagerCleanup) {
  const std::string kTableId = "my-table-id";
  const std::string kTabletId1 = "my-tablet-id-1";
  const std::string kTabletId2 = "my-tablet-id-2";
  const std::string kTabletId3 = "my-tablet-id-3";
  auto compaction_manager = tablet_manager_->full_compaction_manager();

  // Create 3 new tablets.
  std::shared_ptr<TabletPeer> peer;
  ASSERT_OK(CreateNewTablet(kTableId, kTabletId1, schema_, &peer));
  peer.reset();
  ASSERT_OK(CreateNewTablet(kTableId, kTabletId2, schema_, &peer));
  peer.reset();
  ASSERT_OK(CreateNewTablet(kTableId, kTabletId3, schema_, &peer));
  compaction_manager->ScheduleFullCompactions();
  ASSERT_TRUE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId1));
  ASSERT_TRUE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId2));
  ASSERT_TRUE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId3));

  // Delete tablet 1 using TABLET_DATA_DELETED, so peer is removed completely from TsTabletManager.
  boost::optional<int64_t> cas_config_opid_index_less_or_equal;
  boost::optional<TabletServerErrorPB::Code> error_code;
  ASSERT_OK(tablet_manager_->DeleteTablet(kTabletId1,
      tablet::TABLET_DATA_DELETED,
      tablet::ShouldAbortActiveTransactions::kFalse,
      boost::optional<int64_t>{},
      false /* hide_only */,
      false /* keep_data */,
      &error_code));

  // Run ScheduleFullCompactions again. Cleanup will not be triggered in the stats window map
  // because we only execute cleanup every hour, and the number of extra tablet_ids don't meet the
  // threshold.
  compaction_manager->ScheduleFullCompactions();
  ASSERT_TRUE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId1));
  ASSERT_TRUE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId2));
  ASSERT_TRUE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId3));

  // Change the frequency with which we run the cleanup, and run ScheduleFullCompactions again.
  // This time, my-tablet-id-1 should be removed from the stats window map.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_memory_cleanup_interval_sec) = 1;
  SleepFor(MonoDelta::FromSeconds(2));
  compaction_manager->ScheduleFullCompactions();
  ASSERT_FALSE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId1));
  ASSERT_TRUE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId2));
  ASSERT_TRUE(compaction_manager->TEST_TabletIdInStatsWindowMap(kTabletId3));
}

} // namespace tserver
} // namespace yb
