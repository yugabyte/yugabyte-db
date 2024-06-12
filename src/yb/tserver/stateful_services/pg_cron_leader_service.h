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

#include <shared_mutex>

#include "yb/tserver/stateful_services/stateful_service_base.h"

namespace yb {
namespace stateful_service {
class PgCronLeaderService : public StatefulServiceBase {
 public:
  PgCronLeaderService(
      std::function<void(MonoTime)> set_cron_leader_lease_fn,
      const std::shared_future<client::YBClient*>& client_future);

 protected:
  void Activate() override;
  void Deactivate() override;
  uint32 PeriodicTaskIntervalMs() const override;
  Result<bool> RunPeriodicTask() override EXCLUDES(mutex_);
  void DrainForDeactivation() override {}

 private:
  void RefreshLeaderLease() EXCLUDES(mutex_);

  std::function<void(MonoTime)> set_cron_leader_lease_fn_;

  std::shared_mutex mutex_;
  // The time from which the leader can be active.
  MonoTime leader_activate_time_ GUARDED_BY(mutex_) = MonoTime::kUninitialized;
};

}  // namespace stateful_service
}  // namespace yb
