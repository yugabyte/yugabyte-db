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

#include <string>

#include "yb/common/common_net.pb.h"

namespace yb {

HostPortPB MakeHostPortPB(std::string&& host, uint32_t port);

CloudInfoPB MakeCloudInfoPB(std::string&& cloud, std::string&& region, std::string&& zone);

bool IsCloudInfoEqual(const CloudInfoPB& lhs, const CloudInfoPB& rhs);

bool CloudInfoContainsCloudInfo(const CloudInfoPB& lhs, const CloudInfoPB& rhs);

bool PlacementInfoContainsCloudInfo(
    const PlacementInfoPB& placement_info, const CloudInfoPB& cloud_info);

// Returns true if the cloud_info (possibly a wildcard/prefix) matches any placement block.
// This is useful for validating preferred zones with wildcards.
bool CloudInfoMatchesPlacementInfo(
    const CloudInfoPB& cloud_info, const PlacementInfoPB& placement_info);

bool PlacementInfoSpansMultipleRegions(const PlacementInfoPB& placement_info);

bool PlacementInfoContainsPlacementInfo(const PlacementInfoPB& lhs, const PlacementInfoPB& rhs);

}  // namespace yb
