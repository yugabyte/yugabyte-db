//--------------------------------------------------------------------------------------------------
// Copyright (c) Yugabyte, Inc.
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
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_PLACEMENT_INFO_H
#define YB_COMMON_PLACEMENT_INFO_H

#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "yb/common/ql_value.h"

#include "yb/common/common_net.pb.h"

#include "yb/util/result.h"

namespace yb {

// enum depicting the locality level of two PeerMessageQueue::TrackedPeer s'.
YB_DEFINE_ENUM(LocalityLevel, (kNone)(kRegion)(kZone));

class PlacementInfoConverter {
 public:
  struct PlacementInfo {
    string cloud = "";
    string region = "";
    string zone = "";
    int min_num_replicas = 0;
    int leader_preference = 0;
  };

  struct Placement {
    std::vector<PlacementInfo> placement_infos = {};
    int num_replicas = 0;
  };

  static Result<Placement> FromString(const std::string& placement);

  static Result<Placement> FromQLValue(const std::vector<std::string>& placement);

  // Returns the locality level for given CloudInfoPB references.
  static LocalityLevel GetLocalityLevel(
      const CloudInfoPB& src_cloud_info, const CloudInfoPB& dest_cloud_info);

 private:
  static Result<Placement> FromJson(const std::string& placement_str,
                                    const rapidjson::Document& placement);
};

} // namespace yb

#endif // YB_COMMON_PLACEMENT_INFO_H
