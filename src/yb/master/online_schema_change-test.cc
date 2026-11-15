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

// Tests of the online schema change controller without a tablet server. The safe-time and chunk
// RPCs are captured instead of sent, and their responses are fed back by hand; results are checked
// on the durable job entry and the tablets' progress.

#include <map>
#include <string>
#include <vector>

#include "yb/common/entity_ids.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_error.h"
#include "yb/master/master-test_base.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/online_schema_change.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/tablet_split_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

DECLARE_bool(TEST_online_schema_change_backfill_pause_controller);
DECLARE_int32(TEST_sys_catalog_write_rejection_percentage);
DECLARE_int32(ysql_index_backfill_rpc_timeout_ms);
DECLARE_uint32(online_schema_change_backfill_max_resumes);
DECLARE_uint32(online_schema_change_backfill_pin_release_margin_ms);
DECLARE_uint32(TEST_online_schema_change_backfill_replay_chunks_per_tablet);

namespace yb {
namespace master {

namespace {

constexpr uint32_t kDatabaseOid = kPgFirstNormalObjectId + 17;
constexpr uint32_t kSourceTableOid = kPgFirstNormalObjectId + 101;
constexpr uint32_t kTargetTableOid = kPgFirstNormalObjectId + 102;
constexpr uint32_t kTransformFnOid = kPgFirstNormalObjectId + 103;
// Leaves of src and dst when a test treats them as partitioned roots.
constexpr uint32_t kSourceLeafAOid = kPgFirstNormalObjectId + 104;
constexpr uint32_t kSourceLeafBOid = kPgFirstNormalObjectId + 105;
constexpr uint32_t kTargetLeafAOid = kPgFirstNormalObjectId + 106;
constexpr uint32_t kTargetLeafBOid = kPgFirstNormalObjectId + 107;
constexpr int64_t kPgJobId = 9001;
constexpr uint64_t kReadTime = 123456789;

using State = OnlineSchemaChangeJobPB;

}  // namespace

class OnlineSchemaChangeTest : public MasterTestBase {
 protected:
  void SetUp() override {
    MasterTestBase::SetUp();
    // The tests visit jobs themselves.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_pause_controller) = true;

    CreateNamespaceResponsePB ns_resp;
    ASSERT_OK(CreatePgsqlNamespace("osc_db", GetPgsqlNamespaceId(kDatabaseOid), &ns_resp));
    Schema schema({ColumnSchema("key", DataType::INT32, ColumnKind::HASH),
                   ColumnSchema("v", DataType::INT32)});
    for (const auto& [name, oid] : std::vector<std::pair<std::string, uint32_t>>{
             {"src", kSourceTableOid}, {"dst", kTargetTableOid},
             {"src_a", kSourceLeafAOid}, {"src_b", kSourceLeafBOid},
             {"dst_a", kTargetLeafAOid}, {"dst_b", kTargetLeafBOid}}) {
      ASSERT_OK(CreatePgsqlTable(
          GetPgsqlNamespaceId(kDatabaseOid), name, TableIdOf(oid), schema));
    }
    source_table_ = ASSERT_RESULT(catalog_manager().FindTableById(TableIdOf(kSourceTableOid)));
    epoch_ = catalog_manager().GetLeaderEpochInternal();
    // With no tablet server, no tablet would become RUNNING by itself.
    for (const auto oid : {kSourceTableOid, kSourceLeafAOid, kSourceLeafBOid}) {
      for (const auto& tablet : ASSERT_RESULT(ASSERT_RESULT(Table(oid))->GetTablets())) {
        auto l = tablet->LockForWrite();
        l.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);
        ASSERT_OK(catalog_manager().sys_catalog()->Upsert(epoch_, tablet));
        l.Commit();
      }
    }

    controller().TEST_send_safe_time_ = [this](const TabletInfoPtr& tablet, HybridTime) {
      std::lock_guard lock(sent_mutex_);
      safe_times_sent_.push_back(tablet);
      return Status::OK();
    };
    controller().TEST_send_chunk_ =
        [this](const TabletInfoPtr& tablet, const tserver::TableBackfillChunkRequestPB& req) {
          std::lock_guard lock(sent_mutex_);
          chunks_sent_.push_back({tablet, req.start_key()});
          return Status::OK();
        };
  }

