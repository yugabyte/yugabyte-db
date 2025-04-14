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

#include "yb/util/metrics_fwd.h"
#include "yb/tserver/tserver_fwd.h"

namespace yb::tserver {

// This class updates disk size metrics at the database level, accounting for hardlinks.
class TsDataSizeMetrics {
 public:
  explicit TsDataSizeMetrics(TSTabletManager* tablet_manager);
  void Update();

 private:
  scoped_refptr<yb::AtomicGauge<uint64_t>> ts_data_size_metric_;
  scoped_refptr<yb::AtomicGauge<uint64_t>> ts_active_data_size_metric_;
  TSTabletManager* tablet_manager_;
};

} // namespace yb::tserver
