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

#include <functional>
#include <memory>

#include "yb/client/client_fwd.h"

#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"
#include "yb/common/transaction.h"

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_tasks.h"
#include "yb/master/master_fwd.h"
#include "yb/master/multi_step_monitored_task.h"

#include "yb/rpc/rpc.h"

#include "yb/util/status_fwd.h"
#include "yb/util/threadpool.h"

namespace yb {
namespace tserver {
class GetTransactionStatusResponsePB;
}

namespace master {
/*
 * Currently the metadata for YSQL Tables is stored in both the PG catalog and DocDB schema.
 * This class helps maintain consistency between the two schemas. When a DDL transaction fails,
 * since the PG catalog is modified using the DocDB transactions framework, the changes to the
 * PG catalog are automatically rolled-back. This tasks in this file help perform similar rollback
 * for the DocDB schema upon transaction failure.
 * When a DDL transaction modifies the DocDB schema, the transaction metadata, and the current
 * schema of the table is stored in the SysTablesEntryPB. At this point, future DDL operations on
 * this table are blocked until verification is complete. A poller is scheduled to monitor whether
 * the transaction is complete. Once the transaction is detected to be complete, it compares the PG
 * catalog and the DocDB schema and finds whether the transaction is a success or a failure.
 * Based on whether the transaction was a success or failure, the CatalogManager will effect
 * rollback or roll-forward of the DocDB schema.
 *
 * Note that the above protocol introduces eventual consistency between the two types of metadata.
 * However this does not affect correctness because of the following two properties:
 * 1) When inconsistent, the DocDB schema always has more columns/constraints than PG schema.
 * 2) Clients always use the PG schema (which is guaranteed to not return uncommitted state)
 * to prepare their read/write requests.
 *
 * These two properties ensure that we can't have orphaned data or failed integrity checks
 * or use DDL entities created by uncommitted transactions.
 */

// Helper class that encapsulates the logic to poll the transaction status.
class PollTransactionStatusBase {
 public:
  PollTransactionStatusBase(
    const TransactionMetadata& transaction,
    std::shared_future<client::YBClient*> client_future)
    : transaction_(transaction),
      client_future_(std::move(client_future)) {}

  virtual ~PollTransactionStatusBase();

 protected:
  Status VerifyTransaction();
  virtual void TransactionPending() = 0;
  virtual void FinishPollTransaction(Status s) = 0;

  TransactionMetadata transaction_;

 private:
  void TransactionReceived(Status txn_status,
                           const tserver::GetTransactionStatusResponsePB& response);

  std::shared_future<client::YBClient*> client_future_;
  rpc::Rpcs rpcs_;
};

class NamespaceVerificationTask : public MultiStepNamespaceTaskBase,
                                  public PollTransactionStatusBase {
 public:
  static void CreateAndStartTask(
      CatalogManager& catalog_manager,
      scoped_refptr<NamespaceInfo> ns,
      const TransactionMetadata& transaction,
      std::function<void(Result<bool>)> complete_callback,
      SysCatalogTable* sys_catalog,
      std::shared_future<client::YBClient*> client_future,
      rpc::Messenger& messenger,
      const LeaderEpoch& epoch);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kNamespaceVerification;
  }

  std::string type_name() const override { return "Namespace verification"; }

  std::string description() const override {
    return Format("TableSchemaVerificationTask for $0", namespace_info_.ToString());
  };

  ~NamespaceVerificationTask() = default;

  NamespaceVerificationTask(
    CatalogManager& catalog_manager,
    scoped_refptr<NamespaceInfo> ns,
    const TransactionMetadata& transaction,
    std::function<void(Result<bool>)> complete_callback,
    SysCatalogTable* sys_catalog,
    std::shared_future<client::YBClient*> client_future,
    rpc::Messenger& messenger,
    const LeaderEpoch& epoch);

 private:
  Status FirstStep() override;
  void TransactionPending() override;
  Status ValidateRunnable() override;
  void FinishPollTransaction(Status s) override;
  Status CheckNsExists(Status status);

  SysCatalogTable& sys_catalog_;
  bool entry_exists_ = false;
};

class TableSchemaVerificationTask : public MultiStepTableTaskBase,
                                    public PollTransactionStatusBase {
 public:
  static void CreateAndStartTask(
    CatalogManager& catalog_manager,
    scoped_refptr<TableInfo> table,
    const TransactionMetadata& transaction,
    std::function<void(Result<bool>)> complete_callback,
    SysCatalogTable* sys_catalog,
    std::shared_future<client::YBClient*> client_future,
    rpc::Messenger& messenger,
    const LeaderEpoch& epoch,
    bool ddl_atomicity_enabled);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::TableSchemaVerification;
  }

  std::string type_name() const override { return "TableSchemaVerificationTask"; }

  std::string description() const override {
    return Format("TableSchemaVerificationTask for $0", table_info_->ToString());
  };

  ~TableSchemaVerificationTask() = default;

  TableSchemaVerificationTask(
    CatalogManager& catalog_manager,
    scoped_refptr<TableInfo> table,
    const TransactionMetadata& transaction,
    std::function<void(Result<bool>)> complete_callback,
    SysCatalogTable* sys_catalog,
    std::shared_future<client::YBClient*> client_future,
    rpc::Messenger& messenger,
    const LeaderEpoch& epoch,
    bool ddl_atomicity_enabled);

 private:
  Status FirstStep() override;
  void TransactionPending() override;
  Status ValidateRunnable() override;
  Status CheckTableExists(Status s);
  Status CompareSchema(Status s);
  Status FinishTask(Result<bool> is_committed);
  void FinishPollTransaction(Status s) override;

  SysCatalogTable& sys_catalog_;
  bool ddl_atomicity_enabled_;
  bool is_committed_ = false;
};

}  // namespace master
}  // namespace yb