  void TearDown() override {
    controller().Reset();
    controller().TEST_send_safe_time_ = nullptr;
    controller().TEST_send_chunk_ = nullptr;
    MasterTestBase::TearDown();
  }

  CatalogManager& catalog_manager() { return mini_master_->catalog_manager_impl(); }
  OnlineSchemaChangeController& controller() { return catalog_manager().online_schema_change(); }

  static TableId TableIdOf(uint32_t oid) { return GetPgsqlTableId(kDatabaseOid, oid); }

  Result<scoped_refptr<TableInfo>> Table(uint32_t oid) {
    return catalog_manager().FindTableById(TableIdOf(oid));
  }

  static OnlineSchemaChangeJobPB NewJob(int64_t pg_job_id = kPgJobId) {
    OnlineSchemaChangeJobPB job;
    job.set_pg_job_id(pg_job_id);
    job.set_state(State::COMPUTING_READ_TIME);
    job.set_backfilling_timestamp(kReadTime);
    job.set_source_table_oid(kSourceTableOid);
    job.set_target_table_oid(kTargetTableOid);
    job.set_transform_fn_oid(kTransformFnOid);
    job.set_database_name("osc_db");
    return job;
  }

  Status Start(int64_t pg_job_id = kPgJobId) {
    return controller().Start(source_table_, NewJob(pg_job_id), epoch_);
  }

  void Visit() { controller().Reconcile(source_table_, epoch_); }

  // The rescan a new leader term starts with.
  void Discover() { controller().Discover(epoch_); }

  void RunOnce() { controller().RunOnce(epoch_); }

  OnlineSchemaChangeController::JobPtr Job() {
    std::lock_guard lock(controller().mutex_);
    auto it = controller().jobs_.find(source_table_->id());
    return it == controller().jobs_.end() ? nullptr : it->second;
  }

  // Answers every outstanding GetSafeTime, the i-th with kReadTime + times[i].
  void AnswerSafeTimes(const std::vector<uint64_t>& times) {
    auto job = Job();
    ASSERT_TRUE(job);
    std::vector<TabletInfoPtr> sent;
    {
      std::lock_guard lock(sent_mutex_);
      sent.swap(safe_times_sent_);
    }
    ASSERT_EQ(sent.size(), times.size());
    for (size_t i = 0; i < times.size(); ++i) {
      controller().OnSafeTime(job, epoch_, Status::OK(), HybridTime(kReadTime + times[i]));
    }
  }

  void AnswerChunk(
      const TabletInfoPtr& tablet, const Status& status, const std::string& backfilled_until,
      uint64_t rows, const LeaderEpoch& epoch) {
    auto job = Job();
    ASSERT_TRUE(job);
    tserver::TableBackfillChunkResponsePB resp;
    resp.set_backfilled_until(backfilled_until);
    resp.set_num_rows_processed(rows);
    controller().OnChunkDone(job, epoch, tablet, status, resp);
  }

  void AnswerChunk(
      const TabletInfoPtr& tablet, const std::string& backfilled_until, uint64_t rows) {
    AnswerChunk(tablet, Status::OK(), backfilled_until, rows, epoch_);
  }

  void OnSafeTime(
      const OnlineSchemaChangeController::JobPtr& job, const Status& status, HybridTime time) {
    controller().OnSafeTime(job, epoch_, status, time);
  }

  void OnChunkDone(
      const OnlineSchemaChangeController::JobPtr& job, const TabletInfoPtr& tablet,
      const Status& status) {
    controller().OnChunkDone(job, epoch_, tablet, status, {});
  }

  // Chunks sent since the last call, by tablet id.
  std::map<TabletId, std::vector<std::string>> TakeChunksSent() {
    std::lock_guard lock(sent_mutex_);
    std::map<TabletId, std::vector<std::string>> result;
    for (const auto& [tablet, start_key] : chunks_sent_) {
      result[tablet->id()].push_back(start_key);
    }
    chunks_sent_.clear();
    return result;
  }

