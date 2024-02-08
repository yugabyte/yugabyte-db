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

#include "yb/master/xcluster/xcluster_catalog_entity.h"

namespace yb::master {

void XClusterSafeTimeInfo::Load(const XClusterSafeTimePB& metadata) {
  // Debug confirm that there is no xcluster_safe_time_info_ set. This also ensures that this does
  // not visit multiple rows.
  DCHECK(LockForRead()->pb.safe_time_map().empty()) << "Already have XCluster Safe Time data!";

  MetadataCowWrapper<PersistentXClusterSafeTimeInfo>::Load(metadata);
}
}  // namespace yb::master
