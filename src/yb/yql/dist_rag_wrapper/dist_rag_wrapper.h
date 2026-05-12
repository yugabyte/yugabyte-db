// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>
#include <string>

#include "yb/yql/process_wrapper/process_wrapper.h"
#include "yb/util/status.h"

namespace yb {

namespace tserver {
class TabletServer;
}  // namespace tserver

class DistRagServiceSupervisor : public ProcessSupervisor {
 public:
  explicit DistRagServiceSupervisor(tserver::TabletServer& tablet_server);

  virtual ~DistRagServiceSupervisor();

 protected:
  std::shared_ptr<ProcessWrapper> CreateProcessWrapper() override;
  std::string GetProcessName() override {
    return "Distributed RAG Service";
  }
  Status PrepareForStart() override;

 private:
  tserver::TabletServer& tablet_server_;
};

}  // namespace yb
