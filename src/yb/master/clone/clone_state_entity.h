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

#include "yb/common/entity_ids_types.h"
#include "yb/master/catalog_entity_base.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/sys_catalog.h"

namespace yb::master {

struct PersistentCloneStateInfo :
    public Persistent<SysCloneStatePB, SysRowEntryType::CLONE_STATE> {};

class CloneStateInfo : public RefCountedThreadSafe<CloneStateInfo>,
                       public MetadataCowWrapper<PersistentCloneStateInfo> {
 public:
  explicit CloneStateInfo(std::string id);

  virtual const std::string& id() const override { return clone_request_id_; };

  void Load(const SysCloneStatePB& metadata) override;

 private:
  friend class RefCountedThreadSafe<CloneStateInfo>;
  ~CloneStateInfo() = default;

  // The ID field is used in the sys_catalog table.
  const std::string clone_request_id_;

  DISALLOW_COPY_AND_ASSIGN(CloneStateInfo);
};

DECLARE_MULTI_INSTANCE_LOADER_CLASS(CloneState, std::string, SysCloneStatePB);

} // namespace yb::master
