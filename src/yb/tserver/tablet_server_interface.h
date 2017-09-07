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

#ifndef YB_TSERVER_TABLET_SERVER_INTERFACE_H
#define YB_TSERVER_TABLET_SERVER_INTERFACE_H

#include "yb/server/clock.h"
#include "yb/tserver/scanners.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/metrics.h"

namespace yb {
namespace tserver {

class TabletServerIf {
 public:

  virtual ~TabletServerIf() {}

  virtual TSTabletManager* tablet_manager() = 0;

  virtual ScannerManager* scanner_manager() = 0;

  virtual server::Clock* Clock() = 0;
  virtual const scoped_refptr<MetricEntity>& MetricEnt() const = 0;
};

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_TABLET_SERVER_INTERFACE_H
