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

#include "yb/client/client.h"

#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/master/master_defaults.h"

#include "yb/tserver/ysql_advisory_lock_table.h"

DECLARE_bool(ysql_yb_enable_advisory_locks);

namespace yb::tserver {
namespace {

void SetLockId(
    PgsqlAdvisoryLockPB& lock, uint32_t db_oid, uint32_t class_oid,
    uint32_t objid, uint32_t objsubid) {
  lock.add_lock_partition_column_values()->mutable_value()->set_uint32_value(db_oid);
  lock.add_lock_range_column_values()->mutable_value()->set_uint32_value(class_oid);
  lock.add_lock_range_column_values()->mutable_value()->set_uint32_value(objid);
  lock.add_lock_range_column_values()->mutable_value()->set_uint32_value(objsubid);
}

} // namespace

YsqlAdvisoryLocksTable::YsqlAdvisoryLocksTable(std::shared_future<client::YBClient*> client_future)
    : client_future_(std::move(client_future)) {}

Result<client::YBTablePtr> YsqlAdvisoryLocksTable::GetTable() {
  SCHECK(FLAGS_ysql_yb_enable_advisory_locks, NotSupported, "Advisory locks are not enabled");
  std::lock_guard<std::mutex> l(mutex_);
  if (!table_) {
    table_ = VERIFY_RESULT(client_future_.get()->OpenTable(client::YBTableName{
        YQL_DATABASE_CQL, master::kSystemNamespaceName, std::string(kPgAdvisoryLocksTableName)}));
  }
  return table_;
}

Result<client::YBPgsqlLockOpPtr> YsqlAdvisoryLocksTable::CreateLockOp(
    uint32_t db_oid, uint32_t class_oid, uint32_t objid, uint32_t objsubid,
    PgsqlLockRequestPB::PgsqlAdvisoryLockMode mode, bool wait, rpc::Sidecars* sidecars) {
  auto lock = client::YBPgsqlLockOp::NewLock(VERIFY_RESULT(GetTable()), sidecars);
  auto& lock_req = *lock->mutable_request();
  SetLockId(*lock_req.mutable_lock_id(), db_oid, class_oid, objid, objsubid);
  lock_req.set_lock_mode(mode);
  lock_req.set_wait(wait);
  return lock;
}

Result<client::YBPgsqlLockOpPtr> YsqlAdvisoryLocksTable::CreateUnlockOp(
    uint32_t db_oid, uint32_t class_oid, uint32_t objid, uint32_t objsubid,
    PgsqlLockRequestPB::PgsqlAdvisoryLockMode mode, rpc::Sidecars* sidecars) {
  auto unlock = client::YBPgsqlLockOp::NewUnlock(VERIFY_RESULT(GetTable()), sidecars);
  auto& unlock_req = *unlock->mutable_request();
  SetLockId(*unlock_req.mutable_lock_id(), db_oid, class_oid, objid, objsubid);
  unlock_req.set_lock_mode(mode);
  return unlock;
}

Result<client::YBPgsqlLockOpPtr> YsqlAdvisoryLocksTable::CreateUnlockAllOp(
    uint32_t db_oid, rpc::Sidecars* sidecars) {
  auto unlock = client::YBPgsqlLockOp::NewUnlock(VERIFY_RESULT(GetTable()), sidecars);
  unlock->mutable_request()->mutable_lock_id()->add_lock_partition_column_values()->mutable_value()
      ->set_uint32_value(db_oid);
  return unlock;
}

} // namespace yb::tserver
