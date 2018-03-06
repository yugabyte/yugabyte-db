// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_MASTER_H
#define ENT_SRC_YB_MASTER_MASTER_H

namespace yb {
namespace master {
namespace enterprise {

class CatalogManager;
class TSDescriptor;

} // namespace enterprise
} // namespace master
} // namespace yb

#include "../../../../src/yb/master/master.h"

#include "yb/master/dns_manager.h"

namespace yb {
namespace master {
namespace enterprise {

class Master : public yb::master::Master {
  typedef yb::master::Master super;
 public:
  explicit Master(const MasterOptions& opts) : super(opts) {}
  Master(const Master&) = delete;
  void operator=(const Master&) = delete;

  // Periodically updates tservers endpoints in DNS if list of live tservers changed.
  void OnTSHeartbeat(
      const std::vector<std::shared_ptr<yb::master::TSDescriptor>>& live_tservers) override;

 protected:
  CHECKED_STATUS RegisterServices() override;

 private:
  DnsManager dns_manager_;
  // The last time when DNS manager was called to update list of tserver hosts if needed.
  std::atomic<MonoTime> last_dns_sync_{MonoTime::Min()};
  std::atomic<bool> dns_sync_in_progress_{false};
};

} // namespace enterprise
} // namespace master
} // namespace yb
#endif // ENT_SRC_YB_MASTER_MASTER_H
