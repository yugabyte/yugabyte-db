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
//

#pragma once

#include <memory>

#include "yb/master/master_fwd.h"
#include "yb/master/xcluster/master_xcluster_types.h"

#include "yb/util/status_fwd.h"

namespace yb {

class HybridTime;

namespace rpc {
class RpcContext;
}  // namespace rpc

namespace master {

class GetXClusterSafeTimeRequestPB;
class GetXClusterSafeTimeResponsePB;
class SysXClusterConfigEntryPB;
struct LeaderEpoch;

class XClusterManagerIf {
 public:
  virtual Result<HybridTime> GetXClusterSafeTime(const NamespaceId& namespace_id) const = 0;
  virtual Result<XClusterNamespaceToSafeTimeMap> RefreshAndGetXClusterNamespaceToSafeTimeMap(
      const LeaderEpoch& epoch) = 0;
  virtual Result<XClusterNamespaceToSafeTimeMap> GetXClusterNamespaceToSafeTimeMap() const = 0;
  virtual Status SetXClusterNamespaceToSafeTimeMap(
      const int64_t leader_term, const XClusterNamespaceToSafeTimeMap& safe_time_map) = 0;

  virtual Status GetXClusterConfigEntryPB(SysXClusterConfigEntryPB* config) const = 0;

 protected:
  virtual ~XClusterManagerIf() = default;
};

}  // namespace master
}  // namespace yb