  size_t SafeTimesSent() {
    std::lock_guard lock(sent_mutex_);
    return safe_times_sent_.size();
  }

  // Start, then the safe-time round, so the job copies with every source tablet's first chunk out.
  void StartCopying() {
    ASSERT_OK(Start());
    std::vector<uint64_t> times(SafeTimesSent(), 0);
    ASSERT_NO_FATALS(AnswerSafeTimes(times));
    ASSERT_EQ(ReadDurableJob().state(), State::IN_PROGRESS);
  }

  void SetDurableJob(const std::function<void(OnlineSchemaChangeJobPB*)>& mutate) {
    auto l = source_table_->LockForWrite();
    auto& pb = l.mutable_data()->pb;
    if (!pb.has_online_schema_change_job()) {
      *pb.mutable_online_schema_change_job() = NewJob();
      pb.mutable_online_schema_change_job()->set_state(State::IN_PROGRESS);
    }
    mutate(pb.mutable_online_schema_change_job());
    ASSERT_OK(catalog_manager().sys_catalog()->Upsert(epoch_, source_table_));
    l.Commit();
  }

  OnlineSchemaChangeJobPB ReadDurableJob() {
    auto l = source_table_->LockForRead();
    return l->pb.online_schema_change_job();
  }

  void ClearDurableJob() {
    auto l = source_table_->LockForWrite();
    l.mutable_data()->pb.clear_online_schema_change_job();
    ASSERT_OK(catalog_manager().sys_catalog()->Upsert(epoch_, source_table_));
    l.Commit();
  }

  void SetTabletProgress(const TabletInfoPtr& tablet, const std::string& key, uint64_t rows) {
    auto l = tablet->LockForWrite();
    (*l.mutable_data()->pb.mutable_backfilled_until())[OnlineSchemaChangeTabletKey(kPgJobId)] = key;
    (*l.mutable_data()->pb.mutable_backfilled_rows())[OnlineSchemaChangeTabletKey(kPgJobId)] = rows;
    ASSERT_OK(catalog_manager().sys_catalog()->Upsert(epoch_, tablet));
    l.Commit();
  }

  std::optional<std::string> TabletResumeKey(const TabletInfoPtr& tablet) {
    auto l = tablet->LockForRead();
    const auto it = l->pb.backfilled_until().find(OnlineSchemaChangeTabletKey(kPgJobId));
    if (it == l->pb.backfilled_until().end()) {
      return std::nullopt;
    }
    return it->second;
  }

  uint64_t TabletRows(const TabletInfoPtr& tablet) {
    auto l = tablet->LockForRead();
    const auto it = l->pb.backfilled_rows().find(OnlineSchemaChangeTabletKey(kPgJobId));
    return it == l->pb.backfilled_rows().end() ? 0 : it->second;
  }

  static bool HasPin(const scoped_refptr<TableInfo>& table) {
    auto l = table->LockForRead();
    return l->pb.has_table_backfill_pin_job_id() &&
           l->pb.table_backfill_pin_job_id() == kPgJobId &&
           l->pb.schema().table_properties().retain_delete_markers();
  }

  // Writes the pin the way PinOnlineSchemaChangeTargetDeleteMarkers does, minus the alter it pushes
  // to the tablets.
  void SetPin(const scoped_refptr<TableInfo>& table) {
    auto l = table->LockForWrite();
    auto& pb = l.mutable_data()->pb;
    pb.mutable_schema()->mutable_table_properties()->set_retain_delete_markers(true);
    pb.set_table_backfill_pin_job_id(kPgJobId);
    ASSERT_OK(catalog_manager().sys_catalog()->Upsert(epoch_, table));
    l.Commit();
  }

  scoped_refptr<TableInfo> TargetTable() { return CHECK_RESULT(Table(kTargetTableOid)); }
  bool TargetIsPinned() { return HasPin(TargetTable()); }

  Result<TabletInfos> SourceTablets() { return source_table_->GetTablets(); }

