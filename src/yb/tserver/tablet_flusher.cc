// Copyright (c) YugabyteDB, Inc.

#include "yb/tserver/tablet_flusher.h"

#include <array>

#include "yb/docdb/doc_vector_index.h"
#include "yb/rocksdb/db.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_vector_indexes.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/status_format.h"
#include "yb/util/sync_point.h"
#include "yb/util/threadpool.h"

DEFINE_NON_RUNTIME_int32(tablet_flush_concurrency, 4,
    "Maximum admitted admin/local preflight flush jobs per tablet server.");
DEFINE_validator(tablet_flush_concurrency, FLAG_GT_VALUE_VALIDATOR(0));
TAG_FLAG(tablet_flush_concurrency, advanced);

DECLARE_bool(TEST_skip_force_superblock_flush);

THREAD_POOL_METRICS_DEFINE(server, tablet_flush_pool, "Admin tablet flush workers");
METRIC_DEFINE_gauge_uint64(server, tablet_flush_active, "Admitted tablet flush jobs",
    yb::MetricUnit::kOperations, "Flush jobs including error cleanup and retiring work", 0);

namespace yb::tserver {
namespace {

class FlushBatch {
 public:
  FlushBatch(const std::vector<tablet::TabletPtr>& tablets, const FlushTabletsRequestPB& request)
      : tablets_(tablets),
        vector_only_(request.flags() == tablet::FLUSH_COMPACT_VECTOR_INDEX_ONLY ||
            (request.flags() == tablet::FLUSH_COMPACT_DEFAULT &&
             !request.vector_index_ids().empty())),
        regular_only_(request.flags() == tablet::FLUSH_COMPACT_REGULAR_FOR_TEST_ONLY),
        vector_ids_(request.vector_index_ids().begin(), request.vector_index_ids().end()),
        dbs_(tablets.size()) {}

  Status Run(CoarseTimePoint deadline, TabletId* failed_tablet_id) {
    SCHECK(CoarseMonoClock::Now() < deadline, TimedOut, "Tablet flush deadline expired");
    std::vector<ScopedRWOperation> guards;
    guards.reserve(tablets_.size());
    // Guard acquisition must not fail after another tablet's flush has already started.
    for (size_t i = 0; i != tablets_.size(); ++i) {
      auto guard = tablets_[i]->CreateScopedRWOperationBlockingRocksDbShutdownStart();
      if (!guard.ok()) {
        *failed_tablet_id = tablets_[i]->tablet_id();
        return guard.CreateStatus();
      }
      guards.push_back(std::move(guard));
    }
    indexes_.reserve(tablets_.size());
    for (const auto& tablet : tablets_) {
      indexes_.push_back(regular_only_ ? tablet::VectorIndexList() :
          vector_only_ ? tablet->vector_indexes().Collect(vector_ids_) :
                         tablet->vector_indexes().List());
    }
    size_t launched = 0;
    for (; launched != tablets_.size();) {
      const auto i = launched++;
      Record(Start(i), i, failed_tablet_id);
      if (!status_.ok()) {
        break;
      }
    }
    // Even a failed Start/Wait can leave other DBs or indexes running. Keep the batch's
    // reservations and guards until all possibly launched work, including cleanup, retires.
    for (size_t i = 0; i != launched; ++i) {
      std::pair<tablet::Tablet*, Status> hook{tablets_[i].get(), Status::OK()};
      TEST_SYNC_POINT_CALLBACK("TabletFlusher::BeforeWait", &hook);
      Record(hook.second, i, failed_tablet_id);
      Record(indexes_[i].WaitForFlush(), i, failed_tablet_id);
      for (auto* db : dbs_[i]) {
        if (db) {
          Record(db->WaitForFlush(), i, failed_tablet_id);
          db->WaitForFlushJobs();
        }
      }
      TEST_SYNC_POINT_CALLBACK("TabletFlusher::Flushed", tablets_[i].get());
    }
    return status_;
  }

