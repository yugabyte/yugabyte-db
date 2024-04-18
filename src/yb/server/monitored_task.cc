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

#include "yb/server/monitored_task.h"

#include "yb/util/format.h"

namespace yb {
namespace server {

std::string MonitoredTask::ToString() const {
  return Format("{ type: $0 description: $1 }", type(), description());
}

Status RunnableMonitoredTask::BeforeSubmitToTaskPool() {
  return Status::OK();
}

Status RunnableMonitoredTask::OnSubmitFailure() {
  return Status::OK();
}

} // namespace server
} // namespace yb
