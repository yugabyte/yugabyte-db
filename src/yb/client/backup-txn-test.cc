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

#include "yb/client/txn-test-base.h"

#include "yb/master/master_backup.proxy.h"

namespace yb {
namespace client {

class BackupTxnTest : public TransactionTestBase {
 protected:
  void SetUp() override {
    SetIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);
    TransactionTestBase::SetUp();
  }

  master::MasterBackupServiceProxy MakeBackupServiceProxy() {
    return master::MasterBackupServiceProxy(
        &client_->proxy_cache(), cluster_->leader_mini_master()->bound_rpc_addr());
  }
};

TEST_F(BackupTxnTest, CreateSnapshot) {
  ASSERT_NO_FATALS(WriteData());

  auto backup_service_proxy = MakeBackupServiceProxy();

  master::CreateSnapshotRequestPB req;
  req.set_transaction_aware(true);
  auto id = req.add_tables();
  id->set_table_id(table_.table()->id());
  master::CreateSnapshotResponsePB resp;
  rpc::RpcController controller;
  ASSERT_OK(backup_service_proxy.CreateSnapshot(req, &resp, &controller));
  // TODO(txn_backup) verify snapshot
}

} // namespace client
} // namespace yb
