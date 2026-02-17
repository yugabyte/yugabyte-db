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

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/version.hpp>

#include "yb/ash/pg_wait_state.h"

#include "yb/cdc/cdc_service.pb.h"
#include "yb/client/client_fwd.h"

#include "yb/common/pg_types.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/object_lock_shared_fwd.h"

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_util_fwd.h"
#include "yb/tserver/pg_client.fwd.h"

#include "yb/util/async_util.h"

#include "yb/util/lw_function.h"
#include "yb/util/monotime.h"
#include "yb/util/ref_cnt_buffer.h"

#include "yb/yql/pggate/pg_doc_metrics.h"
#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"

namespace yb::pggate {

struct DdlMode {
  bool has_docdb_schema_changes{false};
  std::optional<uint32_t> silently_altered_db;
  bool use_regular_transaction_block{true};

  std::string ToString() const;
  void ToPB(tserver::PgFinishTransactionRequestPB_DdlModePB* dest) const;
};

#define YB_PG_CLIENT_SIMPLE_METHODS \
    (AlterDatabase)(AlterTable) \
    (CreateDatabase)(CreateTable)(CreateTablegroup) \
    (DropDatabase)(DropReplicationSlot)(DropTablegroup)(TruncateTable) \
    (AcquireAdvisoryLock)(ReleaseAdvisoryLock)

struct PerformResult {
  Status status;
  ReadHybridTime catalog_read_time;
  rpc::CallResponsePtr response;
  HybridTime used_in_txn_limit;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(status, catalog_read_time, used_in_txn_limit);
  }
};

namespace pg_client::internal {

template <class Data>
struct ResultTypeResolver {
  using ResultType = Data::ResultType;
};

struct PerformData;

template <>
struct ResultTypeResolver<PerformData> {
  using ResultType = PerformResult;
};

template <class Data>
class ExchangeFuture {
 public:
  using Result = typename ResultTypeResolver<Data>::ResultType;

  ExchangeFuture();
  explicit ExchangeFuture(std::shared_ptr<Data> data);
  ExchangeFuture(ExchangeFuture&& rhs) noexcept;
  ExchangeFuture& operator=(ExchangeFuture&& rhs) noexcept;

  bool valid() const;
  void wait() const;
  bool ready() const;
  Result get();

 private:
  std::shared_ptr<Data> data_;
  mutable std::optional<Result> value_;
};

template <class Data>
class ResultFuture {
 public:
  using Result = typename ResultTypeResolver<Data>::ResultType;

  template <class... Args>
  ResultFuture(Args&&... args) : variant_(std::forward<Args>(args)...) {} // NOLINT

  void Wait() const {
    std::visit([](const auto& future) { future.wait(); }, variant_);
  }

  bool Ready() const {
    return std::visit(
        [](const auto& future) {
          if constexpr (std::is_same_v<std::decay_t<decltype(future)>, SimpleFuture>) {
            return IsReady(future);
          } else {
            return future.ready();
          }
        },
        variant_);
  }

  bool Valid() const {
    return std::visit([](const auto& future) { return future.valid(); }, variant_);
  }

  Result Get() {
    const auto& result = std::visit([](auto& future) { return future.get(); }, variant_);

    // Check for interrupts are waiting on async RPC.
    YBCCheckForInterrupts();
    return result;
  }

 private:
  using SimpleFuture = std::future<Result>;
  std::variant<SimpleFuture, ExchangeFuture<Data>> variant_;
};

} // namespace pg_client::internal

using TableKeyRanges = boost::container::small_vector<RefCntSlice, 2>;

using PerformResultFuture = pg_client::internal::ResultFuture<pg_client::internal::PerformData>;

using WaitEventWatcher = std::function<PgWaitEventWatcher(ash::WaitStateCode, ash::PggateRPC)>;

class PgClient {
 public:
  struct ProxyInitInfo {
    rpc::ProxyCache& cache;
    HostPort host_port;
    MonoDelta resolve_cache_timeout;
  };

  PgClient(
      const ProxyInitInfo& proxy_init_info,
      std::reference_wrapper<const WaitEventWatcher> wait_event_watcher,
      std::atomic<uint64_t>& next_perform_op_serial_no);
  ~PgClient();

  Status Start(rpc::Scheduler& scheduler, std::optional<uint64_t> session_id);

  void Interrupt();

  void SetTimeout(int timeout_ms);
  void ClearTimeout();

  void SetLockTimeout(int lock_timeout_ms);

  uint64_t SessionID() const;

  Result<PgTableDescPtr> OpenTable(
      const PgObjectId& table_id, bool reopen, uint64_t min_ysql_catalog_version,
      master::IncludeHidden include_hidden = master::IncludeHidden::kFalse);

  Result<client::VersionedTablePartitionList> GetTablePartitionList(const PgObjectId& table_id);

  Status FinishTransaction(Commit commit, const std::optional<DdlMode>& ddl_mode = {});

  Result<tserver::PgListClonesResponsePB> ListDatabaseClones();

  Result<master::GetNamespaceInfoResponsePB> GetDatabaseInfo(PgOid oid);

  Result<bool> PollVectorIndexReady(const PgObjectId& table_id);

  Result<std::pair<PgOid, PgOid>> ReserveOids(PgOid database_oid, PgOid next_oid, uint32_t count);

  Result<PgOid> GetNewObjectId(PgOid db_oid);

  Result<bool> IsInitDbDone();

  Result<uint64_t> GetCatalogMasterVersion();

  Result<uint32_t> GetXClusterRole(uint32_t db_oid);

  Status CreateSequencesDataTable();

  Result<client::YBTableName> DropTable(
      tserver::PgDropTableRequestPB* req, CoarseTimePoint deadline);

