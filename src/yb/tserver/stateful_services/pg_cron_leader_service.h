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

#include <shared_mutex>

#include "yb/tserver/stateful_services/stateful_service_base.h"
#include "yb/tserver/stateful_services/pg_cron_leader_service.service.h"

namespace yb {

class PgCronTest;

namespace stateful_service {

constexpr char kPgCronIdColName[] = "id";
constexpr char kPgCronDataColName[] = "data";
const uint64_t kPgCronDataKey = 1;

class PgCronLeaderService : public StatefulRpcServiceBase<PgCronLeaderServiceIf> {
 public:
  PgCronLeaderService(
      std::function<void(MonoTime)> set_cron_leader_lease_fn,
      const scoped_refptr<MetricEntity>& metric_entity,
      const std::shared_future<client::YBClient*>& client_future);

  STATEFUL_SERVICE_IMPL_METHODS(PgCronSetLastMinute, PgCronGetLastMinute);

 protected:
  void Activate() override;
  void Deactivate() override;
  uint32 PeriodicTaskIntervalMs() const override;
  Result<bool> RunPeriodicTask() override EXCLUDES(mutex_);
  void DrainForDeactivation() override {}

 private:
  friend class ::yb::PgCronTest;
  void RefreshLeaderLease() EXCLUDES(mutex_);

  Status SetLastMinute(int64_t last_minute, CoarseTimePoint deadline);
  Result<int64_t> GetLastMinute(CoarseTimePoint deadline);

  static Result<int64_t> ExtractLastMinute(const QLValue& column_value);

  std::function<void(MonoTime)> set_cron_leader_lease_fn_;

  std::shared_mutex mutex_;
  // The time from which the leader can be active.
  MonoTime leader_activate_time_ GUARDED_BY(mutex_) = MonoTime::kUninitialized;
};

}  // namespace stateful_service
}  // namespace yb