  // With no tablet server a split that the online schema change check lets through still fails a
  // later check (the table has no replication info), so only the refusal's error code tells them
  // apart.
  static bool RefusedForOnlineSchemaChangeBackfill(const Status& status) {
    return MasterError(status) == MasterErrorPB::SPLIT_OR_BACKFILL_IN_PROGRESS;
  }

  scoped_refptr<TableInfo> source_table_;
  LeaderEpoch epoch_;

  std::mutex sent_mutex_;
  std::vector<TabletInfoPtr> safe_times_sent_;
  std::vector<std::pair<TabletInfoPtr, std::string>> chunks_sent_;
};

TEST_F(OnlineSchemaChangeTest, StartHoldsTheTablesAndAsksEveryTabletForASafeTime) {
  ASSERT_OK(Start());
  const auto job = ReadDurableJob();
  ASSERT_EQ(job.state(), State::COMPUTING_READ_TIME);
  ASSERT_EQ(job.backfilling_timestamp(), kReadTime);
  ASSERT_EQ(job.resume_count(), 0) << "a start is not a takeover";
  ASSERT_TRUE(TargetIsPinned());
  ASSERT_EQ(SafeTimesSent(), ASSERT_RESULT(SourceTablets()).size());
  ASSERT_TRUE(TakeChunksSent().empty()) << "copying before the read time is written";
}

TEST_F(OnlineSchemaChangeTest, TheReadTimeIsTheMaxSafeTimeAndTheCopyStartsAtIt) {
  ASSERT_OK(Start());
  const auto tablets = ASSERT_RESULT(SourceTablets());
  std::vector<uint64_t> times;
  for (size_t i = 0; i < tablets.size(); ++i) {
    times.push_back(i == 1 ? 1000 : i);
  }
  ASSERT_NO_FATALS(AnswerSafeTimes(times));

  const auto job = ReadDurableJob();
  ASSERT_EQ(job.state(), State::IN_PROGRESS);
  ASSERT_EQ(job.backfilling_timestamp(), kReadTime + 1000);
  const auto sent = TakeChunksSent();
  ASSERT_EQ(sent.size(), tablets.size());
  for (const auto& tablet : tablets) {
    ASSERT_EQ(sent.at(tablet->id()), std::vector<std::string>{""}) << tablet->id();
  }
}

TEST_F(OnlineSchemaChangeTest, OnlyOneChunkPerTabletIsInFlight) {
  ASSERT_NO_FATALS(StartCopying());
  ASSERT_EQ(TakeChunksSent().size(), ASSERT_RESULT(SourceTablets()).size());
  for (int i = 0; i < 3; ++i) {
    Visit();
    RunOnce();
  }
  ASSERT_TRUE(TakeChunksSent().empty()) << "a visit resent a chunk that is still in flight";
}

// A response persists the tablet's resume key and rows, and only that; the next chunk starts at the
// key.
TEST_F(OnlineSchemaChangeTest, AChunkResponseOnlyRecordsTheTabletsProgress) {
  ASSERT_NO_FATALS(StartCopying());
  TakeChunksSent();
  const auto tablets = ASSERT_RESULT(SourceTablets());

  ASSERT_NO_FATALS(AnswerChunk(tablets[0], "key-1", 25));
  ASSERT_EQ(TabletResumeKey(tablets[0]), "key-1");
  ASSERT_EQ(TabletRows(tablets[0]), 25);
  ASSERT_EQ(ReadDurableJob().state(), State::IN_PROGRESS);
  ASSERT_EQ(ReadDurableJob().num_rows_processed(), 0);
  auto sent = TakeChunksSent();
  ASSERT_EQ(sent.size(), 1);
  ASSERT_EQ(sent.at(tablets[0]->id()), std::vector<std::string>{"key-1"});

  ASSERT_NO_FATALS(AnswerChunk(tablets[0], "", 5));
  ASSERT_EQ(TabletResumeKey(tablets[0]), "");
  ASSERT_EQ(TabletRows(tablets[0]), 30);
  ASSERT_TRUE(TakeChunksSent().empty()) << "a drained tablet was sent another chunk";
}

TEST_F(OnlineSchemaChangeTest, TheJobSucceedsOnceEveryTabletHasDrained) {
  ASSERT_NO_FATALS(StartCopying());
  const auto tablets = ASSERT_RESULT(SourceTablets());
  for (size_t i = 0; i < tablets.size(); ++i) {
    ASSERT_EQ(ReadDurableJob().state(), State::IN_PROGRESS) << "after " << i << " tablets";
    ASSERT_NO_FATALS(AnswerChunk(tablets[i], "", 10));
  }
  const auto job = ReadDurableJob();
  ASSERT_EQ(job.state(), State::SUCCESS);
  ASSERT_EQ(job.num_rows_processed(), 10 * tablets.size());
  ASSERT_EQ(job.num_tablets_done(), static_cast<int32_t>(tablets.size()));
  ASSERT_EQ(job.num_tablets_total(), static_cast<int32_t>(tablets.size()));
  ASSERT_TRUE(job.has_finished_at());
}

// A chunk sent before the job succeeded can still be writing on a tserver, e.g. one a deposed
// leader sent. Releasing the pin before it lands lets compaction drop a tombstone that hides the
// row the chunk brings back.
TEST_F(OnlineSchemaChangeTest, TheSuccessKeepsItsPinUntilNoChunkCanStillLand) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_index_backfill_rpc_timeout_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_pin_release_margin_ms) = 0;
  ASSERT_NO_FATALS(StartCopying());
  const auto tablets = ASSERT_RESULT(SourceTablets());
  for (const auto& tablet : tablets) {
    ASSERT_NO_FATALS(AnswerChunk(tablet, "", 1));
  }
  ASSERT_EQ(ReadDurableJob().state(), State::SUCCESS);
  ASSERT_TRUE(TargetIsPinned()) << "the pin went with the outcome write";
  Visit();
  ASSERT_TRUE(TargetIsPinned());
  ASSERT_TRUE(Job()) << "the job was forgotten while it still held its pin";

  ASSERT_OK(WaitFor(
      [this]() {
        Visit();
        return !TargetIsPinned();
      },
      30s * kTimeMultiplier, "Wait for the pin to be released"));
  ASSERT_FALSE(TabletResumeKey(tablets[0])) << "the resume keys outlived the release";
  ASSERT_FALSE(Job());
}

