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
#include "yb/util/status.h"

namespace yb {
class MetricEntity;
class ThreadPool;

namespace tserver {

// Shared by local snapshot preflights and the admin FlushTablets RPC. Admission bounds both
// worker usage and outstanding work; a caller timeout does not release a running job's slot.
class TabletFlusher {
 public:
  using Callback = std::function<void(const Status&, const TabletId&)>;

  explicit TabletFlusher(const scoped_refptr<MetricEntity>& metrics);
  ~TabletFlusher();

  Status Submit(
      std::vector<tablet::TabletPtr> tablets, const FlushTabletsRequestPB& request,
      CoarseTimePoint deadline, Callback callback);
  void StartShutdown();
  void CompleteShutdown();

 private:
  void Release(const std::vector<tablet::TabletPtr>& tablets);

  const size_t limit_;
  std::unique_ptr<ThreadPool> pool_;
  // Admission and shutdown are serialized here; callbacks and I/O run without this mutex.
  std::mutex mutex_;
  bool closing_ GUARDED_BY(mutex_) = false;
  size_t active_ GUARDED_BY(mutex_) = 0;
  std::unordered_set<TabletId> tablets_ GUARDED_BY(mutex_);
  scoped_refptr<AtomicGauge<uint64_t>> active_metric_;
};

}  // namespace tserver
}  // namespace yb
