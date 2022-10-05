// Copyright (c) YugaByte, Inc.
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

namespace cdc {
class CDCServiceProxy;
}

namespace master {
namespace enterprise {

typedef util::SharedPtrTuple<tserver::TabletServerAdminServiceProxy,
    tserver::TabletServerServiceProxy,
    tserver::TabletServerBackupServiceProxy,
    cdc::CDCServiceProxy,
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
 public:
  explicit TSDescriptor(
      std::string perm_id,
      RegisteredThroughHeartbeat registered_through_heartbeat = RegisteredThroughHeartbeat::kTrue)
      : yb::master::TSDescriptor(std::move(perm_id), registered_through_heartbeat) {}

  bool IsAcceptingLeaderLoad(const ReplicationInfoPB& replication_info) const override;

 private:
  // Is the ts in a read-only placement.
  bool IsReadOnlyTS(const ReplicationInfoPB& replication_info) const;
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_TS_DESCRIPTOR_H
