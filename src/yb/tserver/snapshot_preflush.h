// Copyright (c) YugabyteDB, Inc.

#pragma once

#include <memory>

#include "yb/util/monotime.h"

namespace yb {
namespace tablet {
class ScopedReadOperation;
class SnapshotOperation;
}  // namespace tablet
namespace tserver {
struct LeaderTabletPeer;
class TabletServer;

// Orchestration waits on its own bounded workers, never on RPC/reactor or preparer threads.
// Flush I/O uses TabletFlusher's separate executor, so a full preflight pool cannot starve it.
class SnapshotPreflush {
 public:
  explicit SnapshotPreflush(TabletServer* server);
  ~SnapshotPreflush();

  // Always takes ownership and completes/aborts the operation on admission failure.
  void Submit(
      LeaderTabletPeer tablet, std::unique_ptr<tablet::SnapshotOperation> operation,
      tablet::ScopedReadOperation read_operation, CoarseTimePoint deadline);
  void StartShutdown();
  void CompleteShutdown();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tserver
}  // namespace yb
