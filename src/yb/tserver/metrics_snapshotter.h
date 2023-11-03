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

#include <memory>

#include "yb/gutil/macros.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace tserver {

class TabletServer;
class TabletServerOptions;

class MetricsSnapshotter {
 public:
  MetricsSnapshotter(const TabletServerOptions& options, TabletServer* server);
  Status Start();
  Status Stop();

  ~MetricsSnapshotter();

 private:
  class Thread;
  std::unique_ptr<Thread> thread_;
  DISALLOW_COPY_AND_ASSIGN(MetricsSnapshotter);
};

} // namespace tserver
} // namespace yb
