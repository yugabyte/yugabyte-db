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

#include "yb/tserver/stateful_services/pg_cron_leader_service.h"

DECLARE_bool(enable_pg_cron);

DEFINE_RUNTIME_uint32(pg_cron_leader_lease_sec, 60,
    "The time in seconds to hold the pg_cron leader lease.");

DEFINE_RUNTIME_uint32(pg_cron_leadership_refresh_sec, 10,
    "Frequency at which the leadership is revalidated. This should be less than "
    "pg_cron_leader_lease_sec");

DECLARE_uint64(max_clock_skew_usec);

namespace {
void ValidateLeadershipRefreshSec() {
  LOG_IF(FATAL, FLAGS_pg_cron_leadership_refresh_sec >= FLAGS_pg_cron_leader_lease_sec)
      << "Invalid value " << FLAGS_pg_cron_leadership_refresh_sec
      << " for 'pg_cron_leadership_refresh_sec'. Value should be less than "
         "pg_cron_leader_lease_sec "
      << FLAGS_pg_cron_leader_lease_sec;
}
}  // namespace

REGISTER_CALLBACK(pg_cron_leadership_refresh_sec, "pg_cron_leadership_refresh_sec",
    ValidateLeadershipRefreshSec)

namespace yb {
namespace stateful_service {

PgCronLeaderService::PgCronLeaderService(
    std::function<void(MonoTime)> set_cron_leader_lease_fn,
    const std::shared_future<client::YBClient*>& client_future)
    : StatefulServiceBase(StatefulServiceKind::PG_CRON_LEADER, client_future),
      set_cron_leader_lease_fn_(std::move(set_cron_leader_lease_fn)) {}

void PgCronLeaderService::Activate() {
  if (!FLAGS_enable_pg_cron) {
    return;
  }

  LOG_WITH_FUNC(INFO) << "Activated";

  auto leader_term = GetLeaderTerm();

  std::lock_guard lock(mutex_);
  DCHECK(!leader_activate_time_.Initialized());

  if (leader_term.ok()) {
    if (*leader_term == 1) {
      VLOG_WITH_FUNC(1) << "Leader for term 1. Activating immediately";
      leader_activate_time_ = MonoTime::Min();
    } else {
      leader_activate_time_ =
          MonoTime::Now() + MonoDelta::FromMicroseconds(
                                FLAGS_pg_cron_leader_lease_sec + 2 * FLAGS_max_clock_skew_usec);
      LOG_WITH_FUNC(INFO) << "Waiting until " << leader_activate_time_.ToFormattedString()
                          << " for lease of the old leader to expire";
    }
  } else {
    LOG_WITH_FUNC(INFO) << "Lost leadership before activation " << leader_term.status();
  }
}

void PgCronLeaderService::Deactivate() {
  std::lock_guard lock(mutex_);
  // Break the lease immediately. This is best effort but still safe since new leader will not
  // activate until the old leader lease has fully expired.
  set_cron_leader_lease_fn_(MonoTime::kUninitialized);
  leader_activate_time_ = MonoTime::kUninitialized;

  LOG_WITH_FUNC(INFO) << "Deactivated";
}

uint32 PgCronLeaderService::PeriodicTaskIntervalMs() const {
  return FLAGS_pg_cron_leadership_refresh_sec * MonoTime::kMillisecondsPerSecond;
}

Result<bool> PgCronLeaderService::RunPeriodicTask() {
  RefreshLeaderLease();
  return true;
}

void PgCronLeaderService::RefreshLeaderLease() {
  // We could can lose the leadership at any time, so the sequence is important here.
  // Get the time, and then check if we are still the leader. If we are, then use the fetched time
  // to refresh the lease.
  const auto now = MonoTime::Now();

  auto leader_term = GetLeaderTerm();
  if (!leader_term.ok()) {
    LOG_WITH_FUNC(INFO) << "Not refreshing the lease since we failed to get a valid leader term: "
                        << leader_term.status();
    // If we really lost leadership, then Deactivate will get called.
    return;
  }

  SharedLock lock(mutex_);
  if (!leader_activate_time_.Initialized()) {
    LOG(DFATAL) << "Leader activate time not initialized.";
    return;
  }

  if (now < leader_activate_time_) {
    // Not yet time to set the lease.
    return;
  }

  // We are the leader. Renew the lease.
  const auto lease_end = now + MonoDelta::FromSeconds(FLAGS_pg_cron_leader_lease_sec);
  VLOG_WITH_FUNC(1) << "Setting leader lease to " << lease_end.ToFormattedString();
  set_cron_leader_lease_fn_(lease_end);
}

}  // namespace stateful_service
}  // namespace yb
