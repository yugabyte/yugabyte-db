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

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/monotime.h"

namespace yb {
namespace ql {

// Processing could take a while, we are rescheduling it to our thread pool, if not yet
// running in it.
class Rescheduler {
 public:
  virtual bool NeedReschedule() = 0;
  virtual void Reschedule(rpc::ThreadPoolTask* task) = 0;
  virtual CoarseTimePoint GetDeadline() const = 0;

 protected:
  ~Rescheduler() {}
};

}  // namespace ql
}  // namespace yb
