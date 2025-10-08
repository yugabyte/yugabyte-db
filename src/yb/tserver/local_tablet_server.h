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

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace tserver {

class LocalTabletServer {
 public:
  LocalTabletServer() = default;
  virtual ~LocalTabletServer() = default;

  virtual Status GetTabletStatus(const GetTabletStatusRequestPB* req,
                                         GetTabletStatusResponsePB* resp) const = 0;

  virtual bool LeaderAndReady(const TabletId& tablet_id, bool allow_stale = false) const = 0;
};

} // namespace tserver
} // namespace yb