TEST_F(OnlineSchemaChangeTest, ACancelWinsOverTheReadTime) {
  ASSERT_OK(Start());
  // Recorded while the round is out, but not yet seen by a visit.
  SetDurableJob([](OnlineSchemaChangeJobPB* job) { job->set_cancel_requested(true); });
  std::vector<uint64_t> times(SafeTimesSent(), 0);
  ASSERT_NO_FATALS(AnswerSafeTimes(times));
  ASSERT_EQ(ReadDurableJob().state(), State::CANCELLED)
      << "the read time was written over a recorded cancel";
  ASSERT_TRUE(TakeChunksSent().empty());
  ASSERT_FALSE(TargetIsPinned());
}

// An outcome decided before a cancel lands must not hide it: postgres may already show it. A pass
// writes a recorded failure before it looks at the cancel, so the outcome write itself must turn it
// into CANCELLED.
TEST_F(OnlineSchemaChangeTest, ACancelRecordedBeforeTheOutcomeWriteWins) {
  ASSERT_NO_FATALS(StartCopying());
  SetDurableJob([](OnlineSchemaChangeJobPB* job) { job->set_cancel_requested(true); });
  const auto tablets = ASSERT_RESULT(SourceTablets());
  ASSERT_NO_FATALS(AnswerChunk(tablets[0], STATUS(IOError, "boom"), "", 0, epoch_));
  ASSERT_EQ(ReadDurableJob().state(), State::CANCELLED);
}

TEST_F(OnlineSchemaChangeTest, AChunkFailureFailsTheJobAndReleasesItsPin) {
  ASSERT_NO_FATALS(StartCopying());
  ASSERT_TRUE(TargetIsPinned());

  const auto tablets = ASSERT_RESULT(SourceTablets());
  ASSERT_NO_FATALS(AnswerChunk(tablets[0], STATUS(IOError, "disk on fire"), "", 0, epoch_));
  const auto job = ReadDurableJob();
  ASSERT_EQ(job.state(), State::FAILED);
  ASSERT_STR_CONTAINS(job.error_message(), "disk on fire");
  ASSERT_FALSE(TargetIsPinned());
  ASSERT_FALSE(Job());
}

