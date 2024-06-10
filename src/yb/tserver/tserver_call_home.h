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

#pragma once

#include "yb/server/call_home.h"
#include "yb/tserver/tablet_server.h"

namespace yb {

namespace tserver {

class TserverCallHome : public CallHome {
 public:
  explicit TserverCallHome(TabletServer* server);
  ~TserverCallHome() {}

 protected:
  bool SkipCallHome() override { return false; }
};

class TserverCollector : public Collector {
 public:
  using Collector::Collector;

 protected:
  inline TabletServer* tserver() { return down_cast<TabletServer*>(server_); }
};

}  // namespace tserver

}  // namespace yb
