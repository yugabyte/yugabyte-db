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
#include "yb/master/master_fwd.h"

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
 * PG catalog are automatically rolled-back. This class helps perform similar rollback for the
 * DocDB schema upon transaction failure.
 * When a DDL transaction modifies the DocDB schema, the transaction metadata, and the current
 * schema of the table is stored in the SysTablesEntryPB. At this point, future DDL operations on
 * this table are blocked until verification is complete. A poller is scheduled through this
 * class. It monitors whether the transaction is complete. Once the transaction is detected to
 * be complete, it compares the PG catalog and the DocDB schema and finds whether the transaction
 * is a success or a failure.
 * Based on whether the transaction was a success or failure, the CatalogManager will effect
 * rollback or roll-forward of the DocDB schema.
 *
 * Note that the above protocol introduces eventual consistency between the two types of metadata.
 * However this mostly not affect correctness because of the following two properties:
 * 1) When inconsistent, the DocDB schema always has more columns/constraints than PG schema.
 * 2) Clients always use the PG schema (which is guaranteed to not return uncommitted state)
 * to prepare their read/write requests.
 *
 * These two properties ensure that we can't have orphaned data or failed integrity checks
 * or use DDL entities created by uncommitted transactions.
 */

class YsqlTransactionDdl {
 public:
  struct PgColumnFields {
    // Order determines the order in which the columns were created. This is equal to the
    // 'attnum' field in the pg_attribute table in PG catalog.
    int order;
    std::string attname;

    PgColumnFields(int attnum, std::string name) : order(attnum), attname(name) {}
  };

  YsqlTransactionDdl(
      const SysCatalogTable* sys_catalog, std::shared_future<client::YBClient*> client_future,
      ThreadPool* thread_pool)
      : sys_catalog_(sys_catalog), client_future_(std::move(client_future)),
        thread_pool_(thread_pool) {}

  ~YsqlTransactionDdl();

  void set_thread_pool(yb::ThreadPool* thread_pool) {
    thread_pool_ = thread_pool;
  }

  void VerifyTransaction(const TransactionMetadata& transaction,
                         scoped_refptr<TableInfo> table,
                         bool has_ysql_ddl_txn_state,
                         std::function<Status(bool /* is_success */)> complete_callback);

  Result<bool> PgEntryExists(const TableId& tableId,
                             PgOid entry_oid,
                             boost::optional<PgOid> relfilenode_oid);
  Status PgEntryExistsWithReadTime(
      const TableId& tableId,
      PgOid entry_oid,
      boost::optional<PgOid>
          relfilenode_oid,
      const ReadHybridTime& read_time,
      bool* result,
      HybridTime* read_restart_ht);

  Result<bool> PgSchemaChecker(const scoped_refptr<TableInfo>& table);
  Status PgSchemaCheckerWithReadTime(
      const scoped_refptr<TableInfo>& table,
      const ReadHybridTime& read_time,
      bool* result,
      HybridTime* read_restart_ht);

 protected:
  void TransactionReceived(const TransactionMetadata& transaction,
                           scoped_refptr<TableInfo> table,
                           bool has_ysql_ddl_txn_state,
                           std::function<Status(bool)> complete_callback,
                           Status txn_status,
                           const tserver::GetTransactionStatusResponsePB& response);

  bool MatchPgDocDBSchemaColumns(const scoped_refptr<TableInfo>& table,
                                 const Schema& schema,
                                 const std::vector<YsqlTransactionDdl::PgColumnFields>& pg_cols);

  Result<std::vector<PgColumnFields>> ReadPgAttribute(scoped_refptr<TableInfo> table);
  Status ReadPgAttributeWithReadTime(
      scoped_refptr<TableInfo> table,
      const ReadHybridTime& read_time,
      std::vector<PgColumnFields>* pg_cols,
      HybridTime* read_restart_ht);

  // Scan table 'pg_table_id' for all rows that satisfy the SQL filter
  // 'WHERE old_col_name = oid_value'. Each returned row contains the columns specified in
  // 'col_names'.
  Result<std::unique_ptr<docdb::YQLRowwiseIteratorIf>> GetPgCatalogTableScanIterator(
      const PgTableReadData& read_data,
      PgOid oid_value,
      const dockv::ReaderProjection& projection,
      RequestScope* request_scope);

  const SysCatalogTable* sys_catalog_;
  std::shared_future<client::YBClient*> client_future_;
  ThreadPool* thread_pool_;
  rpc::Rpcs rpcs_;
};

}  // namespace master
}  // namespace yb
