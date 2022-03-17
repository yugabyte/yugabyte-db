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

#ifndef YB_YQL_PGGATE_PG_CLIENT_H
#define YB_YQL_PGGATE_PG_CLIENT_H

#include <memory>
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

#include "yb/util/monotime.h"

#include "yb/yql/pggate/pg_gate_fwd.h"

namespace yb {
namespace pggate {

YB_STRONGLY_TYPED_BOOL(DdlMode);

#define YB_PG_CLIENT_SIMPLE_METHODS \
    (AlterDatabase)(AlterTable)(CreateDatabase)(CreateTable)(CreateTablegroup) \
    (DropDatabase)(DropTablegroup)(TruncateTable)

struct PerformResult {
  Status status;
  ReadHybridTime catalog_read_time;
};

using PerformCallback = std::function<void(const PerformResult&)>;

class PgClient {
 public:
  PgClient();
  ~PgClient();

  CHECKED_STATUS Start(rpc::ProxyCache* proxy_cache,
                       rpc::Scheduler* scheduler,
                       const tserver::TServerSharedObject& tserver_shared_object);
  void Shutdown();

  void SetTimeout(MonoDelta timeout);

  Result<PgTableDescPtr> OpenTable(
      const PgObjectId& table_id, bool reopen, CoarseTimePoint invalidate_cache_time);

  CHECKED_STATUS FinishTransaction(Commit commit, DdlMode ddl_mode);

  Result<master::GetNamespaceInfoResponsePB> GetDatabaseInfo(PgOid oid);

  Result<std::pair<PgOid, PgOid>> ReserveOids(PgOid database_oid, PgOid next_oid, uint32_t count);

  Result<bool> IsInitDbDone();

  Result<uint64_t> GetCatalogMasterVersion();

  CHECKED_STATUS CreateSequencesDataTable();

  Result<client::YBTableName> DropTable(
      tserver::PgDropTableRequestPB* req, CoarseTimePoint deadline);

  CHECKED_STATUS BackfillIndex(tserver::PgBackfillIndexRequestPB* req, CoarseTimePoint deadline);

  Result<int32> TabletServerCount(bool primary_only);

  Result<client::TabletServersInfo> ListLiveTabletServers(bool primary_only);

  CHECKED_STATUS SetActiveSubTransaction(
      SubTransactionId id, tserver::PgPerformOptionsPB* options);
  CHECKED_STATUS RollbackSubTransaction(SubTransactionId id);

  CHECKED_STATUS ValidatePlacement(const tserver::PgValidatePlacementRequestPB* req);

  CHECKED_STATUS InsertSequenceTuple(int64_t db_oid,
                                     int64_t seq_oid,
                                     uint64_t ysql_catalog_version,
                                     int64_t last_val,
                                     bool is_called);

  Result<bool> UpdateSequenceTuple(int64_t db_oid,
                                   int64_t seq_oid,
                                   uint64_t ysql_catalog_version,
                                   int64_t last_val,
                                   bool is_called,
                                   boost::optional<int64_t> expected_last_val,
                                   boost::optional<bool> expected_is_called);

  Result<std::pair<int64_t, bool>> ReadSequenceTuple(int64_t db_oid,
                                                     int64_t seq_oid,
                                                     uint64_t ysql_catalog_version);

  CHECKED_STATUS DeleteSequenceTuple(int64_t db_oid, int64_t seq_oid);

  CHECKED_STATUS DeleteDBSequences(int64_t db_oid);

  void PerformAsync(
      tserver::PgPerformOptionsPB* options,
      PgsqlOps* operations,
      const PerformCallback& callback);

#define YB_PG_CLIENT_SIMPLE_METHOD_DECLARE(r, data, method) \
  CHECKED_STATUS method(                             \
      tserver::BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
      CoarseTimePoint deadline);

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_SIMPLE_METHOD_DECLARE, ~, YB_PG_CLIENT_SIMPLE_METHODS);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_CLIENT_H
