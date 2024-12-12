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

#include <yb/tserver/ysql_advisory_lock_table.h>
#include "yb/client/yb_table_name.h"
#include "yb/client/client.h"
#include "yb/client/yb_op.h"
#include "yb/master/master_defaults.h"

DECLARE_bool(yb_enable_advisory_lock);

namespace yb {

namespace {

void SetLockId(PgsqlAdvisoryLockPB& lock, uint32_t db_oid, uint32_t class_oid,
               uint32_t objid, uint32_t objsubid) {
  lock.add_lock_partition_column_values()->mutable_value()->set_uint32_value(db_oid);
  lock.add_lock_range_column_values()->mutable_value()->set_uint32_value(class_oid);
  lock.add_lock_range_column_values()->mutable_value()->set_uint32_value(objid);
  lock.add_lock_range_column_values()->mutable_value()->set_uint32_value(objsubid);
}

} // namespace

YsqlAdvisoryLocksTable::YsqlAdvisoryLocksTable(client::YBClient& client)
    : client_(client) {}

YsqlAdvisoryLocksTable::~YsqlAdvisoryLocksTable() {
}

Result<client::YBTablePtr> YsqlAdvisoryLocksTable::GetTable() {
  SCHECK(FLAGS_yb_enable_advisory_lock, NotSupported, "Advisory locks are not enabled");
  std::lock_guard<std::mutex> l(mutex_);
  if (!table_) {
    static const client::YBTableName table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kPgAdvisoryLocksTableName);
    table_ = VERIFY_RESULT(client_.OpenTable(table_name));
  }
  return table_;
}

Result<client::YBPgsqlLockOpPtr> YsqlAdvisoryLocksTable::CreateLockOp(
    uint32_t db_oid, uint32_t class_oid, uint32_t objid, uint32_t objsubid,
    PgsqlLockRequestPB::PgsqlAdvisoryLockMode mode, bool wait, rpc::Sidecars* sidecars) {
  auto lock = client::YBPgsqlLockOp::NewLock(VERIFY_RESULT(GetTable()), sidecars);
  SetLockId(*lock->mutable_request()->mutable_lock_id(), db_oid, class_oid, objid, objsubid);
  lock->mutable_request()->set_lock_mode(mode);
  lock->mutable_request()->set_wait(wait);
  return lock;
}

Result<client::YBPgsqlLockOpPtr> YsqlAdvisoryLocksTable::CreateUnlockOp(
    uint32_t db_oid, uint32_t class_oid, uint32_t objid, uint32_t objsubid,
    PgsqlLockRequestPB::PgsqlAdvisoryLockMode mode, rpc::Sidecars* sidecars) {
  auto unlock = client::YBPgsqlLockOp::NewUnlock(VERIFY_RESULT(GetTable()), sidecars);
  SetLockId(*unlock->mutable_request()->mutable_lock_id(), db_oid, class_oid, objid, objsubid);
  unlock->mutable_request()->set_lock_mode(mode);
  return unlock;
}

Result<client::YBPgsqlLockOpPtr> YsqlAdvisoryLocksTable::CreateUnlockAllOp(
    uint32_t db_oid, rpc::Sidecars* sidecars) {
  auto unlock = client::YBPgsqlLockOp::NewUnlock(VERIFY_RESULT(GetTable()), sidecars);
  unlock->mutable_request()->mutable_lock_id()->add_lock_partition_column_values()->mutable_value()
      ->set_uint32_value(db_oid);
  return unlock;
}

} // namespace yb
