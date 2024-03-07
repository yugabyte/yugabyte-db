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

#include "yb/master/clone/clone_state_entity.h"

#include <optional>

#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/master/catalog_entity_info.pb.h"

namespace yb::master {

void CloneStateInfo::Load(const SysCloneStatePB& metadata) {
  MetadataCowWrapper<PersistentCloneStateInfo>::Load(metadata);
}

CloneStateInfo::CloneStateInfo(std::string id):
    clone_request_id_(std::move(id)) {}

}  // namespace yb::master