 private:
  Status Start(size_t i) {
    if (indexes_[i]) {
      for (const auto& index : *indexes_[i]) {
        RETURN_NOT_OK(index->Flush());
      }
    }
    if (!vector_only_) {
      rocksdb::FlushOptions options(rocksdb::FlushReason::kAdminFlush);
      options.wait = false;
      const std::array candidates{
          regular_only_ ? nullptr : tablets_[i]->intents_db(), tablets_[i]->regular_db()};
      for (size_t j = 0; j != candidates.size(); ++j) {
        if (auto* db = candidates[j]) {
          // Flush can schedule work and still fail; record the DB before calling it.
          dbs_[i][j] = db;
          RETURN_NOT_OK(db->Flush(options));
        }
      }
    }
    std::pair<tablet::Tablet*, Status> hook{tablets_[i].get(), Status::OK()};
    TEST_SYNC_POINT_CALLBACK("TabletFlusher::BeforeSuperblock", &hook);
    RETURN_NOT_OK(hook.second);
    // Persist dirty metadata too: https://github.com/yugabyte/yugabyte-db/issues/16116.
    return FLAGS_TEST_skip_force_superblock_flush ? Status::OK() :
        tablets_[i]->FlushSuperblock(tablet::OnlyIfDirty::kTrue);
  }

  void Record(const Status& status, size_t i, TabletId* failed_tablet_id) {
    if (status_.ok() && !status.ok()) {
      status_ = status;
      *failed_tablet_id = tablets_[i]->tablet_id();
    }
  }

  const std::vector<tablet::TabletPtr>& tablets_;
  const bool vector_only_;
  const bool regular_only_;
  const TableIds vector_ids_;
  std::vector<tablet::VectorIndexList> indexes_;
  std::vector<std::array<rocksdb::DB*, 2>> dbs_;
  Status status_;
};

}  // namespace

TabletFlusher::TabletFlusher(const scoped_refptr<MetricEntity>& metrics)
    : limit_(FLAGS_tablet_flush_concurrency),
      active_metric_(METRIC_tablet_flush_active.Instantiate(metrics, 0)) {
  CHECK_OK(ThreadPoolBuilder("tablet-flush")
      .set_max_threads(static_cast<int>(limit_))
      .set_metrics(THREAD_POOL_METRICS_INSTANCE(metrics, tablet_flush_pool))
      .Build(&pool_));
}

TabletFlusher::~TabletFlusher() {
  StartShutdown();
  CompleteShutdown();
}

Status TabletFlusher::Submit(
    std::vector<tablet::TabletPtr> tablets, const FlushTabletsRequestPB& request,
    CoarseTimePoint deadline, Callback callback) {
  SCHECK_EQ(request.operation(), FlushTabletsRequestPB::FLUSH, InvalidArgument,
            "Expected a flush request");
  SCHECK_NE(request.flags(), tablet::FLUSH_COMPACT_VECTOR_INDEX_EXCLUDED, InvalidArgument,
            "Vector index excluded flag is not supported for flush");
  SCHECK(CoarseMonoClock::Now() < deadline, TimedOut, "Tablet flush deadline expired");
  std::unordered_set<TabletId> ids;
  std::erase_if(tablets, [&ids](const auto& tablet) {
    return !ids.insert(tablet->tablet_id()).second;
  });
  std::lock_guard lock(mutex_);
  SCHECK(!closing_, ShutdownInProgress, "Tablet flusher is shutting down");
  SCHECK_LT(active_, limit_, ServiceUnavailable, "Tablet flush capacity exhausted");
  for (const auto& id : ids) {
    SCHECK(!tablets_.contains(id), ServiceUnavailable, "Tablet flush already in progress");
  }
  ++active_;
  active_metric_->Increment();
  tablets_.insert(ids.begin(), ids.end());
  // Serialize enqueue with StartShutdown so its subsequent pool drain includes every admitted
  // job. SubmitFunc only enqueues; neither the job nor its callback runs inline under this lock.
  auto status = pool_->SubmitFunc(
      [this, tablets, request, deadline, callback = std::move(callback)] {
    TabletId failed_tablet_id;
    Status status;
    {
      std::lock_guard lock(mutex_);
      if (closing_) {
        status = STATUS(ShutdownInProgress, "Tablet flusher is shutting down");
      }
    }
    if (status.ok()) {
      status = FlushBatch(tablets, request).Run(deadline, &failed_tablet_id);
    }
    Release(tablets);
    callback(status, failed_tablet_id);
  });
  if (!status.ok()) {
    --active_;
    active_metric_->Decrement();
    for (const auto& id : ids) {
      tablets_.erase(id);
    }
  }
  return status;
}

void TabletFlusher::Release(const std::vector<tablet::TabletPtr>& tablets) {
  std::lock_guard lock(mutex_);
  --active_;
  active_metric_->Decrement();
  for (const auto& tablet : tablets) {
    tablets_.erase(tablet->tablet_id());
  }
}

void TabletFlusher::StartShutdown() {
  std::lock_guard lock(mutex_);
  closing_ = true;
}

void TabletFlusher::CompleteShutdown() {
  pool_->Wait();
  pool_->Shutdown();
}

}  // namespace yb::tserver
