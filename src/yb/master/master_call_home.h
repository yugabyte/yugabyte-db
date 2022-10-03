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

#include "yb/master/master.h"
#include "yb/server/call_home.h"

namespace yb {

namespace master {
class Master;
class MasterTest_TestCallHome_Test;

class MasterCallHome : public CallHome {
 public:
  explicit MasterCallHome(Master* server);
  ~MasterCallHome() {}

 private:
  bool SkipCallHome() override;
};

class MasterCollector : public Collector {
 public:
  using Collector::Collector;

 protected:
  inline Master* master() { return down_cast<Master*>(server_); }
};

}  // namespace master

}  // namespace yb
