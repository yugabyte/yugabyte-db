//
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
//

#include "yb/tserver/ysql_advisory_lock_table.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/pgsql_protocol.pb.h"

#include "yb/master/master_defaults.h"

#include "yb/util/status_format.h"

DECLARE_bool(ysql_yb_enable_advisory_locks);

using namespace std::literals;

namespace yb::tserver {
namespace {

void SetValue(QLExpressionPB& exp, uint32_t value) {
  exp.mutable_value()->set_uint32_value(value);
}

class LockIdHelper {
 public:
  explicit LockIdHelper(client::YBPgsqlLockOp& op)
      : lock_id_(*op.mutable_request()->mutable_lock_id()) {}

  const auto& PartitionColumn(uint32_t value) const {
    SetValue(*lock_id_.add_lock_partition_column_values(), value);
    return *this;
  }

  const auto& RangeColumn(uint32_t value) const {
    SetValue(*lock_id_.add_lock_range_column_values(), value);
    return *this;
  }

 private:
  PgsqlAdvisoryLockPB& lock_id_;
};

void SetLockId(
    client::YBPgsqlLockOp& op, const YsqlAdvisoryLocksTable::LockId& lock_id,
    AdvisoryLockTableSchemaVersion schema_version) {
  if (schema_version == AdvisoryLockTableSchemaVersion::kDbIdHash) {
    LockIdHelper(op)
        .PartitionColumn(lock_id.db_oid)
        .RangeColumn(lock_id.class_oid)
        .RangeColumn(lock_id.objid)
        .RangeColumn(lock_id.objsubid);
    return;
  }
  LockIdHelper(op)
      .PartitionColumn(lock_id.db_oid)
      .PartitionColumn(lock_id.class_oid)
      .PartitionColumn(lock_id.objid)
      .PartitionColumn(lock_id.objsubid);
}

auto MapMode(AdvisoryLockMode mode) {
  switch(mode) {
    case AdvisoryLockMode::LOCK_SHARE:
      return PgsqlLockRequestPB::PG_LOCK_SHARE;
    case AdvisoryLockMode::LOCK_EXCLUSIVE:
      return PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE;
    case AdvisoryLockMode::AdvisoryLockMode_INT_MIN_SENTINEL_DO_NOT_USE_:
    case AdvisoryLockMode::AdvisoryLockMode_INT_MAX_SENTINEL_DO_NOT_USE_:
      break;
  }
  FATAL_INVALID_ENUM_VALUE(AdvisoryLockMode, mode);
}

void CommonInit(
    client::YBPgsqlLockOp& op,
    const YsqlAdvisoryLocksTable::LockId& lock_id, AdvisoryLockMode mode,
    AdvisoryLockTableSchemaVersion schema_version) {
  SetLockId(op, lock_id, schema_version);
  op.mutable_request()->set_lock_mode(MapMode(mode));
}

} // namespace

YsqlAdvisoryLocksTable::YsqlAdvisoryLocksTable(std::shared_future<client::YBClient*> client_future)
    : client_future_(std::move(client_future)) {}

Result<client::YBTablePtr> YsqlAdvisoryLocksTable::GetTable() {
  std::lock_guard<std::mutex> l(mutex_);
  if (!table_) {
    table_ = VERIFY_RESULT(client_future_.get()->OpenTable(client::YBTableName{
        YQL_DATABASE_CQL, master::kSystemNamespaceName, std::string(kPgAdvisoryLocksTableName)}));
    schema_version_ = table_->schema().num_hash_key_columns() == 1
        ? AdvisoryLockTableSchemaVersion::kDbIdHash
        : AdvisoryLockTableSchemaVersion::kAllColsHash;
  }
  return table_;
}

Result<client::YBPgsqlLockOpPtr> YsqlAdvisoryLocksTable::MakeLockOp(
    const LockId& lock_id, AdvisoryLockMode mode, bool wait) {
  auto lock = client::YBPgsqlLockOp::NewLock(VERIFY_RESULT(GetTable()));
  CommonInit(*lock, lock_id, mode, schema_version_);
  auto& req = *lock->mutable_request();
  req.set_wait(wait);
  req.set_is_lock(true);
  return lock;
}

Result<client::YBPgsqlLockOpPtr> YsqlAdvisoryLocksTable::MakeUnlockOp(
    const LockId& lock_id, AdvisoryLockMode mode) {
  auto unlock = client::YBPgsqlLockOp::NewUnlock(VERIFY_RESULT(GetTable()));
  CommonInit(*unlock, lock_id, mode, schema_version_);
  return unlock;
}

Result<std::vector<client::YBOperationPtr>> YsqlAdvisoryLocksTable::MakeUnlockAllOps(
    uint32_t db_oid) {
  if (schema_version_ == AdvisoryLockTableSchemaVersion::kDbIdHash) {
    auto unlock = client::YBPgsqlLockOp::NewUnlock(VERIFY_RESULT(GetTable()));
    LockIdHelper(*unlock).PartitionColumn(db_oid);
    return std::vector<client::YBOperationPtr>{unlock};
  }
  auto tablet_ptrs = VERIFY_RESULT(LookupAllTablets(CoarseMonoClock::Now() + 5s));
  std::vector<client::YBOperationPtr> ops;
  ops.reserve(tablet_ptrs.size());
  for (const auto& remote_tablet : tablet_ptrs) {
    auto unlock = client::YBPgsqlLockOp::NewUnlock(VERIFY_RESULT(GetTable()));
    unlock->SetTablet(remote_tablet);
    ops.push_back(std::move(unlock));
  }
  return ops;
}

Result<std::vector<client::internal::RemoteTabletPtr>> YsqlAdvisoryLocksTable::LookupAllTablets(
    CoarseTimePoint deadline) {
  auto table = VERIFY_RESULT(GetTable());
  std::lock_guard<std::mutex> l(mutex_);
  if (!tablet_ptrs_.empty()) {
    return tablet_ptrs_;
  }
  tablet_ptrs_ = VERIFY_RESULT(
      client_future_.get()->LookupAllTabletsFuture(table, deadline).get());
  return tablet_ptrs_;
}

} // namespace yb::tserver