// A response whose progress cannot be written is as good as lost: the chunk is resent from the
// last persisted key, and rewrites the same rows.
TEST_F(OnlineSchemaChangeTest, AnUnwrittenResponseIsResentFromTheLastKey) {
  ASSERT_NO_FATALS(StartCopying());
  TakeChunksSent();
  const auto tablets = ASSERT_RESULT(SourceTablets());
  ASSERT_NO_FATALS(AnswerChunk(tablets[0], "key-1", 25));
  TakeChunksSent();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sys_catalog_write_rejection_percentage) = 100;
  ASSERT_NO_FATALS(AnswerChunk(tablets[0], "key-2", 25));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sys_catalog_write_rejection_percentage) = 0;
  ASSERT_EQ(TabletResumeKey(tablets[0]), "key-1");
  ASSERT_EQ(ReadDurableJob().state(), State::IN_PROGRESS);

  RunOnce();
  const auto sent = TakeChunksSent();
  ASSERT_EQ(sent.size(), 1);
  ASSERT_EQ(sent.at(tablets[0]->id()), std::vector<std::string>{"key-1"});
}

TEST_F(OnlineSchemaChangeTest, AReplayedChunkIsResentFromTheSameKey) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_replay_chunks_per_tablet) = 1;
  auto se = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_online_schema_change_backfill_replay_chunks_per_tablet) =
        0;
  });
  ASSERT_NO_FATALS(StartCopying());
  TakeChunksSent();
  const auto tablets = ASSERT_RESULT(SourceTablets());
  ASSERT_NO_FATALS(AnswerChunk(tablets[0], "key-1", 25));
  ASSERT_FALSE(TabletResumeKey(tablets[0]));
  ASSERT_EQ(TakeChunksSent().at(tablets[0]->id()), std::vector<std::string>{""});
  ASSERT_NO_FATALS(AnswerChunk(tablets[0], "key-1", 25));
  ASSERT_EQ(TabletResumeKey(tablets[0]), "key-1");
}

TEST_F(OnlineSchemaChangeTest, ResponsesOfAnEarlierTermAreIgnored) {
  ASSERT_NO_FATALS(StartCopying());
  const auto tablets = ASSERT_RESULT(SourceTablets());
  const LeaderEpoch earlier(epoch_.leader_term - 1, epoch_.pitr_count);
  ASSERT_NO_FATALS(AnswerChunk(tablets[0], Status::OK(), "key-1", 25, earlier));
  ASSERT_FALSE(TabletResumeKey(tablets[0]));

  auto job = Job();
  controller().Reset();
  OnChunkDone(job, tablets[0], Status::OK());
  ASSERT_FALSE(TabletResumeKey(tablets[0])) << "a response reached a job this master gave up";
  ASSERT_EQ(ReadDurableJob().state(), State::IN_PROGRESS);
}

// A new leader drives the job from the durable entry and the tablets' resume keys.
TEST_F(OnlineSchemaChangeTest, ANewTermTakesOverFromTheDurableState) {
  const auto tablets = ASSERT_RESULT(SourceTablets());
  ASSERT_GE(tablets.size(), 3);
  SetDurableJob([](OnlineSchemaChangeJobPB* job) { job->set_state(State::IN_PROGRESS); });
  SetTabletProgress(tablets[0], "", 7);
  SetTabletProgress(tablets[1], "key-5", 3);

  Discover();
  RunOnce();
  ASSERT_EQ(ReadDurableJob().resume_count(), 1);
  const auto sent = TakeChunksSent();
  ASSERT_FALSE(sent.contains(tablets[0]->id())) << "a drained tablet was copied again";
  ASSERT_EQ(sent.at(tablets[1]->id()), std::vector<std::string>{"key-5"});
  ASSERT_EQ(sent.at(tablets[2]->id()), std::vector<std::string>{""});
  ASSERT_EQ(sent.size(), tablets.size() - 1);
  ASSERT_TRUE(TargetIsPinned()) << "the takeover did not re-assert the pin";

  RunOnce();
  ASSERT_EQ(ReadDurableJob().resume_count(), 1) << "a takeover was counted twice";
}

