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

#pragma once

#include "yb/server/call_home.h"

namespace yb {

namespace master {

// Call-home for a master. The collectors read the cluster through the master's own public RPCs,
// so they need nothing from how the master is implemented.
class MasterCallHome : public CallHome {
 public:
  explicit MasterCallHome(server::RpcAndWebServerBase* server);
  ~MasterCallHome() {}

 private:
  bool SkipCallHome() override;
};

}  // namespace master

}  // namespace yb
