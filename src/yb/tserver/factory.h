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

#pragma once

#include "yb/tserver/ts_tablet_manager.h"

namespace yb {
namespace tserver {

class Factory {
 public:
  std::unique_ptr<TabletServer> CreateTabletServer(const TabletServerOptions& options) {
    return std::make_unique<TabletServer>(options);
  }

  std::unique_ptr<cqlserver::CQLServer> CreateCQLServer(
      const cqlserver::CQLServerOptions& options, IoService* io, tserver::TabletServer* tserver) {
    return std::make_unique<cqlserver::CQLServer>(options, io, tserver);
  }
};

} // namespace tserver
} // namespace yb