TEST_F(OnlineSchemaChangeTest, TheBudgetFailsAJobTakenOverTooOften) {
  constexpr uint32_t kMaxResumes = 3;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_online_schema_change_backfill_max_resumes) = kMaxResumes;
  SetDurableJob([](OnlineSchemaChangeJobPB* job) { job->set_resume_count(kMaxResumes); });

  Discover();
  RunOnce();
  const auto job = ReadDurableJob();
  ASSERT_EQ(job.state(), State::FAILED);
  ASSERT_STR_CONTAINS(job.error_message(), "online_schema_change_backfill_max_resumes");
  ASSERT_EQ(job.resume_count(), kMaxResumes);
  ASSERT_TRUE(TakeChunksSent().empty());
}

TEST_F(OnlineSchemaChangeTest, ATabletThatIsNotRunningFailsTheJob) {
  const auto tablets = ASSERT_RESULT(SourceTablets());
  {
    auto l = tablets[0]->LockForWrite();
    l.mutable_data()->pb.set_state(SysTabletsEntryPB::CREATING);
    ASSERT_OK(catalog_manager().sys_catalog()->Upsert(epoch_, tablets[0]));
    l.Commit();
  }
  ASSERT_NOK(Start());
  ASSERT_EQ(ReadDurableJob().state(), State::FAILED);
  ASSERT_EQ(SafeTimesSent(), 0);
}

TEST_F(OnlineSchemaChangeTest, AMissingTargetFailsTheJob) {
  auto job = NewJob();
  job.set_target_table_oid(kTargetTableOid + 50);
  ASSERT_NOK(controller().Start(source_table_, job, epoch_));
  ASSERT_EQ(ReadDurableJob().state(), State::FAILED);
}

TEST_F(OnlineSchemaChangeTest, ACancelStopsAJobThisMasterIsNotDrivingYet) {
  SetDurableJob([](OnlineSchemaChangeJobPB* job) {
    job->set_state(State::IN_PROGRESS);
    job->set_cancel_requested(true);
  });
  SetPin(TargetTable());

  Discover();
  RunOnce();
  ASSERT_EQ(ReadDurableJob().state(), State::CANCELLED);
  ASSERT_FALSE(TargetIsPinned());
  ASSERT_TRUE(TakeChunksSent().empty());
}

// A retried StartTableBackfill finds the entry its first attempt wrote. A source has one live job,
// and a finished job's entry gives way to the next one.
TEST_F(OnlineSchemaChangeTest, StartIsIdempotentAndOneJobPerSource) {
  ASSERT_OK(Start());
  const auto round = SafeTimesSent();
  ASSERT_OK(Start());
  ASSERT_EQ(SafeTimesSent(), round) << "a retried start sent a second safe-time round";
  ASSERT_EQ(ReadDurableJob().pg_job_id(), kPgJobId);

  auto other = Start(kPgJobId + 1);
  ASSERT_TRUE(other.IsAlreadyPresent()) << other;

  SetDurableJob([](OnlineSchemaChangeJobPB* job) { job->set_state(State::FAILED); });
  auto again = Start();
  ASSERT_TRUE(again.IsAlreadyPresent()) << again;

  // The failed job's visit releases its pin, which the next job takes.
  Visit();
  ASSERT_OK(Start(kPgJobId + 1));
  ASSERT_EQ(ReadDurableJob().pg_job_id(), kPgJobId + 1);
  ASSERT_EQ(ReadDurableJob().state(), State::COMPUTING_READ_TIME);
}

