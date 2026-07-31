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



#include "yb/master/master_fwd.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
class CloudInfoPB;
class NodeInstancePB;
namespace master {
class TSRegistrationPB;
}  // namespace master
namespace rpc {
class ProxyCache;
}  // namespace rpc
}  // namespace yb

namespace yb::master {

class TSDescriptorTestUtil {
 public:
  static Result<TSDescriptorPtr> RegisterNew(
      const NodeInstancePB& instance, const TSRegistrationPB& registration,
      CloudInfoPB local_master_cloud_info, rpc::ProxyCache* proxy_cache,
      RegisteredThroughHeartbeat registered_through_heartbeat = RegisteredThroughHeartbeat::kTrue);
};

}  // namespace yb::master
