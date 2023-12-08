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

#include <memory>
#include <optional>
#include <string>

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/version.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/pg_types.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_util_fwd.h"
#include "yb/tserver/pg_client.fwd.h"

#include "yb/util/enums.h"
#include "yb/util/lw_function.h"
#include "yb/util/monotime.h"
#include "yb/util/ref_cnt_buffer.h"

#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

struct DdlMode {
  bool has_docdb_schema_changes{false};
  std::optional<uint32_t> silently_altered_db;

  std::string ToString() const;
  void ToPB(tserver::PgFinishTransactionRequestPB_DdlModePB* dest) const;
};

#define YB_PG_CLIENT_SIMPLE_METHODS \
    (AlterDatabase)(AlterTable) \
    (CreateDatabase)(CreateReplicationSlot)(CreateTable)(CreateTablegroup) \
    (DropDatabase)(DropReplicationSlot)(DropTablegroup)(TruncateTable)

struct PerformResult {
  Status status;
  ReadHybridTime catalog_read_time;
  rpc::CallResponsePtr response;
  HybridTime used_in_txn_limit;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(status, catalog_read_time, used_in_txn_limit);
  }
};

struct TableKeyRangesWithHt {
  boost::container::small_vector<RefCntSlice, 2> encoded_range_end_keys;
  HybridTime current_ht;
};

struct PerformData;

class PerformExchangeFuture {
 public:
  PerformExchangeFuture() = default;
  explicit PerformExchangeFuture(std::shared_ptr<PerformData> data)
      : data_(std::move(data)) {}

  PerformExchangeFuture(PerformExchangeFuture&& rhs) noexcept : data_(std::move(rhs.data_)) {
  }

  PerformExchangeFuture& operator=(PerformExchangeFuture&& rhs) noexcept {
    data_ = std::move(rhs.data_);
    return *this;
  }

  bool valid() const {
    return data_ != nullptr;
  }

  void wait() const;
  bool ready() const;

  PerformResult get();

 private:
  std::shared_ptr<PerformData> data_;
  mutable std::optional<PerformResult> value_;
};

using PerformResultFuture = std::variant<std::future<PerformResult>, PerformExchangeFuture>;

void Wait(const PerformResultFuture& future);
bool Ready(const std::future<PerformResult>& future);
bool Ready(const PerformExchangeFuture& future);
bool Ready(const PerformResultFuture& future);
bool Valid(const PerformResultFuture& future);
PerformResult Get(PerformResultFuture* future);

class PgClient {
 public:
  PgClient();
  ~PgClient();

  Status Start(rpc::ProxyCache* proxy_cache,
               rpc::Scheduler* scheduler,
               const tserver::TServerSharedObject& tserver_shared_object,
               std::optional<uint64_t> session_id,
               const YBCAshMetadata* ash_metadata,
               bool* is_ash_metadata_set);

  void Shutdown();

  void SetTimeout(MonoDelta timeout);

  uint64_t SessionID() const;

  Result<PgTableDescPtr> OpenTable(
      const PgObjectId& table_id, bool reopen, CoarseTimePoint invalidate_cache_time);

  Result<client::VersionedTablePartitionList> GetTablePartitionList(const PgObjectId& table_id);

  Status FinishTransaction(Commit commit, const std::optional<DdlMode>& ddl_mode = {});

  Result<master::GetNamespaceInfoResponsePB> GetDatabaseInfo(PgOid oid);

  Result<std::pair<PgOid, PgOid>> ReserveOids(PgOid database_oid, PgOid next_oid, uint32_t count);

  Result<PgOid> GetNewObjectId(PgOid db_oid);

  Result<bool> IsInitDbDone();

  Result<uint64_t> GetCatalogMasterVersion();

  Status CreateSequencesDataTable();

  Result<client::YBTableName> DropTable(
      tserver::PgDropTableRequestPB* req, CoarseTimePoint deadline);