TEST_F(OnlineSchemaChangeTest, SplitsAreRefusedWhileTheJobCopies) {
  auto& split_manager = mini_master_->master()->tablet_split_manager();
  ASSERT_FALSE(RefusedForOnlineSchemaChangeBackfill(
      split_manager.ValidateSplitCandidateTable(source_table_)));

  for (const auto state : {State::COMPUTING_READ_TIME, State::IN_PROGRESS}) {
    SetDurableJob([state](OnlineSchemaChangeJobPB* job) { job->set_state(state); });
    for (const auto ignore_disabled : {IgnoreDisabledList::kFalse, IgnoreDisabledList::kTrue}) {
      auto status = split_manager.ValidateSplitCandidateTable(source_table_, ignore_disabled);
      ASSERT_TRUE(RefusedForOnlineSchemaChangeBackfill(status))
          << State::State_Name(state) << ": " << status;
    }
  }
  for (const auto state : {State::SUCCESS, State::FAILED, State::CANCELLED}) {
    SetDurableJob([state](OnlineSchemaChangeJobPB* job) { job->set_state(state); });
    auto status = split_manager.ValidateSplitCandidateTable(source_table_);
    ASSERT_FALSE(RefusedForOnlineSchemaChangeBackfill(status))
        << State::State_Name(state) << ": " << status;
  }
}

TEST_F(OnlineSchemaChangeTest, TheReapReleasesAPinWhoseJobEntryIsGone) {
  SetPin(TargetTable());
  Discover();
  ASSERT_FALSE(TargetIsPinned()) << "a pin with no job entry left to name it was not reaped";
}

TEST_F(OnlineSchemaChangeTest, TheReapLeavesALiveJobsPinAlone) {
  SetDurableJob([](OnlineSchemaChangeJobPB* job) { job->set_state(State::IN_PROGRESS); });
  SetPin(TargetTable());
  Discover();
  ASSERT_TRUE(TargetIsPinned()) << "the reap took a running job's pin";

  // A SUCCESS job's pin stays until no chunk sent before it can still land.
  SetDurableJob([this](OnlineSchemaChangeJobPB* job) {
    job->set_state(State::SUCCESS);
    job->set_finished_at(mini_master_->master()->clock()->Now().ToUint64());
  });
  controller().Reset();
  Discover();
  ASSERT_TRUE(TargetIsPinned()) << "the reap took the pin of a job that has just succeeded";

  SetDurableJob([](OnlineSchemaChangeJobPB* job) { job->set_finished_at(kReadTime); });
  controller().Reset();
  Discover();
  ASSERT_FALSE(TargetIsPinned()) << "a finished job's pin was left behind";
}

TEST_F(OnlineSchemaChangeTest, ValidateRejectsHalfWrittenJobs) {
  const auto complete = NewJob();
  ASSERT_OK(ValidateOnlineSchemaChangeJob(complete));
  ASSERT_NOK(ValidateOnlineSchemaChangeJob(OnlineSchemaChangeJobPB()));

  using Mutation = std::function<void(OnlineSchemaChangeJobPB*)>;
  const std::vector<std::pair<std::string, Mutation>> mutations = {
      {"no job id", [](OnlineSchemaChangeJobPB* j) { j->clear_pg_job_id(); }},
      {"zero job id", [](OnlineSchemaChangeJobPB* j) { j->set_pg_job_id(0); }},
      {"no state", [](OnlineSchemaChangeJobPB* j) { j->clear_state(); }},
      {"no read time", [](OnlineSchemaChangeJobPB* j) { j->clear_backfilling_timestamp(); }},
      {"invalid read time",
       [](OnlineSchemaChangeJobPB* j) {
         j->set_backfilling_timestamp(HybridTime::kInvalid.ToUint64());
       }},
      {"no source oid", [](OnlineSchemaChangeJobPB* j) { j->clear_source_table_oid(); }},
      {"zero source oid",
       [](OnlineSchemaChangeJobPB* j) { j->set_source_table_oid(kPgInvalidOid); }},
      {"no target oid", [](OnlineSchemaChangeJobPB* j) { j->clear_target_table_oid(); }},
      {"no transform oid", [](OnlineSchemaChangeJobPB* j) { j->clear_transform_fn_oid(); }},
  };
  for (const auto& [name, mutate] : mutations) {
    auto job = complete;
    mutate(&job);
    ASSERT_NOK(ValidateOnlineSchemaChangeJob(job)) << name;
  }
}

}  // namespace master
}  // namespace yb
