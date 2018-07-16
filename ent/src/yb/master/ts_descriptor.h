// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_TS_DESCRIPTOR_H
#define ENT_SRC_YB_MASTER_TS_DESCRIPTOR_H

#include "yb/util/shared_ptr_tuple.h"

namespace yb {

namespace consensus {
class ConsensusServiceProxy;
}

namespace tserver {
class TabletServerAdminServiceProxy;
class TabletServerServiceProxy;
class TabletServerBackupServiceProxy;
}

namespace master {
namespace enterprise {

typedef util::SharedPtrTuple<tserver::TabletServerAdminServiceProxy,
    tserver::TabletServerServiceProxy,
    tserver::TabletServerBackupServiceProxy,
    consensus::ConsensusServiceProxy> ProxyTuple;

} // namespace enterprise
} // namespace master
} // namespace yb

#include "../../../../src/yb/master/ts_descriptor.h"

namespace yb {
namespace master {

class ReplicationInfoPB;
namespace enterprise {

class TSDescriptor : public yb::master::TSDescriptor {
  typedef yb::master::TSDescriptor super;
 public:
  explicit TSDescriptor(std::string perm_id)
      : super(std::move(perm_id)) {}
  virtual ~TSDescriptor() {}

  bool IsAcceptingLeaderLoad(const ReplicationInfoPB& replication_info) const override;

 private:
  // Is the ts in a read-only placement.
  bool IsReadOnlyTS(const ReplicationInfoPB& replication_info) const;
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_TS_DESCRIPTOR_H
