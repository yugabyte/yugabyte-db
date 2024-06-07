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

#include "yb/util/ev_util.h"

#include "yb/util/debug-util.h"

namespace yb {

void EvTimerHolder::Init(const ev::loop_ref& loop) {
  auto* timer = timer_.get();
  LOG_IF(DFATAL, timer) << "Timer " << timer << " has been already initialized " << this;
  timer_.reset(timer = new ev::timer());
  timer->set(loop);
  VLOG(3) << "Initialized ev::timer " << timer << " holder " << this;
  VLOG(5) << GetStackTrace();
}

void EvTimerHolder::Shutdown() {
  VLOG(3) << "EvTimerHolder::Shutdown " << this;
  VLOG(5) << GetStackTrace();
  std::unique_ptr<ev::timer> timer(timer_.release());
  if (timer) {
    VLOG(3) << "EvTimerHolder::Shutdown - stopping timer " << timer.get();
    timer->stop();
  }
}


} // namespace yb
