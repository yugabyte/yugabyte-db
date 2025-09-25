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

#include "yb/common/common_net.h"

namespace yb {

HostPortPB MakeHostPortPB(std::string&& host, uint32_t port) {
  HostPortPB result;
  result.set_host(std::move(host));
  result.set_port(port);
  return result;
}


CloudInfoPB MakeCloudInfoPB(std::string&& cloud, std::string&& region, std::string&& zone) {
  CloudInfoPB result;
  *result.mutable_placement_cloud() = std::move(cloud);
  *result.mutable_placement_region() = std::move(region);
  *result.mutable_placement_zone() = std::move(zone);
  return result;
}

}  // namespace yb
