// Copyright (c) YugabyteDB, Inc.

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "yb/common/entity_ids_types.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/tserver/tserver_admin.fwd.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
class MetricEntity;
class ThreadPool;

namespace tserver {

// Shared by local snapshot preflights and the admin FlushTablets RPC. Admission bounds
// outstanding batches (reserved, queued, running, retiring) separately from worker count, so
// accepted work can wait for a worker instead of being rejected. Reserved jobs run on their own
// lane: a reservation is only ever held by an admitted preflight, so the lane sized to the
// preflight limit guarantees each reserved job a worker regardless of admin bulk flushes. A
// caller timeout does not release a running job's slot.
class TabletFlusher {
 public:
  using Callback = std::function<void(const Status&, const TabletId&)>;

  // Admission for one tablet taken ahead of the flush, so a coordinator can reject before
  // asking other replicas to do anything. Released on destruction unless consumed by Submit.
  // Destruction, move-assignment, and Release lock the flusher: never run them while holding
  // TabletFlusher::mutex_, and never let them be the last owner of state locked above it.
  class Reservation {
   public:
    Reservation() = default;
    Reservation(Reservation&& other) noexcept;
    Reservation& operator=(Reservation&& other) noexcept;
    ~Reservation();

    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;

    void Release();

   private:
    friend class TabletFlusher;
    Reservation(TabletFlusher* flusher, TabletId tablet_id);

    TabletFlusher* flusher_ = nullptr;
    TabletId tablet_id_;
  };

  explicit TabletFlusher(const scoped_refptr<MetricEntity>& metrics);
  ~TabletFlusher();

  // Non-waiting admission for a later single-tablet Submit.
  Result<Reservation> Reserve(const TabletId& tablet_id);

  Status Submit(
      std::vector<tablet::TabletPtr> tablets, const FlushTabletsRequestPB& request,
      CoarseTimePoint deadline, Callback callback);
  Status Submit(
      Reservation reservation, const tablet::TabletPtr& tablet,
      const FlushTabletsRequestPB& request, CoarseTimePoint deadline, Callback callback);
  void StartShutdown();
  void CompleteShutdown();

 private:
  static Status Validate(const FlushTabletsRequestPB& request, CoarseTimePoint deadline);
  Status Admit(const std::unordered_set<TabletId>& ids) REQUIRES(mutex_);
  Status Enqueue(
      ThreadPool& pool, bool reserved, std::vector<tablet::TabletPtr> tablets,
      std::unordered_set<TabletId> ids, const FlushTabletsRequestPB& request,
      CoarseTimePoint deadline, Callback callback) REQUIRES(mutex_);
  void ReleaseUnlocked(const std::unordered_set<TabletId>& ids, bool reserved) REQUIRES(mutex_);
  void Release(const std::unordered_set<TabletId>& ids, bool reserved);

  std::unique_ptr<ThreadPool> pool_;
  std::unique_ptr<ThreadPool> reserved_pool_;
  // Admission and shutdown are serialized here; callbacks and I/O run without this mutex.
  std::mutex mutex_;
  bool closing_ GUARDED_BY(mutex_) = false;
  size_t outstanding_ GUARDED_BY(mutex_) = 0;
  // Reserved lane occupancy: unconsumed reservations plus reserved jobs not yet retired.
  size_t reserved_ GUARDED_BY(mutex_) = 0;
  std::unordered_set<TabletId> tablets_ GUARDED_BY(mutex_);
  scoped_refptr<AtomicGauge<uint64_t>> active_metric_;
  scoped_refptr<Counter> expired_queued_metric_;
};

}  // namespace tserver
}  // namespace yb