  Result<int> WaitForBackendsCatalogVersion(
      tserver::PgWaitForBackendsCatalogVersionRequestPB* req, CoarseTimePoint deadline);
  Status BackfillIndex(tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline);

  Status GetIndexBackfillProgress(const std::vector<PgObjectId>& index_ids,
                                  uint64_t* num_rows_read_from_table,
                                  double* num_rows_backfilled);

  Result<yb::tserver::PgGetLockStatusResponsePB> GetLockStatusData(
      const std::string& table_id, const std::string& transaction_id);

  Result<int32> TabletServerCount(bool primary_only);

  Result<client::TabletServersInfo> ListLiveTabletServers(bool primary_only);

  Status RollbackToSubTransaction(SubTransactionId id, tserver::PgPerformOptionsPB* options);

  Status ValidatePlacement(tserver::PgValidatePlacementRequestPB* req);

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

  Result<std::pair<int64_t, bool>> ReadSequenceTuple(
      int64_t db_oid, int64_t seq_oid, std::optional<uint64_t> ysql_catalog_version,
      bool is_db_catalog_version_mode, std::optional<uint64_t> read_time = std::nullopt);

  Status DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid);

  Status DeleteDBSequences(int64_t db_oid);

  PerformResultFuture PerformAsync(
      tserver::PgPerformOptionsPB* options, PgsqlOps&& operations, PgDocMetrics& metrics);

  bool TryAcquireObjectLockInSharedMemory(
      SubTransactionId subtxn_id, const YbcObjectLockId& lock_id,
      docdb::ObjectLockFastpathLockType lock_type);

  Status AcquireObjectLock(
      tserver::PgPerformOptionsPB* options, const YbcObjectLockId& lock_id, YbcObjectLockMode mode,
      std::optional<PgTablespaceOid> tablespace_oid);

  Result<bool> CheckIfPitrActive();

  Result<bool> IsObjectPartOfXRepl(const PgObjectId& table_id);

  Result<TableKeyRanges> GetTableKeyRanges(
      const PgObjectId& table_id, Slice lower_bound_key, Slice upper_bound_key,
      uint64_t max_num_ranges, uint64_t range_size_bytes, bool is_forward, uint32_t max_key_length);

  Result<tserver::PgGetTserverCatalogVersionInfoResponsePB> GetTserverCatalogVersionInfo(
      bool size_only, uint32_t db_oid);

  Result<tserver::PgGetTserverCatalogMessageListsResponsePB> GetTserverCatalogMessageLists(
      uint32_t db_oid, uint64_t ysql_catalog_version, uint32_t num_catalog_versions);

  Result<tserver::PgSetTserverCatalogMessageListResponsePB> SetTserverCatalogMessageList(
      uint32_t db_oid, bool is_breaking_change,
      uint64_t new_catalog_version, const std::optional<std::string>& message_list);

  Status TriggerRelcacheInitConnection(const std::string& dbname);

  Result<tserver::PgCreateReplicationSlotResponsePB> CreateReplicationSlot(
      tserver::PgCreateReplicationSlotRequestPB* req, CoarseTimePoint deadline);

  Result<tserver::PgListSlotEntriesResponsePB> ListSlotEntries();

  Result<tserver::PgListReplicationSlotsResponsePB> ListReplicationSlots();

  Result<tserver::PgGetReplicationSlotResponsePB> GetReplicationSlot(
      const ReplicationSlotName& slot_name);

  Result<tserver::PgActiveSessionHistoryResponsePB> ActiveSessionHistory();

  Result<cdc::InitVirtualWALForCDCResponsePB> InitVirtualWALForCDC(
      const std::string& stream_id, const std::vector<PgObjectId>& table_ids,
      const std::unordered_map<uint32_t, uint32_t>& oid_to_relfilenode,
      const YbcReplicationSlotHashRange* slot_hash_range, uint64_t active_pid,
      const std::vector<PgOid>& publication_oids, bool pub_all_tables);

  Result<cdc::GetLagMetricsResponsePB> GetLagMetrics(
      const std::string& stream_id, int64_t* lag_metric);

  Result<cdc::UpdatePublicationTableListResponsePB> UpdatePublicationTableList(
      const std::string& stream_id, const std::vector<PgObjectId>& table_ids,
      const std::unordered_map<uint32_t, uint32_t>& oid_to_relfilenode);

  Result<cdc::DestroyVirtualWALForCDCResponsePB> DestroyVirtualWALForCDC();

  Result<cdc::GetConsistentChangesResponsePB> GetConsistentChangesForCDC(
      const std::string& stream_id);

  Result<cdc::UpdateAndPersistLSNResponsePB> UpdateAndPersistLSN(
      const std::string& stream_id, YbcPgXLogRecPtr restart_lsn, YbcPgXLogRecPtr confirmed_flush);

  Result<tserver::PgTabletsMetadataResponsePB> TabletsMetadata(bool local_only);

  Result<tserver::PgServersMetricsResponsePB> ServersMetrics();

  Status SetCronLastMinute(int64_t last_minute);
  Result<int64_t> GetCronLastMinute();

  Result<std::string> ExportTxnSnapshot(tserver::PgExportTxnSnapshotRequestPB* req);
  Result<PgTxnSnapshotPB> ImportTxnSnapshot(
      std::string_view snapshot_id, tserver::PgPerformOptionsPB&& options);
  Status ClearExportedTxnSnapshots();

  Status GetYbSystemTableInfo(
      PgOid namespace_oid, std::string_view table_name, PgOid* oid, PgOid* relfilenode);

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

  Result<tserver::PgYCQLStatementStatsResponsePB> YCQLStatementStats();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::pggate
