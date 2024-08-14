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
//

#include "yb/master/ts_descriptor_test_util.h"

namespace yb::master {

Result<TSDescriptorPtr> TSDescriptorTestUtil::RegisterNew(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    CloudInfoPB local_cloud_info, rpc::ProxyCache* proxy_cache,
    RegisteredThroughHeartbeat registered_through_heartbeat) {
  auto [ts_descriptor, write_lock] = VERIFY_RESULT(TSDescriptor::CreateNew(
      instance, registration, std::move(local_cloud_info), proxy_cache,
      registered_through_heartbeat));
  write_lock.Commit();
  return ts_descriptor;
}

}  // namespace yb::master
