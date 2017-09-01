// Copyright (c) YugaByte, Inc.
#ifndef YB_TABLET_TABLET_OPTIONS_H
#define YB_TABLET_TABLET_OPTIONS_H

namespace rocksdb {
class EventListener;
}

namespace yb {
namespace tablet {

struct TabletOptions {
  std::shared_ptr<rocksdb::Cache> block_cache;
  std::shared_ptr<rocksdb::MemoryMonitor> memory_monitor;
  std::vector<std::shared_ptr<rocksdb::EventListener>> listeners;
};

} // namespace tablet
} // namespace yb
#endif // YB_TABLET_TABLET_OPTIONS_H
