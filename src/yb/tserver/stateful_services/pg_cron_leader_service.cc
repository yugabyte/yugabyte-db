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

#include "yb/tserver/stateful_services/pg_cron_leader_service.h"
#include <rapidjson/document.h>

#include "yb/client/session.h"
#include "yb/client/yb_op.h"

#include "yb/common/jsonb.h"
#include "yb/common/json_util.h"
#include "yb/common/ql_protocol.messages.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/util/flag_validators.h"

using namespace std::chrono_literals;

DECLARE_bool(enable_pg_cron);

DEFINE_RUNTIME_uint32(pg_cron_leader_lease_sec, 60,
    "The time in seconds to hold the pg_cron leader lease.");

DEFINE_RUNTIME_uint32(pg_cron_leadership_refresh_sec, 10,
    "Frequency at which the leadership is revalidated. This should be less than "
    "pg_cron_leader_lease_sec");

DEFINE_test_flag(bool, pg_cron_fail_setting_last_minute, false, "Fail setting the last minute");

DEFINE_validator(pg_cron_leadership_refresh_sec,
    FLAG_LT_FLAG_VALIDATOR(pg_cron_leader_lease_sec));

DEFINE_validator(pg_cron_leader_lease_sec,
    FLAG_GT_FLAG_VALIDATOR(pg_cron_leadership_refresh_sec));

DECLARE_uint64(max_clock_skew_usec);

namespace yb {
namespace stateful_service {

namespace {
constexpr uint32 kPgCronJsonVersion1 = 1;
constexpr char kPgCronJsonVersion[] = "version";
constexpr char kPgCronJsonLastMinute[] = "last_minute";
}  // namespace

PgCronLeaderService::PgCronLeaderService(
    std::function<void(MonoTime)> set_cron_leader_lease_fn,
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_future<client::YBClient*>& client_future)
    : StatefulRpcServiceBase(StatefulServiceKind::PG_CRON_LEADER, metric_entity, client_future),
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

Status PgCronLeaderService::SetLastMinute(int64_t last_minute, CoarseTimePoint deadline) {
  VLOG_WITH_FUNC(1) << YB_STRUCT_TO_STRING(last_minute);

  auto session = VERIFY_RESULT(GetYBSession(deadline));
  auto* table = VERIFY_RESULT(GetServiceTable());

  auto write_op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
  auto* const req = write_op->mutable_request();
  QLAddInt64HashValue(req, kPgCronDataKey);

  rapidjson::Document document;
  document.SetObject();
  document.AddMember(kPgCronJsonVersion, kPgCronJsonVersion1, document.GetAllocator());
  document.AddMember(kPgCronJsonLastMinute, last_minute, document.GetAllocator());

  common::Jsonb jsonb;
  RETURN_NOT_OK(jsonb.FromRapidJson(document));
  table->AddJsonbColumnValue(req, kPgCronDataColName, jsonb.MoveSerializedJsonb());

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session->TEST_ApplyAndFlush(std::move(write_op)), "Failed to update pg cron leader table");

  return Status::OK();
}

Result<int64_t> PgCronLeaderService::ExtractLastMinute(const QLValue& column_value) {
  SCHECK(column_value.value().has_jsonb_value(), IllegalState, "Column missing JSON value");

  common::Jsonb jsonb(column_value.value().jsonb_value());
  rapidjson::Document document;
  RETURN_NOT_OK(jsonb.ToRapidJson(&document));
  const auto version = VERIFY_RESULT(common::GetMemberAsUint(document, kPgCronJsonVersion));
  SCHECK_EQ(version, kPgCronJsonVersion1, IllegalState, "Unexpected JSONB version");

  return common::GetMemberAsUint64(document, kPgCronJsonLastMinute);
}

Result<int64_t> PgCronLeaderService::GetLastMinute(CoarseTimePoint deadline) {
  VLOG_WITH_FUNC(1);

  auto session = VERIFY_RESULT(GetYBSession(deadline));
  auto* table = VERIFY_RESULT(GetServiceTable());

  auto read_op = table->NewReadOp();
  auto* const read_req = read_op->mutable_request();
  QLAddInt64HashValue(read_req, kPgCronDataKey);
  table->AddColumns({kPgCronDataColName}, read_req);

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session->TEST_ApplyAndFlush(read_op), "Failed to read from pg cron leader table");

  const auto row_block = ql::RowsResult(read_op.get()).GetRowBlock();
  if (row_block->rows().empty()) {
    YB_LOG_EVERY_N_SECS(INFO, 10)
        << "Returning 0 as last time since no previous stored data was found";
    return 0;
  }

  auto& row_schema = row_block->schema();
  auto data_idx = row_schema.find_column(kPgCronDataColName);
  SCHECK_EQ(row_block->rows().size(), 1, IllegalState, "Expected exactly one row");
  const auto last_minute =
      VERIFY_RESULT(ExtractLastMinute(row_block->rows().front().column(data_idx)));

  VLOG(1) << "Retrieved last_minute: " << last_minute;

  return last_minute;
}

Status PgCronLeaderService::PgCronSetLastMinuteImpl(
    const PgCronSetLastMinuteRequestPB& req, PgCronSetLastMinuteResponsePB* resp,
    rpc::RpcContext& rpc) {
  VLOG_WITH_FUNC(3) << req.ShortDebugString();
  SCHECK_GT(req.last_minute(), 0, InvalidArgument, "Invalid time");

  SCHECK(
      !FLAGS_TEST_pg_cron_fail_setting_last_minute, InternalError,
      "Failing setting last minute for test");

  return SetLastMinute(req.last_minute(), rpc.GetClientDeadline());
}

Status PgCronLeaderService::PgCronGetLastMinuteImpl(
    const PgCronGetLastMinuteRequestPB& req, PgCronGetLastMinuteResponsePB* resp,
    rpc::RpcContext& rpc) {
  VLOG_WITH_FUNC(3) << req.ShortDebugString();

  auto last_minute = VERIFY_RESULT(GetLastMinute(rpc.GetClientDeadline()));
  resp->set_last_minute(last_minute);

  return Status::OK();
}

}  // namespace stateful_service
}  // namespace yb