  Result<int> WaitForBackendsCatalogVersion(
      tserver::PgWaitForBackendsCatalogVersionRequestPB* req, CoarseTimePoint deadline);
  Status BackfillIndex(tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline);

  Status GetIndexBackfillProgress(const std::vector<PgObjectId>& index_ids,
                                  uint64_t** backfill_statuses);

  Result<yb::tserver::PgGetLockStatusResponsePB> GetLockStatusData(
      const std::string& table_id, const std::string& transaction_id);

  Result<int32> TabletServerCount(bool primary_only);

  Result<client::TabletServersInfo> ListLiveTabletServers(bool primary_only);

  Status SetActiveSubTransaction(
      SubTransactionId id, tserver::PgPerformOptionsPB* options);
  Status RollbackToSubTransaction(SubTransactionId id, tserver::PgPerformOptionsPB* options);

  Status ValidatePlacement(const tserver::PgValidatePlacementRequestPB* req);

  Result<client::TableSizeInfo> GetTableDiskSize(const PgObjectId& table_oid);

  Status InsertSequenceTuple(int64_t db_oid,
                             int64_t seq_oid,
                             uint64_t ysql_catalog_version,
                             bool is_db_catalog_version_mode,
                             int64_t last_val,
                             bool is_called);

  Result<bool> UpdateSequenceTuple(int64_t db_oid,
                                   int64_t seq_oid,
                                   uint64_t ysql_catalog_version,
                                   bool is_db_catalog_version_mode,
                                   int64_t last_val,
                                   bool is_called,
                                   std::optional<int64_t> expected_last_val,
                                   std::optional<bool> expected_is_called);

  Result<std::pair<int64_t, int64_t>> FetchSequenceTuple(int64_t db_oid,
                                                         int64_t seq_oid,
                                                         uint64_t ysql_catalog_version,
                                                         bool is_db_catalog_version_mode,
                                                         uint32_t fetch_count,
                                                         int64_t inc_by,
                                                         int64_t min_value,
                                                         int64_t max_value,
                                                         bool cycle);

  Result<std::pair<int64_t, bool>> ReadSequenceTuple(int64_t db_oid,
                                                     int64_t seq_oid,
                                                     uint64_t ysql_catalog_version,
                                                     bool is_db_catalog_version_mode);

  Status DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid);

  Status DeleteDBSequences(int64_t db_oid);

  PerformResultFuture PerformAsync(
      tserver::PgPerformOptionsPB* options,
      PgsqlOps* operations);

  Result<bool> CheckIfPitrActive();

  Result<bool> IsObjectPartOfXRepl(const PgObjectId& table_id);

  Result<TableKeyRangesWithHt> GetTableKeyRanges(
      const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
      uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length,
      uint64_t read_time_serial_no);

  Result<tserver::PgGetTserverCatalogVersionInfoResponsePB> GetTserverCatalogVersionInfo(
      bool size_only, uint32_t db_oid);

  Result<tserver::PgListReplicationSlotsResponsePB> ListReplicationSlots();

  Result<tserver::PgGetReplicationSlotStatusResponsePB> GetReplicationSlotStatus(
      const ReplicationSlotName& slot_name);

  Result<tserver::PgActiveSessionHistoryResponsePB> ActiveSessionHistory();

  using ActiveTransactionCallback = LWFunction<Status(
      const tserver::PgGetActiveTransactionListResponsePB_EntryPB&, bool is_last)>;
  Status EnumerateActiveTransactions(
      const ActiveTransactionCallback& callback, bool for_current_session_only = false) const;

#define YB_PG_CLIENT_SIMPLE_METHOD_DECLARE(r, data, method) \
  Status method(                             \
      tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
      CoarseTimePoint deadline);

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_SIMPLE_METHOD_DECLARE, ~, YB_PG_CLIENT_SIMPLE_METHODS);

  Status CancelTransaction(const unsigned char* transaction_id);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::pggate
